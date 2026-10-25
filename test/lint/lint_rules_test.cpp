#include "test/guchho_test.hpp"

#include "guchho/lint.hpp"

#include <set>
#include <string>

using namespace guchho::lint;

TEST(LintRules, DefaultsMatchTheDocumentedTable) {
    EXPECT_EQ(DefaultSeverity(Rule::kNoUndef), Severity::kError);
    EXPECT_EQ(DefaultSeverity(Rule::kNoUnusedVars), Severity::kWarning);
    EXPECT_EQ(DefaultSeverity(Rule::kNoRedeclare), Severity::kError);
    EXPECT_EQ(DefaultSeverity(Rule::kNoUseBeforeDefine), Severity::kOff);
    EXPECT_EQ(DefaultSeverity(Rule::kNoShadow), Severity::kOff);
    EXPECT_EQ(DefaultSeverity(Rule::kNoUnreachable), Severity::kError);
    EXPECT_EQ(DefaultSeverity(Rule::kNoDuplicateImports), Severity::kError);
    EXPECT_EQ(DefaultSeverity(Rule::kNoConstantCondition), Severity::kWarning);
    EXPECT_EQ(DefaultSeverity(Rule::kValidTypeof), Severity::kError);
    EXPECT_EQ(DefaultSeverity(Rule::kNoConsole), Severity::kOff);
    EXPECT_EQ(DefaultSeverity(Rule::kEqeqeq), Severity::kWarning);
    EXPECT_EQ(DefaultSeverity(Rule::kNoDebugger), Severity::kError);
    EXPECT_EQ(DefaultSeverity(Rule::kNoDuplicateCase), Severity::kError);
    EXPECT_EQ(DefaultSeverity(Rule::kNoEmpty), Severity::kWarning);
    EXPECT_EQ(DefaultSeverity(Rule::kImportNoUnresolved), Severity::kError);
}

TEST(LintRules, EveryRuleHasAUniqueName) {
    std::set<std::string> names;
    for (const RuleInfo& info : AllRules()) {
        EXPECT_FALSE(info.name.empty());
        EXPECT_TRUE(names.insert(std::string(info.name)).second);
        EXPECT_EQ(RuleFromName(info.name), info.id);
        EXPECT_EQ(RuleName(info.id), info.name);
    }
}

TEST(LintRules, OnlyExpectedRulesAreFixable) {
    EXPECT_TRUE(AllRules()[static_cast<size_t>(Rule::kEqeqeq)].fixable);
    EXPECT_TRUE(AllRules()[static_cast<size_t>(Rule::kNoDebugger)].fixable);
    EXPECT_FALSE(AllRules()[static_cast<size_t>(Rule::kNoUndef)].fixable);
}

TEST(LintRules, SeverityParsingAcceptsNamesAndNumbers) {
    EXPECT_EQ(ParseSeverity("off"), Severity::kOff);
    EXPECT_EQ(ParseSeverity("0"), Severity::kOff);
    EXPECT_EQ(ParseSeverity("warn"), Severity::kWarning);
    EXPECT_EQ(ParseSeverity("warning"), Severity::kWarning);
    EXPECT_EQ(ParseSeverity("1"), Severity::kWarning);
    EXPECT_EQ(ParseSeverity("error"), Severity::kError);
    EXPECT_EQ(ParseSeverity("2"), Severity::kError);
    EXPECT_FALSE(ParseSeverity("loud").has_value());
}

TEST(LintRules, SeverityNamesRoundTrip) {
    EXPECT_EQ(SeverityName(Severity::kOff), "off");
    EXPECT_EQ(SeverityName(Severity::kWarning), "warning");
    EXPECT_EQ(SeverityName(Severity::kError), "error");
}

TEST(LintRules, SuggestionsUseTheClosestRuleName) {
    EXPECT_EQ(SuggestRuleName("no-undef"), "no-undef");
    EXPECT_EQ(SuggestRuleName("no-undefi"), "no-undef");
    EXPECT_EQ(SuggestRuleName("eqeqe"), "eqeqeq");
    EXPECT_TRUE(SuggestRuleName("completely-unrelated").empty());
}

TEST(LintRules, UnknownRuleHasNoName) {
    EXPECT_FALSE(RuleFromName("nope").has_value());
    EXPECT_EQ(RuleName(Rule::kCount), "");
    EXPECT_EQ(DefaultSeverity(Rule::kCount), Severity::kOff);
}
