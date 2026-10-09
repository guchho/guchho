// =============================================================================
// test/core/pack_tar_test.cpp — what a tar or tar.gz from guchho contains
// =============================================================================
//
// The archives are read back by a tar reader written here, from the format
// definition, rather than by the writer that produced them. A tar header is
// 512 bytes of named fields and a checksum over all of them; a reader that
// recomputes that checksum and compares it against the one on disk is
// checking the writer against the format rather than against itself, and it
// is the same check a tape drive would make.
//
// The gzip wrapper is opened the same way: the header fields are parsed out
// of the bytes, the deflate stream is inflated with miniz's inflater —
// which is the opposite half of the library from the one that compressed it
// — and the CRC-32 and the length at the end are recomputed from what came
// out. An archive that survives that is one any gunzip will accept.
//
// The scratch directories follow the shape pack_test.cpp uses: named after
// the test, created in the constructor, removed in the destructor.
// =============================================================================

#include "test/guchho_test.hpp"

#include "guchho/filesystem.hpp"
#include "guchho/miniz.hpp"
#include "guchho/pack.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

using guchho::filesystem::PathFromUTF8;
using guchho::filesystem::PathToUTF8;

constexpr std::size_t kBlock = 512;
constexpr std::size_t kRecord = 10240;

// The timestamp used by the metadata tests: 2001-09-09 01:46:40 UTC.
constexpr std::int64_t kTestDate = 1000000000;

// A directory that exists for one test and is removed with it.
class TarTempDir
{
public:
    explicit TarTempDir(const std::string& label)
    {
        static int counter = 0;
        path_ = fs::temp_directory_path() /
                ("guchho-tar-test-" + label + "-" + std::to_string(counter++) + "-" +
                 std::to_string(static_cast<long long>(
                     std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count())));

        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }

    ~TarTempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    TarTempDir(const TarTempDir&)            = delete;
    TarTempDir& operator=(const TarTempDir&) = delete;

    fs::path Path(const std::string& relative) const
    {
        return path_ / PathFromUTF8(relative);
    }

    std::string At(const std::string& relative) const { return PathToUTF8(Path(relative)); }

    void Write(const std::string& relative, const std::string& contents) const
    {
        const fs::path full = Path(relative);
        std::error_code ec;
        fs::create_directories(full.parent_path(), ec);
        std::ofstream out(full, std::ios::binary | std::ios::trunc);
        out << contents;
    }

    void MakeDir(const std::string& relative) const
    {
        std::error_code ec;
        fs::create_directories(Path(relative), ec);
    }

private:
    fs::path path_;
};

guchho::pack::PackOptions OptionsFor(const std::string& input, const std::string& out)
{
    guchho::pack::PackOptions options;
    options.inputs.push_back(input);
    options.outFile = out;
    return options;
}

std::string ReadWholeFile(const std::string& path)
{
    std::ifstream in(PathFromUTF8(path), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

bool AllZeros(const std::uint8_t* data, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (data[i] != 0) return false;
    }
    return true;
}

// ===========================================================================
// A tar reader, written from the format rather than from the writer
// ===========================================================================

// One header as the format lays it out.
struct Header {
    std::string   name;
    std::uint64_t mode      = 0;
    std::uint64_t uid       = 0;
    std::uint64_t gid       = 0;
    std::uint64_t size      = 0;
    std::uint64_t mtime     = 0;
    std::string   chksum;      // the six bytes as written
    char          typeflag    = 0;
    std::string   linkname;
    std::string   magic;
    std::string   version;
    std::string   uname;
    std::string   gname;
    std::uint64_t devmajor    = 0;
    std::uint64_t devminor    = 0;
    std::string   prefix;
    bool          checksum_ok = false;
};

// One entry: a header, and the payload that followed it.
struct Entry {
    Header                            header;
    std::string                       contents;
    std::map<std::string, std::string> pax;
};

// Reads a field: octal digits up to the first NUL or space.
std::uint64_t OctalField(const std::uint8_t* block, std::size_t offset, std::size_t width)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        const std::uint8_t c = block[offset + i];
        if (c == ' ' || c == '\0') break;
        if (c < '0' || c > '7') return 0;
        value = (value << 3) + static_cast<std::uint64_t>(c - '0');
    }
    return value;
}

