// Tests for the plugin list a host hands to the command line.
//
// RunWithPlugins is the entry point for anything that is not a shell. An editor
// embedding the bundler cannot add a flag to a command line it does not own, so
// it attaches a plugin instead; that plugin can refuse a build, add a warning,
// resolve a path the built-in rules do not know about, or append an output file
// of its own. None of that is reachable from Run(), which is what makes this
// file worth having: everything else in test/cli can be checked by typing a
// command, and none of this can.
//
// The tests here are built around one rule, which is also the only safe way to
// test a plugin: never assert on what the plugin did, assert on what the engine
// did in response. A callback that records a flag and then checks the flag has
// proved only that the callback ran. A callback that returns an error and then
// checks that the build stopped has proved the engine obeyed it, which is the
// property a host program actually depends on.
//
// Two of these callbacks run on threads the engine owns — on_start is
// documented as running on its own thread alongside the others — so the
// counters here are atomic. A plain int would have passed on the machine the
// test was written on and failed on a machine with two cores.

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <atomic>
#include <string>
#include <vector>

#include "guchho/api.hpp"

namespace cli::test {

using guchho::test::CliWorkspace;
using guchho::test::kSuccess;
using guchho::test::OutputContains;
using guchho::test::RunCli;
using guchho::test::RunCliWithPlugins;
using guchho::api::Message;
using guchho::api::Plugin;

// A plugin that does nothing except announce that its setup was reached. The
// setup runs once per build, before anything is scanned, so this is the earliest
// point a host can observe.
Plugin CountingPlugin(std::atomic<int>& setups) {
    Plugin plugin;
    plugin.name = "counting";
    plugin.setup = [&setups](guchho::api::PluginBuild& build) {
        setups.fetch_add(1);
        // The build it was handed is a real one, wired to the options the
        // caller passed. A plugin that got an empty shell here would be unable
        // to adapt to the build it is part of, which is the whole reason the
        // argument is passed.
        EXPECT_TRUE(build.initial_options != nullptr);
    };
    return plugin;
}

TEST(CliPlugins, ASetupCallbackIsCalledForABuild) {
    CliWorkspace ws("setup");
    ws.Write("entry.js", "console.log(1);\n");
    std::atomic<int> setups{0};

    const guchho::test::CliResult result =
        RunCliWithPlugins({"build", "entry.js", "--outdir=dist"}, {CountingPlugin(setups)});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_EQ(setups.load(), 1);
}

// A finding, recorded as a test.
//
// on_start is documented as the place to reject a configuration, and as doing so
// before any file has been read. Neither happens here. A plugin attached to a
// one-shot "guchho build" has its setup called and its on_end callback called —
// AWarningFromAPluginReachesTheErrorStream passes — but its on_start callback is
// never invoked, so an error returned from it is discarded and the build runs to
// completion. Registration is not what is missing: setup runs, and build.on_start
// takes the callback without complaint. src/api/api.cpp records it in the
// plugin's OnStartList, and the scan phase is the only place that drains that
// list, so a build that never reaches the scan keeps the callback.
//
// A host embedding this and using on_start to refuse a build is relying on
// something that does not happen, which is worth more than a test that quietly
// agreed with it. So this asserts what happens, and the next test pins the
// companion fact that the callback is not called at all: if either half is fixed
// these two fail, and the fix is to change them rather than to delete them.
TEST(CliPlugins, AnErrorFromAStartCallbackIsDiscarded) {
    CliWorkspace ws("refuse");
    ws.Write("entry.js", "console.log(1);\n");

    Plugin refusing;
    refusing.name = "refusing";
    refusing.setup = [](guchho::api::PluginBuild& build) {
        build.on_start([]() {
            guchho::api::OnStartResult result;
            Message message;
            message.id          = "test-refused";
            message.plugin_name = "refusing";
            message.text        = "this build was refused by a plugin";
            result.errors.push_back(std::move(message));
            return result;
        });
    };

    const guchho::test::CliResult result =
        RunCliWithPlugins({"build", "entry.js", "--outdir=dist"}, {refusing});

    // The build a plugin asked to refuse: 0, no mention of the refusal, and the
    // output written.
    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_FALSE(OutputContains(result.err, "refused by a plugin"));
    EXPECT_TRUE(ws.Exists("dist/entry.js")) << "the build did not run either";
}

// The companion: the callback is accepted, registered, and never called. That
// on_start registration itself works is the useful half — it means the fix is
// in the build path rather than in the plugin interface, and a plugin author has
// nothing to change.
TEST(CliPlugins, AStartCallbackIsRegisteredAndNeverCalled) {
    CliWorkspace ws("agree");
    ws.Write("entry.js", "console.log(1);\n");
    std::atomic<int> starts{0};

    Plugin quiet;
    quiet.name = "quiet";
    quiet.setup = [&starts](guchho::api::PluginBuild& build) {
        build.on_start([&starts]() {
            starts.fetch_add(1);
            return guchho::api::OnStartResult{};
        });
    };

    const guchho::test::CliResult result =
        RunCliWithPlugins({"build", "entry.js", "--outdir=dist"}, {quiet});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_EQ(starts.load(), 0)
        << "on_start fired for a one-shot build, so the test above is out of "
           "date rather than the engine being wrong";
    EXPECT_TRUE(ws.Exists("dist/entry.js"));
}

// A plugin's messages are not kept to itself. This is how a plugin reports
// something about a finished bundle that the engine had no reason to notice —
// and it is the reason a host can use one at all, since a plugin that could not
// say anything would have to throw instead.
TEST(CliPlugins, AWarningFromAPluginReachesTheErrorStream) {
    CliWorkspace ws("warn");
    ws.Write("entry.js", "console.log(1);\n");

    Plugin warning;
    warning.name = "warning";
    warning.setup = [](guchho::api::PluginBuild& build) {
        build.on_end([](guchho::api::BuildResult&) {
            guchho::api::OnEndResult result;
            Message message;
            message.id          = "test-warning";
            message.plugin_name = "warning";
            message.text        = "a plugin has something to say about this bundle";
            result.warnings.push_back(std::move(message));
            return result;
        });
    };

    const guchho::test::CliResult result =
        RunCliWithPlugins({"build", "entry.js", "--outdir=dist"}, {warning});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.err, "something to say about this bundle"));
}

