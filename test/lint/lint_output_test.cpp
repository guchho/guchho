#include "test/guchho_test.hpp"

#include "guchho/lint.hpp"

#include <sstream>
#include <string>

using namespace guchho::lint;

namespace {

Report SampleReport() {
    Report report;
    FileReport file;
    file.path = "src/a.js";
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

} // namespace

TEST(LintOutput, HumanReportPrintsLocationRuleAndSummary) {
    std::ostringstream out;
    PrintReport(out, SampleReport(), Options{});
    const std::string text = out.str();
    EXPECT_TRUE(Contains(text, "src/a.js:2:7: warning"));
    EXPECT_TRUE(Contains(text, "eqeqeq"));
    EXPECT_TRUE(Contains(text, "1 problem (0 errors, 1 warning)"));
}

TEST(LintOutput, QuietHidesWarningLinesButKeepsCounts) {
    std::ostringstream out;
    Options options;
    options.quiet = true;
    PrintReport(out, SampleReport(), options);
    const std::string text = out.str();
    EXPECT_FALSE(Contains(text, "src/a.js:2:7"));
    EXPECT_TRUE(Contains(text, "1 problem (0 errors, 1 warning)"));
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
    EXPECT_EQ(out.str(), "No problems found.\n");
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
