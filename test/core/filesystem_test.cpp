#include "test/guchho_test.hpp"
#include "guchho/filesystem.hpp"
#include "test/helpers/filesystem_test.hpp"

#include <string>
#include <string_view>

using guchho::filesystem::DirEntries;
using guchho::filesystem::Entry;
using guchho::filesystem::EntryKind;
using guchho::filesystem::Fs;
using guchho::filesystem::FsResult;
using guchho::filesystem::GoFilepath;
using guchho::filesystem::InMemoryOpenedFile;
using guchho::filesystem::MakeEmptyDirEntries;
using guchho::filesystem::MockKind;
using guchho::filesystem::ModKey;
using guchho::filesystem::ModKeyResult;
using guchho::filesystem::OpenedFile;
using guchho::filesystem::YarnPnPVirtualPath;
using guchho::filesystem::WatchData;
using guchho::test::MakeMockFS;

// ===========================================================================
// EntryKind enum
// ===========================================================================

TEST(EntryKindTest, EnumValues)
{
    EXPECT_EQ(static_cast<uint8_t>(EntryKind::kInvalid), 0u);
    EXPECT_EQ(static_cast<uint8_t>(EntryKind::kDir), 1u);
    EXPECT_EQ(static_cast<uint8_t>(EntryKind::kFile), 2u);
}

// ===========================================================================
// MockKind enum
// ===========================================================================

TEST(MockKindTest, EnumValues)
{
    EXPECT_EQ(static_cast<uint8_t>(MockKind::kUnix), 0u);
    EXPECT_EQ(static_cast<uint8_t>(MockKind::kWindows), 1u);
}

// ===========================================================================
// kModKeySafetyGap
// ===========================================================================

TEST(KModKeySafetyGapTest, Value)
{
    EXPECT_EQ(guchho::filesystem::kModKeySafetyGap, 3);
}

// ===========================================================================
// FsResult
// ===========================================================================

TEST(FsResultTest, OkResult)
{
    FsResult<int> r;
    r.value = 42;
    EXPECT_TRUE(r.Ok());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value, 42);
}

