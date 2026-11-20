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
using guchho::test::RunCliWithPluginGroups;
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

TEST(CliPlugins, AStartCallbackCanContributeAVirtualModule) {
    CliWorkspace ws("virtual-module");
    ws.Write("entry.js", "import { value } from \"virtual:answer\";\nconsole.log(value);\n");

    Plugin generator;
    generator.name = "generator";
    generator.setup = [](guchho::api::PluginBuild& build) {
        build.on_start([]() {
            guchho::api::OnStartResult result;
            guchho::api::VirtualModule module;
            module.id = "virtual:answer";
            module.namespace_ = "virtual";
            module.loader = guchho::api::Loader::kJS;
            module.contents = "export const value = \"from a virtual module\";\n";
            result.virtual_modules.push_back(std::move(module));
            return result;
        });
    };

    const guchho::test::CliResult result =
        RunCliWithPlugins({"build", "entry.js", "--bundle", "--outdir=dist"}, {generator});

    EXPECT_EQ(result.exit_code, kSuccess) << result.err;
    EXPECT_TRUE(OutputContains(ws.Read("dist/entry.js"), "from a virtual module"))
        << ws.Read("dist/entry.js");
}

TEST(CliPlugins, DuplicateVirtualModulesFailTheBuild) {
    CliWorkspace ws("duplicate-virtual-module");
    ws.Write("entry.js", "console.log(1);\n");

    auto make_plugin = [](const std::string& name) {
        Plugin plugin;
        plugin.name = name;
        plugin.setup = [](guchho::api::PluginBuild& build) {
            build.on_start([]() {
                guchho::api::OnStartResult result;
                guchho::api::VirtualModule module;
                module.id = "virtual:duplicate";
                module.namespace_ = "virtual";
                module.contents = "export default 1;\n";
                result.virtual_modules.push_back(std::move(module));
                return result;
            });
        };
        return plugin;
    };

    const guchho::test::CliResult result = RunCliWithPlugins(
        {"build", "entry.js", "--outdir=dist"},
        {make_plugin("first"), make_plugin("second")});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "Duplicate virtual module")) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "\"first\"")) << result.err;
    EXPECT_TRUE(OutputContains(result.err, "\"second\"")) << result.err;
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

// ---------------------------------------------------------------------------
// Plugins grouped by configuration element
// ---------------------------------------------------------------------------

// A two-configuration project, where each element names a plugin. The two
// plugins share a name on purpose, because that is the case a single flat list
// cannot express: a host with a plugin per output names the same plugin on each
// element and needs each build to reach its own instance.
//
// Grouped, each group reaches exactly one build, so each setup runs once. A
// flat list would have run both plugins on both builds — the counters would be
// 2 and 2 — which is precisely the failure this grouping exists to prevent.
TEST(CliPlugins, EachConfigElementRunsOnlyItsOwnPluginGroup) {
    CliWorkspace ws("groups");
    ws.Write("a.js", "console.log(1);\n");
    ws.Write("b.js", "console.log(2);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a.js\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"b.js\",\"outdir\":\"two\"}}]");
    std::atomic<int> first{0};
    std::atomic<int> second{0};

    const guchho::test::CliResult result =
        RunCliWithPluginGroups({"build"},
                               {{CountingPlugin(first)}, {CountingPlugin(second)}});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("one/a.js")) << ws.Tree();
    EXPECT_TRUE(ws.Exists("two/b.js")) << ws.Tree();
    EXPECT_EQ(first.load(), 1);
    EXPECT_EQ(second.load(), 1);
}

// An element that names no plugin contributes an empty group, and the build for
// it runs with none. The plugin in the first group must not leak onto the
// second build, which is what makes the positional alignment meaningful.
TEST(CliPlugins, AnEmptyGroupLeavesItsBuildWithoutPlugins) {
    CliWorkspace ws("groups-empty");
    ws.Write("a.js", "console.log(1);\n");
    ws.Write("b.js", "console.log(2);\n");
    ws.Write("guchho.config.json",
             "[{\"build\":{\"entry\":\"a.js\",\"outdir\":\"one\"}},"
             " {\"build\":{\"entry\":\"b.js\",\"outdir\":\"two\"}}]");
    std::atomic<int> first{0};

    const guchho::test::CliResult result =
        RunCliWithPluginGroups({"build"}, {{CountingPlugin(first)}, {}});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("one/a.js")) << ws.Tree();
    EXPECT_TRUE(ws.Exists("two/b.js")) << ws.Tree();
    EXPECT_EQ(first.load(), 1);
}

// A build with a single configuration is one group, and the plugin in it runs
// exactly as the flat list would have run it — the grouping does not change the
// ordinary case.
TEST(CliPlugins, ASingleConfigurationUsesItsOneGroup) {
    CliWorkspace ws("groups-single");
    ws.Write("entry.js", "console.log(1);\n");
    ws.Write("guchho.config.json", "{\"build\":{\"entry\":\"entry.js\",\"outdir\":\"dist\"}}");
    std::atomic<int> setups{0};

    const guchho::test::CliResult result =
        RunCliWithPluginGroups({"build"}, {{CountingPlugin(setups)}});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(ws.Exists("dist/entry.js")) << ws.Tree();
    EXPECT_EQ(setups.load(), 1);
}

