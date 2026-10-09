// Tests for Guchho config file discovery and application (guchho_json.cpp).
//
// Every test runs against a tiny in-memory file system so the discovery
// walks never touch the real disk. "guchho.config.js" files are served by a
// fake loader that parses the file's JSON directly, so the tests exercise the
// shared field-mapping code without ever spawning a Node process.

#include "test/guchho_test.hpp"

#include "guchho/cache.hpp"
#include "guchho/config.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"
#include "guchho/resolver.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace filesystem = guchho::filesystem;
namespace logger     = guchho::logger;
namespace config     = guchho::config;
namespace cache      = guchho::cache;
namespace resolver   = guchho::resolver;

namespace {

// A tiny in-memory file system for deterministic discovery tests. Paths are
// unix-style and absolute (rooted at "/"). Directories are derived from the
// stored file paths, so a test only has to register the files it cares about.
class TestFs : public filesystem::Fs {
public:
    std::unordered_map<std::string, std::string> files;
    int                                          read_directory_calls = 0;

    void SetFile(const std::string& path, const std::string& contents)
    {
        files[path] = contents;
    }

    static std::string ToLower(std::string s)
    {
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return s;
    }

    static std::string JoinPath(std::string_view dir, std::string_view base)
    {
        if (dir.empty() || dir == "/") {
            return "/" + std::string(base);
        }
        return std::string(dir) + "/" + std::string(base);
    }

    // Returns true and fills "base" when "path" is a direct child of "dir".
    static bool IsChild(const std::string& dir, const std::string& path, std::string& base)
    {
        std::string prefix = dir;
        if (prefix == "/") {
            prefix = "";
        } else if (!prefix.empty() && prefix.back() == '/') {
            prefix.pop_back();
        }
        if (!path.starts_with(prefix + "/")) {
            return false;
        }
        std::string rest = path.substr(prefix.size() + 1);
        if (rest.empty() || rest.find('/') != std::string::npos) {
            return false;
        }
        base = rest;
        return true;
    }

    filesystem::FsResult<filesystem::DirEntries> ReadDirectory(const std::string& path) override
    {
        ++read_directory_calls;
        filesystem::FsResult<filesystem::DirEntries> result;
        result.value                   = filesystem::MakeEmptyDirEntries(path);
        result.value.data              = std::map<std::string, std::shared_ptr<filesystem::Entry>>{};
        std::string              base;
        for (const auto& [full_path, contents] : files) {
            (void)contents;
            if (IsChild(path, full_path, base)) {
                auto entry     = std::make_shared<filesystem::Entry>();
                entry->dir     = path;
                entry->base    = base;
                entry->need_stat = true;
                result.value.data->emplace(ToLower(base), std::move(entry));
            }
        }
        return result;
    }

    filesystem::FsResult<std::string> ReadFile(const std::string& path) override
    {
        filesystem::FsResult<std::string> result;
        auto                              it = files.find(path);
        if (it == files.end()) {
            result.canonical_error = std::errc::no_such_file_or_directory;
            result.original_error  = "no such file or directory";
            return result;
        }
        result.value = it->second;
        return result;
    }

    filesystem::FsResult<std::shared_ptr<filesystem::OpenedFile>> OpenFile(const std::string& path) override
    {
        (void)path;
        filesystem::FsResult<std::shared_ptr<filesystem::OpenedFile>> result;
        result.canonical_error = std::errc::not_supported;
        return result;
    }

    filesystem::ModKeyResult ModKey(const std::string& path) override
    {
        filesystem::ModKeyResult result;
        auto                     it = files.find(path);
        if (it == files.end()) {
            result.canonical_error = std::errc::no_such_file_or_directory;
            result.original_error  = "no such file or directory";
            return result;
        }
        result.value.size = int64_t(it->second.size());
        return result;
    }

    bool IsAbs(std::string_view path) override
    {
        return !path.empty() && path.front() == '/';
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
        std::string_view p = path;
        while (p.size() > 1 && p.back() == '/') {
            p.remove_suffix(1);
        }
        size_t pos = p.rfind('/');
        if (pos == std::string_view::npos || pos == 0) {
            return "/";
        }
        return std::string(p.substr(0, pos));
    }

    std::string Base(std::string_view path) override
    {
        size_t pos = path.rfind('/');
        return pos == std::string_view::npos ? std::string(path) : std::string(path.substr(pos + 1));
    }

    std::string Ext(std::string_view path) override
    {
        std::string base = Base(path);
        size_t      pos  = base.rfind('.');
        return pos == std::string::npos ? "" : base.substr(pos);
    }

    std::string Join(std::initializer_list<std::string_view> parts) override
    {
        std::string out;
        for (std::string_view part : parts) {
            if (out.empty() || out.back() == '/') {
                out += part;
            } else {
                out += "/";
                out += part;
            }
        }
        return out.empty() ? "/" : out;
    }

    std::string Cwd() override
    {
        return "/";
    }

    std::optional<std::string> Rel(std::string_view base, std::string_view target) override
    {
        if (base == target) {
            return std::string(".");
        }
        if (base == "/") {
            return std::string(target.substr(1));
        }
        if (target.starts_with(base) && base.back() == '/') {
            return std::string(target.substr(base.size()));
        }
        return std::string(target);
    }

