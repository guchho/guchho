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
//   RunCli
//       Calls guchho::cli::Run in this process and reports what happened. In
//       process rather than as a subprocess, because a run touches global state
//       that a second process could not share: the working directory, the
//       locale, the console code page, the signal handlers. CTest runs one
//       test per process, so those globals belong to this test alone. The tests
//       that specifically want the executable's own argument handling — the wide
//       argv conversion in src/main.cpp above all — run guchho.exe through
//       helpers::RunProcess instead, and are the reason both ways of testing
//       exist.
//
//   OutputContains
//       The one way output is compared. A run prints a banner, a duration, a
//       byte count and a platform name, none of which are the same twice, so a
//       stored copy of a whole run would be a record of the machine that
//       produced it. The assertions are on the code, on the substrings that
//       carry meaning, and on what is on disk afterwards.
//
// What the tests here do not do, and say so where it matters: reach the part of
// watch, dev and serve that never returns. runWatch parks on a promise nothing
// satisfies (src/cli/cli_watch.cpp:381) and the other two park inside
// api::Serve, so a test that got that far would sit until CTest gave up on it.
// Those commands are covered up to the last line before the park.
// =============================================================================

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
    #include <io.h>
#else
    #include <unistd.h>
#endif

#include "guchho/api.hpp"
#include "guchho/cli.hpp"

namespace guchho::test {

// The four codes a run can return, as the numbers a process would exit with, so
// that a test can write EXPECT_EQ(result.exit_code, kUsageError) and be talking
// about the documented contract rather than about the number 2.
inline constexpr int kSuccess      = static_cast<int>(guchho::cli::ExitCode::kSuccess);
inline constexpr int kBuildFailure = static_cast<int>(guchho::cli::ExitCode::kBuildFailure);
inline constexpr int kUsageError   = static_cast<int>(guchho::cli::ExitCode::kCLIUsageError);
inline constexpr int kInterrupted  = static_cast<int>(guchho::cli::ExitCode::kInterrupted);

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

    // A directory inside the workspace, created empty. Used where a command is
    // expected to find something already there, such as an output directory
    // that clean is meant to remove.
    void MakeDir(const std::string& relative) const {
        std::error_code ec;
        std::filesystem::create_directories(At(relative), ec);
    }

    // Every file in the workspace, relative and one per line, for a failure
    // message that has to say what a command did rather than only that it did
    // not do what the test wanted. Without it a test that expected a file has
    // nothing to report but the absence, and "no dist" and "a dist with
    // something else in it" look the same from the outside.
    std::string Tree() const {
        std::string out;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 std::filesystem::path(path_), std::filesystem::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            out += std::filesystem::relative(entry.path(), path_).generic_string();
            out += "\n";
        }
        std::sort(out.begin(), out.end());
        return out;
    }

private:
    std::string path_;
    std::filesystem::path previous_;
};

namespace detail {

// Points one file descriptor at a file of its own and keeps the old one.
//
// The redirection is at the descriptor rather than at std::cout's buffer
// because the command line does not only write through the C++ streams: the
// timing line at the end of a run is std::fprintf to stdout
// (src/helpers/timer.cpp), and a test that swapped the stream buffer instead
// would not have seen it — which is precisely the output that decides whether
// transform can be used in a pipeline. The same is true of the diagnostics,
// which the logger sends down its own path.
//
// Each capture gets its own file, named after the stream rather than after the
// test, so two runs in one test cannot read each other's output. The files live
// in the system temporary directory and not in the workspace, because a test
// that builds an entry point by globbing would otherwise find them.
class FdCapture {
public:
    FdCapture(int fd, const std::string& name) : fd_(fd) {
        static int counter = 0;
        path_ = (std::filesystem::temp_directory_path() /
                 ("guchho-cli-capture-" + name + "-" +
                  std::to_string(counter++) + ".tmp"))
            .string();

#ifdef _WIN32
        if (fopen_s(&file_, path_.c_str(), "w+b") != 0) {
            file_ = nullptr;
        }
#else
        file_ = std::fopen(path_.c_str(), "w+b");
#endif
        if (file_ == nullptr) return;

        saved_ = dup_(fd_);
        dup2_(fileno_(file_), fd_);
    }

    ~FdCapture() {
        if (file_ != nullptr) {
            Finish();
        }
    }

    FdCapture(const FdCapture&) = delete;
    FdCapture& operator=(const FdCapture&) = delete;

    bool ok() const { return file_ != nullptr; }

