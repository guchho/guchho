// Tests for Yarn Plug'n'Play manifest handling (yarnpnp.cpp).
//
// Every test drives the compiled PnP tables directly: a manifest is parsed
// from JSON text, flattened by CompileYarnPnPData into resolver::PnpData, and
// then queried through ResolverQuery for resolution, ownership, fallback, and
// package lookups. The file system only fakes the relative-path operation the
// ownership walk needs, so nothing touches the real disk.

#include "test/guchho_test.hpp"

#include "guchho/cache.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace filesystem = guchho::filesystem;
namespace logger     = guchho::logger;
namespace cache      = guchho::cache;
namespace resolver   = guchho::resolver;
namespace javascript = guchho::javascript;

namespace {

// A minimal in-memory file system. The PnP code paths under test only ask
// for relative-path computation (the ownership walk inside FindLocator), so
// that is the only operation meaningfully faked; the rest of the Fs surface
// keeps the interface satisfied with trivially failing defaults.
class TestFs : public filesystem::Fs {
public:
    bool IsAbs(std::string_view path) override
    {
        return !path.empty() && path.front() == '/';
    }

    static std::vector<std::string_view> Split(std::string_view path, char sep)
    {
        std::vector<std::string_view> parts;
        size_t                        start = 0;
        while (true) {
            size_t pos = path.find(sep, start);
            if (pos == std::string_view::npos) {
                parts.push_back(path.substr(start));
                break;
            }
            parts.push_back(path.substr(start, pos - start));
            start = pos + 1;
        }
        return parts;
    }

    std::optional<std::string> Rel(std::string_view base, std::string_view target) override
    {
        std::vector<std::string_view> a = Split(base, '/');
        std::vector<std::string_view> b = Split(target, '/');
        size_t                        common = 0;
        while (common < a.size() && common < b.size() && a[common] == b[common]) {
            ++common;
        }
        std::string out;
        for (size_t i = common; i < a.size(); ++i) {
            out += out.empty() ? ".." : "/..";
        }
        for (size_t i = common; i < b.size(); ++i) {
            out += out.empty() ? "" : "/";
            out += b[i];
        }
        return out.empty() ? std::optional<std::string>(std::string("."))
                           : std::optional<std::string>(out);
    }

    filesystem::FsResult<filesystem::DirEntries> ReadDirectory(const std::string&) override
    {
        return {};
    }

    filesystem::FsResult<std::string> ReadFile(const std::string&) override
    {
        return {};
    }

    filesystem::FsResult<std::shared_ptr<filesystem::OpenedFile>> OpenFile(const std::string&) override
    {
        return {};
    }

    filesystem::ModKeyResult ModKey(const std::string&) override
    {
        return {};
    }

    std::optional<std::string> Abs(std::string_view path) override
    {
        if (IsAbs(path)) {
            return std::string(path);
        }
        return "/" + std::string(path);
    }

    std::string Dir(std::string_view path) override
    {
        size_t pos = path.rfind('/');
        return pos == std::string_view::npos ? "/" : std::string(path.substr(0, pos));
    }

    std::string Base(std::string_view path) override
    {
        size_t pos = path.rfind('/');
        return pos == std::string_view::npos ? std::string(path)
                                             : std::string(path.substr(pos + 1));
    }

    std::string Ext(std::string_view path) override
    {
        std::string base = Base(path);
        size_t      pos  = base.rfind('.');
        return pos == std::string::npos ? "" : base.substr(pos);
    }

    std::string Join(std::initializer_list<std::string_view>) override
    {
        return "/";
    }

    std::string Cwd() override
    {
        return "/";
    }

    std::optional<std::string> EvalSymlinks(std::string_view path) override
    {
        return std::string(path);
    }

    std::pair<std::string, filesystem::EntryKind> Kind(std::string_view, std::string_view) override
    {
        return {};
    }

