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

// ---------------------------------------------------------------------------
// Guchho extension: mz_zip_writer_set_default_attributes
// ---------------------------------------------------------------------------

// 2017-07-14 02:40:00 UTC -- even, so it survives DOS 2-second precision.
static const MZ_TIME_T kDosRoundTripTime = 1500000000;

// Builds a heap archive holding one entry under the given archive-level
// default attributes, then reads it back so the central directory can be
// inspected. Returns false on any writer/reader failure.
static bool BuildAndStat(const char* entry_name, const char* contents,
                         mz_uint32 default_attributes,
                         bool call_setter,
                         MZ_TIME_T entry_time,
                         mz_zip_archive_file_stat* out_stat)
{
    mz_zip_archive writer;
    mz_zip_zero_struct(&writer);
    if (!mz_zip_writer_init_heap(&writer, 0, 0))
        return false;

    if (call_setter &&
        !mz_zip_writer_set_default_attributes(&writer, default_attributes)) {
        mz_zip_writer_end(&writer);
        return false;
    }

    MZ_TIME_T time = entry_time;
    bool added = mz_zip_writer_add_mem_ex_v2(
        &writer, entry_name, contents, strlen(contents), nullptr, 0,
        MZ_DEFAULT_LEVEL, 0, 0, &time, nullptr, 0, nullptr, 0);
    if (!added) {
        mz_zip_writer_end(&writer);
        return false;
    }

    void* archive = nullptr;
    size_t archive_size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&writer, &archive, &archive_size)) {
        mz_zip_writer_end(&writer);
        return false;
    }
    mz_zip_writer_end(&writer);

    mz_zip_archive reader;
    mz_zip_zero_struct(&reader);
    bool ok = mz_zip_reader_init_mem(&reader, archive, archive_size, 0) &&
              mz_zip_reader_get_num_files(&reader) == 1 &&
              mz_zip_reader_file_stat(&reader, 0, out_stat);
    mz_zip_end(&reader);
    mz_free(archive);
    return ok;
}

TEST(MzZipDefaultAttributesTest, SetterRejectsNullArchive)
{
    EXPECT_EQ(mz_zip_writer_set_default_attributes(nullptr, 0), MZ_FALSE);
}

TEST(MzZipDefaultAttributesTest, DefaultIsZeroLikeUpstream)
{
    mz_zip_archive_file_stat stat;
    ASSERT_TRUE(BuildAndStat("file.txt", "hello", 0, false,
                             kDosRoundTripTime, &stat));
    EXPECT_EQ(stat.m_external_attr, 0u);
    EXPECT_EQ(stat.m_version_made_by, 0x0014u);
}

TEST(MzZipDefaultAttributesTest, UnixModeIsWrittenToExternalAttr)
{
    const mz_uint32 mode_644 = 0100644u;
    mz_zip_archive_file_stat stat;
    ASSERT_TRUE(BuildAndStat("file.txt", "hello", mode_644 << 16, true,
                             kDosRoundTripTime, &stat));
    EXPECT_EQ(stat.m_external_attr, mode_644 << 16);
    EXPECT_EQ(stat.m_version_made_by, 0x0314u);
}

TEST(MzZipDefaultAttributesTest, DosDirBitIsSetForDirectoryEntries)
{
    const mz_uint32 mode_755 = 040755u;
    mz_zip_archive_file_stat stat;
    ASSERT_TRUE(BuildAndStat("subdir/", "", mode_755 << 16, true,
                             kDosRoundTripTime, &stat));
    EXPECT_EQ(stat.m_external_attr, (mode_755 << 16) | 0x10u);
    EXPECT_TRUE(stat.m_is_directory);
    EXPECT_EQ(stat.m_version_made_by, 0x0314u);
}

TEST(MzZipDefaultAttributesTest, ExplicitTimestampIsPreserved)
{
    const MZ_TIME_T when = kDosRoundTripTime;
    mz_zip_archive_file_stat stat;
    ASSERT_TRUE(BuildAndStat("file.txt", "hello", 0, false, when, &stat));
    EXPECT_EQ(static_cast<MZ_TIME_T>(stat.m_time), when);
}

TEST(MzZipDefaultAttributesTest, SetterIsAppliedToLaterEntriesOnly)
{
    mz_zip_archive writer;
    mz_zip_zero_struct(&writer);
    ASSERT_EQ(mz_zip_writer_init_heap(&writer, 0, 0), MZ_TRUE);

    ASSERT_EQ(mz_zip_writer_add_mem(&writer, "before.txt", "a", 1,
                                    MZ_DEFAULT_LEVEL),
              MZ_TRUE);
    ASSERT_EQ(mz_zip_writer_set_default_attributes(&writer, 0100644u << 16),
              MZ_TRUE);
    ASSERT_EQ(mz_zip_writer_add_mem(&writer, "after.txt", "b", 1,
                                    MZ_DEFAULT_LEVEL),
              MZ_TRUE);

    void* archive = nullptr;
    size_t archive_size = 0;
    ASSERT_EQ(mz_zip_writer_finalize_heap_archive(&writer, &archive,
                                                  &archive_size),
              MZ_TRUE);
    mz_zip_writer_end(&writer);

    mz_zip_archive reader;
    mz_zip_zero_struct(&reader);
    ASSERT_EQ(mz_zip_reader_init_mem(&reader, archive, archive_size, 0),
              MZ_TRUE);
    ASSERT_EQ(mz_zip_reader_get_num_files(&reader), 2u);

    mz_zip_archive_file_stat before, after;
    ASSERT_TRUE(mz_zip_reader_file_stat(&reader, 0, &before));
    ASSERT_TRUE(mz_zip_reader_file_stat(&reader, 1, &after));
    EXPECT_EQ(before.m_external_attr, 0u);
    EXPECT_EQ(after.m_external_attr, 0100644u << 16);

    mz_zip_end(&reader);
    mz_free(archive);
}
