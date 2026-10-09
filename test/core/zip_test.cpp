// =============================================================================
// test/core/zip_test.cpp — what guchho::zip puts into an archive, and what it
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
#include "guchho/zip.hpp"

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
class ZipTempDir
{
public:
    explicit ZipTempDir(const std::string& label)
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

    ~ZipTempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    ZipTempDir(const ZipTempDir&)            = delete;
    ZipTempDir& operator=(const ZipTempDir&) = delete;

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
guchho::zip::ZipOptions OptionsFor(const std::string& input, const std::string& out)
{
    guchho::zip::ZipOptions options;
    options.inputs = {input};
    options.outFile = out;
    return options;
}

} // namespace

// ===========================================================================
// DefaultOutFile
// ===========================================================================

TEST(ZipDefaultOutFileTest, DirectoryWithoutExtensionGainsOne)
{
    EXPECT_EQ(guchho::zip::DefaultOutFile("dist"), "dist.zip");
    EXPECT_EQ(guchho::zip::DefaultOutFile("dist/"), "dist.zip");
    // The directory part of the input is kept, so the archive lands beside
    // what it was made from rather than wherever the command was run.
    EXPECT_EQ(guchho::zip::DefaultOutFile("./dist"), "./dist.zip");
}

TEST(ZipDefaultOutFileTest, ExistingExtensionIsReplaced)
{
    EXPECT_EQ(guchho::zip::DefaultOutFile("app.js"), "app.zip");
    EXPECT_EQ(guchho::zip::DefaultOutFile("src/index.html"), "src/index.zip");
    EXPECT_EQ(guchho::zip::DefaultOutFile("report.json"), "report.zip");
}

TEST(ZipDefaultOutFileTest, HiddenFileKeepsItsDot)
{
    EXPECT_EQ(guchho::zip::DefaultOutFile(".bashrc"), ".bashrc.zip");
}

TEST(ZipDefaultOutFileTest, AlreadyAnArchiveIsStillNamedAfterItself)
{
    EXPECT_EQ(guchho::zip::DefaultOutFile("release.zip"), "release.zip");
}

// ===========================================================================
// The happy path
// ===========================================================================

TEST(ZipCreateTest, SingleFileIsStoredUnderItsOwnName)
{
    ZipTempDir dir("single");
    dir.Write("app.js", "console.log(1);\n");

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("app.js"), dir.At("out.zip")));

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

TEST(ZipCreateTest, DirectoryIsArchivedUnderItsOwnName)
{
    ZipTempDir dir("tree");
    dir.Write("dist/index.html", "<html></html>");
    dir.Write("dist/app.js", "let a = 1;\n");
    dir.Write("dist/assets/logo.svg", "<svg/>");
    dir.MakeDir("dist/empty");

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("dist"), dir.At("out.zip")));

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

TEST(ZipCreateTest, EntrySeparatorsAreForwardSlashesOnEveryPlatform)
{
    ZipTempDir dir("separators");
    dir.Write("dist/sub/leaf.txt", "leaf");

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("dist"), dir.At("out.zip")));
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

TEST(ZipCreateTest, HiddenFilesAreIncluded)
{
    ZipTempDir dir("hidden");
    dir.Write("proj/.gitignore", "*.log\n");
    dir.Write("proj/visible.txt", "hi");

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("proj"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "proj/.gitignore"));
}

TEST(ZipCreateTest, EmptyFileIsStored)
{
    ZipTempDir dir("empty-file");
    dir.Write("zero.bin", "");

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("zero.bin"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].contents, "");
}

TEST(ZipCreateTest, CurrentDirectoryHasNoPrefix)
{
    ZipTempDir dir("dot");
    dir.Write("index.html", "<html></html>");
    dir.Write("src/main.js", "main();");

    ScopedCwd cwd(PathFromUTF8(dir.path()));
    ASSERT_TRUE(cwd.ok());

    // "." names the tree this command is standing in, and there is no name
    // for it that would mean the same thing on another machine — so its
    // contents go to the top of the archive.
    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(".", dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "index.html"));
    EXPECT_TRUE(Contains(Names(entries), "src/main.js"));
    EXPECT_TRUE(Contains(Names(entries), "src/"));
}

TEST(ZipCreateTest, NestedInputUsesOnlyItsLastComponent)
{
    ZipTempDir dir("nested");
    dir.Write("a/b/dist/file.txt", "x");

    // "../../dist" and "./dist" and "dist" are three spellings of one root,
    // and none of them carries the journey to it into the archive.
    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("a/b/dist"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "dist/file.txt"));
    EXPECT_TRUE(Contains(Names(entries), "dist/"));
}

