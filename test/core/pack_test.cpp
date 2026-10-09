// =============================================================================
// test/core/pack_test.cpp — what guchho::pack puts into an archive, and what it
// refuses to
// =============================================================================
//
// Every test here writes a small tree into a temporary directory, asks for an
// archive of it, and then reads the archive back with miniz's reader rather
// than with the writer that produced it. Reading it back through the same
// library that wrote it is deliberate: the question these tests answer is
// "would a ZIP reader see this?", and a reader is the only thing that knows
// what a reader would see. The central directory is where the answers live —
// the entry name, the external attributes, the timestamp, the method — so
// most assertions are on a stat rather than on a byte.
//
// The scratch directories follow the shape filesystem_test.cpp uses: named
// after the test, created in the constructor, removed in the destructor, so a
// failing run leaves nothing behind for the next one to trip over.
// =============================================================================

#include "test/guchho_test.hpp"

#include "guchho/filesystem.hpp"
#include "guchho/miniz.hpp"
#include "guchho/pack.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using guchho::filesystem::PathFromUTF8;
using guchho::filesystem::PathToUTF8;

// A directory that exists for one test and is removed with it.
//
// Paths are kept in their native form and converted at the boundary. Building
// an fs::path straight from a UTF-8 string would read those bytes as the
// system's narrow encoding on Windows, and a name with an accent in it would
// arrive at the filesystem as two entirely different characters.
class PackTempDir
{
public:
    explicit PackTempDir(const std::string& label)
    {
        static int counter = 0;
        path_ = fs::temp_directory_path() /
                ("guchho-zip-test-" + label + "-" + std::to_string(counter++) + "-" +
                 std::to_string(static_cast<long long>(
                     std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count())));

        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }

    ~PackTempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    PackTempDir(const PackTempDir&)            = delete;
    PackTempDir& operator=(const PackTempDir&) = delete;

    fs::path Path(const std::string& relative) const
    {
        return path_ / PathFromUTF8(relative);
    }

    // A path inside the directory as UTF-8 with forward slashes, so a test
    // can write a non-ASCII name and read the same bytes back on any platform.
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

    // True when something could be created; a Windows runner without
    // developer mode refuses symlinks, and the tests that need one skip.
    bool TrySymlink(const std::string& target, const std::string& link) const
    {
        std::error_code ec;
        fs::create_directory_symlink(PathFromUTF8(target), Path(link), ec);
        return !ec;
    }

    std::string path() const { return PathToUTF8(path_); }

private:
    fs::path path_;
};

// Runs a body with the working directory somewhere else, and puts it back
// however the body ended — a test that leaves the process standing in a
// deleted directory fails every test after it.
class ScopedCwd
{
public:
    explicit ScopedCwd(const fs::path& where) : previous_(fs::current_path())
    {
        std::error_code ec;
        fs::current_path(where, ec);
        ok_ = !ec;
    }

    ~ScopedCwd()
    {
        std::error_code ec;
        fs::current_path(previous_, ec);
    }

    ScopedCwd(const ScopedCwd&)            = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;

    bool ok() const { return ok_; }

private:
    fs::path previous_;
    bool     ok_ = false;
};

// One entry as a reader sees it.
struct ArchiveEntry {
    std::string   name;
    std::string   contents;
    bool          is_dir    = false;
    std::uint32_t external  = 0;
    std::uint16_t made_by   = 0;
    std::uint16_t method    = 0;
    std::time_t   mtime     = 0;
};

// Reads a finished archive back. Returns false and fills "error" when it is
// not one — which is itself the assertion several of these tests make, since
// a file that a reader cannot open is the clearest failure there is.
bool ReadArchive(const std::string& path, std::vector<ArchiveEntry>& out,
                 std::string& error)
{
    out.clear();

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
        return false;
    }

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            error = "cannot stat entry " + std::to_string(i);
            mz_zip_reader_end(&zip);
            return false;
        }

        ArchiveEntry entry;
        entry.name       = stat.m_filename;
        entry.is_dir     = stat.m_is_directory != 0;
        entry.external   = stat.m_external_attr;
        entry.made_by    = stat.m_version_made_by;
        entry.method     = stat.m_method;
        entry.mtime      = stat.m_time;

        if (!entry.is_dir) {
            size_t          size = 0;
            void*           data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
            if (data == nullptr) {
                error = "cannot extract " + entry.name;
                mz_zip_reader_end(&zip);
                return false;
            }
            entry.contents.assign(static_cast<const char*>(data), size);
            mz_free(data);
        }

        out.push_back(std::move(entry));
    }

    mz_zip_reader_end(&zip);
    return true;
}

