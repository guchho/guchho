// =============================================================================
// src/cli/cli_run.cpp — the driver
// =============================================================================
//
// This file is the whole of a Guchho run up to the point where a decision has
// to be made: given a list of arguments, work out which command was asked for
// and hand the arguments to it. It builds nothing, reads no file and knows
// nothing about any flag. Everything it does happens before the first byte is
// compiled, and everything else happens in the file that owns the command.
//
// The division is worth stating, because it is why a command file can be read
// on its own. The work falls into three questions, and each has one home here
// or in one of the command files:
//
//   1. Which command? Answered here, once, by matching the first argument
//      against the command words. This file never parses a flag to find out.
//
//   2. Is the command line acceptable? Answered by the command that was
//      selected, using the shared flag grammar, because only that command
//      knows which of the two option structures its flags belong to.
//
//   3. Is the run to be quiet, and which exit code does it produce? Answered
//      here, and passed down, so that the two answers cannot disagree between
//      commands.
//
// The arguments arrive as a vector of strings with the executable name already
// removed. This file never sees argv and never splits anything: a path
// containing a space arrives as one element because whoever built the vector
// decided where the arguments began and ended, and a caller embedding Guchho
// makes that same decision about its own command line. The consequence is that
// a path is never re-parsed here and cannot be mis-split, and that a host
// which already has its arguments as data — a test, an editor, a language
// server — can call in without quoting anything.
//
// Nothing in this file keeps state between calls. Two runs with equal
// arguments print the same things and return the same code, whether they come
// from two different threads or from one thread a million times.
// =============================================================================

#include "guchho/api.hpp"
#include "guchho/cli.hpp"

#include <atomic>
#include <clocale>
#include <csignal>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