    filesystem::WatchData GetWatchData() override
    {
        return {};
    }
};

// Builds a Source carrying "contents" as the text of the file at "path".
logger::Source SourceForTest(const std::string& path, const std::string& contents)
{
    logger::Source source;
    source.key_path.text    = path;
    source.pretty_paths.abs = path;
    source.pretty_paths.rel = path;
    source.identifier_name  = path;
    source.contents         = contents;
    return source;
}

// Compiles "json_text" into a PnpData via the shared JSON cache. The parsed
// source is kept so the manifest keeps a valid line/column tracker.
struct CompiledManifest {
    logger::Source source;
    std::unique_ptr<resolver::PnpData> data;
    bool ok = false;
};

CompiledManifest CompileManifest(logger::Log& log, const std::string& json_text,
                                 const std::string& abs_dir = "/app",
                                 const std::string& abs_path = "")
{
    cache::JSONCache json_cache;
    std::string  file   = abs_path.empty() ? abs_dir + "/.pnp.data.json" : abs_path;
    logger::Source source = SourceForTest(file, json_text);
    auto [expr, ok] = json_cache.Parse(log, source, javascript::JSONOptions{});

    CompiledManifest result;
    result.ok     = ok;
    result.source = source;
    if (ok) {
        result.data = resolver::CompileYarnPnPData(file, abs_dir, expr, source);
    }
    return result;
}

// A resolver transaction wired to a fake file system and fresh caches,
// mirroring the setup the bundler creates around a ResolverQuery.
struct YarnEnv {
    TestFs                                      fs_;
    logger::Log                                 log_{logger::NewDeferLog(
            logger::DeferLogKind::kDeferLogAll, {})};
    std::unique_ptr<cache::CacheSet>            caches_{cache::MakeCacheSet()};
    resolver::Resolver                          res_{fs_, log_, *caches_};
    resolver::ResolverQuery                     query_{&res_};
};

// A manifest exercising the common resolution paths: the anonymous top-level
// package (null ident), a plain dependency, an aliased dependency, an
// unfulfilled peer dependency, top-level fallback, the fallback pool, and an
// excluded parent package.
const char* kBaseManifest = R"({
    "enableTopLevelFallback": true,
    "fallbackExclusionList": [["pkgA", ["npm:1.0.0"]]],
    "fallbackPool": [["react", "npm:18.2.0"]],
    "packageRegistryData": [
        [null, [[null, {
            "packageLocation": "./",
            "packageDependencies": [
                ["lodash", "npm:4.17.21"],
                ["tslib", "npm:2.6.2"],
                ["chalk", null],
                ["@my/fetch", ["whatwg-fetch", "npm:3.6.2"]]
            ]
        }]]],
        ["lodash", [["npm:4.17.21", {
            "packageLocation": "./.yarn/cache/lodash-npm-4.17.21/node_modules/lodash/",
            "packageDependencies": []
        }]]],
        ["tslib", [["npm:2.6.2", {
            "packageLocation": "./.yarn/cache/tslib-npm-2.6.2/node_modules/tslib/",
            "packageDependencies": []
        }]]],
        ["whatwg-fetch", [["npm:3.6.2", {
            "packageLocation": "./.yarn/cache/whatwg-fetch-npm-3.6.2/node_modules/whatwg-fetch/",
            "packageDependencies": []
        }]]],
        ["react", [["npm:18.2.0", {
            "packageLocation": "./.yarn/cache/react-npm-18.2.0/node_modules/react/",
            "packageDependencies": []
        }]]],
        ["pkgA", [["npm:1.0.0", {
            "packageLocation": "./packages/a/",
            "packageDependencies": []
        }]]],
        ["pkgB", [["npm:1.0.0", {
            "packageLocation": "./packages/b/",
            "packageDependencies": []
        }]]]
    ]
})";

// Compiles the canonical manifest on a fresh transaction. The fixture cannot
// be returned by value because the resolver owns a mutex, so tests declare it
// in place and call this to fill it in.
struct BaseFixture {
    YarnEnv              env;
    CompiledManifest     manifest;
    resolver::PnpData*   data = nullptr;
};

void InitBaseFixture(BaseFixture& fixture)
{
    fixture.manifest = CompileManifest(fixture.env.log_, kBaseManifest);
    EXPECT_TRUE(fixture.manifest.ok);
    if (fixture.manifest.data == nullptr) {
        return;
    }
    fixture.data = fixture.manifest.data.get();
}

