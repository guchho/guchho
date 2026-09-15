#include "test/guchho_test.hpp"
#include "guchho/unicode.hpp"

#include <string_view>

using guchho::unicode::IsIdentifier;
using guchho::unicode::IsIdentifierContinue;
using guchho::unicode::IsIdentifierContinueES5AndESNext;
using guchho::unicode::IsIdentifierES5AndESNext;
using guchho::unicode::IsIdentifierStart;
using guchho::unicode::IsIdentifierStartES5AndESNext;
using guchho::unicode::IsInRangeTable;
using guchho::unicode::Range16;
using guchho::unicode::Range32;
using guchho::unicode::RangeTable;

// ---------------------------------------------------------------------------
// IsInRanges
// ---------------------------------------------------------------------------

TEST(IsInRangesTest, EmptyRangeReturnsFalse)
{
    EXPECT_FALSE(guchho::unicode::IsInRanges<Range16>(nullptr, 0, 0x41));
}

TEST(IsInRangesTest, SingleRangeMatch)
{
    constexpr Range16 ranges[] = { {0x0041, 0x005A, 1} };
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x41));
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x5A));
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x4D));
}

TEST(IsInRangesTest, SingleRangeMismatch)
{
    constexpr Range16 ranges[] = { {0x0041, 0x005A, 1} };
    EXPECT_FALSE(IsInRanges(ranges, 1, 0x40));
    EXPECT_FALSE(IsInRanges(ranges, 1, 0x5B));
}

TEST(IsInRangesTest, StrideSkipsValues)
{
    constexpr Range16 ranges[] = { {0x0041, 0x0045, 2} };
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x41));
    EXPECT_FALSE(IsInRanges(ranges, 1, 0x42));
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x43));
    EXPECT_FALSE(IsInRanges(ranges, 1, 0x44));
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x45));
}

TEST(IsInRangesTest, MultipleRanges)
{
    constexpr Range16 ranges[] = {
        {0x0030, 0x0039, 1},
        {0x0041, 0x0046, 1},
    };
    EXPECT_TRUE(IsInRanges(ranges, 2, 0x35));
    EXPECT_TRUE(IsInRanges(ranges, 2, 0x41));
    EXPECT_FALSE(IsInRanges(ranges, 2, 0x3A));
    EXPECT_FALSE(IsInRanges(ranges, 2, 0x47));
}

TEST(IsInRangesTest, BoundaryValues)
{
    constexpr Range16 ranges[] = { {0x0000, 0x00FF, 1} };
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x0000));
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x00FF));
    EXPECT_FALSE(IsInRanges(ranges, 1, 0x0100));
}

TEST(IsInRangesTest, Range32Support)
{
    constexpr Range32 ranges[] = { {0x10000, 0x1000F, 1} };
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x10000));
    EXPECT_TRUE(IsInRanges(ranges, 1, 0x1000F));
    EXPECT_FALSE(IsInRanges(ranges, 1, 0xFFFF));
    EXPECT_FALSE(IsInRanges(ranges, 1, 0x10010));
}

// ---------------------------------------------------------------------------
// IsInRangeTable
// ---------------------------------------------------------------------------

TEST(IsInRangeTableTest, EmptyTableReturnsFalse)
{
    RangeTable table = { 0, nullptr, 0, nullptr, 0 };
    EXPECT_FALSE(IsInRangeTable(table, 0x41));
}

TEST(IsInRangeTableTest, UsesR16ForLowCodePoints)
{
    static constexpr Range16 r16[] = { {0x0041, 0x005A, 1} };
    RangeTable table = { 0, r16, 1, nullptr, 0 };
    EXPECT_TRUE(IsInRangeTable(table, 0x41));
    EXPECT_TRUE(IsInRangeTable(table, 0x5A));
    EXPECT_FALSE(IsInRangeTable(table, 0x40));
}

TEST(IsInRangeTableTest, UsesR32ForHighCodePoints)
{
    static constexpr Range32 r32[] = { {0x10000, 0x1000F, 1} };
    RangeTable table = { 0, nullptr, 0, r32, 1 };
    EXPECT_TRUE(IsInRangeTable(table, 0x10000));
    EXPECT_TRUE(IsInRangeTable(table, 0x1000F));
    EXPECT_FALSE(IsInRangeTable(table, 0xFFFF));
}

TEST(IsInRangeTableTest, LatinOffsetOptimization)
{
    static constexpr Range16 r16[] = { {0x0000, 0x00FF, 1} };
    RangeTable table = { 0, r16, 1, nullptr, 0 };
    EXPECT_TRUE(IsInRangeTable(table, 0x0041));
    EXPECT_TRUE(IsInRangeTable(table, 0x00FF));
}

// ---------------------------------------------------------------------------
// IsIdentifierStart
// ---------------------------------------------------------------------------

TEST(IsIdentifierStartTest, LowercaseLetters)
{
    EXPECT_TRUE(IsIdentifierStart('a'));
    EXPECT_TRUE(IsIdentifierStart('z'));
    EXPECT_TRUE(IsIdentifierStart('m'));
}