const ArchiveEntry* Find(const std::vector<ArchiveEntry>& entries, const std::string& name)
{
    for (const ArchiveEntry& entry : entries) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

std::vector<std::string> Names(const std::vector<ArchiveEntry>& entries)
{
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const ArchiveEntry& entry : entries) names.push_back(entry.name);
    return names;
}

bool Contains(const std::vector<std::string>& items, const std::string& value)
{
    for (const std::string& item : items) {
        if (item == value) return true;
    }
    return false;
}

// 2017-07-14 02:40:00 UTC. Even, so it survives DOS's two-second precision
// unchanged, and far enough from any DST boundary to round trip exactly on
// every machine this test runs on.
constexpr std::int64_t kTestDate = 1500000000;

// A plain request with one input and one output, which most tests only need
// to vary by a field or two.
guchho::pack::PackOptions OptionsFor(const std::string& input, const std::string& out)
{
    guchho::pack::PackOptions options;
    options.inputs = {input};
    options.outFile = out;
    return options;
}

} // namespace

// ===========================================================================
// DefaultOutFile
// ===========================================================================

TEST(PackDefaultOutFileTest, DirectoryWithoutExtensionGainsOne)
{
    EXPECT_EQ(guchho::pack::DefaultOutFile("dist", "zip"), "dist.zip");
    EXPECT_EQ(guchho::pack::DefaultOutFile("dist/", "zip"), "dist.zip");
    // The directory part of the input is kept, so the archive lands beside
    // what it was made from rather than wherever the command was run.
    EXPECT_EQ(guchho::pack::DefaultOutFile("./dist", "zip"), "./dist.zip");
}

TEST(PackDefaultOutFileTest, ExistingExtensionIsReplaced)
{
    EXPECT_EQ(guchho::pack::DefaultOutFile("app.js", "zip"), "app.zip");
    EXPECT_EQ(guchho::pack::DefaultOutFile("src/index.html", "zip"),
              "src/index.zip");
    EXPECT_EQ(guchho::pack::DefaultOutFile("report.json", "zip"), "report.zip");
}

TEST(PackDefaultOutFileTest, HiddenFileKeepsItsDot)
{
    EXPECT_EQ(guchho::pack::DefaultOutFile(".bashrc", "zip"), ".bashrc.zip");
}

TEST(PackDefaultOutFileTest, AlreadyAnArchiveIsStillNamedAfterItself)
{
    EXPECT_EQ(guchho::pack::DefaultOutFile("release.zip", "zip"), "release.zip");
}

// ===========================================================================
// DefaultOutFile, per format
// ===========================================================================

// The extension follows the format, because two requests that differ only in
// --format must not produce files that differ only in an extension somebody
// guessed.
TEST(PackDefaultOutFileTest, EachFormatHasItsOwnExtension)
{
    EXPECT_EQ(guchho::pack::DefaultOutFile("dist", "tar"), "dist.tar");
    EXPECT_EQ(guchho::pack::DefaultOutFile("dist/", "tar"), "dist.tar");
    EXPECT_EQ(guchho::pack::DefaultOutFile("dist", "tar.gz"), "dist.tar.gz");
    EXPECT_EQ(guchho::pack::DefaultOutFile("app.js", "tar"), "app.tar");
    EXPECT_EQ(guchho::pack::DefaultOutFile("report.json", "tar.gz"),
              "report.tar.gz");
    EXPECT_EQ(guchho::pack::DefaultOutFile(".bashrc", "tar.gz"),
              ".bashrc.tar.gz");
}

// An extension with two components is the case where "replace the last
// extension" and "give it its own name" are different rules. Only the second
// one produces "release.tar.gz" from "release.tar.gz".
TEST(PackDefaultOutFileTest, ANameAlreadyWrittenForTheFormatIsLeftAlone)
{
    EXPECT_EQ(guchho::pack::DefaultOutFile("release.tar", "tar"), "release.tar");
    EXPECT_EQ(guchho::pack::DefaultOutFile("release.tar.gz", "tar.gz"),
              "release.tar.gz");
    // And a name written for one format is re-named for another rather than
    // growing a second extension.
    EXPECT_EQ(guchho::pack::DefaultOutFile("dist.tar", "tar.gz"),
              "dist.tar.gz");
    EXPECT_EQ(guchho::pack::DefaultOutFile("release.tar.gz", "zip"),
              "release.tar.zip");
}