// The path Yarn would record for "lodash" under the canonical manifest.
const char* kLodashDir = "/app/.yarn/cache/lodash-npm-4.17.21/node_modules/lodash";
const char* kReactDir  = "/app/.yarn/cache/react-npm-18.2.0/node_modules/react";

} // namespace

// ---------------------------------------------------------------------------
// Compilation
// ---------------------------------------------------------------------------

TEST(YarnPnP, CompilePopulatesAllTables)
{
    YarnEnv env;
    CompiledManifest compiled = CompileManifest(env.log_, kBaseManifest);
    ASSERT_TRUE(compiled.ok);
    ASSERT_TRUE(compiled.data != nullptr);
    resolver::PnpData& data = *compiled.data;

    EXPECT_EQ(data.abs_path, std::string("/app/.pnp.data.json"));
    EXPECT_EQ(data.abs_dir_path, std::string("/app"));
    EXPECT_TRUE(data.enable_top_level_fallback);

    // "fallbackExclusionList" indexed the excluded ref for pkgA.
    auto excluded = data.fallback_exclusion_list.find("pkgA");
    ASSERT_TRUE(excluded != data.fallback_exclusion_list.end());
    EXPECT_TRUE(excluded->second.count("npm:1.0.0"));

    // "fallbackPool" indexed react's target.
    auto pool = data.fallback_pool.find("react");
    ASSERT_TRUE(pool != data.fallback_pool.end());
    EXPECT_EQ(pool->second.reference, std::string("npm:18.2.0"));

    // The registry holds the top-level package plus each installed package.
    ASSERT_TRUE(data.package_registry_data.count("") == 1);
    ASSERT_TRUE(data.package_registry_data.count("lodash") == 1);
    EXPECT_EQ(data.package_registry_data["lodash"]["npm:4.17.21"].package_location,
              std::string("./.yarn/cache/lodash-npm-4.17.21/node_modules/lodash/"));

    // The top-level package's dependency table was flattened.
    auto top_deps = data.package_registry_data[""][""].package_dependencies;
    ASSERT_TRUE(top_deps.count("lodash") == 1);
    EXPECT_EQ(top_deps["lodash"].reference, std::string("npm:4.17.21"));
    EXPECT_TRUE(top_deps["chalk"].reference.empty());
}

TEST(YarnPnP, CompileSkipsMalformedRows)
{
    YarnEnv env;
    CompiledManifest compiled = CompileManifest(env.log_, R"({
        "fallbackExclusionList": [["only-ident"]],
        "fallbackPool": [["lonely"], ["bad", {"not": "an array"}]],
        "packageRegistryData": [
            ["short"],
            ["pkg", [["npm:1.0.0", {
                "packageLocation": "./pkg/",
                "packageDependencies": "not-an-array"
            }]]]
        ]
    })");
    ASSERT_TRUE(compiled.ok);
    ASSERT_TRUE(compiled.data != nullptr);

    // Every malformed row was skipped. The "pkg" bucket is pre-created by the
    // compiler before its (invalid) reference rows are walked, so it exists
    // but is empty; the other broken rows never even get a bucket.
    EXPECT_TRUE(compiled.data->fallback_exclusion_list.empty());
    EXPECT_TRUE(compiled.data->fallback_pool.empty());
    ASSERT_TRUE(compiled.data->package_registry_data.count("pkg") == 1);
    EXPECT_TRUE(compiled.data->package_registry_data["pkg"].empty());
    EXPECT_TRUE(compiled.data->package_locators_by_locations.empty());
}

TEST(YarnPnP, InvalidIgnorePatternIsStoredVerbatim)
{
    YarnEnv env;
    CompiledManifest compiled = CompileManifest(env.log_, R"({
        "ignorePatternData": "(",
        "packageRegistryData": []
    })");
    ASSERT_TRUE(compiled.ok);
    ASSERT_TRUE(compiled.data != nullptr);
    EXPECT_TRUE(compiled.data->ignore_pattern_data == nullptr);
    EXPECT_EQ(compiled.data->invalid_ignore_pattern_data, std::string("("));
}

