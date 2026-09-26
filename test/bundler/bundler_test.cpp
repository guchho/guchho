#include "test/helpers/bundler_test.hpp"
#include "test/helpers/filesystem_test.hpp"

#include "test/guchho_test.hpp"
#include "guchho/bundler.hpp"
#include "guchho/cache.hpp"
#include "guchho/resolver.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>

namespace bundler::test {

// ---------------------------------------------------------------------------
// Static members
// ---------------------------------------------------------------------------

std::atomic<bool>    Suite::update_snapshots_{false};
std::vector<Suite*>  Suite::global_suites_;

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

std::string Win2Unix(std::string_view p) {
    std::string result(p);
    if (result.size() >= 3 && result[0] == 'C' && result[1] == ':' && result[2] == '\\') {
        result.erase(0, 2);
    }
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

std::string Unix2Win(std::string_view p) {
    std::string result(p);
    std::replace(result.begin(), result.end(), '/', '\\');
    if (!result.empty() && result[0] == '\\') {
        result = "C:" + result;
    }
    return result;
}

// ---------------------------------------------------------------------------
// FormatLog
// ---------------------------------------------------------------------------

std::string FormatLog(const std::vector<guchho::logger::Msg>& msgs,
                      bool include_source) {
    std::ostringstream text;
    guchho::logger::OutputOptions opts;
    opts.include_source = include_source;
    guchho::logger::TerminalInfo  ti;
    for (const auto& msg : msgs) {
        text << msg.String(opts, ti);
    }
    return text.str();
}

// ---------------------------------------------------------------------------
// HasErrors
// ---------------------------------------------------------------------------

static bool HasErrors(const std::vector<guchho::logger::Msg>& msgs) {
    for (const auto& msg : msgs) {
        if (msg.kind == guchho::logger::MsgKind::kError) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// ExpectBundled (runs both Unix + Windows)
// ---------------------------------------------------------------------------

void Suite::ExpectBundled(Bundled args) {
    // Prepare Windows args before moving args into the Unix run,
    // since std::move leaves the original in a moved-from state.
    Bundled win_args;
    win_args.files.reserve(args.files.size());
    for (auto& [k, v] : args.files) {
        win_args.files[Unix2Win(k)] = v;
    }
    win_args.entry_paths.reserve(args.entry_paths.size());
    for (const auto& ep : args.entry_paths) {
        win_args.entry_paths.push_back(Unix2Win(ep));
    }
    win_args.entry_paths_advanced.reserve(args.entry_paths_advanced.size());
    for (const auto& ep : args.entry_paths_advanced) {
        auto win_ep = ep;
        win_ep.InputPath = Unix2Win(ep.InputPath);
        if (!win_ep.OutputPath.empty() && win_ep.OutputPath[0] == '/') {
            win_ep.OutputPath = Unix2Win(win_ep.OutputPath);
        }
        win_args.entry_paths_advanced.push_back(std::move(win_ep));
    }
    win_args.abs_working_dir = Unix2Win(args.abs_working_dir);
    win_args.guchho_config  = args.guchho_config;
    win_args.guchho_config_path = Unix2Win(args.guchho_config_path);
    win_args.expected_scan_log = args.expected_scan_log;
    win_args.expected_compile_log = args.expected_compile_log;
    win_args.debug_logs = args.debug_logs;
    win_args.source_logs = args.source_logs;

    win_args.options = args.options;
    for (auto& p : win_args.options.InjectPaths) {
        p = Unix2Win(p);
    }
    for (auto& [k, v] : win_args.options.PackageAliases) {
        if (!v.empty() && v[0] == '/') {
            v = Unix2Win(v);
        }
    }
    // Rebuild PostResolve.Exact with converted keys
    {
        auto old_exact = std::move(
            win_args.options.ExternalSettingsData.PostResolve.Exact);
        win_args.options.ExternalSettingsData.PostResolve.Exact.clear();
        for (auto& [k, v] : old_exact) {
            std::string new_key = k;
            if (!new_key.empty() && new_key[0] == '/') {
                new_key = Unix2Win(new_key);
            }
            win_args.options.ExternalSettingsData.PostResolve.Exact[new_key] = v;
        }
    }
    win_args.options.AbsOutputFile = Unix2Win(args.options.AbsOutputFile);
    win_args.options.AbsOutputBase = Unix2Win(args.options.AbsOutputBase);
    win_args.options.AbsOutputDir  = Unix2Win(args.options.AbsOutputDir);
    win_args.options.TSConfigPath  = Unix2Win(args.options.TSConfigPath);

    ExpectBundledImpl(std::move(args), guchho::filesystem::MockKind::kUnix);
    ExpectBundledImpl(std::move(win_args),
                      guchho::filesystem::MockKind::kWindows);
}

void Suite::ExpectBundledUnix(Bundled args) {
    ExpectBundledImpl(std::move(args),
                      guchho::filesystem::MockKind::kUnix);
}

void Suite::ExpectBundledWindows(Bundled args) {
    ExpectBundledImpl(std::move(args),
                      guchho::filesystem::MockKind::kWindows);
}

// ---------------------------------------------------------------------------
// ExpectBundledImpl (core)
// ---------------------------------------------------------------------------

static std::string ParentDir(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

void Suite::ExpectBundledImpl(Bundled args,
                              guchho::filesystem::MockKind fs_kind) {
    const char* sub_name =
        (fs_kind == guchho::filesystem::MockKind::kWindows)
            ? "Windows" : "Unix";

    guchho::test::State().current += std::string(".") + sub_name;


    // The output directory is the parent of a single named output file. It is
    // derived both for an "AbsOutputFile" set by the scenario and for one
    // that a "guchho.json" supplies through "build.outfile", so that the
    // config-driven case lands in the same place as the equivalent option.
    auto derive_output_dir_from_output_file = [&]() {
        if (args.options.AbsOutputFile.empty()) {
            return;
        }
        if (fs_kind == guchho::filesystem::MockKind::kWindows) {
            args.options.AbsOutputDir =
                ParentDir(Win2Unix(args.options.AbsOutputFile));
            args.options.AbsOutputDir =
                Unix2Win(args.options.AbsOutputDir);
        } else {
            args.options.AbsOutputDir =
                ParentDir(args.options.AbsOutputFile);
        }
    };

    if (args.options.ExtensionOrder.empty()) {
        args.options.ExtensionOrder = {
            ".tsx", ".ts", ".jsx", ".js", ".css", ".json"};
    }
    derive_output_dir_from_output_file();
    if (args.options.BuildMode == guchho::config::Mode::kBundle ||
        (args.options.BuildMode == guchho::config::Mode::kConvertFormat &&
         args.options.OutputFormat == guchho::config::Format::kIIFE)) {
        args.options.TreeShaking = true;
    }
    if (args.options.BuildMode == guchho::config::Mode::kBundle &&
        args.options.OutputFormat == guchho::config::Format::kPreserve) {
        args.options.OutputFormat = guchho::config::Format::kESModule;
    }

    auto log_kind = args.debug_logs
                    ? guchho::logger::DeferLogKind::kDeferLogAll
                    : guchho::logger::DeferLogKind::kDeferLogNoVerboseOrDebug;

    std::vector<guchho::config::EntryPoint> entry_points;
    entry_points.reserve(args.entry_paths.size() +
                         args.entry_paths_advanced.size());
    for (const auto& path : args.entry_paths) {
        guchho::config::EntryPoint ep;
        ep.InputPath = path;
        entry_points.push_back(ep);
    }
    for (const auto& ep : args.entry_paths_advanced) {
        entry_points.push_back(ep);
    }

    if (args.abs_working_dir.empty()) {
        args.abs_working_dir =
            (fs_kind == guchho::filesystem::MockKind::kWindows)
                ? "C:\\" : "/";
    }
    if (args.options.AbsOutputDir.empty()) {
        args.options.AbsOutputDir = args.abs_working_dir;
    }

    // ---- Scan phase ----
    static const std::unordered_map<guchho::logger::MsgID, guchho::logger::LogLevel> empty_overrides;
    auto log = guchho::logger::NewDeferLog(log_kind, empty_overrides);
    auto caches = guchho::cache::MakeCacheSet();
    auto mock_fs = guchho::test::MakeMockFS(
        args.files, fs_kind, args.abs_working_dir);
    args.options.OmitRuntimeForTests = true;

    // ---- Project config ----
    // A "guchho.json" in the scenario is mapped onto the options exactly the
    // way the config loader maps a real file: the JSON overlays the options
    // built above, and "build.entry" adds entry points. The loaded config is
    // kept alive for the whole run because "opts.Defines" points into the
    // processed defines it owns.
    std::optional<guchho::resolver::GuchhoConfig> guchho_config;
    if (!args.guchho_config.empty()) {
        std::string config_path = args.guchho_config_path;
        if (config_path.empty()) {
            config_path =
                mock_fs->Join({args.abs_working_dir, "guchho.json"});
        }
        guchho_config = guchho::resolver::LoadGuchhoConfigFromText(
            log, caches->json_cache, *mock_fs, args.options,
            args.guchho_config, config_path);
        args.options = guchho_config->opts;
        derive_output_dir_from_output_file();
        for (auto& entry_point : guchho_config->entry_points) {
            entry_points.push_back(std::move(entry_point));
        }
    }

    auto bundle = guchho::bundler::ScanBundle(
        guchho::config::APICall::kBuildCall, log, *mock_fs, *caches,
        entry_points, args.options, nullptr);


    auto strip_leading_newline = [](std::string s) {
        if (!s.empty() && s[0] == '\r') s.erase(0, 1);
        if (!s.empty() && s[0] == '\n') s.erase(0, 1);
        return s;
    };

    auto scan_msgs = log.done();
    {
        std::string actual = FormatLog(scan_msgs, args.source_logs);
        if (actual != strip_leading_newline(args.expected_scan_log)) {
            std::printf(
                "Scan log mismatch for %s\n"
                "  Expected:\n%s\n  Actual:\n%s\n",
                guchho::test::State().current.c_str(),
                args.expected_scan_log.c_str(), actual.c_str());
            guchho::test::State().current_failed = true;
            throw guchho::test::TestAbort{};
        }
    }

    if (HasErrors(scan_msgs)) {
        return;
    }

    // ---- Compile phase ----
    auto compile_log = guchho::logger::NewDeferLog(log_kind, empty_overrides);
    guchho::helpers::Timer timer{"bundler_test"};
    std::unordered_map<std::string, bool> mangle_cache;
    auto [results, metafile_json] = bundle.Compile(
        compile_log, timer, mangle_cache);
    auto compile_msgs = compile_log.done();
    {
        std::string actual = FormatLog(compile_msgs, args.source_logs);
        if (actual != strip_leading_newline(args.expected_compile_log)) {
            std::printf(
                "Compile log mismatch for %s\n"
                "  Expected:\n%s\n  Actual:\n%s\n",
                guchho::test::State().current.c_str(),
                strip_leading_newline(args.expected_compile_log).c_str(), actual.c_str());
            guchho::test::State().current_failed = true;
            throw guchho::test::TestAbort{};
        }
    }

    if (HasErrors(compile_msgs)) {
        return;
    }

    // ---- Format output ----
    std::string generated;
    for (auto& result : results) {
        if (!generated.empty()) {
            generated += '\n';
        }
        std::string abs_path = result.abs_path;
        if (fs_kind == guchho::filesystem::MockKind::kWindows) {
            abs_path = Win2Unix(abs_path);
        }
        generated += "---------- " + abs_path + " ----------\n";
        generated.append(result.contents.begin(), result.contents.end());
    }
    if (!metafile_json.empty()) {
        generated += "---------- metafile.json ----------\n";
        generated += metafile_json;
    }

    // Build snapshot test name (strip ".Unix"/".Windows" suffixes)
    std::string test_name;
    {
        std::string full = guchho::test::State().current;
        while (true) {
            auto dot_pos = full.rfind('.');
            if (dot_pos == std::string::npos) break;
            std::string suffix = full.substr(dot_pos + 1);
            if (suffix == "Unix" || suffix == "Windows") {
                full = full.substr(0, dot_pos);
            } else {
                break;
            }
        }
        test_name = full;
    }

    CompareSnapshot(test_name, generated);
}

// ---------------------------------------------------------------------------
// Snapshot comparison
// ---------------------------------------------------------------------------

static const std::string kSnapshotsDir = "snapshots";
static const std::string kSnapshotSplitter =
    "\n================================================================================\n";

void Suite::CompareSnapshot(const std::string& test_name,
                            const std::string& generated) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (path_.empty()) {
        path_ = kSnapshotsDir + "/snapshots_" + name_ + ".txt";
        generated_snapshots_.clear();
        expected_snapshots_.clear();

        std::ifstream in(path_);
        if (in.is_open()) {
            std::string contents((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
            // Normalize CRLF -> LF
            {
                std::string normalized;
                normalized.reserve(contents.size());
                for (size_t i = 0; i < contents.size(); ++i) {
                    if (contents[i] == '\r' &&
                        i + 1 < contents.size() &&
                        contents[i + 1] == '\n') {
                        normalized += '\n';
                        ++i;
                    } else {
                        normalized += contents[i];
                    }
                }
                contents = std::move(normalized);
            }

            size_t pos = 0;
            while (pos < contents.size()) {
                auto sep = contents.find(kSnapshotSplitter, pos);
                std::string part;
                if (sep == std::string::npos) {
                    part = contents.substr(pos);
                    pos = contents.size();
                } else {
                    part = contents.substr(pos, sep - pos);
                    pos = sep + kSnapshotSplitter.size();
                }
                auto nl = part.find('\n');
                if (nl != std::string::npos) {
                    expected_snapshots_[part.substr(0, nl)] =
                        part.substr(nl + 1);
                } else {
                    expected_snapshots_[part] = "";
                }
            }
        }

#ifdef _WIN32
        char* update_snapshots_value = nullptr;
        size_t update_snapshots_size = 0;
        const bool update_snapshots =
            _dupenv_s(&update_snapshots_value, &update_snapshots_size,
                      "UPDATE_BUNDLER_SNAPSHOTS") == 0 &&
            update_snapshots_value != nullptr;
        std::free(update_snapshots_value);
#else
#if defined(__GNUC__) && !defined(_MSC_VER)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        const bool update_snapshots =
            std::getenv("UPDATE_BUNDLER_SNAPSHOTS") != nullptr;
#if defined(__GNUC__) && !defined(_MSC_VER)
#pragma GCC diagnostic pop
#endif
#endif
        if (update_snapshots) {
            update_snapshots_.store(true, std::memory_order_relaxed);
        }
        global_suites_.push_back(this);
    }

    generated_snapshots_[test_name] = generated;

    if (!update_snapshots_.load(std::memory_order_relaxed)) {
        auto it = expected_snapshots_.find(test_name);
        if (it == expected_snapshots_.end()) {
            // Some snapshot files (e.g. snapshots_default.txt, ported from
            if (auto dot = test_name.find('.'); dot != std::string::npos) {
                std::string bare = test_name.substr(dot + 1);
                it = expected_snapshots_.find(bare);
                if (it == expected_snapshots_.end()) {
                    it = expected_snapshots_.find("Test" + bare);
                }
            }
        }
        if (it != expected_snapshots_.end()) {
            if (it->second != generated) {
                std::printf(
                    "Snapshot mismatch for %s\n"
                    "  Expected:\n%s\n  Generated:\n%s\n",
                    test_name.c_str(), it->second.c_str(),
                    generated.c_str());
                guchho::test::State().current_failed = true;
                throw guchho::test::TestAbort{};
            }
        } else {
            std::printf("No snapshot saved for %s\nGenerated:\n%s\n",
                        test_name.c_str(), generated.c_str());
            guchho::test::State().current_failed = true;
            throw guchho::test::TestAbort{};
        }
    }
}

void Suite::UpdateSnapshots() {
    if (!SnapshotsComplete()) {
        // A partial run (e.g. ctest's one-test-per-process invocations) must
        // never rewrite the snapshot file with an incomplete set.
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(kSnapshotsDir, ec);

    std::vector<std::string> keys;
    keys.reserve(generated_snapshots_.size());
    for (const auto& [k, _] : generated_snapshots_) {
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());

    std::ostringstream contents;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) {
            contents << kSnapshotSplitter;
        }
        contents << keys[i] << '\n'
                 << generated_snapshots_[keys[i]];
    }

    std::ofstream out(path_);
    out << contents.str();
}

bool Suite::SnapshotsComplete() const {
    for (const auto& [key, _] : expected_snapshots_) {
        if (generated_snapshots_.find(key) == generated_snapshots_.end()) {
            return false;
        }
    }
    return true;
}

bool Suite::ValidateSnapshots() {
    bool valid = true;
    for (const auto& [key, _] : expected_snapshots_) {
        if (generated_snapshots_.find(key) ==
            generated_snapshots_.end()) {
            std::printf("    %s: No test found for snapshot %s\n",
                        path_.c_str(), key.c_str());
            valid = false;
        }
    }
    return valid;
}

// ---------------------------------------------------------------------------
// Global cleanup
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Global cleanup
// ---------------------------------------------------------------------------
//
// Register a finish hook so snapshot update/validation runs inside main(),
// before any static destructors. Per-suite work only happens when this process
// generated snapshots for every expected snapshot (a "complete" run). CTest
// invokes one test per process, so normal runs never reach the per-suite
// block; only whole-suite runs (e.g. --filter=BundlerDCE) do.

namespace {

struct BundlerHookRegistrar {
    BundlerHookRegistrar() {
        guchho::test::SetFinishHook(&Suite::RunGlobalCleanup);
    }
};

BundlerHookRegistrar g_hook_registrar;

} // namespace

void Suite::RunGlobalCleanup() {
    bool ok = true;
    for (auto* s : global_suites_) {
        if (!s->SnapshotsComplete()) {
            // Partial run (one test per process via CTest): regenerate or
            // validate only when every expected snapshot was produced.
            continue;
        }
        if (update_snapshots_.load(std::memory_order_relaxed)) {
            s->UpdateSnapshots();
        } else if (!s->ValidateSnapshots()) {
            ok = false;
        }
    }
    if (!ok) {
        std::exit(1);
    }
}

} // namespace bundler::test
