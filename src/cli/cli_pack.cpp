// =============================================================================
// src/cli/cli_pack.cpp — the command that writes an archive
// =============================================================================
//
// Everything about this command that is a spelling rather than a decision
// lives here: which words are inputs, which are flags, what the output is
// called when nobody named it, and what is said when it worked. The decision
// about what goes into the archive — which entries, under which names, with
// which metadata, what is left out — is in src/core/pack/pack.cpp and is not
// repeated here. This file's half is tested by running the command; that
// half's is tested by comparing archives.
//
// The flag grammar in cli_options.cpp is deliberately not used. That grammar
// fills in api::BuildOptions and api::TransformOptions, and a pack request
// fills in neither: it has a list of paths, a destination, an archive format,
// a compression level and a switch. Reading its own arguments in a loop is the
// same choice clean, info and init made, and for the same reason — the flags
// are few, and a flag nobody can put a default next to has nothing to resolve.
//
// Two exit codes are worth stating because they are the whole reason this
// file parses anything at all. A command line that could never have named an
// archive — no inputs, two inputs and no place to put them, a level that is
// not a number, a format this tool cannot write — is worth 2: nothing was read
// from disk and nothing was written, and the fix is in what was typed. A
// request that was well formed and then could not be carried out — a path that
// is not there, an archive that is already there — is worth 1, because nothing
// about the spelling was wrong. The core layer reports both the same way, as a
// message and optionally a note; which code accompanies it is decided by where
// the failure was noticed.
// =============================================================================

#include "guchho/cli.hpp"
#include "guchho/logger.hpp"
#include "guchho/pack.hpp"

#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace guchho::cli {

namespace {

// The value of --level, or nothing when the text is not one.
//
// Only whole numbers from 0 to 9 are accepted, and anything else is reported
// rather than clamped. A typo that quietly became a different level would
// produce a correct-looking archive built the wrong way, which is the same
// reasoning the core layer rejects an out-of-range level for; doing it here
// as well is what makes the complaint worth 2 instead of 1.
//
// Input:  "9"    -> 9
// Input:  "0"    -> 0
// Input:  "10"   -> nothing
// Input:  "6x"   -> nothing
// Input:  ""     -> nothing
std::optional<int> ParseLevel(const std::string& text)
{
    if (text.empty() || text.size() > 2) return std::nullopt;

    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + (c - '0');
    }
    if (value > 9) return std::nullopt;
    return value;
}

std::string Quoted(const std::string& text) { return "\"" + text + "\""; }

} // namespace