TEST(YarnPnP, LookAheadGuardsAreStrippedFromIgnorePattern)
{
    YarnEnv env;
    CompiledManifest compiled = CompileManifest(env.log_, R"({
        "ignorePatternData": "(?!\\.)(?!\\.{1,2}(?:/|$))virtual",
        "packageRegistryData": []
    })");
    ASSERT_TRUE(compiled.ok);
    ASSERT_TRUE(compiled.data != nullptr);
    EXPECT_TRUE(compiled.data->invalid_ignore_pattern_data.empty());
    ASSERT_TRUE(compiled.data->ignore_pattern_data != nullptr);
    EXPECT_TRUE(std::regex_search("./virtual/thing.js", *compiled.data->ignore_pattern_data));
    EXPECT_FALSE(std::regex_search("./real/thing.js", *compiled.data->ignore_pattern_data));
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

TEST(YarnPnP, ResolveBarePackage)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "lodash", "/app/src/main.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kSuccess);
    EXPECT_EQ(result.pkg_dir_path, std::string(kLodashDir));
    EXPECT_EQ(result.pkg_ident, std::string("lodash"));
    EXPECT_TRUE(result.pkg_subpath.empty());
}

TEST(YarnPnP, ResolveWithSubpath)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "lodash/merge", "/app/src/main.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kSuccess);
    EXPECT_EQ(result.pkg_ident, std::string("lodash"));
    EXPECT_EQ(result.pkg_subpath, std::string("/merge"));
}

TEST(YarnPnP, ResolveScopedPackage)
{
    YarnEnv env;
    CompiledManifest compiled = CompileManifest(env.log_, R"({
        "packageRegistryData": [
            [null, [[null, {
                "packageLocation": "./",
                "packageDependencies": [["@scope/pkg", "npm:1.0.0"]]
            }]]],
            ["@scope/pkg", [["npm:1.0.0", {
                "packageLocation": "./.yarn/cache/scoped-pkg/node_modules/@scope/pkg/",
                "packageDependencies": []
            }]]]
        ]
    })");
    ASSERT_TRUE(compiled.data != nullptr);

    resolver::PnpResult result = env.query_.ResolveToUnqualified(
        "@scope/pkg/deep/mod.js", "/app/src/main.js", compiled.data.get());
    EXPECT_EQ(result.status, resolver::PnpStatus::kSuccess);
    EXPECT_EQ(result.pkg_ident, std::string("@scope/pkg"));
    EXPECT_EQ(result.pkg_subpath, std::string("/deep/mod.js"));
    EXPECT_EQ(result.pkg_dir_path,
              std::string("/app/.yarn/cache/scoped-pkg/node_modules/@scope/pkg"));
}

TEST(YarnPnP, ResolveAlias)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "@my/fetch", "/app/src/main.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kSuccess);
    // The requested ident is preserved even though the on-disk package is
    // the aliased "whatwg-fetch".
    EXPECT_EQ(result.pkg_ident, std::string("@my/fetch"));
    EXPECT_EQ(result.pkg_dir_path,
              std::string("/app/.yarn/cache/whatwg-fetch-npm-3.6.2/node_modules/whatwg-fetch"));
}

TEST(YarnPnP, ResolveMalformedScopedSpecifierFails)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "@bare-scope", "/app/src/main.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kErrorGeneric);
}

TEST(YarnPnP, ResolveSkippedWhenImporterIsUnowned)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "lodash", "/tmp/unrelated.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kSkipped);
}

TEST(YarnPnP, ResolveDependencyNotFound)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "missing", "/app/src/main.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kErrorDependencyNotFound);
    EXPECT_EQ(result.error_ident, std::string("missing"));
    EXPECT_TRUE(resolver::PnpStatusIsError(result.status));
}

TEST(YarnPnP, ResolveUnfulfilledPeerDependency)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "chalk", "/app/src/main.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kErrorUnfulfilledPeerDependency);
    EXPECT_EQ(result.error_ident, std::string("chalk"));
}

