#include "guchho/lint.hpp"

#include "guchho/helpers.hpp"

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

void PrintMessage(std::ostream& os, const Diagnostic& diagnostic) {
    os << diagnostic.path << ":" << diagnostic.line << ":"
       << diagnostic.column << ": " << SeverityText(diagnostic.severity)
       << " " << diagnostic.message;
    if (!diagnostic.rule_id.empty()) {
        os << " (" << diagnostic.rule_id << ")";
    }
    os << "\n";
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

    for (const FileReport& file : report.files) {
        for (const Diagnostic& diagnostic : file.diagnostics) {
            if (options.quiet && diagnostic.severity == Severity::kWarning) {
                continue;
            }
            PrintMessage(os, diagnostic);
        }
    }

    const int problems = report.errors + report.warnings;
    if (problems == 0) {
        os << "No problems found.\n";
    } else {
        os << problems << (problems == 1 ? " problem" : " problems")
           << " (" << report.errors << (report.errors == 1 ? " error" : " errors")
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