// ===========================================================================
// The happy path
// ===========================================================================

TEST(PackCreateTest, SingleFileIsStoredUnderItsOwnName)
{
    PackTempDir dir("single");
    dir.Write("app.js", "console.log(1);\n");

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("app.js"), dir.At("out.zip")));

    ASSERT_TRUE(result.Ok()) << result.error;
    EXPECT_EQ(result.path, dir.At("out.zip"));
    EXPECT_GT(result.size, 0u);
    EXPECT_TRUE(result.warnings.empty());

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "app.js");
    EXPECT_EQ(entries[0].contents, "console.log(1);\n");
    EXPECT_FALSE(entries[0].is_dir);
}

TEST(PackCreateTest, DirectoryIsArchivedUnderItsOwnName)
{
    PackTempDir dir("tree");
    dir.Write("dist/index.html", "<html></html>");
    dir.Write("dist/app.js", "let a = 1;\n");
    dir.Write("dist/assets/logo.svg", "<svg/>");
    dir.MakeDir("dist/empty");

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("dist"), dir.At("out.zip")));

    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;

    const std::vector<std::string> names = Names(entries);
    EXPECT_TRUE(Contains(names, "dist/"));
    EXPECT_TRUE(Contains(names, "dist/index.html"));
    EXPECT_TRUE(Contains(names, "dist/app.js"));
    EXPECT_TRUE(Contains(names, "dist/assets/"));
    EXPECT_TRUE(Contains(names, "dist/assets/logo.svg"));
    EXPECT_TRUE(Contains(names, "dist/empty/"));

    const ArchiveEntry* nested = Find(entries, "dist/assets/logo.svg");
    ASSERT_TRUE(nested != nullptr);
    EXPECT_EQ(nested->contents, "<svg/>");

    const ArchiveEntry* empty = Find(entries, "dist/empty/");
    ASSERT_TRUE(empty != nullptr);
    EXPECT_TRUE(empty->is_dir);
    EXPECT_TRUE(empty->contents.empty());
}

TEST(PackCreateTest, EntrySeparatorsAreForwardSlashesOnEveryPlatform)
{
    PackTempDir dir("separators");
    dir.Write("dist/sub/leaf.txt", "leaf");

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("dist"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;

    for (const ArchiveEntry& entry : entries) {
        EXPECT_TRUE(entry.name.find('\\') == std::string::npos) << entry.name;
        EXPECT_FALSE(entry.name.starts_with("/")) << entry.name;
        EXPECT_TRUE(entry.name.find("..") == std::string::npos) << entry.name;
    }
}

TEST(PackCreateTest, HiddenFilesAreIncluded)
{
    PackTempDir dir("hidden");
    dir.Write("proj/.gitignore", "*.log\n");
    dir.Write("proj/visible.txt", "hi");

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("proj"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "proj/.gitignore"));
}

TEST(PackCreateTest, EmptyFileIsStored)
{
    PackTempDir dir("empty-file");
    dir.Write("zero.bin", "");

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("zero.bin"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].contents, "");
}

TEST(PackCreateTest, CurrentDirectoryHasNoPrefix)
{
    PackTempDir dir("dot");
    dir.Write("index.html", "<html></html>");
    dir.Write("src/main.js", "main();");

    ScopedCwd cwd(PathFromUTF8(dir.path()));
    ASSERT_TRUE(cwd.ok());

    // "." names the tree this command is standing in, and there is no name
    // for it that would mean the same thing on another machine — so its
    // contents go to the top of the archive.
    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(".", dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "index.html"));
    EXPECT_TRUE(Contains(Names(entries), "src/main.js"));
    EXPECT_TRUE(Contains(Names(entries), "src/"));
}

TEST(PackCreateTest, NestedInputUsesOnlyItsLastComponent)
{
    PackTempDir dir("nested");
    dir.Write("a/b/dist/file.txt", "x");

    // "../../dist" and "./dist" and "dist" are three spellings of one root,
    // and none of them carries the journey to it into the archive.
    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("a/b/dist"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "dist/file.txt"));
    EXPECT_TRUE(Contains(Names(entries), "dist/"));
}

