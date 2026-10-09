// =============================================================================
// src/cli/cli_project.cpp — the two commands that manage a project in place
// =============================================================================
//
// Everything here is about the working directory rather than about a project's
// output. One command removes what a build left behind and the other answers
// questions about the machine. Neither builds anything, and neither hands a set
// of options to the engine.
//
// That is why this file does not use the flag grammar. The grammar in
// cli_options.cpp exists to fill in api::BuildOptions and
// api::TransformOptions, and neither of these two commands has anything to fill
// in: "guchho clean" has exactly one switch and "guchho info" has none. So each
// reads its own arguments, in a few lines, looking for the two or three
// spellings it answers to.
//
// The third command that used to live here, init, moved to cli_init.cpp when it
// grew two values to choose between, a mode that asks questions, and a refusal
// of arguments it does not recognise. It was the one of the three that could be
// given something to misunderstand, and a file whose banner argued that an
// unrecognised argument could not matter was the wrong place for a command that
// has four options and a refusal of its own.
//
// One consequence of not using the grammar is worth knowing before using these.
// An argument "clean" and "info" do not recognise is not a mistake they will
// tell you about, with one exception: "clean" hands whatever is left to the
// build grammar so that a directory named on the command line is the one it
// removes, and that grammar does complain. "info" is the loosest of the
// commands here, because a report that answers a question nobody asked is
// harmless in a way a deletion is not.
//
// Both reach the file system through the filesystem interface rather than around
// it, so the same code can be run against an in-memory tree. Each builds the
// interface the same way
// — default options, an error string, and a check — and treats a failure to
// create it as a failure of the run rather than as a rejected command line.
// That distinction shows up in the exit code: a command line that was not
// understood is worth 2, and a working directory that cannot be used is worth
// 1, because nothing about what was asked for was wrong.
// =============================================================================
#include "guchho/cli.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/resolver.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace guchho::cli {
// =============================================================================
// Command: clean
// =============================================================================
//
// Removes what a build put in the project, and nothing else.
//
// There is one target, and it is not a name typed in here: it is the directory
// this project's build writes to, worked out by the same resolution a build
// uses. A command with a fixed list cannot be right about a project that has
// configured a different output directory — it would either miss the output
// entirely or, as it once did, remove two more directories on the strength of a
// convention nothing else in the program followed. Deciding where the output is
// and deciding what to delete are the same question, so they get the same
// answer from the same place.
//
// A missing target produces no line of output, because a project that has never
// been built has nothing to clean and there is nothing to tell the person who
// asked.
//
// The switch makes the command safe to try. Deleting a directory is the one
// thing on the command line that cannot be undone by running it again, and
// "--dry-run" answers the only question a person has before running it: what
// would be removed.

