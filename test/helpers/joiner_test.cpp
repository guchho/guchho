#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <span>
#include <string>

using guchho::helpers::Joiner;

namespace {
    // Builds a span over a raw char buffer so tests pass byte lengths
    // explicitly and never rely on null termination.
    std::span<const char> Span(const char* data, std::size_t size)
    {
        return std::span<const char>(data, size);
    }
}

// ---------------------------------------------------------------------------
// Done (empty)
// ---------------------------------------------------------------------------

TEST(JoinerDoneTest, EmptyJoiner)
{
    Joiner j;
    EXPECT_EQ(j.Done(), "");
    EXPECT_EQ(j.Length(), 0u);
}

// ---------------------------------------------------------------------------
// AddString
// ---------------------------------------------------------------------------

TEST(JoinerAddStringTest, SingleString)
{
    Joiner j;
    j.AddString("hello");
    EXPECT_EQ(j.Done(), "hello");
    EXPECT_EQ(j.Length(), 5u);
}

TEST(JoinerAddStringTest, MultipleStrings)
{
    Joiner j;
    j.AddString("a");
    j.AddString("b");
    j.AddString("c");
    EXPECT_EQ(j.Done(), "abc");
    EXPECT_EQ(j.Length(), 3u);
}

TEST(JoinerAddStringTest, EmptyString)
{
    Joiner j;
    j.AddString("");
    j.AddString("x");
    j.AddString("");
    EXPECT_EQ(j.Done(), "x");
    EXPECT_EQ(j.Length(), 1u);
}

TEST(JoinerAddStringTest, LongerContent)
{
    Joiner j;
    j.AddString("hello ");
    j.AddString("world");
    EXPECT_EQ(j.Done(), "hello world");
}

// ---------------------------------------------------------------------------
// AddBytes
// ---------------------------------------------------------------------------

TEST(JoinerAddBytesTest, SingleSpan)
{
    const char data[] = "abc";
    Joiner j;
    j.AddBytes(Span(data, 3));
    EXPECT_EQ(j.Done(), "abc");
}

TEST(JoinerAddBytesTest, MultipleSpans)
{
    const char a[] = "x";
    const char b[] = "yz";
    Joiner j;
    j.AddBytes(Span(a, 1));
    j.AddBytes(Span(b, 2));
    EXPECT_EQ(j.Done(), "xyz");
    EXPECT_EQ(j.Length(), 3u);
}

TEST(JoinerAddBytesTest, EmptySpan)
{
    Joiner j;
    j.AddBytes(Span(nullptr, 0));
    j.AddBytes(Span("ok", 2));
    EXPECT_EQ(j.Done(), "ok");
}

TEST(JoinerAddBytesTest, ContainsBinaryZero)
{
    const char raw[] = {'A', '\0', 'B'};
    Joiner j;
    j.AddBytes(Span(raw, 3));
    EXPECT_EQ(j.Done(), std::string("A\0B", 3));
    EXPECT_EQ(j.Length(), 3u);
}

// ---------------------------------------------------------------------------
// AddString + AddBytes interleaved
// ---------------------------------------------------------------------------

TEST(JoinerMixedTest, InterleavedStringsAndBytes)
{
    const char raw[] = {'B', 'i', 'n', '\0'};
    Joiner j;
    j.AddString("A");
    j.AddBytes(Span(raw, 4));
    j.AddString("E");
    EXPECT_EQ(j.Done(), std::string("ABin\0E", 6));
    EXPECT_EQ(j.Length(), 6u);
}

// ---------------------------------------------------------------------------
// LastByte / Length
// ---------------------------------------------------------------------------

TEST(JoinerAccessorsTest, LastByteTracksAppends)
{
    Joiner j;
    EXPECT_EQ(j.LastByte(), 0u);
    j.AddString("hello");
    EXPECT_EQ(j.LastByte(), static_cast<uint8_t>('o'));
    j.AddString(" world");
    EXPECT_EQ(j.LastByte(), static_cast<uint8_t>('d'));
}

