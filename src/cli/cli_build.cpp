// =============================================================================
// The commands that produce output
//
// Two of Guchho's commands do the work that the rest of the program exists for:
// "build" turns entry points into files on disk, and "transform" turns one
// string of source code into another without going near the file system. This
// file is both of them, plus the one helper they share for writing a file that
// the run owns rather than the build.
//
// Each command takes the arguments exactly as they arrived after the executable
// name, drops its own command word if it is there, answers --help by itself, and
// returns one of the ExitCode values. None of them throws, and none of them
// reads a flag the grammar has not already read: by the time the work starts,
// the options are a filled in api::BuildOptions or api::TransformOptions.
//
// The shape of a command is the same throughout, and it is worth stating once
// here because it is what makes the files below read alike:
//
//   1. Take the arguments, drop the command word, and answer --help before
//      looking at anything else, so a person asking for help never waits on a
//      build.
//   2. Read every flag through the shared grammar. A rejected flag ends the run
//      with a usage error and an exit code that says the command line was the
//      problem, never the build.
//   3. Settle the things the grammar cannot know: where text arriving on the
//      standard input should be resolved from, where the run's own output files
//      belong, and what the mangle cache has to remember.
//   4. Do the work, and only then write the files the run was asked for.
//   5. Report the outcome through an exit code, so a shell script can branch on
//      it without reading the output.
//
// The two commands differ in what they do with what they read. A build owns its
// output: it writes files, and it carries decisions forward between runs in the
// mangle cache. A transform owns nothing — it reads one string and writes one
// string, and the only thing it leaves behind is what appears on the standard
// output.
// =============================================================================

#include "guchho/api.hpp"
#include "guchho/cli.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace guchho::cli {
// ============================================================================
// Writing a file the run owns
// ============================================================================
//
// A build produces its own output through the engine, which knows how to write
// a bundle atomically and how to fail loudly. The two files a run keeps beside
// that output — the metafile and the mangle cache — are written by the run
// itself, after the build has finished, and this is the single place it does it.

    // Writes content to path, reporting a failure to the error stream instead of
    // throwing.
    //
    // The open is bracketed by the same semaphore the bundler uses to cap how
    // many files it holds open at once while it builds its graph, so a run that
    // writes these two files still respects that cap. The slot is given back as
    // soon as the stream is open rather than when it is closed, which is safe
    // here because this is a single short write at the very end of a run and
    // not a loop.
    //
    // Failing to write is reported, not fatal. A run that produced a correct
    // bundle and could not save the size report is still a run that produced a
    // correct bundle, and the person reading the error stream is the only one
    // who can decide whether the missing file matters.
    //
    // Input:  path = "dist/meta.json", content = the metafile text, and the
    //         arguments the run was given so the message matches the rest of it.
    // Output: the file on disk, and nothing printed.
    //
    // Input:  a path in a directory that does not exist.
    // Output: nothing on disk, and "Failed to write to output file: dist/meta.json"
    //         on the error stream.
void writeFileContent(const std::string& path, const std::string& content,
                              const std::vector<std::string>& os_args) {
    filesystem::BeforeFileOpen();
    std::ofstream ofs(path, std::ios::binary);
    filesystem::AfterFileClose();
    if (ofs.is_open()) {
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    } else {
        logger::PrintErrorToStderr(os_args,
            "Failed to write to output file: " + path);
    }
}