    std::optional<std::string> EvalSymlinks(std::string_view path) override
    {
        return std::string(path);
    }

    std::pair<std::string, filesystem::EntryKind> Kind(std::string_view dir, std::string_view base) override
    {
        std::string full = JoinPath(dir, base);
        if (files.count(full) != 0) {
            return {std::string(dir), filesystem::EntryKind::kFile};
        }
        return {std::string(dir), filesystem::EntryKind::kInvalid};
    }

    filesystem::WatchData GetWatchData() override
    {
        return {};
    }
};

// A logger that buffers everything, like the other unit-test suites.
logger::Log NewLog()
{
    return logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll, {});
}

// RAII guard that installs a fake JS loader and restores the previous one.
class JSLoaderGuard {
public:
    explicit JSLoaderGuard(resolver::GuchhoConfigJSLoader loader)
        : prev_(resolver::SetGuchhoConfigJSLoader(loader))
    {
    }

    ~JSLoaderGuard()
    {
        resolver::SetGuchhoConfigJSLoader(prev_);
    }

private:
    resolver::GuchhoConfigJSLoader prev_;
};

// A fake "guchho.config.js" loader: reads the file from the test FS and parses
// its contents as JSON, exactly as the real loader would receive them from
// Node. It never spawns a child process.
int g_fake_js_calls = 0;

resolver::GuchhoConfig FakeJSLoader(
    logger::Log&       log,
    cache::JSONCache&  json_cache,
    filesystem::Fs&    fs,
    config::Options&   opts,
    const std::string& file_path)
{
    ++g_fake_js_calls;
    auto contents = fs.ReadFile(file_path);
    if (!contents.Ok()) {
        return resolver::GuchhoConfig{};
    }
    return resolver::LoadGuchhoConfigFromText(log, json_cache, fs, opts, contents.value, file_path);
}

// A fake loader that reports failure unconditionally, as if Node were missing
// or the guchho.config.js evaluation failed.
resolver::GuchhoConfig AlwaysFailJSLoader(
    logger::Log&       log,
    cache::JSONCache&  json_cache,
    filesystem::Fs&    fs,
    config::Options&   opts,
    const std::string& file_path)
{
    (void)log;
    (void)json_cache;
    (void)fs;
    (void)opts;
    (void)file_path;
    return resolver::GuchhoConfig{};
}

// Asserts that "result" carries exactly the built-in defaults for "root_dir":
// no config was found, no parse error, and every field holds its §9 default.
void ExpectDefaults(const resolver::GuchhoConfig& result, const std::string& root_dir)
{
    EXPECT_FALSE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_TRUE(result.config_dir.empty());
    EXPECT_TRUE(result.config_path.empty());

    EXPECT_EQ(result.builds[0].entry_points.size(), size_t(1));
    EXPECT_EQ(result.builds[0].entry_points[0].InputPath, TestFs::JoinPath(root_dir, "index.html"));

    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, TestFs::JoinPath(root_dir, "dist"));
    EXPECT_TRUE(result.builds[0].opts.AbsOutputFile.empty());

    EXPECT_EQ(result.builds[0].opts.OutputFormat, config::Format::kESModule);
    EXPECT_EQ(result.builds[0].opts.OutputPlatform, config::Platform::kBrowser);
    EXPECT_EQ(result.builds[0].opts.OriginalTargetEnv, std::string("esnext"));

    // No config file means no minification. A build that has to be asked before
    // it shortens the output is one where turning minify on is a decision someone
    // made, not something that happened on the way to a production bundle.
    EXPECT_FALSE(result.builds[0].opts.MinifyWhitespace);
    EXPECT_FALSE(result.builds[0].opts.MinifyIdentifiers);
    EXPECT_FALSE(result.builds[0].opts.MinifySyntax);
    EXPECT_EQ(result.builds[0].opts.SourceMapData, config::SourceMap::kNone);
    EXPECT_FALSE(result.builds[0].opts.CodeSplitting);
    EXPECT_TRUE(result.builds[0].opts.TreeShaking);
}

} // namespace

// ---------------------------------------------------------------------------
// Default configuration / no config file
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, DefaultConfiguration)
{
    TestFs fs;
    config::Options    opts;
    cache::JSONCache   json_cache;
    logger::Log        log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    ExpectDefaults(result, "/project");
}

TEST(GuchhoConfig, NoConfigFile)
{
    TestFs fs;
    // Unrelated files that are not configs.
    fs.SetFile("/project/index.html", "<!doctype html>");
    fs.SetFile("/project/src/app.js", "");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result =
        resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project/src");

    // The defaults are rooted at the discovery start directory.
    ExpectDefaults(result, "/project/src");
}

// ---------------------------------------------------------------------------
// File name priority within one directory
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, FilenamePriorityConfigJSBeatsJson)
{
    TestFs fs;
    JSLoaderGuard guard(&FakeJSLoader);
    fs.SetFile("/project/guchho.config.js", "{\"build\":{\"outdir\":\"from-js\"}}");
    fs.SetFile("/project/guchho.config.json", "{\"build\":{\"outdir\":\"from-config-json\"}}");
    fs.SetFile("/project/guchho.json", "{\"build\":{\"outdir\":\"from-plain-json\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(result.config_path, std::string("/project/guchho.config.js"));
    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, std::string("/project/from-js"));

    // The two JSON files were never merged in and the default entry is kept.
    EXPECT_EQ(result.builds[0].entry_points.size(), size_t(1));
    EXPECT_EQ(result.builds[0].entry_points[0].InputPath, std::string("/project/index.html"));
    EXPECT_EQ(g_fake_js_calls, 1);
}

