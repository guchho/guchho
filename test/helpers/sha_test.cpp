#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using guchho::helpers::Sha256;
using guchho::helpers::Sha384;
using guchho::helpers::Sha512;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<uint8_t> HexToBytes(std::string_view hex)
{
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        uint8_t hi = 0, lo = 0;
        char hc = hex[i];
        char lc = hex[i + 1];
        if (hc >= '0' && hc <= '9') hi = static_cast<uint8_t>(hc - '0');
        else if (hc >= 'a' && hc <= 'f') hi = static_cast<uint8_t>(hc - 'a' + 10);
        else if (hc >= 'A' && hc <= 'F') hi = static_cast<uint8_t>(hc - 'A' + 10);
        if (lc >= '0' && lc <= '9') lo = static_cast<uint8_t>(lc - '0');
        else if (lc >= 'a' && lc <= 'f') lo = static_cast<uint8_t>(lc - 'a' + 10);
        else if (lc >= 'A' && lc <= 'F') lo = static_cast<uint8_t>(lc - 'A' + 10);
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// SHA-256 — FIPS 180-4 appendix B test vectors
// ---------------------------------------------------------------------------

TEST(Sha256Test, EmptyString)
{
    auto digest = Sha256("");
    ASSERT_EQ(digest.size(), 32u);
    auto expected = HexToBytes("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(digest, expected);
}

TEST(Sha256Test, ABC)
{
    auto digest = Sha256("abc");
    ASSERT_EQ(digest.size(), 32u);
    auto expected = HexToBytes("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(digest, expected);
}

TEST(Sha256Test, ABCDEFGHIJ)
{
    auto digest = Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    ASSERT_EQ(digest.size(), 32u);
    auto expected = HexToBytes("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(digest, expected);
}

TEST(Sha256Test, OneMillionA)
{
    // 1,000,000 repetitions of 'a' — FIPS 180-4 long-message test.
    std::string data(1000000, 'a');
    auto digest = Sha256(data);
    ASSERT_EQ(digest.size(), 32u);
    auto expected = HexToBytes("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    EXPECT_EQ(digest, expected);
}

TEST(Sha256Test, SameInputSameDigest)
{
    auto a = Sha256("test");
    auto b = Sha256("test");
    EXPECT_EQ(a, b);
}

TEST(Sha256Test, DifferentInputsDifferentDigests)
{
    auto a = Sha256("hello");
    auto b = Sha256("world");
    EXPECT_NE(a, b);
}

// ---------------------------------------------------------------------------
// SHA-384
// ---------------------------------------------------------------------------

TEST(Sha384Test, EmptyString)
{
    auto digest = Sha384("");
    ASSERT_EQ(digest.size(), 48u);
    auto expected = HexToBytes("38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b");
    EXPECT_EQ(digest, expected);
}

TEST(Sha384Test, ABC)
{
    auto digest = Sha384("abc");
    ASSERT_EQ(digest.size(), 48u);
    auto expected = HexToBytes("cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
    EXPECT_EQ(digest, expected);
}

TEST(Sha384Test, SameInputSameDigest)
{
    auto a = Sha384("test");
    auto b = Sha384("test");
    EXPECT_EQ(a, b);
}

TEST(Sha384Test, DifferentInputsDifferentDigests)
{
    auto a = Sha384("hello");
    auto b = Sha384("world");
    EXPECT_NE(a, b);
}

// ---------------------------------------------------------------------------
// SHA-512
// ---------------------------------------------------------------------------

TEST(Sha512Test, EmptyString)
{
    auto digest = Sha512("");
    ASSERT_EQ(digest.size(), 64u);
    auto expected = HexToBytes("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    EXPECT_EQ(digest, expected);
}

TEST(Sha512Test, ABC)
{
    auto digest = Sha512("abc");
    ASSERT_EQ(digest.size(), 64u);
    auto expected = HexToBytes("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    EXPECT_EQ(digest, expected);
}

TEST(Sha512Test, SameInputSameDigest)
{
    auto a = Sha512("test");
    auto b = Sha512("test");
    EXPECT_EQ(a, b);
}

TEST(Sha512Test, DifferentInputsDifferentDigests)
{
    auto a = Sha512("hello");
    auto b = Sha512("world");
    EXPECT_NE(a, b);
}

// ---------------------------------------------------------------------------
// Cross-algorithm — same input produces different-length digests
// ---------------------------------------------------------------------------

TEST(ShaCrossTest, DifferentLengths)
{
    auto d256 = Sha256("test");
    auto d384 = Sha384("test");
    auto d512 = Sha512("test");
    EXPECT_EQ(d256.size(), 32u);
    EXPECT_EQ(d384.size(), 48u);
    EXPECT_EQ(d512.size(), 64u);
    EXPECT_NE(d256, std::vector<uint8_t>(d384.begin(), d384.begin() + 32));
    EXPECT_NE(d256, std::vector<uint8_t>(d512.begin(), d512.begin() + 32));
}