// ============================================================================
// Command: build
// ============================================================================
//
// The command a bare path lands on, and the one that does the most: it reads
// flags, settles where the run's own files belong, carries the mangle cache in
// and back out, and hands the finished options to the engine.
//
// What it deliberately does not do is decide anything the grammar already knows
// or fix anything it cannot know. A missing output path, an unreadable cache
// and a build that failed are each reported once, in the words of the layer that
// noticed them, and each one becomes an exit code rather than an exception
// unwinding through the command.

    // Runs a build and returns the code the process should exit with.
    //
    // The arguments are the ones that followed the executable name, with or
    // without the "build" word in front, and "plugins" are the ones a host
    // attached through RunWithPlugins(); the build the engine receives has them
    // attached already, so nothing here has to know whether this run came from a
    // terminal or from another program.
    //
    // Two things are asked for here and nowhere else in the command line, which
    // is why they are worth naming. A build may arrive without any entry point
    // at all, and the run then reads its source from the standard input instead
    // — the shape an editor integration uses to bundle a buffer that has never
    // been saved. And a build may be asked to remember the property names it
    // shortened, which turns a single run into a series of runs that agree with
    // each other, because the file beside the output records what was decided
    // last time.
    //
    // Input:  args = { "build", "src/index.js", "--bundle", "--outdir=dist" }
    // Output: the bundle written under dist, a size summary on the standard
    //         output, and 0.
    //
    // Input:  args = { "build", "src/index.js", "--outdir=dist" } with an import
    //         in that file that cannot be resolved.
    // Output: the "Could not resolve" error and its notes on the error stream,
    //         no output files written, and 1 — the command line was fine, so the
    //         code is a build failure rather than a usage error.
    //
    // Input:  args = { "build", "--outfil=out.js" }
    // Output: the complaint on the error stream with the correct spelling as its
    //         note, nothing read from the file system, and 2.
    //
    // Input:  args = { "build" } with nothing else.
    // Output: "No entry points specified" on the error stream, no build, and 2.
    //
    // Input:  args = { "build", "--help" }
    // Output: the build help on the standard output, nothing built, and 0 — help
    //         is an answer, not a failure, so a script that asks for it is not
    //         sent down an error path.
