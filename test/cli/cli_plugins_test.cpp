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
using guchho::test::kBuildFailure;
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

// A plugin's on_start is the documented place to refuse a whole configuration,
// and the refusal has to be the whole answer: an error returned from it stops
// the build, reaches the error stream, and leaves nothing on disk.
//
// This was a finding once. on_start was registered and never called, so an error
// returned from it was discarded and the build ran to completion — the two tests
// below used to assert exactly that, and said why. The scan phase now drains
// OnStartList before it reads a file, which is what the registration side of the
// interface always promised.
//
// The three assertions are the three ways "refused" can be half true. An exit
// code alone would also be satisfied by a build that failed for an unrelated
// reason; a message on the error stream alone would also be satisfied by a
// warning. Only the three together say the plugin's error is what stopped it.
TEST(CliPlugins, AnErrorFromAStartCallbackStopsTheBuild) {
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

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "refused by a plugin"));
    EXPECT_FALSE(ws.Exists("dist/entry.js"))
        << "the build ran to completion after the plugin refused it";
}

// The companion: the callback is called, and called once. A hook that fires per
// file would be a much worse bug than one that never fires, because a host
// refusing on some condition would see it apply to a different build than the
// one it reasoned about.
TEST(CliPlugins, AStartCallbackIsCalledOnceForABuild) {
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
    EXPECT_EQ(starts.load(), 1);
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
