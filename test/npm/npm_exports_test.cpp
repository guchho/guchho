// Tests for the "exports" and "imports" maps of a package.json as they behave
// during resolution: which key of a map wins, which condition is consulted, and
// what the resolver does with a subpath or a target that is not there.
//
// The map parser itself is covered by test/resolver/package_json_test.cpp. What
// is under test here is the part of the algorithm that needs a file system
// around it, so every test resolves a real specifier against a mock file system
// and reads back the file the resolver chose.

#include "test/guchho_test.hpp"
#include "test/helpers/filesystem_test.hpp"

#include "guchho/bundler.hpp"
#include "guchho/cache.hpp"
#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bundler    = guchho::bundler;
namespace cache      = guchho::cache;
namespace compiler   = guchho::compiler;
namespace config     = guchho::config;
namespace filesystem = guchho::filesystem;
namespace logger     = guchho::logger;
namespace resolver   = guchho::resolver;

namespace {

// The extension order guchho itself uses when nothing overrides it.
const std::vector<std::string> kDefaultExtensions = {
    ".tsx", ".ts", ".jsx", ".js", ".css", ".json"
};

// A resolver transaction wired to a mock file system and configured through the
// same factory production code uses, so the ESM condition maps are built exactly
// as they are during a build.
struct NpmEnv {
    std::unordered_map<std::string, std::string> files;
    config::Options                             options;
    std::unique_ptr<filesystem::Fs>              fs;
    logger::Log                                 log;
    std::unique_ptr<cache::CacheSet>            caches;
    std::unique_ptr<resolver::Resolver>         res;
};

NpmEnv MakeEnv(std::unordered_map<std::string, std::string> files,
               const std::string&                            cwd,
               config::Options                               options = {})
{
    if (options.ExtensionOrder.empty()) {
        options.ExtensionOrder = kDefaultExtensions;
    }
    bundler::ApplyOptionDefaults(options);

    NpmEnv env;
    env.files   = std::move(files);
    env.options = options;
    env.fs      = guchho::test::MakeMockFS(
            env.files, filesystem::MockKind::kUnix, cwd);
    env.log    = logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll, {});
    env.caches = cache::MakeCacheSet();
    env.res    = resolver::NewResolver(config::APICall::kBuildCall,
                                       *env.fs, env.log, *env.caches, &env.options);
    return env;
}

// Resolves "specifier" from "source_dir" and returns the file it names, or an
// empty string when the resolver produced no answer at all.
std::string ResolveTo(NpmEnv& env,
                      const std::string& source_dir,
                      const std::string& specifier,
                      compiler::ImportKind kind = compiler::ImportKind::kStmt)
{
    resolver::DebugMeta debug_meta;
    auto result = env.res->Resolve(source_dir, specifier, kind, &debug_meta);
    if (!result.has_value()) {
        return std::string();
    }
    return result->path_pair.primary.text;
}

bool Resolves(NpmEnv& env,
              const std::string& source_dir,
              const std::string& specifier,
              compiler::ImportKind kind = compiler::ImportKind::kStmt)
{
    resolver::DebugMeta debug_meta;
    return env.res->Resolve(source_dir, specifier, kind, &debug_meta).has_value();
}

// A package whose "exports" is the given map body, with every subpath target
// also present on disk as an empty module so a failure means the map was read
// wrongly rather than that a file was missing.
std::unordered_map<std::string, std::string> PackageWithExports(
        const std::string& exports_body,
        const std::vector<std::string>&        subpaths = {})
{
    std::unordered_map<std::string, std::string> files = {
        {"/app/node_modules/pkg/package.json",
            std::string("{\"name\":\"pkg\",\"exports\":{") + exports_body + "}}"},
        {"/app/node_modules/pkg/index.js", "export const which = 'index';\n"},
    };
    for (const std::string& subpath : subpaths) {
        files["/app/node_modules/pkg/" + subpath + ".js"] =
                "export const which = '" + subpath + "';\n";
    }
    return files;
}

} // namespace

// ---------------------------------------------------------------------------
// Which key wins
// ---------------------------------------------------------------------------

