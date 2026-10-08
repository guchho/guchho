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

#include "guchho/helpers.hpp"

namespace cli::test {

using guchho::test::CliResult;
using guchho::test::CliWorkspace;
using guchho::test::kBuildFailure;
using guchho::test::kSuccess;
using guchho::test::kUsageError;
using guchho::test::OutputContains;
using guchho::test::RunCli;
using guchho::test::RunCliWithStdin;
using guchho::helpers::ProcessResult;
using guchho::helpers::RunProcess;

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

// The text around the output, asked for in the plain spelling — the one the
// JavaScript API sends, a single string rather than a record keyed by file
// kind. That spelling used to be read as a transform's only, so a build that
// asked for a banner was turned away by its own command line with "Invalid
// build flag", while the same option on a transform worked. The colon form
// ("--banner:js=") is the one that names the kind, and it keeps working; this
// is the one a caller who wrote one string was using.
//
// The value is taken whole rather than up to the first "=", and the text below
// carries one on purpose: a banner versioned "v=1.0" that stopped at the "="
// would print something nobody wrote.
TEST(CliBuild, ThePlainBannerAndFooterFlagsReachTheOutput) {
    CliWorkspace ws("banner-plain");
    ws.Write("entry.js", "const value = compute(1);\nconsole.log(value);\n");

    const std::string banner = "/*! MyLib v=1.0 | MIT License */";
    const std::string footer = "/*! end of MyLib */";

    EXPECT_EQ(RunCli({"build", "entry.js", "--outfile=plain.js", "--banner=" + banner,
                      "--footer=" + footer}).exit_code,
              kSuccess);
    EXPECT_EQ(RunCli({"build", "entry.js", "--outfile=min.js", "--minify", "--banner=" + banner,
                      "--footer=" + footer}).exit_code,
              kSuccess);

    const std::string plain = ws.Read("plain.js");
    const std::string min   = ws.Read("min.js");

    for (const std::string* text : {&plain, &min}) {
        ASSERT_GE(text->size(), banner.size()) << "the output is shorter than its own banner: [" << *text << "]";

        // Above the program, byte for byte — not a rewritten or reflowed
        // version of it, and not below anything the printer added.
        EXPECT_EQ(text->substr(0, banner.size()), banner)
            << "the banner is not first in: [" << *text << "]";

        // And at the far end, with nothing after it but the newline the footer
        // is written with. Checked as "nothing but whitespace follows" rather
        // than as a fixed byte count, so that a trailing newline is not a
        // failure the test has to know the reason for.
        const auto footer_at = text->rfind(footer);
        ASSERT_NE(footer_at, std::string::npos) << "the footer is missing from: [" << *text << "]";
        const std::string after = text->substr(footer_at + footer.size());
        EXPECT_EQ(after.find_first_not_of(" \t\r\n"), std::string::npos)
            << "the footer is not last in: [" << *text << "]";
    }

    EXPECT_TRUE(OutputContains(plain, "const value = compute(1);"))
        << "the program is missing from the unminified build: [" << plain << "]";
    EXPECT_TRUE(OutputContains(min, "const value=compute(1);"))
        << "minify did not reach the build: [" << min << "]";
    EXPECT_LT(min.size(), plain.size())
        << "the minified build is not smaller, so the comparison above means nothing";
}

// The banner a config file carries, asked for the same way the flag's is: a
// single string sitting above the build. And the ranking the resolver exists
// for — a command line that named a banner wins over the config's answer,
// while a run that named none takes the config's.
TEST(CliBuild, ConfigFileBannerReachesTheOutputAndYieldsToTheFlag) {
    CliWorkspace ws("banner-from-config");
    ws.Write("entry.js", "console.log(1);\n");
    ws.Write("guchho.config.json",
             "{\"build\":{\"entry\":\"entry.js\",\"outfile\":\"cfg.js\","
             "\"banner\":\"/*! from config */\"}}");

    EXPECT_EQ(RunCli({"build"}).exit_code, kSuccess);

    const std::string config_banner = "/*! from config */";
    const std::string cfg            = ws.Read("cfg.js");
    ASSERT_GE(cfg.size(), config_banner.size())
        << "the output is shorter than its own banner: [" << cfg << "]";
    EXPECT_EQ(cfg.substr(0, config_banner.size()), config_banner)
        << "the config's banner is not first in: [" << cfg << "]";

    // The flag rides beside an entry point, which is what keeps the run a
    // build rather than a transform; the config file is still there and still
    // has a banner of its own to lose.
    EXPECT_EQ(RunCli({"build", "entry.js", "--outfile=flag.js", "--banner=/*! from flag */"})
                  .exit_code,
              kSuccess);

    const std::string flag_banner = "/*! from flag */";
    const std::string flag        = ws.Read("flag.js");
    ASSERT_GE(flag.size(), flag_banner.size())
        << "the output is shorter than its own banner: [" << flag << "]";
    EXPECT_EQ(flag.substr(0, flag_banner.size()), flag_banner)
        << "the flag's banner is not first in: [" << flag << "]";
    EXPECT_EQ(flag.find(config_banner), std::string::npos)
        << "the config's banner survived a flag that overrode it: [" << flag << "]";
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

// The other direction for bundling: a config file that says "bundle: true"
// gets a bundle. The import the entry writes is the proof either way — it is
// in the output when the graph was not followed, and gone when it was, and
// the string literal only the helper carries proves the other half.
TEST(CliBuild, ConfigBundleTurnsOnBundling) {
    CliWorkspace ws("bundle-from-config");
    ws.Write("entry.js",
             "import { helperOnly } from \"./helper.js\";\nconsole.log(helperOnly);\n");
    ws.Write("helper.js", "export const helperOnly = \"from-helper\";\n");
    ws.Write("guchho.config.json",
             R"({"build":{"entry":"entry.js","outfile":"out.js","bundle":true}})");

    EXPECT_EQ(RunCli({"build"}).exit_code, kSuccess);

    const std::string out = ws.Read("out.js");
    EXPECT_FALSE(OutputContains(out, "./helper.js"))
        << "the import survived a config that asked for a bundle: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "from-helper"))
        << "the helper's body is missing from the bundle: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "console.log"))
        << "the entry's own code is missing from the bundle: [" << out << "]";
}

// The off half of the same field: "bundle: false" leaves the default alone,
// and the default is not to bundle, so the import stays an import for the
// loader to chase and the helper's body never enters the output.
TEST(CliBuild, ConfigBundleFalseLeavesBundlingOff) {
    CliWorkspace ws("bundle-off-from-config");
    ws.Write("entry.js",
             "import { helperOnly } from \"./helper.js\";\nconsole.log(helperOnly);\n");
    ws.Write("helper.js", "export const helperOnly = \"from-helper\";\n");
    ws.Write("guchho.config.json",
             R"({"build":{"entry":"entry.js","outfile":"out.js","bundle":false}})");

    EXPECT_EQ(RunCli({"build"}).exit_code, kSuccess);

    const std::string out = ws.Read("out.js");
    EXPECT_TRUE(OutputContains(out, "./helper.js"))
        << "a config that asked for no bundle got one anyway: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, "from-helper"))
        << "the helper was inlined into a build that was not bundling: [" << out << "]";
}

// The ranking, on the only field that can be answered in both directions: a
// config that asks for a bundle and a command line that explicitly declines
// one. The flag records itself as explicit, and the config's answer loses.
TEST(CliBuild, BundleFalseFlagOverridesAConfigThatBundles) {
    CliWorkspace ws("bundle-flag-off");
    ws.Write("entry.js",
             "import { helperOnly } from \"./helper.js\";\nconsole.log(helperOnly);\n");
    ws.Write("helper.js", "export const helperOnly = \"from-helper\";\n");
    ws.Write("guchho.config.json",
             R"({"build":{"entry":"entry.js","outfile":"out.js","bundle":true}})");

    EXPECT_EQ(RunCli({"build", "--bundle=false"}).exit_code, kSuccess);

    const std::string out = ws.Read("out.js");
    EXPECT_TRUE(OutputContains(out, "./helper.js"))
        << "\"--bundle=false\" left the config in charge: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, "from-helper"))
        << "the helper was inlined into a build the flag turned off: [" << out << "]";
}

// A CommonJS bundle is a script, so it is not strict unless it says so, and
// what the format owes its reader is a directive at the top of the file. The
// bundler snapshots cover the same ground, but only as something to be looked
// at: a snapshot says what the output looks like, not what a caller can rely
// on. Three things are claimed here that only the bytes can answer, so they
// are asked of the bytes:
//
//   1. the directive is at the very start of the file, not merely somewhere
//      in it - a directive that is not the first thing in the file is not a
//      prologue and does nothing;
//   2. there is exactly one of them, including when the input asked for strict
//      mode itself, because a duplicate would be the two-claims-of-the-same
//      kind of thing the ESM path avoids by dropping the input's;
//   3. minification leaves it alone, including the whitespace pass, which is
//      the pass that would take the newline after it.
//
// The entry point imports nothing on purpose. A bundle of several files can
// legitimately carry more than one "use strict", because an inlined CommonJS
// file keeps the one it was written with, and counting the whole file would
// then be counting something this test is not about.
TEST(CliBuild, CJSOutputStartsWithUseStrictExactlyOnce) {
    CliWorkspace ws("cjs-use-strict");
    ws.Write("entry.js", "export let a = compute(1);\nconsole.log(a);\n");

    struct Case {
        std::string flag;
        std::string outfile;
    };
    const Case cases[] = {
        {"",                 "plain.js"},
        {"--minify",         "all.js"},
        {"--minify-syntax",  "syntax.js"},
        {"--minify-whitespace", "ws.js"},
    };

    for (const Case& one : cases) {
        std::vector<std::string> args{"build", "entry.js",
                                      "--format=cjs", "--outfile=" + one.outfile};
        if (!one.flag.empty()) {
            args.push_back(one.flag);
        }
        ASSERT_EQ(RunCli(args).exit_code, kSuccess) << one.outfile;
    }

    auto occurrences_of = [](const std::string& haystack, const std::string& needle) {
        long count = 0;
        for (std::string::size_type at = haystack.find(needle);
             at != std::string::npos;
             at = haystack.find(needle, at + needle.size())) {
            count++;
        }
        return count;
    };

    for (const Case& one : cases) {
        const std::string out = ws.Read(one.outfile);

        // The program is in every one of them, so what the checks below say
        // about the directive is about the directive rather than about a
        // build that failed and wrote an error into the file.
        EXPECT_TRUE(OutputContains(out, "compute(1)"))
            << "[" << one.outfile << "] did not build the program: [" << out << "]";

        EXPECT_EQ(out.rfind("\"use strict\";", 0), 0U)
            << "the cjs build does not start with the directive, so it is not a prologue: ["
            << out << "]";
        EXPECT_EQ(occurrences_of(out, "\"use strict\""), 1L)
            << one.outfile << " carries the directive "
            << occurrences_of(out, "\"use strict\"") << " times: [" << out << "]";
    }

    // The same build from an entry that already asks for strict mode. This is
    // the case where the linker would have added a second one, so the count is
    // the whole point: still one.
    ws.Write("strict-entry.js", "'use strict'\nexport let a = compute(1);\nconsole.log(a);\n");
    EXPECT_EQ(RunCli({"build", "strict-entry.js", "--format=cjs", "--outfile=strict.js"}).exit_code,
              kSuccess);

    const std::string strict = ws.Read("strict.js");
    EXPECT_EQ(occurrences_of(strict, "\"use strict\""), 1L)
        << "an entry that already asked for strict mode produced a second directive: ["
        << strict << "]";

    // ESM is the other half of the claim: an ES module is strict already, so
    // adding a directive to it would be saying something twice, and the
    // existing behaviour is that the input's own is dropped rather than
    // carried over. Either way the answer here has to be none.
    EXPECT_EQ(RunCli({"build", "strict-entry.js", "--format=esm", "--outfile=esm.js"}).exit_code,
              kSuccess);

    const std::string esm = ws.Read("esm.js");
    EXPECT_EQ(occurrences_of(esm, "\"use strict\""), 0L)
        << "an esm bundle grew a strict-mode directive it does not need: [" << esm << "]";
}

// ---------------------------------------------------------------------------
// UMD: the global name flag and what it is required for
// ---------------------------------------------------------------------------

// "--name" is the documented spelling and "--global-name" is the older one, and
// both have to reach the same thing: a namespace on the browser branch of the
// wrapper. The output is checked as well as the exit code, because a flag that
// is accepted and dropped still succeeds.
TEST(CliBuild, TheNameFlagPublishesTheNamespace) {
    struct Case {
        std::string flag;
        std::string outfile;
    };
    const Case cases[] = {
        {"--name=Lib",        "short.js"},
        {"--global-name=Lib", "long.js"},
    };

    for (const Case& one : cases) {
        CliWorkspace ws("umd-name");
        ws.Write("entry.js", "export const add = (a, b) => a + b;\n");

        const CliResult result = RunCli({"build", "entry.js", "--format=umd",
                                         one.flag, "--outfile=" + one.outfile});
        ASSERT_EQ(result.exit_code, kSuccess) << result.err;

        const std::string out = ws.Read(one.outfile);
        EXPECT_TRUE(OutputContains(out, "global.Lib")) << one.flag << ": [" << out << "]";
        EXPECT_TRUE(OutputContains(out, "exports.add = add"))
            << one.flag << " did not assign the export directly: [" << out << "]";
        EXPECT_FALSE(OutputContains(out, "__toCommonJS"))
            << one.flag << " still converts a namespace on the way out: [" << out << "]";
    }
}

// A name that is not a global path has to be reported at the point it is
// rejected. This one is here because the check that rejects it runs before the
// build starts, and that stage used to return its errors without printing them:
// the exit code said "failed" and the terminal said nothing about why.
TEST(CliBuild, AnUnparseableNameIsReported) {
    CliWorkspace ws("umd-bad-name");
    ws.Write("entry.js", "export const add = (a, b) => a + b;\n");

    const CliResult result = RunCli({"build", "entry.js", "--format=umd",
                                     "--name=1bad", "--outfile=out.js"});

    EXPECT_EQ(result.exit_code, kBuildFailure) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "1bad"))
        << "the failure does not show what was rejected: [" << result.err << "]";
}

