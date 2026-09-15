#include "test/guchho_test.hpp"
#include "guchho/filesystem.hpp"
#include "test/helpers/filesystem_test.hpp"

#include <string>
#include <string_view>

using guchho::filesystem::DirEntries;
using guchho::filesystem::Entry;
using guchho::filesystem::EntryKind;
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
using guchho::filesystem::ParseYarnPnPVirtualPath;
using guchho::filesystem::MangleYarnPnPVirtualPath;
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

// ===========================================================================
// DirEntries
// ===========================================================================

TEST(DirEntriesTest, PeekEntryCountEmpty)
{
    DirEntries entries;
    entries.data = std::map<std::string, std::shared_ptr<Entry>>{};
    entries.dir  = "/empty";
    EXPECT_EQ(entries.PeekEntryCount(), 0);
}

TEST(DirEntriesTest, PeekEntryCountNonEmpty)
{
    auto e1 = std::make_shared<Entry>();
    e1->base = "a.txt";
    auto e2 = std::make_shared<Entry>();
    e2->base = "b.txt";

    DirEntries entries;
    entries.data = std::map<std::string, std::shared_ptr<Entry>>{
        {"a.txt", e1},
        {"b.txt", e2},
    };
    entries.dir = "/dir";
    EXPECT_EQ(entries.PeekEntryCount(), 2);
}

TEST(DirEntriesTest, SortedKeysReturnsSortedNames)
{
    auto e1 = std::make_shared<Entry>();
    e1->base = "c.txt";
    auto e2 = std::make_shared<Entry>();
    e2->base = "a.txt";
    auto e3 = std::make_shared<Entry>();
    e3->base = "b.txt";

    DirEntries entries;
    entries.data = std::map<std::string, std::shared_ptr<Entry>>{
        {"c.txt", e1},
        {"a.txt", e2},
        {"b.txt", e3},
    };
    entries.dir = "/dir";

    auto keys = entries.SortedKeys();
    EXPECT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], "a.txt");
    EXPECT_EQ(keys[1], "b.txt");
    EXPECT_EQ(keys[2], "c.txt");
}

TEST(DirEntriesTest, GetReturnsMatchingEntry)
{
    auto e1 = std::make_shared<Entry>();
    e1->base = "readme.md";

    DirEntries entries;
    entries.data = std::map<std::string, std::shared_ptr<Entry>>{
        {"readme.md", e1},
    };
    entries.dir = "/project";

    auto [entry, diff] = entries.Get("readme.md");
    EXPECT_TRUE(entry != nullptr);
    EXPECT_EQ(entry->base, "readme.md");
    EXPECT_FALSE(diff.has_value());
}

TEST(DirEntriesTest, GetMissingEntryReturnsNullptr)
{
    DirEntries entries;
    entries.data = std::map<std::string, std::shared_ptr<Entry>>{};
    entries.dir = "/empty";

    auto [entry, diff] = entries.Get("missing.txt");
    EXPECT_TRUE(entry == nullptr);
    EXPECT_FALSE(diff.has_value());
}

TEST(MakeEmptyDirEntriesTest, CreatesEmptyDirEntries)
{
    auto entries = MakeEmptyDirEntries("/nonexistent");
    EXPECT_FALSE(entries.data.has_value());
    EXPECT_EQ(entries.dir, "/nonexistent");
}

// ===========================================================================
// Fs (via MakeMockFS)
// ===========================================================================

TEST(FsTest, OpenFileExisting)
{
    auto fs = MakeMockFS({
        {"/hello.txt", "hello world"},
    }, MockKind::kUnix, "/");

    auto result = fs->OpenFile("/hello.txt");
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value->Len(), 11);
    EXPECT_EQ(result.value->Read(0, 5), "hello");
    EXPECT_EQ(result.value->Read(6, 11), "world");
    EXPECT_TRUE(result.value->Close());
}

TEST(FsTest, OpenFileMissing)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    auto result = fs->OpenFile("/missing.txt");
    EXPECT_FALSE(result.Ok());
}

TEST(FsTest, GetWatchDataThrows)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    bool threw = false;
    try {
        fs->GetWatchData();
    } catch (const std::logic_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(FsTest, CwdUnix)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/project");
    EXPECT_EQ(fs->Cwd(), "/project");
}

TEST(FsTest, CwdWindows)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\project");
    EXPECT_EQ(fs->Cwd(), "C:\\project");
}

TEST(FsTest, IsAbsUnix)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    EXPECT_TRUE(fs->IsAbs("/src/main.cpp"));
    EXPECT_FALSE(fs->IsAbs("src/main.cpp"));
}