TEST(GuchhoConfig, FilenamePriorityConfigJSONBeatsPlainJSON)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "{\"build\":{\"outdir\":\"from-config-json\"}}");
    fs.SetFile("/project/guchho.json", "{\"build\":{\"outdir\":\"from-plain-json\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(result.config_path, std::string("/project/guchho.config.json"));
    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, std::string("/project/from-config-json"));
}

// ---------------------------------------------------------------------------
// Parent discovery
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, ParentDiscovery)
{
    TestFs fs;
    JSLoaderGuard guard(&FakeJSLoader);
    fs.SetFile("/project/guchho.config.js", "{\"build\":{\"outdir\":\"root-out\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project/src");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(result.config_path, std::string("/project/guchho.config.js"));
    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, std::string("/project/root-out"));

    // Only "outdir" was overridden; the entry default stays rooted at the
    // discovery start directory.
    EXPECT_EQ(result.builds[0].entry_points.size(), size_t(1));
    EXPECT_EQ(result.builds[0].entry_points[0].InputPath, std::string("/project/src/index.html"));
}

// ---------------------------------------------------------------------------
// Only one config selected; configs are never merged
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, OnlyOneConfigSelected)
{
    TestFs fs;
    JSLoaderGuard guard(&FakeJSLoader);
    // The JS config asks for minification so that the assertion below has
    // something to disagree with the JSON file, which turns minification off.
    fs.SetFile("/project/guchho.config.js",
               "{\"build\":{\"outdir\":\"js-out\",\"minify\":true}}");
    fs.SetFile("/project/src/guchho.config.json",
               "{\"build\":{\"outdir\":\"json-out\",\"minify\":false}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project/src");

    // Discovery stops at the nearest directory that holds a config: the JSON
    // file in "src" wins, and the JS file in the parent is never consulted.
    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(result.config_path, std::string("/project/src/guchho.config.json"));
    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, std::string("/project/src/json-out"));
    EXPECT_FALSE(result.builds[0].opts.MinifyWhitespace);
}

// The real-world shape of the same rule: a leftover guchho.config.js sitting
// several directories above the project must not capture a build whose own
// directory holds a guchho.config.json. Before the nearest-config-wins fix
// the parent JS config was returned, the JSON was silently discarded, and
// the relative "outdir" resolved against the parent's directory.
TEST(GuchhoConfig, NearestJsonBeatsParentJS)
{
    TestFs fs;
    JSLoaderGuard guard(&FakeJSLoader);
    fs.SetFile("/Documents/guchho.config.js", "{\"build\":{\"outdir\":\"dist\"}}");
    fs.SetFile("/Documents/GitHub/project/playground/guchho.config.json",
               "{\"build\":{\"outdir\":\"playground-out\"}}");

    int before = g_fake_js_calls;

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(
        log, json_cache, fs, opts, "/Documents/GitHub/project/playground");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(result.config_path,
              std::string("/Documents/GitHub/project/playground/guchho.config.json"));
    // The outdir is rooted at the config that won, not at the parent that
    // lost, so the build stays inside the project.
    EXPECT_EQ(result.builds[0].opts.AbsOutputDir,
              std::string("/Documents/GitHub/project/playground/playground-out"));
    // The parent's guchho.config.js was never even evaluated.
    EXPECT_EQ(g_fake_js_calls, before);
}

