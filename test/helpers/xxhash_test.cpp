#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <cstdint>
#include <vector>

using guchho::helpers::Xxh64;

namespace {
    // Builds an Xxh64 hasher, writes the data in one shot, and returns Sum64().
    uint64_t HashOneShot(const void* data, size_t len)
    {
        Xxh64 h;
        h.Write(data, len);
        return h.Sum64();
    }
}

// ---------------------------------------------------------------------------
// Xxh64Test
// ---------------------------------------------------------------------------

TEST(Xxh64Test, EmptyHash)
{
    Xxh64 h;
    EXPECT_EQ(h.Sum64(), UINT64_C(0xEF46DB3751D8E999));
}

TEST(Xxh64Test, FreshInstanceMatchesReset)
{
    Xxh64 a;
    a.Reset();
    Xxh64 b;
    EXPECT_EQ(a.Sum64(), b.Sum64());
}

TEST(Xxh64Test, KnownVectors)
{
    const std::pair<std::string, uint64_t> cases[] = {
        {"a",    UINT64_C(0xD24EC4F1A98C6E5B)},
        {"abc",  UINT64_C(0x44BC2CF5AD770999)},
        {"Hello",UINT64_C(0xA75A91375B27D44)},  // from reference
        {"foobar",UINT64_C(0xA2AA05ED9085AAF9)},
        {"foobarbaz", UINT64_C(0x5A47C5542D40801B)},
    };
    for (const auto& [input, expected] : cases) {
        EXPECT_EQ(HashOneShot(input.data(), input.size()), expected);
    }
}

TEST(Xxh64Test, NullByte)
{
    std::vector<uint8_t> buf(1, 0);
    EXPECT_EQ(HashOneShot(buf.data(), buf.size()), UINT64_C(0xE934A84ADB052768));
}

TEST(Xxh64Test, BlockBoundaries)
{
    const std::pair<size_t, uint64_t> cases[] = {
        {31, UINT64_C(0xFAF43DD52DEB083A)},
        {32, UINT64_C(0xF6E9BE5D70632CF5)},
        {33, UINT64_C(0x1DCDF75A2320FB61)},
        {63, UINT64_C(0xD81772F2C42D7324)},
        {64, UINT64_C(0x257B09A147B82A19)},
        {65, UINT64_C(0xD033CD270447F937)},
        {100,UINT64_C(0x17BB1103C92C502F)},
    };
    for (const auto& [len, expected] : cases) {
        std::vector<uint8_t> zeros(len, 0);
        EXPECT_EQ(HashOneShot(zeros.data(), zeros.size()), expected);
    }
}

TEST(Xxh64Test, BytePattern)
{
    std::vector<uint8_t> buf;
    for (int i = 0; i < 48; ++i) {
        buf.push_back(static_cast<uint8_t>(i));
    }
    EXPECT_EQ(HashOneShot(buf.data(), buf.size()), UINT64_C(0x8FE437632DA06964));
}

TEST(Xxh64Test, RepeatedByte)
{
    std::vector<uint8_t> buf(100, 0xAB);
    EXPECT_EQ(HashOneShot(buf.data(), buf.size()), UINT64_C(0x55FE38B77683B147));
}

TEST(Xxh64Test, LargeInput)
{
    std::vector<uint8_t> buf(128, 0);
    EXPECT_EQ(HashOneShot(buf.data(), buf.size()), UINT64_C(0x6F975641F69E7C17));
}

TEST(Xxh64Test, Sum64IsNonDestructive)
{
    Xxh64 h;
    h.Write("abc", 3);
    uint64_t first  = h.Sum64();
    uint64_t second = h.Sum64();
    EXPECT_EQ(first, second);
}

TEST(Xxh64Test, ResetClearsState)
{
    Xxh64 h;
    h.Write("abc", 3);
    uint64_t before = h.Sum64();

    h.Reset();
    uint64_t after = h.Sum64();

    EXPECT_NE(before, after);
    EXPECT_EQ(after, UINT64_C(0xEF46DB3751D8E999));
}

TEST(Xxh64Test, ResetAndReplayProducesSameHash)
{
    std::string data = "Hello, xxHash!";

    Xxh64 a;
    a.Write(data.data(), data.size());
    uint64_t expected = a.Sum64();

    Xxh64 b;
    b.Write(data.data(), 4);
    b.Reset();
    b.Write(data.data(), data.size());
    EXPECT_EQ(b.Sum64(), expected);
}

TEST(Xxh64Test, ChunkedWritesMatchSingleWrite)
{
    std::vector<uint8_t> data;
    for (int i = 0; i < 100; ++i) {
        data.push_back(static_cast<uint8_t>(i * 37 + 11));
    }

    uint64_t single = HashOneShot(data.data(), data.size());

    Xxh64 h;
    for (size_t i = 0; i < data.size(); i += 7) {
        size_t chunk = std::min<size_t>(7, data.size() - i);
        h.Write(data.data() + i, chunk);
    }
    EXPECT_EQ(h.Sum64(), single);
}

TEST(Xxh64Test, SingleByteAtATime)
{
    std::vector<uint8_t> data(100, 0xAB);
    uint64_t bulk = HashOneShot(data.data(), data.size());

    Xxh64 h;
    for (size_t i = 0; i < data.size(); ++i) {
        h.Write(data.data() + i, 1);
    }
    EXPECT_EQ(h.Sum64(), bulk);
}

TEST(Xxh64Test, BisectionAtBlockBoundary)
{
    std::string input = "The quick brown fox jumps over the lazy dog";
    uint64_t single = HashOneShot(input.data(), input.size());

    Xxh64 h;
    h.Write(input.data(), 32);
    h.Write(input.data() + 32, input.size() - 32);
    EXPECT_EQ(h.Sum64(), single);
}

TEST(Xxh64Test, IncrementalWrite)
{
    Xxh64 h;
    h.Write("foo", 3);
    uint64_t sum1 = h.Sum64();
    h.Write("bar", 3);
    uint64_t sum2 = h.Sum64();

    uint64_t together = HashOneShot("foobar", 6);
    EXPECT_EQ(sum2, together);
    EXPECT_NE(sum1, sum2);
}

TEST(Xxh64Test, BisectionAtOddOffsets)
{
    std::string input(200, 'x');
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<char>(i & 0xFF);
    }
    uint64_t single = HashOneShot(input.data(), input.size());

    Xxh64 h;
    h.Write(input.data(), 17);
    h.Write(input.data() + 17, input.size() - 17);
    EXPECT_EQ(h.Sum64(), single);
}