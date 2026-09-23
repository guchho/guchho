#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <filesystem>
#include <string>
#include <vector>

using guchho::helpers::ProcessResult;
using guchho::helpers::RunProcess;

// ---------------------------------------------------------------------------
// Platform child commands
// ---------------------------------------------------------------------------

namespace {

    // Each builder returns an argv for RunProcess() that relies only on the
    // command interpreter present on every host of that platform, so nothing
    // here depends on a binary guchho bundles itself.

    // Prints "guchho-stdout" (plus the platform line ending) on standard
    // output, nothing on standard error, exiting 0.
    std::vector<std::string> StdoutCommand()
    {
#ifdef _WIN32
        return {"cmd", "/d", "/s", "/c", "echo guchho-stdout"};
#else
        return {"sh", "-c", "echo guchho-stdout"};
#endif
    }

    // Prints "guchho-stderr" on standard error, nothing on standard output,
    // exiting 0.
    std::vector<std::string> StderrCommand()
    {
#ifdef _WIN32
        return {"cmd", "/d", "/s", "/c", "echo guchho-stderr 1>&2"};
#else
        return {"sh", "-c", "echo guchho-stderr 1>&2"};
#endif
    }

    // Prints "guchho-mixed-out" on standard output and "guchho-mixed-err" on
    // standard error, interleaved, exiting 0.
    std::vector<std::string> MixedCommand()
    {
#ifdef _WIN32
        return {"cmd", "/d", "/s", "/c", "echo guchho-mixed-out & echo guchho-mixed-err 1>&2"};
#else
        return {"sh", "-c", "echo guchho-mixed-out; echo guchho-mixed-err 1>&2"};
#endif
    }

    // Writes nothing and exits with code 3.
    std::vector<std::string> Exit3Command()
    {
#ifdef _WIN32
        return {"cmd", "/d", "/s", "/c", "exit 3"};
#else
        return {"sh", "-c", "exit 3"};
#endif
    }

    // Prints the current working directory as a bare path, exiting 0.
    std::vector<std::string> PrintCwdCommand()
    {
#ifdef _WIN32
        return {"cmd", "/d", "/s", "/c", "cd"};
#else
        return {"sh", "-c", "pwd"};
#endif
    }

    // Emits "line" 20,000 times, which far exceeds a single pipe buffer, so a
    // run that completes proves the drain threads are draining both pipes
    // while the main thread waits on the child (no deadlock).
    std::vector<std::string> BigOutputCommand()
    {
#ifdef _WIN32
        return {"cmd", "/d", "/s", "/c", "for /L %i in (1,1,20000) do @echo line"};
#else
        return {"sh", "-c", "i=0; while [ $i -lt 20000 ]; do echo line; i=$((i+1)); done"};
#endif
    }

} // namespace

// ---------------------------------------------------------------------------
// Argument validation
// ---------------------------------------------------------------------------

TEST(ProcessTest, RejectsEmptyArgv)
{
    ProcessResult run = RunProcess({}, "");
    EXPECT_FALSE(run.started);
    EXPECT_EQ(run.exit_code, -1);
    EXPECT_EQ(run.stdout_data, "");
    EXPECT_EQ(run.stderr_data, "");
}

TEST(ProcessTest, RejectsEmptyFirstArg)
{
    ProcessResult run = RunProcess({"", "tail"}, "");
    EXPECT_FALSE(run.started);
    EXPECT_EQ(run.exit_code, -1);
    EXPECT_EQ(run.stdout_data, "");
    EXPECT_EQ(run.stderr_data, "");
}

// ---------------------------------------------------------------------------
// Standard output / standard error capture
// ---------------------------------------------------------------------------

TEST(ProcessTest, CapturesStdout)
{
    ProcessResult run = RunProcess(StdoutCommand(), "");
    ASSERT_TRUE(run.started);
    ASSERT_EQ(run.exit_code, 0);
    EXPECT_NE(run.stdout_data.find("guchho-stdout"), std::string::npos)
        << "stdout: " << run.stdout_data;
    EXPECT_EQ(run.stderr_data, "");
}

TEST(ProcessTest, CapturesStderr)
{
    ProcessResult run = RunProcess(StderrCommand(), "");
    ASSERT_TRUE(run.started);
    ASSERT_EQ(run.exit_code, 0);
    EXPECT_EQ(run.stdout_data, "");
    EXPECT_NE(run.stderr_data.find("guchho-stderr"), std::string::npos)
        << "stderr: " << run.stderr_data;
}

TEST(ProcessTest, CapturesStdoutAndStderrTogether)
{
    ProcessResult run = RunProcess(MixedCommand(), "");
    ASSERT_TRUE(run.started);
    ASSERT_EQ(run.exit_code, 0);
    EXPECT_NE(run.stdout_data.find("guchho-mixed-out"), std::string::npos)
        << "stdout: " << run.stdout_data;
    EXPECT_NE(run.stderr_data.find("guchho-mixed-err"), std::string::npos)
        << "stderr: " << run.stderr_data;
}

// ---------------------------------------------------------------------------
// Exit codes
// ---------------------------------------------------------------------------

TEST(ProcessTest, PreservesExitCode)
{
    ProcessResult run = RunProcess(Exit3Command(), "");
    ASSERT_TRUE(run.started);
    EXPECT_EQ(run.exit_code, 3);
}

// ---------------------------------------------------------------------------
// Working directory
// ---------------------------------------------------------------------------

TEST(ProcessTest, RunsInRequestedWorkingDirectory)
{
    // A unique directory name (seeded) keeps parallel ctest runs from
    // colliding. It is created relative to the test's working directory,
    // used as the child's cwd, and removed right after the child reports
    // where it started.
    const std::string dir_name =
        "guchho_process_cwd_" + std::to_string(guchho::test::RandomSeed());
    std::filesystem::create_directory(dir_name);

    ProcessResult run = RunProcess(PrintCwdCommand(), dir_name);

    std::filesystem::remove(dir_name);

    ASSERT_TRUE(run.started);
    ASSERT_EQ(run.exit_code, 0);
    EXPECT_NE(run.stdout_data.find(dir_name), std::string::npos)
        << "stdout: " << run.stdout_data;
}

// ---------------------------------------------------------------------------
// Large output (deadlock avoidance)
// ---------------------------------------------------------------------------

TEST(ProcessTest, DrainsLargeOutputWithoutDeadlock)
{
    ProcessResult run = RunProcess(BigOutputCommand(), "");
    ASSERT_TRUE(run.started);
    ASSERT_EQ(run.exit_code, 0);

    // Each echoed line ends in exactly one newline on every platform, so
    // counting newlines reports the number of lines the child produced.
    size_t lines = 0;
    for (char c : run.stdout_data) {
        if (c == '\n') {
            ++lines;
        }
    }
    EXPECT_EQ(lines, 20000u);
}
