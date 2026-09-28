// Tests for how a bare specifier finds a package in node_modules: the walk up
// the directory tree, the conditions under which it stops, scoped names, the
// implicit-extension order used inside a package, the extra search roots, and
// the glob entry point.
//
// Every test here goes through Resolver::Resolve against a mock file system, so
// what is under test is the walk itself rather than the bundler that would
// otherwise reach it as a side effect. The "exports"/"imports" maps, the
// package.json fields and symlink handling each have their own file.

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
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bundler    = guchho::bundler;
namespace cache      = guchho::cache;
namespace compiler   = guchho::compiler;
namespace config     = guchho::config;
namespace filesystem = guchho::filesystem;
namespace helpers    = guchho::helpers;
namespace logger     = guchho::logger;
namespace resolver   = guchho::resolver;

namespace {

// The extension order guchho itself uses when nothing overrides it. A test
// that does not set one gets this, so the resolver always has a non-empty
// order to build its node_modules variant from.
const std::vector<std::string> kDefaultExtensions = {
    ".tsx", ".ts", ".jsx", ".js", ".css", ".json"
};

// A resolver transaction wired to a mock file system, mirroring the setup the
// bundler creates around a resolver: the file system, a log that buffers
// everything, a fresh cache set, and a resolver configured through the same
// factory production code uses, so the ESM condition maps and the two derived
// extension orders are built exactly as they are during a build.
struct NpmEnv {
    std::unordered_map<std::string, std::string> files;
    config::Options                             options;
    std::unique_ptr<filesystem::Fs>              fs;
    logger::Log                                 log;
    std::unique_ptr<cache::CacheSet>            caches;
    std::unique_ptr<resolver::Resolver>         res;
};

// Builds a transaction over "files", whose paths are absolute and rooted at
// "cwd". "options" is filled in with the same defaults a build would apply, so
// a test only states what it actually cares about.
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

// Resolves "specifier" from "source_dir" and returns the single path it names.
// A test that cares about the path text calls this, so a failed resolution is
// reported here as an empty string rather than as a nullopt the caller has to
// remember to check.
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

// True when the resolver produced an answer at all. Used by the tests about
// specifiers it is meant to refuse.
bool Resolves(NpmEnv& env,
              const std::string& source_dir,
              const std::string& specifier,
              compiler::ImportKind kind = compiler::ImportKind::kStmt)
{
    resolver::DebugMeta debug_meta;
    return env.res->Resolve(source_dir, specifier, kind, &debug_meta).has_value();
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// A package published with a "main" field, which is the simplest thing a walk
// can be asked to find. The walk reads the name and the main field out of it and
// nothing else, so no test here needs a package.json with anything else in it.
std::string MainPackage(const std::string& name)
{
    return std::string("{\"name\":\"") + name + "\",\"main\":\"./index.js\"}";
}

// A package with no "main" at all, so resolution has to fall through to the
// index file.
std::string BarePackage()
{
    return R"({"name":"bare"})";
}

} // namespace

// ---------------------------------------------------------------------------
// The walk
// ---------------------------------------------------------------------------

TEST(NpmModules, HoistsToProjectRootNodeModules)
{
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'dep';\n"},
        {"/app/node_modules/dep/package.json", MainPackage("dep")},
        {"/app/node_modules/dep/index.js", "export const x = 1;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/src", "dep"),
              std::string("/app/node_modules/dep/index.js"));
}

TEST(NpmModules, PrefersNearestNestedNodeModules)
{
    // The same package name is installed twice. The nearer copy wins, which is
    // what makes a nested install shadow a hoisted one.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'dep';\n"},
        {"/app/node_modules/dep/package.json", MainPackage("dep")},
        {"/app/node_modules/dep/index.js", "export const who = 'root';\n"},
        {"/app/src/node_modules/dep/package.json", MainPackage("dep")},
        {"/app/src/node_modules/dep/index.js", "export const who = 'nested';\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/src", "dep"),
              std::string("/app/src/node_modules/dep/index.js"));
}

TEST(NpmModules, FallsBackToParentWhenNestedLacksPackage)
{
    // The nested directory has a node_modules, but not this package in it. The
    // walk has to keep going rather than stop at the first one it finds.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'dep';\n"},
        {"/app/node_modules/dep/package.json", MainPackage("dep")},
        {"/app/node_modules/dep/index.js", "export const x = 1;\n"},
        {"/app/src/node_modules/other/package.json", MainPackage("other")},
        {"/app/src/node_modules/other/index.js", "export const y = 2;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/src", "dep"),
              std::string("/app/node_modules/dep/index.js"));
}