TEST(NpmExports, ExactSubpathWins)
{
    // An exact key beats a pattern that also matches, whatever order the two
    // are written in, so a package can carve out one specific file.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","exports":{)"
            R"("./feature/*":"./wild/*.js","./feature/special":"./exact.js"}})"},
        {"/app/node_modules/pkg/exact.js", "export const which = 'exact';\n"},
        {"/app/node_modules/pkg/wild/special.js", "export const which = 'wild';\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app", "pkg/feature/special"),
              std::string("/app/node_modules/pkg/exact.js"));
}

TEST(NpmExports, PatternSubpath)
{
    // With no exact key for the subpath, the pattern captures the segment and
    // substitutes it into the target.
    NpmEnv env = MakeEnv(PackageWithExports(
        R"("./feature/*":"./wild/*.js")", {"wild/other"}), "/app");

    EXPECT_EQ(ResolveTo(env, "/app", "pkg/feature/other"),
              std::string("/app/node_modules/pkg/wild/other.js"));
}

TEST(NpmExports, AlternationArray)
{
    // An array of targets is walked in order and the first one that resolves to
    // a file that exists wins, which is how a package offers a preferred build
    // with a fallback behind it.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","exports":{".":["./missing.js","./fallback.js"]}})"},
        {"/app/node_modules/pkg/fallback.js", "export const which = 'fallback';\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app", "pkg"),
              std::string("/app/node_modules/pkg/fallback.js"));
}

TEST(NpmExports, DefaultConditionFallback)
{
    // "default" is consulted after the more specific conditions, so it is the
    // answer whenever nothing else matches.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","exports":{".":{"node":"./node.js","default":"./generic.js"}}})"},
        {"/app/node_modules/pkg/node.js", "export const which = 'node';\n"},
        {"/app/node_modules/pkg/generic.js", "export const which = 'generic';\n"},
    }, "/app", {.OutputPlatform = config::Platform::kNode});

    EXPECT_EQ(ResolveTo(env, "/app", "pkg"),
              std::string("/app/node_modules/pkg/node.js"));
}

// ---------------------------------------------------------------------------
// Conditions
// ---------------------------------------------------------------------------

TEST(NpmExports, ImportVsRequireCondition)
{
    // "import" and "require" are the two conditions that differ between an
    // import statement and a require call, so the same package can ship one
    // file per syntax without the build having to guess.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","exports":{".":{"import":"./esm.js","require":"./cjs.js"}}})"},
        {"/app/node_modules/pkg/esm.js", "export const which = 'esm';\n"},
        {"/app/node_modules/pkg/cjs.js", "export const which = 'cjs';\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app", "pkg", compiler::ImportKind::kStmt),
              std::string("/app/node_modules/pkg/esm.js"));
    EXPECT_EQ(ResolveTo(env, "/app", "pkg", compiler::ImportKind::kRequire),
              std::string("/app/node_modules/pkg/cjs.js"));
}

TEST(NpmExports, BrowserConditionOnlyOnBrowserPlatform)
{
    // "browser" is added to the condition set by the output platform, so the
    // same package resolves to a different file for a browser build than for a
    // node one.
    std::unordered_map<std::string, std::string> files = {
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","exports":{".":{"browser":"./browser.js","default":"./generic.js"}}})"},
        {"/app/node_modules/pkg/browser.js", "export const which = 'browser';\n"},
        {"/app/node_modules/pkg/generic.js", "export const which = 'generic';\n"},
    };

    NpmEnv browser = MakeEnv(files, "/app",
                            {.OutputPlatform = config::Platform::kBrowser});
    EXPECT_EQ(ResolveTo(browser, "/app", "pkg"),
              std::string("/app/node_modules/pkg/browser.js"));

    NpmEnv node = MakeEnv(files, "/app",
                          {.OutputPlatform = config::Platform::kNode});
    EXPECT_EQ(ResolveTo(node, "/app", "pkg"),
              std::string("/app/node_modules/pkg/generic.js"));
}

TEST(NpmExports, NodeConditionOnlyOnNodePlatform)
{
    std::unordered_map<std::string, std::string> files = {
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","exports":{".":{"node":"./node.js","default":"./generic.js"}}})"},
        {"/app/node_modules/pkg/node.js", "export const which = 'node';\n"},
        {"/app/node_modules/pkg/generic.js", "export const which = 'generic';\n"},
    };

    NpmEnv node = MakeEnv(files, "/app",
                          {.OutputPlatform = config::Platform::kNode});
    EXPECT_EQ(ResolveTo(node, "/app", "pkg"),
              std::string("/app/node_modules/pkg/node.js"));

    NpmEnv browser = MakeEnv(files, "/app",
                             {.OutputPlatform = config::Platform::kBrowser});
    EXPECT_EQ(ResolveTo(browser, "/app", "pkg"),
              std::string("/app/node_modules/pkg/generic.js"));
}

TEST(NpmExports, CustomConditionFromOptions)
{
    // A condition named on the command line joins the set for every one of
    // default, import and require, which is how a project selects a build a
    // package offers without editing the package.
    config::Options options;
    options.Conditions = {"production"};

    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","exports":{".":{"production":"./prod.js","default":"./dev.js"}}})"},
        {"/app/node_modules/pkg/prod.js", "export const which = 'prod';\n"},
        {"/app/node_modules/pkg/dev.js", "export const which = 'dev';\n"},
    }, "/app", options);

    EXPECT_EQ(ResolveTo(env, "/app", "pkg"),
              std::string("/app/node_modules/pkg/prod.js"));
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

TEST(NpmExports, NotExportedSubpathFails)
{
    // The map has keys, so a subpath outside them is not part of the package's
    // public surface even though the file exists on disk.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json", R"({"name":"pkg","exports":{".":"./index.js"}})"},
        {"/app/node_modules/pkg/index.js", "export const x = 1;\n"},
        {"/app/node_modules/pkg/private.js", "export const y = 2;\n"},
    }, "/app");

    EXPECT_FALSE(Resolves(env, "/app", "pkg/private"));
}

TEST(NpmExports, MissingTargetFileFails)
{
    // A key can be exported and still name a file that is not there. That is a
    // broken package rather than a wrong subpath, and either way there is no
    // answer to return.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json", R"({"name":"pkg","exports":{".":"./gone.js"}})"},
    }, "/app");

    EXPECT_FALSE(Resolves(env, "/app", "pkg"));
}

TEST(NpmExports, ImportsWinOverHoistedPackage)
{
    // "imports" is consulted before the node_modules walk, so a package can
    // point one of its own names at a different package entirely.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/app/package.json",
            R"({"name":"app","imports":{"#dep":"./vendor.js"}})"},
        {"/app/node_modules/app/vendor.js", "export const which = 'vendor';\n"},
        {"/app/node_modules/dep/package.json", R"({"name":"dep","main":"./index.js"})"},
        {"/app/node_modules/dep/index.js", "export const which = 'dep';\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/node_modules/app", "#dep"),
              std::string("/app/node_modules/app/vendor.js"));
}

TEST(NpmExports, ImportsMapResolvesHashSpecifier)
{
    NpmEnv env = MakeEnv({
        {"/app/node_modules/app/package.json",
            R"({"name":"app","imports":{"#internal/*":"./src/*.js"}})"},
        {"/app/node_modules/app/src/util.js", "export const x = 1;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/node_modules/app", "#internal/util"),
              std::string("/app/node_modules/app/src/util.js"));
}

TEST(NpmExports, ExportsMapHidesTsconfigInsideNodeModules)
{
    // A package's own tsconfig is never consulted: the enclosing project config
    // is the only one that applies, which is why a stray config shipped inside
    // node_modules cannot change how its neighbours resolve.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json", R"({"name":"pkg","main":"./index.js"})"},
        {"/app/node_modules/pkg/tsconfig.json", R"({"compilerOptions":{"baseUrl":"./src"}})"},
        {"/app/node_modules/pkg/index.js", "export const x = 1;\n"},
    }, "/app");

    // "target" is only meaningful through a path mapping, and there is none, so
    // the config being ignored is what makes this fail.
    EXPECT_FALSE(Resolves(env, "/app/node_modules/pkg", "target"));
}

TEST(NpmExports, QuerySuffixRetryKeepsIgnoredSuffix)
{
    // A specifier carrying a query string resolves with the suffix stripped, so
    // a cache-busting import still finds the file it is asking about.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json", R"({"name":"pkg","main":"./index.js"})"},
        {"/app/node_modules/pkg/index.js", "export const x = 1;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app", "pkg?raw"),
              std::string("/app/node_modules/pkg/index.js"));
}

TEST(NpmExports, ProbeResolvePackageAsRelativeFindsDotSlash)
{
    // The recovery path for a specifier that names a real file in the project
    // but was written without the leading "./", which is the mistake this probe
    // exists to catch.
    std::unordered_map<std::string, std::string> files = {
        {"/app/util.js", "export const x = 1;\n"},
    };
    NpmEnv env = MakeEnv(files, "/app");

    resolver::DebugMeta debug_meta;
    auto probed = env.res->ProbeResolvePackageAsRelative(
            "/app", "util.js", compiler::ImportKind::kStmt, &debug_meta);
    ASSERT_TRUE(probed.has_value());
    EXPECT_EQ(probed->path_pair.primary.text, std::string("/app/util.js"));
}
