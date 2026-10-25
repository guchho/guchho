#include "test/guchho_test.hpp"
#include "test/helpers/filesystem_test.hpp"

#include "guchho/lint.hpp"

#include <algorithm>
#include <string>
#include <vector>

using guchho::filesystem::MockKind;
using guchho::lint::Config;
using guchho::lint::DefaultConfig;
using guchho::lint::detail::DiscoverFiles;
using guchho::lint::detail::GlobMatch;
using guchho::lint::detail::PatternCoversDirectory;
using guchho::lint::detail::ValidateGlobPattern;

namespace {

std::unique_ptr<guchho::filesystem::Fs> ProjectFs() {
    return guchho::test::MakeMockFS({
        {"/project/src/a.js", "var a = 1;"},
        {"/project/src/b.ts", "const b = 1;"},
        {"/project/src/c.css", ".c {}"},
        {"/project/web/e.js", "var e = 1;"},
        {"/project/node_modules/x.js", "var x = 1;"},
        {"/project/dist/d.js", "var d = 1;"},
        {"/project/.git/g.js", "var g = 1;"},
    }, MockKind::kUnix, "/project");
}

bool Contains(const std::vector<std::string>& files, const std::string& suffix) {
    return std::any_of(files.begin(), files.end(), [&](const std::string& file) {
        return file.size() >= suffix.size() &&
               file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0;
    });
}

} // namespace

TEST(LintGlob, MatchesFilesAndDirectories) {
    EXPECT_TRUE(GlobMatch("*.js", "a.js"));
    EXPECT_TRUE(GlobMatch("*.js", "src/a.js"));
    EXPECT_FALSE(GlobMatch("*.js", "src/a.ts"));
    EXPECT_TRUE(GlobMatch("src/*.js", "src/a.js"));
    EXPECT_FALSE(GlobMatch("src/*.js", "src/deep/a.js"));
    EXPECT_TRUE(GlobMatch("src/**/*.js", "src/deep/a.js"));
    EXPECT_TRUE(GlobMatch("src/**/*.js", "src/a.js"));
    EXPECT_TRUE(GlobMatch("src/*.{js,ts}", "src/a.ts"));
    EXPECT_TRUE(GlobMatch("src/[ab].js", "src/b.js"));
    EXPECT_FALSE(GlobMatch("src/[!ab].js", "src/a.js"));
    EXPECT_TRUE(GlobMatch("src/[a-z].js", "src/z.js"));
}

TEST(LintGlob, PatternCoveringADirectoryIsRecognized) {
    EXPECT_TRUE(PatternCoversDirectory("vendor/**", "vendor"));
    EXPECT_TRUE(PatternCoversDirectory("vendor/*", "vendor"));
    EXPECT_TRUE(PatternCoversDirectory("vendor", "vendor"));
    EXPECT_FALSE(PatternCoversDirectory("vendor/**", "src"));
}

TEST(LintGlob, ValidationAcceptsTheSupportedSyntax) {
    std::string error;
    EXPECT_TRUE(ValidateGlobPattern("src/**/*.{js,ts}", error));
    EXPECT_TRUE(ValidateGlobPattern("src/[!a-c].js", error));
    EXPECT_TRUE(error.empty());
}

TEST(LintGlob, ValidationRejectsUnsupportedSyntax) {
    std::string error;
    EXPECT_FALSE(ValidateGlobPattern("src/+(a|b).js", error));
    EXPECT_NE(error.find("unsupported character"), std::string::npos);

    error.clear();
    EXPECT_FALSE(ValidateGlobPattern("src/\\*.js", error));
    EXPECT_NE(error.find("escape sequences"), std::string::npos);

    error.clear();
    EXPECT_FALSE(ValidateGlobPattern("src/*.{js,ts", error));
    EXPECT_NE(error.find("unbalanced"), std::string::npos);
}

