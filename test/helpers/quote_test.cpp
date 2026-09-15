#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <string_view>

using guchho::helpers::QuoteForJSON;
using guchho::helpers::QuoteSingle;
using guchho::helpers::quoteString;

// ---------------------------------------------------------------------------
// QuoteSingle
// ---------------------------------------------------------------------------

TEST(QuoteSingleTest, EmptyString)
{
    EXPECT_EQ(QuoteSingle("", false), "''");
}

TEST(QuoteSingleTest, SimpleASCII)
{
    EXPECT_EQ(QuoteSingle("hello", false), "'hello'");
}

TEST(QuoteSingleTest, SingleQuoteNotEscaped)
{
    EXPECT_EQ(QuoteSingle("it's", false), "'it's'");
}

TEST(QuoteSingleTest, BackslashEscaped)
{
    EXPECT_EQ(QuoteSingle("a\\b", false), "'a\\\\b'");
}

TEST(QuoteSingleTest, DoubleQuoteNotEscaped)
{
    EXPECT_EQ(QuoteSingle("say \"hi\"", false), "'say \"hi\"'");
}

TEST(QuoteSingleTest, NewlineEscaped)
{
    EXPECT_EQ(QuoteSingle("a\nb", false), "'a\\nb'");
}

TEST(QuoteSingleTest, TabEscaped)
{
    EXPECT_EQ(QuoteSingle("a\tb", false), "'a\\tb'");
}

TEST(QuoteSingleTest, CarriageReturnEscaped)
{
    EXPECT_EQ(QuoteSingle("a\rb", false), "'a\\rb'");
}

TEST(QuoteSingleTest, BackspaceEscaped)
{
    EXPECT_EQ(QuoteSingle("a\bb", false), "'a\\bb'");
}

TEST(QuoteSingleTest, FormFeedEscaped)
{
    EXPECT_EQ(QuoteSingle("a\fb", false), "'a\\fb'");
}

TEST(QuoteSingleTest, NonPrintableASCIIEscaped)
{
    EXPECT_EQ(QuoteSingle(std::string(1, '\x01'), false), "'\\u0001'");
}

TEST(QuoteSingleTest, NullByteEscaped)
{
    EXPECT_EQ(QuoteSingle(std::string(1, '\0'), false), "'\\u0000'");
}

TEST(QuoteSingleTest, UnicodePassThrough)
{
    EXPECT_EQ(QuoteSingle("\xC3\xA9", false), "'\xC3\xA9'");
}

TEST(QuoteSingleTest, UnicodeAsciiOnly)
{
    EXPECT_EQ(QuoteSingle("\xC3\xA9", true), "'\\u00E9'");
}

TEST(QuoteSingleTest, SurrogatePair)
{
    EXPECT_EQ(QuoteSingle("\xF0\x9F\x98\x80", false), "'\xF0\x9F\x98\x80'");
}

TEST(QuoteSingleTest, SurrogatePairAsciiOnly)
{
    EXPECT_EQ(QuoteSingle("\xF0\x9F\x98\x80", true), "'\\uD83D\\uDE00'");
}

TEST(QuoteSingleTest, BOMEscaped)
{
    EXPECT_EQ(QuoteSingle("\xEF\xBB\xBF", false), "'\\uFEFF'");
}

TEST(QuoteSingleTest, MultipleEscapes)
{
    EXPECT_EQ(QuoteSingle("a\t\nb", false), "'a\\t\\nb'");
}

// ---------------------------------------------------------------------------
// QuoteForJSON
// ---------------------------------------------------------------------------

TEST(QuoteForJSONTest, EmptyString)
{
    EXPECT_EQ(QuoteForJSON("", false), "\"\"");
}

TEST(QuoteForJSONTest, SimpleASCII)
{
    EXPECT_EQ(QuoteForJSON("hello", false), "\"hello\"");
}

TEST(QuoteForJSONTest, DoubleQuoteEscaped)
{
    EXPECT_EQ(QuoteForJSON("say \"hi\"", false), "\"say \\\"hi\\\"\"");
}

TEST(QuoteForJSONTest, SingleQuoteNotEscaped)
{
    EXPECT_EQ(QuoteForJSON("it's", false), "\"it's\"");
}

TEST(QuoteForJSONTest, BackslashEscaped)
{
    EXPECT_EQ(QuoteForJSON("a\\b", false), "\"a\\\\b\"");
}

TEST(QuoteForJSONTest, NewlineEscaped)
{
    EXPECT_EQ(QuoteForJSON("a\nb", false), "\"a\\nb\"");
}

TEST(QuoteForJSONTest, TabEscaped)
{
    EXPECT_EQ(QuoteForJSON("a\tb", false), "\"a\\tb\"");
}

TEST(QuoteForJSONTest, CarriageReturnEscaped)
{
    EXPECT_EQ(QuoteForJSON("a\rb", false), "\"a\\rb\"");
}

TEST(QuoteForJSONTest, BackspaceEscaped)
{
    EXPECT_EQ(QuoteForJSON("a\bb", false), "\"a\\bb\"");
}

