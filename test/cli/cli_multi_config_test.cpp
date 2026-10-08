// Tests for a configuration file that describes more than one build.
//
// A "guchho.config.json" whose root is an array is one configuration per
// element, and "guchho build" builds all of them. What is tested here is the
// part no lower layer owns: that the command loops over every configuration,
// that each one is resolved from the same explicit options and the same
// defaults without leaking into its neighbours, that a request no single build
// could honour is refused rather than attached to the first configuration, and
// that a configuration which cannot be used stops the run.
//
// JSON configs are used rather than "guchho.config.js" so the tests never spawn
// Node: the array root reaches the loader through the same text path either
// way, and the resolver's own tests already cover the JS-specific wrapper.

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <string>

namespace cli::test {

using guchho::test::CliResult;
using guchho::test::CliWorkspace;
using guchho::test::kSuccess;
using guchho::test::kUsageError;
using guchho::test::OutputContains;
using guchho::test::RunCli;

namespace {

// A document entry point with the script it loads, so a build of it produces
// two files and the assertion can tell a bundle from a copy.
void WriteHtml(const CliWorkspace& ws, const std::string& name, const std::string& script) {
    ws.Write(name, "<!DOCTYPE html><html><body><script src=\"" + script +
                       "\"></script></body></html>\n");
    ws.Write(script, "console.log(\"" + script + "\");\n");
}

size_t CountOccurrences(const std::string& text, const std::string& needle) {
    size_t count = 0;
    size_t at    = 0;
    while ((at = text.find(needle, at)) != std::string::npos) {
        ++count;
        at += needle.size();
    }
    return count;
}

} // namespace

// ---------------------------------------------------------------------------
// One configuration keeps behaving exactly as before
// ---------------------------------------------------------------------------

TEST(CliMultiConfig, ObjectRootBuildsOnceAndSaysNothingAboutCounts) {
    CliWorkspace ws("multi-single");
    WriteHtml(ws, "index.html", "app.js");
    ws.Write("guchho.config.json", "{\"build\":{\"entry\":\"index.html\",\"outdir\":\"dist\"}}");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("dist/index.html"));
    EXPECT_EQ(ws.CountMatching("dist/assets", "app-", ".js"), 1u);
    EXPECT_FALSE(OutputContains(result.out, "Build 1/"));
}

// ---------------------------------------------------------------------------
// An array root builds every configuration
// ---------------------------------------------------------------------------

TEST(CliMultiConfig, ArrayRootBuildsEveryConfiguration) {
    CliWorkspace ws("multi-array");
    ws.Write("a1.js", "console.log(1);\n");
    ws.Write("a2.js", "console.log(2);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a1.js\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"a2.js\",\"outdir\":\"two\"}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("one/a1.js")) << ws.Tree();
    EXPECT_TRUE(ws.Exists("two/a2.js")) << ws.Tree();
}

TEST(CliMultiConfig, EachConfigurationIsAnnounced) {
    CliWorkspace ws("multi-announce");
    ws.Write("a1.js", "console.log(1);\n");
    ws.Write("a2.js", "console.log(2);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a1.js\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"a2.js\",\"outdir\":\"two\"}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Build 1/2"));
    EXPECT_TRUE(OutputContains(result.out, "Build 2/2"));
}

// Each configuration's entry point drives only its own build, so an HTML
// document with a script in one configuration does not appear in the other.
TEST(CliMultiConfig, ConfigurationsKeepTheirOwnEntries) {
    CliWorkspace ws("multi-entries");
    WriteHtml(ws, "a.html", "a.js");
    WriteHtml(ws, "b.html", "b.js");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a.html\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"b.html\",\"outdir\":\"two\"}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("one/a.html")) << ws.Tree();
    EXPECT_EQ(ws.CountMatching("one/assets", "a-", ".js"), 1u) << ws.Tree();
    EXPECT_TRUE(ws.Exists("two/b.html")) << ws.Tree();
    EXPECT_EQ(ws.CountMatching("two/assets", "b-", ".js"), 1u) << ws.Tree();
    EXPECT_FALSE(ws.Exists("one/b.html"));
}