// Removes the project's output directory.
//
// Reads "--help" and "-h" before doing anything, "--dry-run" to list what would
// be removed without removing it, and the build's own output flags — "--outdir"
// and "--outfile" — so that a directory named on the command line is the one
// removed.
//
// A target that is not there produces no output, because a project that has
// never been built has nothing to clean and there is nothing to tell the person
// who asked. A target that is there and cannot be removed prints the reason on
// the error stream and the run still succeeds: the command was understood and
// did what it could, and a directory held open by something else is not a
// reason to call the whole command wrong.
//
// Input:  { "clean" } with a dist directory holding four entries
// Output: a blank line, "  Removed dist/ (4 items)", a blank line, and 0.
//
// Input:  { "clean", "--dry-run" } in the same directory
// Output: a blank line, "  Would remove dist/", a blank line, and 0, with
//         nothing removed.
//
// Input:  { "clean", "--outdir=build" } in a project whose config says the same
// Output: a line about "build/", and nothing about "dist/" even if that exists.
//
// Input:  { "clean" } in an empty directory
// Output: two blank lines and 0.
//
// Input:  { "clean", "--help" }
// Output: the clean help text, nothing removed, and 0.
int runClean(const std::vector<std::string>& args) {
    bool dry_run = false;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            printCleanHelp(std::cout);
            return 0;
        }
        if (args[i] == "--dry-run") {
            dry_run = true;
            continue;
        }
    }

    filesystem::RealFsOptions fs_opts;
    std::string fs_err;
    auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
    if (!fs) {
        std::cerr << "Error: " << fs_err << "\n";
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    auto cwd = fs->Cwd();

    // The list of arguments is read rather than the output paths directly, so
    // that "--outdir" and "--outfile" are honoured here exactly as they are by a
    // build, and the project configuration gets its turn in between.
    auto clean_args = args;
    if (!clean_args.empty() && clean_args[0] == "clean") {
        clean_args.erase(clean_args.begin());
    }
    clean_args.erase(std::remove(clean_args.begin(), clean_args.end(), "--dry-run"),
                     clean_args.end());
    auto [clean_build, clean_transform, build_opts, transform_opts, clean_extras, clean_err] =
        parseOptionsForRun(clean_args, {}, true);
    (void)clean_build;
    (void)clean_transform;
    (void)transform_opts;
    (void)clean_extras;
    if (clean_err) {
        logger::PrintErrorWithNoteToStderr(args, clean_err->text, clean_err->note);
        return static_cast<int>(ExitCode::kCLIUsageError);
    }
    build_opts = resolveRunOptions(build_opts);

    // One target, and it is the directory this project's build writes to. The
    // previous version of this command removed a fixed list — the output
    // directory, a ".guchho" and a "cache" — which meant it could delete
    // directories nothing had ever put there and, worse, missed the output
    // directory whenever the project had configured a different one. Deciding
    // from the same resolution a build uses is what makes "clean" and "build"
    // agree about where the output is.
    //
    // A build configured with a single output file has still got a directory
    // underneath it, and that is the directory to remove. The file is spelled
    // relative to the working directory, so it is joined first: Dir() is
    // documented in terms of absolute paths and asked for a relative one it has
    // no base to work from, which is how this branch came to remove nothing at
    // all.
    std::vector<std::string> targets;
    if (!build_opts.outdir.empty()) {
        targets.push_back(build_opts.outdir);
    } else if (!build_opts.outfile.empty()) {
        std::optional<std::string> file_abs =
            fs->Abs(fs->Join({cwd, build_opts.outfile}));
        if (file_abs) {
            std::optional<std::string> dir_rel = fs->Rel(cwd, fs->Dir(*file_abs));
            targets.push_back(dir_rel ? *dir_rel : fs->Dir(*file_abs));
        }
    }
    targets.erase(std::remove(targets.begin(), targets.end(), std::string()),
                  targets.end());

    // Opens the list, and is also what a run with nothing to remove prints.
    std::cout << "\n";

    for (const auto& target : targets) {
        std::string full_path = fs->Join({cwd, target});
        // Existence is decided by listing the directory itself. A target that
        // is a file rather than a directory also fails this, and is left alone:
        // this command removes directories it recognises by name, and has no
        // opinion about files.
        auto entries = fs->ReadDirectory(full_path);
        if (entries.Ok()) {
            if (dry_run) {
                std::cout << "  Would remove " << target << "/\n";
            } else {
                // The standard removal, with the error code supplied rather
                // than thrown: one target failing should not end the command
                // before the remaining two have been considered.
                std::error_code ec;
                auto count = std::filesystem::remove_all(
                    filesystem::PathFromUTF8(full_path), ec);
                if (!ec) {
                    std::cout << "  Removed " << target << "/ (" << count << " items)\n";
                } else {
                    // On the error stream and without changing the exit code,
                    // which is the decision described above: this is a report
                    // about one directory, not a failure of the command.
                    std::cerr << "  Failed to remove " << target << "/: " << ec.message() << "\n";
                }
            }
        }
    }

    // Closes the list. The trailing separator in every message is added here
    // for the reader and is not part of the path, so that the three lines
    // below each other line up as a column.
    std::cout << "\n";
    return static_cast<int>(ExitCode::kSuccess);
}

