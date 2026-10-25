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

bool HasMessageContaining(const std::vector<Msg>& msgs, const std::string& text) {
    for (const Msg& msg : msgs) {
        if (msg.data.text.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
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

TEST(JsDiagnosticsTest, UnterminatedStringExplainsExpectedQuote) {
    auto msgs = MessagesFor("let x = \"abc");
    EXPECT_TRUE(HasMessageContaining(msgs, "Unterminated string literal. Expected a closing quote."));
}

TEST(JsDiagnosticsTest, UnterminatedTemplateExplainsExpectedBacktick) {
    auto msgs = MessagesFor("let x = `abc");
    EXPECT_TRUE(HasMessageContaining(msgs, "Unterminated template literal. Expected a closing backtick."));
}

TEST(JsDiagnosticsTest, UnterminatedRegexpExplainsExpectedSlash) {
    auto msgs = MessagesFor("let x = /abc");
    EXPECT_TRUE(HasMessageContaining(msgs, "Unterminated regular expression. Expected a closing \"/\"."));
}

TEST(JsDiagnosticsTest, UnexpectedEndOfFileExplainsIncompleteInput) {
    auto msgs = MessagesFor("let x = 0x");
    EXPECT_TRUE(HasMessageContaining(msgs, "Unexpected end of file. The statement or expression is incomplete."));
}

TEST(JsDiagnosticsTest, InvalidTokenUsesClearWording) {
    auto msgs = MessagesFor("let x = 1x;");
    EXPECT_TRUE(HasMessageContaining(msgs, "Invalid or unexpected token \"x\""));
}

TEST(JsDiagnosticsTest, MissingSemicolonExplainsStatementTerminator) {
    auto msgs = MessagesFor("foo bar;");
    EXPECT_TRUE(HasMessageContaining(msgs, "Expected \";\" after this statement or declaration."));
}

TEST(JsDiagnosticsTest, DuplicateDeclarationMentionsScope) {
    auto msgs = MessagesFor("let x; let x;");
    EXPECT_TRUE(HasMessageContaining(msgs, "\"x\" has already been declared in this scope"));
}

TEST(JsDiagnosticsTest, InvalidAssignmentTargetExplainsExpectedOperand) {
    auto msgs = MessagesFor("x + 1 = 2;");
    EXPECT_TRUE(HasMessageContaining(msgs, "Invalid assignment target. Expected a variable or assignable expression."));
}

TEST(JsDiagnosticsTest, ConstInitializerMessageIsSpecific) {
    auto msgs = MessagesFor("const x;");
    EXPECT_TRUE(HasMessageContaining(msgs, "Missing initializer in const declaration for \"x\""));
}

TEST(JsDiagnosticsTest, MultilineMissingSemicolonIsStillReported) {
    auto msgs = MessagesFor("if (ready) {\n  foo bar\n}");
    EXPECT_TRUE(HasMessageContaining(msgs, "Expected \";\" after this statement or declaration."));
}

TEST(JsDiagnosticsTest, ValidStringAndTemplateProduceNoMessages) {
    EXPECT_TRUE(MessagesFor("let a = \"ok\";\nlet b = `ok`;").empty());
}

TEST(JsDiagnosticsTest, NewlineStillAllowsSemicolonInsertion) {
    EXPECT_TRUE(MessagesFor("foo\nbar").empty());
}

TEST(JsDiagnosticsTest, ParseDoesNotModifySourceBuffer) {
    const std::string original = "consts x = 1;";
    Source source = SourceForTest(original);
    config::Options options{};
    Log log = NewDeferLog(DeferLogKind::kDeferLogNoVerboseOrDebug, {});
    js::Parse(log, source, js::OptionsFromConfig(&options));
    EXPECT_EQ(source.contents, original);
}