TEST(LintDiscovery, DefaultWalkFiltersExtensionsAndVendorDirectories) {
    auto fs = ProjectFs();
    Config config = DefaultConfig();
    std::vector<std::string> files;
    std::string error;
    ASSERT_TRUE(DiscoverFiles(*fs, config, {}, "/project", files, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(files.size(), 3u);
    EXPECT_TRUE(Contains(files, "src/a.js"));
    EXPECT_TRUE(Contains(files, "src/b.ts"));
    EXPECT_TRUE(Contains(files, "web/e.js"));
    EXPECT_FALSE(Contains(files, "node_modules/x.js"));
    EXPECT_FALSE(Contains(files, "dist/d.js"));
    EXPECT_FALSE(Contains(files, ".git/g.js"));
    EXPECT_FALSE(Contains(files, "src/c.css"));
}

TEST(LintDiscovery, ExplicitFileMustHaveASupportedExtension) {
    auto fs = ProjectFs();
    Config config = DefaultConfig();
    std::vector<std::string> files;
    std::string error;
    EXPECT_FALSE(DiscoverFiles(*fs, config, {"src/c.css"}, "/project", files, error));
    EXPECT_NE(error.find("unsupported source file"), std::string::npos);
}

TEST(LintDiscovery, ExplicitDirectoryIsWalked) {
    auto fs = ProjectFs();
    Config config = DefaultConfig();
    std::vector<std::string> files;
    std::string error;
    ASSERT_TRUE(DiscoverFiles(*fs, config, {"src"}, "/project", files, error));
    ASSERT_EQ(files.size(), 2u);
    EXPECT_TRUE(Contains(files, "src/a.js"));
    EXPECT_TRUE(Contains(files, "src/b.ts"));
}

TEST(LintDiscovery, GlobUsesTheConfiguredRoot) {
    auto fs = ProjectFs();
    Config config = DefaultConfig();
    std::vector<std::string> files;
    std::string error;
    ASSERT_TRUE(DiscoverFiles(*fs, config, {"src/*.{js,ts}"}, "/project",
                              files, error));
    ASSERT_EQ(files.size(), 2u);
    EXPECT_TRUE(Contains(files, "src/a.js"));
    EXPECT_TRUE(Contains(files, "src/b.ts"));
}

TEST(LintDiscovery, GlobWithoutMatchesIsAnError) {
    auto fs = ProjectFs();
    Config config = DefaultConfig();
    std::vector<std::string> files;
    std::string error;
    EXPECT_FALSE(DiscoverFiles(*fs, config, {"src/*.jsx"}, "/project", files,
                               error));
    EXPECT_NE(error.find("no files matched"), std::string::npos);
}

TEST(LintDiscovery, IncludeLimitsTheDefaultWalk) {
    auto fs = ProjectFs();
    Config config = DefaultConfig();
    config.include = {"src/**"};
    std::vector<std::string> files;
    std::string error;
    ASSERT_TRUE(DiscoverFiles(*fs, config, {}, "/project", files, error));
    ASSERT_EQ(files.size(), 2u);
    EXPECT_TRUE(Contains(files, "src/a.js"));
    EXPECT_TRUE(Contains(files, "src/b.ts"));
}

TEST(LintDiscovery, ExcludeRemovesWholeDirectories) {
    auto fs = ProjectFs();
    Config config = DefaultConfig();
    config.exclude = {"web/**"};
    std::vector<std::string> files;
    std::string error;
    ASSERT_TRUE(DiscoverFiles(*fs, config, {}, "/project", files, error));
    EXPECT_FALSE(Contains(files, "web/e.js"));
    EXPECT_TRUE(Contains(files, "src/a.js"));
}

TEST(LintDiscovery, MissingPathIsAnError) {
    auto fs = ProjectFs();
    Config config = DefaultConfig();
    std::vector<std::string> files;
    std::string error;
    EXPECT_FALSE(DiscoverFiles(*fs, config, {"missing.js"}, "/project", files,
                               error));
    EXPECT_NE(error.find("does not exist"), std::string::npos);
}
