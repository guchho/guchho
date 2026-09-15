#include "test/guchho_test.hpp"
#include "guchho/miniz.hpp"

#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// mz_version
// ---------------------------------------------------------------------------

TEST(MzVersionTest, ReturnsNonNull)
{
    const char* ver = mz_version();
    ASSERT_TRUE(ver != nullptr);
    EXPECT_TRUE(strlen(ver) > 0);
}

TEST(MzVersionTest, ContainsDots)
{
    const char* ver = mz_version();
    ASSERT_TRUE(ver != nullptr);
    EXPECT_TRUE(strchr(ver, '.') != nullptr);
}

// ---------------------------------------------------------------------------
// mz_adler32
// ---------------------------------------------------------------------------

TEST(MzAdler32Test, InitialValue)
{
    mz_ulong adler = mz_adler32(0, nullptr, 0);
    EXPECT_EQ(adler, 1u);
}

TEST(MzAdler32Test, EmptyDataReturns1)
{
    unsigned char data[] = {0};
    mz_ulong adler = mz_adler32(MZ_ADLER32_INIT, data, 0);
    EXPECT_EQ(adler, 1u);
}

TEST(MzAdler32Test, KnownValue)
{
    const unsigned char data[] = {'W', 'o', 'r', 'l', 'd'};
    mz_ulong adler = mz_adler32(MZ_ADLER32_INIT, data, 5);
    EXPECT_NE(adler, 0u);
}

TEST(MzAdler32Test, DifferentDataDifferentChecksum)
{
    const unsigned char data1[] = {'a', 'b', 'c'};
    const unsigned char data2[] = {'x', 'y', 'z'};
    mz_ulong a1 = mz_adler32(MZ_ADLER32_INIT, data1, 3);
    mz_ulong a2 = mz_adler32(MZ_ADLER32_INIT, data2, 3);
    EXPECT_NE(a1, a2);
}

// ---------------------------------------------------------------------------
// mz_crc32
// ---------------------------------------------------------------------------

TEST(MzCrc32Test, InitialValue)
{
    mz_ulong crc = mz_crc32(0, nullptr, 0);
    EXPECT_EQ(crc, 0u);
}

TEST(MzCrc32Test, EmptyDataReturns0)
{
    unsigned char data[] = {0};
    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, data, 0);
    EXPECT_EQ(crc, 0u);
}

TEST(MzCrc32Test, KnownValue)
{
    const unsigned char data[] = {'H', 'e', 'l', 'l', 'o'};
    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, data, 5);
    EXPECT_NE(crc, 0u);
}

TEST(MzCrc32Test, DifferentDataDifferentChecksum)
{
    const unsigned char data1[] = {'a', 'b', 'c'};
    const unsigned char data2[] = {'x', 'y', 'z'};
    mz_ulong c1 = mz_crc32(MZ_CRC32_INIT, data1, 3);
    mz_ulong c2 = mz_crc32(MZ_CRC32_INIT, data2, 3);
    EXPECT_NE(c1, c2);
}

// ---------------------------------------------------------------------------
// mz_compressBound / mz_deflateBound
// ---------------------------------------------------------------------------

TEST(MzCompressBoundTest, NonZeroForSmallInput)
{
    mz_ulong bound = mz_compressBound(100);
    EXPECT_GT(bound, 0u);
}

TEST(MzCompressBoundTest, LargerThanInput)
{
    mz_ulong bound = mz_compressBound(100);
    EXPECT_GE(bound, 100u);
}

TEST(MzCompressBoundTest, ZeroInput)
{
    mz_ulong bound = mz_compressBound(0);
    EXPECT_GE(bound, 0u);
}

// ---------------------------------------------------------------------------
// mz_compress / mz_uncompress roundtrip
// ---------------------------------------------------------------------------

TEST(MzCompressUncompressTest, Roundtrip)
{
    const char* original = "Hello, World! This is a test of miniz compression.";
    mz_ulong src_len = static_cast<mz_ulong>(strlen(original));
    mz_ulong bound = mz_compressBound(src_len);

    std::vector<unsigned char> compressed(bound);
    mz_ulong comp_len = bound;
    int ret = mz_compress(compressed.data(), &comp_len,
                          reinterpret_cast<const unsigned char*>(original), src_len);
    ASSERT_EQ(ret, MZ_OK);

    std::vector<unsigned char> decompressed(src_len);
    mz_ulong decomp_len = src_len;
    ret = mz_uncompress(decompressed.data(), &decomp_len,
                        compressed.data(), comp_len);
    ASSERT_EQ(ret, MZ_OK);
    EXPECT_EQ(decomp_len, src_len);
    EXPECT_EQ(std::memcmp(decompressed.data(), original, src_len), 0);
}

