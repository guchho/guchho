#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <optional>
#include <string>
#include <vector>

using guchho::helpers::TypoDetector;

// ---------------------------------------------------------------------------
// Empty input
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, EmptyValidList)
{
    TypoDetector d({});
    EXPECT_FALSE(d.MaybeCorrectTypo("anything").has_value());
}

// ---------------------------------------------------------------------------
// Single-char-missing typos
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, SingleCharMissing)
{
    TypoDetector d({"bundle"});
    EXPECT_EQ(d.MaybeCorrectTypo("bundl"), std::string("bundle"));
    EXPECT_EQ(d.MaybeCorrectTypo("bunde"), std::string("bundle"));
    EXPECT_EQ(d.MaybeCorrectTypo("bndle"), std::string("bundle"));
}

TEST(TypoDetectorTest, MultipleValidWords)
{
    TypoDetector d({"bundle", "barrel"});
    EXPECT_EQ(d.MaybeCorrectTypo("bundl"), std::string("bundle"));
    EXPECT_EQ(d.MaybeCorrectTypo("barrl"), std::string("barrel"));
}

// ---------------------------------------------------------------------------
// Extra character (single-char insertion / doubling)
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, ExtraCharacter)
{
    TypoDetector d({"hello"});
    // Doubling the first letter: "hhllo" — deleting either 'h' yields "hllo"
    // which is in the index.
    EXPECT_EQ(d.MaybeCorrectTypo("hhllo"), std::string("hello"));
}

// ---------------------------------------------------------------------------
// Correct word returns itself
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, CorrectWordReturnsItself)
{
    TypoDetector d({"bundle"});
    // Deleting 'b' from "bundle" yields "undle", which is indexed to
    // "bundle" — so the exact-match probe finds it.
    EXPECT_EQ(d.MaybeCorrectTypo("bundle"), std::string("bundle"));
}

// ---------------------------------------------------------------------------
// Words too short to index
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, WordsTooShortSkipped)
{
    TypoDetector d({"cat", "ab", "a"});
    EXPECT_FALSE(d.MaybeCorrectTypo("ca").has_value());
    EXPECT_FALSE(d.MaybeCorrectTypo("c").has_value());
    EXPECT_FALSE(d.MaybeCorrectTypo("ab").has_value());
}

TEST(TypoDetectorTest, ShortAndLongWordsBothPresent)
{
    // "cat" (3 bytes) is skipped; "cats" (4 bytes) is indexed.
    TypoDetector d({"cat", "cats"});
    EXPECT_FALSE(d.MaybeCorrectTypo("ca").has_value());
    EXPECT_EQ(d.MaybeCorrectTypo("cat"), std::string("cats"));
}

// ---------------------------------------------------------------------------
// No plausible correction
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, NoMatch)
{
    TypoDetector d({"hello"});
    EXPECT_FALSE(d.MaybeCorrectTypo("qwerty").has_value());
    EXPECT_FALSE(d.MaybeCorrectTypo("h").has_value());
    EXPECT_FALSE(d.MaybeCorrectTypo("helloo").has_value());
}

// ---------------------------------------------------------------------------
// Multi-byte UTF-8 characters
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, MultiByteCharDeleted)
{
    // "café" in UTF-8 = { 'c', 'a', 'f', 0xC3, 0xA9 } — 5 bytes.
    // Deleting 'é' (2 bytes) yields "caf" which maps back to "café".
    TypoDetector d({"\x63\x61\x66\xC3\xA9"});
    EXPECT_EQ(d.MaybeCorrectTypo("caf"), std::string("\x63\x61\x66\xC3\xA9"));
}

TEST(TypoDetectorTest, MultiByteCharInsertion)
{
    // "café" — probe "café" by deleting its first char ('c') yields "afé"
    // which is indexed, so the correct word is returned.
    TypoDetector d({"\x63\x61\x66\xC3\xA9"});
    EXPECT_EQ(d.MaybeCorrectTypo("\x63\x61\x66\xC3\xA9"),
              std::string("\x63\x61\x66\xC3\xA9"));
}

// ---------------------------------------------------------------------------
// Exact-match lookup (key is itself a one-char-deletion of a valid word)
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, ExactMatchInMap)
{
    TypoDetector d({"unrecognized"});
    EXPECT_EQ(d.MaybeCorrectTypo("unrecognize"), std::string("unrecognized"));
    EXPECT_EQ(d.MaybeCorrectTypo("unrecognized"), std::string("unrecognized"));
}

// ---------------------------------------------------------------------------
// Duplicate valid words
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, DuplicateWords)
{
    TypoDetector d({"hello", "hello"});
    EXPECT_EQ(d.MaybeCorrectTypo("hllo"), std::string("hello"));
}

// ---------------------------------------------------------------------------
// Deletion key collision (last word wins)
// ---------------------------------------------------------------------------

TEST(TypoDetectorTest, DeletionKeyCollisionLastWins)
{
    // "hello" maps "ello" -> "hello"; "jello" maps "ello" -> "jello" (last).
    TypoDetector d({"hello", "jello"});
    EXPECT_EQ(d.MaybeCorrectTypo("ello"), std::string("jello"));
    // "hllo" maps only for "hello".
    EXPECT_EQ(d.MaybeCorrectTypo("hllo"), std::string("hello"));
    // "jllo" maps only for "jello".
    EXPECT_EQ(d.MaybeCorrectTypo("jllo"), std::string("jello"));
}