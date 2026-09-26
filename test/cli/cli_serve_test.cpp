// Tests for serve and dev, up to the line where they stop returning.
//
// Both commands park. runServe hands its directory to api::Serve and comes back
// only when the server is stopped, and runDev does the same with a watcher
// running underneath it, so neither of these tests can ask the question it
// would really like to ask — does the server answer a request — without a
// second process, a timeout, and a way to stop it again. That machinery is
// worth having, and test/api/api_serve_test.cpp is where it belongs, because the
// thing worth testing there is the server and not the two lines that call it.
//
// So what is left is the part that decides whether the parking is ever reached:
// the help, the address, and the flags. A port that is not a number is rejected
// before anything is bound, and that check is the only piece of these two
// commands that can be reached from a test without a socket — which makes it
// worth pinning, because the alternative is a command that binds a port nobody
// asked for.
//
// What the flag rejections here have in common with the build command's is the
// note: the message says what was wrong and the note says what would have been
// right, and a test that only checked the first would let the second rot.

#include "test/helpers/cli_test.hpp"
#include "test/guchho_test.hpp"

#include <string>
#include <vector>

namespace cli::test {

using guchho::test::CliWorkspace;
using guchho::test::kSuccess;
using guchho::test::kUsageError;
using guchho::test::OutputContains;
using guchho::test::RunCli;

// ---------------------------------------------------------------------------
// serve
// ---------------------------------------------------------------------------

TEST(CliServe, HelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("serve-help");

    const guchho::test::CliResult result = RunCli({"serve", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho serve"));
}

// A port that is not a number is the mistake worth catching, because the other
// reading of "3000x" is a port nobody can bind and the failure would arrive
// later, as a server that did not start.
TEST(CliServe, APortThatIsNotANumberIsRejected) {
    CliWorkspace ws("serve-port");

    const guchho::test::CliResult result = RunCli({"serve", "--port=abc"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "--port=abc"));
}

// The help names the flag, so a rejected value can be compared against what the
// command says it wanted.
TEST(CliServe, ARejectedPortIsNamedInTheMessage) {
    CliWorkspace ws("serve-port-msg");

    const guchho::test::CliResult result = RunCli({"serve", "--port=99999"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "99999"));
}

// Not tested, and the reason is in the note at the top of this file: a request
// answered by the served directory, a range, a media type, a redirect. All of
// that is api::Serve, and all of it is already driven over a real loopback
// socket in test/api/api_serve_test.cpp.

// ---------------------------------------------------------------------------
// dev
// ---------------------------------------------------------------------------

TEST(CliServe, DevHelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("dev-help");

    const guchho::test::CliResult result = RunCli({"dev", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho dev"));
}

// dev takes the build grammar, so a flag that is not a build flag is refused
// the same way the build command refuses it. This is the one place the two
// commands can be compared directly, because neither of them gets as far as
// binding anything first.
TEST(CliServe, DevTakesTheBuildGrammar) {
    CliWorkspace ws("dev-grammar");

    const guchho::test::CliResult result = RunCli({"dev", "--nope"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "Invalid build flag"));
}

} // namespace cli::test
