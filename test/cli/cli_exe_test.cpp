// Tests that run the built guchho.exe rather than calling into it.
//
// Every other file in this directory calls guchho::cli::Run in this process,
// which is fast and precise but proves something narrower than it looks. It
// proves the dispatcher works. It cannot prove that the executable starts, that
// it finds its own DLLs, that argv survives the trip from the operating system
// intact, or that the exit code makes it out the other side as a number instead
// of a piece of text. Those are the failures that reach a user as "it works in
// the test suite" and "it does nothing when I run it", and they are all
// somewhere between this test binary and the code the other tests call — in
// main(), in the wmain path that CommandLineToArgvW and UTF16ToString exist for,
// and in the runtime that loads the executable.
//
// So this file starts processes. It uses the same helpers::RunProcess the rest
// of the test suite uses, which means it gets a real exit code and both streams
// back, and it passes the path to the executable through a compile definition
// rather than through the environment, so it cannot accidentally run some
// guchho.exe that happens to be first on PATH — which would be a green test
// measuring the wrong binary.
//
// The one thing it cannot do is feed a process its standard input.
// helpers::RunProcess has no parameter for it, so the transform pipeline is
// covered by cli_transform_test.cpp in-process and the flags here are the ones
// that need no input. Changing RunProcess to accept a stream is a change to
// the common library that this suite is not entitled to make on its own.

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <string>
#include <vector>

#include "guchho/helpers.hpp"

namespace cli::test {

using guchho::test::CliWorkspace;
using guchho::test::kBuildFailure;
using guchho::test::kSuccess;
using guchho::test::kUsageError;
using guchho::test::OutputContains;
using guchho::helpers::ProcessResult;
using guchho::helpers::RunProcess;

namespace {

// The executable under test, as CMake saw it when it generated this file. The
// definition is on the object library that compiles this source, not only on the
// executable, or it would be missing here.
std::vector<std::string> Exe(std::vector<std::string> args) {
    args.insert(args.begin(), GUCHHO_TEST_EXE);
    return args;
}

} // namespace

// ---------------------------------------------------------------------------
// It starts
// ---------------------------------------------------------------------------

// The whole reason this file exists, as its own test. A process that could not
// be started is reported by RunProcess rather than by throwing, so a test that
// forgot to check would see an exit code of zero and pass.
TEST(CliExe, TheExecutableStarts) {
    const ProcessResult result = RunProcess(Exe({"--version"}), ".");

    EXPECT_TRUE(result.started) << "the executable did not start at all";
    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.stdout_data, "guchho"));
}

// The version string is what a person pastes into a bug report, so it is the one
// output that has to arrive through a pipe unscrambled.
TEST(CliExe, TheVersionArrivesThroughAPipe) {
    const ProcessResult result = RunProcess(Exe({"--version"}), ".");

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.stdout_data, "guchho v"));
}

// ---------------------------------------------------------------------------
// Arguments survive the trip
// ---------------------------------------------------------------------------

// The argument that would be lost first. A path with a character outside the
// platform's native encoding is the reason main() converts, and a build driven
// from a pipe through a main() that did not convert would either fail to find
// the file or write its output somewhere else. It is checked from both ends: the
// process had to read the file, and the file it wrote has to hold the document.
//
// The page is a complete document rather than a fragment, because a document
// entry point is parsed as one and complains about a missing doctype before it
// looks at anything else — which would make this a test of the HTML parser
// rather than of the encoding.
TEST(CliExe, APathOutsideTheNativeEncodingSurvives) {
    CliWorkspace ws("non-ascii");
    ws.Write("café/index.html",
             "<!DOCTYPE html>\n<html><body>café</body></html>\n");

    const ProcessResult result = RunProcess(
        Exe({"build", "café/index.html", "--outdir=dist"}), ws.path());

    EXPECT_EQ(result.exit_code, kSuccess) << result.stderr_data;
    EXPECT_TRUE(ws.Exists("dist/index.html"));
    EXPECT_TRUE(OutputContains(ws.Read("dist/index.html"), "café"));
}

// A path with a space in it is the other half: it is not an encoding question
// but an argument-splitting one, and the two bugs look the same to whoever hits
// them.
TEST(CliExe, APathWithASpaceInItIsOneArgument) {
    CliWorkspace ws("space");
    ws.Write("my project/index.html",
             "<!DOCTYPE html>\n<html><body>hi</body></html>\n");

    const ProcessResult result = RunProcess(
        Exe({"build", "my project/index.html", "--outdir=dist"}), ws.path());

    EXPECT_EQ(result.exit_code, kSuccess) << result.stderr_data;
    EXPECT_TRUE(ws.Exists("dist/index.html"));
}

