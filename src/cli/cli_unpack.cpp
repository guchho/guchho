// =============================================================================
// src/cli/cli_unpack.cpp — the command that extracts an archive
// =============================================================================

#include "guchho/cli.hpp"
#include "guchho/logger.hpp"
#include "guchho/unpack.hpp"

#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace guchho::cli {

namespace {

std::string Quoted(const std::string& text) { return "\"" + text + "\""; }

std::optional<std::size_t> ParseSize(const std::string& text)
{
    if (text.empty()) return std::nullopt;
    std::size_t v = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<std::size_t>(c - '0');
    }
    return v;
}

} // namespace

int runUnpack(const std::vector<std::string>& args)
{
    std::vector<std::string> words = args;
    if (!words.empty() && words[0] == "unpack") {
        words.erase(words.begin());
    }

    for (const std::string& word : words) {
        if (word == "--help" || word == "-h") {
            printUnpackHelp(std::cout);
            return static_cast<int>(ExitCode::kSuccess);
        }
    }

    auto usageError = [&args](const std::string& text, const std::string& note) {
        if (note.empty()) {
            logger::PrintErrorToStderr(args, text);
        } else {
            logger::PrintErrorWithNoteToStderr(args, text, note);
        }
        return static_cast<int>(ExitCode::kCLIUsageError);
    };

    unpack::UnpackOptions options;
    bool has_outdir = false;

    for (size_t i = 0; i < words.size(); ++i) {
        const std::string& arg = words[i];

        if (arg == "--overwrite") {
            options.overwrite = true;
            continue;
        }

        if (arg == "-o" || arg == "--outdir") {
            if (i + 1 >= words.size()) {
                return usageError("Missing value for " + arg,
                                  "Give the path to extract into.");
            }
            options.outDir = words[++i];
            has_outdir = true;
            continue;
        }
        if (arg.starts_with("--outdir=")) {
            options.outDir = arg.substr(std::string("--outdir=").size());
            has_outdir = true;
            continue;
        }

        if (arg == "--strip-components") {
            if (i + 1 >= words.size()) {
                return usageError("Missing value for --strip-components",
                                  "Give a non-negative number of components to strip.");
            }
            auto v = ParseSize(words[++i]);
            if (!v) {
                return usageError("Invalid value for --strip-components: " + Quoted(words[i]),
                                  "Give a non-negative number of components to strip.");
            }
            options.stripComponents = *v;
            continue;
        }
        if (arg.starts_with("--strip-components=")) {
            auto v = ParseSize(arg.substr(std::string("--strip-components=").size()));
            if (!v) {
                return usageError("Invalid value for --strip-components",
                                  "Give a non-negative number of components to strip.");
            }
            options.stripComponents = *v;
            continue;
        }

        std::optional<std::string> format;
        if (arg == "--format") {
            if (i + 1 >= words.size()) {
                return usageError("Missing value for --format",
                                  "Name an archive format: zip, tar, or tar.gz.");
            }
            format = words[++i];
        } else if (arg.starts_with("--format=")) {
            format = arg.substr(std::string("--format=").size());
        }
        if (format) {
            if (format->empty()) {
                return usageError("Missing value for --format",
                                  "Name an archive format: zip, tar, or tar.gz.");
            }
            if (!unpack::IsSupportedFormat(*format)) {
                return usageError("Unsupported archive format: " + Quoted(*format),
                                  unpack::SupportedFormatsNote());
            }
            options.format = *format;
            continue;
        }

        if (arg.starts_with("-")) {
            return usageError("Unknown unpack flag: " + Quoted(arg),
                              "Run 'guchho unpack --help' to see the flags this command accepts.");
        }

        if (options.archive.empty()) {
            options.archive = arg;
        } else {
            return usageError("Unexpected argument: " + Quoted(arg),
                              "Only one archive can be unpacked at a time.");
        }
    }

    if (options.archive.empty()) {
        return usageError("No archive file was given",
                          "Name an archive to extract.");
    }

    if (!has_outdir || options.outDir.empty()) {
        options.outDir = unpack::DefaultOutDir(options.archive);
    }

    const unpack::UnpackResult result = unpack::CreateUnpack(options);

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

    logger::PrintSuccessToStdout(args, std::format("Extracted {} to {}", options.archive, result.outDir));
    return static_cast<int>(ExitCode::kSuccess);
}

} // namespace guchho::cli
