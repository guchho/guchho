// =============================================================================
// src/cli/cli_watch.cpp — the watch command
// =============================================================================
//
// "guchho watch" is a build that never finishes. Everything a one-shot build
// does, this does too: it reads the same flag grammar, resolves the same entry
// points, walks the same graph and writes the result to the same place. What
// it adds is that instead of returning an exit code it hands the run to a
// watcher thread and parks, so the files on disk are refreshed every time
// something in the graph changes.
//
// That is what makes it the right command when whatever reads the output is
// not a browser on the same machine. A local page is served by "guchho dev",
// which rebuilds when a request arrives and answers it over HTTP; a watch
// session has no server, no port and no cache headers, so it suits a static
// host, a bind mount, a container volume, or a second program pointed at the
// same directory. Someone who already has a working "guchho build" line and
// only wants the loop added writes "guchho build --watch" instead, which the
// build command handles by enabling the same watcher on the same context.
//
// The shape of the command follows from all of that:
//
//   * The grammar is the build grammar, not a smaller copy of it. This command
//     hands parseOptionsImpl a build structure and nothing else, so every flag
//     "guchho build" accepts is accepted here too, and one it does not accept
//     is reported in the same words with the same note. A flag that describes
//     a build rather than a session — the metafile, the mangle cache, the
//     minify switches — is not rejected here, and most of them keep working,
//     because a rebuild goes through the same option structure this call
//     filled in.
//
//   * The context is built once and then kept, which is the whole reason a
//     rebuild is quick: the validated options, the resolved graph and the
//     caches are already in place, so a change costs one compilation pass and
//     not a fresh session. It follows that every option has to be settled
//     before Watch() is called, because changing any of them afterwards would
//     mean discarding the context and paying the setup cost again on the very
//     next change.
//
//   * The defaults suit a session rather than a single pass. Output is written
//     to disk, because the point of the command is a directory other programs
//     read. Reported messages are capped, because a session that stays up for
//     an hour would otherwise repeat the same warning after every save an
//     editor's format-on-save produces.
//
// A watch session also has no end of its own. The function below returns only
// if the process is ended, which is what its last few lines are about.
// =============================================================================

#include "guchho/api.hpp"
#include "guchho/cli.hpp"
#include "guchho/logger.hpp"

#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace guchho::cli {

