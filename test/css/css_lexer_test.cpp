#include "test/guchho_test.hpp"

#include "guchho/css/css_lexer.hpp"
#include "guchho/logger.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace css = guchho::css;
namespace lexer = css::lexer;

using guchho::logger::DeferLogKind;
using guchho::logger::Loc;
using guchho::logger::Log;
using guchho::logger::MsgID;
using guchho::logger::NewDeferLog;
using guchho::logger::Source;

namespace {

Source MakeSource(std::string contents) {
    Source s;
    s.contents = std::move(contents);
    return s;
}

struct TokenizeOut {
    lexer::TokenizeResult result;
    Log log;
};

TokenizeOut Tokenize(std::string contents, const lexer::Options& options = {}) {
    auto source = MakeSource(std::move(contents));
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    auto result = lexer::Tokenize(log, source, options);
    return TokenizeOut{std::move(result), std::move(log)};
}

struct TokenInfo {
    css::TokenType kind;
    std::string text;
    css::TokenFlags flags{};
    uint16_t unit_offset{};
};

std::vector<TokenInfo> TokenInfos(const TokenizeOut& out, std::string_view contents) {
    std::vector<TokenInfo> v;
    v.reserve(out.result.tokens.size());
    for (auto const& t : out.result.tokens) {
        v.push_back(TokenInfo{t.kind, t.DecodedText(contents), t.flags, t.unit_offset});
    }
    return v;
}

std::vector<css::TokenType> Kinds(std::string_view css_text) {
    auto out = Tokenize(std::string(css_text));
    std::vector<css::TokenType> kinds;
    kinds.reserve(out.result.tokens.size());
    for (auto const& t : out.result.tokens) {
        kinds.push_back(t.kind);
    }
    return kinds;
}

size_t CountMessagesOfID(const Log& log, MsgID id) {
    auto msgs = log.peek();
    size_t count = 0;
    for (auto const& m : msgs) {
        if (m.id == id) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// ToString
// ---------------------------------------------------------------------------

TEST(CssLexerToString, AllKindsMapToAString)
{
    for (int i = 0; i <= static_cast<int>(css::TokenType::kSymbol); ++i) {
        auto kind = static_cast<css::TokenType>(i);
        EXPECT_FALSE(lexer::ToString(kind).empty());
    }
}

TEST(CssLexerToString, KnownValues)
{
    EXPECT_EQ(lexer::ToString(css::TokenType::kEndOfFile), "end of file");
    EXPECT_EQ(lexer::ToString(css::TokenType::kAtKeyword), "@-keyword");
    EXPECT_EQ(lexer::ToString(css::TokenType::kIdent), "identifier");
    EXPECT_EQ(lexer::ToString(css::TokenType::kNumber), "number");
    EXPECT_EQ(lexer::ToString(css::TokenType::kUrl), "URL token");
    EXPECT_EQ(lexer::ToString(css::TokenType::kCdo), "\"<!--\"");
    EXPECT_EQ(lexer::ToString(css::TokenType::kCdc), "\"-->\"");
    EXPECT_EQ(lexer::ToString(css::TokenType::kDelimAmpersand), "\"&\"");
    EXPECT_EQ(lexer::ToString(css::TokenType::kDelimDot), "\".\"");
}

// ---------------------------------------------------------------------------
// IsNumeric
// ---------------------------------------------------------------------------

TEST(CssLexerIsNumeric, NumericKinds)
{
    EXPECT_TRUE(css::IsNumeric(css::TokenType::kNumber));
    EXPECT_TRUE(css::IsNumeric(css::TokenType::kPercentage));
    EXPECT_TRUE(css::IsNumeric(css::TokenType::kDimension));
}

TEST(CssLexerIsNumeric, NonNumericKinds)
{
    EXPECT_FALSE(css::IsNumeric(css::TokenType::kIdent));
    EXPECT_FALSE(css::IsNumeric(css::TokenType::kString));
    EXPECT_FALSE(css::IsNumeric(css::TokenType::kDelim));
    EXPECT_FALSE(css::IsNumeric(css::TokenType::kFunction));
}

// ---------------------------------------------------------------------------
// IsNameStart / IsNameContinue
// ---------------------------------------------------------------------------

TEST(CssLexerIsNameStart, Characters)
{
    EXPECT_TRUE(css::IsNameStart('a'));
    EXPECT_TRUE(css::IsNameStart('Z'));
    EXPECT_TRUE(css::IsNameStart('_'));
    EXPECT_TRUE(css::IsNameStart(0xE9));
    EXPECT_TRUE(css::IsNameStart('\x00'));

    EXPECT_FALSE(css::IsNameStart('0'));
    EXPECT_FALSE(css::IsNameStart('-'));
    EXPECT_FALSE(css::IsNameStart(' '));
    EXPECT_FALSE(css::IsNameStart('\\'));
}

TEST(CssLexerIsNameContinue, Characters)
{
    EXPECT_TRUE(css::IsNameContinue('a'));
    EXPECT_TRUE(css::IsNameContinue('9'));
    EXPECT_TRUE(css::IsNameContinue('-'));

    EXPECT_FALSE(css::IsNameContinue(' '));
    EXPECT_FALSE(css::IsNameContinue('\\'));
    EXPECT_FALSE(css::IsNameContinue('\n'));
}

// ---------------------------------------------------------------------------
// WouldStartIdentifierWithoutEscapes
// ---------------------------------------------------------------------------

TEST(CssLexerWouldStartIdentifierWithoutEscapes, Basic)
{
    EXPECT_TRUE(css::WouldStartIdentifierWithoutEscapes("foo"));
    EXPECT_TRUE(css::WouldStartIdentifierWithoutEscapes("_foo"));
    EXPECT_TRUE(css::WouldStartIdentifierWithoutEscapes("-foo"));
    EXPECT_TRUE(css::WouldStartIdentifierWithoutEscapes("--foo"));
    EXPECT_TRUE(css::WouldStartIdentifierWithoutEscapes("-\xC3\xA9"));

    EXPECT_FALSE(css::WouldStartIdentifierWithoutEscapes(""));
    EXPECT_FALSE(css::WouldStartIdentifierWithoutEscapes("-"));
    EXPECT_FALSE(css::WouldStartIdentifierWithoutEscapes("-1"));
    EXPECT_FALSE(css::WouldStartIdentifierWithoutEscapes("1foo"));
    EXPECT_FALSE(css::WouldStartIdentifierWithoutEscapes("\\66 foo"));
}

// ---------------------------------------------------------------------------
// WouldStartIdentifier / IsValidEscapeAt
// ---------------------------------------------------------------------------

TEST(CssLexerWouldStartIdentifier, AtLocations)
{
    auto source = MakeSource("a1\\6");
    EXPECT_TRUE(css::WouldStartIdentifier(source, Loc{0}));
    EXPECT_FALSE(css::WouldStartIdentifier(source, Loc{1}));
    EXPECT_FALSE(css::WouldStartIdentifier(source, Loc{2}));
}

TEST(CssLexerIsValidEscapeAt, BackslashFollowedByNewlineIsInvalid)
{
    auto valid = MakeSource("a\\6");
    EXPECT_TRUE(css::IsValidEscapeAt(valid, Loc{1}));

    auto invalid = MakeSource("a\\\n");
    EXPECT_FALSE(css::IsValidEscapeAt(invalid, Loc{1}));
}

// ---------------------------------------------------------------------------
// RangeOfIdentifier
// ---------------------------------------------------------------------------

TEST(CssLexerRangeOfIdentifier, Lengths)
{
    auto source = MakeSource("foo bar");
    auto r = css::RangeOfIdentifier(source, Loc{0});
    EXPECT_EQ(r.len, 3);

    auto hyphen = MakeSource("foo-bar");
    EXPECT_EQ(css::RangeOfIdentifier(hyphen, Loc{0}).len, 7);

    auto escaped = MakeSource("f\\6F oo");
    EXPECT_EQ(css::RangeOfIdentifier(escaped, Loc{0}).len, 7);
}

// ---------------------------------------------------------------------------
// Basic token sequences
// ---------------------------------------------------------------------------

TEST(CssLexerTokenize, BasicRule)
{
    auto out = Tokenize("a{color:red}");
    auto infos = TokenInfos(out, "a{color:red}");

    ASSERT_EQ(infos.size(), 6u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kIdent);
    EXPECT_EQ(infos[0].text, "a");
    EXPECT_EQ(infos[1].kind, css::TokenType::kOpenBrace);
    EXPECT_EQ(infos[2].kind, css::TokenType::kIdent);
    EXPECT_EQ(infos[2].text, "color");
    EXPECT_EQ(infos[3].kind, css::TokenType::kColon);
    EXPECT_EQ(infos[4].kind, css::TokenType::kIdent);
    EXPECT_EQ(infos[4].text, "red");
    EXPECT_EQ(infos[5].kind, css::TokenType::kCloseBrace);
    EXPECT_TRUE(out.log.peek().empty());
}

TEST(CssLexerTokenize, WhitespaceCollapsesIntoOneToken)
{
    auto out = Tokenize("  \t\n ");
    auto infos = TokenInfos(out, "  \t\n ");

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kWhitespace);
    EXPECT_EQ(infos[0].text, "  \t\n ");
}

TEST(CssLexerTokenize, CommentIsSkipped)
{
    auto out = Tokenize("a/*x*/b");
    auto infos = TokenInfos(out, "a/*x*/b");

    ASSERT_EQ(infos.size(), 2u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kIdent);
    EXPECT_EQ(infos[0].text, "a");
    EXPECT_EQ(infos[1].kind, css::TokenType::kIdent);
    EXPECT_EQ(infos[1].text, "b");
}

TEST(CssLexerTokenize, RecordAllComments)
{
    lexer::Options options;
    options.record_all_comments = true;
    auto out = Tokenize("a/* hi */b", options);

    EXPECT_TRUE(out.result.tokens.empty() == false);
    EXPECT_EQ(out.result.all_comments.size(), 1u);
    EXPECT_EQ(out.result.all_comments[0].len, 8);
}

TEST(CssLexerTokenize, LegalComment)
{
    auto out = Tokenize("/*! keep */");
    ASSERT_EQ(out.result.legal_comments.size(), 1u);
    EXPECT_TRUE(out.result.legal_comments[0].text.starts_with("/*!"));

    auto preserve = Tokenize("/*@preserve keep*/");
    EXPECT_EQ(preserve.result.legal_comments.size(), 1u);
}

TEST(CssLexerTokenize, SourceMappingURLComment)
{
    auto out = Tokenize("/*# sourceMappingURL=x.map */");
    EXPECT_EQ(out.result.source_map_comment.text, "x.map");
}

TEST(CssLexerTokenize, ApproximateLineCount)
{
    EXPECT_EQ(Tokenize("a").result.approximate_line_count, 1);
    EXPECT_EQ(Tokenize("a\nb\nc").result.approximate_line_count, 3);
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

TEST(CssLexerTokenize, Strings)
{
    auto out = Tokenize("\"foo\" 'bar'");
    auto infos = TokenInfos(out, "\"foo\" 'bar'");

    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kString);
    EXPECT_EQ(infos[0].text, "foo");
    EXPECT_EQ(infos[1].kind, css::TokenType::kWhitespace);
    EXPECT_EQ(infos[2].kind, css::TokenType::kString);
    EXPECT_EQ(infos[2].text, "bar");
}

TEST(CssLexerTokenize, StringWithEscape)
{
    auto out = Tokenize("\"a\\\"b\"");
    auto infos = TokenInfos(out, "\"a\\\"b\"");

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kString);
    EXPECT_EQ(infos[0].text, "a\"b");
}

TEST(CssLexerTokenize, UnterminatedStringWarns)
{
    auto out = Tokenize("\"foo");
    auto infos = TokenInfos(out, "\"foo");

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kUnterminatedString);
    EXPECT_EQ(CountMessagesOfID(out.log, MsgID::kCSS_CSSSyntaxError), 1u);
}