// A text field: up to the first NUL, which is what POSIX means by a
// NUL-terminated field — except that a field which exactly fills its width
// has no NUL to find.
std::string TextField(const std::uint8_t* block, std::size_t offset, std::size_t width)
{
    std::size_t end = offset;
    while (end < offset + width && block[end] != '\0') ++end;
    return std::string(reinterpret_cast<const char*>(block + offset), end - offset);
}

// A numeric field as tar writes one: octal digits, zero padded, no trailing
// NUL — the checksum field is the one that has no room for one.
std::string OctalDigits(std::uint64_t value, std::size_t width)
{
    std::string digits(width, '0');
    for (std::size_t i = width; i-- > 0;) {
        digits[i] = static_cast<char>('0' + (value & 7u));
        value >>= 3;
    }
    return digits;
}

Header ParseHeader(const std::uint8_t* block, const std::string& error_prefix,
                   std::string& error)
{
    Header header;
    header.name     = TextField(block, 0, 100);
    header.mode     = OctalField(block, 100, 7);
    header.uid      = OctalField(block, 108, 7);
    header.gid      = OctalField(block, 116, 7);
    header.size     = OctalField(block, 124, 11);
    header.mtime    = OctalField(block, 136, 11);
    header.chksum   = TextField(block, 148, 8);
    header.typeflag = static_cast<char>(block[156]);
    header.linkname = TextField(block, 157, 100);
    header.magic    = TextField(block, 257, 6);
    header.version  = TextField(block, 263, 2);
    header.uname    = TextField(block, 265, 32);
    header.gname    = TextField(block, 297, 32);
    header.devmajor = OctalField(block, 329, 7);
    header.devminor = OctalField(block, 337, 7);
    header.prefix   = TextField(block, 345, 155);

    // The checksum, recomputed the way the format says: every byte summed
    // with the checksum field itself read as spaces, and the result written
    // back as six octal digits.
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kBlock; ++i) {
        sum += (i >= 148 && i < 156) ? static_cast<std::uint64_t>(' ')
                                     : static_cast<std::uint64_t>(block[i]);
    }

    const std::string expected = OctalDigits(sum, 6);
    header.checksum_ok         = header.chksum == expected;
    if (!header.checksum_ok) {
        error = error_prefix + ": checksum of " + header.name + " is " + header.chksum +
                ", the bytes add up to " + expected;
    }
    return header;
}

// The archive as a reader sees it: the entries, how many bytes the file is,
// and whether it ended the way the format says it ends.
struct Image {
    std::vector<Entry> entries;
    std::size_t        bytes  = 0;
    bool               ended  = false;
    bool               record_aligned = false;
};

