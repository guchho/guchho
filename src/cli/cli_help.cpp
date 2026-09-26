// =============================================================================
// src/cli/cli_help.cpp — what the command line says about itself
// =============================================================================
//
// Everything a person can be told by Guchho without running a build is written
// here: the banner, the usage text, the version line, the per-command help,
// and the summary of what a finished build produced.
//
// It is a separate file from the commands because these strings are the one
// part of the command line that is read by people rather than by the grammar,
// and they change for a different reason. A flag is added because the build
// needs it; a line of help is added or reworded because somebody reading it
// was confused, or because a flag's meaning changed underneath a sentence that
// described it. Mixing the two would have meant editing a build to fix a
// sentence.
//
// Three rules run through everything below.
//
// Help is written per command, not derived from the grammar. The flag tables
// in the grammar could have produced a list of every flag automatically, and
// the result would have been a list and not a description: which of the three
// minify switches to reach for, what a target is, when a bare path is enough.
// Those sentences are the reason this file exists, so the cost of a flag that
// the grammar accepts and this file does not mention is paid on purpose, in
// review, by a person.
//
// The two are not allowed to drift silently. A flag added to the grammar is
// one somebody has to add here as well, and the two words in these texts that
// look like flags but are not the grammar's business are exactly the two that
// are: "--quiet" is read by the dispatcher before any command runs, and
// "--verbose" is the long form of the log level that "--log-level" sets, so
// both are advertised here and neither appears in the grammar's own tables.
//
// Nothing here reads state or touches the file system. Every function takes a
// stream, writes text to it, and returns. That is what makes the whole file
// testable against a string, and it is why a help text can be printed to a
// file or a pipe in a test without anything having to be redirected to get
// there.
// =============================================================================

#include "guchho/cli.hpp"

#include <algorithm>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace guchho::cli {

// =============================================================================
// What the tool calls itself
// =============================================================================
//
// The wordmark, as a raw string literal so that the shapes of the letters are
// exactly as drawn here. Every glyph in it is a box-drawing or block character,
// and the rows line up only because the literal is not re-indented: this is one
// of the two places in the project where leading whitespace is content rather
// than formatting, and the closing delimiter sits in the first column for the
// same reason.
//
// It is text and not an image, which matters for a tool that prints to a
// pipe, a log file or a terminal with a limited character set. Where those
// glyphs cannot be rendered, what is left is a block of substitutions — which
// is still a recognisable shape, and costs nothing to carry. An image would
// have needed a second representation for every place it could not be shown.
static constexpr const char* kBANNER = R"(
   ██████╗ ██╗   ██╗ ██████╗██╗  ██╗██╗  ██╗ ██████╗
  ██╔════╝ ██║   ██║██╔════╝██║  ██║██║  ██║██╔═══██╗
  ██║  ███╗██║   ██║██║     ███████║███████║██║   ██║
  ██║   ██║██║   ██║██║     ██╔══██║██╔══██║██║   ██║
  ╚██████╔╝╚██████╔╝╚██████╗██║  ██║██║  ██║╚██████╔╝
   ╚═════╝  ╚═════╝  ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝
)";

// The version is a compile-time string rather than a number formatted at run
// time, so that the banner, the usage text and the version line cannot be built
// from three different pieces of information.
//
// The macro is the seam. A build is expected to supply it — that is the only
// way a single place has to change for a release — and the fallback below is
// what makes this file compile, and what a build that supplies nothing reports.
// The fallback is deliberately obvious: a version nobody set should be
// recognisable as a version nobody set, because the alternative is a banner
// quietly claiming a release that does not exist.
#ifndef GUCHHO_VERSION_STRING
#define GUCHHO_VERSION_STRING "0.1.0"
#endif
static constexpr const char* kVersionString = GUCHHO_VERSION_STRING;

// =============================================================================
// The banner, the usage text, the version line
// =============================================================================
//
// One line of identity, printed before anything a run produces, plus the two
// answers to questions that are about the tool rather than about a build.
//
// The banner leads with a blank line because the literal above begins with one.
// That is a property of the literal and not a decision made here, and it is
// what keeps the wordmark from starting hard against whatever was printed
// before it.