// The other half of the name requirement: a UMD entry that exports something
// and names no namespace cannot be published anywhere, so the build has to stop
// and say which flag is missing rather than writing a bundle whose browser
// branch is unreachable.
TEST(CliBuild, UMDWithExportsAndNoNameIsABuildFailure) {
    CliWorkspace ws("umd-no-name");
    ws.Write("entry.js", "export const add = (a, b) => a + b;\n");

    const CliResult result = RunCli({"build", "entry.js", "--format=umd",
                                     "--outfile=out.js"});

    EXPECT_EQ(result.exit_code, kBuildFailure) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "--name"))
        << "the failure does not name the flag that would fix it: [" << result.err << "]";
    EXPECT_FALSE(ws.Exists("out.js"))
        << "a failed UMD build wrote its output anyway";
}

// The other half again, inverted: an entry that only runs side effects has
// nothing to publish, so a name is not asked for. This is the boundary between
// the two tests above, and it is where the requirement would be wrong if it
// were applied to the format rather than to the entry's exports.
TEST(CliBuild, UMDWithNoExportsAndNoNameIsAllowed) {
    CliWorkspace ws("umd-side-effect");
    ws.Write("entry.js", "console.log(\"ran\");\n");

    const CliResult result = RunCli({"build", "entry.js", "--format=umd",
                                     "--outfile=out.js"});

    EXPECT_EQ(result.exit_code, kSuccess) << result.err;
    EXPECT_TRUE(OutputContains(ws.Read("out.js"), "console.log(\"ran\")"));
}

