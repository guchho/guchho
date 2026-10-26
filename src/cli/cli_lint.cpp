#include "guchho/cli.hpp"

#include "guchho/helpers.hpp"
#include "guchho/lint.hpp"
#include "guchho/logger.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace guchho::cli {

namespace {

static std::atomic<bool> g_lint_interrupted{false};

void LintSignalHandler(int) {
    g_lint_interrupted.store(true, std::memory_order_relaxed);
}

struct LintArgs {
    lint::Options options;
    bool help{};
    bool format_json{};
};

bool TakeValue(const std::vector<std::string>& args, size_t& i,
               std::string_view option, std::string& value) {
    const std::string& arg = args[i];
    if (arg == option) {
        if (i + 1 >= args.size()) return false;
        value = args[++i];
        return true;
    }
    if (arg.starts_with(option) && arg.size() > option.size() &&
        arg[option.size()] == '=') {
        value = arg.substr(option.size() + 1);
        return !value.empty();
    }
    return false;
}

std::optional<ErrorWithNote> ParseLintArgs(
    const std::vector<std::string>& args, LintArgs& out) {
    out = LintArgs{};

    for (size_t i = 1; i < args.size(); i++) {
        const std::string& arg = args[i];
        bool consumed = false;

        std::string value;
        if (TakeValue(args, i, "--format", value)) {
            if (value == "json") {
                out.format_json = true;
                out.options.json = true;
            } else if (value == "human" || value == "pretty" ||
                       value == "stylish") {
                out.format_json = false;
                out.options.json = false;
            } else {
                return MakeErrorWithNote(
                    "Invalid lint format " + helpers::QuoteSingle(value, true),
                    "Valid formats are \"human\", \"stylish\", and \"json\".");
            }
            consumed = true;
        }

        if (!consumed && TakeValue(args, i, "--config", value)) {
            out.options.config_path = value;
            consumed = true;
        }

        if (!consumed && TakeValue(args, i, "--max-warnings", value)) {
            std::optional<ErrorWithNote> error;
            const int parsed = parseInt(
                value, arg, "Use a non-negative whole number.", error);
            if (error.has_value()) return error;
            if (parsed < 0) {
                return MakeErrorWithNote(
                    "Maximum warnings cannot be negative in " +
                        helpers::QuoteSingle(arg, true),
                    "Use a non-negative whole number.");
            }
            out.options.max_warnings = parsed;
            consumed = true;
        }

        if (!consumed && TakeValue(args, i, "--rule", value)) {
            const size_t separator = value.find_first_of(":=");
            if (separator == std::string::npos || separator == 0) {
                return MakeErrorWithNote(
                    "Missing \":\" in " + helpers::QuoteSingle(value, true),
                    "Use \"--rule=<name>:<level>\", for example "
                    "\"--rule=no-console:error\".");
            }
            const std::string name = value.substr(0, separator);
            const std::string level_text = value.substr(separator + 1);
            std::optional<lint::Severity> severity = lint::ParseSeverity(level_text);
            if (!severity.has_value()) {
                return MakeErrorWithNote(
                    "Invalid lint level " + helpers::QuoteSingle(level_text, true),
                    "Valid levels are \"off\", \"warning\", and \"error\".");
            }
            if (!lint::RuleFromName(name).has_value()) {
                std::string note = "Use \"guchho lint --help\" to list the rules.";
                if (std::string suggestion = lint::SuggestRuleName(name);
                    !suggestion.empty()) {
                    note = "Did you mean \"" + suggestion + "\"? " + note;
                }
                return MakeErrorWithNote(
                    "Unknown lint rule " + helpers::QuoteSingle(name, true),
                    note);
            }
            out.options.rule_overrides[name] = *severity;
            consumed = true;
        }

        if (consumed) continue;

        if (arg == "--help") {
            out.help = true;
            continue;
        }
        if (arg == "--fix") {
            out.options.fix = true;
            continue;
        }
        if (arg == "--fix-dry-run") {
            out.options.fix_dry_run = true;
            continue;
        }
        if (arg == "--watch") {
            out.options.watch = true;
            continue;
        }
        if (arg == "--quiet") {
            out.options.quiet = true;
            continue;
        }
        if (arg == "--json") {
            out.format_json = true;
            out.options.json = true;
            continue;
        }
        if (arg == "--") {
            for (size_t j = i + 1; j < args.size(); j++) {
                out.options.paths.push_back(args[j]);
            }
            break;
        }
        if (arg.starts_with("-")) {
            return MakeErrorWithNote(
                "Invalid lint flag: " + helpers::QuoteSingle(arg, true),
                "Run \"guchho lint --help\" to see the supported flags.");
        }
        out.options.paths.push_back(arg);
    }

    return std::nullopt;
}

void ApplyOverrides(lint::Config& config, const lint::Options& options) {
    for (const auto& [name, severity] : options.rule_overrides) {
        config.rules[name] = severity;
    }
}

bool LoadLintConfig(filesystem::Fs& fs, const std::string& cwd,
                    const lint::Options& options, lint::Config& config,
                    std::string& error) {
    if (!lint::LoadConfig(fs, cwd, options.config_path, config, error)) {
        return false;
    }
    ApplyOverrides(config, options);
    return true;
}

bool LintOnce(filesystem::Fs& fs, const std::string& cwd,
              const lint::Options& options, lint::Config& config,
              std::string& error) {
    const std::string display_config =
        config.path.empty()
            ? std::string()
            : lint::detail::RelativeDisplayPath(fs, cwd, config.path);
    (void)display_config;

    lint::Project project;
    project.config = config;
    project.options = options;
    project.options.cwd = cwd;

    lint::Report report;
    if (!lint::LintProject(fs, project, report, error)) {
        return false;
    }

    if (options.json) {
        lint::PrintJsonReport(std::cout, report, options);
    } else {
        lint::PrintReport(std::cout, report, options);
    }

    if (!options.json && options.max_warnings >= 0 &&
        report.warnings > options.max_warnings) {
        std::cerr << "guchho lint: too many warnings (" << report.warnings
                  << "). Maximum allowed is " << options.max_warnings << ".\n";
    }

    if (report.errors > 0) return false;
    if (options.max_warnings >= 0 && report.warnings > options.max_warnings) {
        return false;
    }
    return true;
}

std::string WatchSignature(filesystem::Fs& fs, const lint::Config& config,
                           const lint::Options& options,
                           const std::string& cwd) {
    std::vector<std::string> files;
    std::string error;
    if (!lint::detail::DiscoverFiles(fs, config, options.paths, cwd, files,
                                     error)) {
        return "error:" + error;
    }
    std::string signature;
    signature.reserve(files.size() * 48);
    for (const std::string& file : files) {
        filesystem::ModKeyResult key = fs.ModKey(file);
        signature += file;
        signature += ':';
        if (key.Ok()) {
            signature += std::to_string(key.value.size);
            signature += ':';
            signature += std::to_string(key.value.mtime_sec);
            signature += ':';
            signature += std::to_string(key.value.mtime_nsec);
        } else {
            signature += "?";
        }
        signature += '\n';
    }
    if (!config.path.empty()) {
        filesystem::ModKeyResult key = fs.ModKey(config.path);
        signature += "config:";
        if (key.Ok()) {
            signature += std::to_string(key.value.size);
            signature += ':';
            signature += std::to_string(key.value.mtime_sec);
            signature += ':';
            signature += std::to_string(key.value.mtime_nsec);
        }
        signature += '\n';
    }
    return signature;
}

} // namespace