TEST(JoinerAccessorsTest, LengthAccumulates)
{
    Joiner j;
    EXPECT_EQ(j.Length(), 0u);
    j.AddString("abc");
    EXPECT_EQ(j.Length(), 3u);
    j.AddBytes(Span("def", 3));
    EXPECT_EQ(j.Length(), 6u);
}

// ---------------------------------------------------------------------------
// EnsureNewlineAtEnd
// ---------------------------------------------------------------------------

TEST(JoinerEnsureNewlineTest, EmptyJoinerIsNoop)
{
    Joiner j;
    j.EnsureNewlineAtEnd();
    EXPECT_EQ(j.Done(), "");
    EXPECT_EQ(j.Length(), 0u);
}

TEST(JoinerEnsureNewlineTest, AlreadyEndsWithNewline)
{
    Joiner j;
    j.AddString("line1\n");
    j.EnsureNewlineAtEnd();
    EXPECT_EQ(j.Done(), "line1\n");
    EXPECT_EQ(j.Length(), 6u);
}

TEST(JoinerEnsureNewlineTest, AppendsNewline)
{
    Joiner j;
    j.AddString("line1");
    j.EnsureNewlineAtEnd();
    EXPECT_EQ(j.Done(), "line1\n");
    EXPECT_EQ(j.Length(), 6u);
}

TEST(JoinerEnsureNewlineTest, SingleNewlineChar)
{
    Joiner j;
    j.AddString("hello");
    j.AddBytes(Span("!", 1));
    j.EnsureNewlineAtEnd();
    EXPECT_EQ(j.Done(), "hello!\n");
}

// ---------------------------------------------------------------------------
// Contains
// ---------------------------------------------------------------------------

TEST(JoinerContainsTest, FoundInString)
{
    Joiner j;
    j.AddString("hello world");
    EXPECT_TRUE(j.Contains("world", Span(nullptr, 0)));
}

TEST(JoinerContainsTest, FoundInBytes)
{
    const char data[] = "abcdef";
    Joiner j;
    j.AddBytes(Span(data, 6));
    EXPECT_TRUE(j.Contains({}, Span("cde", 3)));
}

TEST(JoinerContainsTest, NotFoundInEither)
{
    const char data[] = "abc";
    Joiner j;
    j.AddString("hello");
    j.AddBytes(Span(data, 3));
    EXPECT_FALSE(j.Contains("xyz", Span("zzz", 3)));
    EXPECT_FALSE(j.Contains("xyz", Span("def", 3)));
}

TEST(JoinerContainsTest, EmptyBytePatternMatchesAnyByteSegment)
{
    // std::search treats an empty pattern as found at the start of every
    // range, so an empty span makes Contains return true whenever the
    // joiner holds any byte segment.
    const char data[] = "abc";
    Joiner j;
    j.AddBytes(Span(data, 3));
    EXPECT_TRUE(j.Contains({}, Span(nullptr, 0)));
}

TEST(JoinerContainsTest, EmptyStringPatternMatchesAnyStringSegment)
{
    // find("") succeeds at position 0, so an empty string_view makes
    // Contains return true whenever the joiner holds any string segment.
    Joiner j;
    j.AddString("hello");
    EXPECT_TRUE(j.Contains({}, Span(nullptr, 0)));
}

TEST(JoinerContainsTest, FoundInByteSegment)
{
    const char raw[] = "abc";
    Joiner j;
    j.AddString("hel");
    j.AddBytes(Span(raw, 3));
    EXPECT_TRUE(j.Contains("hel", Span(nullptr, 0)));
    EXPECT_TRUE(j.Contains({}, Span("bc", 2)));
}

TEST(JoinerContainsTest, EmptyJoinerReturnsFalse)
{
    Joiner j;
    EXPECT_FALSE(j.Contains("anything", Span(nullptr, 0)));
    EXPECT_FALSE(j.Contains({}, Span(nullptr, 0)));
}