// ---------------------------------------------------------------------------
// UMD: the minified wrapper
// ---------------------------------------------------------------------------

// The wrapper of a "--minify" UMD build must come back with no whitespace in
// it that is only there to be read, and with the expression rewrites the body
// gets. Both halves matter: the wrapper used to be assembled as text after the
// body had been minified, so it kept the spaces around its operators, its
// "define.amd ?" question mark and its unminified "global, factory" parameter
// names while everything inside the factory was minified around them.
//
// The spaces that survive are the ones that cannot go: between "typeof" and
// its operand, inside "void 0", and inside the "use strict" directive. Those
// are asserted by name, because "no unnecessary whitespace" proved easy to
// satisfy by deleting the wrong thing.
TEST(CliBuild, MinifiedUMDOutputHasNoUnnecessaryWhitespace) {
    CliWorkspace ws("umd-minified-ws");
    ws.Write("entry.js", "export const add = (a, b) => a + b;\nconsole.log(add(2, 3));\n");

    const CliResult result = RunCli({"build", "entry.js", "--format=umd", "--name=Lib",
                                     "--minify", "--outfile=out.js"});
    ASSERT_EQ(result.exit_code, kSuccess) << result.err;
    const std::string out = ws.Read("out.js");

    // One line: the wrapper and the body collapsed onto it together. The
    // newline that ends the file, if the printer wrote one, is not a line
    // break in the middle of the output and is not what this counts.
    EXPECT_LE(std::count(out.begin(), out.end(), '\n'), 1L)
        << "the minified output still breaks into lines: [" << out << "]";

    // Spacing that was only there for a reader, around every operator the
    // template used to spell by hand.
    for (const char* pattern : {" === ", " !== ", " && ", " || ", " ? ", " : ", ", ",
                                "( ", " )", "{ ", " }", "; "}) {
        EXPECT_FALSE(OutputContains(out, pattern))
            << "\"" << pattern << "\" survived minification: [" << out << "]";
    }

    // The dispatcher's parameters are identifiers, so they were minified
    // rather than left as "global" and "factory".
    EXPECT_FALSE(OutputContains(out, "(function(global"))
        << "the wrapper parameters were not minified: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, ",factory)"))
        << "the wrapper parameters were not minified: [" << out << "]";

    // Expression minification reached the wrapper: the comparisons were
    // rewritten the same way the body's are, which is what puts the constants
    // in the form the binder chooses rather than the form the template
    // spelled.
    EXPECT_TRUE(OutputContains(out, "typeof exports==\"object\""))
        << "the wrapper's comparisons were not minified: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "define.amd?"))
        << "the AMD arm kept its reader-facing space: [" << out << "]";

    // The spaces that cannot go.
    EXPECT_TRUE(OutputContains(out, "typeof ")) << "the typeof space was fused: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "void 0")) << "void 0 lost its space: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "\"use strict\""))
        << "the directive did not survive: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "exports.add="))
        << "the export assignment is missing: [" << out << "]";
}

