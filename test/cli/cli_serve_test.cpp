// Tests for serve and dev, up to the line where they stop returning.
//
// Both commands park. runServe hands its directory to api::Serve and comes back
// only when the server is stopped, and runDev does the same with a watcher
// running underneath it. Neither is reachable from a test, and dev is worse
// than unreachable: it binds a socket and then runs the platform's own "open
// this address" command (src/cli/cli_serve.cpp), so a test that got that far
// would open a browser window on the machine running the suite. So no test here
// goes past the flags, and the reason is not only that a test cannot stop these
// commands — it is that they do not come back at all.
//
// What is left is worth having. A port that is not a number is rejected before
// anything is bound, and that check is the only piece of these two commands
// that can be reached from a test without a socket. It is also the check worth
// pinning, because the reading of "3000x" that is not a number has to be caught
// here rather than arriving later as a server that never started.
//
// The parts that matter most are not tested here and the omission is a real
// gap: a request answered by the served directory, a range, a media type, a
// redirect, a rebuild on change. All of that is api::Serve, and all of it is
// already driven over a real loopback socket in test/api/api_serve_test.cpp —
// which is the right place for it, since the thing worth testing there is the
// server rather than the two lines that call it.

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

// Answered before the socket, the way every command answers it: a request for
// help is a success, and the text is the command's own rather than the shared
// usage text, so its examples name the flags a served directory honours.
TEST(CliServe, HelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("serve-help");

    const guchho::test::CliResult result = RunCli({"serve", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho serve"));
}

// A port that is not a number is the mistake worth catching. The other reading
// of "abc" is a port nobody can bind, and that failure arrives later, as a
// server that did not start, with nothing in the message to connect it to the
// argument that caused it.
TEST(CliServe, APortThatIsNotANumberIsRejected) {
    CliWorkspace ws("serve-port");

    const guchho::test::CliResult result = RunCli({"serve", "--port=abc"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "--port=abc"));
    EXPECT_TRUE(OutputContains(result.err, "abc"));
}

// A port that is not a number is rejected before anything is bound. That is the
// only part of this command a test can reach: a port that is a number gets past
// the parsing, past the announcement of the address, and into api::Serve, which
// does not come back — so there is no way to assert on the other half from
// here, and the file header says where that half is tested instead.

// ---------------------------------------------------------------------------
// dev
// ---------------------------------------------------------------------------

TEST(CliServe, DevHelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("dev-help");

    const guchho::test::CliResult result = RunCli({"dev", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho dev"));
}

} // namespace cli::test
