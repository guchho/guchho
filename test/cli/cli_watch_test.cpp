// Tests for watch, up to the line where it stops returning.
//
// runWatch builds a context and then hands it to api::Watch, which reports the
// first build and then keeps watching until the process is killed — the promise
// it waits on is never fulfilled, so the command has no path back to a caller.
// A test that asked watch to do its work would hang rather than fail, and CTest
// would report a timeout with nothing to point at.
//
// So the three tests here are the three ways a watch run ends before that
// point, and between them they cover the whole of the command that is reachable
// from a test process. All of them are the same shape: a question asked in
// advance that watch answers immediately, by refusing, because the answer is
// known without watching a single file. That is worth testing on its own terms.
// A watch command is the one people run in a second terminal, so the first
// thing it has to do is say something at the point where somebody is still
// watching the screen.

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <string>
#include <vector>

namespace cli::test {

using guchho::test::CliWorkspace;
using guchho::test::kBuildFailure;
using guchho::test::kUsageError;
using guchho::test::OutputContains;
using guchho::test::RunCli;

// A watch in a directory with nothing to watch has nothing to report and would
// otherwise sit there silently, which looks like a hang to anybody who started
// it. The message is the important half of this test: it names the two ways out
// of the situation, so a person reading it does not have to know the project's
// layout to guess.
TEST(CliWatch, ADirectoryWithNoEntryPointsIsRefused) {
    CliWorkspace ws("watch-empty");

    const guchho::test::CliResult result = RunCli({"watch"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "No entry points specified"));
    EXPECT_TRUE(OutputContains(result.err, "init")) << "the refusal does not say how to make one";
}

// watch takes the build grammar, so an unknown flag is refused by the same code
// that refuses it to build — which is the reason watch does not need its own
// flag list to be kept in step.
TEST(CliWatch, AnUnknownFlagIsRejectedBeforeAnythingIsWatched) {
    CliWorkspace ws("watch-flag");

    const guchho::test::CliResult result = RunCli({"watch", "--nope"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Invalid build flag: '--nope'"));
}

// Two entry points into one output file is a question the graph can answer
// before any of it is built, and answering it here is the difference between an
// error and a watch session that rebuilds the same mistake every time a file
// changes. The build command refuses this too; the exit code differs, because
// this is a build that ran and failed rather than a mistyped flag.
TEST(CliWatch, SeveralEntryPointsIntoOneFileIsRefused) {
    CliWorkspace ws("watch-outfile");
    ws.Write("a.js", "console.log(1);\n");
    ws.Write("b.js", "console.log(2);\n");

    const guchho::test::CliResult result = RunCli({"watch", "a.js", "b.js", "--outfile=out.js"});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "outdir"))
        << "the error does not say which flag would have worked";
}

// A config file that exists but cannot be parsed leaves this command with
// nothing to watch, and it says so the same way "guchho build" does — rather
// than sitting over a session built from the command line alone while the
// project's real configuration goes unused. The refusal happens before the
// watcher starts, which is what makes it reachable from a test.
TEST(CliWatch, WatchRefusesAnInvalidConfig) {
    CliWorkspace ws("watch-invalid-config");
    ws.Write("guchho.config.json", "{\"build\": {\"outdir\": \"broken");

    const guchho::test::CliResult result = RunCli({"watch"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "The project configuration could not be used"));
}

// The config the session settled on is named before the session's own
// progress line. A config with an outdir but no entry point, in a workspace
// with no index.html for the guess to find, gets past the resolution — where
// "Using" is printed — and stops at the entry-point gate, before any watcher
// starts. The untestable half is a session that stays up; the file header
// says why.
TEST(CliWatch, WatchReportsTheConfigItIsUsing) {
    CliWorkspace ws("watch-using-config");
    ws.Write("guchho.config.json", "{\"build\": {\"outdir\": \"custom-out\"}}");

    const guchho::test::CliResult result = RunCli({"watch"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.out, "Using "));
    EXPECT_TRUE(OutputContains(result.out, "guchho.config.json"));
}

// Not tested: the first build, the rebuild on change, the summary between
// builds, and the shutdown. All four happen after the context is handed over,
// and none of them can be reached from a process that has to keep running to
// see them. api::Watch is the layer that does the watching, and it is driven
// directly where a real watcher can be stopped — the same arrangement the serve
// tests describe in test/cli/cli_serve_test.cpp.

} // namespace cli::test