// Reads an archive that is already in memory. Used for the bytes that came
// out of a gzip stream as well as for a file on disk — a reader should not
// care where the bytes it is reading came from.
bool ParseTar(const std::string& bytes, Image& out, std::string& error)
{
    out = Image{};
    out.bytes         = bytes.size();
    out.record_aligned = (bytes.size() % kRecord) == 0;

    if (bytes.size() % kBlock != 0) {
        error = "the file is not a whole number of 512-byte blocks";
        return false;
    }

    std::map<std::string, std::string> pending_pax;
    bool                               in_pax = false;
    std::string                        pax_raw;

    for (std::size_t at = 0; at + kBlock <= bytes.size();) {
        const auto* block = reinterpret_cast<const std::uint8_t*>(bytes.data()) + at;

        if (AllZeros(block, kBlock)) {
            // The format ends with two empty blocks. A file may carry more
            // padding after them; anything else is an archive that stopped
            // in the middle.
            const std::size_t second = at + kBlock;
            if (second + kBlock > bytes.size() || !AllZeros(
                    reinterpret_cast<const std::uint8_t*>(bytes.data()) + second, kBlock)) {
                error = "an empty block is not followed by another one";
                return false;
            }
            out.ended = true;
            return true;
        }

        std::string         header_error;
        const Header        header = ParseHeader(block, "block " + std::to_string(at),
                                                 header_error);
        if (!header_error.empty()) {
            error = header_error;
            return false;
        }

        std::size_t payload = at + kBlock;
        if (payload + header.size > bytes.size()) {
            error = header.name + ": the payload runs past the end of the file";
            return false;
        }

        if (header.typeflag == 'x' || header.typeflag == 'X') {
            // An extended header: its payload is a sequence of
            // "<length> <key>=<value>\n" records that describe the entry
            // coming next.
            pax_raw.assign(bytes, payload, header.size);
            std::size_t inner = 0;
            while (inner < pax_raw.size()) {
                const std::size_t space = pax_raw.find(' ', inner);
                if (space == std::string::npos) break;
                const std::size_t length = static_cast<std::size_t>(
                    std::strtoull(pax_raw.substr(inner, space - inner).c_str(), nullptr, 10));
                if (length == 0 || inner + length > pax_raw.size()) break;
                const std::string record = pax_raw.substr(inner, length);
                inner += length;

                const std::size_t eq = record.find('=');
                if (eq == std::string::npos) continue;
                const std::size_t key_at = record.find(' ');
                if (key_at == std::string::npos || key_at > eq) continue;
                pending_pax[record.substr(key_at + 1, eq - key_at - 1)] =
                    record.substr(eq + 1, length - (eq + 1) - 1);
            }
            in_pax  = true;
            at      = payload + ((header.size + kBlock - 1) / kBlock) * kBlock;
            continue;
        }

        Entry entry;
        entry.header = header;
        entry.contents.assign(bytes, payload, header.size);
        entry.pax    = pending_pax;
        pending_pax.clear();
        in_pax       = false;

        out.entries.push_back(std::move(entry));
        at = payload + ((header.size + kBlock - 1) / kBlock) * kBlock;
    }

    error = in_pax ? "the archive ends with an extended header and no entry"
                   : "the archive runs out without an empty block";
    return false;
}

// Reads an archive that is on disk.
bool ReadTar(const std::string& path, Image& out, std::string& error)
{
    return ParseTar(ReadWholeFile(path), out, error);
}

// The name a reader ends up using: the PAX record when there is one, and the
// header's own two fields joined the way the format says otherwise.
std::string NameOf(const Entry& entry)
{
    const auto path = entry.pax.find("path");
    if (path != entry.pax.end()) return path->second;
    if (entry.header.prefix.empty()) return entry.header.name;
    return entry.header.prefix + "/" + entry.header.name;
}

const Entry* Find(const Image& image, const std::string& name)
{
    for (const Entry& entry : image.entries) {
        if (NameOf(entry) == name) return &entry;
    }
    return nullptr;
}

// ===========================================================================
// A gzip reader
// ===========================================================================

struct Gzip {
    unsigned char          magic0 = 0;
    unsigned char          magic1 = 0;
    unsigned char          cm     = 0;
    unsigned char          flg    = 0;
    unsigned char          xfl    = 0;
    unsigned char          os     = 0;
    std::uint32_t          mtime  = 0;
    std::size_t            header_size = 0;
    std::string            body;      // the deflate stream
    std::uint32_t          crc   = 0;
    std::uint32_t          isize = 0;
    std::string            inflated;
};

