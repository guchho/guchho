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

#include <algorithm>
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
// The call in the entry point is load-bearing, and not for the reason it looks.
// An unreferenced top-level declaration is dropped from an entry point, and a
// constant one is folded into the place it is used, so a file that only declared
// two constants prints as nothing at all — an empty file, which this test would
// have passed on. compute() is what keeps both names in the output and what
// keeps the declarations from being folded into the console.log call, where
// there would be no a and no b to look for either.
//
// The comment is the load-bearing half of that claim, so it is worth saying
// what became of it rather than only what did not: the printer emits no
// comments, legal ones included, unless the build is asked for them. Somebody
// who needs the original text in the output has to keep it rather than read it
// back out of a bundle, and this is the test that says so.
TEST(CliBuild, TheOutputIsPrintedRatherThanCopied) {
    CliWorkspace ws("printed");
    ws.Write("entry.js",
             "// a comment\n"
             "const a = compute(1);\n"
             "const b = compute(2);\n"
             "console.log(a, b);\n");

    // Whitespace minification is asked for by name rather than inherited from
    // the default, which no longer minifies anything. The test is about the
    // printer, so it says what it needs from the printer.
    EXPECT_EQ(
        RunCli({"build", "entry.js", "--outfile=out.js", "--minify-whitespace"}).exit_code,
        kSuccess);

    const std::string out = ws.Read("out.js");
    EXPECT_TRUE(OutputContains(out, "a=compute(1)")) << "nothing survived the build: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "b=compute(2)")) << "nothing survived the build: [" << out << "]";
    // One line, whatever the input had four of. This is the half of the claim
    // the comment cannot carry: a copy is a copy whatever happens to its
    // comments, and the layout is what says the text was printed.
    EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 1L)
        << "the statements kept their own lines, so this looks like a copy: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, "a comment"))
        << "the comment survived, so this build copied the file instead of printing it";
}

// Minify is a flag the grammar takes and the printer honours, so this is the
// test that the flag reached the build rather than being quietly dropped.
//
// What the flag does is change the answer, so the flag is checked by the
// change: the same entry point built twice, once with the flag and once with
// the flag turned off, and the two have to come out different.
//
// The call is here for the reason it is in the test above: a bare declaration
// would be tree-shaken or folded away, and then there would be nothing in the
// two outputs to tell apart.
TEST(CliBuild, MinifyReachesTheOutput) {
    CliWorkspace ws("minify");
    ws.Write("entry.js", "const value = compute(1);\nconsole.log(value);\n");

    EXPECT_EQ(RunCli({"build", "entry.js", "--outfile=plain.js", "--minify=false"}).exit_code, kSuccess);
    EXPECT_EQ(RunCli({"build", "entry.js", "--outfile=min.js", "--minify"}).exit_code, kSuccess);

    const std::string plain = ws.Read("plain.js");
    const std::string min = ws.Read("min.js");

    // The program is in both, so that what follows is the flag rather than one
    // of the two builds having gone wrong.
    EXPECT_TRUE(OutputContains(plain, "const value = compute(1);")) << "[" << plain << "]";
    EXPECT_TRUE(OutputContains(min, "const value=compute(1);")) << "[" << min << "]";
    EXPECT_TRUE(OutputContains(plain, "console.log(value);")) << "[" << plain << "]";
    EXPECT_TRUE(OutputContains(min, "console.log(value);")) << "[" << min << "]";
    EXPECT_NE(plain, min) << "the flag changed nothing, so it never reached the build";
}

// The default half of the same pair, and the one the help text states: a build
// that says nothing about minification is a build whose output can still be
// read. The comparison is against "--minify=false" rather than against a
// literal, so the two spellings of "off" cannot drift apart either, and the
// bytes are compared whole because a snapshot of one unminified build would be
// a second copy of what the printer is already tested for elsewhere.
TEST(CliBuild, ABuildWithNoMinifyFlagIsNotMinified) {
    CliWorkspace ws("minify-default");
    ws.Write("entry.js", "const value = compute(1);\nconsole.log(value);\n");

    EXPECT_EQ(RunCli({"build", "entry.js", "--outfile=silent.js"}).exit_code, kSuccess);
    EXPECT_EQ(RunCli({"build", "entry.js", "--outfile=off.js", "--minify=false"}).exit_code, kSuccess);

    const std::string silent = ws.Read("silent.js");
    const std::string off    = ws.Read("off.js");

    EXPECT_EQ(silent, off) << "a build with no flag and a build with \"--minify=false\" differ: ["
                           << silent << "] vs [" << off << "]";
    EXPECT_TRUE(OutputContains(silent, "const value = compute(1);"))
        << "a build with no minify flag came out minified: [" << silent << "]";
}

