#include "test/guchho_test.hpp"
#include "test/helpers/cli_test.hpp"

#include "guchho/api.hpp"
#include "guchho/cli.hpp"

#include <chrono>
#include <csignal>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

std::optional<guchho::api::BuildOptions> BuildOptionsFor(const std::vector<std::string>& args) {
    guchho::api::BuildOptions build;
    guchho::cli::ParseOptionsExtras extras;
    auto err = guchho::cli::parseOptionsImpl(args, &build, nullptr,
                                             guchho::cli::ParseOptionsKind::kExternal, extras);
    if (err) return std::nullopt;
    return build;
}

} // namespace

TEST(CliLint, BuildChecksRemainOffByDefault) {
    auto opts = BuildOptionsFor({});
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->lint_undefined_variables, guchho::api::LintLevel::kOff);
    EXPECT_EQ(opts->lint_unused_variables, guchho::api::LintLevel::kOff);
    EXPECT_EQ(opts->lint_unused_parameters, guchho::api::LintLevel::kOff);
}

TEST(CliLint, LegacyBuildLintFlagIsNoLongerAccepted) {
    EXPECT_FALSE(BuildOptionsFor({"--lint=undefined"}).has_value());
    EXPECT_FALSE(BuildOptionsFor({"--lint-level=undefined=error"}).has_value());
}

TEST(CliLint, HelpPrintsTheCommandUsageAndRules) {
    const guchho::test::CliResult result = guchho::test::RunCli({"lint", "--help"});
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.err.empty());
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "Usage: guchho lint"));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "--fix-dry-run"));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "import/no-unresolved"));
}

TEST(CliLint, InvalidFormatIsAUsageError) {
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--format=xml"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_TRUE(guchho::test::OutputContains(result.err, "Invalid lint format"));
}

TEST(CliLint, UnknownRuleIsAUsageErrorAndSuggestsTheClosestName) {
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--rule=no-undefi:error"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_TRUE(guchho::test::OutputContains(result.err, "Unknown lint rule"));
    EXPECT_TRUE(guchho::test::OutputContains(result.err, "no-undef"));
}

TEST(CliLint, MissingRuleLevelIsAUsageError) {
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--rule=no-console"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_TRUE(guchho::test::OutputContains(result.err, "Missing \":\""));
}

TEST(CliLint, NegativeMaximumWarningsIsAUsageError) {
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--max-warnings=-1"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_TRUE(guchho::test::OutputContains(result.err, "cannot be negative"));
}

TEST(CliLint, NonNumericMaximumWarningsIsAUsageError) {
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--max-warnings=abc"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_TRUE(guchho::test::OutputContains(result.err, "Invalid value"));
}

TEST(CliLint, EmptyProjectSucceedsWithoutFiles) {
    guchho::test::CliWorkspace workspace("lint-empty");
    const guchho::test::CliResult result = guchho::test::RunCli({"lint"});
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "No files matched."));
}

TEST(CliLint, MissingPathIsAnOperationalError) {
    guchho::test::CliWorkspace workspace("lint-missing");
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "does-not-exist.js"});
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_TRUE(guchho::test::OutputContains(result.err, "path does not exist"));
}

TEST(CliLint, WarningsAreReportedWithoutFailingTheCommand) {
    guchho::test::CliWorkspace workspace("lint-warning");
    workspace.Write("src/a.js", "var unused = 1;\n");
    const guchho::test::CliResult result = guchho::test::RunCli({"lint", "src"});
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "src/a.js:1:5: warning"));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "no-unused-vars"));
    EXPECT_TRUE(
        guchho::test::OutputContains(result.out, "  1 | var unused = 1;"));
    EXPECT_TRUE(guchho::test::OutputContains(
        result.out, "\xE2\x9C\x96 1 problem (0 errors, 1 warning)"));
}

TEST(CliLint, ErrorsFailTheCommand) {
    guchho::test::CliWorkspace workspace("lint-error");
    workspace.Write("src/a.js", "var value = 1;\nvar value = 2;\n");
    const guchho::test::CliResult result = guchho::test::RunCli(
        {"lint", "--rule", "no-unused-vars:off", "src/a.js"});
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "no-redeclare"));
}

TEST(CliLint, MaxWarningsZeroTurnsAWarningIntoAFailure) {
    guchho::test::CliWorkspace workspace("lint-max-warnings");
    workspace.Write("src/a.js", "var unused = 1;\n");
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--max-warnings=0", "src/a.js"});
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "no-unused-vars"));
}

TEST(CliLint, JsonFormatContainsFilesMessagesAndCounts) {
    guchho::test::CliWorkspace workspace("lint-json");
    workspace.Write("src/a.js", "var unused = 1;\n");
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--json", "src/a.js"});
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "\"files\": ["));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "\"messages\": ["));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "\"severity\": \"warning\""));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "\"ruleId\": \"no-unused-vars\""));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "\"warningCount\": 1"));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "\"fixCount\": 0"));
    EXPECT_FALSE(guchho::test::OutputContains(result.out, "[    {"));
}

