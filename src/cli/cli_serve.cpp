#include "guchho/api.hpp"
#include "guchho/cli.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"

#include <cstdlib>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace guchho::cli {
// =============================================================================
// Command: serve
// =============================================================================
//
// The one command in the project that does no work: it starts a server over a
// directory that is already there and hands every request to it. No build runs,
// no file is read to decide what to send, and nothing a person does to a source
// file changes the output — that is what the next command down is for.
//
// It is also the shortest, and the three settings it has are written out in the
// body below rather than read from a structure's own defaults, so that the whole
// surface of the command is visible in one place: a directory to serve, a host
// only this machine can reach, and a port above the ones a system tends to have
// taken already.

int runServe(const std::vector<std::string>& args) {
    installSignalHandlers();

    auto args_copy = args;
    std::string serve_dir = "dist";
    std::string host = "localhost";
    uint16_t port = 3000;
    bool open_browser = false;

    // Parse args: skip "serve" command word. Checked rather than assumed, so
    // that the same function answers a list that never had a command word in it.
    size_t start_idx = 0;
    if (!args_copy.empty() && args_copy[0] == "serve") {
        start_idx = 1;
    }

    for (size_t i = start_idx; i < args_copy.size(); ++i) {
        const auto& arg = args_copy[i];
        if (arg == "--help" || arg == "-h") {
            printServeHelp(std::cout);
            return 0;
        }
        if (arg == "--open") {
            open_browser = true;
            continue;
        }
        if (arg.starts_with("--host=")) {
            host = arg.substr(std::string_view("--host=").size());
            continue;
        }
        if (arg.starts_with("--port=")) {
            auto value = arg.substr(std::string_view("--port=").size());
            std::optional<ErrorWithNote> port_err;
            // The shared number reader, with a hint naming what the number is
            // for. The cast that follows is not checked: a value larger than the
            // largest port wraps rather than being refused, so what reaches the
            // server is the low sixteen bits of what was typed.
            port = static_cast<uint16_t>(parseInt(value, arg, "Port must be a number.", port_err));
            if (port_err) {
                logger::PrintErrorWithNoteToStderr(args_copy, port_err->text, port_err->note);
                return static_cast<int>(ExitCode::kCLIUsageError);
            }
            continue;
        }
        if (!arg.starts_with("-")) {
            serve_dir = arg;
            continue;
        }
        // Reached only by something that looks like a flag and is not one of the
        // four above. The message quotes the argument as it was typed, and there
        // is no suggestion here: this command has no grammar behind it and no
        // build to be wrong about, so an argument it does not recognise is not
        // one of its own and is refused outright.
        logger::PrintErrorToStderr(args_copy, "Unknown option: " + arg);
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

    printBanner(std::cout);

    std::cout << "  Serving " << serve_dir << " on http://" << host << ":" << port << "\n\n";

    // Build a ServeOptions. The three settings the server is given, and the only
    // three this command fills in: everything else it understands — TLS
    // material, a fallback page, cross-origin rules, a callback per request — is
    // left at its default, because no flag here would set any of them.
    api::ServeOptions serve_opts;
    serve_opts.port = port;
    serve_opts.host = host;
    // The served directory, preferring one that is already set. Nothing sets it
    // before this line, so in practice this always assigns the argument; the
    // test is here so that a default which stopped being empty could not quietly
    // win over what the person asked for.
    serve_opts.servedir = serve_opts.servedir.empty() ? serve_dir : serve_opts.servedir;

    // Create a dummy rebuild function that returns empty. Above it, the
    // filesystem the server resolves every path it is asked for against, made
    // through the same interface as the rest of the project so that a run over
    // an in-memory tree is a run like any other. Then the callback itself, which
    // is the whole difference between this command and the one below: a server
    // over files that are not going to change has nothing to rebuild, so the
    // answer is an empty result. The callback cannot be left out of the call
    // itself, which is why a command that never rebuilds still supplies one.
    filesystem::RealFsOptions fs_opts;
    std::string fs_err;
    auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
    if (!fs) {
        logger::PrintErrorToStderr(args_copy, "Failed to initialize filesystem: " + fs_err);
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    api::RebuildFn rebuild = []() -> api::BuildResult {
        api::BuildResult r;
        return r;
    };

    // The server, which binds its socket before it returns and answers requests
    // on background threads from then on. Failures arrive as a message rather
    // than as an exception: a port already in use is an ordinary thing for a
    // person to hit, not a programming error. The address printed above is the
    // one that was asked for, and it was printed before this call, so a request
    // for a port of zero is announced as zero whatever it ends up bound to.
    std::string error;
    auto serve_result = api::Serve(*fs, rebuild, serve_opts,
                                    logger::LogLevel::kInfo, logger::UseColor::kColorIfTerminal,
                                    error);

    if (!error.empty()) {
        logger::PrintErrorToStderr(args_copy, "Server error: " + error);
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    // The browser, opened only once the socket is accepting connections, so the
    // window it opens cannot arrive before the page it is asking for. The command
    // is the one the platform provides, and the address is built from the two
    // values the person typed and handed to the shell as a single string.
    if (open_browser) {
#ifdef _WIN32
        std::string cmd = "start http://" + host + ":" + std::to_string(port);
        std::system(cmd.c_str());
#elif __APPLE__
        std::string cmd = "open http://" + host + ":" + std::to_string(port);
        std::system(cmd.c_str());
#else
        std::string cmd = "xdg-open http://" + host + ":" + std::to_string(port);
        std::system(cmd.c_str());
#endif
    }

    // Wait for Ctrl+C. The promise is local and nothing ever fulfils it, so this
    // line parks the calling thread for as long as the process lives and the
    // server's own threads carry on answering requests beside it. The comment
    // above says what the wait is for; the mechanism has a consequence worth
    // stating. The handler installed at the top records an interrupt in a flag
    // and returns instead of ending the process, and nothing here reads that
    // flag — so the interrupt does not stop this command, and the process has to
    // be ended from outside it.
    std::promise<void> p;
    p.get_future().get();

    // The orderly shutdown, reached only if the wait above is ever left by some
    // other route: it closes the listener and the open streams, and the socket
    // goes with them.
    serve_result.stop();
    return static_cast<int>(ExitCode::kSuccess);
}

// =============================================================================
// Command: dev
// =============================================================================
//
// A build that is also a server. It builds once so that there is something to
// answer requests from, keeps a build alive so that later builds are cheaper
// than the first one was, serves what that build wrote, and rebuilds whenever a
// file the build read is touched. Reloading the page is then enough to see an
// edit.
//
// The parts are separate on purpose and are started in the order they appear
// below. The first build is a plain build, so a source that does not compile is
// reported while the person is still watching the terminal rather than after a
// browser has been pointed at a broken page. The context comes next, because
// validating the options before a port is opened is cheaper than discovering it
// afterwards. The watcher starts last, once the server is up, so that a file
// changing during setup is picked up by the build that is about to run rather
// than by a watcher that has not started yet.

int runDev(const std::vector<std::string>& args,
                  const std::vector<api::Plugin>& plugins) {
    installSignalHandlers();

    auto args_copy = args;
    std::string host = "localhost";
    uint16_t port = 3000;
    bool open_browser = false;
    bool quiet = false;

    // Parse dev-specific options, separate from build options. Five arguments
    // are claimed below — help, "--open", "--quiet", "--host=" and "--port=" —
    // and everything else is held aside for the flag grammar further down. Not a
    // filtered copy of the arguments but a partition of them, and that is the
    // reason this command never has an unknown option of its own: a spelling it
    // does not claim is a build argument, and the grammar reports a misspelt one
    // with a suggestion for the correct spelling.
    std::vector<std::string> build_args;

    // The same skip as in the command above, and for the same reason.
    size_t start_idx = 0;
    if (!args_copy.empty() && args_copy[0] == "dev") {
        start_idx = 1;
    }

    for (size_t i = start_idx; i < args_copy.size(); ++i) {
        const auto& arg = args_copy[i];
        if (arg == "--help" || arg == "-h") {
            printDevHelp(std::cout);
            return 0;
        }
        if (arg == "--open") {
            open_browser = true;
            continue;
        }
        if (arg == "--quiet") {
            // Taken here rather than left to the grammar, because the grammar
            // fills in a build's options and this is a property of the run. It
            // reaches three things below: the level the build logs at, whether
            // the two progress lines are printed, and whether the entry point
            // guess reports what it tried.
            quiet = true;
            continue;
        }
        if (arg.starts_with("--host=")) {
            host = arg.substr(std::string_view("--host=").size());
            continue;
        }
        if (arg.starts_with("--port=")) {
            auto value = arg.substr(std::string_view("--port=").size());
            std::optional<ErrorWithNote> port_err;
            port = static_cast<uint16_t>(parseInt(value, arg, "Port must be a number.", port_err));
            if (port_err) {
                logger::PrintErrorWithNoteToStderr(args_copy, port_err->text, port_err->note);
                return static_cast<int>(ExitCode::kCLIUsageError);
            }
            continue;
        }
        // Not one of the five, so it belongs to the build: a flag, a path, an
        // option with a value. The grammar will know which, and this function
        // has no opinion.
        build_args.push_back(arg);
    }

    printBanner(std::cout);
    std::cout << "  Starting dev server on http://" << host << ":" << port << "\n\n";

    // Parse build options from remaining args. The three values set here are the
    // ones that belong to a run rather than to a build: how many messages, how
    // chatty, and whether the output is written to disk at all. A development
    // server writes, and logs at info unless it was asked to be quiet, in which
    // case the build says only what went wrong.
    auto build_opts = newBuildOptions();
    build_opts.log_limit = 6;
    build_opts.log_level = quiet ? api::LogLevel::kError : api::LogLevel::kInfo;
    build_opts.write = true;

    ParseOptionsExtras extras;
    auto err = parseOptionsImpl(build_args, &build_opts, nullptr, ParseOptionsKind::kInternal, extras);

    if (err) {
        // Printed with the build's arguments rather than the whole list, so the
        // message comes back in the context of the command that produced it.
        logger::PrintErrorWithNoteToStderr(build_args, err->text, err->note);
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

    attachPlugins(build_opts, plugins);

    // Somewhere to put the output, chosen here because a server needs one. A
    // build given an output file is left alone: naming a file is deliberate, and
    // the directory below is not served in its place.
    if (build_opts.outfile.empty() && build_opts.outdir.empty()) {
        build_opts.outdir = "dist";
    }

    // HTML-first: guess a default entry point when none is provided so that
    // "guchho dev" works right after "guchho init". A project created that way
    // keeps its document in one of three places, and a bare command with nothing
    // to build from is worth a guess when the guess has somewhere obvious to
    // look. When it fails, the message says so and lists the ways out, and the
    // exit code is a usage error — which is a statement about the code rather
    // than about the message.
    if (!applyDefaultHtmlEntry(build_opts, build_args, quiet)) {
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

    // HTML entries bundle by default. A document that refers to its own scripts
    // cannot be served as one copied file, so this default follows the entry
    // point being known rather than preceding it.
    ApplyHtmlBundleDefault(build_opts);

    // Initial build, before any port is opened. The errors below are printed and
    // then not returned, and that is deliberate: a project being developed is
    // broken most of the time it is being run, and a server that refused to start
    // because of a syntax error would have to be restarted after every
    // keystroke. The next save rebuilds, and the browser picks the new output up
    // on the next reload.
    if (!quiet) {
        std::cout << "  Building...\n";
    }
    auto build_result = api::Build(build_opts);
    if (!build_result.errors.empty()) {
        for (const auto& msg : build_result.errors) {
            logger::PrintErrorToStderr(build_args, msg.text);
        }
    }

    // Set up serve. The directory the server reads is the build's output
    // directory, but only when there is one: a build given a single output file
    // has already said where its result goes, and the server's own default is a
    // better answer than a directory the person never named.
    api::ServeOptions serve_opts;
    serve_opts.port = port;
    serve_opts.host = host;
    if (!build_opts.outdir.empty() && build_opts.outfile.empty()) {
        serve_opts.servedir = build_opts.outdir;
    }

    filesystem::RealFsOptions fs_opts;
    std::string fs_err;
    auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
    if (!fs) {
        logger::PrintErrorToStderr(build_args, "Failed to initialize filesystem: " + fs_err);
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    // Create context for rebuild. The build that stays alive, holding the
    // validated options and the caches, which is what makes the rebuilds after
    // this cheaper than the build above was. Creating it is also where the
    // options are checked, so a context that cannot be created arrives with a
    // list of its own reasons and all of them are printed.
    std::vector<api::Message> ctx_errors;
    auto ctx = api::Context(build_opts, ctx_errors);
    if (!ctx) {
        for (const auto& msg : ctx_errors) {
            logger::PrintErrorToStderr(build_args, msg.text);
        }
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    // What the server calls when a request needs output that is not there yet.
    // Captured by reference because the context outlives this call and is
    // released explicitly at the bottom of the function; a rebuild after that
    // would find an inert context and return nothing, which is why the disposal
    // is ordered before the server is stopped.
    api::RebuildFn rebuild = [&ctx]() -> api::BuildResult {
        return ctx->Rebuild();
    };

    // The standalone server rather than the context's own, because the context
    // is wanted for its rebuild and its watcher and not for the socket. The log
    // level is fixed rather than taken from the build's, so a quiet run silences
    // the build and the two progress lines but not the lines the server prints
    // about requests it is answering.
    std::string error;
    auto serve_result = api::Serve(*fs, rebuild, serve_opts,
                                    logger::LogLevel::kInfo, logger::UseColor::kColorIfTerminal,
                                    error);

    if (!error.empty()) {
        logger::PrintErrorToStderr(build_args, "Server error: " + error);
        return static_cast<int>(ExitCode::kBuildFailure);
    }

    if (!quiet) {
        std::cout << "  Dev server ready\n\n";
    }

    // The browser, opened once the server is up and answering — and unlike the
    // first build, after the output has been written, so what the window shows is
    // real rather than an empty directory.
    if (open_browser) {
#ifdef _WIN32
        std::string cmd = "start http://" + host + ":" + std::to_string(port);
        std::system(cmd.c_str());
#elif __APPLE__
        std::string cmd = "open http://" + host + ":" + std::to_string(port);
        std::system(cmd.c_str());
#else
        std::string cmd = "xdg-open http://" + host + ":" + std::to_string(port);
        std::system(cmd.c_str());
#endif
    }

    // Start watching, covering every file the build read. The delay is the one
    // the grammar read out of the build arguments a moment ago, which makes this
    // the only part of what that grammar returned that this function uses.
    ctx->Watch(api::WatchOptions{.delay = extras.watch_delay});

    // Wait for Ctrl+C, by the same mechanism as in the command above: a promise
    // that nothing fulfils, so this thread parks while the server and the
    // watcher carry on. The interrupt installed at the top is recorded in a flag
    // that nothing here reads, and so has the same consequence described there.
    std::promise<void> p;
    p.get_future().get();

    // Shutdown, in this order and for a reason. The context is disposed first so
    // that no rebuild can begin while the server is still able to ask for one;
    // the server is stopped second, which closes the listener and the streams
    // the requests were using. Either call on its own would leave something
    // running.
    ctx->Dispose();
    serve_result.stop();
    return static_cast<int>(ExitCode::kSuccess);
}

} // namespace guchho::cli