TEST(FsTest, IsAbsWindows)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    EXPECT_TRUE(fs->IsAbs("C:\\src\\main.cpp"));
    EXPECT_FALSE(fs->IsAbs("src\\main.cpp"));
}

TEST(FsTest, AbsUnix)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/project");
    EXPECT_EQ(*fs->Abs("src/main.cpp"), "/src/main.cpp");
    EXPECT_EQ(*fs->Abs("/etc/passwd"), "/etc/passwd");
}

TEST(FsTest, DirUnix)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    EXPECT_EQ(fs->Dir("/src/main.cpp"), "/src");
    EXPECT_EQ(fs->Dir("/a/b/c"), "/a/b");
}

TEST(FsTest, DirWindows)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    EXPECT_EQ(fs->Dir("C:\\src\\main.cpp"), "C:\\src");
}

TEST(FsTest, BaseUnix)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    EXPECT_EQ(fs->Base("/src/main.cpp"), "main.cpp");
}

TEST(FsTest, BaseWindows)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    EXPECT_EQ(fs->Base("C:\\src\\main.cpp"), "main.cpp");
}

TEST(FsTest, ExtUnix)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    EXPECT_EQ(fs->Ext("/src/main.cpp"), ".cpp");
    EXPECT_EQ(fs->Ext("/src/Makefile"), "");
}

TEST(FsTest, ExtWindows)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    EXPECT_EQ(fs->Ext("C:\\src\\main.cpp"), ".cpp");
}

TEST(FsTest, JoinUnix)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    EXPECT_EQ(fs->Join({"/src", "main.cpp"}), "/src/main.cpp");
}

TEST(FsTest, JoinWindows)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    EXPECT_EQ(fs->Join({"C:\\src", "main.cpp"}), "C:\\src\\main.cpp");
}

TEST(FsTest, EvalSymlinksUnix)
{
    auto fs = MakeMockFS({}, MockKind::kUnix, "/");
    EXPECT_EQ(*fs->EvalSymlinks("/a/b/../c"), "/a/c");
}

TEST(FsTest, EvalSymlinksWindows)
{
    auto fs = MakeMockFS({}, MockKind::kWindows, "C:\\");
    EXPECT_EQ(*fs->EvalSymlinks("C:\\a\\b\\..\\c"), "C:\\a\\c");
}

// ===========================================================================
// InMemoryOpenedFile
// ===========================================================================

TEST(InMemoryOpenedFileTest, LenReturnsContentSize)
{
    InMemoryOpenedFile file;
    file.contents = "test data";
    EXPECT_EQ(file.Len(), 9);
}

TEST(InMemoryOpenedFileTest, LenEmpty)
{
    InMemoryOpenedFile file;
    file.contents = "";
    EXPECT_EQ(file.Len(), 0);
}

TEST(InMemoryOpenedFileTest, ReadFullRange)
{
    InMemoryOpenedFile file;
    file.contents = "abcdefg";
    EXPECT_EQ(file.Read(0, 7), "abcdefg");
}

TEST(InMemoryOpenedFileTest, ReadPartialRange)
{
    InMemoryOpenedFile file;
    file.contents = "abcdefg";
    EXPECT_EQ(file.Read(2, 5), "cde");
}

TEST(InMemoryOpenedFileTest, ReadPastEnd)
{
    InMemoryOpenedFile file;
    file.contents = "abcdefg";
    EXPECT_EQ(file.Read(5, 99), "fg");
}

TEST(InMemoryOpenedFileTest, ReadAtEnd)
{
    InMemoryOpenedFile file;
    file.contents = "abcdefg";
    EXPECT_EQ(file.Read(7, 10), "");
}

TEST(InMemoryOpenedFileTest, CloseReturnsTrue)
{
    InMemoryOpenedFile file;
    file.contents = "data";
    EXPECT_TRUE(file.Close());
}

TEST(InMemoryOpenedFileTest, IsOpenedFile)
{
    InMemoryOpenedFile file;
    file.contents = "hello";
    OpenedFile* base = &file;
    EXPECT_EQ(base->Len(), 5);
    EXPECT_EQ(base->Read(0, 5), "hello");
    EXPECT_TRUE(base->Close());
}

// ===========================================================================
// GoFilepath
// ===========================================================================

TEST(GoFilepathTest, IsAbsUnix)
{
    GoFilepath fp;
    fp.cwd         = "/project";
    fp.is_windows  = false;
    fp.path_separator = '/';
    EXPECT_TRUE(fp.IsAbs("/src/main.cpp"));
    EXPECT_FALSE(fp.IsAbs("src/main.cpp"));
}

