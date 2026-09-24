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

    EXPECT_EQ(result.entry_points.size(), size_t(1));
    EXPECT_EQ(result.entry_points[0].InputPath, TestFs::JoinPath(root_dir, "index.html"));

    EXPECT_EQ(result.opts.AbsOutputDir, TestFs::JoinPath(root_dir, "dist"));
    EXPECT_TRUE(result.opts.AbsOutputFile.empty());

    EXPECT_EQ(result.opts.OutputFormat, config::Format::kESModule);
    EXPECT_EQ(result.opts.OutputPlatform, config::Platform::kBrowser);
    EXPECT_EQ(result.opts.OriginalTargetEnv, std::string("esnext"));

    EXPECT_TRUE(result.opts.MinifyWhitespace);
    EXPECT_TRUE(result.opts.MinifyIdentifiers);
    EXPECT_TRUE(result.opts.MinifySyntax);
    EXPECT_EQ(result.opts.SourceMapData, config::SourceMap::kNone);
    EXPECT_FALSE(result.opts.CodeSplitting);
    EXPECT_TRUE(result.opts.TreeShaking);
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
    EXPECT_EQ(result.opts.AbsOutputDir, std::string("/project/from-js"));

    // The two JSON files were never merged in and the default entry is kept.
    EXPECT_EQ(result.entry_points.size(), size_t(1));
    EXPECT_EQ(result.entry_points[0].InputPath, std::string("/project/index.html"));
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
    EXPECT_EQ(result.opts.AbsOutputDir, std::string("/project/from-config-json"));
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
    EXPECT_EQ(result.opts.AbsOutputDir, std::string("/project/root-out"));

    // Only "outdir" was overridden; the entry default stays rooted at the
    // discovery start directory.
    EXPECT_EQ(result.entry_points.size(), size_t(1));
    EXPECT_EQ(result.entry_points[0].InputPath, std::string("/project/src/index.html"));
}

// ---------------------------------------------------------------------------
// Only one config selected; configs are never merged
// ---------------------------------------------------------------------------

TEST(GuchhoConfig, OnlyOneConfigSelected)
{
    TestFs fs;
    JSLoaderGuard guard(&FakeJSLoader);
    fs.SetFile("/project/guchho.config.js", "{\"build\":{\"outdir\":\"js-out\"}}");
    fs.SetFile("/project/src/guchho.config.json",
               "{\"build\":{\"outdir\":\"json-out\",\"minify\":false}}");

    config::Options  opts;
    cache::JSONCache json_cache;
    logger::Log      log = NewLog();

    resolver::GuchhoConfig result = resolver::LoadGuchhoConfig(log, json_cache, fs, opts, "/project/src");

    // The src directory has no config; discovery walks up to /project where
    // only the JS config exists. The JSON file in "src" is never consulted.
    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.parse_error);
    EXPECT_EQ(result.config_path, std::string("/project/guchho.config.js"));
    EXPECT_EQ(result.opts.AbsOutputDir, std::string("/project/js-out"));
    EXPECT_TRUE(result.opts.MinifyWhitespace);
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
    EXPECT_EQ(result.opts.AbsOutputDir, std::string("/project/src/dist"));
    EXPECT_EQ(result.entry_points.size(), size_t(1));
    EXPECT_EQ(result.entry_points[0].InputPath, std::string("/project/src/index.html"));
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
    EXPECT_EQ(result.opts.AbsOutputDir, std::string("/project/dist"));
    EXPECT_TRUE(result.opts.MinifyWhitespace);
    EXPECT_EQ(result.entry_points.size(), size_t(1));
    EXPECT_EQ(result.entry_points[0].InputPath, std::string("/project/index.html"));
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

    EXPECT_EQ(result.opts.AbsOutputDir, std::string("/project/custom-out"));
    EXPECT_FALSE(result.opts.MinifyWhitespace);
    EXPECT_FALSE(result.opts.MinifyIdentifiers);
    EXPECT_FALSE(result.opts.MinifySyntax);

    // Everything else keeps its default.
    EXPECT_EQ(result.opts.OutputFormat, config::Format::kESModule);
    EXPECT_EQ(result.opts.OutputPlatform, config::Platform::kBrowser);
    EXPECT_EQ(result.opts.OriginalTargetEnv, std::string("esnext"));
    EXPECT_EQ(result.opts.SourceMapData, config::SourceMap::kNone);
    EXPECT_FALSE(result.opts.CodeSplitting);
    EXPECT_TRUE(result.opts.TreeShaking);
    EXPECT_TRUE(result.opts.AbsOutputFile.empty());
    EXPECT_EQ(result.entry_points.size(), size_t(1));
    EXPECT_EQ(result.entry_points[0].InputPath, std::string("/project/index.html"));
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
        js_opts = result.opts;
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
        config_json_opts = result.opts;
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
        plain_json_opts = result.opts;
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