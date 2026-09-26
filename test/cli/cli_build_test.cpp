// Tests for the build command as the command line reaches it.
//
// The build itself is covered twice over already: test/bundler drives the
// bundler against a mock file system and compares the output it produced, and
// test/api drives api::Build against a real directory. What is untested
// anywhere is the layer in between — the command word, the flag grammar as the
// command line spells it, the entry point defaulting, the summary, and the
// choice between the four exit codes. That layer is where a person meets the
// build, and it is where the decisions are that no lower layer gets to make.
//
// Two of these tests exist because of a bug rather than because of a feature.
// BuildOnlyFlagsAreNotMistakenForTransformFlags and BuildWithoutAnEntryPoint
// builds from the standard input both describe what happens when nobody names
// an entry point, which is a supported thing to do: the source arrives on the
// standard input and the build is otherwise an ordinary build. The first of
// them was a failure once — every build-only flag was handed to the transform
// grammar, which has no use for an output directory — and the table in
// cli_flags.cpp that fixes it has to stay in step with the grammar, so the test
// is written as a loop over the flags rather than as one case.

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
using guchho::test::RunCliWithStdin;

namespace {

// A document entry point with a script it loads, which is the shape of an
// ordinary page: the html names the js, and the build has to follow.
void WriteHtmlProject(CliWorkspace& ws) {
    ws.Write("index.html",
             "<!DOCTYPE html>\n"
             "<html><head><title>t</title></head>\n"
             "<body><script src=\"./app.js\"></script></body></html>\n");
    ws.Write("app.js", "const greeting = \"hi\";\nconsole.log(greeting);\n");
}

} // namespace

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

