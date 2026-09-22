#include "test/guchho_test.hpp"

#include "guchho/html/html_bridge.hpp"
#include "guchho/html/html_lexer.hpp"
#include "guchho/logger.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace html = guchho::html;
namespace logger = guchho::logger;

namespace {

struct Record {
    html::TokenType type = html::TokenType::kEof;
    std::string text;
    html::TagToken tag;
    html::DoctypeToken doctype;
    bool has_location = false;
    html::Location location;
};

class Collector : public html::TokenHandler {
public:
    std::vector<Record> records;
    std::vector<html::ParserError> errors;

    Collector() {
        on_parse_error = [this](const html::ParserError& error) { errors.push_back(error); };
    }

    void OnComment(html::CommentToken& token) override {
        Record r;
        r.type = token.type;
        r.text = token.data;
        TakeLocation(r, token.location);
        records.push_back(std::move(r));
    }

    void OnDoctype(html::DoctypeToken& token) override {
        Record r;
        r.type = token.type;
        r.doctype = token;
        r.text = token.name;
        TakeLocation(r, token.location);
        records.push_back(std::move(r));
    }

    void OnStartTag(html::TagToken& token) override {
        Record r;
        r.type = token.type;
        r.tag = token;
        r.text = token.tag_name;
        TakeLocation(r, token.location);
        records.push_back(std::move(r));
    }

    void OnEndTag(html::TagToken& token) override {
        Record r;
        r.type = token.type;
        r.tag = token;
        r.text = token.tag_name;
        TakeLocation(r, token.location);
        records.push_back(std::move(r));
    }

    void OnEof(html::EofToken& token) override {
        Record r;
        r.type = token.type;
        TakeLocation(r, token.location);
        records.push_back(std::move(r));
    }

    void OnCharacter(html::CharacterToken& token) override { AddCharacter(token); }
    void OnNullCharacter(html::CharacterToken& token) override { AddCharacter(token); }
    void OnWhitespaceCharacter(html::CharacterToken& token) override { AddCharacter(token); }

private:
    void AddCharacter(const html::CharacterToken& token) {
        Record r;
        r.type = token.type;
        r.text = token.chars;
        TakeLocation(r, token.location);
        records.push_back(std::move(r));
    }

    void TakeLocation(Record& r, html::Location* location) {
        if (location) {
            r.has_location = true;
            r.location = *location;
        }
    }
};

Collector LexAll(std::u16string input, bool with_locations = false) {
    Collector collector;
    html::Tokenizer tokenizer(collector, with_locations);
    tokenizer.Write(std::move(input), true);
    return collector;
}

Collector LexContent(html::State initial_state, const std::u16string& last_start_tag,
                     std::u16string input, bool with_locations = false) {
    Collector collector;
    html::Tokenizer tokenizer(collector, with_locations);
    tokenizer.state = initial_state;
    tokenizer.last_start_tag_name = last_start_tag;
    tokenizer.Write(std::move(input), true);
    return collector;
}

size_t CountError(const Collector& collector, html::Err code) {
    size_t count = 0;
    for (const html::ParserError& error : collector.errors) {
        if (error.code == code) {
            ++count;
        }
    }
    return count;
}

std::string AllCharText(const Collector& collector) {
    std::string out;
    for (const Record& r : collector.records) {
        if (r.type == html::TokenType::kCharacter || r.type == html::TokenType::kNullCharacter ||
            r.type == html::TokenType::kWhitespaceCharacter) {
            out += r.text;
        }
    }
    return out;
}

const Record* FindStartTag(const Collector& collector, std::string_view name) {
    for (const Record& r : collector.records) {
        if (r.type == html::TokenType::kStartTag && r.tag.tag_name == name) {
            return &r;
        }
    }
    return nullptr;
}

std::string U8(char32_t cp) {
    std::string out;
    html::AppendCodePoint(out, cp);
    return out;
}

std::u16string U16(char32_t cp) {
    return html::ToUtf16(U8(cp));
}

// ---------------------------------------------------------------------------
// Data state: ordinary text
// ---------------------------------------------------------------------------

TEST(HtmlLexerText, HelloWorld)
{
    auto c = LexAll(u"Hello world");
    ASSERT_EQ(c.records.size(), size_t{4});
    EXPECT_EQ(c.records[0].type, html::TokenType::kCharacter);
    EXPECT_EQ(c.records[0].text, "Hello");
    EXPECT_EQ(c.records[1].type, html::TokenType::kWhitespaceCharacter);
    EXPECT_EQ(c.records[1].text, " ");
    EXPECT_EQ(c.records[2].type, html::TokenType::kCharacter);
    EXPECT_EQ(c.records[2].text, "world");
    EXPECT_EQ(c.records[3].type, html::TokenType::kEof);
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerText, WhitespaceRunCoalesces)
{
    auto c = LexAll(u"a \t\n b");
    ASSERT_EQ(c.records.size(), size_t{4});
    EXPECT_EQ(c.records[0].text, "a");
    EXPECT_EQ(c.records[1].type, html::TokenType::kWhitespaceCharacter);
    EXPECT_EQ(c.records[1].text, " \t\n ");
    EXPECT_EQ(c.records[2].text, "b");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerText, NullCharacterRun)
{
    auto c = LexAll(std::u16string{u'a', 0, u'b'});
    EXPECT_EQ(CountError(c, html::Err::kUnexpectedNullCharacter), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{4});
    EXPECT_EQ(c.records[0].text, "a");
    EXPECT_EQ(c.records[1].type, html::TokenType::kNullCharacter);
    EXPECT_EQ(c.records[1].text, std::string("\0", 1));
    EXPECT_EQ(c.records[2].text, "b");
}

TEST(HtmlLexerText, CarriageReturnFoldedToLineFeed)
{
    auto c = LexAll(u"a\r\nb");
    ASSERT_EQ(c.records.size(), size_t{4});
    EXPECT_EQ(c.records[0].text, "a");
    EXPECT_EQ(c.records[1].type, html::TokenType::kWhitespaceCharacter);
    EXPECT_EQ(c.records[1].text, "\n");
    EXPECT_EQ(c.records[2].text, "b");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerText, AstralCodePointRoundTrips)
{
    auto c = LexAll(U16(0x1F600));
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kCharacter);
    EXPECT_EQ(c.records[0].text, U8(0x1F600));
    EXPECT_EQ(c.records[1].type, html::TokenType::kEof);
}