// The other half of the same requirement: the wrapper that comes out of the
// minification pipeline still has to be a working UMD wrapper. Each of the
// three arms gets exercised the way it is loaded for real — the file run as a
// script, the file required as a module, and the file evaluated where the only
// entry point is the global the browser arm publishes — plus the AMD arm
// through a define() that records what the factory was handed.
//
// node is the executor because it is already the suite's other half's runtime;
// where it is not on the PATH the child never starts, which RunProcess reports
// rather than hiding, and the test says so and stops. What is being proven is
// that the minified wrapper executes, not that this machine has node.
TEST(CliBuild, MinifiedUMDOutputExecutes) {
    CliWorkspace ws("umd-minified-exec");
    ws.Write("entry.js", "export const add = (a, b) => a + b;\nconsole.log(add(2, 3));\n");

    const CliResult build = RunCli({"build", "entry.js", "--format=umd", "--name=Lib",
                                    "--minify", "--outfile=out.js"});
    ASSERT_EQ(build.exit_code, kSuccess) << build.err;

    // As a script. Node loads the file as CommonJS, so this runs the
    // CommonJS arm, and the entry's own console.log is the bundle having
    // executed far enough to call its own body.
    const ProcessResult script = RunProcess({"node", "out.js"}, ws.path());
    if (!script.started) {
        std::cout << "node is not on PATH; skipping the execution half of "
                     "MinifiedUMDOutputExecutes"
                  << std::endl;
        return;
    }
    EXPECT_EQ(script.exit_code, 0) << script.stderr_data;
    EXPECT_TRUE(OutputContains(script.stdout_data, "5"))
        << "the bundle did not run to its own console.log: [" << script.stdout_data << "]";

    // As a module: the CommonJS arm passes the exports object in, so what
    // comes back out of require() is what the wrapper assembled.
    const ProcessResult as_module = RunProcess(
        {"node", "-e", "const m = require('./out.js'); process.stdout.write(String(m.add(40, 2)));"},
        ws.path());
    ASSERT_TRUE(as_module.started);
    EXPECT_EQ(as_module.exit_code, 0) << as_module.stderr_data;
    EXPECT_TRUE(OutputContains(as_module.stdout_data, "42"))
        << "require() did not hand back the wrapper's exports: [" << as_module.stdout_data << "]";

    // As a browser would load it: a context with no module, no exports and no
    // define, where the only arm left is the global one and the namespace has
    // to appear on the global object.
    const ProcessResult as_global = RunProcess(
        {"node", "-e",
         "const fs = require('fs'), vm = require('vm');"
         "const ctx = { console: { log: function () {} } };"
         "ctx.globalThis = ctx; ctx.self = ctx;"
         "vm.runInNewContext(fs.readFileSync('out.js', 'utf8'), ctx);"
         "process.stdout.write(String(ctx.Lib.add(1, 2)));"},
        ws.path());
    ASSERT_TRUE(as_global.started);
    EXPECT_EQ(as_global.exit_code, 0) << as_global.stderr_data;
    EXPECT_TRUE(OutputContains(as_global.stdout_data, "3"))
        << "the global arm did not publish a working namespace: [" << as_global.stdout_data << "]";

    // As AMD: define() receives the dependency list and the factory, and the
    // object the factory writes into is what the arm has to deliver.
    const ProcessResult as_amd = RunProcess(
        {"node", "-e",
         "const fs = require('fs'), vm = require('vm');"
         "let got = null;"
         "function define(deps, factory) { const exp = {}; factory(exp); got = { deps: deps, exp: exp }; }"
         "define.amd = true;"
         "const ctx = { console: { log: function () {} }, define: define };"
         "ctx.globalThis = ctx; ctx.self = ctx;"
         "vm.runInNewContext(fs.readFileSync('out.js', 'utf8'), ctx);"
         "process.stdout.write(got.deps.join('|') + ':' + String(got.exp.add(2, 2)));"},
        ws.path());
    ASSERT_TRUE(as_amd.started);
    EXPECT_EQ(as_amd.exit_code, 0) << as_amd.stderr_data;
    EXPECT_TRUE(OutputContains(as_amd.stdout_data, "exports:4"))
        << "the AMD arm did not deliver through define(): [" << as_amd.stdout_data << "]";
}

