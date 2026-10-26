// =============================================================================
// test/core/unpack_test.cpp — extracting what guchho packed
// =============================================================================

#include "guchho/filesystem.hpp"
#include "guchho/pack.hpp"
#include "guchho/unpack.hpp"
#include "test/guchho_test.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

using guchho::filesystem::PathFromUTF8;
using guchho::filesystem::PathToUTF8;

constexpr std::size_t kBlock = 512;

class UnpackTempDir
{
public:
    explicit UnpackTempDir(const std::string& label)
    {
        static int counter = 0;
        path_ = fs::temp_directory_path() /
                ("guchho-unpack-test-" + label + "-" + std::to_string(counter++) + "-" +
                 std::to_string(static_cast<long long>(
                     std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count())));
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }

    ~UnpackTempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    UnpackTempDir(const UnpackTempDir&)            = delete;
    UnpackTempDir& operator=(const UnpackTempDir&) = delete;

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

std::string ReadWholeFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

guchho::pack::PackOptions PackOptionsFor(const std::string& input,
                                         const std::string& output,
                                         const std::string& format)
{
    guchho::pack::PackOptions options;
    options.inputs.push_back(input);
    options.outFile = output;
    options.format = format;
    options.overwrite = true;
    return options;
}

void PackTree(const UnpackTempDir& dir)
{
    dir.MakeDir("srcdir");
    dir.Write("srcdir/hello.txt", "hello world");
    dir.Write("srcdir/nested/data.bin", "123456");
}

void PutOctal(char* field, std::size_t width, std::uint64_t value)
{
    for (std::size_t i = width; i-- > 0;) {
        field[i] = static_cast<char>('0' + (value & 7u));
        value >>= 3;
    }
}

std::uint32_t TarChecksum(const std::array<char, kBlock>& block)
{
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < kBlock; ++i) {
        sum += (i >= 148 && i < 156) ? static_cast<std::uint32_t>(' ')
                                     : static_cast<std::uint32_t>(
                                           static_cast<unsigned char>(block[i]));
    }
    return sum;
}

void AddTarHeader(std::vector<char>& archive, const std::string& name,
                  const std::string& contents, char typeflag = '0')
{
    std::array<char, kBlock> block {};
    const std::size_t name_length = std::min(name.size(), static_cast<std::size_t>(100));
    std::memcpy(block.data(), name.data(), name_length);
    PutOctal(block.data() + 100, 8, 0644);
    PutOctal(block.data() + 108, 8, 0);
    PutOctal(block.data() + 116, 8, 0);
    PutOctal(block.data() + 124, 12, contents.size());
    PutOctal(block.data() + 136, 12, 0);
    std::memset(block.data() + 148, ' ', 8);
    block[156] = typeflag;
    std::memcpy(block.data() + 257, "ustar", 5);
    block[263] = '\0';
    block[264] = '0';
    block[265] = '0';
    PutOctal(block.data() + 148, 6, TarChecksum(block));
    block[154] = '\0';
    block[155] = ' ';

    archive.insert(archive.end(), block.begin(), block.end());
    archive.insert(archive.end(), contents.begin(), contents.end());
    const std::size_t remainder = contents.size() % kBlock;
    if (remainder != 0) {
        archive.insert(archive.end(), kBlock - remainder, '\0');
    }
}

void AddPaxPath(std::vector<char>& archive, const std::string& path)
{
    std::string value = "path=" + path + "\n";
    std::size_t total = value.size() + 3;
    for (;;) {
        const std::string length = std::to_string(total);
        const std::size_t next = length.size() + 1 + value.size();
        if (next == total) break;
        total = next;
    }
    const std::string record = std::to_string(total) + " " + value;
    AddTarHeader(archive, "././@PaxHeader", record, 'x');
}

void FinishTar(std::vector<char>& archive)
{
    archive.insert(archive.end(), 2 * kBlock, '\0');
}