bool ReadGzip(const std::string& path, Gzip& out, std::string& error)
{
    const std::string bytes = ReadWholeFile(path);
    if (bytes.size() < 18) {
        error = "a gzip file cannot be this short";
        return false;
    }

    auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3])) << 24);
    };

    out.magic0 = static_cast<unsigned char>(bytes[0]);
    out.magic1 = static_cast<unsigned char>(bytes[1]);
    out.cm     = static_cast<unsigned char>(bytes[2]);
    out.flg    = static_cast<unsigned char>(bytes[3]);
    out.mtime  = u32(4);
    out.xfl    = static_cast<unsigned char>(bytes[8]);
    out.os     = static_cast<unsigned char>(bytes[9]);

    std::size_t at = 10;
    if ((out.flg & 0x04) != 0) { // FEXTRA
        if (at + 2 > bytes.size()) {
            error = "the extra field runs past the end";
            return false;
        }
        const std::size_t xlen = static_cast<unsigned char>(bytes[at]) |
                                 (static_cast<std::size_t>(static_cast<unsigned char>(bytes[at + 1])) << 8);
        at += 2 + xlen;
    }
    if ((out.flg & 0x08) != 0) { // FNAME
        while (at < bytes.size() && bytes[at] != '\0') ++at;
        ++at;
    }
    if ((out.flg & 0x10) != 0) { // FCOMMENT
        while (at < bytes.size() && bytes[at] != '\0') ++at;
        ++at;
    }
    if ((out.flg & 0x02) != 0) at += 2; // FHCRC
    if (at + 8 > bytes.size()) {
        error = "the header or trailer runs past the end";
        return false;
    }

    out.header_size = at;
    out.body        = bytes.substr(at, bytes.size() - 8 - at);
    out.crc         = u32(bytes.size() - 8);
    out.isize       = u32(bytes.size() - 4);

    // Inflated with miniz's inflater rather than its deflate side, and over
    // a raw stream because that is what gzip wraps.
    mz_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
        error = "the inflater could not be started";
        return false;
    }

    out.inflated.clear();
    std::vector<unsigned char> chunk(64 * 1024);
    stream.next_in = reinterpret_cast<unsigned char*>(const_cast<char*>(out.body.data()));
    stream.avail_in = static_cast<unsigned int>(out.body.size());

    for (;;) {
        stream.next_out  = chunk.data();
        stream.avail_out = static_cast<unsigned int>(chunk.size());
        const int rc     = mz_inflate(&stream, MZ_NO_FLUSH);
        const std::size_t made = chunk.size() - stream.avail_out;
        out.inflated.append(reinterpret_cast<const char*>(chunk.data()), made);
        if (rc == MZ_STREAM_END) break;
        if (rc != MZ_OK && rc != MZ_BUF_ERROR) {
            error = "the deflate stream is not readable: " +
                    std::string(mz_error(rc) ? mz_error(rc) : "unknown");
            mz_inflateEnd(&stream);
            return false;
        }
        if (made == 0 && stream.avail_in == 0) {
            error = "the deflate stream stops early";
            mz_inflateEnd(&stream);
            return false;
        }
    }
    mz_inflateEnd(&stream);
    return true;
}

// ===========================================================================
// The format itself
// ===========================================================================

TEST(PackTarTest, TheArchiveIsASequenceOfBlocksThatEndsInTwoEmptyOnes)
{
    TarTempDir dir("blocks");
    dir.Write("dist/a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;

    const guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(result.path, image, error)) << error;

    EXPECT_TRUE(image.ended);
    EXPECT_TRUE(image.record_aligned);
    EXPECT_GE(image.bytes, kRecord);
}

TEST(PackTarTest, EveryHeaderCarriesTheFormatMagicAndAConsistentChecksum)
{
    TarTempDir dir("magic");
    dir.Write("dist/a.txt", "a");
    dir.Write("dist/sub/b.txt", "b");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;
    ASSERT_FALSE(image.entries.empty());

    for (const Entry& entry : image.entries) {
        EXPECT_TRUE(entry.header.checksum_ok) << entry.header.name;
        EXPECT_EQ(entry.header.magic, "ustar") << entry.header.name;
        EXPECT_EQ(entry.header.version, "00") << entry.header.name;
        EXPECT_EQ(entry.header.uid, 0u) << entry.header.name;
        EXPECT_EQ(entry.header.gid, 0u) << entry.header.name;
        EXPECT_TRUE(entry.header.uname.empty()) << entry.header.name;
        EXPECT_TRUE(entry.header.gname.empty()) << entry.header.name;
    }
}

TEST(PackTarTest, DirectoriesAreTypeFiveCarryingNoBytes)
{
    TarTempDir dir("dirs");
    dir.Write("dist/a.txt", "a");
    dir.MakeDir("dist/emptydir");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;

    const Entry* dist = Find(image, "dist/");
    ASSERT_TRUE(dist != nullptr);
    EXPECT_EQ(dist->header.typeflag, '5');
    EXPECT_EQ(dist->header.size, 0u);
    EXPECT_TRUE(dist->contents.empty());

    const Entry* empty = Find(image, "dist/emptydir/");
    ASSERT_TRUE(empty != nullptr);
    EXPECT_EQ(empty->header.typeflag, '5');
    EXPECT_EQ(empty->header.size, 0u);
}