// ---------------------------------------------------------------------------
// UMD: "--exports=default"
// ---------------------------------------------------------------------------

// The shape of the wrapper in default mode: the value every arm receives is
// the factory's return, so the CommonJS arm assigns it to "module.exports",
// the AMD arm has no "exports" dependency for the loader to hand the factory,
// and the browser arm assigns the return straight to the named global instead
// of building an object for the body to fill. The named exports the entry also
// declares are exposed nowhere, which is the point of the mode.
TEST(CliBuild, ExportsDefaultPublishesTheDefaultExportDirectly) {
    CliWorkspace ws("umd-exports-default");
    ws.Write("entry.js", "export default class Widget {\n"
                         "  constructor() { this.answer = 42; }\n"
                         "}\n"
                         "export const ignored = 99;\n");

    const CliResult result = RunCli({"build", "entry.js", "--format=umd",
                                     "--exports=default", "--name=Widget",
                                     "--outfile=out.js"});
    ASSERT_EQ(result.exit_code, kSuccess) << result.err;
    const std::string out = ws.Read("out.js");

    EXPECT_TRUE(OutputContains(out, "module.exports = factory(require)"))
        << "the CommonJS arm does not take the factory's return: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "define([], factory)"))
        << "the AMD arm still asks for an exports object: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "global.Widget = factory(void 0)"))
        << "the browser arm does not assign the return to the global: [" << out << "]";
    EXPECT_TRUE(OutputContains(out, "return Widget;"))
        << "the factory does not hand back the default export: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, "exports.default"))
        << "the default was published through the exports object: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, "exports.ignored"))
        << "a named export was published in default mode: [" << out << "]";
}