// Help is answered before the grammar is read, so a command line that asks for
// help and also contains a mistake still gets help. Asking is a success.
TEST(CliBuild, HelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("help");

    const guchho::test::CliResult result = RunCli({"build", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho build"));
}

TEST(CliBuild, HelpShortFormIsTheSame) {
    CliWorkspace ws("help-short");

    EXPECT_EQ(RunCli({"build", "-h"}).exit_code, kSuccess);
}

// ---------------------------------------------------------------------------
// The ordinary case
// ---------------------------------------------------------------------------

// The whole point of the command: an entry point and a place to put the result,
// and both of them honoured.
TEST(CliBuild, AnEntryPointAndAnOutdirProduceADirectory) {
    CliWorkspace ws("outdir");
    WriteHtmlProject(ws);

    const guchho::test::CliResult result = RunCli({"build", "index.html", "--outdir=dist"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("dist/index.html"));
    EXPECT_TRUE(ws.Exists("dist/app.js"));
}

// The summary is a report of what was produced, so it names the files. The
// byte counts and the duration are the machine talking and are not asserted.
TEST(CliBuild, TheSummaryNamesWhatWasWritten) {
    CliWorkspace ws("summary");
    WriteHtmlProject(ws);

    const guchho::test::CliResult result = RunCli({"build", "index.html", "--outdir=dist"});

    EXPECT_TRUE(OutputContains(result.out, "index.html"));
    EXPECT_TRUE(OutputContains(result.out, "2 files generated"));
}

// One file rather than a directory, which is the other shape an output can
// take and the one that cannot be combined with several entry points.
TEST(CliBuild, AnOutfileProducesOneFile) {
    CliWorkspace ws("outfile");
    ws.Write("entry.js", "console.log(1);\n");

    const guchho::test::CliResult result = RunCli({"build", "entry.js", "--outfile=out.js"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("out.js"));
}

// The script is not copied: it went through the parser and the printer, so the
// output is the program's own text rather than the input's. The evidence is in
// the layout rather than in the tokens — a copy would keep the statements on
// their own lines with the comment in front, and what comes back is one line
// with no comment on it.
//
// The comment is the load-bearing half of that claim, so it is worth saying
// what became of it rather than only what did not: the printer emits no
// comments, legal ones included, unless the build is asked for them. Somebody
// who needs the original text in the output has to keep it rather than read it
// back out of a bundle, and this is the test that says so.
TEST(CliBuild, TheOutputIsPrintedRatherThanCopied) {
    CliWorkspace ws("printed");
    ws.Write("entry.js", "// a comment\nconst a = 1;\nconst b = 2;\n");

    EXPECT_EQ(RunCli({"build", "entry.js", "--outfile=out.js"}).exit_code, kSuccess);

    const std::string out = ws.Read("out.js");
    EXPECT_TRUE(OutputContains(out, "a = 1"));
    EXPECT_TRUE(OutputContains(out, "b = 2"));
    EXPECT_FALSE(OutputContains(out, "a comment"))
        << "the comment survived, so this build copied the file instead of printing it";
}

// Minify is a flag the grammar takes and the printer honours, so this is the
// test that the flag reached the build rather than being quietly dropped.
TEST(CliBuild, MinifyReachesTheOutput) {
    CliWorkspace ws("minify");
    ws.Write("entry.js", "const value = 1;\nconsole.log(value);\n");

    EXPECT_EQ(RunCli({"build", "entry.js", "--outfile=out.js", "--minify"}).exit_code, kSuccess);

    const std::string out = ws.Read("out.js");
    EXPECT_FALSE(OutputContains(out, "\n\n"));
    EXPECT_TRUE(OutputContains(out, "value"));
}

// The metafile is the description of what the build read and produced, and it
// is only written when a flag asks for it and a path to write it to was named.
TEST(CliBuild, TheMetafileIsWrittenWhenItIsAskedForByName) {
    CliWorkspace ws("metafile");
    ws.Write("entry.js", "console.log(1);\n");

    const guchho::test::CliResult result =
        RunCli({"build", "entry.js", "--outfile=out.js", "--metafile=meta.json"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("meta.json"));
    EXPECT_TRUE(OutputContains(ws.Read("meta.json"), "outputs"));
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

// A named entry point that is not there is a build that ran and failed, which
// is 1 and not 2: the command line was understood, the file was not there.
TEST(CliBuild, AMissingEntryPointIsABuildFailure) {
    CliWorkspace ws("missing");

    const guchho::test::CliResult result = RunCli({"build", "missing.js", "--outdir=dist"});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "missing.js"));
}

// No entry point, no flags, nothing to build and nothing on the standard input
// either. The message is the one that tells a person what to do next, and the
// code is a usage error because nothing was ever attempted.
TEST(CliBuild, NoEntryPointsAtAllIsAUsageError) {
    CliWorkspace ws("no-entries");
    WriteHtmlProject(ws);

    const guchho::test::CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "No entry points specified"));
}

// A build with no entry point used to be read as a transform, and a transform
// has no --outdir, so the grammar refused the flag and never got as far as the
// input. That half is fixed: --outdir is now evidence that the command line was
// meant as a build, and the message the grammar used to print is gone. The flag
// passed below is the one that used to be refused, which is why it is the flag
// this test passes.
//
// The other half is a finding rather than a test. The request is now recognised
// and the build starts, and then fails inside the engine with "Unable to
// determine how to load path: <stdin>": there is no file to infer a loader
// from, and the value the --loader flag is parsed into does not reach the place
// the entry point is resolved. Naming the file with --sourcefile does not help,
// and neither does --loader=text, and neither does both — the second test below
// is the record of that.
//
// So this asserts what the command line does today rather than what the feature
// promises, and the exit code is the part that matters: 1, from a build that ran,
// and not the 2 the grammar used to return. A build-from-stdin that works fails
// this test, which is the point. The gap is in the engine's loader resolution
// rather than in the grammar this suite fixed, and it should not be able to get
// any quieter about it.
TEST(CliBuild, ABuildWithNoEntryPointIsRecognisedButFailsToLoadIt) {
    CliWorkspace ws("stdin");

    const guchho::test::CliResult result =
        RunCliWithStdin({"build", "--outdir=dist"}, "console.log(1);\n");

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_FALSE(OutputContains(result.err, "Invalid transform flag"))
        << "the build was read as a transform again";
    EXPECT_TRUE(OutputContains(result.err, "<stdin>"));
}

// The half that was supposed to close the gap above, and does not. It is its own
// test so that a fix in either place shows up on its own: a loader that started
// working fails this one, and a diagnostic that started naming the loader fails
// the one above.
TEST(CliBuild, NamingTheLoaderDoesNotMakeAStdinBuildWork) {
    CliWorkspace ws("stdin-loader");

    const guchho::test::CliResult result = RunCliWithStdin(
        {"build", "--outdir=dist", "--loader=text", "--sourcefile=entry.js"},
        "console.log(1);\n");

    EXPECT_NE(result.exit_code, kSuccess);
}

// The table in cli_flags.cpp that keeps the two halves of the grammar in step,
// and the test that keeps the table honest. Every one of these flags is
// accepted for a build and rejected for a transform, so each one is evidence
// that the command line was meant as a build; with none of them in the table,
// each of these would be answered with a complaint about a transform flag, and
// the loop is written so that a flag added to the grammar but not to the table
// fails here rather than in somebody's command line.
//
// The list is the table, spelling for spelling, and it is checked for one thing
// only. Several of these cannot succeed on their own — "--tsconfig=" wants a
// file that is not there, "--external:" and "--alias:" want a package that is
// not installed, the bare "--metafile" belongs to the embedding form of the
// grammar and is rejected on a command line — and a loop that insisted on a
// successful build would be asserting three different things at once. What they
// all share is which grammar they reached.
TEST(CliBuild, BuildOnlyFlagsAreNotMistakenForTransformFlags) {
    const std::vector<std::string> build_only_flags = {
        "--bundle",
        "--mangle-cache=cache.json",
        "--metafile",
        "--metafile=meta.json",
        "--outfile=out.js",
        "--outdir=dist",
        "--outbase=base",
        "--resolve-extensions=.ts",
        "--main-fields=module",
        "--conditions=import",
        "--public-path=/assets",
        "--tsconfig=tsconfig.json",
        "--entry-names=[name]",
        "--chunk-names=chunk-[hash]",
        "--asset-names=asset-[hash]",
        "--loader:.txt=text",
        "--out-extension:.js=.mjs",
        "--packages=external",
        "--external:react",
        "--inject:env=process",
        "--alias:react=preact",
        "--banner:js=banner",
        "--footer:js=footer",
    };

    for (const std::string& flag : build_only_flags) {
        CliWorkspace ws("build-only");
        ws.Write("entry.js", "console.log(1);\n");

        const guchho::test::CliResult result = RunCli({"build", "entry.js", flag});

        EXPECT_FALSE(OutputContains(result.err, "transform flag"))
            << "a build-only flag was read as a transform flag: " << flag;
    }
}

// The other half of the promise: a build-only flag on a command line that names
// an entry point is a build, and these are the ones that need nothing else to
// succeed. The ones left out of this list are not broken by that — a tsconfig
// path wants the file, an external or an alias wants the package, and an outfile
// cannot be combined with the outdir every one of these already carries.
TEST(CliBuild, TheBuildOnlyFlagsThatNeedNothingElseAllSucceed) {
    const std::vector<std::string> self_contained_flags = {
        "--bundle",
        "--mangle-cache=cache.json",
        "--metafile=meta.json",
        "--outbase=base",
        "--resolve-extensions=.ts",
        "--main-fields=module",
        "--conditions=import",
        "--public-path=/assets",
        "--entry-names=[name]",
        "--chunk-names=chunk-[hash]",
        "--asset-names=asset-[hash]",
        "--loader:.txt=text",
        "--out-extension:.js=.mjs",
        "--packages=external",
        "--banner:js=banner",
        "--footer:js=footer",
    };

    for (const std::string& flag : self_contained_flags) {
        CliWorkspace ws("build-ok");
        ws.Write("entry.js", "console.log(1);\n");

        const guchho::test::CliResult result = RunCli({"build", "entry.js", flag, "--outdir=dist"});

        EXPECT_EQ(result.exit_code, kSuccess) << "flag rejected: " << flag;
        EXPECT_TRUE(ws.Exists("dist")) << "nothing written for: " << flag;
    }
}

// ---------------------------------------------------------------------------
// Flag errors
// ---------------------------------------------------------------------------

// A flag nobody has heard of is stopped before the graph is walked, and the
// note beside the complaint is the half that helps: it names what does exist.
TEST(CliBuild, AnUnknownFlagIsRejectedBeforeAnythingIsBuilt) {
    CliWorkspace ws("unknown-flag");
    WriteHtmlProject(ws);

    const guchho::test::CliResult result = RunCli({"build", "index.html", "--nope"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Invalid build flag"));
    EXPECT_TRUE(OutputContains(result.err, "--nope"));
    EXPECT_FALSE(ws.Exists("dist"));
}

// The near-miss rule, which is what keeps the grammar's prefixes from turning
// into a search: a flag that starts with a real one is still a flag nobody has
// heard of.
TEST(CliBuild, ANearMissFlagIsNotAcceptedAsTheFlagItResembles) {
    CliWorkspace ws("near-miss");

    const guchho::test::CliResult result = RunCli({"build", "entry.js", "--outdirs=dist"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Invalid build flag"));
}

} // namespace cli::test