TEST(PackTarTest, RegularFilesAreTypeZeroAndCarryTheirBytes)
{
    TarTempDir dir("files");
    dir.Write("dist/a.txt", "alpha");
    dir.Write("dist/empty.txt", "");
    dir.Write("dist/sub/b.txt", "beta");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;

    const Entry* alpha = Find(image, "dist/a.txt");
    ASSERT_TRUE(alpha != nullptr);
    EXPECT_EQ(alpha->header.typeflag, '0');
    EXPECT_EQ(alpha->header.size, 5u);
    EXPECT_EQ(alpha->contents, "alpha");

    const Entry* empty = Find(image, "dist/empty.txt");
    ASSERT_TRUE(empty != nullptr);
    EXPECT_EQ(empty->header.typeflag, '0');
    EXPECT_EQ(empty->header.size, 0u);
    EXPECT_TRUE(empty->contents.empty());

    const Entry* beta = Find(image, "dist/sub/b.txt");
    ASSERT_TRUE(beta != nullptr);
    EXPECT_EQ(beta->contents, "beta");
}

TEST(PackTarTest, TheDateAskedForIsWrittenIntoEveryHeader)
{
    TarTempDir dir("date");
    dir.Write("dist/a.txt", "a");
    dir.Write("dist/sub/b.txt", "b");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;
    ASSERT_FALSE(image.entries.empty());

    for (const Entry& entry : image.entries) {
        EXPECT_EQ(entry.header.mtime, static_cast<std::uint64_t>(kTestDate))
            << entry.header.name;
    }
}

