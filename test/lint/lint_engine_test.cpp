#include "test/guchho_test.hpp"

#include "guchho/lint.hpp"

#include <string>

using namespace guchho::lint;

namespace {

Config ConfigWith(const std::string& rule, Severity severity) {
    Config config = DefaultConfig();
    config.rules[rule] = severity;
    return config;
}

const Diagnostic* Find(const FileReport& report, const std::string& rule) {
    for (const Diagnostic& diagnostic : report.diagnostics) {
        if (diagnostic.rule_id == rule) return &diagnostic;
    }
    return nullptr;
}

bool Has(const FileReport& report, const std::string& rule) {
    return Find(report, rule) != nullptr;
}

FileReport Lint(const std::string& source, const Config& config) {
    return LintSource("/project/src/a.js", source, config);
}

} // namespace

TEST(LintEngine, ReportsUndefinedVariablesAsErrors) {
    FileReport report = Lint("const value = missing;\n", DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "no-undef");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->severity, Severity::kError);
    EXPECT_EQ(diagnostic->line, 1);
    EXPECT_EQ(diagnostic->column, 15);
    EXPECT_NE(diagnostic->message.find("missing"), std::string::npos);
}

TEST(LintEngine, ReportsUnusedVariablesAsWarnings) {
    FileReport report = Lint("const unused = 1;\n", DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "no-unused-vars");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->severity, Severity::kWarning);
    EXPECT_NE(diagnostic->message.find("unused"), std::string::npos);
}

TEST(LintEngine, ReportsRedeclarations) {
    FileReport report = Lint("var value = 1;\nvar value = 2;\n", DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "no-redeclare");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->severity, Severity::kError);
    EXPECT_EQ(diagnostic->line, 2);
}

TEST(LintEngine, UseBeforeDefineIsOffByDefaultAndCanBeEnabled) {
    Config config = DefaultConfig();
    EXPECT_FALSE(Has(Lint("value;\nlet value = 1;\n", config),
                     "no-use-before-define"));

    config.rules["no-use-before-define"] = Severity::kWarning;
    FileReport report = Lint("value;\nlet value = 1;\n", config);
    const Diagnostic* diagnostic = Find(report, "no-use-before-define");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->line, 1);
}

TEST(LintEngine, UseBeforeDefineAllowsHoistedFunctionDeclarations) {
    Config config = ConfigWith("no-use-before-define", Severity::kWarning);
    EXPECT_FALSE(Has(Lint("call();\nfunction call() {}\n", config),
                     "no-use-before-define"));
}

TEST(LintEngine, UseBeforeDefineReportsTemporalDeadZoneUses) {
    Config config = ConfigWith("no-use-before-define", Severity::kWarning);
    FileReport report = Lint("value;\nconst value = 1;\n", config);
    ASSERT_TRUE(Has(report, "no-use-before-define"));
    EXPECT_EQ(Find(report, "no-use-before-define")->line, 1);
}

TEST(LintEngine, ShadowingIsOffByDefaultAndCanBeEnabled) {
    const std::string source = "let value = 1;\n{\n  let value = 2;\n  value;\n}\n";
    Config config = DefaultConfig();
    EXPECT_FALSE(Has(Lint(source, config), "no-shadow"));

    config.rules["no-shadow"] = Severity::kWarning;
    FileReport report = Lint(source, config);
    const Diagnostic* diagnostic = Find(report, "no-shadow");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->line, 3);
    EXPECT_NE(diagnostic->message.find("upper scope"), std::string::npos);
}

TEST(LintEngine, ShadowingIgnoresSiblingScopes) {
    Config config = ConfigWith("no-shadow", Severity::kWarning);
    const std::string source =
        "function a() { let only = 1; return only; }\n"
        "function b() { let only = 2; return only; }\n"
        "a(); b;\n";
    EXPECT_FALSE(Has(Lint(source, config), "no-shadow"));
}

TEST(LintEngine, ReportsUnreachableCodeAfterReturn) {
    FileReport report =
        Lint("function f() {\n  return 1;\n  let value = 2;\n}\n",
             DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "no-unreachable");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->line, 3);
}

TEST(LintEngine, ReportsDuplicateImports) {
    FileReport report = Lint(
        "import { a } from \"./dep\";\nimport { b } from \"./dep\";\na; b;\n",
        DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "no-duplicate-imports");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->line, 2);
    EXPECT_NE(diagnostic->message.find("./dep"), std::string::npos);
}

