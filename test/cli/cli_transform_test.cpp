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

#include <algorithm>
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

// The fixture the pass tests below all read: one function with a parameter, a
// local binding read exactly once, and a concatenation. Each of the three
// passes has something of its own to act on — whitespace has the layout to
// remove, identifiers have two bindings to shorten, and syntax has the local
// whose value can be returned directly. The same bytes go to every case; the
// flag is the only thing that changes.
const char kGreet[] =
    "function greet(name) {\n"
    "    const message = \"Hello \" + name;\n"
    "    return message;\n"
    "}\n";

// How many line breaks an output has, once Normalized has taken the carriage
// returns out. One means a single line plus the newline the program ends with.
int LineCount(const std::string& text) {
    return static_cast<int>(std::count(text.begin(), text.end(), '\n'));
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
// nothing else, so a caller can redirect it straight into a file. The three
// checks are separate on purpose: the program being present and the program
// being alone are different claims, and a change that adds a line to the output
// breaks the second without touching the first.
//
// The third check is the one that used to be a known violation. The timing
// line came from api::Transform, which ended its work with helpers::Timer::Log
// writing to stdout, so "guchho transform < in.js > out.js" put
// "transform finished in 12ms done" in front of the program. It now goes to the
// error stream (Timer::LogToStderr), where a filter's diagnostics already live,
// and the line is still there for anybody watching a pipeline.
TEST(CliTransform, TheOutputStreamCarriesTheProgramAndNoReport) {
    CliWorkspace ws("clean-output");

    const guchho::test::CliResult result =
        RunCliWithStdin({"transform"}, "const answer = 42;\n");

    const std::string out = Normalized(result.out);
    EXPECT_TRUE(OutputContains(out, "answer = 42"));
    EXPECT_FALSE(OutputContains(out, "Guchho v")) << "the banner reached the output";
    EXPECT_FALSE(OutputContains(out, "files generated")) << "a summary reached the output";
    EXPECT_FALSE(OutputContains(out, "finished in"))
        << "a timing line reached the output stream, which is the program";
}

// The timing line has to survive the move rather than be dropped: it is the
// only record of how long a transform took, and losing it to fix a stream
// would be trading a real diagnostic for a cosmetic one. The error stream is
// where a caller already looks for everything else this command says.
TEST(CliTransform, TheTimingLineIsOnTheErrorStream) {
    CliWorkspace ws("timing-line");

    const guchho::test::CliResult result = RunCliWithStdin({"transform"}, "const a = 1;\n");

    EXPECT_TRUE(OutputContains(result.err, "transform finished in"))
        << "the timing line is missing from the error stream";
    EXPECT_FALSE(OutputContains(result.out, "finished in"))
        << "the timing line went back to the output stream";
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
// The three passes, seen in the output
// ---------------------------------------------------------------------------

// Whitespace alone: the program is one compact line, everything that gave it
// its shape is gone, and everything that gave it its meaning is not. The
// comparison is exact because this pass's whole effect is layout bytes — a
// kept space, a surviving newline and a renamed identifier would each break a
// different claim, and one equality message names all three at once. The
// retained "const message" and "return message" are also the half of the line
// that says the syntax pass did not run: it would have returned the value
// directly.
TEST(CliTransform, WhitespaceMinifyCompactsTheProgramAndKeepsEveryName) {
    CliWorkspace ws("minify-whitespace-output");

    const guchho::test::CliResult result =
        RunCliWithStdin({"transform", "--minify-whitespace"}, kGreet);
    ASSERT_EQ(result.exit_code, kSuccess) << result.err;

    const std::string out = Normalized(result.out);
    EXPECT_EQ(out, "function greet(name){const message=\"Hello \"+name;return message}\n")
        << "whitespace minification did not compact the program while keeping its "
           "names and its declaration: ["
        << out << "]";
}

// Identifiers alone: the two local bindings are shortened and the shape of
// the program is untouched. The new spellings are the mangler's own choice,
// so what is asserted is that the old names are gone rather than that some
// particular letter replaced them — a test that pinned "e" would be pinning
// the mangler's current habit. The line breaks, the spaces around the
// operators and the "const" declaration are each a separate expectation,
// because each is a pass that must not have run.
TEST(CliTransform, IdentifierMinifyShortensTheLocalsAndLeavesTheLayoutAlone) {
    CliWorkspace ws("minify-identifiers-output");

    const guchho::test::CliResult result =
        RunCliWithStdin({"transform", "--minify-identifiers"}, kGreet);
    ASSERT_EQ(result.exit_code, kSuccess) << result.err;

    const std::string out = Normalized(result.out);

    // The parameter and the binding are not the names they started as.
    EXPECT_FALSE(OutputContains(out, "name"))
        << "the parameter kept its name, so identifier minification did not reach it: ["
        << out << "]";
    EXPECT_FALSE(OutputContains(out, "message"))
        << "the local binding kept its name: [" << out << "]";

    // The function's own name is not one of the locals being shortened.
    EXPECT_TRUE(OutputContains(out, "greet"))
        << "the function was renamed along with its locals: [" << out << "]";

    // The layout is the unminified one: still several lines, still spaced.
    EXPECT_GT(LineCount(out), 1)
        << "the program was collapsed to one line, so the whitespace pass ran too: ["
        << out << "]";
    EXPECT_TRUE(OutputContains(out, " = "))
        << "the spaces around the assignment are gone: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, " + "))
        << "the spaces around the concatenation are gone: [" << out << "]";

    // And the syntax pass did not inline the declaration.
    EXPECT_TRUE(OutputContains(out, "const "))
        << "the declaration was rewritten, so the syntax pass ran too: [" << out << "]";
}

// Syntax alone: the local that was read exactly once is gone and its value is
// returned directly, while the names and the layout are the ones the program
// arrived with. The specific rewrite — a const read once folded into the
// return — is what this fixture provably gets from the syntax pass, so the
// assertions pin the value reaching the return and the declaration leaving,
// not the exact byte layout around them.
TEST(CliTransform, SyntaxMinifySimplifiesTheProgramAndKeepsNamesAndLayout) {
    CliWorkspace ws("minify-syntax-output");

    const guchho::test::CliResult result =
        RunCliWithStdin({"transform", "--minify-syntax"}, kGreet);
    ASSERT_EQ(result.exit_code, kSuccess) << result.err;

    const std::string out = Normalized(result.out);

    // The simplification happened: no declaration, and the concatenation is
    // the value the function returns.
    EXPECT_FALSE(OutputContains(out, "const"))
        << "the once-read local is still declared, so no syntax simplification "
           "happened: ["
        << out << "]";
    EXPECT_TRUE(OutputContains(out, "\"Hello \" + name"))
        << "the return does not carry the value directly: [" << out << "]";

    // Every name the program arrived with that is still in it kept its
    // spelling: nothing was renamed.
    EXPECT_TRUE(OutputContains(out, "greet"))
        << "the function was renamed by the syntax pass: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "name"))
        << "the parameter was renamed by the syntax pass: [" << out << "]";

    // And the program is still laid out, not compacted.
    EXPECT_GT(LineCount(out), 1)
        << "the program was collapsed to one line, so the whitespace pass ran too: ["
        << out << "]";
    EXPECT_TRUE(OutputContains(out, " + "))
        << "the spaces around the operators are gone: [" << out << "]";
}

// The shorthand: all three at once, which is what makes it the shorthand.
// One line with no spaces around the operators (whitespace), locals that are
// not the names they arrived as (identifiers), and no declaration left behind
// (syntax) — three expectations because a "--minify" that quietly ran only
// two of the passes would still shorten the program and still pass a test
// that only measured its size.
TEST(CliTransform, TheShorthandEnablesEveryPassAtOnce) {
    CliWorkspace ws("minify-all-output");

    const guchho::test::CliResult result =
        RunCliWithStdin({"transform", "--minify"}, kGreet);
    ASSERT_EQ(result.exit_code, kSuccess) << result.err;

    const std::string out = Normalized(result.out);

    // Whitespace: one line plus the newline the program ends with, and no
    // spaces around the operators (the ones inside the string literal stay).
    EXPECT_EQ(LineCount(out), 1) << "the program is not one compact line: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, " + "))
        << "the spaces around the concatenation survived the whitespace pass: ["
        << out << "]";

    // Identifiers: the locals are not the names they arrived as, and the
    // function's own name still is.
    EXPECT_FALSE(OutputContains(out, "name"))
        << "a local kept its name: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "greet"))
        << "the function was renamed: [" << out << "]";

    // Syntax: the once-read local's declaration is gone.
    EXPECT_FALSE(OutputContains(out, "const"))
        << "the declaration survived the syntax pass: [" << out << "]";
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