TEST(YarnPnP, ResolveFallbackViaTopLevelDependencies)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    // "pkgB" never lists "tslib"; the top-level package supplies it.
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "tslib", "/app/packages/b/x.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kSuccess);
    EXPECT_EQ(result.pkg_ident, std::string("tslib"));
    EXPECT_EQ(result.pkg_dir_path, std::string("/app/.yarn/cache/tslib-npm-2.6.2/node_modules/tslib"));
}

TEST(YarnPnP, ResolveFallbackViaPool)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    // "pkgB" never lists "react" and the top-level package does not either;
    // the "fallbackPool" entry is what resolves it.
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "react", "/app/packages/b/x.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kSuccess);
    EXPECT_EQ(result.pkg_ident, std::string("react"));
    EXPECT_EQ(result.pkg_dir_path, std::string(kReactDir));
}

TEST(YarnPnP, ResolveFallbackBlockedByExclusion)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    // "pkgA" sits in "fallbackExclusionList", so the top-level fallback must
    // not supply "react" for it.
    resolver::PnpResult result = fx.env.query_.ResolveToUnqualified(
        "react", "/app/packages/a/x.js", fx.data);
    EXPECT_EQ(result.status, resolver::PnpStatus::kErrorDependencyNotFound);
    EXPECT_EQ(result.error_ident, std::string("react"));
}

// ---------------------------------------------------------------------------
// Windows path handling
// ---------------------------------------------------------------------------

TEST(YarnPnP, ResolveOnWindowsDrive)
{
    YarnEnv env;
    CompiledManifest compiled = CompileManifest(
        env.log_,
        R"({
            "packageRegistryData": [
                [null, [[null, {
                    "packageLocation": "./",
                    "packageDependencies": [["lodash", "npm:4.17.21"]]
                }]]],
                ["lodash", [["npm:4.17.21", {
                    "packageLocation": "./.yarn/cache/lodash-npm-4.17.21/node_modules/lodash/",
                    "packageDependencies": []
                }]]]
            ]
        })",
        "C:/app", "C:/app/.pnp.data.json");
    ASSERT_TRUE(compiled.data != nullptr);

    resolver::PnpResult result = env.query_.ResolveToUnqualified(
        "lodash", "C:/app/src/main.js", compiled.data.get());
    EXPECT_EQ(result.status, resolver::PnpStatus::kSuccess);
    EXPECT_EQ(result.pkg_dir_path,
              std::string("C:/app/.yarn/cache/lodash-npm-4.17.21/node_modules/lodash"));
}

// ---------------------------------------------------------------------------
// Ownership (FindLocator)
// ---------------------------------------------------------------------------

TEST(YarnPnP, FindLocatorTakesDeepestOwner)
{
    YarnEnv env;
    CompiledManifest compiled = CompileManifest(env.log_, R"({
        "packageRegistryData": [
            [null, [[null, {
                "packageLocation": "./",
                "packageDependencies": []
            }]]],
            ["a", [["npm:1.0.0", {
                "packageLocation": "./node_modules/a/",
                "packageDependencies": []
            }]]],
            ["b", [["npm:1.0.0", {
                "packageLocation": "./node_modules/a/b/",
                "packageDependencies": []
            }]]]
        ]
    })");
    ASSERT_TRUE(compiled.data != nullptr);

    resolver::LocatorResult result = env.query_.FindLocator(
        compiled.data.get(), "/app/node_modules/a/b/index.js");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.locator.ident, std::string("b"));
    EXPECT_EQ(result.locator.reference, std::string("npm:1.0.0"));
}

TEST(YarnPnP, FindLocatorSkipsDiscardedPackages)
{
    YarnEnv env;
    CompiledManifest compiled = CompileManifest(env.log_, R"({
        "packageRegistryData": [
            [null, [[null, {
                "packageLocation": "./",
                "packageDependencies": []
            }]]],
            ["a", [["npm:1.0.0", {
                "packageLocation": "./node_modules/a/",
                "packageDependencies": []
            }]]],
            ["b", [["npm:1.0.0", {
                "packageLocation": "./node_modules/a/b/",
                "packageDependencies": [],
                "discardFromLookup": true
            }]]]
        ]
    })");
    ASSERT_TRUE(compiled.data != nullptr);

    // "b" is on disk but flagged discardFromLookup, so the walk keeps going
    // up and lands on "a".
    resolver::LocatorResult result = env.query_.FindLocator(
        compiled.data.get(), "/app/node_modules/a/b/index.js");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.locator.ident, std::string("a"));
}