TEST(ZipCreateTest, ParentDirectoryComponentsNeverReachTheArchive)
{
    ZipTempDir dir("dotdot");
    dir.Write("a/inner/file.txt", "x");

    const std::string input = dir.At("a") + "/inner/../inner";
    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(input, dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    for (const ArchiveEntry& entry : entries) {
        EXPECT_TRUE(entry.name.find("..") == std::string::npos) << entry.name;
    }
    EXPECT_TRUE(Contains(Names(entries), "inner/file.txt"));
}

TEST(ZipCreateTest, MultipleInputsKeepDistinctRoots)
{
    ZipTempDir dir("multi");
    dir.Write("dist/app.js", "a");
    dir.Write("src/main.js", "b");

    guchho::zip::ZipOptions options;
    options.inputs  = {dir.At("dist"), dir.At("src")};
    options.outFile = dir.At("out.zip");

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    EXPECT_TRUE(Contains(Names(entries), "dist/app.js"));
    EXPECT_TRUE(Contains(Names(entries), "src/main.js"));
}

TEST(ZipCreateTest, UnicodeFilenamesSurvive)
{
    ZipTempDir dir("unicode");
    dir.Write("proj/café.txt", "buerger");
    dir.Write("proj/日本語.md", "nihongo");

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("proj"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;

    const ArchiveEntry* accented = Find(entries, "proj/café.txt");
    ASSERT_TRUE(accented != nullptr);
    EXPECT_EQ(accented->contents, "buerger");
    EXPECT_TRUE(Contains(Names(entries), "proj/日本語.md"));
}

TEST(ZipCreateTest, BinaryContentsRoundTripByteForByte)
{
    ZipTempDir dir("binary");
    std::string bytes;
    bytes.reserve(513);
    for (int i = 0; i < 513; i++) {
        bytes.push_back(static_cast<char>(i & 0xFF));
    }
    dir.Write("blob.bin", bytes);

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("blob.bin"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    ASSERT_EQ(entries[0].contents.size(), bytes.size());
    EXPECT_EQ(std::memcmp(entries[0].contents.data(), bytes.data(), bytes.size()), 0);
}

TEST(ZipCreateTest, LargeFileIsStreamedRatherThanHeldInMemory)
{
    ZipTempDir dir("large");

    // Big enough that a one-megabyte-at-a-time reader has to come back for
    // more than one buffer, which is the part of the streaming path a small
    // file never exercises.
    const std::string chunk(64 * 1024, 'z');
    {
        std::ofstream out(PathFromUTF8(dir.At("big.txt")), std::ios::binary);
        for (int i = 0; i < 40; i++) out << chunk;
    }

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("big.txt"), dir.At("out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].contents.size(), 64u * 1024u * 40u);
    EXPECT_EQ(entries[0].contents[0], 'z');
    EXPECT_EQ(entries[0].contents.back(), 'z');
}

TEST(ZipCreateTest, CompressionLevelsAreRecordedAndReadable)
{
    ZipTempDir dir("levels");
    std::string text;
    for (int i = 0; i < 4000; i++) text += "the quick brown fox jumps over the lazy dog\n";
    dir.Write("text.txt", text);

    guchho::zip::ZipOptions stored = OptionsFor(dir.At("text.txt"), dir.At("stored.zip"));
    stored.level = 0;
    guchho::zip::ZipResult stored_result = guchho::zip::CreateZip(stored);
    ASSERT_TRUE(stored_result.Ok()) << stored_result.error;

    guchho::zip::ZipOptions hard = OptionsFor(dir.At("text.txt"), dir.At("hard.zip"));
    hard.level = 9;
    guchho::zip::ZipResult hard_result = guchho::zip::CreateZip(hard);
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

TEST(ZipCreateTest, DateIsWrittenToEveryEntry)
{
    ZipTempDir dir("date");
    dir.Write("proj/a.txt", "a");
    dir.Write("proj/sub/b.txt", "b");
    dir.MakeDir("proj/sub");

    guchho::zip::ZipOptions options = OptionsFor(dir.At("proj"), dir.At("out.zip"));
    options.date = kTestDate;

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_FALSE(entries.empty());

    for (const ArchiveEntry& entry : entries) {
        EXPECT_EQ(static_cast<std::int64_t>(entry.mtime), kTestDate) << entry.name;
    }
}

TEST(ZipCreateTest, DateOutsideTheDosRangeIsRefused)
{
    ZipTempDir dir("date-range");
    dir.Write("a.txt", "a");

    // 1970 is before the format's first representable year. Wrapping would
    // record it as 2107 or thereabouts, so it is refused instead.
    guchho::zip::ZipOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.zip"));
    options.date = 0;

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("1980") != std::string::npos) << result.error;

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(ZipCreateTest, ModeIsWrittenToFilesAsUnixAttributes)
{
    ZipTempDir dir("mode");
    dir.Write("script.sh", "#!/bin/sh\n");

    guchho::zip::ZipOptions options = OptionsFor(dir.At("script.sh"), dir.At("out.zip"));
    options.mode = 0755u;

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
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

TEST(ZipCreateTest, DirectoryModeGainsExecuteBitsItWouldOtherwiseLack)
{
    ZipTempDir dir("dir-mode");
    dir.Write("proj/a.txt", "a");

    guchho::zip::ZipOptions options = OptionsFor(dir.At("proj"), dir.At("out.zip"));
    options.mode = 0644u;

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
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

TEST(ZipCreateTest, ModeOutsideThePermissionMaskIsRefused)
{
    ZipTempDir dir("mode-range");
    dir.Write("a.txt", "a");

    guchho::zip::ZipOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.zip"));
    options.mode = 0100777u;

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("0100777") != std::string::npos) << result.error;

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(ZipCreateTest, OmittedModeRecordsThePermissionsAlreadyOnDisk)
{
    ZipTempDir dir("default-mode");
    dir.Write("data.txt", "data");

    const std::string source = dir.At("data.txt");
    std::error_code   ec;
    fs::permissions(PathFromUTF8(source),
                    fs::perms::owner_read | fs::perms::owner_write, ec);
    ASSERT_FALSE(ec);

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(source, dir.At("out.zip")));
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

TEST(ZipCreateTest, NoInputsIsRefused)
{
    ZipTempDir dir("no-inputs");

    guchho::zip::ZipOptions options;
    options.outFile = dir.At("out.zip");

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_FALSE(result.note.empty());
    EXPECT_TRUE(result.path.empty());
}

TEST(ZipCreateTest, NoOutputFileIsRefused)
{
    ZipTempDir dir("no-output");
    dir.Write("a.txt", "a");

    guchho::zip::ZipOptions options;
    options.inputs = {dir.At("a.txt")};

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_FALSE(result.Ok());
}

TEST(ZipCreateTest, MissingInputIsNamed)
{
    ZipTempDir dir("missing");
    dir.Write("present.txt", "here");

    guchho::zip::ZipOptions options;
    options.inputs  = {dir.At("present.txt"), dir.At("absent.txt")};
    options.outFile = dir.At("out.zip");

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("absent.txt") != std::string::npos) << result.error;

    // Nothing was written: a run that failed on the second input must not
    // leave an archive that looks like it covered the first.
    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(ZipCreateTest, CompressionLevelOutOfRangeIsRefused)
{
    ZipTempDir dir("bad-level");
    dir.Write("a.txt", "a");

    for (int level : {-1, 10, 99}) {
        guchho::zip::ZipOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.zip"));
        options.level = level;

        guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
        ASSERT_FALSE(result.Ok()) << level;
        EXPECT_TRUE(result.error.find("out of range") != std::string::npos)
            << result.error;
    }

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(ZipCreateTest, ExistingOutputIsLeftAloneWithoutOverwrite)
{
    ZipTempDir dir("existing");
    dir.Write("a.txt", "new content");
    dir.Write("out.zip", "an old archive, or anything else");

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("a.txt"), dir.At("out.zip")));
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("already exists") != std::string::npos)
        << result.error;
    EXPECT_FALSE(result.note.empty());

    std::ifstream in(PathFromUTF8(dir.At("out.zip")), std::ios::binary);
    std::string   untouched((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(untouched, "an old archive, or anything else");
}

TEST(ZipCreateTest, ExistingOutputIsReplacedWithOverwrite)
{
    ZipTempDir dir("overwrite");
    dir.Write("a.txt", "new content");
    dir.Write("out.zip", "an old archive");

    guchho::zip::ZipOptions options = OptionsFor(dir.At("a.txt"), dir.At("out.zip"));
    options.overwrite = true;

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_TRUE(result.Ok()) << result.error;

    std::vector<ArchiveEntry> entries;
    std::string               error;
    ASSERT_TRUE(ReadArchive(result.path, entries, error)) << error;
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].contents, "new content");
}

TEST(ZipCreateTest, OutputThatIsADirectoryIsRefused)
{
    ZipTempDir dir("out-is-dir");
    dir.Write("a.txt", "a");
    dir.MakeDir("out.zip");

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("a.txt"), dir.At("out.zip")));
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("directory") != std::string::npos) << result.error;
}

TEST(ZipCreateTest, DuplicateArchivePathsAreRefused)
{
    ZipTempDir dir("duplicate");
    dir.Write("a/file.txt", "one");
    dir.Write("b/file.txt", "two");

    guchho::zip::ZipOptions options;
    options.inputs  = {dir.At("a"), dir.At("a")};
    options.outFile = dir.At("out.zip");

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("same archive path") != std::string::npos)
        << result.error;
    EXPECT_FALSE(result.note.empty());

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(ZipCreateTest, InputsWhoseRootsCollideAreRefused)
{
    ZipTempDir dir("collide");
    dir.Write("one/dist/app.js", "1");
    dir.Write("two/dist/app.js", "2");

    // Each input is stored under the name of its own last component, so two
    // different trees called "dist" would both claim "dist/" and one would
    // silently overwrite the other in the central directory.
    guchho::zip::ZipOptions options;
    options.inputs  = {dir.At("one/dist"), dir.At("two/dist")};
    options.outFile = dir.At("out.zip");

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("same archive path") != std::string::npos)
        << result.error;
    EXPECT_FALSE(result.note.empty());

    std::error_code ec;
    EXPECT_FALSE(fs::exists(PathFromUTF8(dir.At("out.zip")), ec));
}

TEST(ZipCreateTest, OutputInsideItsOwnInputTreeIsExcluded)
{
    ZipTempDir dir("self-output");
    dir.Write("dist/app.js", "a");
    dir.Write("dist/out.zip", "the archive from a previous run");

    guchho::zip::ZipOptions options = OptionsFor(dir.At("dist"), dir.At("dist/out.zip"));
    options.overwrite = true;

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
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

TEST(ZipCreateTest, OutputFileCannotAlsoBeAnInput)
{
    ZipTempDir dir("output-is-input");
    dir.Write("release.zip", "an existing archive");

    guchho::zip::ZipOptions options = OptionsFor(dir.At("release.zip"),
                                                 dir.At("release.zip"));
    options.overwrite = true;

    guchho::zip::ZipResult result = guchho::zip::CreateZip(options);
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("also an input") != std::string::npos)
        << result.error;
    EXPECT_FALSE(result.note.empty());
}

TEST(ZipCreateTest, OutputDirectoryIsCreatedWhenMissing)
{
    ZipTempDir dir("out-dir");
    dir.Write("a.txt", "a");

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("a.txt"), dir.At("build/deep/out.zip")));
    ASSERT_TRUE(result.Ok()) << result.error;

    std::error_code ec;
    EXPECT_TRUE(fs::exists(PathFromUTF8(result.path), ec));
}