// The documented equivalence: a run with an empty list is a plain run, which is
// what lets Run() be a one-line wrapper. If this drifts, every host that passes
// an empty list has changed behaviour without changing a line.
TEST(CliPlugins, AnEmptyPluginListIsAPlainRun) {
    CliWorkspace ws("empty");
    ws.Write("entry.js", "console.log(1);\n");

    const guchho::test::CliResult with_run =
        RunCli({"build", "entry.js", "--outdir=dist2"});
    const guchho::test::CliResult with_none =
        RunCliWithPlugins({"build", "entry.js", "--outdir=dist"}, {});

    // The two runs are not compared wholesale, because neither stream can be:
    // each summary carries the time its own build took, the name of the
    // directory it wrote to, and — on the error stream, from the engine rather
    // than from the command line — the same three things again. What is compared
    // is the part that means something: the same code, nothing reported as an
    // error, and the same bytes in the two files.
    EXPECT_EQ(with_none.exit_code, with_run.exit_code);
    EXPECT_EQ(with_none.exit_code, kSuccess);
    EXPECT_FALSE(OutputContains(with_none.err, "ERROR"));
    EXPECT_FALSE(OutputContains(with_run.err, "ERROR"));
    EXPECT_TRUE(OutputContains(with_none.out, "entry.js"));
    EXPECT_TRUE(OutputContains(with_run.out, "entry.js"));
    EXPECT_TRUE(ws.Exists("dist/entry.js"));
    EXPECT_TRUE(ws.Exists("dist2/entry.js"));
    EXPECT_EQ(ws.Read("dist/entry.js"), ws.Read("dist2/entry.js"));
}

// Two plugins, and the engine runs both. Registration order is documented for
// end callbacks and is not asserted here; what is asserted is that a list is a
// list, and that attaching a second plugin does not displace the first — the
// failure mode where only the last plugin is ever reached is invisible to a
// host with one plugin.
TEST(CliPlugins, EveryPluginInTheListIsCalled) {
    CliWorkspace ws("two");
    ws.Write("entry.js", "console.log(1);\n");
    std::atomic<int> first{0};
    std::atomic<int> second{0};

    const guchho::test::CliResult result =
        RunCliWithPlugins({"build", "entry.js", "--outdir=dist"},
                          {CountingPlugin(first), CountingPlugin(second)});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_EQ(first.load(), 1);
    EXPECT_EQ(second.load(), 1);
}

} // namespace cli::test