// Each of the three passes, asked for on its own, and what the other two are
// still doing in the output. The source is chosen so each pass has one visible
// effect and no other does: "localValue" survives whitespace minification and
// does not survive identifier minification, the newlines survive identifier
// minification and do not survive whitespace minification, and the "const"
// survives both of them but not the syntax pass, which inlines the return.
//
// The grammar test in cli_minify_test.cpp says which switches were written;
// this one says what the printer did with them, which is the half that only a
// real build can answer.
TEST(CliBuild, EachMinifyPassFlagChangesOnlyItsOwnPass) {
    CliWorkspace ws("minify-passes");
    ws.Write("entry.js",
             "function outer() {\n"
             "  const localValue = compute(1);\n"
             "  return localValue;\n"
             "}\n"
             "console.log(outer());\n");

    struct Case {
        std::string flag;
        std::string outfile;
    };
    const Case cases[] = {
        {"--minify-whitespace",  "ws.js"},
        {"--minify-identifiers", "ids.js"},
        {"--minify-syntax",      "syntax.js"},
        {"--minify",             "all.js"},
    };

    for (const Case& one : cases) {
        const std::vector<std::string> args{"build", "entry.js",
                                            "--outfile=" + one.outfile, one.flag};
        ASSERT_EQ(RunCli(args).exit_code, kSuccess) << one.flag;
    }

    const std::string whitespace_only = ws.Read("ws.js");
    const std::string identifiers_only = ws.Read("ids.js");
    const std::string syntax_only = ws.Read("syntax.js");
    const std::string everything = ws.Read("all.js");

    // Whitespace: the names are the reader's own and the layout is gone.
    EXPECT_TRUE(OutputContains(whitespace_only, "localValue")) << "[" << whitespace_only << "]";
    EXPECT_TRUE(OutputContains(whitespace_only, "const localValue=compute(1)"))
        << "[" << whitespace_only << "]";
    EXPECT_EQ(std::count(whitespace_only.begin(), whitespace_only.end(), '\n'), 1L)
        << "[" << whitespace_only << "]";

    // Identifiers: the names are gone and the line breaks are still there.
    EXPECT_FALSE(OutputContains(identifiers_only, "localValue")) << "[" << identifiers_only << "]";
    EXPECT_TRUE(OutputContains(identifiers_only, "const ") &&
                OutputContains(identifiers_only, "= compute(1)"))
        << "[" << identifiers_only << "]";
    EXPECT_GT(std::count(identifiers_only.begin(), identifiers_only.end(), '\n'), 1L)
        << "[" << identifiers_only << "]";

    // Syntax: the binding and its one use collapse into the call, and the
    // spaces around them go with them. Nothing was renamed and the layout is
    // still one statement per line, so the name that survives is the call's.
    EXPECT_TRUE(OutputContains(syntax_only, "return compute(1)")) << "[" << syntax_only << "]";
    EXPECT_FALSE(OutputContains(syntax_only, "const localValue"))
        << "the syntax pass was asked for and the declaration is still there: ["
        << syntax_only << "]";
    EXPECT_FALSE(OutputContains(syntax_only, "outer(){"))
        << "the whitespace pass ran even though only the syntax one was asked for: ["
        << syntax_only << "]";
    EXPECT_GT(std::count(syntax_only.begin(), syntax_only.end(), '\n'), 1L)
        << "[" << syntax_only << "]";

    // The shorthand is all three at once, which is what makes it the shorthand.
    EXPECT_FALSE(OutputContains(everything, "localValue")) << "[" << everything << "]";
    EXPECT_FALSE(OutputContains(everything, "const localValue")) << "[" << everything << "]";
}

// A project whose config turns minification on, and a command line that asked
// for one pass: the flag wins, and it wins as the pass that was named rather
// than as the whole of "--minify". This is the case the explicit record is for,
// and it is why each of the three marks minification as asked for.
TEST(CliBuild, AMinifyPassFlagOverridesAConfigThatMinifies) {
    CliWorkspace ws("minify-config");
    ws.Write("entry.js",
             "function outer() {\n"
             "  const localValue = compute(1);\n"
             "  return localValue;\n"
             "}\n"
             "console.log(outer());\n");
    ws.Write("guchho.config.json", R"({"build":{"entry":"entry.js","minify":true}})");

    EXPECT_EQ(RunCli({"build", "--outfile=from-flag.js", "--minify-whitespace"}).exit_code, kSuccess);
    EXPECT_EQ(RunCli({"build", "--outfile=from-config.js"}).exit_code, kSuccess);

    const std::string from_flag   = ws.Read("from-flag.js");
    const std::string from_config = ws.Read("from-config.js");

    EXPECT_TRUE(OutputContains(from_flag, "localValue"))
        << "the config answered the flag and renamed what it named: [" << from_flag << "]";
    EXPECT_FALSE(OutputContains(from_config, "localValue"))
        << "the config asked for minification and the names survived: [" << from_config << "]";
}