TEST(FsResultTest, ErrorResult)
{
    FsResult<int> r;
    r.canonical_error = std::errc::no_such_file_or_directory;
    r.original_error = "not found";
    EXPECT_FALSE(r.Ok());
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(FsResultTest, DefaultValueIsEmpty)
{
    FsResult<std::string> r;
    EXPECT_TRUE(r.Ok());
    EXPECT_EQ(r.value, "");
}

TEST(FsResultTest, OriginalErrorEmptyOnSuccess)
{
    FsResult<int> r;
    r.value = 99;
    EXPECT_TRUE(r.Ok());
    EXPECT_TRUE(r.original_error.empty());
}

// ===========================================================================
// ModKey
// ===========================================================================

TEST(ModKeyTest, DefaultValues)
{
    ModKey key;
    EXPECT_EQ(key.inode, 0u);
    EXPECT_EQ(key.size, 0);
    EXPECT_EQ(key.mtime_sec, 0);
    EXPECT_EQ(key.mtime_nsec, 0);
    EXPECT_EQ(key.mode, 0u);
    EXPECT_EQ(key.uid, 0u);
}

TEST(ModKeyTest, EqualityOperator)
{
    ModKey a{1, 100, 1000, 500, 0644, 1000};
    ModKey b{1, 100, 1000, 500, 0644, 1000};
    ModKey c{2, 100, 1000, 500, 0644, 1000};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(ModKeyTest, InequalityDifferentSize)
{
    ModKey a{1, 100, 1000, 0, 0644, 1000};
    ModKey b{1, 200, 1000, 0, 0644, 1000};
    EXPECT_FALSE(a == b);
}

TEST(ModKeyTest, InequalityDifferentMtime)
{
    ModKey a{1, 100, 1000, 0, 0644, 1000};
    ModKey b{1, 100, 2000, 0, 0644, 1000};
    EXPECT_FALSE(a == b);
}

TEST(ModKeyTest, InequalityDifferentMode)
{
    ModKey a{1, 100, 1000, 0, 0644, 1000};
    ModKey b{1, 100, 1000, 0, 0755, 1000};
    EXPECT_FALSE(a == b);
}

TEST(ModKeyTest, AllFieldsDifferent)
{
    ModKey a{1, 100, 1000, 0, 0644, 1000};
    ModKey b{2, 200, 2000, 0, 0755, 2000};
    EXPECT_FALSE(a == b);
}

// ===========================================================================
// ModKeyResult
// ===========================================================================

TEST(ModKeyResultTest, OkResult)
{
    ModKeyResult r;
    r.value = ModKey{1, 100, 1000, 0, 0644, 1000};
    EXPECT_TRUE(r.Ok());
    EXPECT_FALSE(r.unusable);
}

TEST(ModKeyResultTest, UnusableResult)
{
    ModKeyResult r;
    r.unusable = true;
    EXPECT_FALSE(r.Ok());
    EXPECT_TRUE(r.unusable);
}

TEST(ModKeyResultTest, ErrorResult)
{
    ModKeyResult r;
    r.canonical_error = std::errc::no_such_file_or_directory;
    EXPECT_FALSE(r.Ok());
    EXPECT_FALSE(r.unusable);
}

TEST(ModKeyResultTest, ErrorAndUnusable)
{
    ModKeyResult r;
    r.canonical_error = std::errc::io_error;
    r.unusable = true;
    EXPECT_FALSE(r.Ok());
    EXPECT_TRUE(r.unusable);
}

TEST(ModKeyResultTest, DefaultIsOk)
{
    ModKeyResult r;
    EXPECT_TRUE(r.Ok());
    EXPECT_FALSE(r.unusable);
    EXPECT_TRUE(r.original_error.empty());
}

// ===========================================================================
// MockFS Basic Unix (port of TestMockFSBasicUnix)
// ===========================================================================

TEST(MockFSBasicUnixTest, ReadFileMissing)
{
    auto fs = MakeMockFS({
        {"/README.md",    "// README.md"},
        {"/package.json", "// package.json"},
        {"/src/index.js", "// src/index.js"},
        {"/src/util.js",  "// src/util.js"},
    }, MockKind::kUnix, "/");

    auto result = fs->ReadFile("/missing.txt");
    EXPECT_FALSE(result.Ok());
}

TEST(MockFSBasicUnixTest, ReadFileExisting)
{
    auto fs = MakeMockFS({
        {"/README.md",    "// README.md"},
        {"/package.json", "// package.json"},
        {"/src/index.js", "// src/index.js"},
        {"/src/util.js",  "// src/util.js"},
    }, MockKind::kUnix, "/");

    auto result = fs->ReadFile("/README.md");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value, "// README.md");
}

TEST(MockFSBasicUnixTest, ReadFileNested)
{
    auto fs = MakeMockFS({
        {"/README.md",    "// README.md"},
        {"/package.json", "// package.json"},
        {"/src/index.js", "// src/index.js"},
        {"/src/util.js",  "// src/util.js"},
    }, MockKind::kUnix, "/");

    auto result = fs->ReadFile("/src/index.js");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value, "// src/index.js");
}

TEST(MockFSBasicUnixTest, ReadDirectoryMissing)
{
    auto fs = MakeMockFS({
        {"/README.md",    "// README.md"},
        {"/package.json", "// package.json"},
        {"/src/index.js", "// src/index.js"},
        {"/src/util.js",  "// src/util.js"},
    }, MockKind::kUnix, "/");

    auto result = fs->ReadDirectory("/missing");
    EXPECT_FALSE(result.Ok());
}

TEST(MockFSBasicUnixTest, ReadDirectoryNested)
{
    auto fs = MakeMockFS({
        {"/README.md",    "// README.md"},
        {"/package.json", "// package.json"},
        {"/src/index.js", "// src/index.js"},
        {"/src/util.js",  "// src/util.js"},
    }, MockKind::kUnix, "/");

    auto result = fs->ReadDirectory("/src");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value.PeekEntryCount(), 2);

    auto [indexEntry, indexDiff] = result.value.Get("index.js");
    auto [utilEntry, utilDiff]   = result.value.Get("util.js");
    EXPECT_TRUE(indexEntry != nullptr);
    EXPECT_FALSE(indexDiff.has_value());
    EXPECT_EQ(indexEntry->Kind(*fs), EntryKind::kFile);
    EXPECT_TRUE(utilEntry != nullptr);
    EXPECT_FALSE(utilDiff.has_value());
    EXPECT_EQ(utilEntry->Kind(*fs), EntryKind::kFile);
}

