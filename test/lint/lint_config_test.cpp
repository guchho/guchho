#include "test/guchho_test.hpp"
#include "test/helpers/filesystem_test.hpp"

#include "guchho/lint.hpp"

#include <string>

using guchho::filesystem::MockKind;
using guchho::lint::Config;
using guchho::lint::LoadConfig;
using guchho::lint::Severity;

namespace {

std::unique_ptr<guchho::filesystem::Fs> ConfigFs(
    const std::unordered_map<std::string, std::string>& files,
    const std::string& cwd = "/project") {
    return guchho::test::MakeMockFS(files, MockKind::kUnix, cwd);
}

} // namespace

TEST(LintConfig, DefaultsMatchTheRuleTable) {
    Config config = guchho::lint::DefaultConfig();
    EXPECT_EQ(config.rules.at("no-undef"), Severity::kError);
    EXPECT_EQ(config.rules.at("no-unused-vars"), Severity::kWarning);
    EXPECT_EQ(config.rules.at("eqeqeq"), Severity::kWarning);
    EXPECT_EQ(config.rules.at("no-console"), Severity::kOff);
    EXPECT_EQ(config.platform, guchho::config::Platform::kBrowser);
    EXPECT_FALSE(config.report_unused_disable_directives);
    EXPECT_TRUE(config.include.empty());
    EXPECT_TRUE(config.exclude.empty());
}

TEST(LintConfig, MissingConfigLeavesDefaults) {
    auto fs = ConfigFs({});
    Config config;
    std::string error;
    EXPECT_TRUE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(config.path.empty());
    EXPECT_EQ(config.rules.at("no-undef"), Severity::kError);
}

TEST(LintConfig, JsonConfigAppliesRulesGlobalsAndIgnores) {
    auto fs = ConfigFs({{"/project/guchho.lint.config.json", R"({
        "rules": {
            "eqeqeq": "error",
            "no-debugger": 0,
            "no-unused-vars": ["warn"]
        },
        "globals": {
            "console": "readonly",
            "shadowed": "writable"
        },
        "ignores": ["vendor/**"],
        "platform": "node"
    })"}});
    Config config;
    std::string error;
    EXPECT_TRUE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(config.rules.at("eqeqeq"), Severity::kError);
    EXPECT_EQ(config.rules.at("no-debugger"), Severity::kOff);
    EXPECT_EQ(config.rules.at("no-unused-vars"), Severity::kWarning);
    EXPECT_FALSE(config.globals.at("console"));
    EXPECT_TRUE(config.globals.at("shadowed"));
    EXPECT_EQ(config.exclude.size(), 1u);
    EXPECT_EQ(config.exclude.front(), "vendor/**");
    EXPECT_EQ(config.platform, guchho::config::Platform::kNode);
    EXPECT_FALSE(config.path.empty());
}

TEST(LintConfig, SearchWalksUpToTheProjectRoot) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json", R"({"rules":{"no-console":"error"}})"},
        {"/project/src/a.js", "console.log(1);"},
    });
    Config config;
    std::string error;
    EXPECT_TRUE(LoadConfig(*fs, "/project/src", "", config, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(config.rules.at("no-console"), Severity::kError);
}

TEST(LintConfig, ExplicitConfigIsResolvedRelativeToTheWorkingDirectory) {
    auto fs = ConfigFs({
        {"/project/config/lint.json", R"({"rules":{"no-empty":"error"}})"},
    });
    Config config;
    std::string error;
    EXPECT_TRUE(LoadConfig(*fs, "/project", "config/lint.json", config, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(config.rules.at("no-empty"), Severity::kError);
}

TEST(LintConfig, MissingExplicitConfigIsAnOperationalError) {
    auto fs = ConfigFs({});
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "missing.json", config, error));
    EXPECT_NE(error.find("cannot find lint configuration"), std::string::npos);
}

TEST(LintConfig, ExtendsLoadsTheBaseFirstAndChildWins) {
    auto fs = ConfigFs({
        {"/project/base.json", R"({
            "rules": {"eqeqeq": "error", "no-debugger": "off"},
            "globals": {"base": true}
        })"},
        {"/project/guchho.lint.config.json", R"({
            "extends": "./base.json",
            "rules": {"no-debugger": "error"},
            "globals": {"child": true}
        })"},
    });
    Config config;
    std::string error;
    EXPECT_TRUE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(config.rules.at("eqeqeq"), Severity::kError);
    EXPECT_EQ(config.rules.at("no-debugger"), Severity::kError);
    EXPECT_TRUE(config.globals.at("base"));
    EXPECT_TRUE(config.globals.at("child"));
}

TEST(LintConfig, ExtendsCycleIsRejected) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json", R"({"extends":"./base.json"})"},
        {"/project/base.json", R"({"extends":"./guchho.lint.config.json"})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("cycle"), std::string::npos);
}

TEST(LintConfig, UnknownRuleSuggestsTheClosestName) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json", R"({"rules":{"no-undefi":"error"}})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("unknown rule"), std::string::npos);
    EXPECT_NE(error.find("no-undef"), std::string::npos);
}

TEST(LintConfig, RuleOptionsAreRejected) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json",
         R"({"rules":{"no-console":["error",{"allow":["warn"]}]}})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("does not accept options"), std::string::npos);
}

TEST(LintConfig, OverridesAreRejectedWithAUsefulMessage) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json", R"({"overrides":[]})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("not supported"), std::string::npos);
    EXPECT_NE(error.find("include/exclude"), std::string::npos);
}

TEST(LintConfig, InvalidGlobIsRejected) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json", R"({"include":["src/*.{js,ts"]})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("invalid include pattern"), std::string::npos);
}

TEST(LintConfig, InvalidSeverityIsRejected) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json", R"({"rules":{"eqeqeq":"loud"}})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("invalid severity"), std::string::npos);
}

TEST(LintConfig, ReportUnusedDisableDirectivesFalseIsAccepted) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json",
         R"({"reportUnusedDisableDirectives":false})"},
    });
    Config config;
    std::string error;
    EXPECT_TRUE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_FALSE(config.report_unused_disable_directives);
}

TEST(LintConfig, ReportUnusedDisableDirectivesTrueIsRejected) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json",
         R"({"reportUnusedDisableDirectives":true})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("not supported"), std::string::npos);
}

TEST(LintConfig, ModuleAndLatestLanguageOptionsAreAccepted) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json",
         R"({"languageOptions":{"sourceType":"module","ecmaVersion":"latest"}})"},
    });
    Config config;
    std::string error;
    EXPECT_TRUE(LoadConfig(*fs, "/project", "", config, error));
}

TEST(LintConfig, UnsupportedSourceTypeIsRejected) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json",
         R"({"languageOptions":{"sourceType":"script"}})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("only supports"), std::string::npos);
}

TEST(LintConfig, UnsupportedEcmaVersionIsRejected) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json",
         R"({"languageOptions":{"ecmaVersion":2022}})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("only supports"), std::string::npos);
}

TEST(LintConfig, UnknownLanguageOptionIsRejected) {
    auto fs = ConfigFs({
        {"/project/guchho.lint.config.json",
         R"({"languageOptions":{"sourceType":"module","jsx":true}})"},
    });
    Config config;
    std::string error;
    EXPECT_FALSE(LoadConfig(*fs, "/project", "", config, error));
    EXPECT_NE(error.find("unknown language option"), std::string::npos);
}