TEST(PackTarTest, ADateBefore1980IsRecordedRatherThanRefused)
{
    // The window that a ZIP entry has is not a window this format has. A
    // file from 1975 belongs in a tar archive with its own year on it.
    TarTempDir dir("old-date");
    dir.Write("dist/a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = 157783680; // 1975-01-01 00:00:00 UTC

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;
    ASSERT_FALSE(image.entries.empty());

    for (const Entry& entry : image.entries) {
        EXPECT_EQ(entry.header.mtime, 157783680u) << entry.header.name;
    }
}

TEST(PackTarTest, ADateBeforeTheEpochIsRefused)
{
    TarTempDir dir("before-epoch");
    dir.Write("dist/a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = -1;

    const guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("Unix epoch") != std::string::npos) << result.error;

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.tar")), ec));
}

TEST(PackTarTest, TheModeAskedForIsWrittenIntoEveryHeader)
{
    TarTempDir dir("mode");
    dir.Write("dist/script.sh", "#!/bin/sh\n");
    dir.Write("dist/data.txt", "data");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;
    options.mode                      = 0755u;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;

    const Entry* script = Find(image, "dist/script.sh");
    ASSERT_TRUE(script != nullptr);
    EXPECT_EQ(script->header.mode, 0755u);

    const Entry* data = Find(image, "dist/data.txt");
    ASSERT_TRUE(data != nullptr);
    EXPECT_EQ(data->header.mode, 0755u);
}

TEST(PackTarTest, AModeWithoutExecuteBitsIsStillGivenToADirectory)
{
    // 0644 describes a file. Written on a directory it would describe
    // something nobody can walk into, so the execute bits go back in.
    TarTempDir dir("dir-mode");
    dir.Write("dist/a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;
    options.mode                      = 0644u;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;

    const Entry* dist = Find(image, "dist/");
    ASSERT_TRUE(dist != nullptr);
    EXPECT_EQ(dist->header.mode, 0755u);

    const Entry* file = Find(image, "dist/a.txt");
    ASSERT_TRUE(file != nullptr);
    EXPECT_EQ(file->header.mode, 0644u);
}

TEST(PackTarTest, ANameLongerThanOneFieldIsSplitAcrossPrefixAndName)
{
    TarTempDir dir("split");
    const std::string middle(40, 'a');
    const std::string leaf(60, 'b');
    dir.Write("dist/" + middle + "/" + leaf, "split");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;

    const std::string name = "dist/" + middle + "/" + leaf;
    ASSERT_GT(name.size(), 100u);

    const Entry* found = Find(image, name);
    ASSERT_TRUE(found != nullptr) << "no entry is called " << name;
    EXPECT_TRUE(found->pax.empty()) << "the name fits, so no extended header is needed";
    EXPECT_EQ(found->header.prefix, "dist/" + middle);
    EXPECT_EQ(found->header.name, leaf);
    EXPECT_EQ(found->contents, "split");
}

TEST(PackTarTest, ANameThatCannotBeSplitGetsAnExtendedHeader)
{
    TarTempDir dir("pax");
    const std::string leaf(150, 'g');
    dir.Write("dist/" + leaf + ".txt", "pax");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;

    const std::string name = "dist/" + leaf + ".txt";
    ASSERT_GT(name.size(), 155u);

    const Entry* found = Find(image, name);
    ASSERT_TRUE(found != nullptr) << "no entry is called " << name;
    EXPECT_EQ(found->pax.at("path"), name);
    EXPECT_EQ(found->contents, "pax");
}

TEST(PackTarTest, ADirectoryNameThatCannotBeSplitGetsAnExtendedHeaderToo)
{
    TarTempDir dir("pax-dir");
    const std::string leaf(150, 'd');
    dir.Write("dist/" + leaf + "/inner.txt", "in");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.date                      = kTestDate;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(guchho::pack::CreatePack(options).path, image, error)) << error;

    const Entry* found = Find(image, "dist/" + leaf + "/");
    ASSERT_TRUE(found != nullptr);
    EXPECT_EQ(found->pax.at("path"), "dist/" + leaf + "/");
    EXPECT_EQ(found->header.typeflag, '5');
    EXPECT_EQ(found->header.size, 0u);
}

TEST(PackTarTest, TheSameTreePacksToTheSameEntryListInBothFormats)
{
    TarTempDir dir("same");
    dir.Write("dist/a.txt", "alpha");
    dir.Write("dist/sub/b.txt", "beta");
    dir.MakeDir("dist/emptydir");

    guchho::pack::PackOptions tar_options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    tar_options.format                    = "tar";
    tar_options.date                      = kTestDate;

    guchho::pack::PackOptions gz_options = OptionsFor(dir.At("dist"), dir.At("out.tar.gz"));
    gz_options.format                    = "tar.gz";
    gz_options.date                      = kTestDate;

    const guchho::pack::PackResult plain_result = guchho::pack::CreatePack(tar_options);
    ASSERT_TRUE(plain_result.Ok()) << plain_result.error;
    const guchho::pack::PackResult gz_result = guchho::pack::CreatePack(gz_options);
    ASSERT_TRUE(gz_result.Ok()) << gz_result.error;

    Image      plain;
    Image      gzipped;
    std::string error;
    ASSERT_TRUE(ReadTar(plain_result.path, plain, error)) << error;

    Gzip gzip;
    ASSERT_TRUE(ReadGzip(gz_result.path, gzip, error)) << error;
    ASSERT_TRUE(ParseTar(gzip.inflated, gzipped, error)) << error;

    // With the same date asked for, the two archives are the same bytes.
    // tar.gz is tar and gzip, and nowhere in between is there a second
    // opinion about what belongs in the archive.
    EXPECT_EQ(gzip.inflated, ReadWholeFile(plain_result.path));

    ASSERT_EQ(plain.entries.size(), gzipped.entries.size());
    for (std::size_t i = 0; i < plain.entries.size(); ++i) {
        EXPECT_EQ(NameOf(plain.entries[i]), NameOf(gzipped.entries[i]));
        EXPECT_EQ(plain.entries[i].header.typeflag, gzipped.entries[i].header.typeflag);
        EXPECT_EQ(plain.entries[i].header.size, gzipped.entries[i].header.size);
        EXPECT_EQ(plain.entries[i].header.mtime, gzipped.entries[i].header.mtime);
        EXPECT_EQ(plain.entries[i].header.mode, gzipped.entries[i].header.mode);
        EXPECT_EQ(plain.entries[i].contents, gzipped.entries[i].contents);
    }
}

// ===========================================================================
// The gzip wrapper
// ===========================================================================

TEST(PackTarGzTest, TheFileIsGzipAndSaysSoInItsFirstTenBytes)
{
    TarTempDir dir("gzip-header");
    dir.Write("dist/a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar.gz"));
    options.format                    = "tar.gz";
    options.date                      = kTestDate;
    options.level                     = 6;

    const guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    Gzip  gzip;
    std::string error;
    ASSERT_TRUE(ReadGzip(result.path, gzip, error)) << error;

    EXPECT_EQ(gzip.magic0, 0x1f);
    EXPECT_EQ(gzip.magic1, 0x8b);
    EXPECT_EQ(gzip.cm, 8) << "CM 8 is deflate, and it is the only value gzip allows";
    EXPECT_EQ(gzip.flg, 0) << "nothing below needs to say where the file came from";
    EXPECT_EQ(gzip.mtime, 0u) << "an archive is a build product, not a copy of one file";
    EXPECT_EQ(gzip.os, 0xff) << "unknown, so the bytes do not depend on the machine";
}