TEST(HtmlLexerText, IsolatedSurrogateReported)
{
    auto c = LexAll(std::u16string(1, 0xD800));
    EXPECT_EQ(CountError(c, html::Err::kSurrogateInInputStream), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kCharacter);
    EXPECT_EQ(c.records[0].text, U8(0xD800));
}

TEST(HtmlLexerText, EofAfterLessThan)
{
    auto c = LexAll(u"<");
    EXPECT_EQ(CountError(c, html::Err::kEofBeforeTagName), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kCharacter);
    EXPECT_EQ(c.records[0].text, "<");
    EXPECT_EQ(c.records[1].type, html::TokenType::kEof);
}

TEST(HtmlLexerText, DigitAfterLessThanIsInvalidTagName)
{
    auto c = LexAll(u"<4");
    EXPECT_EQ(CountError(c, html::Err::kInvalidFirstCharacterOfTagName), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kCharacter);
    EXPECT_EQ(c.records[0].text, "<4");
}

// ---------------------------------------------------------------------------
// Tags
// ---------------------------------------------------------------------------

TEST(HtmlLexerTag, StartTagNameLowercased)
{
    auto c = LexAll(u"<DIV>");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kStartTag);
    EXPECT_EQ(c.records[0].tag.tag_name, "div");
    EXPECT_EQ(c.records[0].tag.tag_id, html::TagId::kDiv);
    EXPECT_FALSE(c.records[0].tag.self_closing);
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerTag, SelfClosingStartTag)
{
    auto c = LexAll(u"<br/>");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kStartTag);
    EXPECT_EQ(c.records[0].tag.tag_name, "br");
    EXPECT_TRUE(c.records[0].tag.self_closing);
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerTag, EofInTag)
{
    auto c = LexAll(u"<div");
    EXPECT_EQ(CountError(c, html::Err::kEofInTag), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{1});
    EXPECT_EQ(c.records[0].type, html::TokenType::kEof);
}

TEST(HtmlLexerTag, EndTagWithTrailingSolidus)
{
    auto c = LexAll(u"</div/>");
    EXPECT_EQ(CountError(c, html::Err::kEndTagWithTrailingSolidus), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kEndTag);
    EXPECT_EQ(c.records[0].tag.tag_name, "div");
    EXPECT_TRUE(c.records[0].tag.self_closing);
}

TEST(HtmlLexerTag, EndTagWithAttributes)
{
    auto c = LexAll(u"</div x=1>");
    EXPECT_EQ(CountError(c, html::Err::kEndTagWithAttributes), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kEndTag);
    EXPECT_EQ(c.records[0].tag.tag_name, "div");
    EXPECT_EQ(c.records[0].tag.attrs.size(), size_t{1});
}

TEST(HtmlLexerTag, MissingEndTagName)
{
    auto c = LexAll(u"</>");
    EXPECT_EQ(CountError(c, html::Err::kMissingEndTagName), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{1});
    EXPECT_EQ(c.records[0].type, html::TokenType::kEof);
}

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------

TEST(HtmlLexerAttribute, QuotedValue)
{
    auto c = LexAll(u"<a href=\"x\">");
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    const std::string* value = html::GetTokenAttr(tag->tag, "href");
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "x");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerAttribute, BareAttributeHasEmptyValue)
{
    auto c = LexAll(u"<input disabled>");
    const Record* tag = FindStartTag(c, "input");
    ASSERT_TRUE(tag != nullptr);
    const std::string* value = html::GetTokenAttr(tag->tag, "disabled");
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "");
}