// ---------------------------------------------------------------------------
// Invalid configs are FOUND + INVALID and stop discovery
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, InvalidJsonStopsDiscovery)
{
    TestFs fs;
    fs.SetFile("/project/src/guchho.config.json", "{\"build\": {\"outdir\": \"broken");
    fs.SetFile("/project/guchho.config.js", "{\"build\":{\"outdir\":\"parent-good\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result =
        resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project/src");

    // The malformed JSON file exists, so it is selected. Discovery must not
    // continue to the parent's valid guchho.config.js nor fall back to the
    // defaults.
    EXPECT_TRUE(result.found);
    EXPECT_TRUE(result.parse_error);
    EXPECT_EQ(result.config_path, std::string("/project/src/guchho.config.json"));
    EXPECT_TRUE(result.builds.empty());
}

TEST(GuchhoConfig, InvalidJSConfigStopsDiscovery)
{
    TestFs fs;
    JSLoaderGuard guard(&AlwaysFailJSLoader);
    fs.SetFile("/project/guchho.config.js", "console.log('this is not JSON')");
    fs.SetFile("/project/guchho.config.json", "{\"build\":{\"outdir\":\"fallback-json\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    // The JS config exists but the (faked) Node evaluation failed. That is
    // FOUND + INVALID; the JSON fallback in the same directory must not be
    // used, and every field keeps its default.
    EXPECT_TRUE(result.found);
    EXPECT_TRUE(result.parse_error);
    EXPECT_EQ(result.config_path, std::string("/project/guchho.config.js"));
    EXPECT_TRUE(result.builds.empty());
}

// ---------------------------------------------------------------------------
// Partial overrides keep every other default
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, PartialOverrideKeepsDefaults)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"build\":{\"outdir\":\"custom-out\",\"minify\":false}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);

    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, std::string("/project/custom-out"));
    EXPECT_FALSE(result.builds[0].opts.MinifyWhitespace);
    EXPECT_FALSE(result.builds[0].opts.MinifyIdentifiers);
    EXPECT_FALSE(result.builds[0].opts.MinifySyntax);

    // Everything else keeps its default.
    EXPECT_EQ(result.builds[0].opts.OutputFormat, config::Format::kESModule);
    EXPECT_EQ(result.builds[0].opts.OutputPlatform, config::Platform::kBrowser);
    EXPECT_EQ(result.builds[0].opts.OriginalTargetEnv, std::string("esnext"));
    EXPECT_EQ(result.builds[0].opts.SourceMapData, config::SourceMap::kNone);
    EXPECT_FALSE(result.builds[0].opts.CodeSplitting);
    EXPECT_TRUE(result.builds[0].opts.TreeShaking);
    EXPECT_TRUE(result.builds[0].opts.AbsOutputFile.empty());
    EXPECT_EQ(result.builds[0].entry_points.size(), size_t(1));
    EXPECT_EQ(result.builds[0].entry_points[0].InputPath, std::string("/project/index.html"));
}

// ---------------------------------------------------------------------------
// The three file names are interchangeable spellings of the same config
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, EquivalentSchemas)
{
    const std::string text =
        "{\"build\":{\"outdir\":\"shared-out\",\"minify\":{\"whitespace\":false,\"syntax\":true}}}";

    config::Options js_opts;
    config::Options config_json_opts;
    config::Options plain_json_opts;

    // Each spelling is selected on its own in-memory file system, but always
    // from the same directory so config-relative paths (outdir, entry) resolve
    // identically. That is what makes the three files interchangeable.
    {
        TestFs fs;
        JSLoaderGuard guard(&FakeJSLoader);
        fs.SetFile("/a/guchho.config.js", text);

        config::Options  opts;
        cache::JSONCache json_cache;
        logger::Log      log = NewLog();
        resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/a");
        EXPECT_TRUE(result.found);
        EXPECT_FALSE(result.parse_error);
        js_opts = result.builds[0].opts;
    }

    {
        TestFs fs;
        fs.SetFile("/a/guchho.config.json", text);

        config::Options  opts;
        cache::JSONCache json_cache;
        logger::Log      log = NewLog();
        resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/a");
        EXPECT_TRUE(result.found);
        EXPECT_FALSE(result.parse_error);
        config_json_opts = result.builds[0].opts;
    }

    {
        TestFs fs;
        fs.SetFile("/a/guchho.json", text);

        config::Options  opts;
        cache::JSONCache json_cache;
        logger::Log      log = NewLog();
        resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/a");
        EXPECT_TRUE(result.found);
        EXPECT_FALSE(result.parse_error);
        plain_json_opts = result.builds[0].opts;
    }

    EXPECT_EQ(js_opts.AbsOutputDir, config_json_opts.AbsOutputDir);
    EXPECT_EQ(js_opts.AbsOutputDir, plain_json_opts.AbsOutputDir);
    EXPECT_EQ(js_opts.AbsOutputDir, std::string("/a/shared-out"));

    EXPECT_EQ(js_opts.MinifyWhitespace, plain_json_opts.MinifyWhitespace);
    EXPECT_FALSE(js_opts.MinifyIdentifiers);
    EXPECT_TRUE(js_opts.MinifySyntax);

    EXPECT_EQ(js_opts.OutputFormat, config_json_opts.OutputFormat);
    EXPECT_EQ(js_opts.OutputPlatform, plain_json_opts.OutputPlatform);
    EXPECT_EQ(js_opts.OriginalTargetEnv, plain_json_opts.OriginalTargetEnv);
    EXPECT_EQ(js_opts.SourceMapData, config_json_opts.SourceMapData);
    EXPECT_EQ(js_opts.TreeShaking, plain_json_opts.TreeShaking);
    EXPECT_FALSE(js_opts.CodeSplitting);
}

// ---------------------------------------------------------------------------
// The fake loader is used and then restored
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, JSLoaderIsFakedAndRestored)
{
    resolver::GuchhoConfigJSLoader original = resolver::GetGuchhoConfigJSLoader();
    EXPECT_NE(original, &FakeJSLoader);

    {
        JSLoaderGuard guard(&FakeJSLoader);
        EXPECT_EQ(resolver::GetGuchhoConfigJSLoader(), &FakeJSLoader);

        TestFs fs;
        fs.SetFile("/project/guchho.config.js", "{\"build\":{\"outdir\":\"x\"}}");

        config::Options  opts;
        cache::JSONCache json_cache;
        logger::Log      log = NewLog();
        int              before = g_fake_js_calls;

        resolver::GuchhoConfig result =
            resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

        // The config was loaded, which is only possible through the fake
        // (Node is never spawned by these tests).
        EXPECT_TRUE(result.found);
        EXPECT_EQ(g_fake_js_calls, before + 1);
    }

    EXPECT_EQ(resolver::GetGuchhoConfigJSLoader(), original);
}