void WriteBytes(const fs::path& path, const std::vector<char>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}


} // namespace

TEST(Unpack, ZipRoundtrip)
{
    UnpackTempDir dir("zip");
    PackTree(dir);

    const std::string archive = dir.At("out.zip");
    auto pres = guchho::pack::CreatePack(PackOptionsFor(dir.At("srcdir"), archive, "zip"));
    ASSERT_TRUE(pres.Ok());

    guchho::unpack::UnpackOptions options;
    options.archive = archive;
    options.outDir  = dir.At("extracted");
    options.overwrite = true;
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(result.Ok());
    ASSERT_TRUE(fs::exists(dir.Path("extracted/srcdir/hello.txt")));
    ASSERT_TRUE(fs::exists(dir.Path("extracted/srcdir/nested/data.bin")));
    ASSERT_EQ(ReadWholeFile(dir.Path("extracted/srcdir/hello.txt")), std::string("hello world"));
}

TEST(Unpack, TarRoundtrip)
{
    UnpackTempDir dir("tar");
    PackTree(dir);

    const std::string archive = dir.At("out.tar");
    auto pres = guchho::pack::CreatePack(PackOptionsFor(dir.At("srcdir"), archive, "tar"));
    ASSERT_TRUE(pres.Ok());

    guchho::unpack::UnpackOptions options;
    options.archive = archive;
    options.outDir  = dir.At("extracted");
    options.overwrite = true;
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(result.Ok());
    ASSERT_TRUE(fs::exists(dir.Path("extracted/srcdir/hello.txt")));
    ASSERT_EQ(ReadWholeFile(dir.Path("extracted/srcdir/nested/data.bin")), std::string("123456"));
}

TEST(Unpack, TarGzRoundtrip)
{
    UnpackTempDir dir("targz");
    PackTree(dir);

    const std::string archive = dir.At("out.tar.gz");
    auto pres = guchho::pack::CreatePack(PackOptionsFor(dir.At("srcdir"), archive, "tar.gz"));
    ASSERT_TRUE(pres.Ok());

    guchho::unpack::UnpackOptions options;
    options.archive = archive;
    options.outDir  = dir.At("extracted");
    options.overwrite = true;
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(result.Ok());
    ASSERT_EQ(ReadWholeFile(dir.Path("extracted/srcdir/hello.txt")), std::string("hello world"));
}

TEST(Unpack, AutoDetectsFormatFromExtension)
{
    UnpackTempDir dir("detect");
    PackTree(dir);

    const std::string archive = dir.At("out.tgz");
    auto pres = guchho::pack::CreatePack(PackOptionsFor(dir.At("srcdir"), archive, "tar.gz"));
    ASSERT_TRUE(pres.Ok());

    guchho::unpack::UnpackOptions options;
    options.archive = archive;
    options.outDir  = dir.At("extracted");
    options.format.clear();
    options.overwrite = true;
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(result.Ok());
    ASSERT_TRUE(fs::exists(dir.Path("extracted/srcdir/hello.txt")));
}

TEST(Unpack, DefaultOutDirStripsKnownExtensions)
{
    ASSERT_EQ(guchho::unpack::DefaultOutDir("release.zip"), std::string("release"));
    ASSERT_EQ(guchho::unpack::DefaultOutDir("release.tar"), std::string("release"));
    ASSERT_EQ(guchho::unpack::DefaultOutDir("release.tar.gz"), std::string("release"));
    ASSERT_EQ(guchho::unpack::DefaultOutDir("release.tgz"), std::string("release"));
    ASSERT_EQ(guchho::unpack::DefaultOutDir("dist/"), std::string("dist"));
    ASSERT_EQ(guchho::unpack::DefaultOutDir("archive"), std::string("archive"));
}