int runLint(const std::vector<std::string>& args) {
    LintArgs parsed;
    if (auto error = ParseLintArgs(args, parsed)) {
        logger::PrintErrorWithNoteToStderr(args, error->text, error->note);
        return static_cast<int>(ExitCode::kCLIUsageError);
    }
    if (parsed.help) {
        printLintHelp(std::cout);
        return static_cast<int>(ExitCode::kSuccess);
    }

#if defined(GUCHHO_WASM)
    if (parsed.options.watch) {
        logger::PrintErrorWithNoteToStderr(
            args, "lint --watch is not supported by the WASM build",
            "Install the native binary (npm i -D @guchho/linux-x64, "
            "@guchho/darwin-arm64 or @guchho/win32-x64).");
        return static_cast<int>(ExitCode::kCLIUsageError);
    }
#endif

    filesystem::RealFsOptions fs_options;
    fs_options.do_not_cache = parsed.options.watch;
    std::string fs_error;
    std::unique_ptr<filesystem::Fs> fs =
        filesystem::MakeRealFS(fs_options, fs_error);
    if (!fs) {
        std::cerr << "guchho lint: " << fs_error << "\n";
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

    const std::string cwd = fs->Cwd();
    parsed.options.cwd = cwd;
    parsed.options.color = logger::GetTerminalInfo(1).use_color_escapes;

    lint::Config config;
    std::string error;
    if (!LoadLintConfig(*fs, cwd, parsed.options, config, error)) {
        std::cerr << "guchho lint: " << error << "\n";
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

    if (!parsed.options.watch) {
        const bool ok = LintOnce(*fs, cwd, parsed.options, config, error);
        if (!error.empty()) {
            std::cerr << "guchho lint: " << error << "\n";
            return static_cast<int>(ExitCode::kCLIUsageError);
        }
        return static_cast<int>(ok ? ExitCode::kSuccess
                                   : ExitCode::kBuildFailure);
    }

    g_lint_interrupted.store(false, std::memory_order_relaxed);
    std::signal(SIGINT, LintSignalHandler);

    std::string signature = WatchSignature(*fs, config, parsed.options, cwd);
    LintOnce(*fs, cwd, parsed.options, config, error);
    if (!error.empty()) {
        std::cerr << "guchho lint: " << error << "\n";
    }
    if (!parsed.options.quiet) {
        std::cout << "Watching for changes...\n";
    }

    while (!g_lint_interrupted.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        lint::Config reloaded;
        std::string reload_error;
        if (!LoadLintConfig(*fs, cwd, parsed.options, reloaded, reload_error)) {
            std::cerr << "guchho lint: " << reload_error << "\n";
            continue;
        }
        const std::string next =
            WatchSignature(*fs, reloaded, parsed.options, cwd);
        if (next == signature) continue;
        config = std::move(reloaded);
        signature = next;

        error.clear();
        LintOnce(*fs, cwd, parsed.options, config, error);
        if (!error.empty()) {
            std::cerr << "guchho lint: " << error << "\n";
        }
    }

    return static_cast<int>(g_lint_interrupted.load(std::memory_order_relaxed)
                                ? ExitCode::kInterrupted
                                : ExitCode::kSuccess);
}

} // namespace guchho::cli