TEST(QuoteForJSONTest, FormFeedEscaped)
{
    EXPECT_EQ(QuoteForJSON("a\fb", false), "\"a\\fb\"");
}

TEST(QuoteForJSONTest, NonPrintableASCIIEscaped)
{
    EXPECT_EQ(QuoteForJSON(std::string(1, '\x01'), false), "\"\\u0001\"");
}

TEST(QuoteForJSONTest, NullByteEscaped)
{
    EXPECT_EQ(QuoteForJSON(std::string(1, '\0'), false), "\"\\u0000\"");
}

TEST(QuoteForJSONTest, UnicodePassThrough)
{
    EXPECT_EQ(QuoteForJSON("\xC3\xA9", false), "\"\xC3\xA9\"");
}

TEST(QuoteForJSONTest, UnicodeAsciiOnly)
{
    EXPECT_EQ(QuoteForJSON("\xC3\xA9", true), "\"\\u00E9\"");
}

TEST(QuoteForJSONTest, SurrogatePair)
{
    EXPECT_EQ(QuoteForJSON("\xF0\x9F\x98\x80", false), "\"\xF0\x9F\x98\x80\"");
}

TEST(QuoteForJSONTest, SurrogatePairAsciiOnly)
{
    EXPECT_EQ(QuoteForJSON("\xF0\x9F\x98\x80", true), "\"\\uD83D\\uDE00\"");
}

TEST(QuoteForJSONTest, BOMEscaped)
{
    EXPECT_EQ(QuoteForJSON("\xEF\xBB\xBF", false), "\"\\uFEFF\"");
}

// ---------------------------------------------------------------------------
// quoteString
// ---------------------------------------------------------------------------

TEST(QuoteStringTest, EmptyString)
{
    EXPECT_EQ(quoteString(""), "\"\"");
}

TEST(QuoteStringTest, SimpleASCII)
{
    EXPECT_EQ(quoteString("hello"), "\"hello\"");
}

TEST(QuoteStringTest, DoubleQuoteEscaped)
{
    EXPECT_EQ(quoteString("say \"hi\""), "\"say \\\"hi\\\"\"");
}

TEST(QuoteStringTest, BackslashEscaped)
{
    EXPECT_EQ(quoteString("a\\b"), "\"a\\\\b\"");
}

TEST(QuoteStringTest, NewlineEscaped)
{
    EXPECT_EQ(quoteString("a\nb"), "\"a\\nb\"");
}

TEST(QuoteStringTest, TabEscaped)
{
    EXPECT_EQ(quoteString("a\tb"), "\"a\\tb\"");
}

TEST(QuoteStringTest, CarriageReturnEscaped)
{
    EXPECT_EQ(quoteString("a\rb"), "\"a\\rb\"");
}

TEST(QuoteStringTest, BackspaceEscaped)
{
    EXPECT_EQ(quoteString("a\bb"), "\"a\\bb\"");
}

TEST(QuoteStringTest, FormFeedEscaped)
{
    EXPECT_EQ(quoteString("a\fb"), "\"a\\fb\"");
}

TEST(QuoteStringTest, VerticalTabEscaped)
{
    EXPECT_EQ(quoteString("a\vb"), "\"a\\vb\"");
}

TEST(QuoteStringTest, BellEscaped)
{
    EXPECT_EQ(quoteString("a\ab"), "\"a\\ab\"");
}

TEST(QuoteStringTest, DELHexEscaped)
{
    EXPECT_EQ(quoteString(std::string(1, '\x7F')), "\"\\x7f\"");
}

TEST(QuoteStringTest, ControlBelowSpaceHexEscaped)
{
    EXPECT_EQ(quoteString(std::string(1, '\x01')), "\"\\x01\"");
}

TEST(QuoteStringTest, NullByteHexEscaped)
{
    EXPECT_EQ(quoteString(std::string(1, '\0')), "\"\\x00\"");
}

TEST(QuoteStringTest, PrintableNonASCIIPassThrough)
{
    EXPECT_EQ(quoteString("\xC3\xA9"), "\"\xC3\xA9\"");
}

TEST(QuoteStringTest, CJKPassThrough)
{
    EXPECT_EQ(quoteString("\xE4\xB8\x96"), "\"\xE4\xB8\x96\"");
}

TEST(QuoteStringTest, SupplementaryPlanePassThrough)
{
    EXPECT_EQ(quoteString("\xF0\x9F\x98\x80"), "\"\xF0\x9F\x98\x80\"");
}

TEST(QuoteStringTest, BMPNonPrintableEscape)
{
    EXPECT_EQ(quoteString("\xC2\x80"), "\"\\u0080\"");
}

TEST(QuoteStringTest, MultipleEscapes)
{
    EXPECT_EQ(quoteString("a\t\nb"), "\"a\\t\\nb\"");
}

TEST(QuoteStringTest, MixedContent)
{
    EXPECT_EQ(quoteString("hi\tworld\n"), "\"hi\\tworld\\n\"");
}
