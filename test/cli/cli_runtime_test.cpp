// =============================================================================
// test/cli/runtime_test.cpp — the runtime detection and its seams
// =============================================================================
//
// "guchho init" stops being deterministic the day its output depends on which
// runtimes happen to be installed on the machine running the test suite, so
// this file is about keeping the boundary honest in both directions:
//
//   - the pure core (RuntimeFromLookups and the PATH walk) is fed a synthetic
//     PATH and a "file system" made of a set of names, so all four cells of the
//     selection table are proven here and nowhere else; and
//
//   - the one test that touches a real disk proves that a real executable is
//     found through a real PATH, which is the platform-compatibility half of
//     the requirement, and it makes its own file in its own workspace so it
//     needs no belief about what the developer machine has installed.
//
// Every test in this file runs against a workspace that never leaves anything
// behind, and none of them can pass or fail because somebody has Node installed.

#include "guchho/runtime.hpp"
#include "guchho/scaffold.hpp"
#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace cli::test {

using guchho::test::CliWorkspace;

namespace {

// The join a PATH walk performs, reproduced here so an expected candidate can
// be spelled with the platform's own separator rather than a guess.
std::string JoinPath(std::string dir, const std::string& name)
{
    if (dir.empty()) return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
#if defined(_WIN32)
    dir += '\\';
#else
    dir += '/';
#endif
    return dir + name;
}

// A "file system" that answers the executable check from a table, so the
// walker can be driven without touching the test machine's disk or its PATH.
// Existence and executability are separate tables on purpose: a file can be a
// PATH entry and still not be a command.
struct FakeFS {
    std::set<std::string> executable;
    std::set<std::string> inert;  // Present on the disk, but not runnable.