// ---------------------------------------------------------------------------
// Aliases: a Vite-shaped spelling of a canonical field
// ---------------------------------------------------------------------------

// Counts the buffered messages carrying "id", so a test can assert both that a
// warning fired and that it fired exactly once.
size_t CountMessagesOfID(const logger::Log& log, logger::MsgID id)
{
    size_t count = 0;
    for (const auto& msg : log.peek()) {
        if (msg.id == id) ++count;
    }
    return count;
}

TEST(GuchhoConfig, TopLevelEntryAlias)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "{\"entry\":[\"src/main.html\"]}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds[0].entry_points.size(), size_t(1));
    EXPECT_EQ(result.builds[0].entry_points[0].InputPath, std::string("/project/src/main.html"));

    // "entry" is a supported spelling now, so it must not also draw the
    // generic unknown-field warning.
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
}

TEST(GuchhoConfig, OutputAliases)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"output\":{\"dir\":\"out\",\"format\":\"cjs\",\"sourcemap\":\"inline\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, std::string("/project/out"));
    EXPECT_EQ(result.builds[0].opts.OutputFormat, config::Format::kCommonJS);
    EXPECT_EQ(result.builds[0].opts.SourceMapData, config::SourceMap::kInline);
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
}

// The alias and its canonical field are read from different objects, and the
// "output" block is parsed after the "build" block, so the precedence rule is
// what stops the alias from overwriting the value the user set explicitly.
TEST(GuchhoConfig, CanonicalWinsOverAlias)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"build\":{\"entry\":[\"src/main.html\"],\"outdir\":\"canonical\","
               "\"format\":\"esm\",\"sourcemap\":false},"
               "\"entry\":[\"ignored.html\"],"
               "\"output\":{\"dir\":\"alias\",\"format\":\"cjs\",\"sourcemap\":\"inline\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);

    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, std::string("/project/canonical"));
    EXPECT_EQ(result.builds[0].opts.OutputFormat, config::Format::kESModule);
    EXPECT_EQ(result.builds[0].opts.SourceMapData, config::SourceMap::kNone);

    // The losing alias must not append a second entry point.
    ASSERT_EQ(result.builds[0].entry_points.size(), size_t(1));
    EXPECT_EQ(result.builds[0].entry_points[0].InputPath, std::string("/project/src/main.html"));

    // Four conflicts: top-level "entry", plus dir/format/sourcemap.
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_AliasIgnored), size_t(4));
}

// The global name a wrapper format publishes under has two spellings, and both
// have to end up as the same field: "name" is the short form this project's
// brief asks for, "globalName" is the camelCase the rest of the schema uses.
// A config that carries only one of them must not warn about the other - the
// two are the same field, not an alias of a field that is also set.
TEST(GuchhoConfig, GlobalNameAliasIsAcceptedUnderEitherSpelling)
{
    struct Case {
        std::string json;
        std::vector<std::string> expected_parts;
    };
    const Case cases[] = {
        {"{\"build\":{\"name\":\"Lib\"}}",                    {"Lib"}},
        {"{\"build\":{\"globalName\":\"Lib\"}}",              {"Lib"}},
        {"{\"build\":{\"name\":\"Foo.Bar\"}}",                {"Foo", "Bar"}},
        {"{\"output\":{\"name\":\"Lib\"}}",                   {"Lib"}},
        {"{\"output\":{\"globalName\":\"Foo.Bar\"}}",         {"Foo", "Bar"}},
    };

    for (const Case& one : cases) {
        TestFs fs;
        fs.SetFile("/project/guchho.config.json", one.json);

        config::Options  opts;
        cache::JSONCache json_cache;
        logger::Log      log = NewLog();

        resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

        EXPECT_TRUE(result.found) << one.json;
        ASSERT_EQ(result.builds.size(), size_t(1)) << one.json;
        EXPECT_EQ(result.builds[0].opts.GlobalName, one.expected_parts) << one.json;

        // The parsed parts are the linker's input; the text is what the API
        // copies into its own option and what a later precedence check
        // compares, so both have to be there.
        EXPECT_FALSE(result.builds[0].opts.GlobalNameText.empty()) << one.json;

        // Neither spelling is an unknown field.
        EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0))
            << one.json;
        EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_AliasIgnored), size_t(0))
            << one.json;
    }
}

// A config that carries both spellings says which one won, and the losing one
// has no effect. This is the same rule the rest of the schema's aliases follow,
// and it is the difference between a config that works and one that silently
// publishes under the wrong global.
TEST(GuchhoConfig, GlobalNameCanonicalWinsOverItsAlias)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"build\":{\"name\":\"Canonical\",\"globalName\":\"Ignored\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_EQ(result.builds[0].opts.GlobalName, std::vector<std::string>({"Canonical"}));
    EXPECT_EQ(result.builds[0].opts.GlobalNameText, std::string("Canonical"));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_AliasIgnored), size_t(1));
}

