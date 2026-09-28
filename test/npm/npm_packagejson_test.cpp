// Tests for the package.json fields that change what a resolved path means
// rather than which path is chosen: "type", "browser", "sideEffects", and the
// two options that short-circuit the walk, together with the node built-ins the
// resolver answers from memory and the directory it refuses to read.
//
// The "exports"/"imports" maps have their own file. The diagnostics here are
// compared by id, so a test states which message it expects without also
// pinning the severity, the path or the range it happened to be reported at.
// The one exception is a diagnostic with no id to compare on, which the
// resolver raises as a plain error, and that one is compared by its text.

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
#include <utility>
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
// same factory production code uses, so the conditions and the derived
// extension orders are built exactly as they are during a build.
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

// Resolves "specifier" and returns the whole result, because most of these
// tests care about something on it beyond the path.
std::optional<resolver::ResolveResult> Resolve(NpmEnv& env,
                                               const std::string& source_dir,
                                               const std::string& specifier,
                                               compiler::ImportKind kind
                                                       = compiler::ImportKind::kStmt)
{
    resolver::DebugMeta debug_meta;
    return env.res->Resolve(source_dir, specifier, kind, &debug_meta);
}

// A package with the given package.json body and an index.js beside it, which
// is the smallest tree a field test can hang its expectations on.
std::unordered_map<std::string, std::string> Pkg(const std::string& package_json)
{
    return {
        {"/app/node_modules/pkg/package.json", package_json},
        {"/app/node_modules/pkg/index.js", "export const x = 1;\n"},
    };
}

// The ids of the messages the log buffered, in order, with everything else
// about each one discarded.
std::vector<logger::MsgID> WarningIDs(NpmEnv& env)
{
    std::vector<logger::MsgID> ids;
    for (const logger::Msg& msg : env.log.done()) {
        ids.push_back(msg.id);
    }
    return ids;
}

// The id and kind of each message the log buffered, in order. A test that
// cares about both reads them together, because done() drains the log: asking
// it a second time asks about an empty one.
std::vector<std::pair<logger::MsgID, logger::MsgKind>> Messages(NpmEnv& env)
{
    std::vector<std::pair<logger::MsgID, logger::MsgKind>> messages;
    for (const logger::Msg& msg : env.log.done()) {
        messages.push_back({msg.id, msg.kind});
    }
    return messages;
}

// The texts of the messages the log buffered, in order, for the messages that
// were added as plain errors. Those carry no id, so the text is the only thing
// left to say which one it was.
std::vector<std::string> MessageTexts(NpmEnv& env)
{
    std::vector<std::string> texts;
    for (const logger::Msg& msg : env.log.done()) {
        texts.push_back(msg.data.text);
    }
    return texts;
}

} // namespace

// ---------------------------------------------------------------------------
// type
// ---------------------------------------------------------------------------

TEST(NpmPackageJSON, TypeModuleMarksEsm)
{
    NpmEnv env = MakeEnv(Pkg(R"({"name":"pkg","type":"module","main":"./index.js"})"),
                         "/app");

    auto result = Resolve(env, "/app", "pkg");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->module_type_data.type,
              guchho::javascript::ModuleType::kESM_PackageJSON);
}

TEST(NpmPackageJSON, InvalidTypeWarns)
{
    NpmEnv env = MakeEnv(Pkg(R"({"name":"pkg","type":"wat","main":"./index.js"})"),
                         "/app");

    auto result = Resolve(env, "/app", "pkg");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(WarningIDs(env),
              std::vector<logger::MsgID>{logger::MsgID::kPackageJSON_InvalidType});
}

TEST(NpmPackageJSON, TypeEndingInDtsIsDowngradedInsideNodeModules)
{
    // The common mistake is putting a declaration path in "type". Inside
    // node_modules the message is worth a line to a package author and worth
    // nothing to the person building, so it is demoted to debug there.
    NpmEnv env = MakeEnv(Pkg(R"({"name":"pkg","type":"index.d.ts","main":"./index.js"})"),
                         "/app");

    auto result = Resolve(env, "/app", "pkg");
    ASSERT_TRUE(result.has_value());
    // The message is still raised — demotion is about severity, not silence —
    // so the test has to look at the kind rather than at whether it is there.
    using Message = std::pair<logger::MsgID, logger::MsgKind>;
    std::vector<Message> expected{
            {logger::MsgID::kPackageJSON_InvalidType, logger::MsgKind::kDebug}};
    EXPECT_EQ(Messages(env), expected);
}

// ---------------------------------------------------------------------------
// browser
// ---------------------------------------------------------------------------

TEST(NpmPackageJSON, BrowserMapRemaps)
{
    // An object-valued "browser" is a set of substitutions applied after the
    // main field has chosen a file, so a package can ship one layout and be
    // built against another.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","main":"./index.js","browser":{"./index.js":"./browser.js"}})"},
        {"/app/node_modules/pkg/index.js", "export const x = 1;\n"},
        {"/app/node_modules/pkg/browser.js", "export const x = 2;\n"},
    }, "/app", {.OutputPlatform = config::Platform::kBrowser});

    auto result = Resolve(env, "/app", "pkg");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->path_pair.primary.text,
              std::string("/app/node_modules/pkg/browser.js"));
}