    bool IsExecutable(const std::string& path) const
    {
        return executable.count(path) != 0;
    }
};

// Joins a list of directories into a PATH value, with whichever separator the
// platform uses — ":" on a Unix and ";" on Windows — so the crafted PATH below
// is split the same way the walker splits the real one.
std::string PathList(const std::vector<std::string>& dirs)
{
    std::string out;
    for (size_t i = 0; i < dirs.size(); i++) {
        if (i > 0) {
#if defined(_WIN32)
            out += ';';
#else
            out += ':';
#endif
        }
        out += dirs[i];
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

// The four cells of the format table. The selection is two lines long, and the
// test pinning it down is the reason a change to it shows up here rather than
// in a doc comment.
TEST(Runtime, SelectInitConfigFormatChoosesByThePresenceOfARuntime) {
    using guchho::runtime::JavaScriptRuntime;
    using guchho::scaffold::ProjectConfig;

    EXPECT_EQ(guchho::runtime::SelectInitConfigFormat(JavaScriptRuntime::kNode),
              ProjectConfig::kJavaScript);
    EXPECT_EQ(guchho::runtime::SelectInitConfigFormat(JavaScriptRuntime::kBun),
              ProjectConfig::kJavaScript);
    EXPECT_EQ(guchho::runtime::SelectInitConfigFormat(JavaScriptRuntime::kNone),
              ProjectConfig::kJson);
}

// ---------------------------------------------------------------------------
// The lookups
// ---------------------------------------------------------------------------

// A PATH with no node and no bun on it is the JSON fallback, and the fallback
// does not need anything to be named on the path to be the answer.
TEST(Runtime, NothingOnThePathMeansNoRuntime) {
    FakeFS fs;

    const auto runtime = guchho::runtime::RuntimeFromLookups(
        "/usr/bin", "", [&](const std::string& p) { return fs.IsExecutable(p); });

    EXPECT_EQ(runtime, guchho::runtime::JavaScriptRuntime::kNone);
}

// A node on the path is a Node, and it is found by name the way a shell would
// find it rather than by a directory anybody hardcoded.
TEST(Runtime, ANodeOnThePathIsFound) {
    FakeFS fs;
    fs.executable.insert(JoinPath("/opt", "node"));

    const auto runtime = guchho::runtime::RuntimeFromLookups(
        "/opt", "", [&](const std::string& p) { return fs.IsExecutable(p); });

    EXPECT_EQ(runtime, guchho::runtime::JavaScriptRuntime::kNode);
}

// A bun with no node is a Bun, the second rung of the priority ladder.
TEST(Runtime, ABunOnThePathIsFound) {
    FakeFS fs;
    fs.executable.insert(JoinPath("/opt", "bun"));

    const auto runtime = guchho::runtime::RuntimeFromLookups(
        "/opt", "", [&](const std::string& p) { return fs.IsExecutable(p); });

    EXPECT_EQ(runtime, guchho::runtime::JavaScriptRuntime::kBun);
}

// When both are present the answer is Node: the config loader resolves to node
// first, and a generated config should match the runtime the build will use.
TEST(Runtime, NodeWinsWhenBothNodeAndBunAreOnThePath) {
    FakeFS fs;
    fs.executable.insert(JoinPath("/bin", "node"));
    fs.executable.insert(JoinPath("/bin", "bun"));

    const auto runtime = guchho::runtime::RuntimeFromLookups(
        "/bin", "", [&](const std::string& p) { return fs.IsExecutable(p); });

    EXPECT_EQ(runtime, guchho::runtime::JavaScriptRuntime::kNode);
}

// The priority is a rule about the two runtimes, not about the order they
// happen to be listed in: a PATH that names bun's directory first still has a
// Node on it, and still answers Node.
TEST(Runtime, NodeWinsRegardlessOfTheOrderOfThePathEntries) {
    FakeFS fs;
    fs.executable.insert(JoinPath("/bintools", "node"));
    fs.executable.insert(JoinPath("/usrtools", "bun"));

    const auto runtime = guchho::runtime::RuntimeFromLookups(
        PathList({"/usrtools", "/bintools"}), "",
        [&](const std::string& p) { return fs.IsExecutable(p); });

    EXPECT_EQ(runtime, guchho::runtime::JavaScriptRuntime::kNode);
}

// The GUIHO_NODE_BIN override is a Node in its own right, even when nothing is
// on the PATH, because the config loader would run the config with it.
TEST(Runtime, TheNodeOverrideCountsAsANodeOnItsOwn) {
    const auto runtime = guchho::runtime::RuntimeFromLookups(
        "", "/opt/node/bin/node",
        [](const std::string&) { return false; });

    EXPECT_EQ(runtime, guchho::runtime::JavaScriptRuntime::kNode);
}

// And the override outranks anything the PATH says, because it is the more
// specific statement: a person who set it has said which node they mean.
TEST(Runtime, TheNodeOverrideWinsOverABunDiscoveredOnThePath) {
    FakeFS fs;
    fs.executable.insert(JoinPath("/usr/bin", "bun"));

    const auto runtime = guchho::runtime::RuntimeFromLookups(
        "/usr/bin", "/opt/node/bin/node",
        [&](const std::string& p) { return fs.IsExecutable(p); });

    EXPECT_EQ(runtime, guchho::runtime::JavaScriptRuntime::kNode);
}

// ---------------------------------------------------------------------------
// The PATH walker
// ---------------------------------------------------------------------------

// A command is searched for in every PATH entry, not just the first one: the
// entry a machine's node lives in is not usually the first one on the list.
TEST(Runtime, ACommandInALaterPathEntryIsFound) {
    FakeFS fs;
    fs.executable.insert(JoinPath("/three", "node"));

    const std::string path_env = PathList({"/one", "/two", "/three"});

    EXPECT_TRUE(guchho::runtime::FindExecutableOnPath(
        "node", path_env, [&](const std::string& p) { return fs.IsExecutable(p); }));
}

// A name that is a PATH entry but nothing more — a file that is there but not
// executable — is not a command. The walker applies the executable rule to
// every candidate, never to the name alone, so a node that exists without its
// run bit is not a Node.
TEST(Runtime, ANameThatIsNotExecutableIsNotFound) {
    FakeFS fs;
    fs.inert.insert(JoinPath("/one", "node"));

    const auto runtime = guchho::runtime::RuntimeFromLookups(
        PathList({"/one", "/two"}), "",
        [&](const std::string& p) { return fs.IsExecutable(p); });

    EXPECT_EQ(runtime, guchho::runtime::JavaScriptRuntime::kNone);
}

// The name a walker looks for is the platform's own: an extension-less file on
// a Unix, and the bare name plus each PATHEXT suffix on Windows.
TEST(Runtime, CandidatesAreTheNamesThePlatformWouldLookFor) {
    const std::vector<std::string> candidates =
        guchho::runtime::RuntimeCandidatesFor("bin", "node", ";.COM;.EXE;.BAT;.CMD");

#if defined(_WIN32)
    // The extensions are the PATHEXT value as handed in, which keeps its case:
    // ".COM" produces a candidate named "node.COM", the name the platform would
    // look for.
    const std::vector<std::string> want = {
        JoinPath("bin", "node"),
        JoinPath("bin", "node.COM"),
        JoinPath("bin", "node.EXE"),
        JoinPath("bin", "node.BAT"),
        JoinPath("bin", "node.CMD"),
    };
#else
    const std::vector<std::string> want = {JoinPath("bin", "node")};
#endif
    EXPECT_EQ(candidates, want);
}

// ---------------------------------------------------------------------------
// A real file system
// ---------------------------------------------------------------------------

// The platform-compatibility check: a real executable, made in this test's own
// workspace, is found through a real PATH value. Nothing here assumes the
// machine that runs the suite has Node installed — the test is its own node.
TEST(Runtime, ARealExecutableIsFoundThroughARealPath) {
    CliWorkspace ws("rt-bin");

#if defined(_WIN32)
    ws.Write("node.exe", "");
#else
    ws.Write("node", "");
    std::error_code ec;
    std::filesystem::permissions(
        CliWorkspace::Native(ws.At("node")),
        std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    ASSERT_FALSE(ec) << "could not make the node file executable";
#endif

    const std::string bin_dir = CliWorkspace::Native(ws.path()).generic_string();

    const auto real = [](const std::string& p) {
#if defined(_WIN32)
        std::error_code ec;
        return std::filesystem::exists(std::filesystem::path(p), ec) && !ec;
#else
        return ::access(p.c_str(), X_OK) == 0;
#endif
    };

    const auto runtime = guchho::runtime::RuntimeFromLookups(bin_dir, "", real);
    EXPECT_EQ(runtime, guchho::runtime::JavaScriptRuntime::kNode)
        << "the executable the test created was not found through the walker";
}

} // namespace cli::test