TEST(PackCreateTest, ParentDirectoryComponentsNeverReachTheArchive)
{
    PackTempDir dir("dotdot");
    dir.Write("a/inner/file.txt", "x");

    const std::string input = dir.At("a") + "/inner/../inner";
    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(input, dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    for (const ArchiveEntry& entry : entries) {
        EXPECT_TRUE(entry.name.find("..") == std::string::npos) << entry.name;
    }
    EXPECT_TRUE(Contains(Names(entries), "inner/file.txt"));
}

TEST(PackCreateTest, MultipleInputsKeepDistinctRoots)
{
    PackTempDir dir("multi");
    dir.Write("dist/app.js", "a");
    dir.Write("src/main.js", "b");

    guchho::pack::PackOptions options;
    options.inputs  = {dir.At("dist"), dir.At("src")};
    options.outFile = dir.At("out.zip");

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "dist/app.js"));
    EXPECT_TRUE(Contains(Names(entries), "src/main.js"));
}

TEST(PackCreateTest, UnicodeFilenamesSurvive)
{
    PackTempDir dir("unicode");
    dir.Write("proj/café.txt", "buerger");
    dir.Write("proj/日本語.md", "nihongo");

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("proj"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;

    const ArchiveEntry* accented = Find(entries, "proj/café.txt");
    ASSERT_TRUE(accented != nullptr);
    EXPECT_EQ(accented->contents, "buerger");
    EXPECT_TRUE(Contains(Names(entries), "proj/日本語.md"));
}

TEST(PackCreateTest, BinaryContentsRoundTripByteForByte)
{
    PackTempDir dir("binary");
    std::string bytes;
    bytes.reserve(513);
    for (int i = 0; i < 513; i++) {
        bytes.push_back(static_cast<char>(i & 0xFF));
    }
    dir.Write("blob.bin", bytes);

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("blob.bin"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    ASSERT_EQ(entries[0].contents.size(), bytes.size());
    EXPECT_EQ(std::memcmp(entries[0].contents.data(), bytes.data(), bytes.size()), 0);
}

TEST(PackCreateTest, LargeFileIsStreamedRatherThanHeldInMemory)
{
    PackTempDir dir("large");

    // Big enough that a one-megabyte-at-a-time reader has to come back for
    // more than one buffer, which is the part of the streaming path a small
    // file never exercises.
    const std::string chunk(64 * 1024, 'z');
    {
        std::ofstream out(PathFromUTF8(dir.At("big.txt")), std::ios::binary);
        for (int i = 0; i < 40; i++) out << chunk;
    }

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("big.txt"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].contents.size(), 64u * 1024u * 40u);
    EXPECT_EQ(entries[0].contents[0], 'z');
    EXPECT_EQ(entries[0].contents.back(), 'z');
}

TEST(PackCreateTest, CompressionLevelsAreRecordedAndReadable)
{
    PackTempDir dir("levels");
    std::string text;
    for (int i = 0; i < 4000; i++) text += "the quick brown fox jumps over the lazy dog\n";
    dir.Write("text.txt", text);

    guchho::pack::PackOptions stored = OptionsFor(dir.At("text.txt"), dir.At("stored.zip"));
    stored.level = 0;
    guchho::pack::PackResult stored_result = guchho::pack::CreatePack(stored);
    ASSERT_TRUE(stored_result.Ok()) << stored_result.error;

    guchho::pack::PackOptions hard = OptionsFor(dir.At("text.txt"), dir.At("hard.zip"));
    hard.level = 9;
    guchho::pack::PackResult hard_result = guchho::pack::CreatePack(hard);
    ASSERT_TRUE(hard_result.Ok()) << hard_result.error;

    std::vector<ArchiveEntry> stored_entries;
    std::vector<ArchiveEntry> hard_entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(stored_result.path, stored_entries, error)) << error;
    ASSERT_TRUE(ReadArchive(hard_result.path, hard_entries, error)) << error;

    ASSERT_EQ(stored_entries.size(), 1u);
    ASSERT_EQ(hard_entries.size(), 1u);
    EXPECT_EQ(stored_entries[0].contents, text);
    EXPECT_EQ(hard_entries[0].contents, text);

    // Level 0 must be an honest "store": the compressed size cannot exceed
    // the original by anything but the format's own overhead, and level 9
    // must do better than level 0 on text that repeats.
    EXPECT_EQ(stored_entries[0].method, 0u);
    EXPECT_LT(hard_result.size, stored_result.size);
}

