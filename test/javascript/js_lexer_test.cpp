#include "test/guchho_test.hpp"

#include "guchho/javascript/js_lexer.hpp"
#include "guchho/logger.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace js = guchho::javascript;
namespace config = guchho::config;

using js::T;

using guchho::logger::DeferLogKind;
using guchho::logger::Log;
using guchho::logger::NewDeferLog;
using guchho::logger::Source;

namespace {

Source MakeSource(std::string contents) {
    Source s;
    s.contents = std::move(contents);
    return s;
}

struct TokenOut {
    T kind{};
    std::string raw;
    std::string identifier;
    double number{};
};

struct LexAllOut {
    std::vector<TokenOut> tokens;
    Log log;
    bool stopped_early{};
};

// Drives a streaming JavaScript lexer over the whole input, collecting each
// token's kind and raw text. Syntax errors (LexerPanic) are caught.
LexAllOut LexAll(const std::string& contents, config::TSOptions ts = {},
    bool expand_regular_expressions = false) {
    LexAllOut out;
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    try {
        std::unique_ptr<js::Lexer> lexer;
        lexer = std::make_unique<js::Lexer>(log, MakeSource(contents), std::move(ts));
        for (;;) {
            if (lexer->token == T::kEndOfFile) {
                break;
            }
            if (expand_regular_expressions && lexer->token == T::kSlash) {
                lexer->ScanRegExp();
            }
            TokenOut token;
            token.kind = lexer->token;
            token.raw = std::string(lexer->Raw());
            token.identifier = lexer->identifier.str;
            token.number = lexer->number;
            out.tokens.push_back(std::move(token));
            lexer->Next();
        }
    } catch (const js::LexerPanic&) {
        out.stopped_early = true;
    }
    out.log = std::move(log);
    return out;
}

bool HasMessageWithText(const Log& log, const std::string& text) {
    auto msgs = log.peek();
    for (const auto& m : msgs) {
        if (m.data.text.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// ToString
// ---------------------------------------------------------------------------

TEST(JsLexerToString, AllKindsMapToAString)
{
    for (int i = 0; i <= static_cast<int>(T::kWith); ++i) {
        EXPECT_FALSE(js::ToString(static_cast<T>(i)).empty());
    }
}

TEST(JsLexerToString, KnownValues)
{
    EXPECT_EQ(js::ToString(T::kEndOfFile), "end of file");
    EXPECT_EQ(js::ToString(T::kSyntaxError), "syntax error");
    EXPECT_EQ(js::ToString(T::kHashbang), "hashbang comment");
    EXPECT_EQ(js::ToString(T::kIdentifier), "identifier");
    EXPECT_EQ(js::ToString(T::kEscapedKeyword), "escaped keyword");
    EXPECT_EQ(js::ToString(T::kStringLiteral), "string");
    EXPECT_EQ(js::ToString(T::kPrivateIdentifier), "private identifier");
    EXPECT_EQ(js::ToString(T::kPlus), "\"+\"");
    EXPECT_EQ(js::ToString(T::kEqualsGreaterThan), "\"=>\"");
}

// ---------------------------------------------------------------------------
// Basic token sequences
// ---------------------------------------------------------------------------

TEST(JsLexerTokenize, SimpleStatement)
{
    auto out = LexAll("var foo = 1;");

    ASSERT_EQ(out.tokens.size(), 5u);
    EXPECT_EQ(out.tokens[0].kind, T::kVar);
    EXPECT_EQ(out.tokens[0].raw, "var");
    EXPECT_EQ(out.tokens[1].kind, T::kIdentifier);
    EXPECT_EQ(out.tokens[1].raw, "foo");
    EXPECT_EQ(out.tokens[2].kind, T::kEquals);
    EXPECT_EQ(out.tokens[3].kind, T::kNumericLiteral);
    EXPECT_EQ(out.tokens[3].raw, "1");
    EXPECT_EQ(out.tokens[4].kind, T::kSemicolon);
    EXPECT_TRUE(out.log.peek().empty());
}

TEST(JsLexerTokenize, FunctionDeclaration)
{
    auto out = LexAll("function add(a, b) {}");

    ASSERT_EQ(out.tokens.size(), 9u);
    EXPECT_EQ(out.tokens[0].kind, T::kFunction);
    EXPECT_EQ(out.tokens[1].kind, T::kIdentifier);
    EXPECT_EQ(out.tokens[2].kind, T::kOpenParen);
    EXPECT_EQ(out.tokens[3].kind, T::kIdentifier);
    EXPECT_EQ(out.tokens[4].kind, T::kComma);
    EXPECT_EQ(out.tokens[5].kind, T::kIdentifier);
    EXPECT_EQ(out.tokens[6].kind, T::kCloseParen);
    EXPECT_EQ(out.tokens[7].kind, T::kOpenBrace);
}

TEST(JsLexerTokenize, WhitespaceAndCommentsAreSkipped)
{
    auto out = LexAll("a /* c1 */ b // c2\n c");

    ASSERT_EQ(out.tokens.size(), 3u);
    EXPECT_EQ(out.tokens[0].raw, "a");
    EXPECT_EQ(out.tokens[1].raw, "b");
    EXPECT_EQ(out.tokens[2].raw, "c");
    EXPECT_TRUE(out.log.peek().empty());
}

TEST(JsLexerTokenize, Hashbang)
{
    auto out = LexAll("#!/usr/bin/env node\nvar x = 1;");

    ASSERT_EQ(out.tokens.size(), 6u);
    EXPECT_EQ(out.tokens[0].kind, T::kHashbang);
    EXPECT_EQ(out.tokens[0].raw, "#!/usr/bin/env node");
    EXPECT_EQ(out.tokens[1].kind, T::kVar);
    EXPECT_EQ(out.tokens[2].kind, T::kIdentifier);
    EXPECT_EQ(out.tokens[2].raw, "x");
    EXPECT_EQ(out.tokens[3].kind, T::kEquals);
    EXPECT_EQ(out.tokens[4].kind, T::kNumericLiteral);
}

// ---------------------------------------------------------------------------
// Punctuation / operators
// ---------------------------------------------------------------------------

TEST(JsLexerTokenize, Punctuation)
{
    struct Case {
        const char* input;
        T kind;
    };
    Case cases[] = {
        {"(", T::kOpenParen},
        {")", T::kCloseParen},
        {"[", T::kOpenBracket},
        {"]", T::kCloseBracket},
        {"{", T::kOpenBrace},
        {"}", T::kCloseBrace},
        {";", T::kSemicolon},
        {":", T::kColon},
        {",", T::kComma},
        {"@", T::kAt},
        {"~", T::kTilde},
    };
    for (const auto& c : cases) {
        auto out = LexAll(c.input);
        ASSERT_EQ(out.tokens.size(), 1u) << c.input;
        EXPECT_EQ(out.tokens[0].kind, c.kind) << c.input;
        EXPECT_EQ(out.tokens[0].raw, c.input) << c.input;
    }
}

TEST(JsLexerTokenize, Operators)
{
    struct Case {
        const char* input;
        T kind;
    };
    Case cases[] = {
        {"+", T::kPlus},
        {"-", T::kMinus},
        {"*", T::kAsterisk},
        {"/", T::kSlash},
        {"%", T::kPercent},
        {"&", T::kAmpersand},
        {"|", T::kBar},
        {"^", T::kCaret},
        {"!", T::kExclamation},
        {"=", T::kEquals},
        {"<", T::kLessThan},
        {">", T::kGreaterThan},
        {"?", T::kQuestion},
        {"++", T::kPlusPlus},
        {"--", T::kMinusMinus},
        {"+=", T::kPlusEquals},
        {"-=", T::kMinusEquals},
        {"*=", T::kAsteriskEquals},
        {"**", T::kAsteriskAsterisk},
        {"**=", T::kAsteriskAsteriskEquals},
        {"/=", T::kSlashEquals},
        {"%=", T::kPercentEquals},
        {"&=", T::kAmpersandEquals},
        {"&&", T::kAmpersandAmpersand},
        {"&&=", T::kAmpersandAmpersandEquals},
        {"|=", T::kBarEquals},
        {"||", T::kBarBar},
        {"||=", T::kBarBarEquals},
        {"^=", T::kCaretEquals},
        {"!=", T::kExclamationEquals},
        {"!==", T::kExclamationEqualsEquals},
        {"==", T::kEqualsEquals},
        {"===", T::kEqualsEqualsEquals},
        {"=>", T::kEqualsGreaterThan},
        {"<=", T::kLessThanEquals},
        {"<<", T::kLessThanLessThan},
        {"<<=", T::kLessThanLessThanEquals},
        {">=", T::kGreaterThanEquals},
        {">>", T::kGreaterThanGreaterThan},
        {">>=", T::kGreaterThanGreaterThanEquals},
        {">>>", T::kGreaterThanGreaterThanGreaterThan},
        {">>>=", T::kGreaterThanGreaterThanGreaterThanEquals},
        {"??", T::kQuestionQuestion},
        {"\?\?=", T::kQuestionQuestionEquals},
        {"?.", T::kQuestionDot},
        {"...", T::kDotDotDot},
    };
    for (const auto& c : cases) {
        auto out = LexAll(c.input);
        ASSERT_EQ(out.tokens.size(), 1u) << c.input;
        EXPECT_EQ(out.tokens[0].kind, c.kind) << c.input;
        EXPECT_EQ(out.tokens[0].raw, c.input) << c.input;
    }
}

TEST(JsLexerTokenize, SlashIsDivisionNotRegExpByDefault)
{
    auto out = LexAll("/x/");

    ASSERT_EQ(out.tokens.size(), 3u);
    EXPECT_EQ(out.tokens[0].kind, T::kSlash);
    EXPECT_EQ(out.tokens[0].raw, "/");
    EXPECT_EQ(out.tokens[1].kind, T::kIdentifier);
    EXPECT_EQ(out.tokens[1].raw, "x");
    EXPECT_EQ(out.tokens[2].kind, T::kSlash);
    EXPECT_EQ(out.tokens[2].raw, "/");
}

// ---------------------------------------------------------------------------
// Identifiers / keywords
// ---------------------------------------------------------------------------

TEST(JsLexerTokenize, Keywords)
{
    struct Case {
        const char* input;
        T kind;
    };
    Case cases[] = {
        {"break", T::kBreak},
        {"case", T::kCase},
        {"catch", T::kCatch},
        {"class", T::kClass},
        {"const", T::kConst},
        {"continue", T::kContinue},
        {"debugger", T::kDebugger},
        {"default", T::kDefault},
        {"delete", T::kDelete},
        {"do", T::kDo},
        {"else", T::kElse},
        {"enum", T::kEnum},
        {"export", T::kExport},
        {"extends", T::kExtends},
        {"false", T::kFalse},
        {"finally", T::kFinally},
        {"for", T::kFor},
        {"function", T::kFunction},
        {"if", T::kIf},
        {"import", T::kImport},
        {"in", T::kIn},
        {"instanceof", T::kInstanceof},
        {"new", T::kNew},
        {"null", T::kNull},
        {"return", T::kReturn},
        {"super", T::kSuper},
        {"switch", T::kSwitch},
        {"this", T::kThis},
        {"throw", T::kThrow},
        {"true", T::kTrue},
        {"try", T::kTry},
        {"typeof", T::kTypeof},
        {"var", T::kVar},
        {"void", T::kVoid},
        {"while", T::kWhile},
        {"with", T::kWith},
    };
    for (const auto& c : cases) {
        auto out = LexAll(c.input);
        ASSERT_EQ(out.tokens.size(), 1u) << c.input;
        EXPECT_EQ(out.tokens[0].kind, c.kind) << c.input;
    }
}

TEST(JsLexerTokenize, ContextualIdentifiers)
{
    const char* inputs[] = {"let", "async", "await", "yield", "as", "from", "of"};
    for (const char* input : inputs) {
        auto out = LexAll(input);
        ASSERT_EQ(out.tokens.size(), 1u) << input;
        EXPECT_EQ(out.tokens[0].kind, T::kIdentifier) << input;
    }
}

TEST(JsLexerTokenize, Identifiers)
{
    const char* inputs[] = {"foo", "Bar", "_baz", "$qux", "caf\xC3\xA9"};
    for (const char* input : inputs) {
        auto out = LexAll(input);
        ASSERT_EQ(out.tokens.size(), 1u) << input;
        EXPECT_EQ(out.tokens[0].kind, T::kIdentifier) << input;
        EXPECT_EQ(out.tokens[0].raw, input) << input;
    }
}

TEST(JsLexerTokenize, PrivateIdentifier)
{
    auto out = LexAll("#foo");

    ASSERT_EQ(out.tokens.size(), 1u);
    EXPECT_EQ(out.tokens[0].kind, T::kPrivateIdentifier);
    EXPECT_EQ(out.tokens[0].raw, "#foo");
}

TEST(JsLexerTokenize, EscapedKeyword)
{
    auto out = LexAll("\\u0069f");

    ASSERT_EQ(out.tokens.size(), 1u);
    EXPECT_EQ(out.tokens[0].kind, T::kEscapedKeyword);
    EXPECT_EQ(out.tokens[0].identifier, "if");
}

TEST(JsLexerTokenize, EscapedIdentifier)
{
    auto out = LexAll("f\\u006Fo");

    ASSERT_EQ(out.tokens.size(), 1u);
    EXPECT_EQ(out.tokens[0].kind, T::kIdentifier);
    EXPECT_EQ(out.tokens[0].identifier, "foo");
}

TEST(JsLexerTokenize, IsContextualKeyword)
{
    auto source = MakeSource("as");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, std::move(source), config::TSOptions{});

    ASSERT_EQ(lexer.token, T::kIdentifier);
    EXPECT_TRUE(lexer.IsContextualKeyword("as"));
    EXPECT_FALSE(lexer.IsContextualKeyword("in"));
    EXPECT_FALSE(lexer.IsIdentifierOrKeyword() == false);
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

TEST(JsLexerNumeric, Literals)
{
    struct Case {
        const char* input;
        double value;
    };
    Case cases[] = {
        {"0", 0},
        {"1", 1},
        {"42", 42},
        {"1.5", 1.5},
        {".5", 0.5},
        {"1.", 1.0},
        {"1.25", 1.25},
        {"1e3", 1000},
        {"1e+2", 100},
        {"1E-4", 0.0001},
        {"0.5e2", 50},
        {"0x1A", 26},
        {"0XFF", 255},
        {"0b101", 5},
        {"0B10", 2},
        {"0o17", 15},
        {"0O77", 63},
    };
    for (const auto& c : cases) {
        auto out = LexAll(c.input);
        ASSERT_EQ(out.tokens.size(), 1u) << c.input;
        EXPECT_EQ(out.tokens[0].kind, T::kNumericLiteral) << c.input;
        EXPECT_EQ(out.tokens[0].raw, c.input) << c.input;
        EXPECT_DOUBLE_EQ(out.tokens[0].number, c.value) << c.input;
    }
}

TEST(JsLexerNumeric, NumericSeparators)
{
    auto out = LexAll("1_000_000");
    ASSERT_EQ(out.tokens.size(), 1u);
    EXPECT_EQ(out.tokens[0].kind, T::kNumericLiteral);
    EXPECT_DOUBLE_EQ(out.tokens[0].number, 1000000);

    auto hex = LexAll("0xFF_FF");
    ASSERT_EQ(hex.tokens.size(), 1u);
    EXPECT_DOUBLE_EQ(hex.tokens[0].number, 65535);
}

TEST(JsLexerNumeric, BigInt)
{
    auto out = LexAll("123n");
    ASSERT_EQ(out.tokens.size(), 1u);
    EXPECT_EQ(out.tokens[0].kind, T::kBigIntegerLiteral);
    EXPECT_EQ(out.tokens[0].raw, "123n");
    EXPECT_EQ(out.tokens[0].identifier, "123");

    auto hex = LexAll("0x10n");
    ASSERT_EQ(hex.tokens.size(), 1u);
    EXPECT_EQ(hex.tokens[0].kind, T::kBigIntegerLiteral);
    EXPECT_EQ(hex.tokens[0].raw, "0x10n");
    EXPECT_EQ(hex.tokens[0].identifier, "0x10");

    auto huge = LexAll("9007199254740993n");
    ASSERT_EQ(huge.tokens.size(), 1u);
    EXPECT_EQ(huge.tokens[0].kind, T::kBigIntegerLiteral);
    EXPECT_EQ(huge.tokens[0].identifier, "9007199254740993");
}

TEST(JsLexerNumeric, InvalidLiterals)
{
    const char* inputs[] = {"0b2", "0x", "1_", "1a", "1.5n", "0x_1"};
    for (const char* input : inputs) {
        auto out = LexAll(input);
        EXPECT_TRUE(out.stopped_early) << input;
        EXPECT_FALSE(out.log.peek().empty()) << input;
    }
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

TEST(JsLexerString, DoubleAndSingle)
{
    auto out = LexAll("\"foo\" 'bar'");

    ASSERT_EQ(out.tokens.size(), 2u);
    EXPECT_EQ(out.tokens[0].kind, T::kStringLiteral);
    EXPECT_EQ(out.tokens[0].raw, "\"foo\"");
    EXPECT_EQ(out.tokens[1].kind, T::kStringLiteral);
    EXPECT_EQ(out.tokens[1].raw, "'bar'");
    EXPECT_TRUE(out.log.peek().empty());
}

TEST(JsLexerString, EscapeSequences)
{
    auto source = MakeSource("\"\\u0041\\x42\\n'\"");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, std::move(source), config::TSOptions{});

    ASSERT_EQ(lexer.token, T::kStringLiteral);
    EXPECT_EQ(lexer.StringLiteral(), u"AB\n'");
}

TEST(JsLexerString, UnicodeEscape)
{
    auto source = MakeSource("\"\\u{1F600}\"");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, std::move(source), config::TSOptions{});

    ASSERT_EQ(lexer.token, T::kStringLiteral);
    EXPECT_EQ(lexer.StringLiteral(), u"\U0001F600");
}

TEST(JsLexerString, UnterminatedStringErrors)
{
    auto out = LexAll("\"foo");

    EXPECT_TRUE(out.stopped_early);
    EXPECT_TRUE(HasMessageWithText(out.log, "Unterminated string literal"));
}

// ---------------------------------------------------------------------------
// Template literals
// ---------------------------------------------------------------------------

TEST(JsLexerTemplate, NoSubstitution)
{
    auto out = LexAll("`text`");

    ASSERT_EQ(out.tokens.size(), 1u);
    EXPECT_EQ(out.tokens[0].kind, T::kNoSubstitutionTemplateLiteral);
    EXPECT_EQ(out.tokens[0].raw, "`text`");
}

TEST(JsLexerTemplate, HeadMiddleTail)
{
    auto source = MakeSource("`a${1}b${2}c`");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, std::move(source), config::TSOptions{});

    EXPECT_EQ(lexer.token, T::kTemplateHead);
    EXPECT_EQ(lexer.Raw(), "`a${");

    lexer.Next();
    ASSERT_EQ(lexer.token, T::kNumericLiteral);
    EXPECT_EQ(lexer.Raw(), "1");

    lexer.Next();
    ASSERT_EQ(lexer.token, T::kCloseBrace);
    lexer.RescanCloseBraceAsTemplateToken();
    EXPECT_EQ(lexer.token, T::kTemplateMiddle);
    EXPECT_EQ(lexer.Raw(), "}b${");

    lexer.Next();
    ASSERT_EQ(lexer.token, T::kNumericLiteral);
    EXPECT_EQ(lexer.Raw(), "2");

    lexer.Next();
    ASSERT_EQ(lexer.token, T::kCloseBrace);
    lexer.RescanCloseBraceAsTemplateToken();
    EXPECT_EQ(lexer.token, T::kTemplateTail);
    EXPECT_EQ(lexer.Raw(), "}c`");

    lexer.Next();
    EXPECT_EQ(lexer.token, T::kEndOfFile);
    EXPECT_TRUE(log.peek().empty());
}

// ---------------------------------------------------------------------------
// Regular expressions
// ---------------------------------------------------------------------------

TEST(JsLexerRegExp, Scan)
{
    auto out = LexAll("/a+/gi", {}, true);

    ASSERT_EQ(out.tokens.size(), 1u);
    EXPECT_EQ(out.tokens[0].kind, T::kSlash);
    EXPECT_EQ(out.tokens[0].raw, "/a+/gi");
    EXPECT_TRUE(out.log.peek().empty());
}

TEST(JsLexerRegExp, CharacterClass)
{
    auto out = LexAll("/[a-z]+[0-9]/imgy", {}, true);

    ASSERT_EQ(out.tokens.size(), 1u);
    EXPECT_EQ(out.tokens[0].raw, "/[a-z]+[0-9]/imgy");
}

TEST(JsLexerRegExp, DuplicateFlagErrors)
{
    auto out = LexAll("/x/gg", {}, true);

    EXPECT_FALSE(out.log.peek().empty());
    EXPECT_TRUE(HasMessageWithText(out.log, "Duplicate flag"));
}

TEST(JsLexerRegExp, UnterminatedErrors)
{
    auto out = LexAll("/x", {}, true);

    EXPECT_TRUE(out.stopped_early);
    EXPECT_TRUE(HasMessageWithText(out.log, "Unterminated regular expression"));
}

// ---------------------------------------------------------------------------
// Whitespace / newline semantics
// ---------------------------------------------------------------------------

TEST(JsLexerNewline, HasNewlineBefore)
{
    auto source = MakeSource("a\nb");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, std::move(source), config::TSOptions{});

    // The first token in the file reports a newline before it, matching the
    // esbuild reference implementation (HasNewlineBefore = end == 0 in Next()).
    ASSERT_EQ(lexer.token, T::kIdentifier);
    EXPECT_TRUE(lexer.has_newline_before);

    lexer.Next();
    ASSERT_EQ(lexer.token, T::kIdentifier);
    EXPECT_TRUE(lexer.has_newline_before);
}

TEST(JsLexerNewline, NoNewlineBetweenTokens)
{
    auto source = MakeSource("a b");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, std::move(source), config::TSOptions{});