TEST(IsIdentifierStartTest, UppercaseLetters)
{
    EXPECT_TRUE(IsIdentifierStart('A'));
    EXPECT_TRUE(IsIdentifierStart('Z'));
    EXPECT_TRUE(IsIdentifierStart('M'));
}

TEST(IsIdentifierStartTest, UnderscoreAndDollar)
{
    EXPECT_TRUE(IsIdentifierStart('_'));
    EXPECT_TRUE(IsIdentifierStart('$'));
}

TEST(IsIdentifierStartTest, DigitsAreNotValid)
{
    EXPECT_FALSE(IsIdentifierStart('0'));
    EXPECT_FALSE(IsIdentifierStart('9'));
}

TEST(IsIdentifierStartTest, SpaceAndPunctuation)
{
    EXPECT_FALSE(IsIdentifierStart(' '));
    EXPECT_FALSE(IsIdentifierStart('.'));
    EXPECT_FALSE(IsIdentifierStart('-'));
    EXPECT_FALSE(IsIdentifierStart('+'));
}

TEST(IsIdentifierStartTest, NullCharacter)
{
    EXPECT_FALSE(IsIdentifierStart('\0'));
}

// ---------------------------------------------------------------------------
// IsIdentifierContinue
// ---------------------------------------------------------------------------

TEST(IsIdentifierContinueTest, LowercaseLetters)
{
    EXPECT_TRUE(IsIdentifierContinue('a'));
    EXPECT_TRUE(IsIdentifierContinue('z'));
}

TEST(IsIdentifierContinueTest, UppercaseLetters)
{
    EXPECT_TRUE(IsIdentifierContinue('A'));
    EXPECT_TRUE(IsIdentifierContinue('Z'));
}

TEST(IsIdentifierContinueTest, Digits)
{
    EXPECT_TRUE(IsIdentifierContinue('0'));
    EXPECT_TRUE(IsIdentifierContinue('9'));
}

TEST(IsIdentifierContinueTest, UnderscoreAndDollar)
{
    EXPECT_TRUE(IsIdentifierContinue('_'));
    EXPECT_TRUE(IsIdentifierContinue('$'));
}

TEST(IsIdentifierContinueTest, ZeroWidthJoinerAndNonJoiner)
{
    EXPECT_TRUE(IsIdentifierContinue(0x200C));
    EXPECT_TRUE(IsIdentifierContinue(0x200D));
}

TEST(IsIdentifierContinueTest, SpaceAndPunctuation)
{
    EXPECT_FALSE(IsIdentifierContinue(' '));
    EXPECT_FALSE(IsIdentifierContinue('.'));
    EXPECT_FALSE(IsIdentifierContinue(','));
}

// ---------------------------------------------------------------------------
// IsIdentifierStartES5AndESNext
// ---------------------------------------------------------------------------

TEST(IsIdentifierStartES5AndESNextTest, ASCIICharacters)
{
    EXPECT_TRUE(IsIdentifierStartES5AndESNext('a'));
    EXPECT_TRUE(IsIdentifierStartES5AndESNext('Z'));
    EXPECT_TRUE(IsIdentifierStartES5AndESNext('_'));
    EXPECT_TRUE(IsIdentifierStartES5AndESNext('$'));
}

TEST(IsIdentifierStartES5AndESNextTest, DigitsAreNotValid)
{
    EXPECT_FALSE(IsIdentifierStartES5AndESNext('0'));
    EXPECT_FALSE(IsIdentifierStartES5AndESNext('5'));
    EXPECT_FALSE(IsIdentifierStartES5AndESNext('9'));
}

TEST(IsIdentifierStartES5AndESNextTest, AboveBMPRejected)
{
    EXPECT_FALSE(IsIdentifierStartES5AndESNext(0x10000));
    EXPECT_FALSE(IsIdentifierStartES5AndESNext(0x1F600));
}

// ---------------------------------------------------------------------------
// IsIdentifierContinueES5AndESNext
// ---------------------------------------------------------------------------

TEST(IsIdentifierContinueES5AndESNextTest, ASCIICharacters)
{
    EXPECT_TRUE(IsIdentifierContinueES5AndESNext('a'));
    EXPECT_TRUE(IsIdentifierContinueES5AndESNext('Z'));
    EXPECT_TRUE(IsIdentifierContinueES5AndESNext('0'));
    EXPECT_TRUE(IsIdentifierContinueES5AndESNext('9'));
    EXPECT_TRUE(IsIdentifierContinueES5AndESNext('_'));
    EXPECT_TRUE(IsIdentifierContinueES5AndESNext('$'));
}

TEST(IsIdentifierContinueES5AndESNextTest, ZeroWidthJoinerAndNonJoiner)
{
    EXPECT_TRUE(IsIdentifierContinueES5AndESNext(0x200C));
    EXPECT_TRUE(IsIdentifierContinueES5AndESNext(0x200D));
}