TEST(CliLint, QuietKeepsCountsButHidesWarningMessages) {
    guchho::test::CliWorkspace workspace("lint-quiet");
    workspace.Write("src/a.js", "var unused = 1;\n");
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--quiet", "--json", "src/a.js"});
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "\"warningCount\": 1"));
    EXPECT_FALSE(guchho::test::OutputContains(result.out, "no-unused-vars"));
}

TEST(CliLint, FixRewritesSafeProblems) {
    guchho::test::CliWorkspace workspace("lint-fix");
    workspace.Write("src/a.js",
                    "var x = 1;\nif (typeof x == \"number\") { x; }\n");
    const guchho::test::CliResult result = guchho::test::RunCli(
        {"lint", "--fix", "--rule=no-unused-vars:off", "src/a.js"});
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(guchho::test::OutputContains(workspace.Read("src/a.js"),
                                             "typeof x === \"number\""));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "1 fix applied."));
}

TEST(CliLint, WatchRelintsAddedFilesAndStopsOnInterrupt) {
    guchho::test::CliWorkspace workspace("lint-watch");
    workspace.Write("src/a.js", "var unused = 1;\n");
    std::thread stopper([&workspace] {
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        workspace.Write("src/b.js", "var alsoUnused = 2;\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        std::raise(SIGINT);
    });
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--watch", "src"});
    stopper.join();
    EXPECT_EQ(result.exit_code, guchho::test::kInterrupted);
    EXPECT_TRUE(
        guchho::test::OutputContains(result.out, "Watching for changes..."));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "src/b.js"))
        << result.out;
}

TEST(CliLint, WatchNoticesDeletedFiles) {
    guchho::test::CliWorkspace workspace("lint-watch-delete");
    workspace.Write("src/a.js", "var unused = 1;\n");
    workspace.Write("src/b.js", "let used = 2;\nused;\n");
    std::thread stopper([&workspace] {
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        std::error_code error;
        std::filesystem::remove(
            guchho::test::CliWorkspace::Native(workspace.At("src/a.js")),
            error);
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        std::raise(SIGINT);
    });
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--watch", "src"});
    stopper.join();
    EXPECT_EQ(result.exit_code, guchho::test::kInterrupted);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "1 problem"));
    const std::string tail = "\xE2\x9C\x94 No problems found.\n";
    ASSERT_GE(result.out.size(), tail.size());
    EXPECT_EQ(result.out.compare(result.out.size() - tail.size(), tail.size(),
                                 tail),
              0)
        << result.out;
}

TEST(CliLint, WatchReloadsAConfigurationFileThatAppears) {
    guchho::test::CliWorkspace workspace("lint-watch-config");
    workspace.Write("src/a.js", "var unused = 1;\n");
    std::thread stopper([&workspace] {
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        workspace.Write("guchho.lint.config.json",
                        "{\"rules\":{\"no-unused-vars\":\"off\"}}\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        std::raise(SIGINT);
    });
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--watch", "src"});
    stopper.join();
    EXPECT_EQ(result.exit_code, guchho::test::kInterrupted);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "no-unused-vars"));
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "No problems found."));
}

TEST(CliLint, WatchFixDoesNotLoopOrLoseTheFix) {
    guchho::test::CliWorkspace workspace("lint-watch-fix");
    workspace.Write("src/a.js",
                    "var x = 1;\nif (typeof x == \"number\") { x; }\n");
    std::thread stopper([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1800));
        std::raise(SIGINT);
    });
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "--watch", "--fix", "src"});
    stopper.join();
    EXPECT_EQ(result.exit_code, guchho::test::kInterrupted);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "1 fix applied."));
    EXPECT_TRUE(guchho::test::OutputContains(workspace.Read("src/a.js"),
                                             "typeof x === \"number\""));
}

TEST(CliLint, ConfigFileIsDiscoveredAndApplied) {
    guchho::test::CliWorkspace workspace("lint-config");
    workspace.Write("guchho.lint.config.json",
                    "{\"rules\":{\"no-unused-vars\":\"off\"}}\n");
    workspace.Write("src/a.js", "var unused = 1;\n");
    const guchho::test::CliResult result =
        guchho::test::RunCli({"lint", "src/a.js"});
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "No problems found."));
}

TEST(CliLint, RuleOverrideWinsOverTheConfigFile) {
    guchho::test::CliWorkspace workspace("lint-rule-override");
    workspace.Write("guchho.lint.config.json",
                    "{\"rules\":{\"no-unused-vars\":\"off\"}}\n");
    workspace.Write("src/a.js", "var unused = 1;\n");
    const guchho::test::CliResult result = guchho::test::RunCli(
        {"lint", "--rule", "no-unused-vars:error", "src/a.js"});
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "no-unused-vars"));
}

TEST(CliLint, FixDryRunDoesNotWriteTheFile) {
    guchho::test::CliWorkspace workspace("lint-fix-dry-run");
    const std::string source =
        "var x = 1;\nif (typeof x == \"number\") { x; }\n";
    workspace.Write("src/a.js", source);
    const guchho::test::CliResult result = guchho::test::RunCli(
        {"lint", "--fix-dry-run", "--rule=no-unused-vars:off", "src/a.js"});
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(workspace.Read("src/a.js"), source);
    EXPECT_TRUE(guchho::test::OutputContains(result.out, "1 fix would be applied."));
}
