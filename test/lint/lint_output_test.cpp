#include "test/guchho_test.hpp"

#include "guchho/lint.hpp"
#include "guchho/logger.hpp"

#include <sstream>
#include <string>

using namespace guchho::lint;

namespace {

Report SampleReport() {
    Report report;
    FileReport file;
    file.path = "src/a.js";
    file.source = "const a = 1;\nif (a == b) {}\n";
    Diagnostic warning;
    warning.path = file.path;
    warning.rule_id = "eqeqeq";
    warning.severity = Severity::kWarning;
    warning.message = "Expected '===' and instead saw '=='.";
    warning.line = 2;
    warning.column = 7;
    warning.end_line = 2;
    warning.end_column = 9;
    file.diagnostics.push_back(warning);
    file.warnings = 1;
    report.warnings = 1;
    report.files_with_problems = 1;
    report.files.push_back(file);
    return report;
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::string Icon(guchho::logger::MsgKind kind) {
    return std::string(guchho::logger::MsgKindToIcon(kind));
}

std::string StripAnsi(const std::string& text) {
    std::string stripped;
    stripped.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '[') {
            const std::size_t end = text.find('m', i + 2);
            if (end != std::string::npos) {
                i = end + 1;
                continue;
            }
        }
        stripped.push_back(text[i++]);
    }
    return stripped;
}

} // namespace

TEST(LintOutput, HumanReportPrintsLocationRuleSnippetAndSummary) {
    std::ostringstream out;
    PrintReport(out, SampleReport(), Options{});
    const std::string expected =
        std::string("src/a.js:2:7: WARNING  [eqeqeq]  Expected '===' and "
                    "instead saw '=='.\n"
                    "  2 | if (a == b) {}\n"
                    "    |       ^^\n"
                    "\n") +
        Icon(guchho::logger::MsgKind::kWarning) +
        " 1 problem (0 errors, 1 warning)\n";
    EXPECT_EQ(out.str(), expected);
    EXPECT_FALSE(Contains(out.str(), "\033["));
}

TEST(LintOutput, QuietHidesWarningLinesButKeepsCounts) {
    std::ostringstream out;
    Options options;
    options.quiet = true;
    PrintReport(out, SampleReport(), options);
    const std::string text = out.str();
    EXPECT_FALSE(Contains(text, "src/a.js:2:7"));
    EXPECT_FALSE(Contains(text, "if (a == b)"));
    EXPECT_EQ(text, Icon(guchho::logger::MsgKind::kWarning) +
                        " 1 problem (0 errors, 1 warning)\n");
}

TEST(LintOutput, HumanReportOmitsSnippetWhenSourceIsUnavailable) {
    Report report = SampleReport();
    report.files[0].source.clear();
    std::ostringstream out;
    PrintReport(out, report, Options{});
    const std::string text = out.str();
    EXPECT_TRUE(Contains(text, "src/a.js:2:7: WARNING  [eqeqeq]"));
    EXPECT_FALSE(Contains(text, "  2 |"));
    EXPECT_FALSE(Contains(text, "^^"));
}

TEST(LintOutput, HumanReportExpandsTabsInSnippets) {
    Report report;
    FileReport file;
    file.path = "src/tab.js";
    file.source = "\tif (a == b) {}\n";
    Diagnostic warning;
    warning.path = file.path;
    warning.rule_id = "eqeqeq";
    warning.severity = Severity::kWarning;
    warning.message = "Expected '===' and instead saw '=='.";
    warning.line = 1;
    warning.column = 8;
    warning.end_line = 1;
    warning.end_column = 10;
    file.diagnostics.push_back(warning);
    file.warnings = 1;
    report.warnings = 1;
    report.files.push_back(file);

    std::ostringstream out;
    PrintReport(out, report, Options{});
    const std::string text = out.str();
    EXPECT_TRUE(Contains(text, "  1 |     if (a == b) {}"));
    EXPECT_TRUE(Contains(text, "^^"));
}

TEST(LintOutput, HumanReportColorsOutputWhenEnabled) {
    Options options;
    options.color = true;
    std::ostringstream out;
    PrintReport(out, SampleReport(), options);
    const std::string text = out.str();
    const auto& colors = guchho::logger::kTerminalColors;

    EXPECT_TRUE(Contains(text, std::string(colors.bold) + "src/a.js:2:7:" +
                                   std::string(colors.reset) + " "));
    EXPECT_TRUE(Contains(text, std::string(colors.yellow) + "WARNING" +
                                   std::string(colors.reset)));
    EXPECT_TRUE(Contains(text, std::string(colors.cyan) + "[eqeqeq]" +
                                   std::string(colors.reset)));
    EXPECT_TRUE(Contains(text, std::string(colors.dim) + "  2 | " +
                                   std::string(colors.reset) +
                                   "if (a == b) {}"));
    EXPECT_TRUE(Contains(text, std::string(colors.dim) + "    | " +
                                   std::string(colors.reset) + "      " +
                                   std::string(colors.yellow) + "^^" +
                                   std::string(colors.reset)));
    EXPECT_TRUE(Contains(text, std::string(colors.yellow) +
                                   Icon(guchho::logger::MsgKind::kWarning) +
                                   std::string(colors.reset) +
                                   " 1 problem (0 errors, 1 warning)"));
}