// =============================================================================
// The watch command
// =============================================================================
//
// Starts a watch session and stays up for as long as that session lasts.
//
// "args" is the command line as the dispatcher received it, which still
// contains the leading "watch" word. Dropping it here rather than in the
// dispatcher is deliberate: every command owns its own --help, and a help text
// that has to be read from a shared table says less than one written next to
// the flags it describes. The work is done on a copy, so the caller's vector
// comes back unchanged.
//
// "quiet" was decided by the dispatcher, which is the only place that looks at
// the whole command line at once, and it means two things here: the log level
// is lowered to kError, and this command prints no progress lines of its own.
// The banner is not this command's to print — the dispatcher prints it before
// calling in, so a caller that wants no banner has not asked for one by the
// time it gets here.
//
// Input:  { "watch", "src/index.html" }
// Output: the session starts. A context is built, the output goes to "dist"
//         unless a location was named, and the log stream shows
//         "[watch] build finished, watching for changes". The function does
//         not return, and the process stays up.
//
// Input:  { "watch", "--help" }
// Output: the watch help on the standard output stream, and 0. This is the
//         one place in the function that returns a bare zero rather than an
//         ExitCode: asking for help is a success, and kSuccess is that zero.
//
// Input:  { "watch", "--nope" }
// Output: "Invalid build flag: \"--nope\"" with a note listing the flags that
//         do exist, on the error stream, and kCLIUsageError. No context is
//         built, so nothing on disk has been touched at this point.
//
// Input:  { "watch" } in a directory with no index.html
// Output: "No entry points specified. Create an index.html, run \"guchho
//         init\", or pass an entry point." on the error stream, and
//         kCLIUsageError, because a watcher over an empty graph would sit
//         there reporting nothing to do.
//
// Input:  { "watch", "a.js", "b.js" } with --outfile set
// Output: the context refuses the combination — "Must use \"outdir\" when
//         there are multiple input files" — every message it produced is
//         printed to the error stream, and kBuildFailure. Options that the
//         grammar could not reject on its own are caught here instead, which
//         is why this is a build failure and the one above is a usage error.
int runWatch(const std::vector<std::string>& args, bool quiet,
                    const std::vector<api::Plugin>& plugins) {
    // -------------------------------------------------------------------------
    // An interrupt is a request to stop, not a reason to stop writing
    // -------------------------------------------------------------------------
    //
    // A watch session is interrupted in the middle of writing output far more
    // often than a one-shot build is, and a half-written file in "dist" is
    // worse than a stale one: whatever is serving that directory picks up the
    // broken file and serves it. The handler therefore records the interrupt
    // and lets the run finish. The flag it sets lives in cli_run.cpp and no
    // caller reads it, so nothing in this function changes because of it; the
    // useful part is what no longer happens — the process is not torn down in
    // the middle of a pass.

    installSignalHandlers();

    // -------------------------------------------------------------------------
    // The command word, and the one flag this command answers itself
    // -------------------------------------------------------------------------
    //
    // The word is optional, because a host that builds a command line itself
    // may not repeat it. The guard is there for the empty case: a caller is
    // free to hand over an empty vector and this must not read index 0.
    //
    // --help and -h are answered before the grammar sees anything, since the
    // grammar would reject a command whose whole argument list is a request
    // for help. The help text is this command's own, not the shared usage
    // text, so the examples name the flags a watch session actually honours.

    auto args_copy = args;
    if (!args_copy.empty() && args_copy[0] == "watch") {
        args_copy.erase(args_copy.begin());
    }

    for (const auto& arg : args_copy) {
        if (arg == "--help" || arg == "-h") {
            printWatchHelp(std::cout);
            return 0;
        }
    }

    // -------------------------------------------------------------------------
    // Options a session needs, before any flag is read
    // -------------------------------------------------------------------------
    //
    // These four assignments are the difference between a watch session and a
    // build, and they are made first so that a flag on the command line can
    // still override them:
    //
    //   log_limit = 6
    //       A cap on reported messages. A single build rarely produces more
    //       than a handful, but a session that runs all afternoon reports the
    //       same handful after every save, and the value stops being a
    //       summary. --log-limit changes it.
    //
    //   log_level
    //       Info normally, error when quiet. Info is what the watcher's own
    //       "[watch] build started" and "finished" lines are printed at, so a
    //       quiet session goes quiet all the way down.
    //
    //   write = true
    //       The single switch that lets a build touch the disk. A watch
    //       session wants it, because a directory that is never written to is
    //       not something another process can watch.
    //
    // newBuildOptions() returns the structure with its own defaults, which is
    // the same starting point "guchho build" uses; nothing here special-cases a
    // session afterwards.

    auto build_opts = newBuildOptions();
    build_opts.log_limit = 6;
    build_opts.log_level = quiet ? api::LogLevel::kError : api::LogLevel::kInfo;
    build_opts.write = true;

    // -------------------------------------------------------------------------
    // The flags, read once and never again
    // -------------------------------------------------------------------------
    //
    // The build structure is passed and the transform structure is null, which
    // is how the grammar knows which half of itself to use. kInternal means
    // the complaint is reported with its note: this is a command line somebody
    // typed, so the spelling that does work is the useful half of the message.
    //
    // "extras" collects the settings that describe a session rather than a
    // build, and this command reads exactly one of them. --watch-delay is
    // carried into the Watch() call at the end. --watch is parsed and not
    // consulted, because this command is a watch session from its first line
    // and there is nothing left for the flag to switch on — it is the flag
    // that adds watching to a build instead. Both are accepted rather than
    // rejected so that a command line written for "guchho build" also works
    // when the word "build" is changed to "watch".
    //
    // Input:  { "--outdir=dist", "--watch-delay=250" }
    // Output: an empty optional, outdir = "dist", and watch_delay = 250.
    //
    // Input:  { "--outfil=out.js" }
    // Output: an ErrorWithNote reading "Invalid build flag:
    //         \"--outfil=out.js\"" whose note suggests "--outfile=".

    ParseOptionsExtras extras;
    auto err = parseOptionsImpl(args_copy, &build_opts, nullptr, ParseOptionsKind::kInternal, extras);

    // A rejected flag ends the command here rather than being collected: a
    // half-understood command line would otherwise be carried into a session
    // that stayed up waiting for changes, with the user believing the flag they
    // mistyped had been applied. The note travels with the complaint because
    // the two are one message to the person who typed it.

    if (err) {
        logger::PrintErrorWithNoteToStderr(args_copy, err->text, err->note);
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

    // -------------------------------------------------------------------------
    // Plugins, and an output location to write to
    // -------------------------------------------------------------------------
    //
    // The host's plugins are attached before anything is built, because a
    // plugin's setup runs while the context is created and a plugin that
    // contributes files has to be known by then. The list is appended to
    // rather than replacing, so a plugin named in a configuration file keeps
    // its place.
    //
    // "dist" is the fallback destination, and it is applied only when neither
    // spelling of an output location was given — the same rule the build
    // command follows, so "guchho watch" and "guchho build" of the same entry
    // point leave their output in the same place.

    attachPlugins(build_opts, plugins);

    if (build_opts.outfile.empty() && build_opts.outdir.empty()) {
        build_opts.outdir = "dist";
    }

    // -------------------------------------------------------------------------
    // An entry point, guessed if none was named
    // -------------------------------------------------------------------------
    //
    // A session with no entry point would have no graph to watch, so this is a
    // usage error rather than an empty watch. The guess is worth making here
    // and not in a build: a person who types "guchho watch" in a project has
    // almost always just built that project, and the file is sitting in the
    // root where they left it. The search covers index.html, src/index.html
    // and public/index.html, and reports which one it took unless quiet, since
    // a session that silently watches a different file than expected is a
    // session that appears to do nothing.
    //
    // Input:  { "watch" } in a directory holding index.html
    // Output: entry_points = { "index.html" }, a line naming the entry unless
    //         quiet, and true.
    //
    // Input:  { "watch" } in an empty directory
    // Output: the "No entry points specified" message on the error stream, and
    //         false.

    if (!applyDefaultHtmlEntry(build_opts, args_copy, quiet)) {
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

    // -------------------------------------------------------------------------
    // A document entry point bundles, unless the command line said otherwise
    // -------------------------------------------------------------------------
    //
    // An .html entry refers to the scripts and styles it loads, so leaving
    // bundling off would produce a directory of untouched assets instead of
    // the single file a browser needs. The check runs over the plain entry
    // point list only, and only while bundling is still off, so an explicit
    // choice on the command line always wins. It is the shared helper rather
    // than a private line here because "guchho dev" makes the same promise
    // for the same reason, and a watcher that rebuilt a document differently
    // from the server that serves it would be a genuinely confusing thing.

    ApplyHtmlBundleDefault(build_opts);

    // -------------------------------------------------------------------------
    // Saying so, once
    // -------------------------------------------------------------------------
    //
    // One line, before the first build, because a session with no output looks
    // like a program that has hung. The blank line after it separates this from
    // the per-change lines the watcher prints from then on, and neither is
    // printed when quiet — the exit code and the errors are still there, they
    // just arrive without a running commentary.

    if (!quiet) {
        std::cout << "Watching for changes...\n\n";
    }

    // -------------------------------------------------------------------------
    // The context: everything the session will be
    // -------------------------------------------------------------------------
    //
    // Context() validates the options and prepares the build, then hands back
    // an object that is already usable — no first build is run here, and none
    // is needed, because the first build happens as soon as the watcher
    // starts and reports that it is watching.
    //
    // A null context is a set of option combinations the grammar could not
    // reject on its own, because they involve more than one flag at a time:
    // an output file named alongside several inputs, a log level that is not a
    // log level. They are ordinary user-facing problems, which is why they come
    // back as messages to print rather than as an exception, and every one of
    // them is printed rather than just the first — the second is often the one
    // that explains the first. A one-shot build reports these as a build
    // failure, and so does this: the command line was well formed and the build
    // could not be run with it.
    //
    // Input:  entry point "a.js", bundle on, write on, outfile set
    // Output: a context, ready to be watched.
    //
    // Input:  two entry points with outfile set and no outdir
    // Output: no context, and one message reading "Must use \"outdir\" when
    //         there are multiple input files".

    std::vector<api::Message> ctx_errors;
    auto ctx = api::Context(build_opts, ctx_errors);
    if (!ctx) {
        for (const auto& msg : ctx_errors) {
            logger::PrintErrorToStderr(args_copy, msg.text);
        }
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    // -------------------------------------------------------------------------
    // Hand the run to the watcher, and stay up
    // -------------------------------------------------------------------------
    //
    // Watch() does not build anything and does not return a result: it takes
    // over the session, starts its own thread, and gives that thread a function
    // that runs a pass and reports back which files the pass read. Two things
    // follow from the watch list being produced by the builds themselves. A
    // rebuild that follows an import into a directory nobody had looked at
    // before starts watching that directory, because the new pass returned it;
    // and a file that leaves the graph stops being watched, because the pass
    // that dropped it no longer returned it.
    //
    // The delay is the quiet period between noticing a change and rebuilding,
    // taken from --watch-delay. It exists for editors, which commonly write a
    // file three times in a moment: save, format, save again. Without a delay
    // that is three builds of which two are wasted; the default of 0 means no
    // delay, so a rebuild starts the instant a change is seen, and the flag is
    // there for a project that would rather trade a moment of staleness for
    // fewer passes.
    //
    // Watch() also switches the session into watch mode, which is what makes
    // later passes collect the data they hand back. It is why the call has to
    // come before the first build rather than after it.
    //
    // Input:  a valid context and a --watch-delay of 250
    // Output: a running watcher thread. It prints "[watch] build started
    //         (change: \"src/index.js\")" and "[watch] build finished" at log
    //         level info, rebuilds the graph, writes the output, and returns
    //         the new watch list. It repeats until the process ends.
    //
    // The promise below is what keeps the process alive. Its future is never
    // satisfied — nothing in this file holds the promise's shared state, and
    // there is no code path that sets it — so the thread that runs it waits
    // here for the lifetime of the session, holding the context alive and
    // keeping the process from returning from main while the watcher thread is
    // still working. The watcher's thread is detached in the sense that
    // nothing joins it: it is the watcher that ends the process, by being
    // ended with it.
    //
    // The kSuccess below is therefore the value this command would report if
    // the session were ever ended from the inside. Nothing else in this
    // function returns, which is also why there is no build summary at the
    // end of a watch run: a summary describes one finished build, and a watch
    // session is a run of them that has not stopped.

    ctx->Watch(api::WatchOptions{.delay = extras.watch_delay});
    std::promise<void> p;
    p.get_future().get();

    return static_cast<int>(ExitCode::kSuccess);
}

} // namespace guchho::cli
