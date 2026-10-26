#pragma once

// =============================================================================
// guchho/lint.hpp — the dedicated lint engine
// =============================================================================
//
// This is deliberately separate from config::LintOptions, which controls the
// small set of checks a production build can perform while parsing. The lint
// command has its own rule set, configuration file, discovery rules and
// reporting model; keeping those here means adding a lint rule never changes
// what a normal build does.
// =============================================================================

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"

namespace guchho::lint {

    enum class Severity : uint8_t {
        kOff,
        kWarning,
        kError,
    };

    std::optional<Severity> ParseSeverity(std::string_view text);
    std::string_view        SeverityName(Severity severity);

    enum class Rule : uint8_t {
        kNoUndef,
        kNoUnusedVars,
        kNoRedeclare,
        kNoUseBeforeDefine,
        kNoShadow,
        kNoUnreachable,
        kNoDuplicateImports,
        kNoConstantCondition,
        kValidTypeof,
        kNoConsole,
        kEqeqeq,
        kNoDebugger,
        kNoDuplicateCase,
        kNoEmpty,
        kImportNoUnresolved,
        kCount,
    };

    inline constexpr size_t kRuleCount = static_cast<size_t>(Rule::kCount);

    struct RuleInfo {
        Rule             id;
        std::string_view name;
        Severity         default_severity;
        bool             fixable;
    };

    const std::array<RuleInfo, kRuleCount>& AllRules();
    std::optional<Rule>                     RuleFromName(std::string_view name);
    std::string_view                        RuleName(Rule rule);
    std::string                             SuggestRuleName(std::string_view name);
    Severity                                DefaultSeverity(Rule rule);

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    struct Config {
        std::vector<std::string>                  include;
        std::vector<std::string>                  exclude;
        std::unordered_map<std::string, Severity> rules;
        std::unordered_map<std::string, bool>     globals;
        config::Platform                          platform{config::Platform::kBrowser};
        bool                                      report_unused_disable_directives{};
        std::string                               path;
    };

    Config DefaultConfig();
    bool   LoadConfig(filesystem::Fs& fs, const std::string& cwd,
                      const std::string& explicit_path, Config& out, std::string& error);

    // -------------------------------------------------------------------------
    // Findings and reports
    // -------------------------------------------------------------------------

    struct Diagnostic {
        std::string           path;
        std::string           rule_id;
        Severity              severity{Severity::kWarning};
        std::string           message;
        int                   line{1};
        int                   column{1};
        int                   end_line{1};
        int                   end_column{1};
        bool                  is_syntax{};
        std::vector<logger::Fix> fixes;
    };

    struct FileReport {
        std::string            path;
        std::string            source;
        std::vector<Diagnostic> diagnostics;
        int                    errors{};
        int                    warnings{};
        int                    fixes{};
    };

    struct Report {
        std::vector<FileReport> files;
        int                     errors{};
        int                     warnings{};
        int                     fixes{};
        int                     files_with_problems{};
    };

    struct Options {
        std::vector<std::string>                  paths;
        std::unordered_map<std::string, Severity> rule_overrides;
        std::string                               config_path;
        std::string                               cwd;
        int                                       max_warnings{-1};
        bool                                      fix{};
        bool                                      fix_dry_run{};
        bool                                      quiet{};
        bool                                      watch{};
        bool                                      json{};
        bool                                      color{};
    };

    struct Project {
        Config  config;
        Options options;
    };

    // Lints one in-memory source. Import resolution is skipped; use
    // LintProject when `import/no-unresolved` needs the project resolver.
    FileReport LintSource(const std::string& path, const std::string& source,
                          const Config& config);

    // Discovers, parses, resolves, reports, and optionally fixes a project.
    // `error` is set for an operational failure (config, discovery, or I/O).
    bool LintProject(filesystem::Fs& fs, const Project& project, Report& report,
                     std::string& error);

    // Applies non-overlapping safe fixes. Returns false only for an I/O error.
    bool ApplyFixes(const std::string& path, const std::string& original,
                    const std::vector<Diagnostic>& diagnostics, std::string& output,
                    int& applied, std::string& error);

    // Human-readable and JSON output. Both are deterministic.
    void PrintReport(std::ostream& os, const Report& report, const Options& options);
    void PrintJsonReport(std::ostream& os, const Report& report, const Options& options);

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------
    //
    // These are exposed so the CLI and tests can share the discovery, glob, and
    // source-analysis machinery. They are not part of the stable public API and
    // may change without notice.
    // -------------------------------------------------------------------------

    namespace detail {

        struct SourcePosition {
            int line{1};
            int column{1};
        };

        SourcePosition PositionForOffset(const std::string& source, int32_t offset);

        std::string RelativeDisplayPath(filesystem::Fs& fs, const std::string& cwd,
                                        const std::string& path);

        bool HasGlobMeta(std::string_view pattern);
        bool ValidateGlobPattern(std::string_view pattern, std::string& error);
        bool GlobMatch(std::string_view pattern, std::string_view path);
        bool PatternCoversDirectory(std::string_view pattern, std::string_view dir);

        bool DiscoverFiles(filesystem::Fs& fs, const Config& config,
                           const std::vector<std::string>& paths,
                           const std::string& cwd, std::vector<std::string>& files,
                           std::string& error);

        struct Analysis {
            FileReport                          report;
            std::vector<compiler::ImportRecord> imports;
        };

        Analysis AnalyzeSource(const std::string& path, const std::string& source,
                               const Config& config);

    } // namespace detail

} // namespace guchho::lint