TEST(HtmlLexerAttribute, AttributeNameLowercasedValuePreserved)
{
    auto c = LexAll(u"<a Foo=\"Bar\">");
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    const std::string* value = html::GetTokenAttr(tag->tag, "foo");
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "Bar");
    EXPECT_TRUE(html::GetTokenAttr(tag->tag, "Foo") == nullptr);
}

TEST(HtmlLexerAttribute, DuplicateAttributeKeepsFirstValue)
{
    auto c = LexAll(u"<a id=1 id=2>");
    EXPECT_EQ(CountError(c, html::Err::kDuplicateAttribute), size_t{1});
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    EXPECT_EQ(tag->tag.attrs.size(), size_t{1});
    const std::string* value = html::GetTokenAttr(tag->tag, "id");
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "1");
}

TEST(HtmlLexerAttribute, GetTokenAttrMissingAttribute)
{
    auto c = LexAll(u"<a href=\"a\">");
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    EXPECT_TRUE(html::GetTokenAttr(tag->tag, "missing") == nullptr);
}

TEST(HtmlLexerAttribute, EqualsSignImmediatelyAfterTagName)
{
    auto c = LexAll(u"<a =x>");
    EXPECT_EQ(CountError(c, html::Err::kUnexpectedEqualsSignBeforeAttributeName), size_t{1});
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    ASSERT_EQ(tag->tag.attrs.size(), size_t{1});
    EXPECT_EQ(tag->tag.attrs[0].name, "=x");
    EXPECT_EQ(tag->tag.attrs[0].value, "");
}

TEST(HtmlLexerAttribute, SolidusCreatesAttributeInsteadOfSelfClosing)
{
    auto c = LexAll(u"<a /x>");
    EXPECT_EQ(CountError(c, html::Err::kUnexpectedSolidusInTag), size_t{1});
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    EXPECT_FALSE(tag->tag.self_closing);
    ASSERT_EQ(tag->tag.attrs.size(), size_t{1});
    EXPECT_EQ(tag->tag.attrs[0].name, "x");
    EXPECT_EQ(tag->tag.attrs[0].value, "");
}

TEST(HtmlLexerAttribute, MissingWhitespaceBetweenAttributes)
{
    auto c = LexAll(u"<a x=\"1\"y=\"2\">");
    EXPECT_EQ(CountError(c, html::Err::kMissingWhitespaceBetweenAttributes), size_t{1});
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    ASSERT_EQ(tag->tag.attrs.size(), size_t{2});
    EXPECT_EQ(*html::GetTokenAttr(tag->tag, "x"), "1");
    EXPECT_EQ(*html::GetTokenAttr(tag->tag, "y"), "2");
}

TEST(HtmlLexerAttribute, MissingAttributeValue)
{
    auto c = LexAll(u"<a foo=>");
    EXPECT_EQ(CountError(c, html::Err::kMissingAttributeValue), size_t{1});
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    const std::string* value = html::GetTokenAttr(tag->tag, "foo");
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "");
}

TEST(HtmlLexerAttribute, CharacterReferenceInQuotedValue)
{
    auto c = LexAll(u"<a x=\"a&amp;b\">");
    EXPECT_TRUE(c.errors.empty());
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    const std::string* value = html::GetTokenAttr(tag->tag, "x");
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "a&b");
}

TEST(HtmlLexerAttribute, CharacterReferenceInUnquotedValue)
{
    auto c = LexAll(u"<a x=a&amp;b>");
    EXPECT_TRUE(c.errors.empty());
    const Record* tag = FindStartTag(c, "a");
    ASSERT_TRUE(tag != nullptr);
    const std::string* value = html::GetTokenAttr(tag->tag, "x");
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "a&b");
}

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------

TEST(HtmlLexerComment, BasicComment)
{
    auto c = LexAll(u"<!-- hi -->");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, " hi ");
    EXPECT_EQ(c.records[1].type, html::TokenType::kEof);
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerComment, AbruptClosingOfEmptyComment)
{
    auto c = LexAll(u"<!-->");
    EXPECT_EQ(CountError(c, html::Err::kAbruptClosingOfEmptyComment), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, "");
}

TEST(HtmlLexerComment, EmptyComment)
{
    auto c = LexAll(u"<!---->");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, "");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerComment, NestedComment)
{
    auto c = LexAll(u"<!--a<!--b-->");
    EXPECT_EQ(CountError(c, html::Err::kNestedComment), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, "a<!--b");
}

TEST(HtmlLexerComment, IncorrectlyClosedComment)
{
    auto c = LexAll(u"<!--a--!>");
    EXPECT_EQ(CountError(c, html::Err::kIncorrectlyClosedComment), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, "a");
}

TEST(HtmlLexerComment, EofInComment)
{
    auto c = LexAll(u"<!--x");
    EXPECT_EQ(CountError(c, html::Err::kEofInComment), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, "x");
    EXPECT_EQ(c.records[1].type, html::TokenType::kEof);
}

TEST(HtmlLexerComment, DashesInsideComment)
{
    auto c = LexAll(u"<!--a---b-->");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, "a---b");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerComment, BogusQuestionMarkBecomesComment)
{
    auto c = LexAll(u"<?xml?>");
    EXPECT_EQ(CountError(c, html::Err::kUnexpectedQuestionMarkInsteadOfTagName), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, "?xml?");
}

