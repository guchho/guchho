// Tests for the three commands that treat the working directory as their own.
//
// init, clean and info are the only commands with no way to be told where to
// work: each of them calls fs->Cwd() and takes no flag that names a directory,
// so the directory a test gets is the one the test process is standing in. That
// is why every test here builds a CliWorkspace first, and it is also why clean
// is safe to test at all — it removes "dist", ".guchho" and "cache" by name,
// with no confirmation and no flag, and a clean that ran in the repository
// would take the repository's own build output with it.
//
// What these three have in common is that they are the command line's whole
// interface with somebody who has never built anything: init hands over a
// project that has to build, clean is the answer to "where did my output go",
// and info is the answer to "what am I running and what did it read". The round
// trip at the end is the test that ties them to the build command, because an
// init that writes files the build cannot read is a worse outcome than an init
// that fails loudly.

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <string>
#include <vector>

namespace cli::test {

using guchho::test::CliWorkspace;
using guchho::test::kSuccess;
using guchho::test::OutputContains;
using guchho::test::RunCli;

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

// What a new project is: a page, the script it loads, the style it loads, and a
// configuration file. All four, because the first three are useless without the
// fourth and the fourth is useless without the first three.
TEST(CliProject, InitCreatesAProject) {
    CliWorkspace ws("init");

    const guchho::test::CliResult result = RunCli({"init"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("src/index.html"));
    EXPECT_TRUE(ws.Exists("src/main.js"));
    EXPECT_TRUE(ws.Exists("src/style.css"));
    EXPECT_TRUE(ws.Exists("guchho.config.js"));
}

// Every file it wrote is named, because the alternative is a person guessing
// what they now have.
TEST(CliProject, InitNamesWhatItCreated) {
    CliWorkspace ws("init-names");

    const guchho::test::CliResult result = RunCli({"init"});

    EXPECT_TRUE(OutputContains(result.out, "Create"));
    EXPECT_TRUE(OutputContains(result.out, "src/index.html"));
    EXPECT_TRUE(OutputContains(result.out, "guchho.config.js"));
}

// Running it twice must not destroy what is there. Overwriting somebody's
// project because they ran the wrong command in the wrong directory is the one
// outcome this command must never have.
TEST(CliProject, InitTwiceKeepsTheFilesThatAreAlreadyThere) {
    CliWorkspace ws("init-twice");
    ws.Write("src/main.js", "// mine\n");

    EXPECT_EQ(RunCli({"init"}).exit_code, kSuccess);
    const guchho::test::CliResult second = RunCli({"init"});

    EXPECT_EQ(second.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(second.out, "Skip"));
    EXPECT_EQ(ws.Read("src/main.js"), std::string("// mine\n"));
}

// And the way to ask for it on purpose.
TEST(CliProject, InitForceOverwrites) {
    CliWorkspace ws("init-force");
    ws.Write("src/main.js", "// mine\n");

    EXPECT_EQ(RunCli({"init", "--force"}).exit_code, kSuccess);
    EXPECT_NE(ws.Read("src/main.js"), std::string("// mine\n"));
}

// The files are a starting point rather than a demonstration, so what is in
// them has to be one another: the page loads the script and the style by the
// names the project actually has, and the script imports the style the same way.
// The paths are relative to the file that names them, which is why they are
// "./main.js" inside src/index.html and not "src/main.js" — a test that checked
// for the latter would pass against a project that could not load.
TEST(CliProject, TheFilesInitWritesAreOneAnother) {
    CliWorkspace ws("init-linked");

    EXPECT_EQ(RunCli({"init"}).exit_code, kSuccess);

    const std::string page  = ws.Read("src/index.html");
    const std::string script = ws.Read("src/main.js");
    EXPECT_TRUE(OutputContains(page, "./main.js"));
    EXPECT_TRUE(OutputContains(page, "./style.css"));
    EXPECT_TRUE(OutputContains(script, "./style.css"));
    EXPECT_TRUE(ws.Exists("src/style.css")) << "the page asks for a style nobody wrote";
}

TEST(CliProject, InitHelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("init-help");

    const guchho::test::CliResult result = RunCli({"init", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho init"));
}

// ---------------------------------------------------------------------------
// clean
// ---------------------------------------------------------------------------

// The three directories a build can leave behind, and nothing else. A clean
// that removed more than this would be removing something it did not write.
TEST(CliProject, CleanRemovesTheOutputDirectories) {
    CliWorkspace ws("clean");
    ws.MakeDir("dist");
    ws.MakeDir(".guchho");
    ws.MakeDir("cache");
    ws.MakeDir("src");

    const guchho::test::CliResult result = RunCli({"clean"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_FALSE(ws.Exists("dist"));
    EXPECT_FALSE(ws.Exists(".guchho"));
    EXPECT_FALSE(ws.Exists("cache"));
    EXPECT_TRUE(ws.Exists("src")) << "clean removed something it did not write";
}

// What went is named, with the number of items, because "did it work" is the
// first question anybody asks of a command that deletes things.
TEST(CliProject, CleanNamesWhatItRemoved) {
    CliWorkspace ws("clean-names");
    ws.MakeDir("dist");

    const guchho::test::CliResult result = RunCli({"clean"});

    EXPECT_TRUE(OutputContains(result.out, "Removed"));
    EXPECT_TRUE(OutputContains(result.out, "dist"));
}

// The way to find out what would go without having it go.
TEST(CliProject, CleanDryRunLeavesEverythingAlone) {
    CliWorkspace ws("clean-dry");
    ws.MakeDir("dist");

    const guchho::test::CliResult result = RunCli({"clean", "--dry-run"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Would remove"));
    EXPECT_TRUE(ws.Exists("dist"));
}

// A clean in a directory with nothing to clean is a success, not a complaint:
// the state somebody asked for is the state they are already in.
TEST(CliProject, CleanWithNothingToRemoveSucceeds) {
    CliWorkspace ws("clean-empty");

    const guchho::test::CliResult result = RunCli({"clean"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_FALSE(OutputContains(result.err, "error"));
}

TEST(CliProject, CleanHelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("clean-help");

    const guchho::test::CliResult result = RunCli({"clean", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho clean"));
}

// ---------------------------------------------------------------------------
// info
// ---------------------------------------------------------------------------

// The three things a person asks when something is not working: what it is
// running on, whether it was built in release mode, and whether it found a
// configuration. The values themselves are the machine's, so only the fields
// are checked.
TEST(CliProject, InfoReportsTheEnvironment) {
    CliWorkspace ws("info");

    const guchho::test::CliResult result = RunCli({"info"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Platform:"));
    EXPECT_TRUE(OutputContains(result.out, "Build:"));
    EXPECT_TRUE(OutputContains(result.out, "Config:"));
}

// With no configuration file, that is what it says — and saying so plainly is
// the point, because a config nobody found is the first suspect in most
// "why is it building the wrong thing" questions.
TEST(CliProject, InfoSaysWhenNoConfigWasFound) {
    CliWorkspace ws("info-no-config");

    const guchho::test::CliResult result = RunCli({"info"});

    EXPECT_TRUE(OutputContains(result.out, "Config:"));
    EXPECT_TRUE(OutputContains(result.out, "none"));
}

// And when there is one, it names the file it found. This is the same lookup
// the build does, so it doubles as a check that a project file is where the
// build expects to find it.
TEST(CliProject, InfoNamesTheConfigFileItFound) {
    CliWorkspace ws("info-config");
    ws.Write("guchho.config.js", "export default {};\n");

    const guchho::test::CliResult result = RunCli({"info"});

    EXPECT_TRUE(OutputContains(result.out, "guchho.config.js"));
}

TEST(CliProject, InfoHelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("info-help");

    const guchho::test::CliResult result = RunCli({"info", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho info"));
}

// ---------------------------------------------------------------------------
// The round trip
// ---------------------------------------------------------------------------

// The one test here that is about all of it at once, and the reason the other
// eleven exist.
//
// A new project is only real if it builds. init writes four files, the build
// command reads them, and the thing that connects the two is the entry point
// init puts in its configuration file — so this walks the whole path a person
// walks: initialise, then build what was initialised, and check the page came
// out the other side. An init that wrote a file name the build does not look
// for would pass every test above and fail this one.
TEST(CliProject, InitThenBuildProducesThePage) {
    CliWorkspace ws("round-trip");

    ASSERT_EQ(RunCli({"init"}).exit_code, kSuccess);
    const guchho::test::CliResult build =
        RunCli({"build", "src/index.html", "--outdir=dist"});

    EXPECT_EQ(build.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("dist/index.html"));
    EXPECT_TRUE(ws.Exists("dist/main.js"));
    // The script is bundled into an IIFE, so what is in it is the project's own
    // statement rather than its source text — the string it sets is the proof
    // that this is the file init wrote and not an empty shell.
    EXPECT_TRUE(OutputContains(ws.Read("dist/main.js"), "Hello from Guchho!"));
}

// And the output can be taken away again, which is the other half of a project
// being usable: build, clean, build.
TEST(CliProject, AProjectSurvivesCleanAndBuildsAgain) {
    CliWorkspace ws("rebuild");

    ASSERT_EQ(RunCli({"init"}).exit_code, kSuccess);
    ASSERT_EQ(RunCli({"build", "src/index.html", "--outdir=dist"}).exit_code, kSuccess);
    ASSERT_EQ(RunCli({"clean"}).exit_code, kSuccess);
    EXPECT_FALSE(ws.Exists("dist"));

    EXPECT_EQ(RunCli({"build", "src/index.html", "--outdir=dist"}).exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("dist/index.html"));
}

} // namespace cli::test