TEST(LintEngine, ReportsInvalidTypeofComparisons) {
    FileReport report = Lint(
        "let value = 1;\nif (typeof value === \"strnig\") { value; }\n",
        DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "valid-typeof");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->severity, Severity::kError);
    EXPECT_NE(diagnostic->message.find("strnig"), std::string::npos);
}

TEST(LintEngine, ConsoleIsOffByDefaultAndCanBeEnabled) {
    const std::string source = "console.log(\"hello\");\n";
    Config config = DefaultConfig();
    EXPECT_FALSE(Has(Lint(source, config), "no-console"));

    config.rules["no-console"] = Severity::kError;
    FileReport report = Lint(source, config);
    const Diagnostic* diagnostic = Find(report, "no-console");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->line, 1);
    EXPECT_EQ(diagnostic->column, 1);
}

TEST(LintEngine, EqeqeqCarriesASafeFixForTypeof) {
    FileReport report = Lint(
        "let value = 1;\nif (typeof value == \"number\") { value; }\n",
        DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "eqeqeq");
    ASSERT_NE(diagnostic, nullptr);
    ASSERT_EQ(diagnostic->fixes.size(), 1u);
    EXPECT_EQ(diagnostic->fixes.front().replacement, "===");
}

TEST(LintEngine, EqeqeqDoesNotOfferAnUnsafeTypeCoercionFix) {
    FileReport report = Lint(
        "let value = 1;\nif (value == 1) { value; }\n", DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "eqeqeq");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_TRUE(diagnostic->fixes.empty());
}

TEST(LintEngine, DebuggerCarriesASafeRemovalFix) {
    FileReport report = Lint("debugger;\n", DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "no-debugger");
    ASSERT_NE(diagnostic, nullptr);
    ASSERT_EQ(diagnostic->fixes.size(), 1u);
    EXPECT_TRUE(diagnostic->fixes.front().replacement.empty());
}

TEST(LintEngine, ReportsDuplicateCaseLabels) {
    FileReport report = Lint(
        "let value = 1;\nswitch (value) {\ncase 1: break;\ncase 1: break;\n}\n",
        DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "no-duplicate-case");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->line, 4);
}

TEST(LintEngine, ReportsEmptyBlocks) {
    FileReport report = Lint("let value = 1;\nif (value) {}\n", DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "no-empty");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->severity, Severity::kWarning);
}

TEST(LintEngine, ReportsConstantConditions) {
    FileReport report =
        Lint("let value = 1;\nif (true) { value = 2; }\n", DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "no-constant-condition");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_EQ(diagnostic->line, 2);
}

TEST(LintEngine, SyntaxErrorsAreReportedAsDiagnostics) {
    FileReport report = Lint("const = ;\n", DefaultConfig());
    const Diagnostic* diagnostic = Find(report, "syntax");
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_TRUE(diagnostic->is_syntax);
    EXPECT_EQ(diagnostic->severity, Severity::kError);
}

TEST(LintEngine, ConfiguredGlobalsSuppressUndefinedReports) {
    Config config = DefaultConfig();
    config.globals["missing"] = true;
    FileReport report = Lint("missing;\n", config);
    EXPECT_FALSE(Has(report, "no-undef"));
}

TEST(LintEngine, ReadonlyGlobalsAlsoSuppressUndefinedReports) {
    Config config = DefaultConfig();
    config.globals["missing"] = false;
    FileReport report = Lint("missing;\n", config);
    EXPECT_FALSE(Has(report, "no-undef"));
}

TEST(LintEngine, SeverityOffSuppressesARule) {
    Config config = ConfigWith("no-debugger", Severity::kOff);
    FileReport report = Lint("debugger;\n", config);
    EXPECT_FALSE(Has(report, "no-debugger"));
}

TEST(LintEngine, ImportResolutionIsSkippedForSingleSources) {
    FileReport report = Lint("import { a } from \"./missing\";\na;\n",
                             DefaultConfig());
    EXPECT_FALSE(Has(report, "import/no-unresolved"));
}

TEST(LintEngine, CountsMatchTheDiagnostics) {
    FileReport report = Lint(
        "debugger;\nconst value = missing;\n", DefaultConfig());
    int errors = 0;
    int warnings = 0;
    for (const Diagnostic& diagnostic : report.diagnostics) {
        if (diagnostic.severity == Severity::kError) errors++;
        if (diagnostic.severity == Severity::kWarning) warnings++;
    }
    EXPECT_EQ(report.errors, errors);
    EXPECT_EQ(report.warnings, warnings);
}