TEST(Unpack, OverwriteIsRefusedUnlessAsked)
{
    UnpackTempDir dir("overwrite");
    PackTree(dir);

    const std::string archive = dir.At("out.zip");
    auto pres = guchho::pack::CreatePack(PackOptionsFor(dir.At("srcdir"), archive, "zip"));
    ASSERT_TRUE(pres.Ok());

    guchho::unpack::UnpackOptions options;
    options.archive = archive;
    options.outDir  = dir.At("extracted");
    options.overwrite = true;
    ASSERT_TRUE(guchho::unpack::CreateUnpack(options).Ok());

    dir.Write("extracted/srcdir/hello.txt", "changed");

    options.overwrite = false;
    auto kept = guchho::unpack::CreateUnpack(options);
    ASSERT_TRUE(kept.Ok());
    ASSERT_EQ(ReadWholeFile(dir.Path("extracted/srcdir/hello.txt")), std::string("changed"));
    ASSERT_TRUE(!kept.warnings.empty());

    options.overwrite = true;
    ASSERT_TRUE(guchho::unpack::CreateUnpack(options).Ok());
    ASSERT_EQ(ReadWholeFile(dir.Path("extracted/srcdir/hello.txt")), std::string("hello world"));
}

TEST(Unpack, MissingArchiveIsAnError)
{
    UnpackTempDir dir("missing");

    guchho::unpack::UnpackOptions options;
    options.archive = dir.At("does-not-exist.zip");
    options.outDir  = dir.At("extracted");
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(!result.Ok());
    ASSERT_TRUE(!result.error.empty());
}

TEST(Unpack, StripComponentsDropsLeadingDirectories)
{
    UnpackTempDir dir("strip");
    PackTree(dir);

    const std::string archive = dir.At("out.zip");
    auto pres = guchho::pack::CreatePack(PackOptionsFor(dir.At("srcdir"), archive, "zip"));
    ASSERT_TRUE(pres.Ok());

    guchho::unpack::UnpackOptions options;
    options.archive = archive;
    options.outDir  = dir.At("extracted");
    options.stripComponents = 1;
    options.overwrite = true;
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(result.Ok());
    ASSERT_TRUE(fs::exists(dir.Path("extracted/hello.txt")));
}

TEST(Unpack, TraversalIsSkipped)
{
    UnpackTempDir dir("traversal");

    std::vector<char> archive;
    AddTarHeader(archive, "../evil.txt", "bad");
    FinishTar(archive);
    const fs::path tar_path = dir.Path("evil.tar");
    WriteBytes(tar_path, archive);

    guchho::unpack::UnpackOptions options;
    options.archive = PathToUTF8(tar_path);
    options.outDir  = dir.At("extracted");
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(result.Ok());
    ASSERT_TRUE(!result.warnings.empty());
    ASSERT_TRUE(!fs::exists(dir.Path("extracted/../evil.txt")));
}

TEST(Unpack, CorruptTarChecksumIsRejected)
{
    UnpackTempDir dir("checksum");

    std::vector<char> archive;
    AddTarHeader(archive, "file.txt", "contents");
    FinishTar(archive);
    archive[0] = 'F'; // name no longer matches the checksum

    const fs::path tar_path = dir.Path("corrupt.tar");
    WriteBytes(tar_path, archive);

    guchho::unpack::UnpackOptions options;
    options.archive = PathToUTF8(tar_path);
    options.outDir  = dir.At("extracted");
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(!result.Ok());
    ASSERT_TRUE(result.error.find("checksum") != std::string::npos);
}

