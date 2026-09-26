// =============================================================================
// guchho/cli.hpp — the command line surface of Guchho
// =============================================================================
//
// This header is the boundary between Guchho as a program and Guchho as a
// library. Everything the shipped driver can do is reachable from here, and
// nothing here reaches back into the parser, the linker or the file system
// internals: the declarations below speak only in terms of api::BuildOptions,
// api::TransformOptions, api::Plugin, filesystem::Fs and the logger.
//
// The entry points an embedder is expected to call fall into three groups:
//
//   1. Running a command line. Run() and RunWithPlugins() take the arguments
//      that follow the executable name, print usage and diagnostics on the
//      standard streams, and return the code the process should exit with.
//      This is what a shell script, a test harness or an editor integration
//      calls.
//
//   2. Reading arguments without running anything. ParseBuildOptions() and
//      ParseTransformOptions() share the flag grammar with the command line but
//      perform no I/O, so an embedder can find out what a command line would
//      mean — or check one a user just typed — and decide for itself what to
//      do with it.
//
//   3. The helpers behind them: the property name cache that keeps shortened
//      names stable between builds, the two part error message every flag
//      complaint is reported with, the loader name table, and the heuristic
//      that switches bundling on for a document entry point.
//
// A fourth part sits below those three, under a banner of its own: the
// declarations the files inside src/cli share with one another — the exit
// codes, the flag grammar, the printed help, and the one function per command.
// Those are here because the command line is split into a file per command and
// the pieces have to see each other. They were kept for a while in a second
// header private to that directory, which is now this section: a declaration an
// embedder cannot name is a declaration that cannot be documented, traced or
// tested from outside, and the two audiences read the same grammar and the same
// exit codes either way. Nothing in the three groups above calls anything below,
// so a caller that only wants the entry points can stop before it.
//
// Rules that hold everywhere below:
//
//   * Arguments are a vector of strings and never include the executable name,
//     exactly as a host program receives them after splitting its own command
//     line. A path containing a space is still a single element, so nothing
//     here has to guess where a value began or ended.
//
//   * Configuration is input only, and failure is data. The parse functions
//     return the message in the second half of a pair instead of throwing, and
//     hand back the options they managed to fill in alongside it, so a caller
//     can show what it did understand as well as what it rejected.
//
//   * The cache helpers are the only ones that touch storage, and they do it
//     through the filesystem::Fs instance they are given, so a run against an
//     in-memory tree behaves exactly like a run on disk.
//
//   * Nothing here keeps state between calls. The same arguments produce the
//     same result every time, which is what makes the whole surface usable
//     from a test.
// =============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include <unordered_map>

#include "guchho/api.hpp"
#include "guchho/filesystem.hpp"

namespace guchho::cli {

    // =========================================================================
    // The property name cache
    // =========================================================================
    //
    // Shortening identifiers turns readable property names into one or two
    // letters, and the same property has to keep the same short name in every
    // build of a project. A name that moved between two builds would invalidate
    // every cached copy of the previous file — a browser cache, a service
    // worker copy, an incremental client — and force the whole application to
    // reload for a change that touched one property. Guchho therefore keeps a
    // small JSON file, named by the build's "mangle_cache" option, that maps
    // each original property name to the short name chosen for it.
    //
    // The file is the unit of consistency, and it belongs to the build rather
    // than to this header: the functions below read it, hand the mapping to
    // the linker, and write the updated mapping back when the run finishes.

    // The file as it was read, in the two shapes the build needs.
    struct MangleCacheResult {
        // Original property name -> the short name chosen for it. An empty
        // optional is a property that is deliberately left alone: the file
        // recorded "false" for it, and shortening it would break whatever
        // outside the bundle reads it by name — a template, a configuration
        // payload, a test that reflects over the result.
        std::unordered_map<std::string, std::optional<std::string>> cache;

        // The same keys in the order the file listed them. The map cannot carry
        // that, and the order is what keeps the rewritten file a small diff
        // instead of a reshuffle on every build.
        std::vector<std::string> order;
    };