TEST(PackTarGzTest, WhatComesOutIsTheSameTarTheUncompressedFormatWouldHaveWritten)
{
    TarTempDir dir("gzip-roundtrip");
    dir.Write("dist/a.txt", "alpha");
    dir.Write("dist/sub/b.txt", "beta");
    dir.MakeDir("dist/emptydir");

    guchho::pack::PackOptions plain_options = OptionsFor(dir.At("dist"), dir.At("plain.tar"));
    plain_options.format                    = "tar";
    plain_options.date                      = kTestDate;

    guchho::pack::PackOptions gz_options = OptionsFor(dir.At("dist"), dir.At("out.tar.gz"));
    gz_options.format                    = "tar.gz";
    gz_options.date                      = kTestDate;

    const guchho::pack::PackResult plain_result = guchho::pack::CreatePack(plain_options);
    ASSERT_TRUE(plain_result.Ok()) << plain_result.error;
    const guchho::pack::PackResult gz_result = guchho::pack::CreatePack(gz_options);
    ASSERT_TRUE(gz_result.Ok()) << gz_result.error;

    Gzip  gzip;
    std::string error;
    ASSERT_TRUE(ReadGzip(gz_result.path, gzip, error)) << error;

    EXPECT_EQ(gzip.inflated, ReadWholeFile(plain_result.path))
        << "the two formats are the same archive, one of them compressed";

    // The two numbers gzip puts at the end are a promise about those bytes.
    const std::uint32_t crc = static_cast<std::uint32_t>(
        mz_crc32(0, reinterpret_cast<const unsigned char*>(gzip.inflated.data()),
                 static_cast<mz_ulong>(gzip.inflated.size())));
    EXPECT_EQ(gzip.crc, crc);
    EXPECT_EQ(gzip.isize, static_cast<std::uint32_t>(gzip.inflated.size()));
}

TEST(PackTarGzTest, EveryLevelProducesSomethingGunzipAccepts)
{
    // The level is miniz's to interpret; what is asserted here is that each
    // one produces a well-formed gzip file whose contents are the right
    // archive, and that the levels that are meant to differ in size do.
    TarTempDir dir("levels");
    dir.Write("dist/a.txt", std::string(4096, 'a'));

    std::vector<std::size_t> sizes;
    for (int level : {1, 6, 9}) {
        guchho::pack::PackOptions options = OptionsFor(dir.At("dist"),
                                                       dir.At("out" + std::to_string(level) +
                                                              ".tar.gz"));
        options.format = "tar.gz";
        options.date   = kTestDate;
        options.level  = level;

        const guchho::pack::PackResult result = guchho::pack::CreatePack(options);
        ASSERT_TRUE(result.Ok()) << result.error;

        Gzip  gzip;
        std::string error;
        ASSERT_TRUE(ReadGzip(result.path, gzip, error)) << error;

        Image image;
        ASSERT_TRUE(ParseTar(gzip.inflated, image, error)) << error;
        ASSERT_EQ(image.entries.size(), 2u);
        EXPECT_TRUE(Find(image, "dist/") != nullptr);

        const std::uint32_t crc = static_cast<std::uint32_t>(
            mz_crc32(0, reinterpret_cast<const unsigned char*>(gzip.inflated.data()),
                     static_cast<mz_ulong>(gzip.inflated.size())));
        EXPECT_EQ(gzip.crc, crc) << "level " << level;
        EXPECT_EQ(gzip.isize, static_cast<std::uint32_t>(gzip.inflated.size()));

        sizes.push_back(static_cast<std::size_t>(result.size));
    }

    // Highly repetitive content at the fastest level still compresses, and
    // the best level never does worse than it.
    EXPECT_LT(sizes[0], 4096u);
    EXPECT_LE(sizes[2], sizes[0]);
}