TEST(YarnPnP, FindLocatorHonoursIgnorePattern)
{
    YarnEnv env;
    CompiledManifest compiled = CompileManifest(env.log_, R"({
        "ignorePatternData": "virtual",
        "packageRegistryData": [
            [null, [[null, {
                "packageLocation": "./",
                "packageDependencies": []
            }]]]
        ]
    })");
    ASSERT_TRUE(compiled.data != nullptr);

    // Paths matching the ignore pattern are deliberately unowned.
    resolver::LocatorResult result = env.query_.FindLocator(
        compiled.data.get(), "/app/.yarn/__virtual__/x-virtual-abc123/node_modules/foo/index.js");
    EXPECT_FALSE(result.ok);
}

TEST(YarnPnP, FindLocatorLetsTopLevelOwnProjectFiles)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::LocatorResult result =
        fx.env.query_.FindLocator(fx.data, "/app/src/deep/nested.js");
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.locator.ident.empty());
    EXPECT_TRUE(result.locator.reference.empty());
}

TEST(YarnPnP, FindLocatorExternalFileIsUnowned)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::LocatorResult result = fx.env.query_.FindLocator(fx.data, "/tmp/other.js");
    EXPECT_FALSE(result.ok);
}

// ---------------------------------------------------------------------------
// Lookups used by resolution
// ---------------------------------------------------------------------------

TEST(YarnPnP, GetPackageFindsRegisteredLocator)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::PackageResult result = fx.env.query_.GetPackage(fx.data, "lodash", "npm:4.17.21");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.pkg.package_location,
              std::string("./.yarn/cache/lodash-npm-4.17.21/node_modules/lodash/"));
}

TEST(YarnPnP, GetPackageMissesUnknownLocator)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    EXPECT_FALSE(fx.env.query_.GetPackage(fx.data, "lodash", "npm:9.9.9").ok);
    EXPECT_FALSE(fx.env.query_.GetPackage(fx.data, "unknown", "npm:1.0.0").ok);
}

TEST(YarnPnP, ResolveViaFallbackPrefersTopLevelDependencies)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    // "tslib" is a direct dependency of the top-level package, so it wins
    // over the pool even though both could supply it.
    resolver::LocatorResult result = fx.env.query_.ResolveViaFallback(fx.data, "tslib");
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.locator.ident.empty());
    EXPECT_EQ(result.locator.reference, std::string("npm:2.6.2"));
}

TEST(YarnPnP, ResolveViaFallbackFallsBackToPool)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::LocatorResult result = fx.env.query_.ResolveViaFallback(fx.data, "react");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.locator.reference, std::string("npm:18.2.0"));
}

TEST(YarnPnP, ResolveViaFallbackMissReturnsEmpty)
{
    BaseFixture fx;
    InitBaseFixture(fx);
    resolver::LocatorResult result = fx.env.query_.ResolveViaFallback(fx.data, "gone");
    EXPECT_FALSE(result.ok);
}

TEST(YarnPnP, StatusPredicate)
{
    EXPECT_TRUE(resolver::PnpStatusIsError(resolver::PnpStatus::kErrorGeneric));
    EXPECT_TRUE(resolver::PnpStatusIsError(resolver::PnpStatus::kErrorDependencyNotFound));
    EXPECT_TRUE(resolver::PnpStatusIsError(resolver::PnpStatus::kErrorUnfulfilledPeerDependency));
    EXPECT_FALSE(resolver::PnpStatusIsError(resolver::PnpStatus::kSuccess));
    EXPECT_FALSE(resolver::PnpStatusIsError(resolver::PnpStatus::kSkipped));
}