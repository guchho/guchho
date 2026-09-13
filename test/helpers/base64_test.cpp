#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <initializer_list>
#include <string>
#include <utility>

using guchho::helpers::Base64StdEncode;
using guchho::helpers::Base64URLEncode;
using guchho::helpers::Base32StdEncode;

namespace {
    // Builds a binary string from explicit byte values so tests can pass
    // arbitrary (including zero and high-bit) bytes as input.
    std::string Bytes(std::initializer_list<unsigned char> bytes)
    {
        std::string s;
        for (const unsigned char b : bytes) {
            s.push_back(static_cast<char>(b));
        }
        return s;
    }

    // Builds a binary string holding `count` consecutive byte values
    // starting at `start`. Used to sweep the whole Base64 alphabet.
    std::string ByteRun(unsigned char start, int count)
    {
        std::string s;
        for (int i = 0; i < count; ++i) {
            s.push_back(static_cast<char>(start + static_cast<unsigned char>(i)));
        }
        return s;
    }
}

// ---------------------------------------------------------------------------
// Base64StdEncode
// ---------------------------------------------------------------------------

TEST(Base64StdEncodeTest, Empty)
{
    EXPECT_EQ(Base64StdEncode(""), "");
}

TEST(Base64StdEncodeTest, RFC4648Vectors)
{
    const std::pair<std::string, std::string> cases[] = {
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    };
    for (const auto& [input, expected] : cases) {
        EXPECT_EQ(Base64StdEncode(input), expected);
    }
}

TEST(Base64StdEncodeTest, SingleByte)
{
    EXPECT_EQ(Base64StdEncode("A"), "QQ==");
    EXPECT_EQ(Base64StdEncode(Bytes({0x00})), "AA==");
    EXPECT_EQ(Base64StdEncode(Bytes({0xFF})), "/w==");
}

TEST(Base64StdEncodeTest, TwoBytes)
{
    EXPECT_EQ(Base64StdEncode("AB"), "QUI=");
    EXPECT_EQ(Base64StdEncode(Bytes({0x00, 0x00})), "AAA=");
    EXPECT_EQ(Base64StdEncode(Bytes({0xFF, 0xFF})), "//8=");
    EXPECT_EQ(Base64StdEncode(Bytes({0xFB, 0xFF})), "+/8=");
}

TEST(Base64StdEncodeTest, ThreeBytes)
{
    EXPECT_EQ(Base64StdEncode("ABC"), "QUJD");
    EXPECT_EQ(Base64StdEncode(Bytes({0x00, 0x00, 0x00})), "AAAA");
}

TEST(Base64StdEncodeTest, Hello)
{
    EXPECT_EQ(Base64StdEncode("Hello"), "SGVsbG8=");
}

TEST(Base64StdEncodeTest, CoversFullAlphabet)
{
    // Encoding bytes 0x00..0x2F (48 bytes) yields 64 output characters,
    // one for every position of the standard alphabet.
    EXPECT_EQ(Base64StdEncode(ByteRun(0x00, 48)),
              "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4v");
}

// ---------------------------------------------------------------------------
// Base64URLEncode
// ---------------------------------------------------------------------------

TEST(Base64URLEncodeTest, Empty)
{
    EXPECT_EQ(Base64URLEncode(""), "");
}

TEST(Base64URLEncodeTest, SameAsStandardWithoutSymbols)
{
    EXPECT_EQ(Base64URLEncode("foo"), "Zm9v");
    EXPECT_EQ(Base64URLEncode("Hello"), "SGVsbG8=");
    EXPECT_EQ(Base64URLEncode(Bytes({0x00})), "AA==");
}

TEST(Base64URLEncodeTest, ReplacesPlusAndSlash)
{
    // 0xFB 0xFF encodes to "+/8=" in standard Base64; URL-safe swaps
    // '+' -> '-' and '/' -> '_'.
    EXPECT_EQ(Base64URLEncode(Bytes({0xFB, 0xFF})), "-_8=");
}

TEST(Base64URLEncodeTest, ReplacesSymbolsAcrossChunks)
{
    EXPECT_EQ(Base64URLEncode(ByteRun(0x00, 48)),
              "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4v");
}

// ---------------------------------------------------------------------------
// Base32StdEncode
// ---------------------------------------------------------------------------

TEST(Base32StdEncodeTest, Empty)
{
    EXPECT_EQ(Base32StdEncode(""), "");
}

TEST(Base32StdEncodeTest, RFC4648Vectors)
{
    const std::pair<std::string, std::string> cases[] = {
        {"f", "MY======"},
        {"fo", "MZXQ===="},
        {"foo", "MZXW6==="},
        {"foob", "MZXW6YQ="},
        {"fooba", "MZXW6YTB"},
        {"foobar", "MZXW6YTBOI======"},
    };
    for (const auto& [input, expected] : cases) {
        EXPECT_EQ(Base32StdEncode(input), expected);
    }
}

TEST(Base32StdEncodeTest, Hello)
{
    EXPECT_EQ(Base32StdEncode("Hello"), "JBSWY3DP");
}

TEST(Base32StdEncodeTest, SingleByte)
{
    EXPECT_EQ(Base32StdEncode(Bytes({0x00})), "AA======");
    EXPECT_EQ(Base32StdEncode(Bytes({0xFF})), "74======");
}

TEST(Base32StdEncodeTest, TwoBytes)
{
    EXPECT_EQ(Base32StdEncode(Bytes({0x00, 0x00})), "AAAA====");
}

TEST(Base32StdEncodeTest, ThreeBytes)
{
    EXPECT_EQ(Base32StdEncode(Bytes({0x00, 0x00, 0x00})), "AAAAA===");
}

TEST(Base32StdEncodeTest, FourBytes)
{
    EXPECT_EQ(Base32StdEncode("foob"), "MZXW6YQ=");
}

TEST(Base32StdEncodeTest, FiveBytes)
{
    EXPECT_EQ(Base32StdEncode("fooba"), "MZXW6YTB");
}