TEST(NpmModules, ExportsMapStopsTheWalk)
{
    // "exports" is an encapsulation boundary: a package that declares one and
    // refuses the subpath hides its files from every parent directory too, so
    // the walk must not continue upwards to another copy.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'dep/private';\n"},
        {"/app/node_modules/dep/package.json", R"({"name":"dep","exports":{"./public":"./public.js"}})"},
        {"/app/node_modules/dep/public.js", "export const a = 1;\n"},
        {"/app/node_modules/dep/private.js", "export const b = 2;\n"},
    }, "/app");

    EXPECT_FALSE(Resolves(env, "/app/src", "dep/private"));
}

TEST(NpmModules, ExportslessPackageDoesNotStopTheWalk)
{
    // The same layout with no "exports" at all. Nothing is encapsulated, so the
    // unresolved subpath is allowed to keep looking upwards.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'missing';\n"},
        {"/app/node_modules/dep/package.json", MainPackage("dep")},
        {"/app/node_modules/dep/index.js", "export const x = 1;\n"},
    }, "/app");

    EXPECT_FALSE(Resolves(env, "/app/src", "missing"));
    EXPECT_FALSE(Resolves(env, "/app/src", "dep/nope"));
}

TEST(NpmModules, ResolvesScopedPackageSubpath)
{
    // A scoped name spans two path segments, and the package root is the
    // directory holding its package.json, not the scope directory.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import '@scope/pkg/util';\n"},
        {"/app/node_modules/@scope/pkg/package.json", R"({"name":"@scope/pkg","main":"./main.js"})"},
        {"/app/node_modules/@scope/pkg/main.js", "export const a = 1;\n"},
        {"/app/node_modules/@scope/pkg/util.js", "export const b = 2;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/src", "@scope/pkg/util"),
              std::string("/app/node_modules/@scope/pkg/util.js"));
}

TEST(NpmModules, RejectsBareScopeWithoutPackage)
{
    // "@scope" on its own names no package: there is no second segment to read
    // as the package, so it is not a bare specifier at all.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import '@scope';\n"},
        {"/app/node_modules/@scope/pkg/package.json", MainPackage("@scope/pkg")},
        {"/app/node_modules/@scope/pkg/index.js", "export const x = 1;\n"},
    }, "/app");

    EXPECT_FALSE(Resolves(env, "/app/src", "@scope"));
}

TEST(NpmModules, IndexFileWhenPackageHasNoMain)
{
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'bare';\n"},
        {"/app/node_modules/bare/package.json", BarePackage()},
        {"/app/node_modules/bare/index.js", "export const x = 1;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/src", "bare"),
              std::string("/app/node_modules/bare/index.js"));
}

TEST(NpmModules, ModuleFieldWinsOnBrowserPlatform)
{
    // The browser field order is browser, module, main; "main" exists here too
    // but "module" is consulted first, so it wins.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'dual';\n"},
        {"/app/node_modules/dual/package.json", R"({"name":"dual","main":"./main.js","module":"./module.js"})"},
        {"/app/node_modules/dual/main.js", "export const which = 'main';\n"},
        {"/app/node_modules/dual/module.js", "export const which = 'module';\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/src", "dual"),
              std::string("/app/node_modules/dual/module.js"));
}

TEST(NpmModules, MainFieldWinsOnNodePlatform)
{
    // The node field order is main, module, so the same package resolves to a
    // different file purely because of the target platform.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'dual';\n"},
        {"/app/node_modules/dual/package.json", R"({"name":"dual","main":"./main.js","module":"./module.js"})"},
        {"/app/node_modules/dual/main.js", "export const which = 'main';\n"},
        {"/app/node_modules/dual/module.js", "export const which = 'module';\n"},
    }, "/app", {.OutputPlatform = config::Platform::kNode});

    EXPECT_EQ(ResolveTo(env, "/app/src", "dual"),
              std::string("/app/node_modules/dual/main.js"));
}

TEST(NpmModules, PlainJsBeatsStrayDtsInsideNodeModules)
{
    // Inside node_modules the type-annotated extensions are probed after the
    // plain JavaScript ones, so a published .js is found before a declaration
    // file that happens to share its name.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'typed';\n"},
        {"/app/node_modules/typed/package.json", MainPackage("typed")},
        {"/app/node_modules/typed/index.js", "export const x = 1;\n"},
        {"/app/node_modules/typed/index.d.ts", "export declare const x: number;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/src", "typed"),
              std::string("/app/node_modules/typed/index.js"));
}