TEST(MockFSBasicUnixTest, ReadDirectoryTopLevel)
{
    auto fs = MakeMockFS({
        {"/README.md",    "// README.md"},
        {"/package.json", "// package.json"},
        {"/src/index.js", "// src/index.js"},
        {"/src/util.js",  "// src/util.js"},
    }, MockKind::kUnix, "/");

    auto result = fs->ReadDirectory("/");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value.PeekEntryCount(), 3);

    auto [srcEntry, srcDiff]             = result.value.Get("src");
    auto [readmeEntry, readmeDiff]       = result.value.Get("README.md");
    auto [packageEntry, packageDiff]     = result.value.Get("package.json");
    EXPECT_TRUE(srcEntry != nullptr);
    EXPECT_FALSE(srcDiff.has_value());
    EXPECT_EQ(srcEntry->Kind(*fs), EntryKind::kDir);
    EXPECT_TRUE(readmeEntry != nullptr);
    EXPECT_FALSE(readmeDiff.has_value());
    EXPECT_EQ(readmeEntry->Kind(*fs), EntryKind::kFile);
    EXPECT_TRUE(packageEntry != nullptr);
    EXPECT_FALSE(packageDiff.has_value());
    EXPECT_EQ(packageEntry->Kind(*fs), EntryKind::kFile);
}

// ===========================================================================
// MockFS Basic Windows (port of TestMockFSBasicWindows)
// ===========================================================================

TEST(MockFSBasicWindowsTest, ReadFileMissing)
{
    auto fs = MakeMockFS({
        {"C:\\README.md",       "// README.md"},
        {"C:\\package.json",    "// package.json"},
        {"C:\\src\\index.js",   "// src/index.js"},
        {"C:\\src\\util.js",    "// src/util.js"},
        {"D:\\other\\file.txt", "// other/file.txt"},
    }, MockKind::kWindows, "C:\\");

    auto result = fs->ReadFile("C:\\missing.txt");
    EXPECT_FALSE(result.Ok());
}

TEST(MockFSBasicWindowsTest, ReadFileExisting)
{
    auto fs = MakeMockFS({
        {"C:\\README.md",       "// README.md"},
        {"C:\\package.json",    "// package.json"},
        {"C:\\src\\index.js",   "// src/index.js"},
        {"C:\\src\\util.js",    "// src/util.js"},
        {"D:\\other\\file.txt", "// other/file.txt"},
    }, MockKind::kWindows, "C:\\");

    auto result = fs->ReadFile("C:\\README.md");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value, "// README.md");
}

TEST(MockFSBasicWindowsTest, ReadFileNested)
{
    auto fs = MakeMockFS({
        {"C:\\README.md",       "// README.md"},
        {"C:\\package.json",    "// package.json"},
        {"C:\\src\\index.js",   "// src/index.js"},
        {"C:\\src\\util.js",    "// src/util.js"},
        {"D:\\other\\file.txt", "// other/file.txt"},
    }, MockKind::kWindows, "C:\\");

    auto result = fs->ReadFile("C:\\src\\index.js");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value, "// src/index.js");
}

TEST(MockFSBasicWindowsTest, ReadFileOtherDrive)
{
    auto fs = MakeMockFS({
        {"C:\\README.md",       "// README.md"},
        {"C:\\package.json",    "// package.json"},
        {"C:\\src\\index.js",   "// src/index.js"},
        {"C:\\src\\util.js",    "// src/util.js"},
        {"D:\\other\\file.txt", "// other/file.txt"},
    }, MockKind::kWindows, "C:\\");

    auto result = fs->ReadFile("D:\\other\\file.txt");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value, "// other/file.txt");
}

TEST(MockFSBasicWindowsTest, ReadFileCrossDriveMissing)
{
    auto fs = MakeMockFS({
        {"C:\\README.md",       "// README.md"},
        {"C:\\package.json",    "// package.json"},
        {"C:\\src\\index.js",   "// src/index.js"},
        {"C:\\src\\util.js",    "// src/util.js"},
        {"D:\\other\\file.txt", "// other/file.txt"},
    }, MockKind::kWindows, "C:\\");

    auto result = fs->ReadFile("C:\\other\\file.txt");
    EXPECT_FALSE(result.Ok());
}