// Prints the wordmark, the version and one line saying what the tool is.
//
// Called by the dispatcher for the two commands whose output is a build and
// then nothing else, and by the two server commands themselves, which print it
// immediately above the address they bound to. It is never printed by a
// command whose whole output is an answer, because a banner above a list of
// versions is noise between a person and what they asked for.
//
// Input:  called with the stream the run is printing to
// Output: a blank line, the six rows of the wordmark, "Guchho v<version>", the
//         line "Fast HTML-first JavaScript, CSS & asset bundler", and a blank
//         line to separate all of it from what comes next.
//
// Input:  the same call, with the run quiet
// Output: the same text. Quieting a run is the caller's decision and is made
//         where the banner is printed, not here.
void printBanner(std::ostream& os) {
    os << kBANNER << "\n"
       << "Guchho v" << kVersionString << "\n"
       << "Fast HTML-first JavaScript, CSS & asset bundler\n\n";
}

// The answer to a run that named no command, and to one that asked for help.
//
// This is the only text that lists every command, so it is the only place where
// the set of commands is a fact rather than an accident of which function was
// written. It opens with the banner because a person who typed "guchho" and got
// this has not seen the tool yet, and a usage line on its own is an answer to a
// question they have not formed.
//
// The list is one line per command, with the arguments each one takes in the
// position where the reader is looking for them, and the rest of the line
// saying what it is for in a phrase rather than a sentence. "serve [dir]" tells
// a person that a directory may follow; "init" tells them that it may not.
//
// The two closing lines exist so that the two other ways of asking are visible
// from here: a command's own help, which is more specific, and the version,
// which is the question asked most often by a script.
//
// Input:  called when the argument list is empty, when it is a lone --help, and
//         when it names no command and contains no path
// Output: the banner, then "Usage:", the eight commands, and the two closing
//         lines. The same text in all three cases, because they are the same
//         question asked three ways.
void printUsage(std::ostream& os) {
    printBanner(os);
    os << "Usage:\n"
       << "  guchho <command> [options]\n"
       << "\n"
       << "Commands:\n"
       << "  build [entry...]    Production build\n"
       << "  dev                 Development server with watch\n"
       << "  serve [dir]         Serve built output\n"
       << "  watch [entry...]    Watch and rebuild without server\n"
       << "  init                Initialize a new project\n"
       << "  clean               Remove build output\n"
       << "  info                Show environment info\n"
       << "  transform           Transform source from stdin\n"
       << "\n"
       << "Run 'guchho <command> --help' for command-specific help.\n"
       << "Run 'guchho --version' for version information.\n";
}

// The version, and nothing else.
//
// It is the one answer in this file with no banner above it, because the two
// questions "what is this" and "which one is this" have different audiences: a
// person who typed "guchho" is asking the first, and a script that captures the
// output is asking the second and would have to strip a wordmark to get it.
//
// Input:  called when the argument list is a lone --version or -v
// Output: one line, "guchho v<version>", and a newline.
void printVersion(std::ostream& os) {
    os << "guchho v" << kVersionString << "\n";
}

// =============================================================================
// Help for the individual commands
// =============================================================================
//
// Eight functions, one per command, each printed by the command itself when it
// sees --help before it has done anything else. Printing it from the command
// rather than from the dispatcher is what lets the text describe that command's
// flags and no others, and it means a run that has already printed a banner has
// already told the person what the tool is before the specifics arrive.
//
// Every one of these texts has the same four parts in the same order — a usage
// line, a blank line, one sentence saying what the command is for, the options
// — and then either examples or a short closing block. The order is not
// arbitrary: what the command is called, what it does, what it accepts, what it
// looks like. A person who has read the first three lines should be able to
// stop.
//
// The options are in one column, left-aligned, with the description starting at
// a fixed distance so that a long flag name does not push its own description
// out of line. The values in angle brackets are the ones a flag requires rather
// than accepts as a switch.