// A transform callback sees the source after loading and before parsing. The
// returned code is what the parser and printer see, so a change here has to show
// up in the emitted file even though no loader or linker rule was touched.
TEST(CliPlugins, ATransformCallbackCanReplaceTheSource) {
    CliWorkspace ws("transform-replace");
    ws.Write("entry.js", "console.log(42);\n");

    Plugin transforming;
    transforming.name = "transforming";
    transforming.setup = [](guchho::api::PluginBuild& build) {
        build.on_transform(
            guchho::api::OnTransformOptions{.filter = ".*", .namespace_ = ""},
            [](const guchho::api::OnTransformArgs& args) {
                guchho::api::OnTransformResult result;
                if (args.path.find("entry.js") != std::string::npos) {
                    result.code = "console.log(43);\n";
                }
                return result;
            });
    };

    const guchho::test::CliResult result =
        RunCliWithPlugins({"build", "entry.js", "--outdir=dist"}, {transforming});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(ws.Read("dist/entry.js"), "43"));
    EXPECT_FALSE(OutputContains(ws.Read("dist/entry.js"), "42"));
}

// Each callback sees the previous callback's output. Two simple substitutions
// prove the chain: the second one can only find its input if the first one ran
// and its result was handed forward.
TEST(CliPlugins, TransformCallbacksRunInPluginOrder) {
    CliWorkspace ws("transform-chain");
    ws.Write("entry.js", "console.log(42);\n");

    auto replacing = [](const char* name, const char* from, const char* to) {
        Plugin plugin;
        plugin.name = name;
        plugin.setup = [from, to](guchho::api::PluginBuild& build) {
            build.on_transform(
                guchho::api::OnTransformOptions{.filter = ".*", .namespace_ = ""},
                [from, to](const guchho::api::OnTransformArgs& args) {
                    guchho::api::OnTransformResult result;
                    std::string code = args.code;
                    const std::size_t at = code.find(from);
                    if (at != std::string::npos) {
                        code.replace(at, std::string(from).size(), to);
                        result.code = std::move(code);
                    }
                    return result;
                });
        };
        return plugin;
    };

    const guchho::test::CliResult result = RunCliWithPlugins(
        {"build", "entry.js", "--outdir=dist"},
        {replacing("first", "42", "43"), replacing("second", "43", "44")});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(ws.Read("dist/entry.js"), "44"));
    EXPECT_TRUE(ws.Exists("dist/entry.js"));
}

// An error from a transform is a build failure, and no output is written. This
// is the same contract as an error from a start or load callback.
TEST(CliPlugins, AnErrorFromATransformCallbackStopsTheBuild) {
    CliWorkspace ws("transform-error");
    ws.Write("entry.js", "console.log(42);\n");

    Plugin refusing;
    refusing.name = "refusing-transform";
    refusing.setup = [](guchho::api::PluginBuild& build) {
        build.on_transform(
            guchho::api::OnTransformOptions{.filter = ".*", .namespace_ = ""},
            [](const guchho::api::OnTransformArgs&) {
                guchho::api::OnTransformResult result;
                Message message;
                message.id          = "test-transform-refused";
                message.plugin_name = "refusing-transform";
                message.text        = "this source was refused by a transform";
                result.errors.push_back(std::move(message));
                return result;
            });
    };

    const guchho::test::CliResult result =
        RunCliWithPlugins({"build", "entry.js", "--outdir=dist"}, {refusing});

    EXPECT_EQ(result.exit_code, kBuildFailure);
    EXPECT_TRUE(OutputContains(result.err, "refused by a transform"));
    EXPECT_FALSE(ws.Exists("dist/entry.js"));
}

// A custom resolve question is allowed to leave the calling plugin out, which
// is what stops a plugin that rewrites imports from answering its own question.
// Two plugins claim the same specifier; the first one asks with its own name on
// the question, so only the second answer is possible.
TEST(CliPlugins, ACustomResolveSkipsTheCallingPlugin) {
    CliWorkspace ws("resolve-custom");
    ws.Write("entry.js", "console.log(1);\n");
    std::atomic<int> skipped{0};

    auto claim = [](const char* plugin_name, const char* answer) {
        Plugin plugin;
        plugin.name = plugin_name;
        plugin.setup = [plugin_name, answer](guchho::api::PluginBuild& build) {
            build.on_resolve(
                guchho::api::OnResolveOptions{.filter = ".*", .namespace_ = ""},
                [plugin_name, answer](const guchho::api::OnResolveArgs& args) {
                    guchho::api::OnResolveResult result;
                    if (args.path == "virtual:shared") {
                        result.plugin_name = plugin_name;
                        result.path = answer;
                        result.namespace_ = "virtual";
                    }
                    return result;
                });
        };
        return plugin;
    };

    Plugin asking = claim("asking", "virtual:asking");
    asking.setup = [&skipped, previous = std::move(asking.setup)](
                       guchho::api::PluginBuild& build) mutable {
        previous(build);
        auto resolver = build.resolve;
        build.on_start([resolver, &skipped]() {
            guchho::api::ResolveOptions question;
            question.plugin_name = "asking";
            question.kind = guchho::api::ResolveKind::kJSImportStatement;
            question.custom = true;

            const guchho::api::ResolveResult answer = resolver("virtual:shared", question);
            if (answer.errors.empty() && answer.path == "virtual:helper") {
                skipped.fetch_add(1);
            }
            return guchho::api::OnStartResult{};
        });
    };

    const guchho::test::CliResult result = RunCliWithPlugins(
        {"build", "entry.js", "--outdir=dist"},
        {asking, claim("helper", "virtual:helper")});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_EQ(skipped.load(), 1);
}

} // namespace cli::test