TEST(MzCompressUncompressTest, EmptyString)
{
    const char* original = "";
    mz_ulong src_len = 0;
    mz_ulong bound = mz_compressBound(src_len);

    std::vector<unsigned char> compressed(bound);
    mz_ulong comp_len = bound;
    int ret = mz_compress(compressed.data(), &comp_len,
                          reinterpret_cast<const unsigned char*>(original), src_len);
    ASSERT_EQ(ret, MZ_OK);

    std::vector<unsigned char> decompressed(1);
    mz_ulong decomp_len = 1;
    ret = mz_uncompress(decompressed.data(), &decomp_len,
                        compressed.data(), comp_len);
    ASSERT_EQ(ret, MZ_OK);
    EXPECT_EQ(decomp_len, 0u);
}

TEST(MzCompressUncompressTest, LargeData)
{
    std::string original(10000, 'A');
    mz_ulong src_len = static_cast<mz_ulong>(original.size());
    mz_ulong bound = mz_compressBound(src_len);

    std::vector<unsigned char> compressed(bound);
    mz_ulong comp_len = bound;
    int ret = mz_compress(compressed.data(), &comp_len,
                          reinterpret_cast<const unsigned char*>(original.data()), src_len);
    ASSERT_EQ(ret, MZ_OK);

    std::vector<unsigned char> decompressed(src_len);
    mz_ulong decomp_len = src_len;
    ret = mz_uncompress(decompressed.data(), &decomp_len,
                        compressed.data(), comp_len);
    ASSERT_EQ(ret, MZ_OK);
    EXPECT_EQ(decomp_len, src_len);
    EXPECT_EQ(std::memcmp(decompressed.data(), original.data(), src_len), 0);
}

TEST(MzCompressUncompressTest, RepeatedDataCompressesWell)
{
    std::string original(1000, 'X');
    mz_ulong src_len = static_cast<mz_ulong>(original.size());
    mz_ulong bound = mz_compressBound(src_len);

    std::vector<unsigned char> compressed(bound);
    mz_ulong comp_len = bound;
    int ret = mz_compress(compressed.data(), &comp_len,
                          reinterpret_cast<const unsigned char*>(original.data()), src_len);
    ASSERT_EQ(ret, MZ_OK);
    EXPECT_LT(comp_len, src_len);
}

// ---------------------------------------------------------------------------
// mz_compress2 with different levels
// ---------------------------------------------------------------------------

TEST(MzCompress2Test, Level0NoCompression)
{
    const char* original = "test data for compression levels";
    mz_ulong src_len = static_cast<mz_ulong>(strlen(original));
    mz_ulong bound = mz_compressBound(src_len);

    std::vector<unsigned char> compressed(bound);
    mz_ulong comp_len = bound;
    int ret = mz_compress2(compressed.data(), &comp_len,
                           reinterpret_cast<const unsigned char*>(original), src_len,
                           MZ_NO_COMPRESSION);
    ASSERT_EQ(ret, MZ_OK);

    std::vector<unsigned char> decompressed(src_len);
    mz_ulong decomp_len = src_len;
    ret = mz_uncompress(decompressed.data(), &decomp_len,
                        compressed.data(), comp_len);
    ASSERT_EQ(ret, MZ_OK);
    EXPECT_EQ(decomp_len, src_len);
    EXPECT_EQ(std::memcmp(decompressed.data(), original, src_len), 0);
}

TEST(MzCompress2Test, Level9BestCompression)
{
    const char* original = "test data for best compression level";
    mz_ulong src_len = static_cast<mz_ulong>(strlen(original));
    mz_ulong bound = mz_compressBound(src_len);

    std::vector<unsigned char> compressed(bound);
    mz_ulong comp_len = bound;
    int ret = mz_compress2(compressed.data(), &comp_len,
                           reinterpret_cast<const unsigned char*>(original), src_len,
                           MZ_BEST_COMPRESSION);
    ASSERT_EQ(ret, MZ_OK);

    std::vector<unsigned char> decompressed(src_len);
    mz_ulong decomp_len = src_len;
    ret = mz_uncompress(decompressed.data(), &decomp_len,
                        compressed.data(), comp_len);
    ASSERT_EQ(ret, MZ_OK);
    EXPECT_EQ(decomp_len, src_len);
    EXPECT_EQ(std::memcmp(decompressed.data(), original, src_len), 0);
}

