#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <cstdint>
#include <string>
#include <string_view>

using guchho::helpers::HashCombine;
using guchho::helpers::HashCombineString;

// ---------------------------------------------------------------------------
// HashCombine
// ---------------------------------------------------------------------------

TEST(HashCombineTest, ZeroSeedZeroHash)
{
    uint32_t result = HashCombine(0, 0);
    EXPECT_NE(result, 0u);
}

TEST(HashCombineTest, ZeroSeedNonZeroHash)
{
    uint32_t a = HashCombine(0, 1);
    uint32_t b = HashCombine(0, 2);
    EXPECT_NE(a, b);
}

TEST(HashCombineTest, NonZeroSeedZeroHash)
{
    uint32_t a = HashCombine(1, 0);
    uint32_t b = HashCombine(2, 0);
    EXPECT_NE(a, b);
}

TEST(HashCombineTest, OrderDependent)
{
    uint32_t ab = HashCombine(10, 20);
    uint32_t ba = HashCombine(20, 10);
    EXPECT_NE(ab, ba);
}

TEST(HashCombineTest, SameInputsSameOutput)
{
    uint32_t a = HashCombine(5, 42);
    uint32_t b = HashCombine(5, 42);
    EXPECT_EQ(a, b);
}

TEST(HashCombineTest, ChainedCombines)
{
    uint32_t h = 0;
    h = HashCombine(h, 1);
    h = HashCombine(h, 2);
    h = HashCombine(h, 3);
    EXPECT_NE(h, 0u);

    uint32_t h2 = 0;
    h2 = HashCombine(h2, 1);
    h2 = HashCombine(h2, 2);
    h2 = HashCombine(h2, 3);
    EXPECT_EQ(h, h2);
}

TEST(HashCombineTest, GoldenRatioConstant)
{
    uint32_t result = HashCombine(0, 0);
    EXPECT_EQ(result, 0x9e3779b9u);
}

// ---------------------------------------------------------------------------
// HashCombineString
// ---------------------------------------------------------------------------

TEST(HashCombineStringTest, EmptyString)
{
    uint32_t h = HashCombineString(0, "");
    EXPECT_NE(h, 0u);
}

TEST(HashCombineStringTest, SingleChar)
{
    uint32_t h = HashCombineString(0, "a");
    EXPECT_NE(h, 0u);
}

TEST(HashCombineStringTest, SameStringSameHash)
{
    uint32_t a = HashCombineString(0, "hello");
    uint32_t b = HashCombineString(0, "hello");
    EXPECT_EQ(a, b);
}

TEST(HashCombineStringTest, DifferentStringsDifferentHash)
{
    uint32_t a = HashCombineString(0, "hello");
    uint32_t b = HashCombineString(0, "world");
    EXPECT_NE(a, b);
}

TEST(HashCombineStringTest, CaseSensitive)
{
    uint32_t lower = HashCombineString(0, "hello");
    uint32_t upper = HashCombineString(0, "Hello");
    EXPECT_NE(lower, upper);
}

TEST(HashCombineStringTest, LengthMatters)
{
    uint32_t ab = HashCombineString(0, "ab");
    uint32_t abc = HashCombineString(0, "abc");
    EXPECT_NE(ab, abc);
}

TEST(HashCombineStringTest, PrefixSameButDifferentLength)
{
    uint32_t prefix = HashCombineString(0, "foo");
    uint32_t full = HashCombineString(0, "foobar");
    EXPECT_NE(prefix, full);
}

TEST(HashCombineStringTest, SeedAffectsResult)
{
    uint32_t noSeed = HashCombineString(0, "test");
    uint32_t withSeed = HashCombineString(42, "test");
    EXPECT_NE(noSeed, withSeed);
}

TEST(HashCombineStringTest, SameSeedSameResult)
{
    uint32_t a = HashCombineString(99, "test");
    uint32_t b = HashCombineString(99, "test");
    EXPECT_EQ(a, b);
}

TEST(HashCombineStringTest, UnicodeCodePoints)
{
    // "é" (U+00E9) encoded as UTF-8: 0xC3 0xA9
    char e1[] = {'\xC3', '\xA9'};
    uint32_t h1 = HashCombineString(0, std::string_view(e1, sizeof(e1)));
    // "è" (U+00E8) encoded as UTF-8: 0xC3 0xA8
    char e2[] = {'\xC3', '\xA8'};
    uint32_t h2 = HashCombineString(0, std::string_view(e2, sizeof(e2)));
    EXPECT_NE(h1, h2);
}

TEST(HashCombineStringTest, MultiByteSequence)
{
    // Chinese character "中" (U+4E2D) encoded as UTF-8: 0xE4 0xB8 0xAD
    char data[] = {'\xE4', '\xB8', '\xAD'};
    uint32_t h = HashCombineString(0, std::string_view(data, sizeof(data)));
    EXPECT_NE(h, 0u);
}

TEST(HashCombineStringTest, Emoji)
{
    // "😀" (U+1F600) encoded as UTF-8: 0xF0 0x9F 0x98 0x80
    char data[] = {'\xF0', '\x9F', '\x98', '\x80'};
    uint32_t h = HashCombineString(0, std::string_view(data, sizeof(data)));
    EXPECT_NE(h, 0u);
}

TEST(HashCombineStringTest, MixedAsciiAndUnicode)
{
    uint32_t a = HashCombineString(0, "hello");
    uint32_t b = HashCombineString(0, "héllo");
    EXPECT_NE(a, b);
}

TEST(HashCombineStringTest, InvalidUTF8StopsEarly)
{
    // 0xFF is invalid UTF-8 start byte; processing should stop
    char data[] = {'a', 'b', 'c', '\xFF', 'd', 'e', 'f'};
    uint32_t h = HashCombineString(0, std::string_view(data, sizeof(data)));
    EXPECT_NE(h, 0u);
}

TEST(HashCombineStringTest, TruncatedSequence)
{
    // 0xC3 starts a 2-byte sequence but is followed by invalid continuation
    char data[] = {'\xC3'};
    uint32_t h = HashCombineString(0, std::string_view(data, sizeof(data)));
    EXPECT_NE(h, 0u);
}

TEST(HashCombineStringTest, AllASCII)
{
    std::string allAscii;
    for (int i = 32; i < 127; i++) {
        allAscii += static_cast<char>(i);
    }
    uint32_t h = HashCombineString(0, allAscii);
    EXPECT_NE(h, 0u);
}

TEST(HashCombineStringTest, NullByte)
{
    uint32_t a = HashCombineString(0, "abc");
    char data[] = {'a', 'b', '\0', 'c'};
    uint32_t b = HashCombineString(0, std::string_view(data, sizeof(data)));
    EXPECT_NE(a, b);
}