// ---------------------------------------------------------------------------
// Numbers / dimensions / percentages
// ---------------------------------------------------------------------------

TEST(CssLexerTokenize, Numbers)
{
    std::string inputs[] = {"123", "-1", "+1.5", ".5", "1e3", "1e+2"};
    for (auto const& input : inputs) {
        auto kinds = Kinds(input);
        ASSERT_EQ(kinds.size(), 1u) << input;
        EXPECT_EQ(kinds[0], css::TokenType::kNumber) << input;
    }
}

TEST(CssLexerTokenize, Percentage)
{
    auto out = Tokenize("50%");
    auto infos = TokenInfos(out, "50%");
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kPercentage);
    EXPECT_EQ(infos[0].text, "50%");
}

TEST(CssLexerTokenize, Dimension)
{
    auto out = Tokenize("10px");
    auto infos = TokenInfos(out, "10px");
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kDimension);
    EXPECT_EQ(infos[0].unit_offset, 2);
    EXPECT_EQ(infos[0].text, "10px");
}

// ---------------------------------------------------------------------------
// Hash
// ---------------------------------------------------------------------------

TEST(CssLexerTokenize, Hash)
{
    auto out = Tokenize("#foo #123 #");
    auto infos = TokenInfos(out, "#foo #123 #");

    ASSERT_EQ(infos.size(), 5u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kHash);
    EXPECT_EQ(infos[0].text, "foo");
    EXPECT_NE(infos[0].flags & css::kIsID, 0);

    EXPECT_EQ(infos[1].kind, css::TokenType::kWhitespace);

    EXPECT_EQ(infos[2].kind, css::TokenType::kHash);
    EXPECT_EQ(infos[2].text, "123");
    EXPECT_EQ(infos[2].flags & css::kIsID, 0);

    EXPECT_EQ(infos[3].kind, css::TokenType::kWhitespace);

    EXPECT_EQ(infos[4].kind, css::TokenType::kDelim);
}

