// Tests for the questions the command line answers about itself.
//
// Everything here is a property of the whole argument list rather than of any
// one command: the empty run, the two forms of help, the two forms of version,
// and the moment where a flag with no command word in front of it is handed to
// the build. They live together because they are all decided in one place —
// runImpl in cli_run.cpp, before any command file is reached — and a change to
// that function moves all of them at once.
//
// The exit codes are the point of most of these. A build tool's exit code is
// the part a script reads, and the four numbers in cli.hpp are a contract with
// whoever wrote the script: 0 for a success and for the help that is not a
// failure, 1 for a build that ran and failed, 2 for a command line that was
// rejected before anything ran. Nothing below asserts on a literal; each one
// names the code it means through the header's constants.
//
// What is not tested here: the banner. It is the first thing a person sees and
// it is the one piece of a run that is deliberately full of decoration, so the
// assertions check that the text a person reads for a decision is there — the
// usage, the command list, the version — rather than comparing the whole thing.

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <string>
#include <vector>

namespace cli::test {

using guchho::test::CliWorkspace;
using guchho::test::kBuildFailure;
using guchho::test::kSuccess;
using guchho::test::kUsageError;
using guchho::test::OutputContains;
using guchho::test::RunCli;

namespace {

// The eight words the usage text lists, in the order it lists them. Each is
// checked by name so that a command dropped from the list is a test that fails
// on its own rather than a diff nobody reads.
const std::vector<std::string> kCommandWords = {
    "build", "dev", "serve", "watch", "init", "clean", "info", "transform",
};

} // namespace

// ---------------------------------------------------------------------------
// The empty run
// ---------------------------------------------------------------------------

// A bare "guchho" is a question, not a mistake, and the answer is the usage
// text with a success code. A build tool that treats it as an error makes a
// script that runs the command to check a version report a failure the script
// then has to interpret.
TEST(CliRun, NoArgumentsPrintsUsageAndSucceeds) {
    CliWorkspace ws("empty");

    const guchho::test::CliResult result = RunCli({});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage:"));
    EXPECT_TRUE(result.err.empty());
}

// The same answer for the two spellings of the request, and the reason the
// version flag is not answered here: both forms have to mean help at the top
// level or they collide with the build grammar's own short forms.
TEST(CliRun, HelpFlagPrintsUsage) {
    CliWorkspace ws("help");

    EXPECT_EQ(RunCli({"--help"}).exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(RunCli({"--help"}).out, "Usage:"));
}

TEST(CliRun, HelpShortFormPrintsUsage) {
    CliWorkspace ws("help-short");

    EXPECT_EQ(RunCli({"-h"}).exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(RunCli({"-h"}).out, "Usage:"));
}

// Every command has to appear in the list, or a person who does not know the
// name has no way to find it out.
TEST(CliRun, UsageNamesEveryCommand) {
    CliWorkspace ws("usage-commands");

    const guchho::test::CliResult result = RunCli({});

    for (const std::string& word : kCommandWords) {
        EXPECT_TRUE(OutputContains(result.out, word)) << "missing from the usage text: " << word;
    }
}

// ---------------------------------------------------------------------------
// The version
// ---------------------------------------------------------------------------

// One argument and nothing else. The rule is not fussiness about --version; it
// is what stops "-v" from meaning two things at once, since inside a build the
// grammar rejects it and suggests "--log-level=verbose" instead.
TEST(CliRun, VersionFlagPrintsVersion) {
    CliWorkspace ws("version");

    const guchho::test::CliResult result = RunCli({"--version"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "guchho v"));
}

TEST(CliRun, VersionShortFormPrintsVersion) {
    CliWorkspace ws("version-short");

    EXPECT_EQ(RunCli({"-v"}).exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(RunCli({"-v"}).out, "guchho v"));
}

// Two arguments are not a version request, so the flag is not answered and is
// handed to the build grammar instead, which is where it belongs and where it
// is rejected. This is the test that would fail if the "exactly one argument"
// rule were ever loosened.
TEST(CliRun, VersionWithASecondArgumentIsNotAVersionRequest) {
    CliWorkspace ws("version-extra");

    const guchho::test::CliResult result = RunCli({"--version", "extra.js"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Invalid build flag"));
}

// ---------------------------------------------------------------------------
// Nothing to run
// ---------------------------------------------------------------------------

// A flag on its own, with no command word and no path to build, is not a
// request for anything — there is nothing in it that says what to do. The
// dispatcher answers it the way it answers an empty command line, by printing
// the usage text and succeeding.
//
// That is worth pinning because it is the one place a typo is not reported as
// a typo. Add a path and the same flag is rejected by the build grammar with a
// message naming the spelling, which is VersionWithASecondArgumentIsNotAVersion-
// Request above; leave the path out and the person gets the usage text, the
// banner, and exit code 0. Whether that is the right answer is a question
// about the dispatcher rather than about this test — what the test fixes is
// that it is the answer today, so a change to it has to be a deliberate one.
TEST(CliRun, AFlagWithNoCommandAndNoPathPrintsUsage) {
    CliWorkspace ws("flag-only");

    const guchho::test::CliResult result = RunCli({"--nope"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage:"));
    EXPECT_FALSE(OutputContains(result.err, "Invalid build flag"));
}

// A path with no command word in front of it is a build, and the dispatcher's
// own error for it is a usage error rather than a build failure: nothing was
// built, so nothing failed.
TEST(CliRun, APathWithNoCommandIsTreatedAsABuild) {
    CliWorkspace ws("bare-path");
    ws.Write("entry.js", "console.log(1);\n");

    const guchho::test::CliResult result = RunCli({"entry.js", "--outdir=dist"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("dist/entry.js"));
}

} // namespace cli::test