// ===========================================================================
// date and mode metadata
// ===========================================================================

TEST(PackCreateTest, DateIsWrittenToEveryEntry)
{
    PackTempDir dir("date");
    dir.Write("proj/a.txt", "a");
    dir.Write("proj/sub/b.txt", "b");
    dir.MakeDir("proj/sub");

    guchho::pack::PackOptions options = OptionsFor(dir.At("proj"), dir.At("out.zip"));
    options.date = kTestDate;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_FALSE(entries.empty());

    for (const ArchiveEntry& entry : entries) {
        EXPECT_EQ(static_cast<std::int64_t>(entry.mtime), kTestDate) << entry.name;
    }
}

TEST(PackCreateTest, DateOutsideTheDosRangeIsRefused)
{
    PackTempDir dir("date-range");
    dir.Write("a.txt", "a");

    // 1970 is before the format's first representable year. Wrapping would
    // record it as 2107 or thereabouts, so it is refused instead.
    guchho::pack::PackOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.zip"));
    options.date = 0;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("1980") != std::string::npos) << result.error;

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(PackCreateTest, ModeIsWrittenToFilesAsUnixAttributes)
{
    PackTempDir dir("mode");
    dir.Write("script.sh", "#!/bin/sh\n");

    guchho::pack::PackOptions options = OptionsFor(dir.At("script.sh"), dir.At("out.zip"));
    options.mode = 0755u;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);

    EXPECT_EQ(entries[0].external, 0100755u << 16);
    // A non-zero high half is what makes the header say "this came from a
    // Unix-like system"; without it a reader sees a DOS-made archive and
    // discards the mode above.
    EXPECT_EQ(entries[0].made_by, 0x0314u);
}

TEST(PackCreateTest, DirectoryModeGainsExecuteBitsItWouldOtherwiseLack)
{
    PackTempDir dir("dir-mode");
    dir.Write("proj/a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("proj"), dir.At("out.zip"));
    options.mode = 0644u;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;

    const ArchiveEntry* root = Find(entries, "proj/");
    ASSERT_TRUE(root != nullptr);
    // The low half carries the DOS directory flag, which miniz sets itself
    // from the trailing slash rather than taking it from the mode above.
    EXPECT_EQ(root->external, (0040755u << 16) | 0x10u);
    EXPECT_TRUE(root->is_dir);

    // A file keeps the mode it was given, execute bits and all.
    const ArchiveEntry* file = Find(entries, "proj/a.txt");
    ASSERT_TRUE(file != nullptr);
    EXPECT_EQ(file->external, 0100644u << 16);
}

TEST(PackCreateTest, ModeOutsideThePermissionMaskIsRefused)
{
    PackTempDir dir("mode-range");
    dir.Write("a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.zip"));
    options.mode = 0100777u;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("0100777") != std::string::npos) << result.error;

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(PackCreateTest, OmittedModeRecordsThePermissionsAlreadyOnDisk)
{
    PackTempDir dir("default-mode");
    dir.Write("data.txt", "data");

    const std::string source = dir.At("data.txt");
    std::error_code   ec;
    fs::permissions(PathFromUTF8(source),
                    fs::perms::owner_read | fs::perms::owner_write, ec);
    ASSERT_FALSE(ec);

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(source, dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);

    // The file type and the permissions share the high half of the field;
    // the low half stays clear for a plain file.
    const std::uint32_t high = entries[0].external >> 16;
    EXPECT_EQ(entries[0].external & 0xFFFFu, 0u);
    EXPECT_EQ(high & 0170000u, 0100000u);

    // Exactly what the disk says, to the bit. Windows reports an ordinary
    // file as executable and read-write no matter how it was created, so
    // rather than naming the bits this asserts the archive and the disk
    // agree — which is the thing that could go wrong on this code path.
    const std::uint32_t on_disk =
        static_cast<std::uint32_t>(
            fs::status(PathFromUTF8(source)).permissions()) &
        07777u;
    EXPECT_EQ(high & 07777u, on_disk);
    EXPECT_NE(high & 0400u, 0u);
    EXPECT_NE(high & 0200u, 0u);
}

// ===========================================================================
// Refusals
// ===========================================================================