TEST(HtmlLexerComment, CdataInHtmlContent)
{
    auto c = LexAll(u"<![CDATA[x]]>");
    EXPECT_EQ(CountError(c, html::Err::kCdataInHtmlContent), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, "[CDATA[x]]");
}

TEST(HtmlLexerComment, IncorrectlyOpenedComment)
{
    auto c = LexAll(u"<!weird>");
    EXPECT_EQ(CountError(c, html::Err::kIncorrectlyOpenedComment), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kComment);
    EXPECT_EQ(c.records[0].text, "weird");
}

// ---------------------------------------------------------------------------
// Character references
// ---------------------------------------------------------------------------

TEST(HtmlLexerCharRef, NamedReference)
{
    auto c = LexAll(u"&amp;");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kCharacter);
    EXPECT_EQ(c.records[0].text, "&");
    EXPECT_EQ(c.records[1].type, html::TokenType::kEof);
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerCharRef, NamedReferenceInTextRun)
{
    auto c = LexAll(u"caf&eacute;");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].text, "caf" + U8(0xE9));
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerCharRef, NumericReferenceForms)
{
    {
        auto c = LexAll(u"&#65;");
        ASSERT_EQ(c.records.size(), size_t{2});
        EXPECT_EQ(c.records[0].text, "A");
        EXPECT_TRUE(c.errors.empty());
    }
    {
        auto c = LexAll(u"&#x41;");
        EXPECT_EQ(c.records[0].text, "A");
        EXPECT_TRUE(c.errors.empty());
    }
    {
        auto c = LexAll(u"&#X41;");
        EXPECT_EQ(c.records[0].text, "A");
        EXPECT_TRUE(c.errors.empty());
    }
    {
        auto c = LexAll(u"&#x1F600;");
        ASSERT_EQ(c.records.size(), size_t{2});
        EXPECT_EQ(c.records[0].text, U8(0x1F600));
        EXPECT_TRUE(c.errors.empty());
    }
}

TEST(HtmlLexerCharRef, NumericReferenceWithoutSemicolon)
{
    auto c = LexAll(u"&#65");
    EXPECT_EQ(CountError(c, html::Err::kMissingSemicolonAfterCharacterReference), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].text, "A");
}

TEST(HtmlLexerCharRef, NamedReferenceWithoutSemicolonSkipsRemainder)
{
    auto c = LexAll(u"&amp x");
    EXPECT_EQ(CountError(c, html::Err::kMissingSemicolonAfterCharacterReference), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{4});
    EXPECT_EQ(c.records[0].text, "&");
    EXPECT_EQ(c.records[1].type, html::TokenType::kWhitespaceCharacter);
    EXPECT_EQ(c.records[1].text, " ");
    EXPECT_EQ(c.records[2].text, "x");
}

TEST(HtmlLexerCharRef, UnknownNamedReferenceIsLiteral)
{
    auto c = LexAll(u"&foo;");
    EXPECT_EQ(CountError(c, html::Err::kUnknownNamedCharacterReference), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kCharacter);
    EXPECT_EQ(c.records[0].text, "&foo;");
}

TEST(HtmlLexerCharRef, PartialNamedReferenceIsLiteral)
{
    auto c = LexAll(u"&notit;");
    EXPECT_EQ(CountError(c, html::Err::kMissingSemicolonAfterCharacterReference), size_t{1});
    EXPECT_EQ(CountError(c, html::Err::kUnknownNamedCharacterReference), size_t{0});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kCharacter);
    EXPECT_EQ(c.records[0].text, "\u00ACit;");
}

TEST(HtmlLexerCharRef, NumericReferenceWithoutDigitsIsLiteral)
{
    auto c = LexAll(u"&#;");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].text, "&#;");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerCharRef, NullReferenceReplaced)
{
    auto c = LexAll(u"&#0;");
    EXPECT_EQ(CountError(c, html::Err::kNullCharacterReference), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].text, U8(0xFFFD));
}

TEST(HtmlLexerCharRef, SurrogateReferenceReplaced)
{
    auto c = LexAll(u"&#xD800;");
    EXPECT_EQ(CountError(c, html::Err::kSurrogateCharacterReference), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].text, U8(0xFFFD));
}

TEST(HtmlLexerCharRef, OutOfRangeReferenceReplaced)
{
    auto c = LexAll(u"&#x110000;");
    EXPECT_EQ(CountError(c, html::Err::kCharacterReferenceOutsideUnicodeRange), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].text, U8(0xFFFD));
}

TEST(HtmlLexerCharRef, ControlReferenceKept)
{
    auto c = LexAll(u"&#13;");
    EXPECT_EQ(CountError(c, html::Err::kControlCharacterReference), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].text, "\r");
}

// ---------------------------------------------------------------------------
// Locations
// ---------------------------------------------------------------------------

