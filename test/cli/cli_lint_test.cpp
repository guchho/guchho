#include "test/guchho_test.hpp"

#include "guchho/api.hpp"
#include "guchho/cli.hpp"

#include <optional>
#include <string>
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

std::optional<guchho::api::TransformOptions> TransformOptionsFor(const std::vector<std::string>& args) {
    guchho::api::TransformOptions transform;
    guchho::cli::ParseOptionsExtras extras;
    auto err = guchho::cli::parseOptionsImpl(args, nullptr, &transform,
                                             guchho::cli::ParseOptionsKind::kExternal, extras);
    if (err) return std::nullopt;
    return transform;
}

} // namespace

TEST(CliLint, ChecksAreOffByDefault) {
    auto opts = BuildOptionsFor({});
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->lint_undefined_variables, guchho::api::LintLevel::kOff);
    EXPECT_EQ(opts->lint_unused_variables, guchho::api::LintLevel::kOff);
    EXPECT_EQ(opts->lint_unused_parameters, guchho::api::LintLevel::kOff);
}

TEST(CliLint, UndefinedCheckEnablesOnlyUndefinedWarnings) {
    auto opts = BuildOptionsFor({"--lint=undefined"});
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->lint_undefined_variables, guchho::api::LintLevel::kWarning);
    EXPECT_EQ(opts->lint_unused_variables, guchho::api::LintLevel::kOff);
    EXPECT_EQ(opts->lint_unused_parameters, guchho::api::LintLevel::kOff);
}

TEST(CliLint, UnusedCheckEnablesVariablesAndParameters) {
    auto opts = BuildOptionsFor({"--lint=unused"});
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->lint_undefined_variables, guchho::api::LintLevel::kOff);
    EXPECT_EQ(opts->lint_unused_variables, guchho::api::LintLevel::kWarning);
    EXPECT_EQ(opts->lint_unused_parameters, guchho::api::LintLevel::kWarning);
}

TEST(CliLint, CommaSeparatedChecksAreBothEnabled) {
    auto opts = BuildOptionsFor({"--lint=undefined,unused"});
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->lint_undefined_variables, guchho::api::LintLevel::kWarning);
    EXPECT_EQ(opts->lint_unused_variables, guchho::api::LintLevel::kWarning);
}

TEST(CliLint, LevelFlagOverridesOneCheck) {
    auto opts = BuildOptionsFor({"--lint=undefined,unused", "--lint-level=undefined=error"});
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->lint_undefined_variables, guchho::api::LintLevel::kError);
    EXPECT_EQ(opts->lint_unused_variables, guchho::api::LintLevel::kWarning);
}

TEST(CliLint, IndividualUnusedChecksCanBeSelected) {
    auto opts = BuildOptionsFor({"--lint-level=unused-variable=error"});
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->lint_unused_variables, guchho::api::LintLevel::kError);
    EXPECT_EQ(opts->lint_unused_parameters, guchho::api::LintLevel::kOff);
}

TEST(CliLint, TransformAcceptsTheSameFlags) {
    auto opts = TransformOptionsFor({"--lint=undefined", "--lint-level=unused-parameter=error"});
    ASSERT_TRUE(opts.has_value());
    EXPECT_EQ(opts->lint_undefined_variables, guchho::api::LintLevel::kWarning);
    EXPECT_EQ(opts->lint_unused_parameters, guchho::api::LintLevel::kError);
}

TEST(CliLint, UnknownCheckIsRejected) {
    auto opts = BuildOptionsFor({"--lint=undefned"});
    EXPECT_FALSE(opts.has_value());
}

TEST(CliLint, InvalidLevelIsRejected) {
    auto opts = BuildOptionsFor({"--lint-level=undefined=loud"});
    EXPECT_FALSE(opts.has_value());
}

TEST(CliLint, UnknownCheckSuggestsTheClosestName) {
    guchho::api::BuildOptions build;
    guchho::cli::ParseOptionsExtras extras;
    auto err = guchho::cli::parseOptionsImpl({"--lint=undefned"}, &build, nullptr,
                                             guchho::cli::ParseOptionsKind::kExternal, extras);
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->note.find("Did you mean \"undefined\""), std::string::npos);
}