// A name in the "output" block is an alias for the same field, so a config that
// sets it in both places is telling the linker two different things. The build
// block is the canonical home for it, so it wins and the other is reported.
TEST(GuchhoConfig, GlobalNameInOutputDoesNotOverrideTheBuildBlock)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"build\":{\"name\":\"FromBuild\"},"
               "\"output\":{\"name\":\"FromOutput\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_EQ(result.builds[0].opts.GlobalName, std::vector<std::string>({"FromBuild"}));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_AliasIgnored), size_t(1));
}

// A config written in the flat schema must not start warning now that aliases
// exist. This is the regression guard for the whole additive change.
TEST(GuchhoConfig, CanonicalSchemaStaysSilent)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"build\":{\"entry\":[\"src/main.html\"],\"outdir\":\"dist\","
               "\"outfile\":\"\",\"format\":\"esm\",\"platform\":\"browser\","
               "\"target\":\"esnext\",\"minify\":true,\"bundle\":true,"
               "\"sourcemap\":false,"
               "\"splitting\":false,\"treeShaking\":true,\"pretty\":false},"
               "\"css\":{\"minify\":true},\"assets\":{\"inlineLimit\":4096},"
               "\"resolve\":{\"extensions\":[\".ts\"],\"alias\":{}},"
               "\"external\":[],\"define\":{},\"logLevel\":\"info\","
               "\"output\":{\"entryFileNames\":\"[name]-[hash]\","
               "\"chunkFileNames\":\"chunks/[name]-[hash]\","
               "\"assetFileNames\":\"assets/[name]-[hash]\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_AliasIgnored), size_t(0));
}

// ---------------------------------------------------------------------------
// Fields that are accepted but have no effect now say so
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, UnsupportedFieldsWarn)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"root\":\".\",\"server\":{\"port\":3000},\"watch\":true}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);

    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_RootIgnored), size_t(1));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_ServerIgnored), size_t(1));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_WatchIgnored), size_t(1));

    // Each is still a recognised field, so the generic warning stays quiet and
    // the specific one is the only thing the user has to read.
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
}

// An absent field must stay silent; otherwise every config without a dev
// server would get four warnings on every build.
TEST(GuchhoConfig, UnsupportedFieldsQuietWhenAbsentOrNull)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"root\":null,\"server\":null,\"watch\":null,\"build\":{\"minify\":true}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_RootIgnored), size_t(0));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_ServerIgnored), size_t(0));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_WatchIgnored), size_t(0));
}

// ---------------------------------------------------------------------------
// A banner from a config file is a banner, not an unknown field
// ---------------------------------------------------------------------------

// Every buffered message's text, so a test can say what the user would have
// read rather than only how many warnings fired.
std::string AllMessagesText(const logger::Log& log)
{
    std::string all;
    for (const auto& msg : log.peek()) {
        all += msg.data.text;
        all += "\n";
    }
    return all;
}

// The plain spelling: one string, which is what "--banner=text" also means, so
// it lands on the JavaScript half of the record the API validates.
TEST(GuchhoConfig, BannerStringIsAccepted)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"build\":{\"entry\":\"src/main.js\",\"banner\":\"/*! themed */\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_EQ(result.builds[0].opts.Banner.at("js"), std::string("/*! themed */"));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
}

// The record spelling: one entry per output kind, the same shape the
// "--banner:js=" flags build, kept per key rather than collapsed.
TEST(GuchhoConfig, BannerRecordIsAccepted)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"build\":{\"entry\":\"src/main.js\","
               "\"banner\":{\"js\":\"/*! j */\",\"css\":\"/*! c */\"}}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_EQ(result.builds[0].opts.Banner.at("js"), std::string("/*! j */"));
    EXPECT_EQ(result.builds[0].opts.Banner.at("css"), std::string("/*! c */"));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
}

// A banner of a shape neither spelling accepts is read and not guessed at:
// the field itself is known, so the generic unknown-field warning has nothing
// to say, and the value is dropped the way a non-boolean "minify" is dropped.
TEST(GuchhoConfig, BannerOfAnotherShapeIsSilentlyIgnored)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "{\"build\":{\"banner\":5}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_TRUE(result.builds[0].opts.Banner.empty());
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
}

// A typo is still a typo, and the name the user has to fix is in the text:
// the full dotted path, so "bundile" is not mistaken for a top-level field.
TEST(GuchhoConfig, UnknownBuildFieldsNameThemselves)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "{\"build\":{\"bundile\":true,\"indent\":4}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(2));

    const std::string text = AllMessagesText(log);
    EXPECT_NE(text.find("build.bundile"), std::string::npos) << "text was: [" << text << "]";
    EXPECT_NE(text.find("build.indent"), std::string::npos) << "text was: [" << text << "]";
}

// ---------------------------------------------------------------------------
// A config file may turn bundling on
// ---------------------------------------------------------------------------

// The field is read the way "splitting" is read: a boolean in, a boolean
// stored, and no warning of any kind — neither the generic unknown-field one
// (the name is known) nor the old "not read" one (it is a lie now).
TEST(GuchhoConfig, ConfigBundleIsRead)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "{\"build\":{\"entry\":\"a.js\",\"bundle\":true}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_TRUE(result.builds[0].opts.Bundle);
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
}