TEST(PackFormatTest, ThreeFormatsAreKnown)
{
    EXPECT_EQ(std::string(guchho::pack::kDefaultFormat), "zip");
    EXPECT_TRUE(guchho::pack::IsSupportedFormat("zip"));
    EXPECT_TRUE(guchho::pack::IsSupportedFormat("tar"));
    EXPECT_TRUE(guchho::pack::IsSupportedFormat("tar.gz"));
    EXPECT_FALSE(guchho::pack::IsSupportedFormat("7z"));
    EXPECT_FALSE(guchho::pack::IsSupportedFormat("tgz"));

    // Exact comparison, on purpose: a format is spelled in lower case and
    // "ZIP" is a different answer rather than a near miss.
    EXPECT_FALSE(guchho::pack::IsSupportedFormat("ZIP"));
    EXPECT_FALSE(guchho::pack::IsSupportedFormat(""));

    // The list and the default cannot drift apart: the default is what a
    // request that named no format gets, so it has to be one of the formats
    // the list says are available.
    ASSERT_FALSE(guchho::pack::kFormats.empty());
    EXPECT_EQ(guchho::pack::kFormats.front(),
              std::string_view(guchho::pack::kDefaultFormat));
}

// The refusal has one spelling wherever it is printed, and that spelling names
// every format rather than the one that was asked for — the point of the
// message is to say what would have worked.
TEST(PackFormatTest, TheRefusalNamesEverySupportedFormat)
{
    const std::string note = guchho::pack::SupportedFormatsNote();

    EXPECT_EQ(note, "Only \"zip\", \"tar\" and \"tar.gz\" are supported today.");
    EXPECT_TRUE(note.find("zip") != std::string::npos) << note;
    EXPECT_TRUE(note.find("tar") != std::string::npos) << note;
    EXPECT_TRUE(note.find("tar.gz") != std::string::npos) << note;
}

TEST(PackFormatTest, AnUnspokenFormatIsTheDefaultOne)
{
    PackTempDir dir("format-default");
    dir.Write("a.txt", "a");

    // Nothing sets format, which is the way almost every caller spells it.
    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("a.txt"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "a.txt");
}

TEST(PackFormatTest, TheDefaultFormatNamedOutLoudIsStillAccepted)
{
    PackTempDir dir("format-zip");
    dir.Write("a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.zip"));
    options.format = "zip";

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    EXPECT_TRUE(result.Ok()) << result.error;
}

TEST(PackFormatTest, UnsupportedFormatIsRefused)
{
    PackTempDir dir("format-unsupported");
    dir.Write("a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.7z"));
    options.format = "7z";

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("Unsupported archive format") != std::string::npos)
        << result.error;
    EXPECT_TRUE(result.error.find("\"7z\"") != std::string::npos) << result.error;
    EXPECT_EQ(result.note, guchho::pack::SupportedFormatsNote());
    EXPECT_TRUE(result.path.empty());

    // Nothing was written: a format nobody can write must not become an
    // archive nobody can open, not even one the rest of the request was fine
    // with.
    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.7z")), ec));
}

TEST(PackFormatTest, UnsupportedFormatIsRefusedBeforeAnythingElse)
{
    PackTempDir dir("format-first");

    // No inputs and no output file, both of which are otherwise refusals of
    // their own. The format is the request's first question and answers first.
    guchho::pack::PackOptions options;
    options.format = "7z";

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("Unsupported archive format") != std::string::npos)
        << result.error;
}

// A level on a format that does not compress is refused at the same step, for
// the same reason: it is a question about the request, not about the disk, and
// answering it later would mean writing an archive the caller did not ask for.
TEST(PackFormatTest, ALevelIsRefusedForTheUncompressedFormat)
{
    PackTempDir dir("format-level");
    dir.Write("a.txt", "a");

    guchho::pack::PackOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.tar"));
    options.format = "tar";
    options.level  = 9;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("does not apply") != std::string::npos)
        << result.error;
    EXPECT_TRUE(result.error.find("\"tar\"") != std::string::npos) << result.error;
    EXPECT_FALSE(result.note.empty());
    EXPECT_TRUE(result.path.empty());

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.tar")), ec));
}

// Refused before anything else, including the level's own range: the level 9
// is a perfectly good level, it is the pairing that is wrong.
TEST(PackFormatTest, TheLevelRefusalComesBeforeAnythingAboutTheDisk)
{
    PackTempDir dir("format-level-first");

    guchho::pack::PackOptions options;
    options.format = "tar";
    options.level  = 9;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("does not apply") != std::string::npos)
        << result.error;
}

