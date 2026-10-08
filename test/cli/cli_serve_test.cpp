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

// A config file that exists but cannot be parsed has no output directory to
// serve. Before the shared resolution learned to report "config_invalid" to
// its callers, this command carried on with an empty answer and served
// nothing at all — the directory the server started over was the empty
// string. The refusal is the same one "guchho build" gives, and it happens
// before the banner and long before a socket is bound, which is what makes it
// reachable from a test where a started server would not be.
TEST(CliServe, ServeRefusesAnInvalidConfig) {
    CliWorkspace ws("serve-invalid-config");
    ws.Write("guchho.config.json", "{\"build\": {\"outdir\": \"broken");

    const guchho::test::CliResult result = RunCli({"serve"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "The project configuration could not be used"));
}

// ---------------------------------------------------------------------------
// dev
// ---------------------------------------------------------------------------

TEST(CliServe, DevHelpIsItsOwnAndSucceeds) {
    CliWorkspace ws("dev-help");

    const guchho::test::CliResult result = RunCli({"dev", "--help"});

    EXPECT_EQ(result.exit_code, kSuccess);
    EXPECT_TRUE(OutputContains(result.out, "Usage: guchho dev"));
}

// The same refusal as the command above, for the command that builds before it
// serves. An unusable config here used to mean a silent fallback to the raw
// request, no output directory — not even the built-in "dist" — and a server
// that "--open" would then point a browser at with nothing behind it.
TEST(CliServe, DevRefusesAnInvalidConfig) {
    CliWorkspace ws("dev-invalid-config");
    ws.Write("guchho.config.json", "{\"build\": {\"outdir\": \"broken");

    const guchho::test::CliResult result = RunCli({"dev"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.err, "The project configuration could not be used"));
}

// The config this run settled on is named before the server is. This is the
// testable half of that promise: a config that names an outdir but no entry
// point, in a workspace with no index.html for the guess to find, gets past
// the resolution — where "Using" is printed — and stops at the entry-point
// gate, before anything is bound. The untestable half is a dev server that
// stays up, and the file header says why.
TEST(CliServe, DevReportsTheConfigItIsUsing) {
    CliWorkspace ws("dev-using-config");
    ws.Write("guchho.config.json", "{\"build\": {\"outdir\": \"custom-out\"}}");

    const guchho::test::CliResult result = RunCli({"dev"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_TRUE(OutputContains(result.out, "Using "));
    EXPECT_TRUE(OutputContains(result.out, "guchho.config.json"));
}

// Quiet takes the line away, the same way it takes the build summary away.
TEST(CliServe, DevDoesNotReportTheConfigWhenQuiet) {
    CliWorkspace ws("dev-using-config-quiet");
    ws.Write("guchho.config.json", "{\"build\": {\"outdir\": \"custom-out\"}}");

    const guchho::test::CliResult result = RunCli({"dev", "--quiet"});

    EXPECT_EQ(result.exit_code, kUsageError);
    EXPECT_FALSE(OutputContains(result.out, "Using "));
}

} // namespace cli::test
