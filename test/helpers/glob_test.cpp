#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <string_view>
#include <vector>

using guchho::helpers::GlobPart;
using guchho::helpers::GlobPatternToString;
using guchho::helpers::GlobWildcard;
using guchho::helpers::ParseGlobPattern;

// ---------------------------------------------------------------------------
// ParseGlobPattern
// ---------------------------------------------------------------------------

TEST(ParseGlobPatternTest, EmptyString)
{
    auto parts = ParseGlobPattern("");
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].prefix, "");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, LiteralOnly)
{
    auto parts = ParseGlobPattern("hello");
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].prefix, "hello");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, SingleStarMiddle)
{
    auto parts = ParseGlobPattern("src/*.ts");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].prefix, "src/");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[1].prefix, ".ts");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, DoubleStarMiddle)
{
    auto parts = ParseGlobPattern("src/**/*.ts");
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0].prefix, "src/");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllIncludingSlash);
    EXPECT_EQ(parts[1].prefix, "/");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[2].prefix, ".ts");
    EXPECT_EQ(parts[2].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, StarAtStart)
{
    auto parts = ParseGlobPattern("*.ts");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].prefix, "");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[1].prefix, ".ts");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, StarAtEnd)
{
    auto parts = ParseGlobPattern("src/*");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].prefix, "src/");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[1].prefix, "");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, DoubleStarAtStart)
{
    auto parts = ParseGlobPattern("**/*.ts");
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0].prefix, "");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllIncludingSlash);
    EXPECT_EQ(parts[1].prefix, "/");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[2].prefix, ".ts");
    EXPECT_EQ(parts[2].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, DoubleStarAtEnd)
{
    auto parts = ParseGlobPattern("src/**");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].prefix, "src/");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllIncludingSlash);
    EXPECT_EQ(parts[1].prefix, "");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, TripleStarDegradesToDouble)
{
    auto parts = ParseGlobPattern("src/***.ts");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].prefix, "src/");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[1].prefix, ".ts");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, MultipleStarsNotFlankedDegradeToSingle)
{
    auto parts = ParseGlobPattern("a**b*.c");
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0].prefix, "a");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[1].prefix, "b");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[2].prefix, ".c");
    EXPECT_EQ(parts[2].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, BackslashFlankedDoubleStar)
{
    auto parts = ParseGlobPattern("src\\**\\*.ts");
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0].prefix, "src\\");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllIncludingSlash);
    EXPECT_EQ(parts[1].prefix, "\\");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[2].prefix, ".ts");
    EXPECT_EQ(parts[2].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, ConsecutiveSeparators)
{
    auto parts = ParseGlobPattern("a//**/*.ts");
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0].prefix, "a//");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllIncludingSlash);
    EXPECT_EQ(parts[1].prefix, "/");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[2].prefix, ".ts");
    EXPECT_EQ(parts[2].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, OnlyStars)
{
    auto parts = ParseGlobPattern("**");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].prefix, "");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllIncludingSlash);
    EXPECT_EQ(parts[1].prefix, "");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, SingleStarOnly)
{
    auto parts = ParseGlobPattern("*");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].prefix, "");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[1].prefix, "");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, ComplexPattern)
{
    auto parts = ParseGlobPattern("a/**/b/*/*.c");
    ASSERT_EQ(parts.size(), 4u);
    EXPECT_EQ(parts[0].prefix, "a/");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kAllIncludingSlash);
    EXPECT_EQ(parts[1].prefix, "/b/");
    EXPECT_EQ(parts[1].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[2].prefix, "/");
    EXPECT_EQ(parts[2].wildcard, GlobWildcard::kAllExceptSlash);
    EXPECT_EQ(parts[3].prefix, ".c");
    EXPECT_EQ(parts[3].wildcard, GlobWildcard::kNone);
}

TEST(ParseGlobPatternTest, NoWildcardMultipleSegments)
{
    auto parts = ParseGlobPattern("a/b/c");
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].prefix, "a/b/c");
    EXPECT_EQ(parts[0].wildcard, GlobWildcard::kNone);
}

// ---------------------------------------------------------------------------
// GlobPatternToString
// ---------------------------------------------------------------------------

TEST(GlobPatternToStringTest, EmptyPattern)
{
    std::vector<GlobPart> pattern = {{"", GlobWildcard::kNone}};
    EXPECT_EQ(GlobPatternToString(pattern), "");
}

TEST(GlobPatternToStringTest, LiteralOnly)
{
    std::vector<GlobPart> pattern = {{"hello", GlobWildcard::kNone}};
    EXPECT_EQ(GlobPatternToString(pattern), "hello");
}

TEST(GlobPatternToStringTest, SingleStar)
{
    std::vector<GlobPart> pattern = {
        {"src/", GlobWildcard::kAllExceptSlash},
        {".ts", GlobWildcard::kNone},
    };
    EXPECT_EQ(GlobPatternToString(pattern), "src/*.ts");
}

TEST(GlobPatternToStringTest, DoubleStar)
{
    std::vector<GlobPart> pattern = {
        {"src/", GlobWildcard::kAllIncludingSlash},
        {"/", GlobWildcard::kAllExceptSlash},
        {".ts", GlobWildcard::kNone},
    };
    EXPECT_EQ(GlobPatternToString(pattern), "src/**/*.ts");
}

TEST(GlobPatternToStringTest, StarAtStart)
{
    std::vector<GlobPart> pattern = {
        {"", GlobWildcard::kAllExceptSlash},
        {".ts", GlobWildcard::kNone},
    };
    EXPECT_EQ(GlobPatternToString(pattern), "*.ts");
}

TEST(GlobPatternToStringTest, DoubleStarAtStart)
{
    std::vector<GlobPart> pattern = {
        {"", GlobWildcard::kAllIncludingSlash},
        {"/", GlobWildcard::kAllExceptSlash},
        {".ts", GlobWildcard::kNone},
    };
    EXPECT_EQ(GlobPatternToString(pattern), "**/*.ts");
}

// ---------------------------------------------------------------------------
// Roundtrip
// ---------------------------------------------------------------------------

TEST(GlobRoundtripTest, SimplePattern)
{
    std::string input = "src/**/*.ts";
    auto parts = ParseGlobPattern(input);
    EXPECT_EQ(GlobPatternToString(parts), input);
}

TEST(GlobRoundtripTest, SingleStar)
{
    std::string input = "docs/*";
    auto parts = ParseGlobPattern(input);
    EXPECT_EQ(GlobPatternToString(parts), input);
}

TEST(GlobRoundtripTest, LiteralOnly)
{
    std::string input = "README.md";
    auto parts = ParseGlobPattern(input);
    EXPECT_EQ(GlobPatternToString(parts), input);
}

TEST(GlobRoundtripTest, StarAtStart)
{
    std::string input = "*.js";
    auto parts = ParseGlobPattern(input);
    EXPECT_EQ(GlobPatternToString(parts), input);
}

TEST(GlobRoundtripTest, DoubleStarAtStart)
{
    std::string input = "**/*.json";
    auto parts = ParseGlobPattern(input);
    EXPECT_EQ(GlobPatternToString(parts), input);
}

TEST(GlobRoundtripTest, TripleStarNormalized)
{
    std::string input = "src/***.ts";
    auto parts = ParseGlobPattern(input);
    EXPECT_EQ(GlobPatternToString(parts), "src/*.ts");
}

TEST(GlobRoundtripTest, ComplexPattern)
{
    std::string input = "a/**/b/*/*.c";
    auto parts = ParseGlobPattern(input);
    EXPECT_EQ(GlobPatternToString(parts), input);
}