    // Reads the cache file at "abs_path" and returns it in the form above.
    //
    // The read goes through "fs" rather than around it, so a run against an
    // in-memory tree sees the same file a run on disk would. "os_args" is not
    // scanned for options: it is handed to the logger so that a complaint
    // about this one file is printed with the same verbosity and colour
    // settings as the rest of the run.
    //
    // The interesting case is the one that is not a failure. A file that is not
    // there is normal on a first build, so it produces an empty result and no
    // message, and the build simply starts a fresh cache. Everything else — a
    // file that cannot be read, a syntax error, a top level that is not an
    // object, a value that is neither a string nor "false" — is reported
    // through that log and also produces an empty result, because a mapping
    // that is only half understood is worse than none: continuing with it
    // would silently shorten a different set of properties than the previous
    // build did.
    //
    // Input:  a file containing
    //             { "firstName": "a", "apiKey": false }
    // Output: cache holding "firstName" -> "a" and "apiKey" -> empty optional,
    //         order holding both names in the order they were written, and
    //         nothing logged.
    //
    // Input:  a file containing { "firstName": true }
    // Output: an error naming "firstName" and asking for a name or "false",
    //         logged, and both containers empty in the result.
    //
    // Input:  no file at that path
    // Output: both containers empty, nothing logged.
    MangleCacheResult parseMangleCache(
        const std::vector<std::string>& os_args,
        filesystem::Fs& fs,
        const std::string& abs_path);

    // Formats a mapping back into the cache file's text form. Nothing is
    // written here — the caller decides where the text goes, which is what
    // lets a build keep the file next to its configuration and lets a test
    // compare the string.
    //
    // Two details make the file pleasant to keep in a repository. Names are
    // printed in the order they were read, so a build that changed nothing
    // rewrites the file byte for byte. And when a build has added names since
    // the file was read, the new ones are placed to match the file's existing
    // style: merged into a fully sorted file if the original order was already
    // sorted, and otherwise appended as a sorted block after the original
    // order, so a hand written file keeps its shape. A name that is listed in
    // "original_order" but has no mapping any more is written as "false", which
    // records that the property is now left alone instead of letting it be
    // forgotten and shortened to something new later.
    //
    // "ascii_only" escapes everything outside the ASCII range, for a cache file
    // that has to survive a terminal, an editor or a version control system
    // with a different idea of the encoding.
    //
    // Input:  cache holding "firstName" -> "a" and "apiKey" -> empty optional,
    //         original_order = { "firstName", "apiKey" }, ascii_only = false
    // Output: the text
    //             {
    //               "firstName": "a",
    //               "apiKey": false
    //             }
    //         with two space indentation and a trailing newline, and the
    //         values otherwise untouched.
    std::string printMangleCache(
        const std::unordered_map<std::string, std::optional<std::string>>& mangle_cache,
        const std::vector<std::string>& original_order,
        bool ascii_only);


    // =========================================================================
    // Two part command line errors
    // =========================================================================
    //
    // A rejected flag is only half a message on its own. "Invalid build flag:
    // \"-o\"" says that something is wrong; the note is what lets a user fix
    // it, because it names the spelling that does work. The two travel
    // together so a caller can print the advice only when there is some, and
    // keep the complaint itself free of it.

    struct ErrorWithNote {
        // The complaint, always present.
        std::string text;

        // How to fix it, and often empty: there is nothing useful to suggest
        // for a value that is simply not a number.
        std::string note;
    };

    // The one place a pair like that is built. Both parameters arrive by value
    // and are moved into the result, so a caller can hand over the temporary
    // strings produced by the quoting helpers without copying them first.
    //
    // Input:  MakeErrorWithNote("Invalid build flag: \"-o\"",
    //                           "Use \"--outfile=\" instead of \"-o\".")
    // Output: an ErrorWithNote whose text is the first argument and whose note
    //         is the second; where it is printed is the caller's decision.
    inline ErrorWithNote MakeErrorWithNote(std::string text, std::string note) {
        return ErrorWithNote{std::move(text), std::move(note)};
    }

    // =========================================================================
    // Loader names
    // =========================================================================

    // Translates one loader name into the value the engine uses, for the flag
    // that sets every file at once and for the form that names a single file.
    //
    // "out" is written only when the name is known, so a caller that gets a
    // message back can keep whatever it had stored there. The table is also the
    // single source of truth for spelling: the note attached to a bad name is
    // produced from it, so a name that is accepted and a name that is offered
    // as a suggestion can never drift apart.
    //
    // Input:  ParseLoader("json", out)
    // Output: out = api::Loader::kJSON and an empty optional, meaning success.
    //
    // Input:  ParseLoader("jsonn", out)
    // Output: out untouched, and the message
    //         "Invalid loader \"jsonn\"", which the caller turns into an error
    //         with the accepted names attached as its note.
    std::optional<std::string> ParseLoader(const std::string& value, api::Loader& out);


    // =========================================================================
    // Running a command line
    // =========================================================================
    //
    // The commands are build, transform, watch, serve, dev, init, clean and
    // info. The word that selects one is the first argument, so "guchho build
    // src/index.js" and "guchho src/index.js" ask for the same thing: a leading
    // path with no known command in front of it means build, because that is
    // what a person who typed a single path meant.