TEST(NpmModules, ExtensionlessImportInsideNodeModules)
{
    // The implicit extension list is consulted for a bare specifier too, and it
    // includes the plain JavaScript extensions.
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'lib/util';\n"},
        {"/app/node_modules/lib/package.json", MainPackage("lib")},
        {"/app/node_modules/lib/index.js", "export const x = 1;\n"},
        {"/app/node_modules/lib/util.js", "export const y = 2;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/src", "lib/util"),
              std::string("/app/node_modules/lib/util.js"));
}

TEST(NpmModules, AbsNodePathsSearchRoot)
{
    // A configured absolute search root is tried after the node_modules walk,
    // so a package installed outside the project is still findable.
    config::Options options;
    options.AbsNodePaths = {"/vendor/modules"};

    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'dep';\n"},
        {"/vendor/modules/dep/package.json", MainPackage("dep")},
        {"/vendor/modules/dep/index.js", "export const x = 1;\n"},
    }, "/app", options);

    EXPECT_EQ(ResolveTo(env, "/app/src", "dep"),
              std::string("/vendor/modules/dep/index.js"));
}

TEST(NpmModules, SelfReferenceByPackageName)
{
    // A package that declares "name" and "exports" can import itself by name,
    // which is how a package reaches its own entry point without a relative
    // path that would have to be written once per published layout.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/self/package.json", R"({"name":"self","exports":{".":"./index.js"}})"},
        {"/app/node_modules/self/index.js", "export const x = 1;\n"},
        {"/app/node_modules/self/inner.js", "import 'self';\nexport const y = 2;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/node_modules/self", "self"),
              std::string("/app/node_modules/self/index.js"));
}

TEST(NpmModules, SelfReferenceScoped)
{
    NpmEnv env = MakeEnv({
        {"/app/node_modules/@s/p/package.json", R"({"name":"@s/p","exports":{".":"./index.js"}})"},
        {"/app/node_modules/@s/p/index.js", "export const x = 1;\n"},
        {"/app/node_modules/@s/p/inner.js", "import '@s/p';\nexport const y = 2;\n"},
    }, "/app");

    EXPECT_EQ(ResolveTo(env, "/app/node_modules/@s/p", "@s/p"),
              std::string("/app/node_modules/@s/p/index.js"));
}

TEST(NpmModules, MissingPackageFails)
{
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'absent';\n"},
    }, "/app");

    EXPECT_FALSE(Resolves(env, "/app/src", "absent"));
}

TEST(NpmModules, MissingSubpathFails)
{
    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'dep/nope';\n"},
        {"/app/node_modules/dep/package.json", MainPackage("dep")},
        {"/app/node_modules/dep/index.js", "export const x = 1;\n"},
    }, "/app");

    EXPECT_FALSE(Resolves(env, "/app/src", "dep/nope"));
}

// ---------------------------------------------------------------------------
// Globs
// ---------------------------------------------------------------------------

TEST(NpmModules, GlobMatchesMultipleFilesInNodeModules)
{
    // Globs are entry points, so this is driven the way the scanner drives it:
    // a "./"-prefixed pattern and ImportKind::kEntryPoint. Anything else is a
    // relative import that happens to hold a star, which the resolver refuses
    // rather than guessing at.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json", MainPackage("pkg")},
        {"/app/node_modules/pkg/a.js", "export const a = 1;\n"},
        {"/app/node_modules/pkg/b.js", "export const b = 2;\n"},
    }, "/app");

    logger::Msg warning;
    auto found = env.res->ResolveGlob("/app/node_modules/pkg",
                                      helpers::ParseGlobPattern("./*.js"),
                                      compiler::ImportKind::kEntryPoint,
                                      "\"./*.js\"", &warning);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->size(), size_t(2));
}

TEST(NpmModules, GlobWithNoMatchesIsEmptySuccess)
{
    // A glob that matches nothing is a successful search with no results, not
    // a failure, so the caller can tell it apart from a broken pattern.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json", MainPackage("pkg")},
        {"/app/node_modules/pkg/a.js", "export const a = 1;\n"},
    }, "/app");

    logger::Msg warning;
    auto found = env.res->ResolveGlob("/app/node_modules/pkg",
                                      helpers::ParseGlobPattern("./*.css"),
                                      compiler::ImportKind::kEntryPoint,
                                      "\"./*.css\"", &warning);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->empty());
}