namespace guchho::cli {
// =============================================================================
// The interrupt flag
// =============================================================================
//
// An interrupt that ends a Guchho process where it stands is almost always
// ending it at the least convenient moment: half a bundle written, a cache
// half updated, a service worker copy of the previous output already replaced
// by one that does not parse. The default disposition of SIGINT is exactly
// that, so it is replaced here, once, for the commands that can run long
// enough for it to matter.
//
// What the handler is allowed to do is the whole design. It runs on the
// context the signal interrupted, at an arbitrary point inside the build, and
// the only safe thing it can touch is a flag: no allocation, no logging, no
// lock that a thread holding it might be waiting on. A single relaxed store
// is that.
//
// The flag is deliberately not read here either. It is file-local, so nothing
// outside this file could read it even if it wanted to, and what the store
// buys today is the absence of a kill rather than a graceful exit: an
// interrupted run finishes the pass it was in and returns the exit code that
// pass produced. ExitCode::kInterrupted exists in the command line surface for
// the reader that does check the flag before reporting success, and no such
// reader is written yet.
static std::atomic<bool> g_interrupted{false};

// Relaxed ordering, because the flag carries no data. A reader would have
// nothing to order against: it would not be reading a message the handler
// wrote, only learning that a signal arrived. The atomicity is what matters
// here — the store has to be visible to whichever thread later decides what to
// do, and it must not be a torn read of a byte that two signals could both be
// writing.
static void signalHandler(int) {
    g_interrupted.store(true, std::memory_order_relaxed);
}

// Arranges for an interrupt to be noticed rather than obeyed. Called once per
// run, by the commands whose work is long enough to be interrupted at all.
//
// SIGINT is installed everywhere because it is the interrupt a person types.
// SIGTERM is installed only where it exists: Windows has no such signal, it
// ends processes through a different mechanism, and asking for it there would
// not compile.
//
// Input:  called once, before a build or a watch session starts
// Output: nothing to return. SIGINT — and SIGTERM on the platforms that have
//         it — now record the interrupt in g_interrupted instead of ending
//         the process, so the run finishes what it was doing.
void installSignalHandlers() {

    std::signal(SIGINT, signalHandler);
#ifndef _WIN32
    std::signal(SIGTERM, signalHandler);
#endif
}

// =============================================================================
// detectCommand — the command word
// =============================================================================
//
// Answers one question: does the first argument name a command? There are two
// possible answers, and the second one is a question rather than a verdict.
//
// The first argument is matched against the command words, and a match is
// final. "guchho build" is a build whatever else follows it, so a project that
// happens to have a file called "info" in it cannot be built by writing
// "guchho info" — the person writes the path, and "./info" is not a command
// word. Preferring the word over the path is the right way round, because the
// word is typed deliberately and the path is usually a leftover from the last
// command.
//
// A first argument that is not a command word tells us nothing on its own,
// which is why there is a second output: "has_entry" asks whether any argument
// at all is a path rather than a flag. A bare path is the common way to ask
// for a build — "guchho src/index.js" is the same request as "guchho build
// src/index.js" — and the only thing that distinguishes the two cases is that
// one of them has a path in it. So the fallback is a scan, not a decision, and
// the caller decides what a leading path means.
//
// Two consequences of the scan covering every argument, which the first-argument
// match does not, are worth knowing before changing either rule. A command
// word is only recognised in first position, so "guchho --quiet build" does
// not select the build command: the first argument is a flag, the scan then
// finds "build" and reports it as a path, and the result is a build looking
// for a file of that name. And a command word is not a path either, so
// "guchho build" reports has_entry false even though the scan would have
// matched it — the flag is only consulted for kNone, where a command word is
// not in play.
//
// Input:  { "build", "src/index.js" }
// Output: kBuild. The match is final, so the path after it is not inspected.
//
// Input:  { "src/index.js", "--outdir=dist" }
// Output: kNone with has_entry true — the first argument is a path, not a
//         command, and something in the list is an input. The caller turns
//         this into a build.
//
// Input:  { "--outdir=dist" }
// Output: kNone with has_entry false: there is a flag but no path, so there is
//         nothing to build and nothing was asked for.
//
// Input:  { "--nope" }
// Output: kNone with has_entry false, exactly as above. Whether the flag is
//         known is not this function's question, and the grammar has not been
//         consulted yet.
//
// Input:  { "transform" }
// Output: kTransform, from the word alone.
Command detectCommand(const std::vector<std::string>& args, bool& has_entry) {

    // Both outputs are set on every path out of this function, including the
    // empty one, so a caller can read them without checking which way it went.
    has_entry = false;

    if (args.empty()) return Command::kNone;

    const std::string& first = args[0];
    if (first == "build")  return Command::kBuild;
    if (first == "dev")    return Command::kDev;
    if (first == "serve")  return Command::kServe;
    if (first == "watch")  return Command::kWatch;
    if (first == "init")   return Command::kInit;
    if (first == "clean")  return Command::kClean;
    if (first == "info")   return Command::kInfo;
    if (first == "transform") return Command::kTransform;

    // Not a command word, so the leading argument means something else. A path
    // is the case worth catching, and it is recognised by the one thing a
    // flag cannot be: not starting with a dash. That test is deliberately
    // cheap and deliberately shallow — "out=in" is an entry point, "-x.js" is
    // a flag as far as this function is concerned, and a value that happens to
    // be attached to a flag is not scanned separately.
    for (const auto& arg : args) {
        if (!arg.starts_with("-")) {
            has_entry = true;
            break;
        }
    }

    return Command::kNone;
}

// =============================================================================
// runImpl — the dispatcher
// =============================================================================
//
// Decides the command, settles the two things that have to be settled before
// any command can run, and hands over. It is a static function because it is
// not part of the command line surface: the two functions below it are the
// entry points, and giving this one a name of its own would invite somebody to
// call it directly and bypass nothing in particular.
//
// Input:  { "build", "src/index.js", "--outdir=dist" }
// Output: the banner, then the build's own output, and 0.
//
// Input:  { "src/index.js" }
// Output: the banner, then a build of that path, and 0 — the same build the
//         word "build" would have asked for.
//
// Input:  { "watch", "--outdir=dist" }
// Output: the banner, then a watch session that does not return.
//
// Input:  { "--help" } or { "-h" }
// Output: the banner and the usage text, and 0.
//
// Input:  { "--version" } or { "-v" }
// Output: "guchho v1.0.1", and 0.
//
// Input:  { }
// Output: the banner and the usage text, and 0. A bare invocation in a
//         script is a request for information, not a failure, so it is not an
//         error code — the same answer as --help, and for the same reason.
//
// Input:  { "--outdir=dist" }
// Output: the banner and the usage text, and 0: a flag with no path is
//         nothing to do, and saying so is more use than a complaint would be.
static int runImpl(const std::vector<std::string>& os_args,
                   const std::vector<api::Plugin>& plugins)
{
    // -------------------------------------------------------------------------
    // Make the console and the process agree on UTF-8, before anything is
    // printed
    // -------------------------------------------------------------------------
    //
    // This runs first because it is the one thing that must be true before the
    // first character reaches a stream, and nothing after it can put it right.
    //
    // Guchho's output contains whatever paths the project has, and a project
    // with a non-ASCII path in it produces messages containing that path.
    // Those messages are UTF-8, so the console has to be told to read them as
    // UTF-8. On Windows that is two things: the console's own code pages, which
    // decide how bytes are turned into glyphs, and the C locale, which decides
    // how the standard library turns bytes into characters when it prints them.
    // Setting only one of the two gives a run where the message text is right
    // and some of it comes out as replacement characters.
    //
    // The locale name is asked for as ".UTF-8" first, because the empty name
    // means "whatever the environment already says", and an inherited
    // environment is a machine's configuration rather than this program's. Not
    // every Windows installation knows that name, so the empty name is the
    // fallback and is a better answer than failing to start.
    //
    // WIN32_LEAN_AND_MEAN is defined before windows.h above because the two
    // headers this file needs from it are the console functions and nothing
    // else, and the full declaration costs compile time on every file that
    // follows this pattern.

#ifdef _WIN32
    if (!std::setlocale(LC_ALL, ".UTF-8")) {
        std::setlocale(LC_ALL, "");
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#else
    std::setlocale(LC_ALL, "");
#endif

    // -------------------------------------------------------------------------
    // The three questions about the command line as a whole
    // -------------------------------------------------------------------------
    //
    // None of the flags below belongs to any command. They are answered here
    // because they are about the entire argument list rather than about an
    // entry point or an output location, and answering them anywhere else would
    // mean every command file repeating the same three tests.
    //
    // The empty case prints the usage text, and so does the case where the
    // arguments name no command and contain no path. Both return 0 for the
    // reason given above: nothing has failed, and a build tool that treats
    // "guchho" with no arguments as an error makes a shell script that runs it
    // to check a version report a failure it then has to interpret.
    //
    // --help and --version require an argument list of exactly one. That is
    // not fussiness about --help, which each command answers for itself, but
    // it is what keeps the short forms from colliding with the flag grammar.
    // "-v" is the one that matters: at the top level it is the version, and
    // inside a build the grammar rejects it and suggests "--log-level=verbose"
    // instead, because that is what a person who typed "-v" after "build" meant.
    // Both readings can only coexist because neither of them can see the other's
    // arguments.

    if (os_args.empty()) {
        printUsage(std::cout);
        return 0;
    }

    if (os_args.size() == 1 && (os_args[0] == "--help" || os_args[0] == "-h")) {
        printUsage(std::cout);
        return 0;
    }
    if (os_args.size() == 1 && (os_args[0] == "--version" || os_args[0] == "-v")) {
        printVersion(std::cout);
        return 0;
    }

    // -------------------------------------------------------------------------
    // Which command, and what to do when there is none
    // -------------------------------------------------------------------------
    //
    // A path in the arguments with no command word in front of it means build.
    // That is the one inference this function makes, and it is the one that
    // makes "guchho src/index.js" work; there is no configuration, flag or
    // environment variable that changes it, because a single path is an
    // unambiguous request and a default nobody can turn off is easier to
    // remember than a default governed by a setting.
    //
    // Still nothing after that, so there is no command to run. Usage is printed
    // and 0 returned, for the same reason the empty case does: a run that asked
    // nothing cannot have failed at anything.

    bool has_entry = false;
    Command cmd = detectCommand(os_args, has_entry);

    if (cmd == Command::kNone && has_entry) {
        cmd = Command::kBuild;
    }

    if (cmd == Command::kNone) {
        printUsage(std::cout);
        return 0;
    }

    // -------------------------------------------------------------------------
    // Quiet, read the same way everywhere
    // -------------------------------------------------------------------------
    //
    // "quiet" is not a build flag, so it cannot be read by the grammar: it
    // changes what the run prints before the grammar has been consulted, and
    // it is honoured by every command, so it belongs to the dispatcher.
    //
    // The scan stops at the first occurrence and removes nothing — the
    // argument list the command receives is the one that arrived, unchanged, so
    // the command's own grammar still sees "--quiet" among its arguments and
    // reports it as a flag it does not know. That is worth knowing when a
    // command line is assembled here rather than typed: the flag is honoured
    // here and still diagnosed there.

    bool quiet = false;
    for (const auto& arg : os_args) {
        if (arg == "--quiet") { quiet = true; break; }
    }

    // -------------------------------------------------------------------------
    // Hand over
    // -------------------------------------------------------------------------
    //
    // Every command is given the arguments exactly as they arrived, including
    // the command word. Each one drops that word itself, for the same reason
    // each one answers its own --help: the command is the only place that knows
    // what its own spelling of help says.
    //
    // The banner is printed here for the two commands whose output is a build
    // and then nothing else, and by the command itself for the two that have
    // something to announce under it — a server prints the address it bound to
    // directly beneath the banner, so the banner belongs with the command that
    // has a line to follow it. transform, init, clean and info print no banner
    // at all: their output is the answer.
    //
    // Each case returns the command's exit code unchanged, which is why a
    // command can return kCLIUsageError for a bad flag and kBuildFailure for a
    // build that did not succeed and be believed about either way. The default
    // arm is not reachable while every enumerator is handled above it, and
    // exists so that a command added to the enumeration without a case here
    // shows a person their usage text rather than falling out of the function.

    switch (cmd) {
    case Command::kBuild: {
        if (!quiet) printBanner(std::cout);
        int result = runBuild(os_args, quiet, plugins);
        return result;
    }
    case Command::kTransform:
        return runTransform(os_args);
    case Command::kWatch: {
        if (!quiet) printBanner(std::cout);
        return runWatch(os_args, quiet, plugins);
    }
    case Command::kServe:
        return runServe(os_args);
    case Command::kDev:
        return runDev(os_args, plugins);
    case Command::kInit:
        return runInit(os_args);
    case Command::kClean:
        return runClean(os_args);
    case Command::kInfo:
        return runInfo(os_args);
    default:
        printUsage(std::cout);
        return 0;
    }
}

// =============================================================================
// The command line surface
// =============================================================================
//
// Two functions, and the only reason there are two is the plugin list. Both
// call straight through to the dispatcher above, so there is no behaviour here
// that a caller can reach through one and not the other.
//
// Run() is what a shell script, a test harness or an editor integration calls.
// RunWithPlugins() is the same call for a host that has something to add to a
// build: a plugin can contribute files, read a flag before the arguments are
// parsed, and run before and after each build, and the engine calls only the
// plugins it was handed. The list is borrowed for the duration of the call and
// is not retained afterwards, so a plugin that outlives the run must not still
// be reachable from a background thread once the call has returned.
//
// A run with no plugins is self-contained: it reads what it needs, writes what
// was asked for, and prints a summary. Passing an empty list is the same thing,
// which is what makes Run() a one-line function rather than a second
// implementation.
//
// Input:  Run({ "build", "src/index.js", "--outdir=dist" })
// Output: the banner, the summary of the files written to "dist", and 0.
//
// Input:  Run({ "build", "--nope" })
// Output: "Invalid build flag: \"--nope\"" on the error stream with a note
//         naming the flags that do exist, and 2 — nothing was read from disk,
//         because the command line is rejected before the build starts.
//
// Input:  Run({})
// Output: the banner and the usage text, and 0.
//
// Input:  RunWithPlugins({ "build", "src/index.js" }, { size_report_plugin })
// Output: the same build, its summary, and 0, with the plugin's hooks called at
//         the points api::Plugin describes.
int Run(const std::vector<std::string>& os_args) {

    return runImpl(os_args, {});
}

int RunWithPlugins(const std::vector<std::string>& os_args,
                   const std::vector<api::Plugin>& plugins) {

    return runImpl(os_args, plugins);
}

} // namespace guchho::cli