    // Runs a command line and returns the code the process should exit with.
    // The arguments are the ones that follow the executable name, in the order
    // they were given; nothing is appended, reordered or quietly defaulted
    // behind the caller's back.
    //
    // With no plugins attached a run is self contained: it reads what it needs,
    // writes what was asked for, and prints a summary. Anything that only
    // makes sense to a host program — reading a flag before the arguments are
    // parsed, or reacting to the finished run — is done by passing plugins
    // instead.
    //
    // The exit code is the useful output: 0 for success and for the forms of
    // help that are not failures, 1 when a build ran and failed, 2 when the
    // command line itself was rejected, and 130 when the run was interrupted.
    //
    // Input:  Run({ "build", "src/index.js", "--outdir=dist" })
    // Output: a banner, a short summary of the files written, and 0.
    //
    // Input:  Run({ "build", "--nope" })
    // Output: "Invalid build flag: \"--nope\"" on the error stream and 2, with
    //         nothing read from the file system.
    //
    // Input:  Run({})
    // Output: the usage text and 0, so a bare invocation in a script is not
    //         treated as a failure.
    int Run(const std::vector<std::string>& os_args);

    // The same thing with plugins attached, which is the only difference
    // between the two entry points. A plugin can add flags, inspect each parsed
    // build, run before and after the build itself, and see the paths the build
    // read; the engine never calls a plugin it was not handed here.
    //
    // The list is borrowed for the duration of the call. A plugin that outlives
    // the run must not still be reachable from a background thread once the
    // call has returned.
    //
    // Input:  RunWithPlugins({ "build", "src/index.js", "--outdir=dist" },
    //                         { size_report_plugin })
    // Output: the same build, its summary, and 0, with the plugin's hooks
    //         called at the points api::Plugin describes.
    int RunWithPlugins(const std::vector<std::string>& os_args,
                       const std::vector<api::Plugin>& plugins);

    // =========================================================================
    // Reading arguments without running anything
    // =========================================================================
    //
    // Both functions below accept the grammar the command line accepts, with
    // two differences that matter to a caller. The command word is not part of
    // that grammar here, because a host that already knows which build it is
    // configuring passes the flags alone. And parsing stops at the first
    // complaint, while still returning everything that was read before it, so
    // an editor can show the option it understood next to the message about
    // the one it did not.

    // Parses build flags.
    //
    // A bare path becomes an entry point; the same path written as "out=in"
    // becomes an advanced entry point, which is how one specific output file is
    // requested. --minify is a shorthand that sets all three minify switches at
    // once. A bare --sourcemap with no output location named anywhere in the
    // same command line means the map is inlined, since there would otherwise
    // be no file to put it in.
    //
    // Input:  ParseBuildOptions({ "--bundle", "--minify", "--outdir=dist",
    //                             "src/index.js" })
    // Output: api::BuildOptions with bundling on, all three minify switches on,
    //         outdir = "dist" and one entry point, plus an empty optional.
    //
    // Input:  ParseBuildOptions({ "dist/app.js=src/index.js", "--metafile" })
    // Output: one advanced entry point with output "dist/app.js" and input
    //         "src/index.js", a metafile requested, plus an empty optional.
    //
    // Input:  ParseBuildOptions({ "--outfil=out.js" })
    // Output: the default options, and the message
    //         "Invalid build flag: \"--outfil=out.js\"" as the second half of
    //         the pair. The suggestion of the correct spelling is a note, and
    //         notes are not part of this result.
    std::pair<api::BuildOptions, std::optional<std::string>>
    ParseBuildOptions(const std::vector<std::string>& os_args);

    // Parses transform flags, which are the same flags minus everything that
    // describes a build rather than a single file. There are no entry points,
    // no output location and no watching here, so a bare path is a mistake
    // rather than an entry point.
    //
    // Input:  ParseTransformOptions({ "--minify" })
    // Output: api::TransformOptions with minify_syntax, minify_whitespace and
    //         minify_identifiers all set, plus an empty optional.
    //
    // Input:  ParseTransformOptions({ "--minify-identifiers", "src/index.js" })
    // Output: the default transform options, and the message
    //         "Invalid transform flag: \"src/index.js\"" as the second half of
    //         the pair.
    std::pair<api::TransformOptions, std::optional<std::string>>
    ParseTransformOptions(const std::vector<std::string>& os_args);

    // =========================================================================
    // A default the caller would otherwise have to ask for
    // =========================================================================