// ---------------------------------------------------------------------------
// URL
// ---------------------------------------------------------------------------

TEST(CssLexerTokenize, Url)
{
    auto out = Tokenize("url(foo.png)");
    auto infos = TokenInfos(out, "url(foo.png)");

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kUrl);
    EXPECT_EQ(infos[0].text, "foo.png");
}

TEST(CssLexerTokenize, UrlUppercase)
{
    auto out = Tokenize("URL(http://x)");
    auto infos = TokenInfos(out, "URL(http://x)");
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kUrl);
    EXPECT_EQ(infos[0].text, "http://x");
}

TEST(CssLexerTokenize, UrlWithWhitespaceIsBadUrl)
{
    auto out = Tokenize("url(foo bar.png)");
    auto infos = TokenInfos(out, "url(foo bar.png)");

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kBadUrl);
    EXPECT_EQ(CountMessagesOfID(out.log, MsgID::kCSS_CSSSyntaxError), 1u);
}

TEST(CssLexerTokenize, UrlWithSurroundingWhitespaceIsValid)
{
    auto out = Tokenize("url( foo.png )");
    auto infos = TokenInfos(out, "url( foo.png )");

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kUrl);
    EXPECT_EQ(infos[0].text, "foo.png");
}

TEST(CssLexerTokenize, UrlAtEOFWarns)
{
    auto out = Tokenize("url(foo");
    auto infos = TokenInfos(out, "url(foo");

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kUrl);
    EXPECT_EQ(CountMessagesOfID(out.log, MsgID::kCSS_CSSSyntaxError), 1u);
}