// A field named on the command line overrides that field in every
// configuration, which is the same ranking a single configuration gets.
TEST(CliMultiConfig, ExplicitOutdirAppliesToEveryConfiguration) {
    CliWorkspace ws("multi-outdir");
    ws.Write("a1.js", "console.log(1);\n");
    ws.Write("a2.js", "console.log(2);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a1.js\"}}, {\"build\":{\"entry\":\"a2.js\"}}]");

    const CliResult result = RunCli({"build", "--outdir=shared"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("shared/a1.js")) << ws.Tree();
    EXPECT_TRUE(ws.Exists("shared/a2.js")) << ws.Tree();
}

// ---------------------------------------------------------------------------
// Each configuration's own format reaches its own output
// ---------------------------------------------------------------------------

// The reason the array root exists: two builds of one entry point, in two module
// systems, in one file. This is the test that was missing while the format was
// being dropped, because every other test in this file either agreed on the
// output directory or never mentioned a format — so two configurations asking
// for different formats used to write identical bytes to two different files,
// and every test in the suite still passed.
TEST(CliMultiConfig, EachConfigurationGetsItsOwnFormat) {
    CliWorkspace ws("multi-format");
    ws.Write("src/index.js", "export function add(a, b) {\n  return a + b;\n}\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"src/index.js\",\"outfile\":\"dist/index.cjs.js\","
             "\"format\":\"cjs\",\"target\":\"esnext\",\"minify\":true}},"
             " {\"build\":{\"entry\":\"src/index.js\",\"outfile\":\"dist/index.umd.js\","
             "\"format\":\"umd\",\"name\":\"Multi\",\"target\":\"esnext\",\"minify\":true}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kSuccess);
    ASSERT_TRUE(ws.Exists("dist/index.cjs.js")) << ws.Tree();
    ASSERT_TRUE(ws.Exists("dist/index.umd.js")) << ws.Tree();

    const std::string as_cjs = ws.Read("dist/index.cjs.js");
    const std::string as_umd = ws.Read("dist/index.umd.js");

    // The two are the whole point, so they are compared as wholes rather than
    // as a set of markers: equal output would satisfy every marker below and
    // still be the bug.
    EXPECT_NE(as_cjs, as_umd)
        << "two configurations asked for two formats and got the same bytes";

    // What each one is, named by the wrapper its format is defined by. These
    // survive minification, which is on above, so the assertion does not depend
    // on how pretty the output is.
    //
    // "module.exports" is not a test for cjs: a umd bundle contains it too, in
    // the branch that hands the exports to a CommonJS consumer. The marker for
    // cjs is the assignment with nothing in front of it, which only a cjs
    // output has, and the marker for umd is the AMD branch, which only a umd
    // output has.
    EXPECT_TRUE(OutputContains(as_cjs, "=f(") || OutputContains(as_cjs, ";module.exports"))
        << "the cjs build did not assign its exports through module.exports: ["
        << as_cjs << "]";
    EXPECT_FALSE(OutputContains(as_cjs, "define.amd"))
        << "the cjs build grew a umd wrapper: [" << as_cjs << "]";
    EXPECT_TRUE(OutputContains(as_umd, "define.amd"))
        << "the umd build has no AMD branch: [" << as_umd << "]";
    EXPECT_TRUE(OutputContains(as_umd, "globalThis"))
        << "the umd build has no browser-global branch: [" << as_umd << "]";

    // Neither is the entry point's own text any more, which is what "converted"
    // means and what distinguishes a real conversion from a copy that only
    // happens to differ in whitespace. The check is for the module keyword
    // rather than the function's name, because a minifier is free to rename
    // "add" and does.
    EXPECT_FALSE(OutputContains(as_cjs, "export{") ||
                 OutputContains(as_cjs, "export function"))
        << "the cjs build left the source as an ES module: [" << as_cjs << "]";
    EXPECT_FALSE(OutputContains(as_umd, "export{") ||
                 OutputContains(as_umd, "export function"))
        << "the umd build left the source as an ES module: [" << as_umd << "]";
}

// A format named in a config file is honoured whether or not the build
// bundles, because a lone .js entry is converted rather than bundled and the
// request means the same thing either way. This is the half that used to be
// dropped: the command line's "--format cjs" has always worked on an unbundled
// entry, through kConvertFormat, and the config file's "format" did not.
TEST(CliMultiConfig, AConfigFormatAppliesToAnUnbundledEntry) {
    CliWorkspace ws("multi-format-plain");
    ws.Write("a.js", "export function add(a, b) {\n  return a + b;\n}\n");
    ws.Write("guchho.config.json",
             "{\"build\":{\"entry\":\"a.js\",\"outdir\":\"dist\",\"format\":\"cjs\"}}");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kSuccess);
    ASSERT_TRUE(ws.Exists("dist/a.js")) << ws.Tree();
    const std::string out = ws.Read("dist/a.js");
    EXPECT_TRUE(OutputContains(out, "module.exports"))
        << "the configured format was ignored: [" << out << "]";
    EXPECT_FALSE(OutputContains(out, "export{"))
        << "the configured format was ignored: [" << out << "]";
}

// A config that says nothing about the format still gets the built-in default
// rather than the one a neighbouring configuration asked for. This is the
// other half of the same fix: carrying an explicit request across must not
// carry a request that was never made.
TEST(CliMultiConfig, AConfigurationWithoutAFormatKeepsTheDefault) {
    CliWorkspace ws("multi-format-default");
    ws.Write("a.js", "export function add(a, b) {\n  return a + b;\n}\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a.js\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"a.js\",\"outdir\":\"two\",\"format\":\"cjs\"}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kSuccess);
    ASSERT_TRUE(ws.Exists("one/a.js")) << ws.Tree();
    ASSERT_TRUE(ws.Exists("two/a.js")) << ws.Tree();

    const std::string defaulted = ws.Read("one/a.js");
    const std::string converted = ws.Read("two/a.js");
    EXPECT_TRUE(OutputContains(converted, "module.exports"))
        << "the configuration that asked for cjs did not get it: [" << converted << "]";
    // The one that named no format is still an ES module, and the check is for
    // the module keyword rather than for a particular name because a minifier
    // renames the function and rewrites the export as "export{...}"; either
    // spelling counts. Plain "export" would not do as a check, because it is a
    // substring of the "exports" in a umd wrapper.
    EXPECT_TRUE(OutputContains(defaulted, "export{") ||
                OutputContains(defaulted, "export function"))
        << "a configuration that asked for nothing was given a format anyway: ["
        << defaulted << "]";
    EXPECT_NE(defaulted, converted)
        << "the configuration that named no format produced the same bytes as the "
           "one that named cjs";
}

// ---------------------------------------------------------------------------
// Requests that cannot be shared between builds are refused
// ---------------------------------------------------------------------------

// A metafile describes one build's inputs and outputs, so writing one for
// several configurations would be a report of only the last one.
TEST(CliMultiConfig, MetafileIsRejectedWithSeveralConfigurations) {
    CliWorkspace ws("multi-metafile");
    ws.Write("a1.js", "console.log(1);\n");
    ws.Write("a2.js", "console.log(2);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a1.js\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"a2.js\",\"outdir\":\"two\"}}]");

    const CliResult result = RunCli({"build", "--metafile=meta.json"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(ws.Exists("meta.json"));
    EXPECT_FALSE(ws.Exists("one"));
}

TEST(CliMultiConfig, WatchIsRejectedWithSeveralConfigurations) {
    CliWorkspace ws("multi-watch");
    ws.Write("a1.js", "console.log(1);\n");
    ws.Write("a2.js", "console.log(2);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a1.js\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"a2.js\",\"outdir\":\"two\"}}]");

    const CliResult result = RunCli({"build", "--watch"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(ws.Exists("one"));
}

TEST(CliMultiConfig, MangleCacheIsRejectedWithSeveralConfigurations) {
    CliWorkspace ws("multi-mangle");
    ws.Write("a1.js", "console.log(1);\n");
    ws.Write("a2.js", "console.log(2);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a1.js\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"a2.js\",\"outdir\":\"two\"}}]");

    const CliResult result = RunCli({"build", "--mangle-cache=cache.json"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(ws.Exists("cache.json"));
    EXPECT_FALSE(ws.Exists("one"));
}

// ---------------------------------------------------------------------------
// A configuration that cannot be used stops the run
// ---------------------------------------------------------------------------

// Two configurations writing the same file would have the later overwrite the
// earlier, so the collision is named instead of discovered as a missing file.
// The message carries both 1-based positions in the array and the path they
// collide on, so the fix is a number and a name rather than a guess.
TEST(CliMultiConfig, ConfigurationsSharingAnOutputAreRejected) {
    CliWorkspace ws("multi-collision");
    ws.Write("a.js", "console.log(1);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a.js\",\"outdir\":\"dist\"}},"
             " {\"build\":{\"entry\":\"a.js\",\"outdir\":\"dist\"}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(ws.Exists("dist"));

    const std::string all = result.err + result.out;
    EXPECT_EQ(CountOccurrences(all, "configuration 1 and configuration 2 write to the same file"),
              size_t(1))
        << "message was: [" << all << "]";
    EXPECT_TRUE(OutputContains(all, ws.path()))
        << "the message does not name the path: [" << all << "]";
}

// The four-configuration shape: the third and the fourth share an output
// directory and an entry point, and the numbers in the message are positions
// in the array, so "3 and 4" points at the two elements to edit. Nothing is
// built — not even the first two configurations, which do not collide —
// because a run that cannot finish for two of four is refused whole.
TEST(CliMultiConfig, ThirdAndFourthConfigurationsSharingAnOutputAreRejected) {
    CliWorkspace ws("multi-collision-34");
    ws.Write("a.js", "console.log(1);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a.js\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"a.js\",\"outdir\":\"two\"}},"
             " {\"build\":{\"entry\":\"a.js\",\"outdir\":\"three\"}},"
             " {\"build\":{\"entry\":\"a.js\",\"outdir\":\"three\"}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(ws.Exists("one"));
    EXPECT_FALSE(ws.Exists("two"));
    EXPECT_FALSE(ws.Exists("three"));

    const std::string all = result.err + result.out;
    EXPECT_EQ(CountOccurrences(all, "configuration 3 and configuration 4 write to the same file"),
              size_t(1))
        << "message was: [" << all << "]";
    EXPECT_TRUE(OutputContains(all, ws.path()))
        << "the message does not name the path: [" << all << "]";
}

// The other side of the check above: configurations that resolve to different
// destinations are not collateral for it. Two named output files are two
// different destinations even though they come from the same entry point.
TEST(CliMultiConfig, ConfigurationsWithDistinctOutputsAreBuilt) {
    CliWorkspace ws("multi-distinct");
    ws.Write("a.js", "console.log(1);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a.js\",\"outfile\":\"first.js\"}},"
             " {\"build\":{\"entry\":\"a.js\",\"outfile\":\"second.js\"}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("first.js"));
    EXPECT_TRUE(ws.Exists("second.js"));
    EXPECT_EQ(CountOccurrences(result.err + result.out, "write to the same file"), size_t(0));
}

TEST(CliMultiConfig, EmptyArrayIsRejected) {
    CliWorkspace ws("multi-empty");
    ws.Write("guchho.config.json", "[]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(ws.Exists("dist"));
}

TEST(CliMultiConfig, ScalarRootIsRejected) {
    CliWorkspace ws("multi-scalar");
    ws.Write("guchho.config.json", "\"not a configuration\"");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(ws.Exists("dist"));
}

TEST(CliMultiConfig, NonObjectArrayElementIsRejected) {
    CliWorkspace ws("multi-element");
    ws.Write("guchho.config.json", "[{\"build\":{\"entry\":\"a.js\"}}, 5]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(ws.Exists("dist"));
}

// ---------------------------------------------------------------------------
// HTML-entry warnings are per configuration
// ---------------------------------------------------------------------------

// "format" is ignored for an HTML entry, and only the configuration that named
// one is told: the warning is not a property of the run.
TEST(CliMultiConfig, HtmlFormatWarningNamesOnlyTheConfigurationThatAsked) {
    CliWorkspace ws("multi-html-format");
    WriteHtml(ws, "a.html", "a.js");
    WriteHtml(ws, "b.html", "b.js");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a.html\",\"outdir\":\"one\",\"format\":\"iife\"}},"
             " {\"build\":{\"entry\":\"b.html\",\"outdir\":\"two\"}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_EQ(CountOccurrences(result.err + result.out, "is ignored when the entry point is HTML"),
              size_t(1));
}

} // namespace cli::test