TEST(GoFilepathTest, IsAbsWindows)
{
    GoFilepath fp;
    fp.cwd         = "C:\\project";
    fp.is_windows  = true;
    fp.path_separator = '\\';
    EXPECT_TRUE(fp.IsAbs("C:\\src\\main.cpp"));
    EXPECT_TRUE(fp.IsAbs("\\\\server\\share"));
    EXPECT_FALSE(fp.IsAbs("src\\main.cpp"));
}

TEST(GoFilepathTest, AbsUnix)
{
    GoFilepath fp;
    fp.cwd         = "/project";
    fp.is_windows  = false;
    fp.path_separator = '/';
    EXPECT_EQ(fp.Abs("src/main.cpp"), "/project/src/main.cpp");
    EXPECT_EQ(fp.Abs("/etc/passwd"), "/etc/passwd");
}

TEST(GoFilepathTest, BaseUnix)
{
    GoFilepath fp;
    fp.is_windows = false;
    fp.path_separator = '/';
    EXPECT_EQ(fp.Base("/src/main.cpp"), "main.cpp");
    EXPECT_EQ(fp.Base("/usr/bin/"), "bin");
}

TEST(GoFilepathTest, DirUnix)
{
    GoFilepath fp;
    fp.is_windows = false;
    fp.path_separator = '/';
    EXPECT_EQ(fp.Dir("/src/main.cpp"), "/src");
    EXPECT_EQ(fp.Dir("/a/b/c"), "/a/b");
}

TEST(GoFilepathTest, ExtUnix)
{
    GoFilepath fp;
    fp.is_windows = false;
    fp.path_separator = '/';
    EXPECT_EQ(fp.Ext("/src/main.cpp"), ".cpp");
    EXPECT_EQ(fp.Ext("/src/Makefile"), "");
}

TEST(GoFilepathTest, CleanUnix)
{
    GoFilepath fp;
    fp.is_windows = false;
    fp.path_separator = '/';
    EXPECT_EQ(fp.Clean("/a/b/../c/./d"), "/a/c/d");
    EXPECT_EQ(fp.Clean("a//b"), "a/b");
}

TEST(GoFilepathTest, JoinUnix)
{
    GoFilepath fp;
    fp.is_windows = false;
    fp.path_separator = '/';
    EXPECT_EQ(fp.Join({"/src", "main.cpp"}), "/src/main.cpp");
}

TEST(GoFilepathTest, RelUnix)
{
    GoFilepath fp;
    fp.cwd         = "/";
    fp.is_windows  = false;
    fp.path_separator = '/';
    EXPECT_EQ(*fp.Rel("/a/b", "/a/b/c/d"), "c/d");
    EXPECT_EQ(*fp.Rel("/a/b", "/a/b"), ".");
}

TEST(GoFilepathTest, IsPathSeparator)
{
    GoFilepath fp;
    fp.is_windows = false;
    EXPECT_TRUE(fp.IsPathSeparator('/'));
    EXPECT_FALSE(fp.IsPathSeparator('\\'));

    fp.is_windows = true;
    EXPECT_TRUE(fp.IsPathSeparator('/'));
    EXPECT_TRUE(fp.IsPathSeparator('\\'));
}

TEST(GoFilepathTest, SameWordUnix)
{
    GoFilepath fp;
    fp.is_windows = false;
    EXPECT_TRUE(fp.SameWord("abc", "abc"));
    EXPECT_FALSE(fp.SameWord("ABC", "abc"));
}

TEST(GoFilepathTest, SameWordWindows)
{
    GoFilepath fp;
    fp.is_windows = true;
    EXPECT_TRUE(fp.SameWord("README.md", "readme.md"));
    EXPECT_FALSE(fp.SameWord("README.md", "OTHER.md"));
}

TEST(GoFilepathTest, VolumeNameLenUnix)
{
    GoFilepath fp;
    fp.is_windows = false;
    EXPECT_EQ(fp.VolumeNameLen("/usr/bin"), 0);
}

TEST(GoFilepathTest, VolumeNameLenWindows)
{
    GoFilepath fp;
    fp.is_windows = true;
    EXPECT_EQ(fp.VolumeNameLen("C:\\src\\main.cpp"), 2);
}

// ===========================================================================
// YarnPnPVirtualPath
// ===========================================================================

