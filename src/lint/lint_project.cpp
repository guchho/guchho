#include "guchho/lint.hpp"

#include "guchho/bundler.hpp"
#include "guchho/cache.hpp"
#include "guchho/resolver.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace guchho::lint {

namespace {

bool ShouldResolve(const compiler::ImportRecord& record) {
    if (record.glob_pattern != nullptr) return false;
    if (record.path.text.empty()) return false;
    if (record.path.IsDisabled()) return false;
    switch (record.kind) {
    case compiler::ImportKind::kStmt:
    case compiler::ImportKind::kRequire:
    case compiler::ImportKind::kDynamic:
    case compiler::ImportKind::kRequireResolve:
        return true;
    default:
        return false;
    }
}

bool WriteFile(const std::string& path, const std::string& contents,
               std::string& error) {
    std::ofstream stream(filesystem::PathFromUTF8(path),
                         std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "cannot write " + path;
        return false;
    }
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

void Recalculate(Report& report) {
    report.errors = 0;
    report.warnings = 0;
    report.files_with_problems = 0;
    for (FileReport& file : report.files) {
        file.errors = 0;
        file.warnings = 0;
        for (const Diagnostic& diagnostic : file.diagnostics) {
            if (diagnostic.severity == Severity::kError) file.errors++;
            if (diagnostic.severity == Severity::kWarning) file.warnings++;
        }
        report.errors += file.errors;
        report.warnings += file.warnings;
        if (!file.diagnostics.empty()) report.files_with_problems++;
    }
}

} // namespace

FileReport LintSource(const std::string& path, const std::string& source,
                      const Config& config) {
    return detail::AnalyzeSource(path, source, config).report;
}

bool ApplyFixes(const std::string& path, const std::string& original,
                const std::vector<Diagnostic>& diagnostics, std::string& output,
                int& applied, std::string& error) {
    (void)path;
    output = original;
    applied = 0;
    error.clear();

    struct PendingFix {
        logger::Range range;
        std::string replacement;
    };
    std::vector<PendingFix> fixes;
    for (const Diagnostic& diagnostic : diagnostics) {
        for (const logger::Fix& fix : diagnostic.fixes) {
            if (fix.safety != logger::FixSafety::kSafe) continue;
            if (fix.range.loc.start < 0 || fix.range.len < 0) continue;
            const size_t start = static_cast<size_t>(fix.range.loc.start);
            const size_t end = std::min(original.size(),
                                        start + static_cast<size_t>(fix.range.len));
            if (start > original.size()) continue;
            fixes.push_back(PendingFix{
                logger::Range{fix.range.loc,
                              static_cast<int32_t>(end - start)},
                fix.replacement,
            });
        }
    }

    std::sort(fixes.begin(), fixes.end(),
              [](const PendingFix& a, const PendingFix& b) {
                  if (a.range.loc.start != b.range.loc.start) {
                      return a.range.loc.start < b.range.loc.start;
                  }
                  return a.range.len < b.range.len;
              });

    std::string result;
    result.reserve(original.size());
    size_t cursor = 0;
    for (const PendingFix& fix : fixes) {
        const size_t start = static_cast<size_t>(fix.range.loc.start);
        const size_t end = start + static_cast<size_t>(fix.range.len);
        if (start < cursor) continue;
        result.append(original, cursor, start - cursor);
        result += fix.replacement;
        cursor = end;
        applied++;
    }
    result.append(original, cursor, std::string::npos);
    output = std::move(result);
    return true;
}

bool LintProject(filesystem::Fs& fs, const Project& project, Report& report,
                 std::string& error) {
    report = Report{};
    error.clear();

    const std::string cwd = project.options.cwd.empty() ? fs.Cwd()
                                                        : project.options.cwd;

    std::vector<std::string> files;
    if (!detail::DiscoverFiles(fs, project.config, project.options.paths, cwd,
                               files, error)) {
        return false;
    }

    const Severity unresolved_severity =
        project.config.rules.contains("import/no-unresolved")
            ? project.config.rules.at("import/no-unresolved")
            : DefaultSeverity(Rule::kImportNoUnresolved);
    const bool check_unresolved = unresolved_severity != Severity::kOff;

    std::unique_ptr<cache::CacheSet> caches;
    config::Options resolver_options;
    resolver_options.OutputPlatform = project.config.platform;
    resolver_options.ExtensionOrder = {".tsx", ".ts", ".jsx", ".js",
                                       ".css", ".json"};
    resolver_options.ExtensionToLoader = bundler::DefaultExtensionToLoaderMap();

    logger::Log resolver_log = logger::NewDeferLog(
        logger::DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    std::unique_ptr<resolver::Resolver> resolver;
    if (check_unresolved) {
        caches = cache::MakeCacheSet();
        resolver = resolver::NewResolver(config::APICall::kBuildCall, fs,
                                         resolver_log, *caches,
                                         &resolver_options);
    }

    report.fixes = 0;
    for (const std::string& absolute : files) {
        filesystem::FsResult<std::string> read = fs.ReadFile(absolute);
        if (!read.Ok()) {
            error = "cannot read " + absolute + ": " + read.original_error;
            return false;
        }

        std::string contents = std::move(read.value);
        detail::Analysis analysis =
            detail::AnalyzeSource(absolute, contents, project.config);

        int applied = 0;
        if (project.options.fix || project.options.fix_dry_run) {
            std::string fixed;
            std::string fix_error;
            if (!ApplyFixes(absolute, contents, analysis.report.diagnostics,
                            fixed, applied, fix_error)) {
                error = fix_error;
                return false;
            }
            if (applied > 0 && fixed != contents) {
                if (project.options.fix && !project.options.fix_dry_run) {
                    if (!WriteFile(absolute, fixed, error)) return false;
                }
                contents = std::move(fixed);
                analysis = detail::AnalyzeSource(absolute, contents,
                                                 project.config);
            }
        }

        if (check_unresolved && resolver != nullptr) {
            for (const compiler::ImportRecord& record : analysis.imports) {
                if (!ShouldResolve(record)) continue;
                resolver::DebugMeta meta;
                std::optional<resolver::ResolveResult> resolved =
                    resolver->Resolve(fs.Dir(absolute), record.path.text,
                                      record.kind, &meta);
                if (resolved.has_value()) continue;

                detail::SourcePosition start = detail::PositionForOffset(
                    contents, record.range.loc.start);
                detail::SourcePosition end = detail::PositionForOffset(
                    contents, record.range.loc.start +
                                  std::max(record.range.len, 0));
                Diagnostic diagnostic;
                diagnostic.path = absolute;
                diagnostic.rule_id = "import/no-unresolved";
                diagnostic.severity = unresolved_severity;
                diagnostic.message = "Unable to resolve path to module \"" +
                                     record.path.text + "\".";
                if (!meta.suggestion_text.empty()) {
                    diagnostic.message += " Did you mean \"" +
                                          meta.suggestion_text + "\"?";
                } else if (!meta.notes.empty() &&
                           !meta.notes.front().text.empty()) {
                    diagnostic.message += " " + meta.notes.front().text;
                }
                diagnostic.line = start.line;
                diagnostic.column = start.column;
                diagnostic.end_line = end.line;
                diagnostic.end_column = end.column;
                analysis.report.diagnostics.push_back(std::move(diagnostic));
            }
            std::sort(analysis.report.diagnostics.begin(),
                      analysis.report.diagnostics.end(),
                      [](const Diagnostic& a, const Diagnostic& b) {
                          if (a.line != b.line) return a.line < b.line;
                          if (a.column != b.column) return a.column < b.column;
                          if (a.rule_id != b.rule_id) {
                              return a.rule_id < b.rule_id;
                          }
                          return a.message < b.message;
                      });
        }

        const std::string display =
            detail::RelativeDisplayPath(fs, cwd, absolute);
        analysis.report.path = display;
        for (Diagnostic& diagnostic : analysis.report.diagnostics) {
            diagnostic.path = display;
        }
        if (analysis.report.source.empty() &&
            !analysis.report.diagnostics.empty()) {
            analysis.report.source = contents;
        }
        analysis.report.fixes = applied;
        report.fixes += applied;
        report.files.push_back(std::move(analysis.report));
    }

    std::sort(report.files.begin(), report.files.end(),
              [](const FileReport& a, const FileReport& b) {
                  return a.path < b.path;
              });
    Recalculate(report);
    return true;
}

} // namespace guchho::lint
