// Tests for the transform command, which is the one command whose output is a
// program rather than a report.
//
// transform is a filter. The source arrives on the standard input and the
// result leaves on the standard output, so it drops into a pipeline between two
// other tools without either of them knowing it is there — and that promise has
// a consequence which is easier to break than to notice. A pipeline does not
// read the first line and stop, so anything the command prints in front of the
// program is not decoration, it is a syntax error in the middle of somebody
// else's file. The tests below are mostly about that boundary: what lands on
// each stream, and what does not.
//
// The timing line at the end of a run is written with std::fprintf to stdout
// (src/helpers/timer.cpp), which is why the harness captures the file
// descriptors rather than the C++ stream buffers. A capture at the stream level
// would not have seen that line at all, and this file is where that matters:
// ATimingLineAlsoReachesTheOutputStream is the tripwire for a promise the
// command line makes and does not yet keep.

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <string>
#include <vector>

namespace cli::test {

using guchho::test::CliWorkspace;
using guchho::test::kBuildFailure;
using guchho::test::kSuccess;
using guchho::test::OutputContains;
using guchho::test::RunCli;
using guchho::test::RunCliWithStdin;

namespace {

// The whole output of a transform, with the line endings a Windows pipe adds
// taken out, so a comparison does not depend on which platform ran it.
std::string Normalized(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c != '\r') {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// The ordinary case
// ---------------------------------------------------------------------------

// Source in, program out, and a success code. The output is checked for what it
// contains rather than compared whole, because the printer is free to lay the
// program out as it likes.
TEST(CliTransform, SourceInProgramOut) {
    CliWorkspace ws("plain");

    const guchho::test::CliResult result =
        RunCliWithStdin({"transform"}, "const answer = 42;\nconsole.log(answer);\n");

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "console.log"));
    EXPECT_TRUE(OutputContains(result.out, "42"));
}

// The contract, stated as a test. A transform's output is the program and
// nothing else, so a caller can redirect it straight into a file. The two
// checks are separate on purpose: the program being present and the program
// being alone are different claims, and a change that adds a line to the output
// breaks the second without touching the first.
//
// The timing line is not checked here, though it should be. It is on the output
// stream today, and the next test says so and explains why it is left that way
// rather than quietly dropped from this one.
TEST(CliTransform, TheOutputStreamCarriesTheProgramAndNoReport) {
    CliWorkspace ws("clean-output");

    const guchho::test::CliResult result =
        RunCliWithStdin({"transform"}, "const answer = 42;\n");

    const std::string out = Normalized(result.out);
    EXPECT_TRUE(OutputContains(out, "answer = 42"));
    EXPECT_FALSE(OutputContains(out, "Guchho v")) << "the banner reached the output";
    EXPECT_FALSE(OutputContains(out, "files generated")) << "a summary reached the output";
}

// A known violation of the promise the test above makes, pinned rather than
// papered over.
//
// Three places say the transformed source is the only thing on the output
// stream: the comment above runTransform in src/cli/cli_build.cpp, the
// Run() documentation in include/guchho/cli.hpp, and the fact that this command
// is the one a pipeline pipes into. The line that breaks it is not printed by
// the command line at all — it comes from api::Transform, which ends its work
// with helpers::Timer::Log("done") at src/api/api.cpp:3347, and that writes to
// stdout with std::fprintf. So "guchho transform < in.js > out.js" writes
// "transform finished in 12ms done" as the first line of out.js, which is a
// syntax error in the middle of somebody's program.
//
// It is left failing-by-design rather than deleted from the suite, because the
// fix is in the api layer and not in the command line: the line has to stop
// being written to stdout, or has to become conditional on something a filter
// can turn off. Either way this test is where that change lands — it will fail,
// and the assertion above can then start checking "finished in" as well.
TEST(CliTransform, ATimingLineAlsoReachesTheOutputStream) {
    CliWorkspace ws("timing-line");

    const guchho::test::CliResult result = RunCliWithStdin({"transform"}, "const a = 1;\n");

    EXPECT_TRUE(OutputContains(result.out, "finished in"))
        << "the timing line has moved off the output stream; update "
           "TheOutputStreamCarriesTheProgramAndNoReport to check for it and "
           "delete this test";
    EXPECT_TRUE(OutputContains(result.out, "transform finished in"));
}

// A transform prints no banner at all, which is the other half of the same
// promise: its answer is a program, and a banner in front of it would be part
// of the file somebody downstream receives.
TEST(CliTransform, NoBannerIsPrinted) {
    CliWorkspace ws("no-banner");

    const guchho::test::CliResult result = RunCliWithStdin({"transform"}, "const a = 1;\n");

    EXPECT_FALSE(OutputContains(result.out, "Guchho v"));
    EXPECT_FALSE(OutputContains(result.out, "===="));
}

// Minify is the flag that makes the difference easiest to see, since it removes
// the comments, the indentation and the line breaks the printer would otherwise
// lay out. The input is long enough that the saving is far larger than the two
// or three characters the timing line's own number can vary by.
TEST(CliTransform, MinifyShortensTheProgram) {
    CliWorkspace ws("minify");

    const std::string source =
        "// a comment that minify removes\n"
        "const first = 1;\n"
        "const second = 2;\n"
        "const third = 3;\n"
        "const fourth = 4;\n"
        "const fifth = 5;\n"
        "console.log(first, second, third, fourth, fifth);\n";

    const guchho::test::CliResult plain     = RunCliWithStdin({"transform"}, source);
    const guchho::test::CliResult minified  = RunCliWithStdin({"transform", "--minify"}, source);

    EXPECT_EQ(minified.exit_code, kSuccess);
    EXPECT_TRUE(minified.out.size() + 8 < plain.out.size());
    EXPECT_TRUE(OutputContains(minified.out, "first"));
    EXPECT_FALSE(OutputContains(minified.out, "a comment that minify removes"));
}

// ---------------------------------------------------------------------------
// Failure
// ---------------------------------------------------------------------------

// A program that does not parse is a build that ran and failed: the input was
// read, the parser was reached, and the result is a diagnostic rather than a
// program. The distinction that matters to a pipeline is that the error stream
// is where the diagnostic goes, so a caller reading the output stream still
// gets a file it can look at.
TEST(CliTransform, ASyntaxErrorIsABuildFailureOnTheErrorStream) {
    CliWorkspace ws("syntax-error");

    const guchho::test::CliResult result = RunCliWithStdin({"transform"}, "const = ;\n");

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "Expected identifier"));
    EXPECT_FALSE(OutputContains(result.out, "Expected identifier"))
        << "a diagnostic reached the output stream";
}

// The report carries the place in the text that was read, which is the only
// thing that makes a diagnostic from a pipeline useful: the caller never had a
// file to open, so the line and column are all there is.
TEST(CliTransform, ADiagnosticNamesTheLineItIsAbout) {
    CliWorkspace ws("diagnostic");

    const guchho::test::CliResult result =
        RunCliWithStdin({"transform"}, "const first = 1;\nconst = ;\n");

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "2:"));
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

// Asking for help is a success, and it is answered by this command rather than
// by the shared usage text, because the examples in it name the flags a
// transform actually honours.
TEST(CliTransform, HelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("help");

    const guchho::test::CliResult result = RunCli({"transform", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho transform"));
}

} // namespace cli::test