TEST(YarnPnPVirtualPathTest, StructFields)
{
    YarnPnPVirtualPath vp;
    vp.prefix = "/a/c/d.js";
    vp.suffix = "c/d.js";
    EXPECT_EQ(vp.prefix, "/a/c/d.js");
    EXPECT_EQ(vp.suffix, "c/d.js");
}

TEST(YarnPnPVirtualPathTest, ParseDunderVirtual)
{
    auto result = ParseYarnPnPVirtualPath("/a/b/__virtual__/abc123/2/c/d.js");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->prefix, "/a/c/d.js");
    EXPECT_EQ(result->suffix, "c/d.js");
}

TEST(YarnPnPVirtualPathTest, ParseDoubleVirtual)
{
    auto result = ParseYarnPnPVirtualPath("/a/b/$$virtual/abc123/2/c/d.js");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->prefix, "/a/c/d.js");
    EXPECT_EQ(result->suffix, "c/d.js");
}

TEST(YarnPnPVirtualPathTest, ParseDepthZero)
{
    auto result = ParseYarnPnPVirtualPath("/a/b/__virtual__/hash/0/c/d.js");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->prefix, "/a/b");
    EXPECT_EQ(result->suffix, "c/d.js");
}

TEST(YarnPnPVirtualPathTest, ParseDepthExceedsParents)
{
    auto result = ParseYarnPnPVirtualPath("/a/__virtual__/hash/10/c/d.js");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->prefix, "/");
    EXPECT_EQ(result->suffix, "c/d.js");
}

TEST(YarnPnPVirtualPathTest, ParseEmptySuffix)
{
    auto result = ParseYarnPnPVirtualPath("/a/b/__virtual__/hash/2");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->prefix, "/a");
    EXPECT_EQ(result->suffix, "");
}

TEST(YarnPnPVirtualPathTest, ParseWindowsStyle)
{
    auto result = ParseYarnPnPVirtualPath("C:\\a\\b\\__virtual__\\hash\\2\\c\\d.js");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->prefix, "C:\\a\\c\\d.js");
    EXPECT_EQ(result->suffix, "c\\d.js");
}

TEST(YarnPnPVirtualPathTest, ParseRelativePath)
{
    auto result = ParseYarnPnPVirtualPath("a/b/__virtual__/hash/1/c/d.js");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->prefix, "a/c/d.js");
    EXPECT_EQ(result->suffix, "c/d.js");
}

TEST(YarnPnPVirtualPathTest, ParseNonVirtualPath)
{
    EXPECT_FALSE(ParseYarnPnPVirtualPath("/src/main.cpp").has_value());
}

TEST(YarnPnPVirtualPathTest, ParseNoDepthAfterHash)
{
    EXPECT_FALSE(ParseYarnPnPVirtualPath("/a/b/__virtual__/hashonly").has_value());
}

TEST(YarnPnPVirtualPathTest, ParseNonNumericDepth)
{
    EXPECT_FALSE(ParseYarnPnPVirtualPath("/a/b/__virtual__/hash/abc/c/d.js").has_value());
}

TEST(YarnPnPVirtualPathTest, MangleVirtualPath)
{
    EXPECT_EQ(MangleYarnPnPVirtualPath("/a/b/__virtual__/abc123/2/c/d.js"), "/a/c/d.js");
}

TEST(YarnPnPVirtualPathTest, MangleDoubleVirtualPath)
{
    EXPECT_EQ(MangleYarnPnPVirtualPath("/a/b/$$virtual/abc123/2/c/d.js"), "/a/c/d.js");
}

TEST(YarnPnPVirtualPathTest, MangleNonVirtualPath)
{
    EXPECT_EQ(MangleYarnPnPVirtualPath("/src/main.cpp"), "/src/main.cpp");
}

// ===========================================================================
// WatchData
// ===========================================================================

TEST(WatchDataTest, DefaultIsEmpty)
{
    WatchData wd;
    EXPECT_TRUE(wd.paths.empty());
}

TEST(WatchDataTest, AddPath)
{
    WatchData wd;
    wd.paths["/src/main.cpp"] = []() -> std::string { return "/src/main.cpp"; };
    EXPECT_EQ(wd.paths.size(), 1u);
    EXPECT_EQ(wd.paths["/src/main.cpp"](), "/src/main.cpp");
}

TEST(WatchDataTest, AddMultiplePaths)
{
    WatchData wd;
    wd.paths["/a"] = []() -> std::string { return "/a"; };
    wd.paths["/b"] = []() -> std::string { return "/b"; };
    wd.paths["/c"] = []() -> std::string { return "/c"; };
    EXPECT_EQ(wd.paths.size(), 3u);
}