TEST(MockFSBasicWindowsTest, ReadDirectoryMissing)
{
    auto fs = MakeMockFS({
        {"C:\\README.md",       "// README.md"},
        {"C:\\package.json",    "// package.json"},
        {"C:\\src\\index.js",   "// src/index.js"},
        {"C:\\src\\util.js",    "// src/util.js"},
        {"D:\\other\\file.txt", "// other/file.txt"},
    }, MockKind::kWindows, "C:\\");

    auto result = fs->ReadDirectory("C:\\missing");
    EXPECT_FALSE(result.Ok());
}

TEST(MockFSBasicWindowsTest, ReadDirectoryNested)
{
    auto fs = MakeMockFS({
        {"C:\\README.md",       "// README.md"},
        {"C:\\package.json",    "// package.json"},
        {"C:\\src\\index.js",   "// src/index.js"},
        {"C:\\src\\util.js",    "// src/util.js"},
        {"D:\\other\\file.txt", "// other/file.txt"},
    }, MockKind::kWindows, "C:\\");

    auto result = fs->ReadDirectory("C:\\src");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value.PeekEntryCount(), 2);

    auto [indexEntry, indexDiff] = result.value.Get("index.js");
    auto [utilEntry, utilDiff]   = result.value.Get("util.js");
    EXPECT_TRUE(indexEntry != nullptr);
    EXPECT_FALSE(indexDiff.has_value());
    EXPECT_EQ(indexEntry->Kind(*fs), EntryKind::kFile);
    EXPECT_TRUE(utilEntry != nullptr);
    EXPECT_FALSE(utilDiff.has_value());
    EXPECT_EQ(utilEntry->Kind(*fs), EntryKind::kFile);
}

TEST(MockFSBasicWindowsTest, ReadDirectoryOtherDrive)
{
    auto fs = MakeMockFS({
        {"C:\\README.md",       "// README.md"},
        {"C:\\package.json",    "// package.json"},
        {"C:\\src\\index.js",   "// src/index.js"},
        {"C:\\src\\util.js",    "// src/util.js"},
        {"D:\\other\\file.txt", "// other/file.txt"},
    }, MockKind::kWindows, "C:\\");

    auto result = fs->ReadDirectory("D:\\other");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value.PeekEntryCount(), 1);

    auto [fileEntry, fileDiff] = result.value.Get("file.txt");
    EXPECT_TRUE(fileEntry != nullptr);
    EXPECT_FALSE(fileDiff.has_value());
    EXPECT_EQ(fileEntry->Kind(*fs), EntryKind::kFile);
}

TEST(MockFSBasicWindowsTest, ReadDirectoryTopLevel)
{
    auto fs = MakeMockFS({
        {"C:\\README.md",       "// README.md"},
        {"C:\\package.json",    "// package.json"},
        {"C:\\src\\index.js",   "// src/index.js"},
        {"C:\\src\\util.js",    "// src/util.js"},
        {"D:\\other\\file.txt", "// other/file.txt"},
    }, MockKind::kWindows, "C:\\");

    auto result = fs->ReadDirectory("C:\\");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value.PeekEntryCount(), 3);

    auto [srcEntry, srcDiff]         = result.value.Get("src");
    auto [readmeEntry, readmeDiff]   = result.value.Get("README.md");
    auto [packageEntry, packageDiff] = result.value.Get("package.json");
    EXPECT_TRUE(srcEntry != nullptr);
    EXPECT_FALSE(srcDiff.has_value());
    EXPECT_EQ(srcEntry->Kind(*fs), EntryKind::kDir);
    EXPECT_TRUE(readmeEntry != nullptr);
    EXPECT_FALSE(readmeDiff.has_value());
    EXPECT_EQ(readmeEntry->Kind(*fs), EntryKind::kFile);
    EXPECT_TRUE(packageEntry != nullptr);
    EXPECT_FALSE(packageDiff.has_value());
    EXPECT_EQ(packageEntry->Kind(*fs), EntryKind::kFile);
}

// ===========================================================================
// MockFS Rel Unix (port of TestMockFSRelUnix)
// ===========================================================================

