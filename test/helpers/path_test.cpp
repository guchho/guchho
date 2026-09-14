#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <string>
#include <utility>
#include <vector>

using guchho::helpers::IsInsideNodeModules;
using guchho::helpers::IsFileURL;
using guchho::helpers::FileURLFromFilePath;
using guchho::helpers::FilePathFromFileURL;
using guchho::helpers::SplitPathSegments;
using guchho::helpers::MakeRelativePath;
using guchho::helpers::AddDotSlashPrefix;
using guchho::helpers::IsPublicPathConfigured;
using guchho::helpers::JoinPublicPath;

// ---------------------------------------------------------------------------
// IsInsideNodeModules
// ---------------------------------------------------------------------------

TEST(IsInsideNodeModulesTest, Empty)
{
    EXPECT_FALSE(IsInsideNodeModules(""));
}

TEST(IsInsideNodeModulesTest, BareNodeModules)
{
    EXPECT_FALSE(IsInsideNodeModules("node_modules"));
}

TEST(IsInsideNodeModulesTest, NestedPath)
{
    EXPECT_TRUE(IsInsideNodeModules("src/node_modules/pkg/index.js"));
}

TEST(IsInsideNodeModulesTest, WindowsBackslash)
{
    EXPECT_TRUE(IsInsideNodeModules("src\\node_modules\\pkg\\index.js"));
}

TEST(IsInsideNodeModulesTest, NoMatch)
{
    EXPECT_FALSE(IsInsideNodeModules("vendor/lib.js"));
    EXPECT_FALSE(IsInsideNodeModules("src/nodes_modules/x.js"));
}

// ---------------------------------------------------------------------------
// IsFileURL
// ---------------------------------------------------------------------------

TEST(IsFileURLTest, BasicTrue)
{
    EXPECT_TRUE(IsFileURL("file", "", "/home/user/file.js"));
    EXPECT_TRUE(IsFileURL("file", "localhost", "/tmp/a.txt"));
}

TEST(IsFileURLTest, WrongScheme)
{
    EXPECT_FALSE(IsFileURL("http", "", "/index.html"));
}

TEST(IsFileURLTest, NonEmptyHost)
{
    EXPECT_FALSE(IsFileURL("file", "example.com", "/a.txt"));
}

TEST(IsFileURLTest, EmptyPath)
{
    EXPECT_FALSE(IsFileURL("file", "", ""));
}

TEST(IsFileURLTest, PathWithoutLeadingSlash)
{
    EXPECT_FALSE(IsFileURL("file", "", "tmp/a.txt"));
}

// ---------------------------------------------------------------------------
// FileURLFromFilePath
// ---------------------------------------------------------------------------

TEST(FileURLFromFilePathTest, POSIXPath)
{
    EXPECT_EQ(FileURLFromFilePath("/home/user/file.js"),
              "file:///home/user/file.js");
}

TEST(FileURLFromFilePathTest, WindowsBackslash)
{
    EXPECT_EQ(FileURLFromFilePath("C:\\Users\\test\\a.txt"),
              "file:///C:/Users/test/a.txt");
}

TEST(FileURLFromFilePathTest, AlreadyHasLeadingSlash)
{
    // path starting with '/' should not get a double slash.
    EXPECT_EQ(FileURLFromFilePath("/tmp/a.txt"),
              "file:///tmp/a.txt");
}

// ---------------------------------------------------------------------------
// FilePathFromFileURL
// ---------------------------------------------------------------------------

TEST(FilePathFromFileURLTest, POSIXUnchanged)
{
    EXPECT_EQ(FilePathFromFileURL("/home/user/file.js", "/home/user"),
              "/home/user/file.js");
}

TEST(FilePathFromFileURLTest, WindowsStripsLeadingSlash)
{
    // On Windows the leading '/' is stripped and all forward slashes
    // become backslashes.
    EXPECT_EQ(FilePathFromFileURL("/C:/Users/test/a.txt", "C:\\"),
              "C:\\Users\\test\\a.txt");
}

TEST(FilePathFromFileURLTest, EmptyCwdTreatedAsWindows)
{
    EXPECT_EQ(FilePathFromFileURL("/tmp/a.txt", ""),
              "tmp\\a.txt");
}

// ---------------------------------------------------------------------------
// SplitPathSegments
// ---------------------------------------------------------------------------

TEST(SplitPathSegmentsTest, Basic)
{
    EXPECT_EQ(SplitPathSegments("a/b/c"),
              (std::vector<std::string>{"a", "b", "c"}));
}

TEST(SplitPathSegmentsTest, DropsEmptyAndDot)
{
    EXPECT_EQ(SplitPathSegments("a//b/./c"),
              (std::vector<std::string>{"a", "b", "c"}));
}