TEST(ZipCreateTest, AFailedRunLeavesNoTemporaryFileBehind)
{
    ZipTempDir dir("temp-cleanup");
    dir.Write("tree/a.txt", "a");
    dir.Write("tree/b.txt", "b");

    // The second input does not exist, so the run fails after the first has
    // been walked and before a byte of archive has been written.
    guchho::zip::ZipOptions bad;
    bad.inputs  = {dir.At("tree"), dir.At("nope")};
    bad.outFile = dir.At("out.zip");
    ASSERT_FALSE(guchho::zip::CreateZip(bad).Ok());

    // And a run that succeeds over the same output must not find anything
    // left over from the one that did not.
    guchho::zip::ZipOptions good;
    good.inputs  = {dir.At("tree")};
    good.outFile = dir.At("out.zip");
    ASSERT_TRUE(guchho::zip::CreateZip(good).Ok());

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

TEST(ZipCreateTest, SymlinksInsideTheTreeAreSkippedWithAWarning)
{
    ZipTempDir dir("symlink-inside");
    dir.Write("tree/real.txt", "real");
    dir.Write("elsewhere.txt", "somewhere else");

    if (!dir.TrySymlink(dir.At("elsewhere.txt"), "tree/link.txt")) {
        // Creating a symlink needs privileges this runner may not have.
        return;
    }

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("tree"), dir.At("out.zip")));
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

TEST(ZipCreateTest, SymlinkNamedAsTheInputIsFollowedOnce)
{
    ZipTempDir dir("symlink-root");
    dir.Write("target/inside.txt", "reached");

    if (!dir.TrySymlink(dir.At("target"), "alias")) {
        return;
    }

    // Naming a link is naming what it points at, so the archive is rooted
    // at the name the person typed and holds the target's contents.
    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("alias"), dir.At("out.zip")));
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

TEST(ZipCreateTest, BrokenSymlinkInputIsReportedAsSuch)
{
    ZipTempDir dir("symlink-broken");
    dir.Write("real.txt", "here");

    if (!dir.TrySymlink(dir.At("does-not-exist.txt"), "dangling")) {
        return;
    }

    guchho::zip::ZipResult result =
        guchho::zip::CreateZip(OptionsFor(dir.At("dangling"), dir.At("out.zip")));
    ASSERT_FALSE(result.Ok());
    EXPECT_TRUE(result.error.find("does not resolve") != std::string::npos)
        << result.error;
}