TEST(LintOutput, HumanReportPlainTextMatchesColoredFormatting) {
    Options colored;
    colored.color = true;
    Options plain;
    plain.color = false;

    std::ostringstream colored_out;
    std::ostringstream plain_out;
    PrintReport(colored_out, SampleReport(), colored);
    PrintReport(plain_out, SampleReport(), plain);

    EXPECT_FALSE(Contains(plain_out.str(), "\033["));
    EXPECT_EQ(StripAnsi(colored_out.str()), plain_out.str());
}

TEST(LintOutput, HumanReportUsesPluralSummary) {
    Report report = SampleReport();
    report.errors = 31;
    report.warnings = 1;
    std::ostringstream out;
    PrintReport(out, report, Options{});
    EXPECT_TRUE(Contains(out.str(),
                         Icon(guchho::logger::MsgKind::kError) +
                             " 32 problems (31 errors, 1 warning)"));
}

TEST(LintOutput, HumanReportSaysWhenNothingMatched) {
    std::ostringstream out;
    PrintReport(out, Report{}, Options{});
    EXPECT_EQ(out.str(), "No files matched.\n");
}

TEST(LintOutput, HumanReportSaysWhenNoProblemsWereFound) {
    Report report;
    report.files.push_back(FileReport{});
    std::ostringstream out;
    PrintReport(out, report, Options{});
    EXPECT_EQ(out.str(), std::string(guchho::logger::SuccessIcon()) +
                             " No problems found.\n");
}

TEST(LintOutput, HumanReportDescribesDryRunFixes) {
    Report report = SampleReport();
    report.fixes = 1;
    Options options;
    options.fix_dry_run = true;
    std::ostringstream out;
    PrintReport(out, report, options);
    EXPECT_TRUE(Contains(out.str(), "1 fix would be applied."));
}

TEST(LintOutput, JsonReportHasBothArraysAndCounts) {
    std::ostringstream out;
    PrintJsonReport(out, SampleReport(), Options{});
    const std::string text = out.str();
    EXPECT_TRUE(Contains(text, "\"files\": ["));
    EXPECT_TRUE(Contains(text, "\"messages\": [\n"));
    EXPECT_TRUE(Contains(text, "\"path\": \"src/a.js\""));
    EXPECT_TRUE(Contains(text, "\"ruleId\": \"eqeqeq\""));
    EXPECT_TRUE(Contains(text, "\"severity\": \"warning\""));
    EXPECT_TRUE(Contains(text, "\"errorCount\": 0"));
    EXPECT_TRUE(Contains(text, "\"warningCount\": 1"));
    EXPECT_TRUE(Contains(text, "\"filesWithProblems\": 1"));
    EXPECT_TRUE(Contains(text, "\"fixCount\": 0"));
    EXPECT_FALSE(Contains(text, "[    {"));
}

TEST(LintOutput, JsonReportMatchesTheDocumentedSchema) {
    std::ostringstream out;
    PrintJsonReport(out, SampleReport(), Options{});
    const std::string expected = R"json({
  "files": [
    {
      "path": "src/a.js",
      "errorCount": 0,
      "warningCount": 1,
      "messages": [
        {
          "ruleId": "eqeqeq",
          "severity": "warning",
          "message": "Expected '===' and instead saw '=='.",
          "line": 2,
          "column": 7,
          "endLine": 2,
          "endColumn": 9,
          "syntax": false,
          "fixCount": 0
        }
      ]
    }
  ],
  "messages": [
    {
      "path": "src/a.js",
      "ruleId": "eqeqeq",
      "severity": "warning",
      "message": "Expected '===' and instead saw '=='.",
      "line": 2,
      "column": 7,
      "endLine": 2,
      "endColumn": 9,
      "syntax": false,
      "fixCount": 0
    }
  ],
  "errorCount": 0,
  "warningCount": 1,
  "fixCount": 0,
  "filesWithProblems": 1
}
)json";
    EXPECT_EQ(out.str(), expected);
}

TEST(LintOutput, JsonReportIgnoresColorOption) {
    Options options;
    options.color = true;
    std::ostringstream out;
    PrintJsonReport(out, SampleReport(), options);
    EXPECT_FALSE(Contains(out.str(), "\033["));
}

TEST(LintOutput, JsonReportIsDeterministic) {
    std::ostringstream first;
    std::ostringstream second;
    const Report report = SampleReport();
    PrintJsonReport(first, report, Options{});
    PrintJsonReport(second, report, Options{});
    EXPECT_EQ(first.str(), second.str());
}

TEST(LintOutput, QuietJsonOmitsWarningMessages) {
    Options options;
    options.quiet = true;
    std::ostringstream out;
    PrintJsonReport(out, SampleReport(), options);
    const std::string text = out.str();
    EXPECT_FALSE(Contains(text, "Expected '==='"));
    EXPECT_TRUE(Contains(text, "\"warningCount\": 1"));
    EXPECT_TRUE(Contains(text, "\"filesWithProblems\": 1"));
}