// Each of the three arms executed the way it is loaded for real. The CommonJS
// arm through require(), the browser arm in a context with no module system at
// all, and the AMD arm through a define() that keeps the factory's return --
// which in default mode is the whole module value, where the namespace mode's
// AMD arm instead kept what the factory wrote into an exports object.
TEST(CliBuild, ExportsDefaultDeliversTheDefaultExportThroughEveryArm) {
    CliWorkspace ws("umd-exports-default-exec");
    ws.Write("entry.js", "export default class W {\n"
                         "  constructor() { this.n = 42; }\n"
                         "}\n");

    const CliResult build = RunCli({"build", "entry.js", "--format=umd",
                                    "--exports=default", "--name=W",
                                    "--outfile=out.js"});
    ASSERT_EQ(build.exit_code, kSuccess) << build.err;

    const ProcessResult as_module = RunProcess(
        {"node", "-e", "const W = require('./out.js'); process.stdout.write(String(new W().n));"},
        ws.path());
    if (!as_module.started) {
        std::cout << "node is not on PATH; skipping the execution half of "
                     "ExportsDefaultDeliversTheDefaultExportThroughEveryArm"
                  << std::endl;
        return;
    }
    EXPECT_EQ(as_module.exit_code, 0) << as_module.stderr_data;
    EXPECT_TRUE(OutputContains(as_module.stdout_data, "42"))
        << "require() did not hand back the default export: [" << as_module.stdout_data << "]";

    const ProcessResult as_global = RunProcess(
        {"node", "-e",
         "const fs = require('fs'), vm = require('vm');"
         "const ctx = {}; ctx.globalThis = ctx; ctx.self = ctx;"
         "vm.runInNewContext(fs.readFileSync('out.js', 'utf8'), ctx);"
         "process.stdout.write(String(new ctx.W().n));"},
        ws.path());
    ASSERT_TRUE(as_global.started);
    EXPECT_EQ(as_global.exit_code, 0) << as_global.stderr_data;
    EXPECT_TRUE(OutputContains(as_global.stdout_data, "42"))
        << "the global arm did not publish the default export: [" << as_global.stdout_data << "]";

    const ProcessResult as_amd = RunProcess(
        {"node", "-e",
         "const fs = require('fs'), vm = require('vm');"
         "let got = null;"
         "function define(deps, factory) { got = { len: deps.length, val: factory() }; }"
         "define.amd = true;"
         "const ctx = { define: define }; ctx.globalThis = ctx; ctx.self = ctx;"
         "vm.runInNewContext(fs.readFileSync('out.js', 'utf8'), ctx);"
         "process.stdout.write(String(got.len) + ':' + String(new got.val().n));"},
        ws.path());
    ASSERT_TRUE(as_amd.started);
    EXPECT_EQ(as_amd.exit_code, 0) << as_amd.stderr_data;
    EXPECT_TRUE(OutputContains(as_amd.stdout_data, "0:42"))
        << "the AMD arm did not keep the factory's return: [" << as_amd.stdout_data << "]";
}

// The default may be a plain function, and in default mode the function
// itself is what every arm delivers: the global is called directly, with no
// ".default" to reach through.
TEST(CliBuild, ExportsDefaultPublishesADefaultFunctionAsTheGlobal) {
    CliWorkspace ws("umd-exports-default-fn");
    ws.Write("entry.js", "export default function Exprify() {\n"
                         "  return 42;\n"
                         "}\n");

    const CliResult build = RunCli({"build", "entry.js", "--format=umd",
                                    "--exports=default", "--name=Exprify",
                                    "--outfile=out.js"});
    ASSERT_EQ(build.exit_code, kSuccess) << build.err;

    if (!RunProcess({"node", "-v"}, ws.path()).started) {
        std::cout << "node is not on PATH; skipping the execution half of "
                     "ExportsDefaultPublishesADefaultFunctionAsTheGlobal"
                  << std::endl;
        return;
    }

    const ProcessResult as_module = RunProcess(
        {"node", "-e", "process.stdout.write(String(require('./out.js')()));"},
        ws.path());
    EXPECT_EQ(as_module.exit_code, 0) << as_module.stderr_data;
    EXPECT_TRUE(OutputContains(as_module.stdout_data, "42"))
        << "require() did not hand back the default function: [" << as_module.stdout_data << "]";

    const ProcessResult as_global = RunProcess(
        {"node", "-e",
         "const fs = require('fs'), vm = require('vm');"
         "const ctx = {}; ctx.globalThis = ctx; ctx.self = ctx;"
         "vm.runInNewContext(fs.readFileSync('out.js', 'utf8'), ctx);"
         "process.stdout.write(String(ctx.Exprify()));"},
        ws.path());
    ASSERT_TRUE(as_global.started);
    EXPECT_EQ(as_global.exit_code, 0) << as_global.stderr_data;
    EXPECT_TRUE(OutputContains(as_global.stdout_data, "42"))
        << "the global is not the default function itself: [" << as_global.stdout_data << "]";
}