TEST(HtmlLexerLocation, LineAndColumnTracking)
{
    auto c = LexAll(u"ab\n<p>", true);
    ASSERT_EQ(c.records.size(), size_t{4});

    EXPECT_TRUE(c.records[0].has_location);
    EXPECT_EQ(c.records[0].text, "ab");
    EXPECT_EQ(c.records[0].location.start_line, 1);
    EXPECT_EQ(c.records[0].location.start_col, 1);
    EXPECT_EQ(c.records[0].location.start_offset, 0);
    EXPECT_EQ(c.records[0].location.end_offset, 2);

    EXPECT_TRUE(c.records[1].has_location);
    EXPECT_EQ(c.records[1].location.start_offset, 2);

    EXPECT_TRUE(c.records[2].has_location);
    EXPECT_EQ(c.records[2].location.start_line, 2);
    EXPECT_EQ(c.records[2].location.start_col, 1);
    EXPECT_EQ(c.records[2].location.start_offset, 3);
    EXPECT_EQ(c.records[2].location.end_offset, 6);
    EXPECT_EQ(c.records[2].location.end_line, 2);
    EXPECT_EQ(c.records[2].location.end_col, 4);
}

TEST(HtmlLexerLocation, DisabledWhenLocationsOff)
{
    auto c = LexAll(u"<p>x", false);
    ASSERT_FALSE(c.records.empty());
    for (const Record& r : c.records) {
        EXPECT_FALSE(r.has_location);
    }
}

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

TEST(HtmlLexerStreaming, SplitAcrossChunks)
{
    Collector collector;
    html::Tokenizer tokenizer(collector, false);
    tokenizer.Write(u"<div class=\"", false);
    tokenizer.Write(u"x\">hi", true);
    ASSERT_EQ(collector.records.size(), size_t{3});
    EXPECT_EQ(collector.records[0].type, html::TokenType::kStartTag);
    EXPECT_EQ(collector.records[0].tag.tag_name, "div");
    const std::string* class_value = html::GetTokenAttr(collector.records[0].tag, "class");
    ASSERT_TRUE(class_value != nullptr);
    EXPECT_EQ(*class_value, "x");
    EXPECT_EQ(collector.records[1].text, "hi");
    EXPECT_EQ(collector.records[2].type, html::TokenType::kEof);
    EXPECT_TRUE(collector.errors.empty());
}

TEST(HtmlLexerStreaming, PauseDefersProcessing)
{
    Collector collector;
    html::Tokenizer tokenizer(collector, false);
    tokenizer.Pause();
    tokenizer.Write(u"<p>hi", true);
    EXPECT_EQ(collector.records.size(), size_t{0});
    tokenizer.Resume();
    ASSERT_EQ(collector.records.size(), size_t{3});
    EXPECT_EQ(collector.records[0].type, html::TokenType::kStartTag);
    EXPECT_EQ(collector.records[1].text, "hi");
    EXPECT_EQ(collector.records[2].type, html::TokenType::kEof);
}

TEST(HtmlLexerStreaming, InactiveAfterEof)
{
    Collector collector;
    html::Tokenizer tokenizer(collector, false);
    tokenizer.Write(u"x", true);
    EXPECT_FALSE(tokenizer.active);
    EXPECT_EQ(tokenizer.state, html::State::kData);
}

// ---------------------------------------------------------------------------
// RAWTEXT
// ---------------------------------------------------------------------------

TEST(HtmlLexerRawtext, ReferencesNotDecoded)
{
    auto c = LexContent(html::State::kRawtext, u"style", u"a & b</style>c");
    ASSERT_EQ(c.records.size(), size_t{8});
    EXPECT_EQ(c.records[0].text, "a");
    EXPECT_EQ(c.records[1].type, html::TokenType::kWhitespaceCharacter);
    EXPECT_EQ(c.records[2].text, "&");
    EXPECT_EQ(c.records[3].text, " ");
    EXPECT_EQ(c.records[4].text, "b");
    EXPECT_EQ(c.records[5].type, html::TokenType::kEndTag);
    EXPECT_EQ(c.records[5].tag.tag_name, "style");
    EXPECT_EQ(c.records[6].text, "c");
    EXPECT_EQ(c.records[7].type, html::TokenType::kEof);
    EXPECT_TRUE(AllCharText(c) == "a & bc");
    EXPECT_TRUE(c.errors.empty());
}

TEST(HtmlLexerRawtext, CommentLikeTextIsJustText)
{
    auto c = LexContent(html::State::kRawtext, u"style", u"<!-- not a comment -->");
    EXPECT_TRUE(c.errors.empty());
    for (const Record& r : c.records) {
        EXPECT_TRUE(r.type == html::TokenType::kCharacter ||
                    r.type == html::TokenType::kWhitespaceCharacter ||
                    r.type == html::TokenType::kEof);
    }
    EXPECT_TRUE(AllCharText(c) == "<!-- not a comment -->");
}

// ---------------------------------------------------------------------------
// RCDATA
// ---------------------------------------------------------------------------