// The other direction, and the one that only exists because minification is off
// by default now: a config that turns it on and a command line that turns it
// back off. "--minify=false" was already accepted, but before the default
// flipped there was almost nothing for it to disagree with.
TEST(CliBuild, MinifyFalseOverridesAConfigThatMinifies) {
    CliWorkspace ws("minify-config-off");
    ws.Write("entry.js", "const value = compute(1);\nconsole.log(value);\n");
    ws.Write("guchho.config.json", R"({"build":{"entry":"entry.js","minify":true}})");

    EXPECT_EQ(RunCli({"build", "--outfile=off.js", "--minify=false"}).exit_code, kSuccess);

    const std::string off = ws.Read("off.js");
    EXPECT_TRUE(OutputContains(off, "const value = compute(1);"))
        << "\"--minify=false\" left the config in charge: [" << off << "]";
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
// The second half is fixed as well: the source that arrived on the standard
// input used to be lost between the option validation and the scan, because the
// options hold a plain pointer to the text and that text was moved twice on its
// way to the owner that was supposed to keep it. A build fed from standard input
// therefore found an empty string where its source should have been, printed a
// success, and wrote a zero-byte file — the worst shape a failure can take,
// because everything it reported was true and the program was gone.
//
// So the exit code is the first half of this assertion and the contents are the
// second, and the second is the load-bearing one: a build that reported success
// and wrote nothing would satisfy an exit-code test alone. The output is
// therefore read back off disk and compared, rather than trusted.
TEST(CliBuild, ABuildWithNoEntryPointBuildsTheStandardInput) {
    CliWorkspace ws("stdin");

    const guchho::test::CliResult result =
        RunCliWithStdin({"build", "--outdir=dist"}, "console.log(1);\n");

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_FALSE(OutputContains(result.err, "Invalid transform flag"))
        << "the build was read as a transform again";
    EXPECT_TRUE(ws.Exists("dist/stdin.js")) << "nothing was written: [" << ws.Tree() << "]";
    EXPECT_EQ(ws.Read("dist/stdin.js"), "console.log(1);\n")
        << "the program on the standard input did not survive the build";
}

// Naming the loader and the file, which is what the first test's predecessor
// recorded as making no difference. It does now, and the point of keeping the
// test is the loader it asks for: --loader=text wraps the program as a string
// rather than printing it as JavaScript, so the two builds of the same input
// differ in a way only the loader can explain.
//
// The output is still named after the standard input and not after --sourcefile,
// because that flag names the source for diagnostics and does not rename the
// output; the file is therefore read from where the build says it wrote it.
TEST(CliBuild, NamingTheLoaderAndTheSourceFileChangesTheStdinBuild) {
    CliWorkspace ws("stdin-loader");

    const guchho::test::CliResult plain =
        RunCliWithStdin({"build", "--outdir=dist"}, "console.log(1);\n");
    EXPECT_EQ(plain.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("dist/stdin.js")) << "nothing was written: [" << ws.Tree() << "]";
    // Read between the two runs rather than after both, because both write to
    // the same file: a second build would leave only the last one's bytes on
    // disk and the first would have nothing left to compare.
    const std::string as_js = ws.Read("dist/stdin.js");

    const guchho::test::CliResult named = RunCliWithStdin(
        {"build", "--outdir=dist", "--loader=text", "--sourcefile=entry.js"},
        "console.log(1);\n");

    EXPECT_EQ(named.exit_code, kSuccess);
    EXPECT_FALSE(OutputContains(named.err, "Unable to determine how to load"))
        << "naming the loader still does not reach the entry point: [" << named.err << "]";
    const std::string as_text = ws.Read("dist/stdin.js");

    EXPECT_TRUE(OutputContains(as_js, "console.log(1)"))
        << "the program did not survive the build: [" << as_js << "]";
    EXPECT_TRUE(OutputContains(as_text, "console.log(1)"))
        << "the program did not survive the build: [" << as_text << "]";
    EXPECT_NE(as_js, as_text)
        << "the loader was asked for and nothing about the build changed";
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
