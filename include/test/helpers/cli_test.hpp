#pragma once

// =============================================================================
// include/test/helpers/cli_test.hpp — driving the command line from a test
// =============================================================================
//
// The command line is the one part of Guchho that a person types at, and until
// now nothing tested it: no test target linked guchho_cli, so the dispatch in
// cli_run.cpp, the entry points in cli_project.cpp, the exit codes in cli.hpp
// and every "what happens when nobody named an entry point" decision were only
// ever checked by running the binary by hand. This header is what those tests
// need, and there are only three things to build.
//
//   CliWorkspace
//       A directory that exists for the length of one test and is also the
//       working directory while it does. The chdir is not a convenience: init,
//       clean and info all act on the process working directory and take no
//       flag that points them anywhere else (they call fs->Cwd()), and clean
//       removes three directories by name, so a test that ran it without one
//       would delete the repository's own dist. The order on the way out is
//       just as load-bearing — Windows refuses to remove a directory that is
//       still the working directory, so the old directory is restored first and
//       the tree is deleted second.
//
//   CaptureOutput
//       Swaps the buffers of std::cout and std::cerr for the length of a run.
//       A run's output is the answer for the commands that are a filter rather
//       than a build — transform writes the transformed program to the standard
//       output and nothing else, and a test that could not see it could only
//       check the exit code. Swapping the streambuf rather than the file
//       descriptor is enough, because the command line writes through the C++
//       streams and nothing in it writes with printf.
//
//   RunCli
//       Calls guchho::cli::Run in this process and reports what happened. In
//       process rather than as a subprocess, because a run touches global state
//       that a second process could not share: the working directory, the
//       locale, the console code page, the signal handlers. CTest runs one
//       test per process, so those globals are this test's alone. The tests
//       that specifically want the executable's own argument handling — the wide
//       argv conversion in src/main.cpp above all — run guchho.exe through
//       helpers::RunProcess instead, and are the reason both ways of testing
//       exist.
//
// What these helpers deliberately do not do is compare whole output against a
// stored file, the way test/bundler does. A run prints a banner, a duration, a
// byte count and a platform name, none of which are the same twice, so a
// snapshot of one would be a record of the machine that produced it. The
// assertions are on the exit code, on the substrings that carry meaning, and on
// what is on disk afterwards.
// =============================================================================

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "guchho/cli.hpp"

namespace guchho::test {

// A temporary directory that is also the working directory for as long as it
// lives. See the note at the top of this file for why both halves are needed.
class CliWorkspace {
public:
    // The name is not derived from anything the test controls, so two tests
    // running at the same moment cannot pick the same directory. The counter
    // covers two workspaces in one test, and the clock covers two processes.
    explicit CliWorkspace(const std::string& label) {
        static int counter = 0;
        path_ = (std::filesystem::temp_directory_path() /
                 ("guchho-cli-test-" + label + "-" + std::to_string(counter++) + "-" +
                  std::to_string(static_cast<long long>(
                      std::chrono::high_resolution_clock::now()
                          .time_since_epoch()
                          .count()))))
            .string();
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);

        // Everything below assumes the run under test is looking at this
        // directory, so the move has to happen before the test body starts and
        // the move back has to happen before the deletion.
        previous_ = std::filesystem::current_path();
        std::filesystem::current_path(path_);
    }

    ~CliWorkspace() {
        std::error_code ec;
        std::filesystem::current_path(previous_, ec);
        std::filesystem::remove_all(path_, ec);
    }

    CliWorkspace(const CliWorkspace&) = delete;
    CliWorkspace& operator=(const CliWorkspace&) = delete;

    const std::string& path() const { return path_; }

    // A path inside the workspace, built from a relative path with forward
    // slashes so a test reads the same on every platform.
    std::string At(const std::string& relative) const {
        return (std::filesystem::path(path_) / std::filesystem::path(relative)).string();
    }

    // Writes "contents" to "relative", creating the directories it needs.
    void Write(const std::string& relative, const std::string& contents) const {
        const std::string full = At(relative);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(full).parent_path(), ec);
        std::ofstream out(full, std::ios::binary | std::ios::trunc);
        out << contents;
    }

    bool Exists(const std::string& relative) const {
        std::error_code ec;
        return std::filesystem::exists(At(relative), ec);
    }

    std::string Read(const std::string& relative) const {
        std::ifstream in(At(relative), std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }

private:
    std::string path_;
    std::filesystem::path previous_;
};

// What one run did: the code it returned, and the two streams it wrote to.
struct CliResult {
    int exit_code = 0;
    std::string out;
    std::string err;
};

// Runs the command line in this process, with nothing on the standard input.
//
// "args" is the list the command line documents: the command word first where
// there is one, never the executable name. Returns rather than asserts, so that
// a test can say what it expected instead of the harness guessing.
//
// input:  { "build", "index.html", "--outdir=dist" } in a workspace
// output: a run, its exit code, and what it wrote to the two output streams
inline CliResult RunCliWithStdin(const std::vector<std::string>& args,
                                 const std::string& stdin_text) {
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf* const saved_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* const saved_err = std::cerr.rdbuf(err.rdbuf());

    std::istringstream input(stdin_text);
    std::streambuf* const saved_in = std::cin.rdbuf(input.rdbuf());

    CliResult result;
    try {
        result.exit_code = guchho::cli::Run(args);
    } catch (...) {
        // The streams have to be put back before anything is rethrown, or the
        // rest of this test binary writes into two strings on the stack of a
        // frame that no longer exists.
        std::cin.rdbuf(saved_in);
        std::cout.rdbuf(saved_out);
        std::cerr.rdbuf(saved_err);
        throw;
    }

    std::cin.rdbuf(saved_in);
    std::cout.rdbuf(saved_out);
    std::cerr.rdbuf(saved_err);

    result.out = out.str();
    result.err = err.str();
    return result;
}

// The same run with nothing on the standard input, which is what every command
// except the two that are filters sees.
inline CliResult RunCli(const std::vector<std::string>& args) {
    return RunCliWithStdin(args, std::string());
}

// True when "text" contains "needle". Every output assertion in the command
// line tests goes through this, because what a run prints is a mixture of the
// part that means something and the parts that are the machine talking — the
// banner, the duration, the byte counts, the platform.
inline bool OutputContains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace guchho::test