TEST(HtmlLexerRcdata, ReferencesDecoded)
{
    auto c = LexContent(html::State::kRcdata, u"title",
                        std::u16string{u'a', u' ', u'&', u'a', u'm', u'p', u';', u' ', 0,
                                       u' ', u'<', u' ', u'b'});
    EXPECT_EQ(CountError(c, html::Err::kUnexpectedNullCharacter), size_t{1});
    EXPECT_TRUE(AllCharText(c) == "a & " + U8(0xFFFD) + " < b");
}

TEST(HtmlLexerRcdata, ClosingEndTagRecognized)
{
    auto c = LexContent(html::State::kRcdata, u"title", u"x</title>y");
    ASSERT_EQ(c.records.size(), size_t{4});
    EXPECT_EQ(c.records[0].text, "x");
    EXPECT_EQ(c.records[1].type, html::TokenType::kEndTag);
    EXPECT_EQ(c.records[1].tag.tag_name, "title");
    EXPECT_EQ(c.records[2].text, "y");
    EXPECT_EQ(c.records[3].type, html::TokenType::kEof);
    EXPECT_TRUE(c.errors.empty());
}

// ---------------------------------------------------------------------------
// Script data
// ---------------------------------------------------------------------------

TEST(HtmlLexerScript, CommentLikeTextIsCharacterData)
{
    auto c = LexContent(html::State::kScriptData, u"script", u"<!-- a -->");
    EXPECT_TRUE(c.errors.empty());
    for (const Record& r : c.records) {
        EXPECT_TRUE(r.type == html::TokenType::kCharacter ||
                    r.type == html::TokenType::kWhitespaceCharacter ||
                    r.type == html::TokenType::kEof);
    }
    EXPECT_TRUE(AllCharText(c) == "<!-- a -->");
}

TEST(HtmlLexerScript, UnterminatedCommentLikeText)
{
    auto c = LexContent(html::State::kScriptData, u"script", u"<!-- x");
    EXPECT_EQ(CountError(c, html::Err::kEofInScriptHtmlCommentLikeText), size_t{1});
}

TEST(HtmlLexerScript, EndTagClosesScript)
{
    auto c = LexContent(html::State::kScriptData, u"script", u"var a = 1</script>");
    ASSERT_GT(c.records.size(), size_t{1});
    const Record& pre_eof = c.records[c.records.size() - 2];
    EXPECT_EQ(pre_eof.type, html::TokenType::kEndTag);
    EXPECT_EQ(pre_eof.tag.tag_name, "script");
    EXPECT_EQ(c.records.back().type, html::TokenType::kEof);
}

// ---------------------------------------------------------------------------
// Plaintext
// ---------------------------------------------------------------------------

TEST(HtmlLexerPlaintext, EverythingIsText)
{
    auto c = LexContent(html::State::kPlaintext, u"", u"<b>hi &amp; done");
    EXPECT_TRUE(c.errors.empty());
    for (const Record& r : c.records) {
        EXPECT_TRUE(r.type == html::TokenType::kCharacter ||
                    r.type == html::TokenType::kWhitespaceCharacter ||
                    r.type == html::TokenType::kEof);
    }
    EXPECT_TRUE(AllCharText(c) == "<b>hi &amp; done");
}

// ---------------------------------------------------------------------------
// Doctype
// ---------------------------------------------------------------------------

TEST(HtmlLexerDoctype, SimpleConforming)
{
    auto c = LexAll(u"<!DOCTYPE html>");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_EQ(c.records[0].type, html::TokenType::kDoctype);
    EXPECT_EQ(c.records[0].text, "html");
    EXPECT_TRUE(c.errors.empty());
    EXPECT_TRUE(c.records[0].doctype.has_name);
    EXPECT_EQ(c.records[0].doctype.name, "html");
    EXPECT_FALSE(c.records[0].doctype.force_quirks);
    EXPECT_FALSE(c.records[0].doctype.has_public_id);
    EXPECT_FALSE(c.records[0].doctype.has_system_id);
    EXPECT_TRUE(html::IsConformingDoctype(c.records[0].doctype));
    EXPECT_EQ(html::GetDocumentMode(c.records[0].doctype), html::DocumentMode::kNoQuirks);
}

TEST(HtmlLexerDoctype, KeywordCaseInsensitive)
{
    auto c = LexAll(u"<!doCTYPe html>");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_TRUE(c.records[0].doctype.has_name);
    EXPECT_EQ(c.records[0].doctype.name, "html");
    EXPECT_TRUE(html::IsConformingDoctype(c.records[0].doctype));
    EXPECT_EQ(html::GetDocumentMode(c.records[0].doctype), html::DocumentMode::kNoQuirks);
}