TEST(MockFSRelUnixTest, SamePath)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("/a/b", "/a/b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, ".");
}

TEST(MockFSRelUnixTest, Child)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("/a/b", "/a/b/c");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "c");
}

TEST(MockFSRelUnixTest, Grandchild)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("/a/b", "/a/b/c/d");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "c/d");
}

TEST(MockFSRelUnixTest, Parent)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("/a/b/c", "/a/b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..");
}

TEST(MockFSRelUnixTest, Grandparent)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("/a/b/c/d", "/a/b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "../..");
}

TEST(MockFSRelUnixTest, Sibling)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("/a/b/c", "/a/b/x");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "../x");
}

TEST(MockFSRelUnixTest, SiblingDeep)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("/a/b/c/d", "/a/b/x");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "../../x");
}

TEST(MockFSRelUnixTest, Nibling)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("/a/b/c", "/a/b/x/y");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "../x/y");
}

TEST(MockFSRelUnixTest, NiblingDeep)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("/a/b/c/d", "/a/b/x/y");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "../../x/y");
}

TEST(MockFSRelUnixTest, RelativeParent)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("a/b", "a/c");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "../c");
}

TEST(MockFSRelUnixTest, RelativeDotParent)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel("./a/b", "./a/c");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "../c");
}

TEST(MockFSRelUnixTest, DotToRelative)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel(".", "./a/b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a/b");
}

TEST(MockFSRelUnixTest, DotToRelativeDoubleSlash)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel(".", ".//a/b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a/b");
}

TEST(MockFSRelUnixTest, DotToRelativeDotSlash)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel(".", "././a/b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a/b");
}

TEST(MockFSRelUnixTest, DotToRelativeMixed)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->Rel(".", "././/a/b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a/b");
}

// ===========================================================================
// MockFS Rel Windows (port of TestMockFSRelWindows)
// ===========================================================================

TEST(MockFSRelWindowsTest, SamePath)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b", "C:\\a\\b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, ".");
}

TEST(MockFSRelWindowsTest, Child)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b", "C:\\a\\b\\c");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "c");
}

TEST(MockFSRelWindowsTest, Grandchild)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b", "C:\\a\\b\\c\\d");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "c\\d");
}

TEST(MockFSRelWindowsTest, Parent)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b\\c", "C:\\a\\b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..");
}

TEST(MockFSRelWindowsTest, Grandparent)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b\\c\\d", "C:\\a\\b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..\\..");
}

TEST(MockFSRelWindowsTest, Sibling)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b\\c", "C:\\a\\b\\x");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..\\x");
}

TEST(MockFSRelWindowsTest, SiblingDeep)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b\\c\\d", "C:\\a\\b\\x");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..\\..\\x");
}

TEST(MockFSRelWindowsTest, Nibling)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b\\c", "C:\\a\\b\\x\\y");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..\\x\\y");
}

TEST(MockFSRelWindowsTest, NiblingDeep)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b\\c\\d", "C:\\a\\b\\x\\y");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..\\..\\x\\y");
}

TEST(MockFSRelWindowsTest, RelativeParent)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("a\\b", "a\\c");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..\\c");
}

TEST(MockFSRelWindowsTest, RelativeDotParent)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel(".\\a\\b", ".\\a\\c");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..\\c");
}

TEST(MockFSRelWindowsTest, DotToRelative)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel(".", ".\\a\\b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a\\b");
}

TEST(MockFSRelWindowsTest, DotToRelativeDoubleSlash)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel(".", ".\\\\a\\b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a\\b");
}

TEST(MockFSRelWindowsTest, DotToRelativeDotSlash)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel(".", ".\\.\\a\\b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a\\b");
}

TEST(MockFSRelWindowsTest, DotToRelativeMixed)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel(".", ".\\.\\\\a\\b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a\\b");
}

TEST(MockFSRelWindowsTest, CrossDriveAbsolute)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a\\b", "\\a\\b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, ".");
}

TEST(MockFSRelWindowsTest, CrossDriveUNC)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("\\a", "\\b");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "..\\b");
}

TEST(MockFSRelWindowsTest, CrossDriveDifferentVolumes)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    auto result = fs->Rel("C:\\a", "D:\\a");
    EXPECT_FALSE(result.has_value());
}