// The build: the largest of the eight, and the reference for the rest.
//
// This is the text every other one is measured against, because build is the
// command that has every kind of flag: a switch, a switch that takes a value, a
// repeated flag naming a package, and a flag whose value is itself a name and a
// value. The examples at the end are the three shapes a person is most likely
// to want — a document with an output directory, a module minified with a
// source map, and a build that stays up.
//
// Note the two rows that are not switches: "--external" and "--define" and
// "--loader" take values after a colon rather than an equals sign, because they
// may be repeated and because a value that is itself a name and a value reads
// better as a pair. It is the one place the help is teaching the spelling of a
// flag rather than describing it.
//
// Input:  called by the build command when the argument list contains --help or
//         -h, before any flag is read
// Output: the usage line, a description naming the three kinds of input it
//         processes, the options above, and three worked examples.
void printBuildHelp(std::ostream& os) {
    os << "Usage: guchho build [entry...] [options]\n"
       << "\n"
       << "Production-oriented build. Processes HTML, JS, CSS and assets.\n"
       << "\n"
       << "Options:\n"
       << "  --outdir=<dir>         Output directory\n"
       << "  --outfile=<file>       Output file\n"
       << "  --minify               Minify all output\n"
       << "  --sourcemap            Enable source maps\n"
       << "  --target=<target>      Target (es2020, chrome80, node16)\n"
       << "  --format=<format>      Format: iife, cjs, esm, umd, amd, system\n"
       << "  --platform=<p>         Platform: browser, node, neutral\n"
       << "  --splitting            Enable code splitting\n"
       << "  --tree-shaking         Enable tree shaking\n"
       << "  --bundle               Bundle all imports\n"
       << "  --external:<pkg>       Mark package as external\n"
       << "  --define:<k>=<v>       Replace identifier with value\n"
       << "  --loader:<ext>=<l>     Loader for file extension\n"
       << "  --watch                Watch for changes\n"
       << "  --metafile             Write metafile JSON\n"
       << "  --analyze              Analyze bundle size\n"
       << "  --jsx=<mode>           JSX: transform, preserve, automatic\n"
       << "  --tsconfig=<file>      Path to tsconfig.json\n"
       << "  --log-level=<level>    verbose, debug, info, warning, error, silent\n"
       << "  --color / --no-color   Force color on/off\n"
       << "  --quiet                Minimal output\n"
       << "  --verbose              Detailed output\n"
       << "\n"
       << "Examples:\n"
       << "  guchho build src/index.html --outdir=dist\n"
       << "  guchho build src/app.js --minify --sourcemap\n"
       << "  guchho build --watch\n";
}

// The development server: a build that rebuilds when a request arrives, and
// answers it over HTTP.
//
// The three flags that are its own rather than the build's are at the top,
// because they are the reason to choose this command over the build one: where
// it binds, and whether it opens a browser by itself. Below them sit the build
// flags, because a development server is a build that has been given an address.
//
// Input:  called by the dev command when the argument list contains --help or
//         -h
// Output: the usage line, one sentence naming the watching and the rebuilding,
//         the options above with the defaults a person is most likely to want to
//         change, and three examples — the bare command, a port, and a host
//         that is not localhost.
void printDevHelp(std::ostream& os) {
    os << "Usage: guchho dev [options]\n"
       << "\n"
       << "Start a development server with file watching and automatic rebuilds.\n"
       << "\n"
       << "Options:\n"
       << "  --host=<host>          Bind host (default: localhost)\n"
       << "  --port=<port>          Bind port (default: 3000)\n"
       << "  --open                 Open browser on start\n"
       << "  --outdir=<dir>         Output directory (default: dist)\n"
       << "  --minify               Minify output\n"
       << "  --sourcemap            Enable source maps\n"
       << "  --target=<target>      Target environment\n"
       << "  --log-level=<level>    Log level\n"
       << "  --color / --no-color   Force color on/off\n"
       << "  --quiet                Minimal output\n"
       << "  --verbose              Detailed output\n"
       << "\n"
       << "Examples:\n"
       << "  guchho dev\n"
       << "  guchho dev --port=8080 --open\n"
       << "  guchho dev --host=0.0.0.0\n";
}

// The static server: the same address, without the rebuilding.
//
// The shortest of the three server texts, and deliberately so. It has no build
// flags at all, and saying so is more useful than listing the ones it cannot
// use: a person who wants a rebuild wants dev, and the sentence above the
// options is what tells them that.
//
// The one positional argument is a directory of already-built output, which is
// why the usage line carries it and why the second example gives one. It
// defaults to the usual output directory, which is what the bare command serves.
//
// Input:  called by the serve command when the argument list contains --help
//         or -h
// Output: the usage line, one sentence saying it serves without rebuilding, the
//         three address flags, and two examples — the bare command and one with
//         a directory and a port.
void printServeHelp(std::ostream& os) {
    os << "Usage: guchho serve [dir] [options]\n"
       << "\n"
       << "Serve built output without rebuilding.\n"
       << "\n"
       << "Options:\n"
       << "  --host=<host>          Bind host (default: localhost)\n"
       << "  --port=<port>          Bind port (default: 3000)\n"
       << "  --open                 Open browser on start\n"
       << "\n"
       << "Examples:\n"
       << "  guchho serve\n"
       << "  guchho serve dist --port=8080\n";
}