// Default and named exports together: the default is the value and the named
// ones are exposed nowhere, and the build says nothing about it -- the
// combination is representable in this mode, so reporting it would be
// inventing a problem.
TEST(CliBuild, ExportsDefaultExposesOnlyTheDefaultAmongSeveralExports) {
    CliWorkspace ws("umd-exports-default-mixed");
    ws.Write("entry.js", "export default 7;\n"
                         "export const named = 99;\n"
                         "export function fn() { return named; }\n");

    const CliResult result = RunCli({"build", "entry.js", "--format=umd",
                                     "--exports=default", "--name=Mixed",
                                     "--outfile=out.js"});
    ASSERT_EQ(result.exit_code, kSuccess) << result.err;
    const std::string out = ws.Read("out.js");

    EXPECT_FALSE(OutputContains(out, "exports.default"))
        << "the default went through the exports object: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, "exports.named"))
        << "a named export was published alongside it: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, "exports.fn"))
        << "a named export was published alongside it: [" << out << "]";

    const ProcessResult as_module = RunProcess(
        {"node", "-e", "process.stdout.write(String(require('./out.js')));"}, ws.path());
    if (!as_module.started) {
        std::cout << "node is not on PATH; skipping the execution half of "
                     "ExportsDefaultExposesOnlyTheDefaultAmongSeveralExports"
                  << std::endl;
        return;
    }
    EXPECT_EQ(as_module.exit_code, 0) << as_module.stderr_data;
    EXPECT_TRUE(OutputContains(as_module.stdout_data, "7"))
        << "require() did not hand back the default export: [" << as_module.stdout_data << "]";
}

// A CommonJS entry in default mode: "module.exports" is its default export,
// and the entry is wrapped so the assignment lands on the wrapper's own
// module rather than the host's. require() therefore comes back with the
// value the entry assigned, and the browser arm runs without reaching for a
// "module" it does not have.
TEST(CliBuild, ExportsDefaultRunsACjsEntryInItsOwnWrapper) {
    CliWorkspace ws("umd-exports-default-cjs");
    ws.Write("entry.js", "module.exports = function Ctor() { this.q = 9; };\n");

    const CliResult build = RunCli({"build", "entry.js", "--format=umd",
                                    "--exports=default", "--name=Ctor",
                                    "--outfile=out.js"});
    ASSERT_EQ(build.exit_code, kSuccess) << build.err;

    if (!RunProcess({"node", "-v"}, ws.path()).started) {
        std::cout << "node is not on PATH; skipping the execution half of "
                     "ExportsDefaultRunsACjsEntryInItsOwnWrapper"
                  << std::endl;
        return;
    }

    const ProcessResult as_module = RunProcess(
        {"node", "-e", "const C = require('./out.js'); process.stdout.write(String(new C().q));"},
        ws.path());
    EXPECT_EQ(as_module.exit_code, 0) << as_module.stderr_data;
    EXPECT_TRUE(OutputContains(as_module.stdout_data, "9"))
        << "require() did not hand back module.exports: [" << as_module.stdout_data << "]";

    const ProcessResult as_global = RunProcess(
        {"node", "-e",
         "const fs = require('fs'), vm = require('vm');"
         "const ctx = {}; ctx.globalThis = ctx; ctx.self = ctx;"
         "vm.runInNewContext(fs.readFileSync('out.js', 'utf8'), ctx);"
         "process.stdout.write(String(new ctx.Ctor().q));"},
        ws.path());
    ASSERT_TRUE(as_global.started);
    EXPECT_EQ(as_global.exit_code, 0) << as_global.stderr_data;
    EXPECT_TRUE(OutputContains(as_global.stdout_data, "9"))
        << "the global arm did not run the CommonJS body: [" << as_global.stdout_data << "]";
}

// The mode asks for a default export, so an entry without one cannot be
// published and has to stop with that said rather than handing every module
// system "undefined".
TEST(CliBuild, ExportsDefaultWithoutADefaultExportIsABuildFailure) {
    CliWorkspace ws("umd-exports-default-none");
    ws.Write("entry.js", "export const onlyNamed = 1;\n");

    const CliResult result = RunCli({"build", "entry.js", "--format=umd",
                                     "--exports=default", "--name=X",
                                     "--outfile=out.js"});

    EXPECT_EQ(result.exit_code, kBuildFailure) << result.err;
    EXPECT_TRUE(OutputContains(result.err,
                               "--exports=default requires the entry point to have a default export"))
        << "the failure does not say what is missing: [" << result.err << "]";
    EXPECT_FALSE(ws.Exists("out.js"))
        << "a failed build wrote its output anyway";
}

// The option only means anything for UMD, where there is a wrapper with arms
// to publish through; a format whose exports are already the module's value
// is told so rather than silently ignoring the flag. Every other output
// format Guchho has gets the same answer.
TEST(CliBuild, ExportsDefaultOutsideUmdIsRejected) {
    CliWorkspace ws("umd-exports-default-format");
    ws.Write("entry.js", "export default 1;\n");

    for (const char* format : {"esm", "cjs", "iife", "amd", "system"}) {
        const CliResult result = RunCli({"build", "entry.js",
                                         std::string("--format=") + format,
                                         "--exports=default", "--outfile=out.js"});

        EXPECT_EQ(result.exit_code, kBuildFailure) << format << ": " << result.err;
        EXPECT_TRUE(OutputContains(result.err,
                                   "--exports=default is only supported with --format=umd"))
            << format << " does not say which format is required: [" << result.err << "]";
        EXPECT_FALSE(ws.Exists("out.js")) << format << " wrote output anyway";
    }
}