TEST(CssLexerTokenize, UrlWithQuotesIsFunction)
{
    auto out = Tokenize("url('foo')");
    auto infos = TokenInfos(out, "url('foo')");

    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kFunction);
    EXPECT_EQ(infos[0].text, "url");
    EXPECT_EQ(infos[1].kind, css::TokenType::kString);
    EXPECT_EQ(infos[2].kind, css::TokenType::kCloseParen);
}

// ---------------------------------------------------------------------------
// CDO / CDC / at-keyword / function
// ---------------------------------------------------------------------------

TEST(CssLexerTokenize, CdoAndCdc)
{
    EXPECT_EQ(Kinds("<!--")[0], css::TokenType::kCdo);
    EXPECT_EQ(Kinds("-->")[0], css::TokenType::kCdc);
}

TEST(CssLexerTokenize, AtKeyword)
{
    auto out = Tokenize("@media @");
    auto infos = TokenInfos(out, "@media @");

    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kAtKeyword);
    EXPECT_EQ(infos[0].text, "media");
    EXPECT_EQ(infos[1].kind, css::TokenType::kWhitespace);
    EXPECT_EQ(infos[2].kind, css::TokenType::kDelim);
    EXPECT_EQ(infos[2].text, "@");
}

TEST(CssLexerTokenize, Function)
{
    auto out = Tokenize("foo()");
    auto infos = TokenInfos(out, "foo()");

    ASSERT_EQ(infos.size(), 2u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kFunction);
    EXPECT_EQ(infos[0].text, "foo");
    EXPECT_EQ(infos[1].kind, css::TokenType::kCloseParen);
}