// =============================================================================
// Command: info
// =============================================================================
//
// Three facts about the machine and the project, and the first thing anybody
// pastes into a bug report.
//
// It is the shortest of the eight commands and the only one with no settings at
// all, and that is a deliberate answer to the question of what to print. A
// person who has typed "guchho info" has usually just seen something they did
// not expect, and the useful thing to tell them is which machine they are on
// and which build they are running — not a list of things that could be
// configured. Anything configurable is the flag that configures it, and a
// report that repeated the defaults would be three more lines of nothing.
//
// Nothing here is detected. The platform and the build type are decided when
// this file is compiled, because both are properties of the binary rather than
// of the project, and a report that asked the operating system at run time
// could disagree with the binary it was printed by.

// Prints the platform, the build type and the configuration file in use.
//
// Reads "--help" and "-h" and nothing else. There is no flag to change what is
// printed, because there is no version of this report that is more or less
// detailed: the three lines are the three facts that are useful.
//
// Prints its own banner. The commands that build print theirs before they run,
// and this one is not one of them, so a run of it produces exactly one banner
// rather than none or two.
//
// The filesystem is created only to look for the configuration file, and unlike
// the two commands above, a failure to create it is not reported and does not
// affect the result: the report is still true, and "none" is a fair thing to
// say about a configuration file that could not be looked for.
//
// Input:  { "info" } in a project holding guchho.config.js
// Output: the banner, "  Platform:", "  Build:" and "  Config:" lines each
//         naming one thing, a blank line, and 0.
//
// Input:  { "info" } in a directory with no configuration file
// Output: the same, with "none" where the configuration would be, and 0.
//
// Input:  { "info", "--help" }
// Output: the info help text, two lines, and 0 — no banner, because help is
//         answered before anything is printed.
int runInfo(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            printInfoHelp(std::cout);
            return 0;
        }
    }

    printBanner(std::cout);

    // One of three names, chosen when this file was compiled. The last arm is
    // not a list of the systems that are not Windows or macOS; it is what is
    // left, and it is what a binary built anywhere else will say.
#ifdef _WIN32
    std::string platform = "Windows";
#elif __APPLE__
    std::string platform = "macOS";
#else
    std::string platform = "Linux";
#endif

    // The same kind of decision: a build with its assertions compiled out says
    // Release whether or not it was optimised, and a build with them in says
    // Debug however it was configured. Of the two, this is the one worth
    // knowing when a run behaves differently from a release build.
    std::string build_type =
#ifdef NDEBUG
        "Release";
#else
        "Debug";
#endif

    // Which of the names is a file that is actually there, in the order the
    // loader looks. The order is the resolver's, not this command's: a report
    // that named a different file from the one a build would have read would be
    // worse than no report at all, and the two have already been out of step —
    // this list once put "guchho.json" ahead of "guchho.config.json", which is
    // the opposite of the order the loader takes. The first match is the one this
    // command's own sibling writes, so a project created by "guchho init" reports
    // the file it was given.
    filesystem::RealFsOptions fs_opts;
    std::string fs_err;
    auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
    std::string config = "none";
    if (fs) {
        auto cwd = fs->Cwd();
        for (const auto& name : resolver::GuchhoConfigFileNames()) {
            std::string full_path = fs->Join({cwd, name});
            // The same probe the init command uses to decide whether to skip a
            // file: list the directory, find the name in the listing, and ask
            // what kind of entry it is. A directory with a configuration file's
            // name on it is not a configuration file.
            auto entries = fs->ReadDirectory(fs->Dir(full_path));
            if (entries.Ok()) {
                auto [entry, diff] = entries.value.Get(fs->Base(full_path));
                if (entry && entry->Kind(*fs) == filesystem::EntryKind::kFile) {
                    config = name;
                    break;
                }
            }
        }
    }

    // The names are padded to a common width so that the values line up as a
    // column, which is what makes three lines readable as one fact each rather
    // than as three sentences.
    std::cout << "  Platform:      " << platform << "\n"
              << "  Build:         " << build_type << "\n"
              << "  Config:        " << config << "\n"
              << "\n";

    return static_cast<int>(ExitCode::kSuccess);
}

} // namespace guchho::cli