// Watch without a server: the command for when whatever reads the output is
// not a browser.
//
// The shortest of the eight, because the flags are not its own. The one line
// under "Options" says so rather than repeating the build's twenty, and that is
// the honest text: a person who has read the build help already knows them, and
// a person who has not is one command away from it. The parenthetical in the
// description is the distinction from dev that matters most, since the two are
// otherwise easy to confuse — one answers requests, this one writes files and
// leaves the rest alone.
//
// Input:  called by the watch command when the argument list contains --help or
//         -h
// Output: the usage line, one sentence naming the watching and saying there is
//         no server, the single options line, and two examples.
void printWatchHelp(std::ostream& os) {
    os << "Usage: guchho watch [entry...] [options]\n"
       << "\n"
       << "Watch source files and rebuild on changes (no server).\n"
       << "\n"
       << "Options:\n"
       << "  (same as 'guchho build')\n"
       << "\n"
       << "Examples:\n"
       << "  guchho watch src/index.html\n"
       << "  guchho watch --outdir=dist\n";
}

// Project scaffolding: the command that writes files rather than reading them.
//
// The only help text here with a "Generates" section instead of examples,
// because for a command that creates a project the files it will create are the
// most useful thing to know before running it — particularly whether anything
// already there will be touched. The configuration file is listed alongside the
// three sources rather than apart from them, since it is as much a part of the
// result as they are.
//
// The single flag is the one that decides whether the command may overwrite, and
// it is the reason the section is there at all: a scaffolding command is the one
// thing in this list that writes where the user did not ask it to.
//
// Input:  called by the init command when the argument list contains --help or
//         -h
// Output: the usage line, one sentence, the --force flag, and the four files
//         the command generates.
void printInitHelp(std::ostream& os) {
    os << "Usage: guchho init [options]\n"
       << "\n"
       << "Initialize a new project with starter files.\n"
       << "\n"
       << "Options:\n"
       << "  --force    Overwrite existing files\n"
       << "\n"
       << "Generates:\n"
       << "  src/index.html\n"
       << "  src/main.js\n"
       << "  src/style.css\n"
       << "  guchho.config.js\n";
}

// Removing what a build produced.
//
// The shortest text in the file, and the only command whose help is shorter
// than its behaviour deserves to be explained anywhere else: the flag is the
// whole interface, and it is the safe one. "--dry-run" exists because the
// default behaviour of this command is to delete, and the way to make a
// deletion command safe to try is to make the first attempt print what it would
// have done.
//
// Input:  called by the clean command when the argument list contains --help or
//         -h
// Output: the usage line, one sentence, and the --dry-run flag.
void printCleanHelp(std::ostream& os) {
    os << "Usage: guchho clean [options]\n"
       << "\n"
       << "Remove generated build output.\n"
       << "\n"
       << "Options:\n"
       << "  --dry-run    Show what would be removed without deleting\n";
}

// The environment report.
//
// No options at all, and the text says nothing about any. That is the honest
// shape for a command that reports rather than acts: a flag here would be a flag
// that changed what is reported, and the only thing worth changing about a
// report is where it goes, which is decided by the stream and not by a switch.
//
// Input:  called by the info command when the argument list contains --help or
//         -h
// Output: the usage line and one sentence, and nothing else.
void printInfoHelp(std::ostream& os) {
    os << "Usage: guchho info [options]\n"
       << "\n"
       << "Show environment and configuration information.\n";
}

// =============================================================================
// Reporting what a build produced
// =============================================================================

