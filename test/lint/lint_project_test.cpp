#include "test/guchho_test.hpp"
#include "test/helpers/filesystem_test.hpp"

#include "guchho/lint.hpp"

#include <string>
#include <unordered_map>
#include <vector>

using guchho::filesystem::MockKind;
using namespace guchho::lint;

namespace {

std::unique_ptr<guchho::filesystem::Fs> ProjectFs() {
    return guchho::test::MakeMockFS({
        {"/project/src/a.js", "var a = 1;\n"},
        {"/project/src/b.js", "var b = 2;\n"},
        {"/project/web/c.js", "var c = 3;\n"},
        {"/project/node_modules/x.js", "var x = 4;\n"},
    }, MockKind::kUnix, "/project");
}

Project MakeProject() {
    Project project;
    project.config = DefaultConfig();
    project.config.rules["import/no-unresolved"] = Severity::kOff;
    project.options.cwd = "/project";
    return project;
}

Project MakeImportProject() {
    Project project = MakeProject();
    project.config.rules["import/no-unresolved"] = Severity::kError;
    project.options.paths = {"src/a.js"};
    return project;
}

std::vector<const Diagnostic*> Unresolved(const Report& report) {
    std::vector<const Diagnostic*> matches;
    for (const FileReport& file : report.files) {
        for (const Diagnostic& diagnostic : file.diagnostics) {
            if (diagnostic.rule_id == "import/no-unresolved") {
                matches.push_back(&diagnostic);
            }
        }
    }
    return matches;
}

} // namespace

TEST(LintProject, ReportsFilesSortedByRelativePath) {
    auto fs = ProjectFs();
    Project project = MakeProject();
    project.options.paths = {"src"};
    Report report;
    std::string error;
    ASSERT_TRUE(LintProject(*fs, project, report, error));
    EXPECT_TRUE(error.empty());
    ASSERT_EQ(report.files.size(), 2u);
    EXPECT_EQ(report.files[0].path, "src/a.js");
    EXPECT_EQ(report.files[1].path, "src/b.js");
    EXPECT_EQ(report.warnings, 2);
    EXPECT_EQ(report.errors, 0);
}

TEST(LintProject, DefaultWalkHonorsIncludeAndExclude) {
    auto fs = ProjectFs();
    Project project = MakeProject();
    project.config.include = {"src/**"};
    project.config.exclude = {"src/b.js"};
    Report report;
    std::string error;
    ASSERT_TRUE(LintProject(*fs, project, report, error));
    ASSERT_EQ(report.files.size(), 1u);
    EXPECT_EQ(report.files.front().path, "src/a.js");
}

TEST(LintProject, NoMatchingFilesIsNotAnOperationalError) {
    auto fs = guchho::test::MakeMockFS({
        {"/project/readme.txt", "nothing here"},
    }, MockKind::kUnix, "/project");
    Project project = MakeProject();
    Report report;
    std::string error;
    EXPECT_TRUE(LintProject(*fs, project, report, error));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(report.files.empty());
}

TEST(LintProject, RelativeAndTypeScriptImportsResolve) {
    auto fs = guchho::test::MakeMockFS({
        {"/project/src/a.js",
         "import { b } from \"./b\";\nimport { t } from \"./types\";\nb; t;\n"},
        {"/project/src/b.js", "export const b = 1;\n"},
        {"/project/src/types.ts", "export const t = 2;\n"},
    }, MockKind::kUnix, "/project");
    Project project = MakeImportProject();
    Report report;
    std::string error;
    ASSERT_TRUE(LintProject(*fs, project, report, error));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(Unresolved(report).empty());
}

TEST(LintProject, BareScopedAndSubpathPackagesResolve) {
    auto fs = guchho::test::MakeMockFS({
        {"/project/src/a.js",
         "import \"dep\";\nimport \"@scope/pkg\";\nimport \"@scope/pkg/extra.js\";\n"},
        {"/project/node_modules/dep/package.json",
         R"({"name":"dep","main":"./index.js"})"},
        {"/project/node_modules/dep/index.js", "export const dep = 1;\n"},
        {"/project/node_modules/@scope/pkg/package.json",
         R"({"name":"@scope/pkg","main":"./index.js"})"},
        {"/project/node_modules/@scope/pkg/index.js", "export const pkg = 1;\n"},
        {"/project/node_modules/@scope/pkg/extra.js", "export const extra = 1;\n"},
    }, MockKind::kUnix, "/project");
    Project project = MakeImportProject();
    Report report;
    std::string error;
    ASSERT_TRUE(LintProject(*fs, project, report, error));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(Unresolved(report).empty());
}

TEST(LintProject, PackageExportsBlockPrivateSubpaths) {
    auto fs = guchho::test::MakeMockFS({
        {"/project/src/a.js",
         "import \"pkg/public\";\nimport \"pkg/private\";\n"},
        {"/project/node_modules/pkg/package.json",
         R"({"name":"pkg","exports":{".":"./index.js","./public":"./public.js"}})"},
        {"/project/node_modules/pkg/index.js", "export const root = 1;\n"},
        {"/project/node_modules/pkg/public.js", "export const visible = 1;\n"},
        {"/project/node_modules/pkg/private.js", "export const hidden = 1;\n"},
    }, MockKind::kUnix, "/project");
    Project project = MakeImportProject();
    Report report;
    std::string error;
    ASSERT_TRUE(LintProject(*fs, project, report, error));
    EXPECT_TRUE(error.empty());
    std::vector<const Diagnostic*> unresolved = Unresolved(report);
    ASSERT_EQ(unresolved.size(), 1u);
    EXPECT_TRUE(unresolved.front()->message.find("pkg/private") !=
                std::string::npos);
}