TEST(IsIdentifierContinueES5AndESNextTest, AboveBMPRejected)
{
    EXPECT_FALSE(IsIdentifierContinueES5AndESNext(0x10000));
    EXPECT_FALSE(IsIdentifierContinueES5AndESNext(0x1F600));
}

TEST(IsIdentifierContinueES5AndESNextTest, SpaceAndPunctuation)
{
    EXPECT_FALSE(IsIdentifierContinueES5AndESNext(' '));
    EXPECT_FALSE(IsIdentifierContinueES5AndESNext('.'));
}

// ---------------------------------------------------------------------------
// IsIdentifier
// ---------------------------------------------------------------------------

TEST(IsIdentifierTest, EmptyString)
{
    EXPECT_FALSE(IsIdentifier(""));
}

TEST(IsIdentifierTest, SimpleIdentifier)
{
    EXPECT_TRUE(IsIdentifier("abc"));
    EXPECT_TRUE(IsIdentifier("hello_world"));
    EXPECT_TRUE(IsIdentifier("$start"));
}

TEST(IsIdentifierTest, WithDigits)
{
    EXPECT_TRUE(IsIdentifier("abc123"));
    EXPECT_TRUE(IsIdentifier("var2"));
}

TEST(IsIdentifierTest, StartWithDigit)
{
    EXPECT_FALSE(IsIdentifier("123abc"));
    EXPECT_FALSE(IsIdentifier("0var"));
}

TEST(IsIdentifierTest, StartWithSpace)
{
    EXPECT_FALSE(IsIdentifier(" abc"));
}

TEST(IsIdentifierTest, StartWithPunctuation)
{
    EXPECT_FALSE(IsIdentifier(".foo"));
    EXPECT_FALSE(IsIdentifier("-bar"));
}

TEST(IsIdentifierTest, ContainsSpace)
{
    EXPECT_FALSE(IsIdentifier("hello world"));
}

TEST(IsIdentifierTest, ContainsPunctuation)
{
    EXPECT_FALSE(IsIdentifier("a.b"));
    EXPECT_FALSE(IsIdentifier("a,b"));
    EXPECT_FALSE(IsIdentifier("a-b"));
}

TEST(IsIdentifierTest, SingleCharacterIdentifiers)
{
    EXPECT_TRUE(IsIdentifier("a"));
    EXPECT_TRUE(IsIdentifier("Z"));
    EXPECT_TRUE(IsIdentifier("_"));
    EXPECT_TRUE(IsIdentifier("$"));
}

TEST(IsIdentifierTest, SingleDigitNotValid)
{
    EXPECT_FALSE(IsIdentifier("0"));
    EXPECT_FALSE(IsIdentifier("9"));
}

TEST(IsIdentifierTest, UnicodeIdentifier)
{
    EXPECT_TRUE(IsIdentifier("cafe\u0301"));
    EXPECT_TRUE(IsIdentifier("e\u0301"));
}

// ---------------------------------------------------------------------------
// IsIdentifierES5AndESNext
// ---------------------------------------------------------------------------

TEST(IsIdentifierES5AndESNextTest, EmptyString)
{
    EXPECT_FALSE(IsIdentifierES5AndESNext(""));
}

TEST(IsIdentifierES5AndESNextTest, SimpleIdentifier)
{
    EXPECT_TRUE(IsIdentifierES5AndESNext("abc"));
    EXPECT_TRUE(IsIdentifierES5AndESNext("hello_world"));
    EXPECT_TRUE(IsIdentifierES5AndESNext("$start"));
}

TEST(IsIdentifierES5AndESNextTest, WithDigits)
{
    EXPECT_TRUE(IsIdentifierES5AndESNext("abc123"));
    EXPECT_TRUE(IsIdentifierES5AndESNext("var2"));
}

TEST(IsIdentifierES5AndESNextTest, StartWithDigit)
{
    EXPECT_FALSE(IsIdentifierES5AndESNext("123abc"));
    EXPECT_FALSE(IsIdentifierES5AndESNext("0var"));
}

TEST(IsIdentifierES5AndESNextTest, StartWithSpace)
{
    EXPECT_FALSE(IsIdentifierES5AndESNext(" abc"));
}

TEST(IsIdentifierES5AndESNextTest, ContainsSpace)
{
    EXPECT_FALSE(IsIdentifierES5AndESNext("hello world"));
}

TEST(IsIdentifierES5AndESNextTest, SingleCharacterIdentifiers)
{
    EXPECT_TRUE(IsIdentifierES5AndESNext("a"));
    EXPECT_TRUE(IsIdentifierES5AndESNext("Z"));
    EXPECT_TRUE(IsIdentifierES5AndESNext("_"));
    EXPECT_TRUE(IsIdentifierES5AndESNext("$"));
}

TEST(IsIdentifierES5AndESNextTest, UnicodeIdentifier)
{
    EXPECT_TRUE(IsIdentifierES5AndESNext("cafe\u0301"));
    EXPECT_TRUE(IsIdentifierES5AndESNext("e\u0301"));
}
