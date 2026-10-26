#include "guchho/lint.hpp"

#include "guchho/helpers.hpp"

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace guchho::lint {

namespace {

std::string_view SeverityText(Severity severity) {
    switch (severity) {
    case Severity::kError:
        return "error";
    case Severity::kWarning:
        return "warning";
    default:
        return "off";
    }
}

logger::MsgKind SeverityKind(Severity severity) {
    return severity == Severity::kError ? logger::MsgKind::kError
                                        : logger::MsgKind::kWarning;
}

std::string_view SeverityLabel(Severity severity) {
    if (severity == Severity::kOff) return "off";
    return logger::MsgKindToString(SeverityKind(severity));
}

std::string_view SeverityColor(Severity severity,
                               const logger::Colors& colors) {
    switch (severity) {
    case Severity::kError:
        return colors.red;
    case Severity::kWarning:
        return colors.yellow;
    default:
        return {};
    }
}

std::string ExpandTabs(std::string_view text) {
    std::string expanded;
    expanded.reserve(text.size());
    for (char ch : text) {
        if (ch == '\t') {
            const std::size_t spaces = 4 - (expanded.size() % 4);
            expanded.append(spaces, ' ');
        } else {
            expanded.push_back(ch);
        }
    }
    return expanded;
}

bool FindSourceLine(const std::string& source, int line_number,
                    std::string_view& out) {
    if (line_number < 1 || source.empty()) return false;

    std::size_t start = 0;
    for (int current = 1; current < line_number; ++current) {
        const std::size_t newline = source.find('\n', start);
        if (newline == std::string::npos) return false;
        start = newline + 1;
    }
    if (start >= source.size()) return false;

    std::size_t end = source.find('\n', start);
    if (end == std::string::npos) end = source.size();
    std::string_view line(source.data() + start, end - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    out = line;
    return true;
}

void PrintHeader(std::ostream& os, const Diagnostic& diagnostic,
                 const logger::Colors& colors) {
    os << colors.bold << diagnostic.path << ":" << diagnostic.line << ":"
       << diagnostic.column << ":" << colors.reset << " ";

    const std::string_view severity = SeverityLabel(diagnostic.severity);
    os << SeverityColor(diagnostic.severity, colors) << severity
       << colors.reset;
    if (severity.size() < 7) {
        os << std::string(7 - severity.size(), ' ');
    }

    if (!diagnostic.rule_id.empty()) {
        os << "  " << colors.cyan << "[" << diagnostic.rule_id << "]"
           << colors.reset;
    }
    os << "  " << diagnostic.message << "\n";
}

void PrintSnippet(std::ostream& os, const Diagnostic& diagnostic,
                  const FileReport& file,
                  const logger::Colors& colors) {
    std::string_view line;
    if (!FindSourceLine(file.source, diagnostic.line, line)) return;

    const std::string number = std::to_string(diagnostic.line);
    const std::string expanded = ExpandTabs(line);
    os << colors.dim << std::string(2, ' ') << number << " | " << colors.reset
       << expanded << "\n";

    std::size_t start = 0;
    if (diagnostic.column > 1) {
        start = std::min<std::size_t>(
            static_cast<std::size_t>(diagnostic.column - 1), line.size());
    }

    std::size_t length = 1;
    if (diagnostic.end_line == diagnostic.line &&
        diagnostic.end_column > diagnostic.column) {
        length = static_cast<std::size_t>(
            diagnostic.end_column - diagnostic.column);
    }
    if (start < line.size()) {
        length = std::min(length, line.size() - start);
    } else {
        length = 1;
    }
    if (length == 0) length = 1;

    const std::string prefix = ExpandTabs(line.substr(0, start));
    const std::string marker = ExpandTabs(line.substr(start, length));
    const std::size_t caret_width =
        std::max<std::size_t>(1, marker.size());

    os << colors.dim << std::string(number.size() + 3, ' ') << "| "
       << colors.reset << std::string(prefix.size(), ' ')
       << SeverityColor(diagnostic.severity, colors)
       << std::string(caret_width, '^') << colors.reset << "\n";
}

std::string JsonString(std::string_view value) {
    return helpers::QuoteForJSON(value, false);
}

void PrintJsonMessage(std::ostream& os, const Diagnostic& diagnostic,
                      std::string_view indent) {
    os << indent << "{\n";
    os << indent << "  \"ruleId\": " << JsonString(diagnostic.rule_id) << ",\n";
    os << indent << "  \"severity\": "
       << JsonString(SeverityText(diagnostic.severity)) << ",\n";
    os << indent << "  \"message\": " << JsonString(diagnostic.message) << ",\n";
    os << indent << "  \"line\": " << diagnostic.line << ",\n";
    os << indent << "  \"column\": " << diagnostic.column << ",\n";
    os << indent << "  \"endLine\": " << diagnostic.end_line << ",\n";
    os << indent << "  \"endColumn\": " << diagnostic.end_column << ",\n";
    os << indent << "  \"syntax\": "
       << (diagnostic.is_syntax ? "true" : "false") << ",\n";
    os << indent << "  \"fixCount\": " << diagnostic.fixes.size() << "\n";
    os << indent << "}";
}

} // namespace

void PrintReport(std::ostream& os, const Report& report,
                 const Options& options) {
    if (report.files.empty()) {
        os << "No files matched.\n";
        return;
    }

    logger::Colors colors{};
    if (options.color) colors = logger::kTerminalColors;

    for (const FileReport& file : report.files) {
        for (const Diagnostic& diagnostic : file.diagnostics) {
            if (options.quiet && diagnostic.severity == Severity::kWarning) {
                continue;
            }
            PrintHeader(os, diagnostic, colors);
            PrintSnippet(os, diagnostic, file, colors);
            os << "\n";
        }
    }

    const int problems = report.errors + report.warnings;
    if (problems == 0) {
        os << colors.green << logger::SuccessIcon() << colors.reset
           << " No problems found.\n";
    } else {
        const logger::MsgKind kind =
            report.errors > 0 ? logger::MsgKind::kError
                              : logger::MsgKind::kWarning;
        const std::string_view color =
            report.errors > 0 ? colors.red : colors.yellow;
        os << color << logger::MsgKindToIcon(kind) << colors.reset << " "
           << problems
           << (problems == 1 ? " problem" : " problems") << " ("
           << report.errors << (report.errors == 1 ? " error" : " errors")
           << ", " << report.warnings
           << (report.warnings == 1 ? " warning" : " warnings") << ")\n";
    }

    if (report.fixes > 0) {
        os << report.fixes << (report.fixes == 1 ? " fix" : " fixes");
        if (options.fix_dry_run) {
            os << " would be applied.\n";
        } else {
            os << " applied.\n";
        }
    }
}

void PrintJsonReport(std::ostream& os, const Report& report,
                     const Options& options) {
    os << "{\n";
    os << "  \"files\": [\n";
    bool first_file = true;
    for (const FileReport& file : report.files) {
        if (!first_file) os << ",\n";
        first_file = false;
        os << "    {\n";
        os << "      \"path\": " << JsonString(file.path) << ",\n";
        os << "      \"errorCount\": " << file.errors << ",\n";
        os << "      \"warningCount\": " << file.warnings << ",\n";
        os << "      \"messages\": [";
        if (!file.diagnostics.empty()) os << "\n";
        bool first_message = true;
        bool any_message = false;
        for (const Diagnostic& diagnostic : file.diagnostics) {
            if (options.quiet && diagnostic.severity == Severity::kWarning) {
                continue;
            }
            any_message = true;
            if (!first_message) os << ",\n";
            first_message = false;
            PrintJsonMessage(os, diagnostic, "        ");
        }
        if (any_message) os << "\n      ";
        os << "]\n";
        os << "    }";
    }
    if (!first_file) os << "\n";
    os << "  ],\n";

    os << "  \"messages\": [\n";
    bool first = true;
    for (const FileReport& file : report.files) {
        for (const Diagnostic& diagnostic : file.diagnostics) {
            if (options.quiet && diagnostic.severity == Severity::kWarning) {
                continue;
            }
            if (!first) os << ",\n";
            first = false;
            os << "    {\n";
            os << "      \"path\": " << JsonString(diagnostic.path) << ",\n";
            os << "      \"ruleId\": " << JsonString(diagnostic.rule_id) << ",\n";
            os << "      \"severity\": "
               << JsonString(SeverityText(diagnostic.severity)) << ",\n";
            os << "      \"message\": " << JsonString(diagnostic.message) << ",\n";
            os << "      \"line\": " << diagnostic.line << ",\n";
            os << "      \"column\": " << diagnostic.column << ",\n";
            os << "      \"endLine\": " << diagnostic.end_line << ",\n";
            os << "      \"endColumn\": " << diagnostic.end_column << ",\n";
            os << "      \"syntax\": "
               << (diagnostic.is_syntax ? "true" : "false") << ",\n";
            os << "      \"fixCount\": " << diagnostic.fixes.size() << "\n";
            os << "    }";
        }
    }
    if (!first) os << "\n";
    os << "  ],\n";
    os << "  \"errorCount\": " << report.errors << ",\n";
    os << "  \"warningCount\": " << report.warnings << ",\n";
    os << "  \"fixCount\": " << report.fixes << ",\n";
    os << "  \"filesWithProblems\": " << report.files_with_problems << "\n";
    os << "}\n";
}

} // namespace guchho::lint