int runBuild(const std::vector<std::string>& args, bool quiet,
                    const std::vector<api::Plugin>& plugins) {
        // A build is long enough to be interrupted, and being interrupted in the
        // middle of writing is how an output file ends up truncated. The
        // handler only records that an interrupt happened; the commands that
        // keep running poll it and stop at a point of their own choosing.
        installSignalHandlers();

        // The grammar below reads flags, not command words, so the word goes
        // first. It is optional because a bare path is a build too, which is
        // decided before this function is called.
        auto args_copy = args;
        if (!args_copy.empty() && args_copy[0] == "build") {
            args_copy.erase(args_copy.begin());
        }

        // Help is answered before the flags are read, so that a command line
        // which asks for help and also contains a mistake still gets help.
        for (const auto& arg : args_copy) {
            if (arg == "--help" || arg == "-h") {
                printBuildHelp(std::cout);
                return 0;
            }
        }

        // The size reporting flags are taken out of the argument list here and
        // become a plugin instead, so the grammar never has to know they exist
        // and the build stays the same build whichever way they were asked for.
        auto analyze = filterAnalyzeFlags(args_copy);

        // Reading the flags fills in either a build or a transform, whichever
        // this run turned out to be, plus the settings that describe the run
        // rather than the build. At most one of the two options is set, and both
        // are returned by value so this holds on to no frame that is gone.
        auto [build_ptr, transform_ptr, build_opts, transform_opts, extras, err] =
            parseOptionsForRun(args_copy, plugins);

        if (err) {
            logger::PrintErrorWithNoteToStderr(args_copy, err->text, err->note);
            return static_cast<int>(ExitCode::kCLIUsageError);
        }

        // There is a build to run when something was named to build from, or
        // when bundling or writing was asked for on its own — the last of which
        // is how a build with no entry point at all still gets one, by way of
        // the standard input. With none of those, there is nothing to do and
        // the message below says so.
        if (!build_opts.entry_points.empty() || !build_opts.entry_points_advanced.empty() ||
            build_opts.bundle || build_opts.write) {

            if (analyze != AnalyzeMode::kDisabled) {
                addAnalyzePlugin(build_opts);
            }

            // "NODE_PATH" adds directories to search for packages, and it is
            // spelled with the separator of the host: a semicolon on Windows and
            // a colon everywhere else, which is the one place the path syntax of
            // the two platforms leaks into a value rather than into how Guchho
            // writes its own paths. An empty element is refused rather than
            // dropped, because dropping one would quietly search a different
            // directory than the person setting the variable meant.
            if (const char* node_path = std::getenv("NODE_PATH")) {
                std::string path_str(node_path);
                char separator = filesystem::CheckIfWindows() ? ';' : ':';
                build_opts.node_paths = splitWithEmptyCheck(path_str, separator);
            }

            // No entry point means the source is arriving on the standard input.
            // It is read here, in full, and handed over as one virtual file: the
            // graph then sees the text as it is, unsaved, and the build is
            // otherwise an ordinary build.
            if (build_opts.entry_points.empty() && build_opts.entry_points_advanced.empty()) {
                if (!build_opts.stdin_data.has_value()) {
                    build_opts.stdin_data = api::StdinOptions{};
                }
                std::string contents((std::istreambuf_iterator<char>(std::cin)),
                                      std::istreambuf_iterator<char>());
                // A stream that neither read nor hit its end failed for some
                // other reason, and an empty build would hide it. A stream that
                // simply ended is an empty input, which is a legitimate thing to
                // ask to build.
                if (std::cin.fail() && !std::cin.eof()) {
                    logger::PrintErrorToStderr(args_copy, "Could not read from stdin");
                    return static_cast<int>(ExitCode::kBuildFailure);
                }
                build_opts.stdin_data->contents = std::move(contents);
                // Relative imports in text that has no file of its own are
                // resolved against the working directory, which is the only
                // place such a build can be said to be happening.
                filesystem::RealFsOptions fs_opts;
                std::string fs_err;
                auto fs = filesystem::MakeRealFS(fs_opts, fs_err);
                if (fs) {
                    build_opts.stdin_data->resolve_dir = fs->Cwd();
                }
            } else if (build_opts.stdin_data.has_value()) {
                // Two ways of naming the same thing at once. Each is named
                // specifically, because "that option does not apply here" on its
                // own leaves the reader to work out which of their flags it was.
                if (!build_opts.stdin_data->sourcefile.empty()) {
                    logger::PrintErrorToStderr(args_copy, "\"sourcefile\" only applies when reading from stdin");
                } else {
                    logger::PrintErrorToStderr(args_copy, "\"loader\" without extension only applies when reading from stdin");
                }
                return static_cast<int>(ExitCode::kCLIUsageError);
            }

            // A document entry point cannot be served as one copied file,
            // because it refers to the scripts and styles it needs, so bundling
            // is switched on for it rather than refused.
            ApplyHtmlBundleDefault(build_opts);

            // The metafile is the report of what the build read and produced, and
            // it is written by the run rather than by the engine. Two things are
            // settled here: that there is somewhere to put it, and where that is
            // in absolute terms, so the write after the build does not have to
            // care what the working directory is by then.
            std::string metafile_abs_path;
            std::string metafile_abs_dir;
            if (extras.metafile) {
                if (build_opts.outfile.empty() && build_opts.outdir.empty()) {
                    logger::PrintErrorToStderr(args_copy, "Cannot use \"metafile\" without an output path");
                    return static_cast<int>(ExitCode::kCLIUsageError);
                }
                filesystem::RealFsOptions fs_opts;
                std::string fs_err;
                auto real_fs = filesystem::MakeRealFS(fs_opts, fs_err);
                if (real_fs) {
                    auto abs = real_fs->Abs(*extras.metafile);
                    if (!abs) {
                        logger::PrintErrorToStderr(args_copy, "Invalid metafile path: " + *extras.metafile);
                        return static_cast<int>(ExitCode::kCLIUsageError);
                    }
                    metafile_abs_path = *abs;
                    metafile_abs_dir = real_fs->Dir(metafile_abs_path);
                }
            }

            // The mangle cache is read before the build so that a property
            // shortened by an earlier run keeps the name it was given, and is
            // written after it so the next run can do the same. It is a file the
            // run owns, needs an output path to sit beside, and is settled here
            // in the same two steps as the metafile: a place, and an absolute
            // path to it.
            std::string mangle_abs_path;
            std::string mangle_abs_dir;
            std::vector<std::string> mangle_cache_order;
            if (extras.mangle_cache) {
                if (build_opts.outfile.empty() && build_opts.outdir.empty()) {
                    logger::PrintErrorToStderr(args_copy, "Cannot use \"mangle-cache\" without an output path");
                    return static_cast<int>(ExitCode::kCLIUsageError);
                }
                filesystem::RealFsOptions fs_opts;
                std::string fs_err;
                auto real_fs = filesystem::MakeRealFS(fs_opts, fs_err);
                if (real_fs) {
                    auto abs = real_fs->Abs(*extras.mangle_cache);
                    if (!abs) {
                        logger::PrintErrorToStderr(args_copy, "Invalid mangle cache path: " + *extras.mangle_cache);
                        return static_cast<int>(ExitCode::kCLIUsageError);
                    }
                    mangle_abs_path = *abs;
                    mangle_abs_dir = real_fs->Dir(mangle_abs_path);

                    // The reader reports anything wrong with the file itself and
                    // hands back nothing in that case.
                    auto result = parseMangleCache(args_copy, *real_fs, mangle_abs_path);
                    if (result.cache.empty() && !result.order.empty()) {
                        return static_cast<int>(ExitCode::kBuildFailure);
                    }
                    // What the build needs from the file is which names it must
                    // not shorten again, so the recorded value reduces to a
                    // decision; the order the names were written in is kept for
                    // the file this run will write back.
                    for (const auto& [k, v] : result.cache) {
                        build_opts.mangle_cache[k] = v.has_value();
                    }
                    mangle_cache_order = std::move(result.order);
                }
            }

            // Watching replaces the build rather than preceding it: a watch builds
            // repeatedly, so the build itself is left to the context and this
            // function waits forever. The promise that is never satisfied is the
            // wait, and the process stays alive because of it.
            if (extras.watch) {
                std::vector<api::Message> ctx_errors;
                auto ctx = api::Context(build_opts, ctx_errors);
                if (!ctx) {
                    for (const auto& msg : ctx_errors) {
                        logger::PrintErrorToStderr(args_copy, msg.text);
                    }
                    return static_cast<int>(ExitCode::kBuildFailure);
                }
                ctx->Watch(api::WatchOptions{.delay = extras.watch_delay});
                std::promise<void> p;
                p.get_future().get();
            }

            // The build itself, and the clock around it. A steady clock is the
            // right one here: a build is measured in seconds and wall clock time
            // is the thing the summary is reporting, so a clock that the system
            // adjusting mid-build would make would overstate the run.
            auto start = std::chrono::steady_clock::now();
            auto result = api::Build(build_opts);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            // The run's own files are written after the build and before the
            // errors are judged, so that a build which succeeded has both of its
            // outputs whether or not it also had something to complain about. The
            // directory is created here because the output path is a name to be
            // honoured, not a directory the person is expected to have made.
            if (!metafile_abs_path.empty() && !result.metafile.empty()) {
                std::string mkdir_err;
                filesystem::MkdirAll(*filesystem::MakeRealFS({}, mkdir_err), metafile_abs_dir, mkdir_err);
                writeFileContent(metafile_abs_path, result.metafile, args_copy);
            }

            // The cache the build came back with is turned into the file's two
            // value forms: a name that took part in shortening is recorded as
            // present, and one that did not is recorded as false, so the file
            // keeps the distinction instead of quietly forgetting a name and
            // shortening it differently next time. The order the file was read in
            // is handed over as well, which is what keeps a build that decided
            // nothing new from rewriting the file in a different order.
            if (!mangle_abs_path.empty() && !result.mangle_cache.empty()) {
                std::string mkdir_err;
                filesystem::MkdirAll(*filesystem::MakeRealFS({}, mkdir_err), mangle_abs_dir, mkdir_err);
                std::unordered_map<std::string, std::optional<std::string>> cache;
                for (const auto& [k, v] : result.mangle_cache) {
                    cache[k] = v ? std::optional<std::string>{""} : std::nullopt;
                }
                auto bytes = printMangleCache(cache, mangle_cache_order,
                                             build_opts.charset == api::Charset::kASCII);
                writeFileContent(mangle_abs_path, bytes, args_copy);
            }

            // A build with errors is a failed build, and the code says so. This
            // is the one answer a caller needs most and the one that cannot be
            // inferred from the output: the two failures a command can have are
            // told apart by the code, and both write to the error stream.
            if (!result.errors.empty()) {
                return static_cast<int>(ExitCode::kBuildFailure);
            }

            // The summary is a report of what was produced, so it is printed only
            // when there is something to report and the person asked to hear it.
            if (!quiet) {
                std::cout << "\n";
                printBuildSummary(result, static_cast<double>(elapsed), quiet);
            }

            return static_cast<int>(ExitCode::kSuccess);
        }

        logger::PrintErrorToStderr(args_copy, "No entry points specified");
        return static_cast<int>(ExitCode::kCLIUsageError);
    }