// ---------------------------------------------------------------------------
// mz_error
// ---------------------------------------------------------------------------

TEST(MzErrorTest, OkReturnsNonNull)
{
    const char* err = mz_error(MZ_OK);
    ASSERT_TRUE(err != nullptr);
}

TEST(MzErrorTest, StreamEndReturnsString)
{
    const char* err = mz_error(MZ_STREAM_END);
    ASSERT_TRUE(err != nullptr);
}

TEST(MzErrorTest, DataErrorReturnsString)
{
    const char* err = mz_error(MZ_DATA_ERROR);
    ASSERT_TRUE(err != nullptr);
}

// ---------------------------------------------------------------------------
// Low-level tdefl/tinfl roundtrip
// ---------------------------------------------------------------------------

TEST(TdeflTinflTest, MemToMemRoundtrip)
{
    const char* original = "Low-level compression test with tdefl/tinfl APIs.";
    size_t src_len = strlen(original);

    size_t out_buf_len = src_len + 256;
    std::vector<unsigned char> compressed(out_buf_len);
    size_t comp_len = tdefl_compress_mem_to_mem(
        compressed.data(), out_buf_len,
        original, src_len, TDEFL_WRITE_ZLIB_HEADER);
    EXPECT_GT(comp_len, 0u);

    std::vector<unsigned char> decompressed(src_len + 256);
    size_t decomp_len = tinfl_decompress_mem_to_mem(
        decompressed.data(), decompressed.size(),
        compressed.data(), comp_len,
        TINFL_FLAG_PARSE_ZLIB_HEADER);
    ASSERT_NE(decomp_len, TINFL_DECOMPRESS_MEM_TO_MEM_FAILED);
    EXPECT_EQ(decomp_len, src_len);
    EXPECT_EQ(std::memcmp(decompressed.data(), original, src_len), 0);
}

// ---------------------------------------------------------------------------
// Heap allocation roundtrip
// ---------------------------------------------------------------------------

TEST(HeapAllocTest, CompressDecompressRoundtrip)
{
    const char* original = "Heap allocation roundtrip test for miniz.";
    size_t src_len = strlen(original);

    size_t comp_len = 0;
    void* compressed = tdefl_compress_mem_to_heap(
        original, src_len, &comp_len, TDEFL_WRITE_ZLIB_HEADER);
    ASSERT_TRUE(compressed != nullptr);
    EXPECT_GT(comp_len, 0u);

    size_t decomp_len = 0;
    void* decompressed = tinfl_decompress_mem_to_heap(
        compressed, comp_len, &decomp_len,
        TINFL_FLAG_PARSE_ZLIB_HEADER);
    ASSERT_TRUE(decompressed != nullptr);
    EXPECT_EQ(decomp_len, src_len);
    EXPECT_EQ(std::memcmp(decompressed, original, src_len), 0);

    mz_free(compressed);
    mz_free(decompressed);
}

// ---------------------------------------------------------------------------
// mz_bool / mz_free
// ---------------------------------------------------------------------------

TEST(MzFreeTest, FreeNullDoesNotCrash)
{
    mz_free(nullptr);
}

// ---------------------------------------------------------------------------
// Compress then verify size is smaller for compressible data
// ---------------------------------------------------------------------------

TEST(CompressSizeTest, CompressedIsSmallerForText)
{
    std::string text(500, ' ');
    for (size_t i = 0; i < text.size(); i++) {
        text[i] = static_cast<char>('a' + (i % 26));
    }
    mz_ulong src_len = static_cast<mz_ulong>(text.size());
    mz_ulong bound = mz_compressBound(src_len);

    std::vector<unsigned char> compressed(bound);
    mz_ulong comp_len = bound;
    int ret = mz_compress(compressed.data(), &comp_len,
                          reinterpret_cast<const unsigned char*>(text.data()), src_len);
    ASSERT_EQ(ret, MZ_OK);
    EXPECT_LT(comp_len, src_len);
}