    ASSERT_EQ(lexer.token, T::kIdentifier);
    lexer.Next();
    ASSERT_EQ(lexer.token, T::kIdentifier);
    EXPECT_FALSE(lexer.has_newline_before);
}

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------

TEST(JsLexerComments, CommentsBeforeToken)
{
    auto source = MakeSource("a /* one */ b");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, std::move(source), config::TSOptions{});

    ASSERT_EQ(lexer.token, T::kIdentifier);
    EXPECT_TRUE(lexer.comments_before_token.empty());
    EXPECT_TRUE(lexer.all_comments.empty());

    lexer.Next();
    ASSERT_EQ(lexer.token, T::kIdentifier);
    EXPECT_TRUE(lexer.comments_before_token.empty() == false);
    EXPECT_TRUE(lexer.all_comments.empty() == false);
    EXPECT_TRUE(log.peek().empty());
}

TEST(JsLexerComments, LegalComment)
{
    auto source = MakeSource("/*! keep */ foo");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, source, config::TSOptions{});

    ASSERT_EQ(lexer.token, T::kIdentifier);
    ASSERT_EQ(lexer.legal_comments_before_token.size(), 1u);

    auto text = source.TextForRange(lexer.legal_comments_before_token[0]);
    EXPECT_TRUE(text.starts_with("/*!"));
}