TEST(PackTarGzTest, AnEmptyDirectoryAndAnEmptyFileBothSurvive)
{
    TarTempDir dir("empties");
    dir.MakeDir("dist/emptydir");
    dir.Write("dist/empty.txt", "");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar.gz"));
    options.format                    = "tar.gz";
    options.date                      = kTestDate;

    const guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    Gzip  gzip;
    std::string error;
    ASSERT_TRUE(ReadGzip(result.path, gzip, error)) << error;

    Image image;
    ASSERT_TRUE(ParseTar(gzip.inflated, image, error)) << error;
    EXPECT_EQ(image.entries.size(), 3u) << "the root, the empty directory, the empty file";
    EXPECT_TRUE(image.record_aligned);
    EXPECT_TRUE(image.record_aligned)
        << "the uncompressed bytes are the same archive the tar format writes";

    const Entry* directory = Find(image, "dist/emptydir/");
    ASSERT_TRUE(directory != nullptr);
    EXPECT_EQ(directory->header.typeflag, '5');

    const Entry* file = Find(image, "dist/empty.txt");
    ASSERT_TRUE(file != nullptr);
    EXPECT_EQ(file->header.typeflag, '0');
    EXPECT_EQ(file->header.size, 0u);
}

TEST(PackTarGzTest, ALevelOnPlainTarIsRefusedRatherThanIgnored)
{
    TarTempDir dir("level-on-tar");
    dir.Write("dist/a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    options.level                     = 9;

    const guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("does not apply") != std::string::npos) << result.error;

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.tar")), ec));
}

TEST(PackTarTest, TheOutputIsRefusedWhenItAlreadyExists)
{
    TarTempDir dir("overwrite");
    dir.Write("dist/a.txt", "a");
    dir.Write("out.tar", "not an archive");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";

    const guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("exists") != std::string::npos) << result.error;
    EXPECT_EQ(ReadWholeFile(dir.At("out.tar")), "not an archive");

    options.overwrite = true;
    const guchho::pack::PackResult second = guchho::pack::CreatePack(options);
    ASSERT_TRUE(second.Ok()) << second.error;

    Image image;
    std::string error;
    ASSERT_TRUE(ReadTar(second.path, image, error)) << error;
    EXPECT_EQ(image.entries.size(), 2u) << "the file and the directory above it";
}

TEST(PackTarTest, TheDefaultOutputNameEndsInTheFormatTheUserNamed)
{
    EXPECT_EQ(guchho::pack::DefaultOutFile("dist", "tar"), "dist.tar");
    EXPECT_EQ(guchho::pack::DefaultOutFile("dist", "tar.gz"), "dist.tar.gz");
    EXPECT_EQ(guchho::pack::DefaultOutFile("dist.tar", "tar"), "dist.tar");
}

TEST(PackTarTest, AFailedRunLeavesNothingBehind)
{
    TarTempDir dir("no-partial");
    dir.Write("dist/a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("out.tar"));
    options.format                    = "tar";
    // A directory that cannot be read makes the walk fail after the output
    // has been decided, which is the case where a temporary file would be
    // most likely to be left lying about.
    options.inputs.push_back(dir.At("does-not-exist"));

    const guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.tar")), ec));

    for (const fs::directory_entry& entry : fs::directory_iterator(dir.Path("."), ec)) {
        const std::string name = PathToUTF8(entry.path().filename());
        EXPECT_TRUE(name == "dist" || name == "out.tar") << name;
    }
}

} // namespace