// An unknown value stops on the command line itself, with the note naming the
// one value that does exist.
TEST(CliBuild, AnUnknownExportsValueIsAUsageError) {
    CliWorkspace ws("umd-exports-default-value");
    ws.Write("entry.js", "export default 1;\n");

    const CliResult result = RunCli({"build", "entry.js", "--format=umd",
                                     "--exports=namespace", "--name=X",
                                     "--outfile=out.js"});

    EXPECT_EQ(result.exit_code, kUsageError) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "Invalid value"))
        << "the failure does not quote what was rejected: [" << result.err << "]";
    EXPECT_TRUE(OutputContains(result.err, "\"default\""))
        << "the note does not name the valid value: [" << result.err << "]";
}

// Minified default mode: the wrapper still parses, Guchho still reads its own
// output back as source, Node still executes it, and the class behind the
// global is still the default. Semantics, not bytes -- the minifier is
// allowed to spell the wrapper however it likes.
TEST(CliBuild, ExportsDefaultSurvivesMinification) {
    CliWorkspace ws("umd-exports-default-min");
    ws.Write("entry.js", "export default class Exprify {\n"
                         "  constructor() { this.value = 42; }\n"
                         "}\n");

    const CliResult build = RunCli({"build", "entry.js", "--format=umd",
                                    "--exports=default", "--name=Exprify",
                                    "--minify", "--outfile=out.js"});
    ASSERT_EQ(build.exit_code, kSuccess) << build.err;
    ASSERT_TRUE(ws.Exists("out.js"));

    // Guchho reads its own output: building the bundle again as an entry is
    // a full lex-parse round trip through the real pipeline.
    const CliResult reparsed = RunCli({"build", "out.js", "--outfile=again.js"});
    EXPECT_EQ(reparsed.exit_code, kSuccess) << reparsed.err;

    if (!RunProcess({"node", "-v"}, ws.path()).started) {
        std::cout << "node is not on PATH; skipping the execution half of "
                     "ExportsDefaultSurvivesMinification"
                  << std::endl;
        return;
    }

    const ProcessResult as_module = RunProcess(
        {"node", "-e",
         "const E = require('./out.js'); process.stdout.write(String(new E().value));"},
        ws.path());
    EXPECT_EQ(as_module.exit_code, 0) << as_module.stderr_data;
    EXPECT_TRUE(OutputContains(as_module.stdout_data, "42"))
        << "require() did not hand back the default class: [" << as_module.stdout_data << "]";

    const ProcessResult as_global = RunProcess(
        {"node", "-e",
         "const fs = require('fs'), vm = require('vm');"
         "const ctx = {}; ctx.globalThis = ctx; ctx.self = ctx;"
         "vm.runInNewContext(fs.readFileSync('out.js', 'utf8'), ctx);"
         "process.stdout.write(String(new ctx.Exprify().value));"},
        ws.path());
    ASSERT_TRUE(as_global.started);
    EXPECT_EQ(as_global.exit_code, 0) << as_global.stderr_data;
    EXPECT_TRUE(OutputContains(as_global.stdout_data, "42"))
        << "the global is not the default class after minification: ["
        << as_global.stdout_data << "]";
}

// Asking for a map alongside the new mode still writes one, and it is a real
// map: a file next to the bundle, referenced from the bundle, carrying the
// fields a consumer reads.
TEST(CliBuild, ExportsDefaultStillEmitsASourceMap) {
    CliWorkspace ws("umd-exports-default-map");
    ws.Write("entry.js", "export default 42;\n");

    const CliResult build = RunCli({"build", "entry.js", "--format=umd",
                                    "--exports=default", "--name=M",
                                    "--sourcemap", "--outfile=out.js"});
    ASSERT_EQ(build.exit_code, kSuccess) << build.err;

    ASSERT_TRUE(ws.Exists("out.js"));
    ASSERT_TRUE(ws.Exists("out.js.map"))
        << "no map file was written next to the bundle";
    EXPECT_TRUE(OutputContains(ws.Read("out.js"), "sourceMappingURL"))
        << "the bundle does not reference its map";

    const std::string map = ws.Read("out.js.map");
    EXPECT_TRUE(OutputContains(map, "\"mappings\""))
        << "the map carries no mappings: [" << map << "]";
    EXPECT_TRUE(OutputContains(map, "\"sources\""))
        << "the map names no sources: [" << map << "]";
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

// The entry point is echoed back with the separator of the shell it was
// spelled with, which on Windows is a backslash that reads as an escape in
// the message. The diagnostic reports forward slashes on every platform so
// the same mistake prints identically everywhere; the spelling handed to the
// file system is untouched, which is why the build still fails the same way.
TEST(CliBuild, AMissingEntryPointIsReportedWithForwardSlashes) {
    CliWorkspace ws("missing-backslash");

    const guchho::test::CliResult result =
        RunCli({"build", "src\\index.js", "--outdir=dist"});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "src/index.js"));
    EXPECT_EQ(result.err.find("src\\index.js"), std::string::npos) << result.err;
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
        "--exports=default",
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