// The off half is accepted just as quietly. What the resolver does with it is
// a resolution's answer — an off means "leave the default alone", so there is
// no message telling anybody their "false" did something it did not.
TEST(GuchhoConfig, ConfigBundleFalseIsAccepted)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "{\"build\":{\"entry\":\"a.js\",\"bundle\":false}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_FALSE(result.builds[0].opts.Bundle);
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
}

// A value that is not a boolean is dropped rather than guessed at — a field
// that means one thing must not be invented an answer — and the field itself
// is known, so the generic warning has nothing to say either way.
TEST(GuchhoConfig, ConfigBundleOfAnotherShapeIsSilentlyIgnored)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "{\"build\":{\"bundle\":\"yes\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_FALSE(result.builds[0].opts.Bundle);
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
}

// ---------------------------------------------------------------------------
// Multiple configurations (array root)
// ---------------------------------------------------------------------------

// An array root is one build per element, each resolved on its own.
TEST(GuchhoConfig, ArrayRootProducesOneBuildPerElement)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "[{\"build\":{\"outdir\":\"one\",\"minify\":false}},"
               " {\"build\":{\"outdir\":\"two\",\"minify\":true}}]");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(2));

    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, std::string("/project/one"));
    EXPECT_FALSE(result.builds[0].opts.MinifyWhitespace);
    EXPECT_EQ(result.builds[1].opts.AbsOutputDir, std::string("/project/two"));
    EXPECT_TRUE(result.builds[1].opts.MinifyWhitespace);
}

// Nothing one element says may leak into another. The two elements disagree
// about minification on purpose, so a shared struct would show up here as the
// second build taking the first one's answer.
TEST(GuchhoConfig, ArrayElementsDoNotShareState)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "[{\"build\":{\"minify\":false}}, {\"build\":{\"outdir\":\"two\",\"minify\":true}}]");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    ASSERT_EQ(result.builds.size(), size_t(2));
    EXPECT_FALSE(result.builds[0].opts.MinifyWhitespace);
    EXPECT_TRUE(result.builds[1].opts.MinifyWhitespace);
    EXPECT_TRUE(result.builds[1].opts.MinifySyntax);
}

// "format" is recorded per element, so the HTML-entry warning can tell which
// configuration actually named one.
TEST(GuchhoConfig, ArrayFormatRecordedPerElement)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "[{\"build\":{\"format\":\"iife\"}}, {\"build\":{\"outdir\":\"two\"}}]");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    ASSERT_EQ(result.builds.size(), size_t(2));
    EXPECT_EQ(result.builds[0].opts.OutputFormat, config::Format::kIIFE);
    EXPECT_TRUE(result.builds[0].opts.FormatFromConfig);
    EXPECT_EQ(result.builds[1].opts.OutputFormat, config::Format::kESModule);
    EXPECT_FALSE(result.builds[1].opts.FormatFromConfig);
}

// Each element owns its processed defines, and "opts.Defines" points at its own
// storage rather than at a shared or dangling object.
TEST(GuchhoConfig, ArrayElementsHaveIndependentDefines)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "[{\"define\":{\"A\":\"1\"}}, {\"define\":{\"B\":\"2\"}}]");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    ASSERT_EQ(result.builds.size(), size_t(2));
    EXPECT_TRUE(result.builds[0].defines_owned != nullptr);
    EXPECT_TRUE(result.builds[1].defines_owned != nullptr);
    EXPECT_TRUE(result.builds[0].opts.Defines == result.builds[0].defines_owned.get());
    EXPECT_TRUE(result.builds[1].opts.Defines == result.builds[1].defines_owned.get());
    EXPECT_TRUE(result.builds[0].opts.Defines != result.builds[1].opts.Defines);
}

// An element without "build.entry" falls back to the default entry on its own;
// the element beside it keeps the entry it named.
TEST(GuchhoConfig, ArrayElementWithoutEntryGetsDefaultEntry)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "[{\"build\":{\"entry\":\"app.js\"}}, {\"build\":{\"outdir\":\"two\"}}]");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    ASSERT_EQ(result.builds.size(), size_t(2));
    ASSERT_EQ(result.builds[0].entry_points.size(), size_t(1));
    EXPECT_EQ(result.builds[0].entry_points[0].InputPath, std::string("/project/app.js"));
    EXPECT_TRUE(result.builds[0].entry_from_config);
    ASSERT_EQ(result.builds[1].entry_points.size(), size_t(1));
    EXPECT_EQ(result.builds[1].entry_points[0].InputPath, std::string("/project/index.html"));
    EXPECT_FALSE(result.builds[1].entry_from_config);
}

// A config file whose root is an array is found where a single config would
// have been; discovery still stops at the first config it may use.
TEST(GuchhoConfig, ArrayRootStopsDiscovery)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "[{\"build\":{\"outdir\":\"a\"}},{}]");
    fs.SetFile("/project/src/index.html", "");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result =
        resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project/src");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(2));
    // The second element named nothing, so it keeps the built-in output
    // directory rooted at the discovery start.
    EXPECT_EQ(result.builds[1].opts.AbsOutputDir, std::string("/project/src/dist"));
}

// A rejected config is rejected whole: no configurations, a logged diagnostic,
// and "parse_error" set.
TEST(GuchhoConfig, EmptyArrayIsParseError)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "[]");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_TRUE(result.parse_error);
    EXPECT_TRUE(result.builds.empty());
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_EmptyArray), size_t(1));
}