// A byte count as something a person can read at a glance.
//
// The three units are the ones every tool prints, chosen over the exact ones
// because this is a line in a summary rather than a figure to divide: the point
// is to say whether a bundle is in kilobytes or megabytes, and "1048576 bytes"
// says that less quickly than "1.0 MB".
//
// The base is 1024 while the names are the short ones, which is a deliberate
// choice and slightly dishonest arithmetic: 1 KB here is 1024 bytes, and a
// person comparing this line against a tool that uses 1000 will be out by
// three percent. Alternating between the two conventions within one summary
// would be worse than either.
//
// Two details are worth stating because they are visible in the output. Anything
// under 1024 bytes is printed as a whole number of bytes, because "0.5 KB" for a
// file that is 512 bytes long is less readable than "512 B". Everything above
// that is printed to one decimal place, and the unit changes at each power of
// 1024 rather than at each power of 1000 — which means the largest figure in KB
// is "1024.0 KB", a value that has been rounded up from a file one byte short of
// a megabyte.
//
// Input:  bytes = 512
// Output: "512 B".
//
// Input:  bytes = 1536
// Output: "1.5 KB".
//
// Input:  bytes = 5 * 1024 * 1024
// Output: "5.0 MB".
//
// Input:  bytes = 1024 * 1024 - 1
// Output: "1024.0 KB", since the change of unit is checked before the value is
//         rounded.
std::string formatSize(size_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) {

        // A stream rather than a formatted string function, because the fixed
        // notation and the single decimal place have to be set together, and
        // this is the one place in the command line that formats a number for a
        // person rather than for a machine.
        double kb = static_cast<double>(bytes) / 1024.0;
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(1);
        oss << kb << " KB";
        return oss.str();
    }

    // The megabyte case is written out rather than folded into a loop over the
    // two units, because two cases do not need one and a loop would need the
    // names to be a table. A gigabyte of output is a broken command line, not a
    // bundle, and there is nothing past megabytes to print.
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(1);
    oss << mb << " MB";
    return oss.str();
}

// The closing summary of a build: one line per file, and a total.
//
// It is called once a build has finished, after the caller has already
// established that there were no errors, so everything here is a report of
// something that happened rather than a report of a problem. A failed build has
// no output files to list, which means this prints nothing for one — and that is
// not a special case handled here, it is a consequence of a build either
// producing output or producing errors.
//
// Errors and warnings are not printed here. They have already been printed, with
// their paths, by the logger while the build ran, and repeating them here would
// be a second report of the same events with less of the detail.
//
// The files are sorted so that two runs of the same build print the same lines
// in the same order. A build that produced its output in a different order
// between runs — which resolution order makes entirely possible — would
// otherwise produce a summary that differs on every run, and a summary nobody
// can read twice is one nobody reads.
//
// Input:  a result holding one output file of 1536 bytes at "dist/app.js", and
//         elapsed_ms = 412.7
// Output: "  dist/app.js  1.5 KB", then a blank line, then
//         "  1 file generated in 412ms".
//
// Input:  the same result, with quiet set
// Output: nothing at all. The check is here and not only at the call site so
//         that the function cannot print under a quiet run however it is
//         reached.
//
// Input:  a result with no output files
// Output: nothing, and in particular no "0 files generated" line — a build that
//         produced nothing has already said so, and a count of zero here would
//         be a fact about the summary rather than about the build.
void printBuildSummary(const api::BuildResult& result, double elapsed_ms, bool quiet) {
    if (quiet) return;

    // Pairs rather than a sort of the output files themselves: the result is
    // shared with whatever produced it, and sorting it in place would reorder
    // something the caller may still be reading. A pair of path and size is also
    // what the summary actually needs, since the contents are not printed.
    std::vector<std::pair<std::string, size_t>> files;

    for (const auto& f : result.output_files) {
        files.emplace_back(f.path, f.contents.size());
    }

    // Sorted by path, which the pair does by its first member. Two files with
    // the same path cannot happen in one result, so the size is never reached
    // as a tiebreaker and the order of equal paths does not matter.
    std::sort(files.begin(), files.end());

    // Two spaces of indent, so these lines sit under the build's own output
    // rather than at the left margin, and two spaces between the path and the
    // size, so a column of sizes lines up when the paths are of similar length
    // and stays readable when they are not.
    for (const auto& [path, bytes] : files) {
        std::cout << "  " << path << "  " << formatSize(bytes) << "\n";
    }

    if (!files.empty()) {
        std::cout << "\n  " << files.size() << " file"
                  << (files.size() == 1 ? "" : "s")
                  << " generated";

        // The elapsed time is a whole number of milliseconds because that is
        // the resolution anybody reads it at, and because a fractional
        // millisecond in a build summary is precision nobody asked for. It is
        // omitted entirely when it is zero or negative, which is what a caller
        // that never measured it passes — a time of "0ms" would claim a
        // measurement that was not made.
        if (elapsed_ms > 0) {
            std::cout << " in " << static_cast<int>(elapsed_ms) << "ms";
        }
        std::cout << "\n";
    }
}

} // namespace guchho::cli