TEST(LintProject, BuiltinNodeModulesFollowTheConfiguredPlatform) {
    const std::unordered_map<std::string, std::string> files = {
        {"/project/src/a.js", "import fs from \"fs\";\nfs;\n"},
    };
    {
        auto fs = guchho::test::MakeMockFS(files, MockKind::kUnix, "/project");
        Project project = MakeImportProject();
        Report report;
        std::string error;
        ASSERT_TRUE(LintProject(*fs, project, report, error));
        EXPECT_EQ(Unresolved(report).size(), 1u);
    }
    {
        auto fs = guchho::test::MakeMockFS(files, MockKind::kUnix, "/project");
        Project project = MakeImportProject();
        project.config.platform = guchho::config::Platform::kNode;
        Report report;
        std::string error;
        ASSERT_TRUE(LintProject(*fs, project, report, error));
        EXPECT_TRUE(Unresolved(report).empty());
    }
}

TEST(LintProject, MissingImportsReportTheirLocationOnce) {
    auto fs = guchho::test::MakeMockFS({
        {"/project/src/a.js",
         "import \"./missing\";\nimport \"not-installed\";\n"},
    }, MockKind::kUnix, "/project");
    Project project = MakeImportProject();
    Report report;
    std::string error;
    ASSERT_TRUE(LintProject(*fs, project, report, error));
    EXPECT_TRUE(error.empty());
    std::vector<const Diagnostic*> unresolved = Unresolved(report);
    ASSERT_EQ(unresolved.size(), 2u);
    EXPECT_EQ(unresolved[0]->line, 1);
    EXPECT_EQ(unresolved[1]->line, 2);
    EXPECT_EQ(unresolved[0]->column, 8);
    EXPECT_EQ(unresolved[1]->column, 8);
}

TEST(LintProject, FixDryRunCountsFixesWithoutChangingTheSource) {
    auto fs = guchho::test::MakeMockFS({
        {"/project/src/a.js", "var a = 1;\nif (typeof a == \"number\") { a; }\n"},
    }, MockKind::kUnix, "/project");
    Project project = MakeProject();
    project.options.paths = {"src/a.js"};
    project.options.fix_dry_run = true;
    Report report;
    std::string error;
    ASSERT_TRUE(LintProject(*fs, project, report, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(report.fixes, 1);
    ASSERT_EQ(report.files.size(), 1u);
    auto contents = fs->ReadFile("/project/src/a.js");
    ASSERT_TRUE(contents.Ok());
    EXPECT_NE(contents.value.find("a == \"number\""), std::string::npos);
}

TEST(LintProject, ReadFailureIsAnOperationalError) {
    auto fs = guchho::test::MakeMockFS({}, MockKind::kUnix, "/project");
    Project project = MakeProject();
    project.options.paths = {"missing.js"};
    Report report;
    std::string error;
    EXPECT_FALSE(LintProject(*fs, project, report, error));
    EXPECT_FALSE(error.empty());
}

TEST(LintApplyFixes, AppliesNonOverlappingFixesInOrder) {
    const std::string source = "a == b; c == d;";
    Diagnostic diagnostic;
    diagnostic.fixes.push_back(guchho::logger::Fix{
        guchho::logger::Range{guchho::logger::Loc{2}, 2}, "===",
        guchho::logger::FixSafety::kSafe, ""});
    diagnostic.fixes.push_back(guchho::logger::Fix{
        guchho::logger::Range{guchho::logger::Loc{10}, 2}, "===",
        guchho::logger::FixSafety::kSafe, ""});

    std::string output;
    int applied = 0;
    std::string error;
    ASSERT_TRUE(ApplyFixes("a.js", source, {diagnostic}, output, applied, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(applied, 2);
    EXPECT_EQ(output, "a === b; c === d;");
}

TEST(LintApplyFixes, SkipsUnsafeFixesAndOverlaps) {
    const std::string source = "abc";
    Diagnostic diagnostic;
    diagnostic.fixes.push_back(guchho::logger::Fix{
        guchho::logger::Range{guchho::logger::Loc{0}, 2}, "x",
        guchho::logger::FixSafety::kRequiresJudgment, ""});
    diagnostic.fixes.push_back(guchho::logger::Fix{
        guchho::logger::Range{guchho::logger::Loc{0}, 2}, "y",
        guchho::logger::FixSafety::kSafe, ""});
    diagnostic.fixes.push_back(guchho::logger::Fix{
        guchho::logger::Range{guchho::logger::Loc{1}, 1}, "z",
        guchho::logger::FixSafety::kSafe, ""});

    std::string output;
    int applied = 0;
    std::string error;
    ASSERT_TRUE(ApplyFixes("a.js", source, {diagnostic}, output, applied, error));
    EXPECT_EQ(applied, 1);
    EXPECT_EQ(output, "yc");
}

TEST(LintApplyFixes, ClampsRangesToTheSourceLength) {
    const std::string source = "abc";
    Diagnostic diagnostic;
    diagnostic.fixes.push_back(guchho::logger::Fix{
        guchho::logger::Range{guchho::logger::Loc{2}, 99}, "z",
        guchho::logger::FixSafety::kSafe, ""});

    std::string output;
    int applied = 0;
    std::string error;
    ASSERT_TRUE(ApplyFixes("a.js", source, {diagnostic}, output, applied, error));
    EXPECT_EQ(applied, 1);
    EXPECT_EQ(output, "abz");
}