TEST(NpmPackageJSON, BrowserFalseDisables)
{
    // A false value is not a path but a decision: the module has no
    // browser-side equivalent. The resolution still succeeds, so the linker can
    // drop the import, but the path comes back marked disabled rather than
    // looking like a normal file.
    NpmEnv env = MakeEnv({
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","main":"./index.js","browser":{"./index.js":false}})"},
        {"/app/node_modules/pkg/index.js", "export const x = 1;\n"},
    }, "/app", {.OutputPlatform = config::Platform::kBrowser});

    auto result = Resolve(env, "/app", "pkg");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->path_pair.primary.IsDisabled());
}

TEST(NpmPackageJSON, BrowserFieldIgnoredOnNodePlatform)
{
    // The map is only read for a browser build, so the same package resolves to
    // the unmapped file for node.
    std::unordered_map<std::string, std::string> files = {
        {"/app/node_modules/pkg/package.json",
            R"({"name":"pkg","main":"./index.js","browser":{"./index.js":"./browser.js"}})"},
        {"/app/node_modules/pkg/index.js", "export const x = 1;\n"},
        {"/app/node_modules/pkg/browser.js", "export const x = 2;\n"},
    };

    NpmEnv env = MakeEnv(files, "/app", {.OutputPlatform = config::Platform::kNode});
    auto result = Resolve(env, "/app", "pkg");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->path_pair.primary.text,
              std::string("/app/node_modules/pkg/index.js"));
}

// ---------------------------------------------------------------------------
// sideEffects
// ---------------------------------------------------------------------------

TEST(NpmPackageJSON, SideEffectsFalseMarksPackageClean)
{
    // "sideEffects": false travels back on the result so the linker can drop an
    // import whose bindings are unused.
    NpmEnv env = MakeEnv(Pkg(R"({"name":"pkg","main":"./index.js","sideEffects":false})"),
                         "/app");

    auto result = Resolve(env, "/app", "pkg");
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->primary_side_effects_data, nullptr);
    EXPECT_FALSE(result->primary_side_effects_data->is_side_effects_array_in_json);
}

TEST(NpmPackageJSON, SideEffectsArrayWildcardMatches)
{
    // The array form is recorded differently from the boolean form, and the
    // difference is what tells the linker the package has some side effects
    // even though it also lists the files that do not.
    NpmEnv env = MakeEnv(Pkg(R"({"name":"pkg","main":"./index.js","sideEffects":["./pure.js"]})"),
                         "/app");

    auto result = Resolve(env, "/app", "pkg");
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(result->primary_side_effects_data, nullptr);
    EXPECT_TRUE(result->primary_side_effects_data->is_side_effects_array_in_json);
}

// ---------------------------------------------------------------------------
// Node built-ins
// ---------------------------------------------------------------------------

TEST(NpmPackageJSON, NodeBuiltinIsExternalOnNodePlatform)
{
    // A built-in has no file to point at, so the answer is marked external and
    // the linker is told the import itself has no side effects.
    NpmEnv env = MakeEnv({{"/app/index.js", "import 'path';\n"}}, "/app",
                         {.OutputPlatform = config::Platform::kNode});

    auto result = Resolve(env, "/app", "path");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->path_pair.is_external);
    EXPECT_NE(result->primary_side_effects_data, nullptr);
}

TEST(NpmPackageJSON, NodeColonPrefixIsNodePlatformOnly)
{
    // The "node:" prefix does not make a specifier a built-in on its own. Only
    // the node platform answers built-ins from memory, because only there is
    // there a Node runtime to answer them. On the browser platform the prefix
    // carries no special meaning, so the walk goes looking for a package and
    // finds none — the bundler attaches a "this is built into Node" hint to that
    // miss instead of quietly marking it external.
    NpmEnv env = MakeEnv({{"/app/index.js", "import 'node:path';\n"}}, "/app",
                         {.OutputPlatform = config::Platform::kBrowser});

    auto result = Resolve(env, "/app", "node:path");
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Options that short-circuit the walk
// ---------------------------------------------------------------------------

TEST(NpmPackageJSON, ExternalPackagesShortCircuits)
{
    // Treating every bare specifier as external is how a library build stops
    // pulling its dependencies in, so the walk must not be entered at all.
    config::Options options;
    options.ExternalPackages = true;

    NpmEnv env = MakeEnv({
        {"/app/index.js", "import 'dep';\n"},
        {"/app/node_modules/dep/package.json", R"({"name":"dep","main":"./index.js"})"},
        {"/app/node_modules/dep/index.js", "export const x = 1;\n"},
    }, "/app", options);

    auto result = Resolve(env, "/app", "dep");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->path_pair.is_external);
}