// A flag with a value in it, so the two halves of --flag=value are checked
// together rather than one at a time.
TEST(CliExe, AFlagAndItsValueArriveTogether) {
    CliWorkspace ws("flag");
    ws.Write("entry.js", "console.log(1);\n");

    const ProcessResult result =
        RunProcess(Exe({"build", "entry.js", "--outdir=out put"}), ws.path());

    EXPECT_EQ(result.exit_code, kSuccess) << result.stderr_data;
    EXPECT_TRUE(ws.Exists("out put/entry.js"));
}

// ---------------------------------------------------------------------------
// Exit codes and streams
// ---------------------------------------------------------------------------

// The three codes a caller can act on, and they are the reason to run a process
// rather than call the function: a host embedding this has to tell them apart
// from the outside, and an exit code that only exists as a return value does not
// survive being handed to a shell. 2 for a command line that was rejected, 1
// for a build that ran and failed, 0 for a build that worked.
TEST(CliExe, TheExitCodeSaysWhatKindOfFailureItWas) {
    CliWorkspace ws("codes");
    ws.Write("entry.js", "console.log(1);\n");

    const ProcessResult ok = RunProcess(
        Exe({"build", "entry.js", "--outdir=dist"}), ws.path());
    EXPECT_EQ(ok.exit_code, kSuccess);

    const ProcessResult rejected = RunProcess(
        Exe({"build", "entry.js", "--nope"}), ws.path());
    EXPECT_EQ(rejected.exit_code, kUsageError);

    const ProcessResult failed = RunProcess(
        Exe({"build", "missing.js", "--outdir=dist"}), ws.path());
    EXPECT_EQ(failed.exit_code, kBuildFailure);
}

// A rejected command line is answered on the error stream and nowhere else, so
// that a caller reading stdout for a program is not handed a diagnostic. The
// program side of this is what makes the transform tests possible.
//
// The path is not optional here, and that is the other thing this test pins: a
// rejected flag on its own is not a rejected command line, because there is
// nothing in it to reject — with no entry point the dispatcher has no build to
// try and answers with the usage text instead. Add a file to build and the same
// flag is refused with 2 and a message naming the spelling.
TEST(CliExe, ADiagnosticArrivesOnTheErrorStream) {
    CliWorkspace ws("diagnostic");
    ws.Write("entry.js", "console.log(1);\n");

    const ProcessResult result =
        RunProcess(Exe({"build", "entry.js", "--nope"}), ws.path());

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.stderr_data, "Invalid build flag"));
    EXPECT_FALSE(OutputContains(result.stdout_data, "Invalid build flag"))
        << "a diagnostic arrived on the output stream";
}

// And the usage text, which is the one case where a caller reads stdout for
// something that is not a program: no arguments is a success, not a failure.
TEST(CliExe, NoArgumentsPrintsUsageAndSucceeds) {
    const ProcessResult result = RunProcess(Exe({}), ".");

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.stdout_data, "Usage:"));
    EXPECT_TRUE(OutputContains(result.stdout_data, "build"));
}

// The working directory is the process's, not the test's: this file is the only
// place in the suite where a command has to find its inputs without being told
// where they are, and RunProcess is what sets that. init writes into the
// directory it is started in, so the files appearing under the workspace and
// nowhere else is the assertion.
TEST(CliExe, TheWorkingDirectoryIsTheOneTheProcessWasGiven) {
    CliWorkspace ws("cwd");
    ws.Write("src/index.html", "<html><body>hi</body></html>\n");

    const ProcessResult result = RunProcess(Exe({"info"}), ws.path());

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_FALSE(ws.Exists("guchho.config.js")) << "info wrote a configuration file";
}

// A process that does not exist is a failure to start, not a failure to run,
// and the two are told apart by the flag rather than by an exit code — which is
// worth pinning here, at the bottom of the file, because it is the failure this
// harness itself would report if the compile definition were ever wrong. A
// missing executable here must not look like a passing test.
TEST(CliExe, AProcessThatCannotStartIsReportedAsSuch) {
    const ProcessResult result =
        RunProcess({"definitely-not-a-real-executable-guchho", "--version"}, ".");

    EXPECT_FALSE(result.started);
    EXPECT_FALSE(OutputContains(result.stdout_data, "guchho v"));
}

} // namespace cli::test
