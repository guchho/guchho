// =============================================================================
// test/cli/cli_unpack_test.cpp — the command that extracts an archive
// =============================================================================

#include "guchho/cli.hpp"
#include "test/guchho_test.hpp"
#include "test/helpers/cli_test.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace {

using guchho::test::CliWorkspace;
using guchho::test::kBuildFailure;
using guchho::test::kSuccess;
using guchho::test::kUsageError;
using guchho::test::OutputContains;
using guchho::test::RunCli;

void MakeSource(CliWorkspace& workspace)
{
    workspace.MakeDir("srcdir");
    workspace.Write("srcdir/hello.txt", "hello world");
}

void Pack(const std::string& output, const std::string& format = "zip")
{
    const auto result = RunCli({"pack", "srcdir", "-o", output, "--format=" + format});
    ASSERT_EQ(result.exit_code, kSuccess);
}

} // namespace

TEST(CLIUnpack, Help)
{
    std::vector<std::string> args = {"guchho", "unpack", "--help"};
    const int code = guchho::cli::runUnpack(args);
    ASSERT_EQ(code, 0);
}

TEST(CLIUnpack, ExtractsIntoNamedOutdir)
{
    CliWorkspace workspace("named-outdir");
    MakeSource(workspace);
    Pack("out.zip");

    const auto result = RunCli({"unpack", "out.zip", "-o", "extracted"});

    ASSERT_EQ(result.exit_code, kSuccess);
    ASSERT_TRUE(OutputContains(result.out, "Extracted"));
    ASSERT_TRUE(workspace.Exists("extracted/srcdir/hello.txt"));
    ASSERT_EQ(workspace.Read("extracted/srcdir/hello.txt"), std::string("hello world"));
}

TEST(CLIUnpack, DefaultsOutdirToArchiveName)
{
    CliWorkspace workspace("default-outdir");
    MakeSource(workspace);
    Pack("release.zip");

    const auto result = RunCli({"unpack", "release.zip"});

    ASSERT_EQ(result.exit_code, kSuccess);
    ASSERT_TRUE(workspace.Exists("release/srcdir/hello.txt"));
}

TEST(CLIUnpack, FormatOptionReadsAnArchiveWithNoKnownExtension)
{
    CliWorkspace workspace("format");
    MakeSource(workspace);
    Pack("archive.bin", "tar");

    const auto result =
        RunCli({"unpack", "archive.bin", "--format=tar", "-o", "extracted"});

    ASSERT_EQ(result.exit_code, kSuccess);
    ASSERT_TRUE(workspace.Exists("extracted/srcdir/hello.txt"));
}

TEST(CLIUnpack, OverwriteIsOffByDefault)
{
    CliWorkspace workspace("overwrite");
    MakeSource(workspace);
    Pack("out.zip");

    ASSERT_EQ(RunCli({"unpack", "out.zip", "-o", "extracted"}).exit_code, kSuccess);
    workspace.Write("extracted/srcdir/hello.txt", "changed");

    const auto kept = RunCli({"unpack", "out.zip", "-o", "extracted"});
    ASSERT_EQ(kept.exit_code, kSuccess);
    ASSERT_TRUE(OutputContains(kept.err, "Skipped existing"));
    ASSERT_EQ(workspace.Read("extracted/srcdir/hello.txt"), std::string("changed"));

    const auto replaced =
        RunCli({"unpack", "out.zip", "-o", "extracted", "--overwrite"});
    ASSERT_EQ(replaced.exit_code, kSuccess);
    ASSERT_EQ(workspace.Read("extracted/srcdir/hello.txt"),
              std::string("hello world"));
}

TEST(CLIUnpack, StripComponentsRemovesLeadingDirectories)
{
    CliWorkspace workspace("strip");
    MakeSource(workspace);
    Pack("out.zip");

    const auto result =
        RunCli({"unpack", "out.zip", "-o", "extracted", "--strip-components", "1"});

    ASSERT_EQ(result.exit_code, kSuccess);
    ASSERT_TRUE(workspace.Exists("extracted/hello.txt"));
}

TEST(CLIUnpack, UsageErrorsReturnTwo)
{
    CliWorkspace workspace("usage");

    ASSERT_EQ(RunCli({"unpack"}).exit_code, kUsageError);

    const auto unknown = RunCli({"unpack", "x.zip", "--bogus"});
    ASSERT_EQ(unknown.exit_code, kUsageError);
    ASSERT_TRUE(OutputContains(unknown.err, "Unknown unpack flag"));

    const auto format = RunCli({"unpack", "x.zip", "--format=7z"});
    ASSERT_EQ(format.exit_code, kUsageError);
    ASSERT_TRUE(OutputContains(format.err, "Unsupported archive format"));

    const auto outdir = RunCli({"unpack", "x.zip", "-o"});
    ASSERT_EQ(outdir.exit_code, kUsageError);
    ASSERT_TRUE(OutputContains(outdir.err, "Missing value"));

    const auto strip = RunCli({"unpack", "x.zip", "--strip-components=nope"});
    ASSERT_EQ(strip.exit_code, kUsageError);
    ASSERT_TRUE(OutputContains(strip.err, "Invalid value"));

    const auto extra = RunCli({"unpack", "a.zip", "b.zip"});
    ASSERT_EQ(extra.exit_code, kUsageError);
    ASSERT_TRUE(OutputContains(extra.err, "Unexpected argument"));
}

TEST(CLIUnpack, MissingArchiveReturnsOne)
{
    CliWorkspace workspace("missing");

    const auto result = RunCli({"unpack", "missing.zip", "-o", "extracted"});

    ASSERT_EQ(result.exit_code, kBuildFailure);
    ASSERT_TRUE(OutputContains(result.err, "does not exist"));
}