// =============================================================================
// Command: pack
// =============================================================================
//
// Reads the arguments, hands the request to the core, and reports what came
// back. The command word is dropped first so that the loop below sees only
// the arguments; every other command does the same, for the same reason: the
// dispatcher hands each command the arguments exactly as they arrived.
//
// Input:  args = { "pack", "dist/", "-o", "release.zip", "--level=9" }
// Output: "Created release.zip (<size>)" on the standard output, and 0.
//
// Input:  args = { "pack" }
// Output: "No input files were given" on the error stream with a note, and 2.
//
// Input:  args = { "pack", "dist/", "src/" }
// Output: "Multiple inputs need an output file" on the error stream with a
//         note naming -o, and 2 — there is no single name that both trees
//         would agree on, and guessing one would put an archive where
//         nobody asked for it.
//
// Input:  args = { "pack", "dist/", "--help" }
// Output: the pack usage on the standard output, and 0.
//
// Input:  args = { "pack", "dist/", "--format=tar" }
// Output: "Unsupported archive format: \"tar\"" on the error stream with a
//         note, and 2 — the spelling was wrong, so nothing was read and
//         nothing was written.
//
// Input:  args = { "pack", "absent/" }
// Output: "Input does not exist: ..." on the error stream, and 1 — the
//         spelling was fine and the path was not there.
int runPack(const std::vector<std::string>& args)
{
    std::vector<std::string> words = args;
    if (!words.empty() && words[0] == "pack") {
        words.erase(words.begin());
    }

    // Answered before anything else is read, so help is available even for a
    // command line that is otherwise wrong.
    for (const std::string& word : words) {
        if (word == "--help" || word == "-h") {
            printPackHelp(std::cout);
            return static_cast<int>(ExitCode::kSuccess);
        }
    }

    // A complaint about what was typed, with the code that goes with it. The
    // note is where the fix is, and it is on its own line rather than in the
    // sentence because the two are read differently: the first says what was
    // refused, the second says what to do about it.
    auto usageError = [&args](const std::string& text, const std::string& note) {
        if (note.empty()) {
            logger::PrintErrorToStderr(args, text);
        } else {
            logger::PrintErrorWithNoteToStderr(args, text, note);
        }
        return static_cast<int>(ExitCode::kCLIUsageError);
    };

    pack::PackOptions options;
    bool             has_outfile = false;

    for (size_t i = 0; i < words.size(); ++i) {
        const std::string& arg = words[i];

        if (arg == "--allow-overwrite") {
            options.overwrite = true;
            continue;
        }

        if (arg == "-o" || arg == "--outfile") {
            if (i + 1 >= words.size()) {
                return usageError("Missing value for " + arg,
                                  "Give the path to write the archive to.");
            }
            options.outFile = words[++i];
            has_outfile     = true;
            continue;
        }
        if (arg.starts_with("--outfile=")) {
            options.outFile = arg.substr(std::string("--outfile=").size());
            has_outfile     = true;
            continue;
        }

        if (arg == "--level") {
            if (i + 1 >= words.size()) {
                return usageError("Missing value for --level",
                                  "The level runs from 0 (store, do not compress) to 9.");
            }
            const std::optional<int> level = ParseLevel(words[++i]);
            if (!level) {
                return usageError("Invalid compression level: " + Quoted(words[i]),
                                  "The level runs from 0 (store, do not compress) to 9.");
            }
            options.level = *level;
            continue;
        }
        if (arg.starts_with("--level=")) {
            const std::optional<int> level = ParseLevel(arg.substr(std::string("--level=").size()));
            if (!level) {
                return usageError(
                    "Invalid compression level: " + Quoted(arg.substr(std::string("--level=").size())),
                    "The level runs from 0 (store, do not compress) to 9.");
            }
            options.level = *level;
            continue;
        }

        // The archive format, asked of pack::IsSupportedFormat() rather than
        // of a second list written out here: a format the command line
        // accepted and the library then refused would turn a spelling mistake
        // into a failure reported after the work had begun. Checked here it is
        // a usage error, and worth 2 instead of 1.
        std::optional<std::string> format;
        if (arg == "--format") {
            if (i + 1 >= words.size()) {
                return usageError("Missing value for --format",
                                  "Name an archive format, or leave the flag off "
                                  "to use the default one.");
            }
            format = words[++i];
        } else if (arg.starts_with("--format=")) {
            format = arg.substr(std::string("--format=").size());
        }
        if (format) {
            if (format->empty()) {
                return usageError("Missing value for --format",
                                  "Name an archive format, or leave the flag off "
                                  "to use the default one.");
            }
            if (!pack::IsSupportedFormat(*format)) {
                return usageError("Unsupported archive format: " + Quoted(*format),
                                  std::format("Only \"{}\" is supported today.",
                                              pack::kDefaultFormat));
            }
            options.format = *format;
            continue;
        }

        // Everything else that starts with a dash is a flag this command does
        // not have. Reporting it rather than treating it as an input path is
        // the difference between "guchho pack dist --allow-ovewrite" building
        // an archive with a strange file in it and telling the person what
        // they misspelt.
        if (arg.starts_with("-")) {
            return usageError("Unknown pack flag: " + Quoted(arg),
                              "Run 'guchho pack --help' to see the flags this command accepts.");
        }

        options.inputs.push_back(arg);
    }

    if (has_outfile && options.outFile.empty()) {
        return usageError("Missing value for --outfile",
                          "Give the path to write the archive to.");
    }

    if (options.inputs.empty()) {
        return usageError("No input files were given",
                          "Name at least one file or directory to archive.");
    }

    if (!has_outfile) {
        if (options.inputs.size() > 1) {
            return usageError(
                "Multiple inputs need an output file",
                "Name the archive with -o/--outfile, or give one input and let "
                "its own name be used.");
        }
        options.outFile = pack::DefaultOutFile(options.inputs.front());
    }

    const pack::PackResult result = pack::CreatePack(options);

    // Warnings come first, whether or not the run succeeded: they are about
    // what was left out of the archive, and a person who asked for a tree
    // containing a symlink needs to know that even when the command then
    // failed for an unrelated reason.
    for (const std::string& warning : result.warnings) {
        logger::PrintWarningToStderr(args, warning);
    }

    if (!result.Ok()) {
        if (result.note.empty()) {
            logger::PrintErrorToStderr(args, result.error);
        } else {
            logger::PrintErrorWithNoteToStderr(args, result.error, result.note);
        }
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    logger::PrintSuccessToStdout(
        args, std::format("Created {} ({})", result.path,
                          formatSize(static_cast<size_t>(result.size))));
    return static_cast<int>(ExitCode::kSuccess);
}

} // namespace guchho::cli