TEST(JsLexerComments, PureAnnotation)
{
    auto source = MakeSource("/* @__PURE__ */ foo");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, std::move(source), config::TSOptions{});

    ASSERT_EQ(lexer.token, T::kIdentifier);
    EXPECT_TRUE(js::Has(lexer.has_comment_before, js::CommentBefore::kPure));
}

TEST(JsLexerComments, SourceMappingURL)
{
    auto source = MakeSource("//# sourceMappingURL=x.map\nfoo");
    auto log = NewDeferLog(DeferLogKind::kDeferLogAll, {});
    js::Lexer lexer(log, std::move(source), config::TSOptions{});

    ASSERT_EQ(lexer.token, T::kIdentifier);
    EXPECT_EQ(lexer.source_mapping_url.text, "x.map");
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

TEST(JsLexerErrors, InvalidCharacter)
{
    auto out = LexAll("\x01");

    ASSERT_EQ(out.tokens.size(), 1u);
    EXPECT_EQ(out.tokens[0].kind, T::kSyntaxError);
}

TEST(JsLexerErrors, InvalidPrivateIdentifier)
{
    auto out = LexAll("#1");

    EXPECT_TRUE(out.stopped_early);
    EXPECT_FALSE(out.log.peek().empty());
}

TEST(JsLexerErrors, InvalidIdentifierEscape)
{
    auto out = LexAll("\\u006");

    EXPECT_TRUE(out.stopped_early);
    EXPECT_FALSE(out.log.peek().empty());
}

} // namespace