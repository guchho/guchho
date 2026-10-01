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
    EXPECT_TRUE(ws.Exists("dist/app.js"));
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
    EXPECT_TRUE(ws.Exists("one/a.js")) << ws.Tree();
    EXPECT_TRUE(ws.Exists("two/b.html")) << ws.Tree();
    EXPECT_TRUE(ws.Exists("two/b.js")) << ws.Tree();
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
TEST(CliMultiConfig, ConfigurationsSharingAnOutputAreRejected) {
    CliWorkspace ws("multi-collision");
    ws.Write("a.js", "console.log(1);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a.js\",\"outdir\":\"dist\"}},"
             " {\"build\":{\"entry\":\"a.js\",\"outdir\":\"dist\"}}]");

    const CliResult result = RunCli({"build"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(ws.Exists("dist"));
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