// ============================================================================
// Command: transform
// ============================================================================
//
// The smallest useful thing Guchho does, and the one most often reached for by
// something that is not a build at all: read one piece of source code, hand it
// to the same parser and printer the bundler uses, and write the result out. No
// entry points, no output files, no graph, no watching.
//
// It is a filter. The source arrives on the standard input and the result leaves
// on the standard output, so it drops into a pipeline between two other tools
// without either of them knowing it is there.

    // Transforms one string read from the standard input and writes the result
    // to the standard output.
    //
    // The grammar is the build grammar minus everything that describes a build
    // rather than a single file, and the same reading function fills it in — one
    // grammar for both, so a flag cannot mean one thing here and another there.
    // Two defaults are set before it runs: the log level, so a transform does
    // not narrate a routine success, and the log limit, so a file with many
    // problems reports a few of them and says how many it did not.
    //
    // Nothing is written to disk and nothing is printed but the code, which is
    // what makes the output safe to pipe: the bytes on the standard output are
    // the transformed source and nothing else, with every diagnostic on the
    // error stream instead.
    //
    // Input:  args = { "transform", "--minify" } with "const f = (x) => x * 2;"
    //         on the standard input.
    // Output: the minified source on the standard output, nothing on the error
    //         stream, and 0.
    //
    // Input:  args = { "transform" } with a syntax error on the standard input.
    // Output: the diagnostic on the error stream, including the line and column
    //         within the text that was read, nothing on the standard output, and
    //         1.
    //
    // Input:  args = { "transform", "--help" }
    // Output: the transform usage on the standard output and 0. The help is
    //         printed without the banner, because the output of this command is
    //         meant to be a program and a banner in front of it would corrupt
    //         whatever the pipeline asked for.