TEST(SplitPathSegmentsTest, KeepsDoubleDot)
{
    EXPECT_EQ(SplitPathSegments("a/../b"),
              (std::vector<std::string>{"a", "..", "b"}));
}

TEST(SplitPathSegmentsTest, EmptyPath)
{
    EXPECT_EQ(SplitPathSegments(""), (std::vector<std::string>{}));
}

TEST(SplitPathSegmentsTest, TrailingSlash)
{
    EXPECT_EQ(SplitPathSegments("a/b/"),
              (std::vector<std::string>{"a", "b"}));
}

// ---------------------------------------------------------------------------
// MakeRelativePath
// ---------------------------------------------------------------------------

TEST(MakeRelativePathTest, SameDirectory)
{
    // When both paths share the same directory and file name, the file
    // name relative to its parent directory is returned.
    EXPECT_EQ(MakeRelativePath("dist/admin/index.html",
                               "dist/admin/index.html"),
              "index.html");
}

TEST(MakeRelativePathTest, SiblingFile)
{
    EXPECT_EQ(MakeRelativePath("dist/admin/index.html",
                               "dist/assets/app.js"),
              "../assets/app.js");
}

TEST(MakeRelativePathTest, NoCommonPrefix)
{
    EXPECT_EQ(MakeRelativePath("src/main.cpp",
                               "test/main.cpp"),
              "../test/main.cpp");
}

// ---------------------------------------------------------------------------
// AddDotSlashPrefix
// ---------------------------------------------------------------------------

TEST(AddDotSlashPrefixTest, BareRelativePath)
{
    EXPECT_EQ(AddDotSlashPrefix("app.js"), "./app.js");
}

TEST(AddDotSlashPrefixTest, AlreadyHasDotSlash)
{
    // Only "../", ".", "..", and "/" are recognized as already having a
    // prefix — "./" is not one of them, so a second prefix is added.
    EXPECT_EQ(AddDotSlashPrefix("./app.js"), "././app.js");
}

TEST(AddDotSlashPrefixTest, ParentTraversal)
{
    EXPECT_EQ(AddDotSlashPrefix("../lib/a.js"), "../lib/a.js");
}

TEST(AddDotSlashPrefixTest, DotAlone)
{
    EXPECT_EQ(AddDotSlashPrefix("."), ".");
}

TEST(AddDotSlashPrefixTest, DoubleDotAlone)
{
    EXPECT_EQ(AddDotSlashPrefix(".."), "..");
}

TEST(AddDotSlashPrefixTest, RootRelative)
{
    EXPECT_EQ(AddDotSlashPrefix("/assets/app.js"), "/assets/app.js");
}

// ---------------------------------------------------------------------------
// IsPublicPathConfigured
// ---------------------------------------------------------------------------

TEST(IsPublicPathConfiguredTest, Empty)
{
    EXPECT_FALSE(IsPublicPathConfigured(""));
}

TEST(IsPublicPathConfiguredTest, DotSlash)
{
    EXPECT_FALSE(IsPublicPathConfigured("./"));
}

TEST(IsPublicPathConfiguredTest, Dot)
{
    EXPECT_FALSE(IsPublicPathConfigured("."));
}

TEST(IsPublicPathConfiguredTest, ValidPath)
{
    EXPECT_TRUE(IsPublicPathConfigured("/app/"));
    EXPECT_TRUE(IsPublicPathConfigured("https://cdn.example.com/"));
}

// ---------------------------------------------------------------------------
// JoinPublicPath
// ---------------------------------------------------------------------------

TEST(JoinPublicPathTest, NoPublicPath)
{
    EXPECT_EQ(JoinPublicPath("", "app.js"), "app.js");
    EXPECT_EQ(JoinPublicPath(".", "app.js"), "app.js");
    EXPECT_EQ(JoinPublicPath("./", "app.js"), "app.js");
}

TEST(JoinPublicPathTest, WithTrailingSlash)
{
    EXPECT_EQ(JoinPublicPath("/app/", "assets/app.js"),
              "/app/assets/app.js");
}

TEST(JoinPublicPathTest, WithoutTrailingSlash)
{
    EXPECT_EQ(JoinPublicPath("/app", "assets/app.js"),
              "/app/assets/app.js");
}

TEST(JoinPublicPathTest, StripsLeadingDotSlash)
{
    EXPECT_EQ(JoinPublicPath("/app/", "./assets/app.js"),
              "/app/assets/app.js");
}

TEST(JoinPublicPathTest, LeadingSlashNotStripped)
{
    // JoinPublicPath only strips "./" and empty segments — a leading "/"
    // in rel_path is not removed, producing a double slash.
    EXPECT_EQ(JoinPublicPath("/app/", "/assets/app.js"),
              "/app//assets/app.js");
}