    // Switches bundling on when a document is being built.
    //
    // A document entry point — an ".html" file — cannot be served as one copied
    // file: it refers to its scripts and styles, so a build that left bundling
    // off would hand back a directory of untouched assets instead of the single
    // file a browser needs. So when any entry point ends in ".html" and
    // bundling has not been asked for either way, it is switched on here rather
    // than refused, because such a request cannot mean anything else.
    //
    // The test is a plain case sensitive suffix check over the plain entry point
    // list, and it runs only while bundling is still off: an explicit choice on
    // the command line always wins, and an entry point given in the "output=input"
    // form is not inspected.
    //
    // Input:  entry points { "index.html" } and bundling off
    // Output: the same structure with bundling on, nothing else touched.
    //
    // Input:  entry points { "main.js" } and bundling off
    // Output: unchanged.
    //
    // Input:  entry points { "index.html" } and bundling already on
    // Output: unchanged.
    void ApplyHtmlBundleDefault(api::BuildOptions& opts);

    // =========================================================================
    // Shared by the command line's own files
    // =========================================================================
    //
    // Everything from here to the end of the namespace is here for the
    // implementation, not for the caller. The command line is one file per
    // command, and those files read the same grammar, print the same help and
    // return the same exit codes, so the declarations they have in common live
    // together below rather than in whichever file happened to be written first.
    //
    // Each one is defined exactly once, in the file named beside it. Nothing
    // here keeps state between calls: the single exception is the interrupt
    // flag, which is deliberately file-local to cli_run.cpp because no caller
    // reads it — only the handler that sets it and the commands that poll it.

    // -------------------------------------------------------------------------
    // Vocabulary
    // -------------------------------------------------------------------------
    //
    // The small types several files name in a signature or a switch.

    // The code a command returns. Run() hands it to the process, and a command
    // that fails early returns one of these rather than a bare literal, so the
    // meaning of a returned 2 is written down in one place.

    enum class ExitCode : int {
        kSuccess = 0,
        kBuildFailure = 1,
        kCLIUsageError = 2,
        kInterrupted = 130,
    };

    // The command word that selected what a run should do. The word is the
    // first argument, and a first argument that is a path rather than a command
    // is a build: see the detection in cli_run.cpp.

    enum class Command : uint8_t {
        kNone,
        kBuild,
        kDev,
        kServe,
        kWatch,
        kInit,
        kClean,
        kInfo,
        kTransform,
    };

    // Whether the flags being parsed are the ones the command line itself will
    // act on, or the ones a host program asked about through
    // ParseBuildOptions() / ParseTransformOptions(). The two grammars are the
    // same, but the external one reports the complaint without its note,
    // because a note about a command line spelling is not an answer to a
    // question an embedder asked.

    enum class ParseOptionsKind : uint8_t {
        kInternal,
        kExternal,
    };

    // The three settings a build needs that are not part of api::BuildOptions
    // because they describe the run rather than the build: watching, and the
    // two files the run is asked to write alongside the output.

    struct ParseOptionsExtras {
        bool watch{};
        int watch_delay{};
        std::optional<std::string> metafile;
        std::optional<std::string> mangle_cache;
    };

    // How much the run was asked to say about the size of what it produced.
    // Turning it on keeps the metafile, because the report is made from it.

    enum class AnalyzeMode : uint8_t {
        kDisabled,
        kEnabled,
        kVerbose,
    };

    // -------------------------------------------------------------------------
    // Interruption
    // -------------------------------------------------------------------------

    // Arranges for an interrupt to be noticed rather than ending the process
    // mid-write. Called by the commands that run long enough to be interrupted.

    void installSignalHandlers();

    // -------------------------------------------------------------------------
    // Output
    // -------------------------------------------------------------------------
    //
    // The banner, the usage text, the per-command help and the closing summary
    // of a build, all of which several commands print.

    void printBanner(std::ostream& os);
    void printUsage(std::ostream& os);
    void printVersion(std::ostream& os);
    void printBuildHelp(std::ostream& os);
    void printDevHelp(std::ostream& os);
    void printServeHelp(std::ostream& os);
    void printWatchHelp(std::ostream& os);
    void printInitHelp(std::ostream& os);
    void printCleanHelp(std::ostream& os);
    void printInfoHelp(std::ostream& os);

    void printBuildSummary(const api::BuildResult& result, double elapsed_ms, bool quiet);

    // -------------------------------------------------------------------------
    // The flag grammar
    // -------------------------------------------------------------------------
    //
    // The default options a run starts from, and the two defaults a run applies
    // once the flags have been read: bundling on for a document entry point, and
    // a guessed entry point when none was given.

    api::BuildOptions newBuildOptions();
    api::TransformOptions newTransformOptions();