TEST(Unpack, PreserveFlagsAreIndependent)
{
    UnpackTempDir dir("preserve-flags");
    dir.MakeDir("srcdir");
    dir.Write("srcdir/hello.txt", "hello");

    constexpr std::time_t kOldTime = 981173106; // 2001-02-03 04:05:06 UTC
    const fs::path source = dir.Path("srcdir/hello.txt");
    std::error_code ec;
    const auto old_system = std::chrono::system_clock::from_time_t(kOldTime);
    const auto old_file   = fs::file_time_type::clock::now() +
                          (old_system - std::chrono::system_clock::now());
    fs::last_write_time(source, old_file, ec);
    ASSERT_FALSE(ec);

    const std::string archive = dir.At("out.tar");
    auto pres = guchho::pack::CreatePack(PackOptionsFor(dir.At("srcdir"), archive, "tar"));
    ASSERT_TRUE(pres.Ok());

    auto unpackTo = [&](const std::string& out, bool timestamps) {
        guchho::unpack::UnpackOptions options;
        options.archive = archive;
        options.outDir  = out;
        options.overwrite = true;
        options.preservePermissions = true;
        options.preserveTimestamps  = timestamps;
        return guchho::unpack::CreateUnpack(options);
    };

    auto toSeconds = [](fs::file_time_type value) {
        const auto diff = value - fs::file_time_type::clock::now();
        const auto system = std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(diff));
        return static_cast<long long>(std::chrono::system_clock::to_time_t(system));
    };

    // Permissions on, timestamps off: the extracted file keeps the moment it
    // was written rather than the moment the source had in 2001.
    ASSERT_TRUE(unpackTo(dir.At("fresh"), false).Ok());
    const auto fresh = fs::last_write_time(dir.Path("fresh/srcdir/hello.txt"), ec);
    ASSERT_FALSE(ec);
    ASSERT_GT(std::llabs(toSeconds(fresh) - static_cast<long long>(kOldTime)),
              static_cast<long long>(86400));

    // Timestamps on: the archive's value is restored.
    ASSERT_TRUE(unpackTo(dir.At("kept"), true).Ok());
    const auto kept = fs::last_write_time(dir.Path("kept/srcdir/hello.txt"), ec);
    ASSERT_FALSE(ec);
    ASSERT_LE(std::llabs(toSeconds(kept) - static_cast<long long>(kOldTime)), 2LL);
}

TEST(Unpack, ExistingSymlinkAtDestinationIsNotFollowed)
{
#ifdef _WIN32
    return; // creating symlinks requires privileges on Windows
#else
    UnpackTempDir dir("symlink-dest");
    dir.Write("outside.txt", "original");
    dir.MakeDir("extracted");

    std::error_code ec;
    fs::create_symlink(dir.Path("outside.txt"), dir.Path("extracted/evil.txt"), ec);
    if (ec) return; // the host cannot create symbolic links

    std::vector<char> archive;
    AddTarHeader(archive, "evil.txt", "owned");
    FinishTar(archive);
    const fs::path tar_path = dir.Path("evil.tar");
    WriteBytes(tar_path, archive);

    guchho::unpack::UnpackOptions options;
    options.archive   = PathToUTF8(tar_path);
    options.outDir    = dir.At("extracted");
    options.overwrite = true;
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(result.Ok());
    ASSERT_EQ(ReadWholeFile(dir.Path("outside.txt")), std::string("original"));
    ASSERT_TRUE(!result.warnings.empty());
#endif
}

TEST(Unpack, PaxPathHeaderIsHonoured)
{
    UnpackTempDir dir("pax");

    std::vector<char> archive;
    AddPaxPath(archive, "pax/name.txt");
    AddTarHeader(archive, "placeholder.txt", "pax contents");
    FinishTar(archive);
    const fs::path tar_path = dir.Path("pax.tar");
    WriteBytes(tar_path, archive);

    guchho::unpack::UnpackOptions options;
    options.archive = PathToUTF8(tar_path);
    options.outDir  = dir.At("extracted");
    auto result = guchho::unpack::CreateUnpack(options);

    ASSERT_TRUE(result.Ok());
    ASSERT_TRUE(fs::exists(dir.Path("extracted/pax/name.txt")));
    ASSERT_EQ(ReadWholeFile(dir.Path("extracted/pax/name.txt")),
              std::string("pax contents"));
}