TEST(HtmlLexerDoctype, LegacyPublicIdentifierTriggersQuirks)
{
    auto c = LexAll(u"<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_TRUE(c.records[0].doctype.has_public_id);
    EXPECT_EQ(c.records[0].doctype.public_id, "-//W3C//DTD HTML 4.01 Transitional//EN");
    EXPECT_FALSE(c.records[0].doctype.has_system_id);
    EXPECT_TRUE(c.errors.empty());
    EXPECT_FALSE(html::IsConformingDoctype(c.records[0].doctype));
    EXPECT_EQ(html::GetDocumentMode(c.records[0].doctype), html::DocumentMode::kQuirks);
}

TEST(HtmlLexerDoctype, LegacyCompatSystemIdentifierIsConforming)
{
    auto c = LexAll(u"<!DOCTYPE html SYSTEM \"about:legacy-compat\">");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_TRUE(c.records[0].doctype.has_system_id);
    EXPECT_EQ(c.records[0].doctype.system_id, "about:legacy-compat");
    EXPECT_TRUE(c.errors.empty());
    EXPECT_TRUE(html::IsConformingDoctype(c.records[0].doctype));
    EXPECT_EQ(html::GetDocumentMode(c.records[0].doctype), html::DocumentMode::kNoQuirks);
}

TEST(HtmlLexerDoctype, XhtmlTransitionalLimitedQuirks)
{
    auto c = LexAll(u"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" "
                    u"\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">");
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_TRUE(c.records[0].doctype.has_public_id);
    EXPECT_TRUE(c.records[0].doctype.has_system_id);
    EXPECT_TRUE(c.errors.empty());
    EXPECT_FALSE(html::IsConformingDoctype(c.records[0].doctype));
    EXPECT_EQ(html::GetDocumentMode(c.records[0].doctype), html::DocumentMode::kLimitedQuirks);
}

TEST(HtmlLexerDoctype, MissingNameForcesQuirks)
{
    auto c = LexAll(u"<!DOCTYPE>");
    EXPECT_EQ(CountError(c, html::Err::kMissingDoctypeName), size_t{1});
    ASSERT_EQ(c.records.size(), size_t{2});
    EXPECT_FALSE(c.records[0].doctype.has_name);
    EXPECT_TRUE(c.records[0].doctype.force_quirks);
    EXPECT_EQ(html::GetDocumentMode(c.records[0].doctype), html::DocumentMode::kQuirks);
}

TEST(HtmlLexerDoctype, GetDocumentModeMatrix)
{
    auto Mode = [](bool has_name, std::string name, bool has_public, std::string public_id,
                   bool has_system, std::string system_id) {
        html::DoctypeToken token;
        token.has_name = has_name;
        token.name = std::move(name);
        token.has_public_id = has_public;
        token.public_id = std::move(public_id);
        token.has_system_id = has_system;
        token.system_id = std::move(system_id);
        return html::GetDocumentMode(token);
    };

    EXPECT_EQ(Mode(true, "html", false, "", false, ""), html::DocumentMode::kNoQuirks);
    EXPECT_EQ(Mode(true, "html", false, "", true, "about:legacy-compat"),
              html::DocumentMode::kNoQuirks);
    EXPECT_EQ(Mode(true, "html", true, "-//W3C//DTD HTML 4.0 Transitional//EN", false, ""),
              html::DocumentMode::kQuirks);
    EXPECT_EQ(Mode(true, "html", true, "-//W3C//DTD HTML 4.01 Transitional//EN", true, "x"),
              html::DocumentMode::kNoQuirks);
    EXPECT_EQ(Mode(true, "html", true, "-//W3C//DTD XHTML 1.0 Transitional//EN", true, "x"),
              html::DocumentMode::kLimitedQuirks);
    EXPECT_EQ(Mode(true, "html", false, "", true,
                   "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd"),
              html::DocumentMode::kQuirks);
    EXPECT_EQ(Mode(false, "", false, "", false, ""), html::DocumentMode::kQuirks);
    EXPECT_EQ(Mode(true, "svg", false, "", false, ""), html::DocumentMode::kQuirks);
}

TEST(HtmlLexerDoctype, IsConformingMatrix)
{
    html::DoctypeToken token;
    token.has_name = true;
    token.name = "html";
    EXPECT_TRUE(html::IsConformingDoctype(token));

    token.has_public_id = true;
    token.public_id = "x";
    EXPECT_FALSE(html::IsConformingDoctype(token));

    token.has_public_id = false;
    token.has_system_id = true;
    token.system_id = "about:legacy-compat";
    EXPECT_TRUE(html::IsConformingDoctype(token));

    token.system_id = "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd";
    EXPECT_FALSE(html::IsConformingDoctype(token));
}

// ---------------------------------------------------------------------------
// Preprocessor
// ---------------------------------------------------------------------------

TEST(HtmlPreprocessor, CrLfFoldedToLineFeed)
{
    html::Preprocessor pp;
    pp.SetInput(u"a\r\nb");
    EXPECT_EQ(pp.Advance(), u'a');
    EXPECT_EQ(pp.Advance(), u'\n');
    EXPECT_EQ(pp.Advance(), u'b');
}

TEST(HtmlPreprocessor, CarriageReturnNormalized)
{
    html::Preprocessor pp;
    pp.SetInput(u"a\rb");
    EXPECT_EQ(pp.Advance(), u'a');
    EXPECT_EQ(pp.Advance(), u'\n');
    EXPECT_EQ(pp.Advance(), u'b');
}

TEST(HtmlPreprocessor, SurrogatePairJoined)
{
    html::Preprocessor pp;
    pp.SetInput(U16(0x1F600));
    EXPECT_EQ(pp.Advance(), static_cast<char32_t>(0x1F600));
}

TEST(HtmlPreprocessor, LineColumnAndOffsetTracking)
{
    html::Preprocessor pp;
    pp.SetInput(u"ab\ncd");

    EXPECT_EQ(pp.Advance(), u'a');
    EXPECT_EQ(pp.line, 1);

    EXPECT_EQ(pp.Advance(), u'b');
    EXPECT_EQ(pp.Advance(), u'\n');
    EXPECT_EQ(pp.line, 1);
    EXPECT_EQ(pp.Col(), 3);

    EXPECT_EQ(pp.Advance(), u'c');
    EXPECT_EQ(pp.line, 2);
    EXPECT_EQ(pp.Col(), 1);
    EXPECT_EQ(pp.Offset(), 3);

    EXPECT_EQ(pp.Advance(), u'd');
    EXPECT_EQ(pp.Offset(), 4);
}

TEST(HtmlPreprocessor, StreamingWritePreservesLineState)
{
    html::Preprocessor pp;
    pp.Write(u"ab\n", false);
    EXPECT_EQ(pp.Advance(), u'a');
    EXPECT_EQ(pp.Advance(), u'b');
    EXPECT_EQ(pp.Advance(), u'\n');

    pp.Write(u"cd", true);
    EXPECT_EQ(pp.Advance(), u'c');
    EXPECT_EQ(pp.line, 2);
    EXPECT_EQ(pp.Offset(), 3);
    EXPECT_EQ(pp.Advance(), u'd');
}

// ---------------------------------------------------------------------------
// Logger integration: html::Err -> logger::MsgCat catalog -> FormatMsg
// ---------------------------------------------------------------------------

TEST(HtmlLogger, ErrDescriptionIdentifiesControlCharacterError)
{
    EXPECT_EQ(html::ErrDescription(html::Err::kControlCharacterInInputStream),
              "A control character was found in the input stream");
}

TEST(HtmlLogger, ErrDescriptionIdentifiesCharacterReferenceError)
{
    EXPECT_EQ(html::ErrDescription(html::Err::kUnknownNamedCharacterReference),
              "The named character reference was not recognized");
}

TEST(HtmlLogger, ErrDescriptionIdentifiesCommentError)
{
    EXPECT_EQ(html::ErrDescription(html::Err::kNestedComment),
              "A comment contained a nested comment marker");
}

TEST(HtmlLogger, ErrDescriptionIdentifiesNumericReferenceError)
{
    EXPECT_EQ(html::ErrDescription(html::Err::kNullCharacterReference),
              "A numeric character reference referenced the NULL character");
}

TEST(HtmlLogger, ErrDescriptionMatchesCatalogTemplate)
{
    EXPECT_EQ(html::ErrDescription(html::Err::kMissingSemicolonAfterCharacterReference),
              logger::FormatMsg(logger::MsgCat::kHTML_MissingSemicolonAfterCharacterReference));
    EXPECT_EQ(html::ErrDescription(html::Err::kDuplicateAttribute),
              logger::FormatMsg(logger::MsgCat::kHTML_DuplicateAttribute));
    EXPECT_EQ(html::ErrDescription(html::Err::kMisplacedDoctype),
              logger::FormatMsg(logger::MsgCat::kHTML_MisplacedDoctype));
}

TEST(HtmlLogger, EveryErrCodeProducesNonEmptyMessage)
{
    const int last = static_cast<int>(html::Err::kEofInElementThatCanContainOnlyText);
    for (int i = 0; i <= last; ++i) {
        const html::Err code = static_cast<html::Err>(i);
        const std::string msg = html::ErrDescription(code);
        EXPECT_FALSE(msg.empty()) << "missing logger message for Err(" << i << ")";
    }
}

TEST(HtmlLogger, MessagesContainNoLeftoverPlaceholders)
{
    const int last = static_cast<int>(html::Err::kEofInElementThatCanContainOnlyText);
    for (int i = 0; i <= last; ++i) {
        const html::Err code = static_cast<html::Err>(i);
        const std::string msg = html::ErrDescription(code);
        EXPECT_EQ(msg.find("{}"), std::string::npos)
            << "unfilled placeholder in message for Err(" << i << "): " << msg;
    }
}

TEST(HtmlLogger, ParametrizedBridgeTemplateFillsPlaceholder)
{
    std::string url = "https://cdn.example.com/app.js";
    EXPECT_EQ(logger::FormatMsg(logger::MsgCat::kHTML_EmptyResourceURL, url),
              "https://cdn.example.com/app.js has an empty resource URL");
}

TEST(HtmlLogger, GenericParseErrorFallbackIsReportable)
{
    EXPECT_FALSE(logger::FormatMsg(logger::MsgCat::kHTML_ParseError).empty());
}

} // namespace