TEST(PackCreateTest, NoInputsIsRefused)
{
    PackTempDir dir("no-inputs");

    guchho::pack::PackOptions options;
    options.outFile = dir.At("out.zip");

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_FALSE(result.note.empty());
    EXPECT_TRUE(result.path.empty());
}

TEST(PackCreateTest, NoOutputFileIsRefused)
{
    PackTempDir dir("no-output");
    dir.Write("a.txt", "a");

    guchho::pack::PackOptions options;
    options.inputs = {dir.At("a.txt")};

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
}

TEST(PackCreateTest, MissingInputIsNamed)
{
    PackTempDir dir("missing");
    dir.Write("present.txt", "here");

    guchho::pack::PackOptions options;
    options.inputs  = {dir.At("present.txt"), dir.At("absent.txt")};
    options.outFile = dir.At("out.zip");

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("absent.txt") != std::string::npos) << result.error;

    // Nothing was written: a run that failed on the second input must not
    // leave an archive that looks like it covered the first.
    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(PackCreateTest, CompressionLevelOutOfRangeIsRefused)
{
    PackTempDir dir("bad-level");
    dir.Write("a.txt", "a");

    for (int level : {-1, 10, 99}) {
        guchho::pack::PackOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.zip"));
        options.level = level;

        guchho::pack::PackResult result = guchho::pack::CreatePack(options);
        ASSERT_FALSE(result.Ok()) << level;
        EXPECT_TRUE(result.error.find("out of range") != std::string::npos)
            << result.error;
    }

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(PackCreateTest, ExistingOutputIsLeftAloneWithoutOverwrite)
{
    PackTempDir dir("existing");
    dir.Write("a.txt", "new content");
    dir.Write("out.zip", "an old archive, or anything else");

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("a.txt"), dir.At("out.zip")));
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("already exists") != std::string::npos)
        << result.error;
    EXPECT_FALSE(result.note.empty());

    std::ifstream in(PathFromUTF8(dir.At("out.zip")), std::ios::binary);
    std::string   untouched((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(untouched, "an old archive, or anything else");
}

TEST(PackCreateTest, ExistingOutputIsReplacedWithOverwrite)
{
    PackTempDir dir("overwrite");
    dir.Write("a.txt", "new content");
    dir.Write("out.zip", "an old archive");

    guchho::pack::PackOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.zip"));
    options.overwrite = true;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].contents, "new content");
}

TEST(PackCreateTest, OutputThatIsADirectoryIsRefused)
{
    PackTempDir dir("out-is-dir");
    dir.Write("a.txt", "a");
    dir.MakeDir("out.zip");

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("a.txt"), dir.At("out.zip")));
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("directory") != std::string::npos) << result.error;
}

TEST(PackCreateTest, DuplicateArchivePathsAreRefused)
{
    PackTempDir dir("duplicate");
    dir.Write("a/file.txt", "one");
    dir.Write("b/file.txt", "two");

    guchho::pack::PackOptions options;
    options.inputs  = {dir.At("a"), dir.At("a")};
    options.outFile = dir.At("out.zip");

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("same archive path") != std::string::npos)
        << result.error;
    EXPECT_FALSE(result.note.empty());

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(PackCreateTest, InputsWhoseRootsCollideAreRefused)
{
    PackTempDir dir("collide");
    dir.Write("one/dist/app.js", "1");
    dir.Write("two/dist/app.js", "2");

    // Each input is stored under the name of its own last component, so two
    // different trees called "dist" would both claim "dist/" and one would
    // silently overwrite the other in the central directory.
    guchho::pack::PackOptions options;
    options.inputs  = {dir.At("one/dist"), dir.At("two/dist")};
    options.outFile = dir.At("out.zip");

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("same archive path") != std::string::npos)
        << result.error;
    EXPECT_FALSE(result.note.empty());

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(PackCreateTest, OutputInsideItsOwnInputTreeIsExcluded)
{
    PackTempDir dir("self-output");
    dir.Write("dist/app.js", "a");
    dir.Write("dist/out.zip", "the archive from a previous run");

    guchho::pack::PackOptions options = OptionsFor(dir.At("dist"), dir.At("dist/out.zip"));
    options.overwrite = true;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_TRUE(result.Ok()) << result.error;
    ASSERT_EQ(result.warnings.size(), 1u);
    EXPECT_TRUE(result.warnings[0].find("being written") != std::string::npos)
        << result.warnings[0];

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_FALSE(Contains(Names(entries), "dist/out.zip"));
    EXPECT_TRUE(Contains(Names(entries), "dist/app.js"));
}

