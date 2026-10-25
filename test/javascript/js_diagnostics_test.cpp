#include "test/helpers/javascript_test.hpp"

#include <string>
#include <vector>

using namespace guchho::logger;

namespace {

std::vector<Msg> MessagesFor(const std::string& source) {
    config::Options options{};
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    js::Parse(log, SourceForTest(source), js::OptionsFromConfig(&options));
    return log.done();
}

const Msg* FindMessageWithFixes(const std::vector<Msg>& msgs) {
    for (const Msg& msg : msgs) {
        if (!msg.data.fixes.empty()) {
            return &msg;
        }
    }
    return nullptr;
}

} // namespace

TEST(JsDiagnosticsTest, KeywordTypoAttachesReplacementFix) {
    auto msgs = MessagesFor("consts x = 1;");
    const Msg* msg = FindMessageWithFixes(msgs);
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->data.fixes.size(), 1u);

    const Fix& fix = msg->data.fixes[0];
    EXPECT_EQ(fix.range.loc.start, 0);
    EXPECT_EQ(fix.range.len, 6);
    EXPECT_EQ(fix.replacement, "const");
    EXPECT_EQ(fix.safety, FixSafety::kSafe);
    EXPECT_NE(fix.explanation.find("const"), std::string::npos);

    ASSERT_NE(msg->data.location, nullptr);
    EXPECT_NE(msg->data.location->suggestion.find("const"), std::string::npos);
    EXPECT_NE(msg->data.text.find("Expected"), std::string::npos);
}

TEST(JsDiagnosticsTest, MissingDelimiterAttachesInsertionFix) {
    const std::string source = "if (ready { run(); }";
    auto msgs = MessagesFor(source);
    const Msg* msg = FindMessageWithFixes(msgs);
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->data.fixes.size(), 1u);

    const Fix& fix = msg->data.fixes[0];
    EXPECT_EQ(fix.range.loc.start, static_cast<int32_t>(source.find('{')));
    EXPECT_EQ(fix.range.len, 0);
    EXPECT_EQ(fix.replacement, ")");
    EXPECT_EQ(fix.safety, FixSafety::kSafe);
    EXPECT_NE(msg->data.text.find("Expected"), std::string::npos);
}

TEST(JsDiagnosticsTest, OrdinaryIdentifierStatementsGetNoFix) {
    auto msgs = MessagesFor("consts + 1;");
    EXPECT_TRUE(msgs.empty());
}

TEST(JsDiagnosticsTest, ValidKeywordLikeCallGetsNoFix) {
    auto msgs = MessagesFor("iff(x);");
    EXPECT_TRUE(msgs.empty());
}

TEST(JsDiagnosticsTest, FunctionTypoAttachesReplacementFix) {
    auto msgs = MessagesFor("functon f() {}");
    const Msg* msg = FindMessageWithFixes(msgs);
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->data.fixes.size(), 1u);
    EXPECT_EQ(msg->data.fixes[0].replacement, "function");
}

TEST(JsDiagnosticsTest, ControlKeywordTypoAttachesReplacementFix) {
    auto msgs = MessagesFor("iff (ready) {}");
    const Msg* msg = FindMessageWithFixes(msgs);
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->data.fixes.size(), 1u);
    EXPECT_EQ(msg->data.fixes[0].replacement, "if");
}

TEST(JsDiagnosticsTest, AmbiguousKeywordTypoGetsNoFix) {
    auto msgs = MessagesFor("far x = 1;");
    EXPECT_EQ(FindMessageWithFixes(msgs), nullptr);
}

TEST(JsDiagnosticsTest, UnexpectedIdentifierAfterExpressionGetsNoFix) {
    auto msgs = MessagesFor("foo bar;");
    EXPECT_EQ(FindMessageWithFixes(msgs), nullptr);
}

TEST(JsDiagnosticsTest, MissingBracketAttachesInsertionFix) {
    auto msgs = MessagesFor("let x = [1, 2;");
    const Msg* msg = FindMessageWithFixes(msgs);
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->data.fixes.size(), 1u);
    EXPECT_EQ(msg->data.fixes[0].replacement, "]");
    EXPECT_EQ(msg->data.fixes[0].range.len, 0);
}

TEST(JsDiagnosticsTest, MissingBraceAttachesInsertionFix) {
    auto msgs = MessagesFor("let x = {a: 1;");
    const Msg* msg = FindMessageWithFixes(msgs);
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->data.fixes.size(), 1u);
    EXPECT_EQ(msg->data.fixes[0].replacement, "}");
    EXPECT_EQ(msg->data.fixes[0].range.len, 0);
}

TEST(JsDiagnosticsTest, MissingBlockBraceGetsNoFix) {
    auto msgs = MessagesFor("try x;");
    EXPECT_EQ(FindMessageWithFixes(msgs), nullptr);
}

TEST(JsDiagnosticsTest, EndOfFileErrorGetsNoFix) {
    auto msgs = MessagesFor("if (ready");
    EXPECT_EQ(FindMessageWithFixes(msgs), nullptr);
}

TEST(JsDiagnosticsTest, MultilineKeywordTypoFixKeepsOriginalRange) {
    const std::string source = "consts x =\n  1;";
    auto msgs = MessagesFor(source);
    const Msg* msg = FindMessageWithFixes(msgs);
    ASSERT_NE(msg, nullptr);
    ASSERT_EQ(msg->data.fixes.size(), 1u);
    EXPECT_EQ(msg->data.fixes[0].range.loc.start, 0);
    EXPECT_EQ(msg->data.fixes[0].range.len, 6);
}

TEST(JsDiagnosticsTest, ParseDoesNotModifySourceBuffer) {
    const std::string original = "consts x = 1;";
    Source source = SourceForTest(original);
    config::Options options{};
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    js::Parse(log, source, js::OptionsFromConfig(&options));
    EXPECT_EQ(source.contents, original);
}