int runTransform(const std::vector<std::string>& args) {
        // The grammar reads flags, so the command word goes first. It is
        // optional for the same reason it is optional for a build.
        auto args_copy = args;
        if (!args_copy.empty() && args_copy[0] == "transform") {
            args_copy.erase(args_copy.begin());
        }

        // Answered before the flags are read, so help is available even for a
        // command line that is otherwise wrong. The usage is short on purpose
        // and points at the build help for the flags themselves, which are the
        // same flags.
        for (const auto& arg : args_copy) {
            if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: guchho transform <source.js> [options]\n"
                          << "\n"
                          << "Transform source code from stdin.\n"
                          << "\n"
                          << "Options are the same as 'guchho build'.\n";
                return 0;
            }
        }

        // The defaults are the ones a person running a transform wants: told
        // what happened, told a few times, and not told about it repeatedly.
        ParseOptionsExtras extras;
        auto transform_opts = newTransformOptions();
        transform_opts.log_limit = 6;
        transform_opts.log_level = api::LogLevel::kInfo;
        auto err = parseOptionsImpl(args_copy, nullptr, &transform_opts, ParseOptionsKind::kInternal, extras);

        if (err) {
            logger::PrintErrorWithNoteToStderr(args_copy, err->text, err->note);
            return static_cast<int>(ExitCode::kCLIUsageError);
        }

        // The input is read in full, because a transform is one piece of source
        // code rather than a graph of them, and there is no path to resolve it
        // against or loader to infer from one.
        std::string input((std::istreambuf_iterator<char>(std::cin)),
                           std::istreambuf_iterator<char>());
        if (std::cin.fail() && !std::cin.eof()) {
            logger::PrintErrorToStderr(args_copy, "Could not read from stdin");
            return static_cast<int>(ExitCode::kBuildFailure);
        }

        auto result = api::Transform(input, transform_opts);
        if (!result.errors.empty()) {
            return static_cast<int>(ExitCode::kBuildFailure);
        }

        // The bytes are written as they are, without a newline of their own, so
        // that what comes out is exactly the code the engine produced.
        std::cout.write(reinterpret_cast<const char*>(result.code.data()),
                       static_cast<std::streamsize>(result.code.size()));
        return static_cast<int>(ExitCode::kSuccess);
    }

} // namespace guchho::cli