TEST(GuchhoConfig, ScalarRootIsParseError)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "\"just a string\"");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_TRUE(result.parse_error);
    EXPECT_TRUE(result.builds.empty());
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_InvalidRoot), size_t(1));
}

TEST(GuchhoConfig, ArrayElementNotObjectIsParseError)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "[{\"build\":{\"outdir\":\"ok\"}}, 42]");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_TRUE(result.parse_error);
    EXPECT_TRUE(result.builds.empty());
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_InvalidArrayElement), size_t(1));
}

// The JS loader shares the text path, so an array returned from
// "guchho.config.js" resolves the same way.
TEST(GuchhoConfig, JSArrayRootProducesMultipleBuilds)
{
    TestFs fs;
    JSLoaderGuard guard(&FakeJSLoader);
    fs.SetFile("/project/guchho.config.js",
               "[{\"build\":{\"outdir\":\"js-one\"}}, {\"build\":{\"outdir\":\"js-two\"}}]");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(2));
    EXPECT_EQ(result.builds[0].opts.AbsOutputDir, std::string("/project/js-one"));
    EXPECT_EQ(result.builds[1].opts.AbsOutputDir, std::string("/project/js-two"));
}

// ---------------------------------------------------------------------------
// define: the substitutions a project sets for itself
// ---------------------------------------------------------------------------

// The canonical spelling. "define" is a flag's name, and every other option a
// flag accepts lives under "build", so this is where it belongs — and where a
// config written from the flag documentation is written.
TEST(GuchhoConfig, BuildDefineIsAccepted)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"build\":{\"entry\":\"src/main.js\","
               "\"define\":{\"__DEV__\":\"false\",\"process.env.NODE_ENV\":\"\\\"production\\\"\"}}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));

    // The raw text the API re-validates through the same pass a "--define:"
    // flag goes through, exactly as it was written.
    const auto& texts = result.builds[0].opts.DefineTexts;
    ASSERT_EQ(texts.size(), size_t(2));
    EXPECT_EQ(texts.at("__DEV__"), std::string("false"));
    EXPECT_EQ(texts.at("process.env.NODE_ENV"), std::string("\"production\""));

    // And the parsed table a reader consuming the config directly points at.
    EXPECT_TRUE(result.builds[0].defines_owned != nullptr);
    EXPECT_TRUE(result.builds[0].opts.Defines != nullptr);
}

// The top-level spelling is the alias, and it is still read: a config written
// before "build.define" existed keeps working.
TEST(GuchhoConfig, TopLevelDefineAliasIsAccepted)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "{\"define\":{\"__DEV__\":\"true\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    ASSERT_EQ(result.builds.size(), size_t(1));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_AliasIgnored), size_t(0));
    ASSERT_EQ(result.builds[0].opts.DefineTexts.size(), size_t(1));
    EXPECT_EQ(result.builds[0].opts.DefineTexts.at("__DEV__"), std::string("true"));
}

// Both spellings at once: the canonical one wins, the alias is reported
// rather than appended — merging them would double every key the two share
// and silently pick a winner for the keys they do not.
TEST(GuchhoConfig, BuildDefineWinsOverTopLevelAlias)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "{\"build\":{\"define\":{\"A\":\"1\"}},\"define\":{\"B\":\"2\"}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoConfig_AliasIgnored), size_t(1));
    const std::string text = AllMessagesText(log);
    EXPECT_NE(text.find("build.define"), std::string::npos) << "text was: [" << text << "]";

    const auto& texts = result.builds[0].opts.DefineTexts;
    ASSERT_EQ(texts.size(), size_t(1));
    EXPECT_EQ(texts.at("A"), std::string("1"));
}

// The value is the replacement expression as written, so a value that is not
// one is reported where it was written and left out of both tables — the
// field is known, so the generic unknown-field warning has nothing to say.
TEST(GuchhoConfig, UnsupportedDefineValueUnderBuildIsReported)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json", "{\"build\":{\"define\":{\"FLAG\":{}}}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_UnknownField), size_t(0));
    EXPECT_EQ(CountMessagesOfID(log, logger::MsgID::kGuchhoJSON_InvalidFormat), size_t(1));
    EXPECT_TRUE(result.builds[0].opts.DefineTexts.empty());
}

// Each configuration carries its own substitutions, so two builds in one
// config file cannot see each other's defines.
TEST(GuchhoConfig, ArrayElementsHaveIndependentDefineTexts)
{
    TestFs fs;
    fs.SetFile("/project/guchho.config.json",
               "[{\"build\":{\"define\":{\"A\":\"1\"}}}, {\"build\":{\"define\":{\"B\":\"2\"}}}]");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project");

    ASSERT_EQ(result.builds.size(), size_t(2));
    ASSERT_EQ(result.builds[0].opts.DefineTexts.size(), size_t(1));
    ASSERT_EQ(result.builds[1].opts.DefineTexts.size(), size_t(1));
    EXPECT_EQ(result.builds[0].opts.DefineTexts.at("A"), std::string("1"));
    EXPECT_EQ(result.builds[1].opts.DefineTexts.at("B"), std::string("2"));
}
