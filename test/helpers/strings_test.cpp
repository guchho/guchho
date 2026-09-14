#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <utility>
#include <vector>

using guchho::helpers::ToLowerASCII;
using guchho::helpers::StringArraysEqual;
using guchho::helpers::StringArrayArraysEqual;
using guchho::helpers::StringArrayToQuotedCommaSeparatedString;
using guchho::helpers::EqualFoldASCII;

// ---------------------------------------------------------------------------
// ToLowerASCII
// ---------------------------------------------------------------------------

TEST(ToLowerASCIITest, Empty)
{
    EXPECT_EQ(ToLowerASCII(""), "");
}

TEST(ToLowerASCIITest, AlreadyLowercase)
{
    EXPECT_EQ(ToLowerASCII("https"), "https");
    EXPECT_EQ(ToLowerASCII("abc123"), "abc123");
}

TEST(ToLowerASCIITest, MixedCase)
{
    const std::pair<std::string, std::string> cases[] = {
        {"HeLLo-GUCHHO_123", "hello-guchho_123"},
        {"ABC", "abc"},
        {"aBcD", "abcd"},
    };
    for (const auto& [input, expected] : cases) {
        EXPECT_EQ(ToLowerASCII(input), expected);
    }
}

TEST(ToLowerASCIITest, NonAsciiAndPunctuationPassThrough)
{
    const std::string mixed_bytes = "ÅÄÖ!"; // high-bit UTF-8 bytes untouched
    EXPECT_EQ(ToLowerASCII(mixed_bytes), mixed_bytes);
    EXPECT_EQ(ToLowerASCII("UPPER/lower?"), "upper/lower?");
}

// ---------------------------------------------------------------------------
// StringArraysEqual
// ---------------------------------------------------------------------------

TEST(StringArraysEqualTest, Empty)
{
    EXPECT_TRUE(StringArraysEqual({}, {}));
}

TEST(StringArraysEqualTest, EqualInOrder)
{
    EXPECT_TRUE(StringArraysEqual({"a", "b"}, {"a", "b"}));
    EXPECT_TRUE(StringArraysEqual({"single"}, {"single"}));
}

TEST(StringArraysEqualTest, DifferentSizes)
{
    EXPECT_FALSE(StringArraysEqual({"a"}, {}));
    EXPECT_FALSE(StringArraysEqual({}, {"a"}));
    EXPECT_FALSE(StringArraysEqual({"a", "b"}, {"a", "b", "c"}));
}

TEST(StringArraysEqualTest, SameSizeDifferentValues)
{
    EXPECT_FALSE(StringArraysEqual({"a", "b"}, {"a", "c"}));
    EXPECT_FALSE(StringArraysEqual({"a", "b"}, {"b", "a"})); // order matters
}

TEST(StringArraysEqualTest, CaseSensitive)
{
    EXPECT_FALSE(StringArraysEqual({"a"}, {"A"}));
}

// ---------------------------------------------------------------------------
// StringArrayArraysEqual
// ---------------------------------------------------------------------------

TEST(StringArrayArraysEqualTest, Empty)
{
    EXPECT_TRUE(StringArrayArraysEqual({}, {}));
    EXPECT_TRUE(StringArrayArraysEqual({{}, {}}, {{}, {}}));
}

TEST(StringArrayArraysEqualTest, EqualAtBothLevels)
{
    EXPECT_TRUE(StringArrayArraysEqual({{"a"}, {"b", "c"}}, {{"a"}, {"b", "c"}}));
    EXPECT_TRUE(StringArrayArraysEqual({{"x", "y"}}, {{"x", "y"}}));
}

TEST(StringArrayArraysEqualTest, DifferentOuterSize)
{
    EXPECT_FALSE(StringArrayArraysEqual({{"a"}}, {{"a"}, {"b"}}));
    EXPECT_FALSE(StringArrayArraysEqual({{"a"}, {"b"}}, {{"a"}}));
}

TEST(StringArrayArraysEqualTest, DifferentInnerShape)
{
    EXPECT_FALSE(StringArrayArraysEqual({{"x", "y"}}, {{"x"}, {"y"}}));
    EXPECT_FALSE(StringArrayArraysEqual({{"x"}, {"y"}}, {{"x", "y"}}));
}

TEST(StringArrayArraysEqualTest, DifferentInnerValues)
{
    EXPECT_FALSE(StringArrayArraysEqual({{"a"}, {"b"}}, {{"a"}, {"c"}}));
}

// ---------------------------------------------------------------------------
// StringArrayToQuotedCommaSeparatedString
// ---------------------------------------------------------------------------

TEST(StringArrayToQuotedCommaSeparatedStringTest, Empty)
{
    EXPECT_EQ(StringArrayToQuotedCommaSeparatedString({}), "");
}

TEST(StringArrayToQuotedCommaSeparatedStringTest, SingleItem)
{
    EXPECT_EQ(StringArrayToQuotedCommaSeparatedString({"single"}), "\"single\"");
}

TEST(StringArrayToQuotedCommaSeparatedStringTest, MultipleItems)
{
    EXPECT_EQ(StringArrayToQuotedCommaSeparatedString({"foo", "bar"}),
              "\"foo\", \"bar\"");
    EXPECT_EQ(StringArrayToQuotedCommaSeparatedString({"a", "b", "c"}),
              "\"a\", \"b\", \"c\"");
}

TEST(StringArrayToQuotedCommaSeparatedStringTest, ItemsAreNotEscaped)
{
    EXPECT_EQ(StringArrayToQuotedCommaSeparatedString({"say \"hi\""}),
              "\"say \"hi\"\"");
}

TEST(StringArrayToQuotedCommaSeparatedStringTest, EmptyItemStillQuoted)
{
    EXPECT_EQ(StringArrayToQuotedCommaSeparatedString({""}), "\"\"");
    EXPECT_EQ(StringArrayToQuotedCommaSeparatedString({"a", ""}), "\"a\", \"\"");
}

// ---------------------------------------------------------------------------
// EqualFoldASCII
// ---------------------------------------------------------------------------

TEST(EqualFoldASCIITest, Empty)
{
    EXPECT_TRUE(EqualFoldASCII("", ""));
}

TEST(EqualFoldASCIITest, EqualIgnoringCase)
{
    const std::pair<std::string, std::string> cases[] = {
        {"Hello", "hELLo"},
        {"README.md", "readme.MD"},
        {"https", "HTTPS"},
        {"AbC-123", "aBc-123"},
    };
    for (const auto& [a, b] : cases) {
        EXPECT_TRUE(EqualFoldASCII(a, b));
        EXPECT_TRUE(EqualFoldASCII(b, a));
    }
}

TEST(EqualFoldASCIITest, NotEqual)
{
    EXPECT_FALSE(EqualFoldASCII("abc", "abd"));
    EXPECT_FALSE(EqualFoldASCII("abc", "ABC "));
}

TEST(EqualFoldASCIITest, DifferentLengths)
{
    EXPECT_FALSE(EqualFoldASCII("abc", "abcd"));
    EXPECT_FALSE(EqualFoldASCII("abcd", "abc"));
}

TEST(EqualFoldASCIITest, NonAsciiBytesCompareVerbatim)
{
    // High-bit bytes are not folded; they compare as-is.
    EXPECT_TRUE(EqualFoldASCII("\xC3\xA4", "\xC3\xA4"));
    EXPECT_FALSE(EqualFoldASCII("\xC3\xA4", "A"));
}