TEST(NpmPackageJSON, PackageAliasLongestPrefixWins)
{
    // Aliases are matched on whole path segments and the most specific one
    // wins, so a general alias does not swallow a longer name.
    config::Options options;
    options.PackageAliases = {
        {"dep", "/other/dep"},
        {"dep/sub", "/app/real.js"},
    };

    NpmEnv env = MakeEnv({
        {"/app/index.js", "import 'dep/sub';\n"},
        {"/app/real.js", "export const x = 1;\n"},
        {"/other/dep/package.json", R"({"name":"dep","main":"./index.js"})"},
        {"/other/dep/index.js", "export const x = 2;\n"},
    }, "/app", options);

    auto result = Resolve(env, "/app", "dep/sub");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->path_pair.primary.text, std::string("/app/real.js"));
}

TEST(NpmPackageJSON, PackageAliasResolvesFromCwd)
{
    // An aliased specifier is resolved as though it had been written in the
    // working directory, because that is where the package it was aliased to
    // lives.
    config::Options options;
    options.PackageAliases = {{"alias", "/vendor/pkg"}};

    NpmEnv env = MakeEnv({
        {"/app/src/index.js", "import 'alias';\n"},
        {"/vendor/pkg/package.json", R"({"name":"pkg","main":"./index.js"})"},
        {"/vendor/pkg/index.js", "export const x = 1;\n"},
    }, "/app", options);

    auto result = Resolve(env, "/app/src", "alias");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->path_pair.primary.text,
              std::string("/vendor/pkg/index.js"));
}

// ---------------------------------------------------------------------------
// Directories the resolver will not read
// ---------------------------------------------------------------------------

// A mock file system that fails to read one particular directory, with the
// error the test needs. The shared mock can only report "no such file or
// directory", and telling a permission failure apart from an I/O failure is
// the whole point of these two tests, so the error is scripted per directory.
class ScriptedErrFs : public guchho::test::MockFS {
public:
    ScriptedErrFs(const std::unordered_map<std::string, std::string>& input,
                  filesystem::MockKind                                kind,
                  const std::string&                                  abs_working_dir,
                  const std::string&                                  unreadable_dir,
                  std::errc                                           unreadable_error)
        : MockFS(input, kind, abs_working_dir),
          unreadable_dir_(unreadable_dir),
          unreadable_error_(unreadable_error) {}

    filesystem::FsResult<filesystem::DirEntries> ReadDirectory(
            const std::string& path) override
    {
        filesystem::FsResult<filesystem::DirEntries> result =
                MockFS::ReadDirectory(path);
        if (path == unreadable_dir_) {
            result.value            = filesystem::DirEntries{};
            result.canonical_error  = unreadable_error_;
            result.original_error   = "scripted failure";
        }
        return result;
    }

private:
    std::string unreadable_dir_;
    std::errc   unreadable_error_;
};

TEST(NpmPackageJSON, UnreadableDirectoryIsOpaqueNotAnError)
{
    // A directory nobody is allowed to read is treated as an empty one rather
    // than as a failure, so a locked directory somewhere above the project
    // cannot break resolution of files that are perfectly readable below it,
    // and it produces no diagnostic to read either.
    std::unordered_map<std::string, std::string> files = {
        {"/locked/index.js", "export const x = 1;\n"},
    };

    NpmEnv env;
    env.files = files;
    env.fs    = std::make_unique<ScriptedErrFs>(files,
                                                filesystem::MockKind::kUnix,
                                                "/locked",
                                                "/locked",
                                                std::errc::permission_denied);
    env.log    = logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll, {});
    env.caches = cache::MakeCacheSet();
    env.res    = resolver::NewResolver(config::APICall::kBuildCall,
                                       *env.fs, env.log, *env.caches, &env.options);

    EXPECT_FALSE(Resolve(env, "/locked", "absent").has_value());
    EXPECT_EQ(WarningIDs(env), std::vector<logger::MsgID>{});
}

TEST(NpmPackageJSON, OtherDirectoryReadErrorIsReported)
{
    // Any other failure to read a directory is a real problem, so it is
    // reported rather than swallowed. The pair with the test above is what
    // makes the special case defensible: it is special, not general.
    std::unordered_map<std::string, std::string> files = {
        {"/locked/index.js", "export const x = 1;\n"},
    };

    NpmEnv env;
    env.files = files;
    env.fs    = std::make_unique<ScriptedErrFs>(files,
                                                filesystem::MockKind::kUnix,
                                                "/locked",
                                                "/locked",
                                                std::errc::io_error);
    env.log    = logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll, {});
    env.caches = cache::MakeCacheSet();
    env.res    = resolver::NewResolver(config::APICall::kBuildCall,
                                       *env.fs, env.log, *env.caches, &env.options);

    EXPECT_FALSE(Resolve(env, "/locked", "absent").has_value());

    // The directory read failure is added as a plain error, so it has no id to
    // compare on and the text is the only thing that says which one it was. The
    // path in it is matched loosely because it is printed in whichever style the
    // options ask for, and this test is about the failure being reported at all.
    std::vector<std::string> texts = MessageTexts(env);
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_NE(texts[0].find("Cannot read directory"), std::string::npos);
    EXPECT_NE(texts[0].find("scripted failure"), std::string::npos);
}