    // Puts the descriptor back and hands over everything written to it. Called
    // once by the test's owner; the destructor calls it too, for the path where
    // a run threw, because a descriptor left pointing at a deleted file is a
    // problem for every test after this one.
    std::string Finish() {
        if (file_ == nullptr) return {};

        // Whatever the streams have buffered belongs in the file before the
        // file is read, and the file has to be rewound before it is read.
        std::fflush(stdout);
        std::fflush(stderr);
        std::cout.flush();
        std::cerr.flush();

        dup2_(saved_, fd_);
        close_(saved_);

        std::rewind(file_);
        std::string text;
        char        buffer[4096];
        size_t      got = 0;
        while ((got = std::fread(buffer, 1, sizeof(buffer), file_)) > 0) {
            text.append(buffer, got);
        }
        std::fclose(file_);
        file_ = nullptr;
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        return text;
    }

private:
#ifdef _WIN32
    static int dup_(int fd) { return _dup(fd); }
    static int dup2_(int to, int from) { return _dup2(to, from); }
    static void close_(int fd) { _close(fd); }
    static int fileno_(FILE* f) { return _fileno(f); }
#else
    static int dup_(int fd) { return dup(fd); }
    static int dup2_(int to, int from) { return dup2(to, from); }
    static void close_(int fd) { ::close(fd); }
    static int fileno_(FILE* f) { return fileno(f); }
#endif

    int fd_ = 1;
    int saved_ = -1;
    std::string path_;
    FILE* file_ = nullptr;
};

} // namespace detail

// What one run did: the code it returned, and the two streams it wrote to.
struct CliResult {
    int exit_code = 0;
    std::string out;
    std::string err;
};

namespace detail {

// Runs "run" with both output streams pointed at files of their own, and returns
// what it returned along with what it wrote.
//
// Both entry points below come through here, so neither of them is the one that
// has to get the capture and the restore right. The streams are put back before
// the result is returned even when the run throws, because a test binary that
// has thrown its way past the restore writes every later line into a temporary
// file that is on its way out.
template <typename Run>
CliResult Captured(Run&& run) {
    FdCapture out(1, "out");
    FdCapture err(2, "err");

    CliResult result;
    try {
        result.exit_code = run();
    } catch (...) {
        result.out = out.Finish();
        result.err = err.Finish();
        throw;
    }

    result.out = out.Finish();
    result.err = err.Finish();
    return result;
}

} // namespace detail

// Puts std::cin back the way it was found, whether the run finished or threw.
// The clear() matters as much as the swap: the stream keeps its error and
// end-of-file flags across a change of buffer, so a second run in the same
// process would read a stream that is already at its end and see no input at
// all.
namespace detail {
struct StdinRestorer {
    std::streambuf* saved;
    ~StdinRestorer() {
        std::cin.clear();
        std::cin.rdbuf(saved);
    }
};
} // namespace detail

// Runs the command line in this process.
//
// "args" is the list the command line documents: the command word first where
// there is one, never the executable name. "stdin_text" stands in for whatever
// a person piped in, which is the only way to reach the two commands that read
// the standard input — runBuild and runTransform both read std::cin to its end,
// so the stream is pointed at the text and pointed back afterwards. The end
// matters as much as the text: a run that only ever read the first line would
// report a clean build of a truncated program.
//
// Returns rather than asserts, so that a test can say what it expected instead
// of the harness guessing.
//
// input:  { "build", "index.html", "--outdir=dist" } in a workspace
// output: a run, its exit code, and what it wrote to the two output streams
inline CliResult RunCliWithStdin(const std::vector<std::string>& args,
                                 const std::string& stdin_text) {
    std::istringstream input(stdin_text);
    std::streambuf* const saved_in = std::cin.rdbuf(input.rdbuf());
    std::cin.clear();
    detail::StdinRestorer restore{saved_in};

    return detail::Captured([&] { return guchho::cli::Run(args); });
}

// The same run with nothing on the standard input, which is what every command
// except the two that are filters ever sees.
inline CliResult RunCli(const std::vector<std::string>& args) {
    return RunCliWithStdin(args, std::string());
}

// The same, for the other entry point: a run with plugins attached.
//
// It needs its own function rather than an extra argument on the one above,
// because a plugin is api::Plugin — a struct with a std::function in it, and
// therefore a type the common header should not have to know about for the
// seven files that never touch one. The capture and the stdin handling are the
// same code either way, which is the point of sharing them.
inline CliResult RunCliWithPlugins(const std::vector<std::string>& args,
                                   const std::vector<api::Plugin>& plugins) {
    return detail::Captured([&] { return guchho::cli::RunWithPlugins(args, plugins); });
}

// True when "text" contains "needle". Every output assertion in the command
// line tests goes through this, because what a run prints is a mixture of the
// part that means something and the parts that are the machine talking.
inline bool OutputContains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace guchho::test