    // Whether "arg" is "flag" on its own or "flag=...", and whether it is
    // something a build can use at all (a path, or the one flag that is both a
    // switch and a value).

    bool isBoolFlag(const std::string& arg, std::string_view flag);
    bool isArgForBuild(const std::string& arg);

    // A "=true" or "=false" value, a log level name, and a --target list, where
    // each target is either a language level or an engine with its version.
    // All three report through ErrorWithNote rather than by throwing, because
    // the grammar treats a rejected value as one complaint among many.

    std::pair<bool, std::optional<ErrorWithNote>> parseBoolFlag(
        const std::string& arg, bool default_value);
    std::optional<ErrorWithNote> parseLogLevel(
        const std::string& value, const std::string& arg, api::LogLevel& out);
    std::optional<ErrorWithNote> parseTargets(
        const std::vector<std::string>& targets,
        const std::string& arg,
        api::Target& out_target,
        std::vector<api::Engine>& out_engines);

    // A comma separated value, refusing to lose an empty element.

    std::vector<std::string> splitWithEmptyCheck(const std::string& s, char sep);

    // A whole number, reporting a complaint through "err" rather than by
    // throwing, and leaving it to the caller whether that complaint is fatal.

    int parseInt(const std::string& value, const std::string& arg,
                 const std::string& error_hint, std::optional<ErrorWithNote>& err);

    // Reads every flag in "os_args" into whichever of the two option structures
    // was handed in, stopping at the first complaint and returning it.
    //
    // The reason it fills in either one rather than having a build form and a
    // transform form is that the two share almost all of their flags: a separate
    // reader per structure would be a second copy of the grammar, and the two
    // copies would disagree the first time a flag was added to one of them.

    std::optional<ErrorWithNote> parseOptionsImpl(
        const std::vector<std::string>& os_args,
        api::BuildOptions* build_opts,
        api::TransformOptions* transform_opts,
        ParseOptionsKind kind,
        ParseOptionsExtras& extras);

    // What parseOptionsForRun() hands back: at most one of the two pointers is
    // ever set, and the structures are returned by value so the caller is not
    // left holding a pointer into a frame that has already gone.

    using ParseOptionsForRunResult = std::tuple<
        api::BuildOptions*,
        api::TransformOptions*,
        api::BuildOptions,
        api::TransformOptions,
        ParseOptionsExtras,
        std::optional<ErrorWithNote>
    >;

    // The options for a run, with the settings a host supplied attached and the
    // defaults a command line implies already applied.

    ParseOptionsForRunResult parseOptionsForRun(const std::vector<std::string>& os_args,
                                                const std::vector<api::Plugin>& plugins);

    // The analyze flags, taken out of the argument list before the grammar sees
    // them, along with how much was asked for.

    AnalyzeMode filterAnalyzeFlags(std::vector<std::string>& os_args);
    void addAnalyzePlugin(api::BuildOptions& build_options);

    // The plugins a host handed to RunWithPlugins(), attached to every build
    // this process makes.

    void attachPlugins(api::BuildOptions& build_options,
                       const std::vector<api::Plugin>& plugins);

    // -------------------------------------------------------------------------
    // The HTML-first default entry point
    // -------------------------------------------------------------------------
    //
    // Shared by watch and dev, which are the two commands that stay running
    // long enough for a missing entry point to be worth guessing at.

    bool applyDefaultHtmlEntry(api::BuildOptions& build_opts,
                               const std::vector<std::string>& os_args,
                               bool quiet);

    // -------------------------------------------------------------------------
    // The commands
    // -------------------------------------------------------------------------
    //
    // One per file. Each takes the arguments as given, drops the command word,
    // answers --help itself, and returns one of the ExitCode values above.

    int runBuild(const std::vector<std::string>& args, bool quiet,
                 const std::vector<api::Plugin>& plugins);
    int runTransform(const std::vector<std::string>& args);
    int runWatch(const std::vector<std::string>& args, bool quiet,
                 const std::vector<api::Plugin>& plugins);
    int runServe(const std::vector<std::string>& args);
    int runDev(const std::vector<std::string>& args,
               const std::vector<api::Plugin>& plugins);
    int runInit(const std::vector<std::string>& args);
    int runClean(const std::vector<std::string>& args);
    int runInfo(const std::vector<std::string>& args);

// =============================================================================
// End of the command line surface. Everything above is pure declaration plus
// one inline constructor: no global state, no lazily initialised registry and
// no cached grammar, so two calls with equal arguments are interchangeable
// even when they come from different threads.
// =============================================================================
} // namespace guchho::cli