TEST(PackCreateTest, OutputFileCannotAlsoBeAnInput)
{
    PackTempDir dir("output-is-input");
    dir.Write("release.zip", "an existing archive");

    guchho::pack::PackOptions options = OptionsFor(dir.At("release.zip"),
                                                 dir.At("release.zip"));
    options.overwrite = true;

    guchho::pack::PackResult result = guchho::pack::CreatePack(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("also an input") != std::string::npos)
        << result.error;
    EXPECT_FALSE(result.note.empty());
}

TEST(PackCreateTest, OutputDirectoryIsCreatedWhenMissing)
{
    PackTempDir dir("out-dir");
    dir.Write("a.txt", "a");

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("a.txt"), dir.At("build/deep/out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::error_code ec;
    EXPECT_TRUE(fs::exists(PathFromUTF8(result.path), ec));
}

TEST(PackCreateTest, AFailedRunLeavesNoTemporaryFileBehind)
{
    PackTempDir dir("temp-cleanup");
    dir.Write("tree/a.txt", "a");
    dir.Write("tree/b.txt", "b");

    // The second input does not exist, so the run fails after the first has
    // been walked and before a byte of archive has been written.
    guchho::pack::PackOptions bad;
    bad.inputs  = {dir.At("tree"), dir.At("nope")};
    bad.outFile = dir.At("out.zip");
    ASSERT_FALSE(guchho::pack::CreatePack(bad).Ok());

    // And a run that succeeds over the same output must not find anything
    // left over from the one that did not.
    guchho::pack::PackOptions good;
    good.inputs  = {dir.At("tree")};
    good.outFile = dir.At("out.zip");
    ASSERT_TRUE(guchho::pack::CreatePack(good).Ok());

    std::error_code ec;
    std::vector<std::string> leftovers;
    for (const fs::directory_entry& entry :
         fs::directory_iterator(PathFromUTF8(dir.path()), ec)) {
        const std::string name = PathToUTF8(entry.path().filename());
        if (name != "tree" && name != "out.zip") leftovers.push_back(name);
    }
    ASSERT_TRUE(leftovers.empty()) << leftovers.front();
}

// ===========================================================================
// Symlinks
// ===========================================================================

TEST(PackCreateTest, SymlinksInsideTheTreeAreSkippedWithAWarning)
{
    PackTempDir dir("symlink-inside");
    dir.Write("tree/real.txt", "real");
    dir.Write("elsewhere.txt", "somewhere else");

    if (!dir.TrySymlink(dir.At("elsewhere.txt"), "tree/link.txt")) {
        // Creating a symlink needs privileges this runner may not have.
        return;
    }

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("tree"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    ASSERT_EQ(result.warnings.size(), 1u);
    EXPECT_TRUE(result.warnings[0].find("symbolic link") != std::string::npos)
        << result.warnings[0];

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_FALSE(Contains(Names(entries), "tree/link.txt"));
    EXPECT_TRUE(Contains(Names(entries), "tree/real.txt"));
}

TEST(PackCreateTest, SymlinkNamedAsTheInputIsFollowedOnce)
{
    PackTempDir dir("symlink-root");
    dir.Write("target/inside.txt", "reached");

    if (!dir.TrySymlink(dir.At("target"), "alias")) {
        return;
    }

    // Naming a link is naming what it points at, so the archive is rooted
    // at the name the person typed and holds the target's contents.
    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("alias"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "alias/"));
    EXPECT_TRUE(Contains(Names(entries), "alias/inside.txt"));

    const ArchiveEntry* file = Find(entries, "alias/inside.txt");
    ASSERT_TRUE(file != nullptr);
    EXPECT_EQ(file->contents, "reached");
}

TEST(PackCreateTest, BrokenSymlinkInputIsReportedAsSuch)
{
    PackTempDir dir("symlink-broken");
    dir.Write("real.txt", "here");

    if (!dir.TrySymlink(dir.At("does-not-exist.txt"), "dangling")) {
        return;
    }

    guchho::pack::PackResult result =
        guchho::pack::CreatePack(OptionsFor(dir.At("dangling"), dir.At("out.zip")));
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("does not resolve") != std::string::npos)
        << result.error;
}