// ---------------------------------------------------------------------------
// Delimiters
// ---------------------------------------------------------------------------

TEST(CssLexerTokenize, Delimiters)
{
    struct Case {
        std::string input;
        css::TokenType kind;
    };
    Case cases[] = {
        {">", css::TokenType::kDelimGreaterThan},
        {"~", css::TokenType::kDelimTilde},
        {"&", css::TokenType::kDelimAmpersand},
        {"*", css::TokenType::kDelimAsterisk},
        {"|", css::TokenType::kDelimBar},
        {"!", css::TokenType::kDelimExclamation},
        {"=", css::TokenType::kDelimEquals},
        {"^", css::TokenType::kDelimCaret},
        {"$", css::TokenType::kDelimDollar},
        {"/", css::TokenType::kDelimSlash},
        {"+", css::TokenType::kDelimPlus},
        {".", css::TokenType::kDelimDot},
        {"-", css::TokenType::kDelimMinus},
        {"<", css::TokenType::kDelimLessThan},
        {"?", css::TokenType::kDelim},
    };
    for (auto const& c : cases) {
        auto kinds = Kinds(c.input);
        ASSERT_EQ(kinds.size(), 1u) << c.input;
        EXPECT_EQ(kinds[0], c.kind) << c.input;
    }
}

// ---------------------------------------------------------------------------
// Single-line comments
// ---------------------------------------------------------------------------

TEST(CssLexerTokenize, DoubleSlashWarnsAndIsDelim)
{
    auto out = Tokenize("//foo");
    auto infos = TokenInfos(out, "//foo");

    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kDelimSlash);
    EXPECT_NE(infos[0].flags & css::kDidWarnAboutSingleLineComment, 0);
    EXPECT_EQ(infos[1].kind, css::TokenType::kDelimSlash);
    EXPECT_EQ(infos[2].kind, css::TokenType::kIdent);
    EXPECT_EQ(CountMessagesOfID(out.log, MsgID::kCSS_JSCommentInCSS), 1u);
}

// ---------------------------------------------------------------------------
// Escapes
// ---------------------------------------------------------------------------

TEST(CssLexerTokenize, EscapedIdentDecodes)
{
    auto out = Tokenize("f\\6F o");
    auto infos = TokenInfos(out, "f\\6F o");

    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kIdent);
    EXPECT_EQ(infos[0].text, "foo");
}

TEST(CssLexerTokenize, InvalidEscapeAfterBackslash)
{
    auto out = Tokenize("\\\n");
    auto infos = TokenInfos(out, "\\\n");

    ASSERT_EQ(infos.size(), 2u);
    EXPECT_EQ(infos[0].kind, css::TokenType::kDelim);
    EXPECT_EQ(infos[1].kind, css::TokenType::kWhitespace);
    EXPECT_FALSE(out.log.peek().empty());
}

} // namespace
