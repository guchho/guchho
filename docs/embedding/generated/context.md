---
title: Sessions and the server
description: The long-lived objects: a build context, a running server and the watcher.
---

<!-- GENERATED from include/guchho/api.hpp and include/guchho/cli.hpp by scripts/gen-docs-embedding.mjs. Do not edit by hand. -->

# Sessions and the server

A BuildContext is a build that is still alive. Work is either synchronous or owned by an object the caller creates and destroys; nothing is started implicitly.

In this page:

- [`BuildContext`](#buildcontext)
- [`Watcher`](#watcher)
- [`CORSOptions`](#corsoptions)

## BuildContext

A live build. Create one with Context(), use it as much as you like, and
Dispose() it last.

The interface is abstract so a test can substitute a context that serves
canned results, and so the implementation details — the file system
instance, the caches, the watcher thread — can change without touching
callers.

```cpp
class BuildContext {
public:
    virtual ~BuildContext() = default;

    // Runs the build again with the options this context was created with.
    // Concurrent callers are fine: the second one waits for the build already
    // in progress and receives that result rather than starting a competing
    // build. The files are returned, not written, unless "write" was
    // requested.
    //
    // Input:  a context created with bundle = true and one entry point
    // Output: a BuildResult describing this run in full — every output file
    //         the build produced, plus any errors and warnings. Rebuilding
    //         reuses the validated options and the caches, which is what makes
    //         it cheaper than calling Build() again.
    //
    // A disposed context is inert rather than dangerous: this returns an empty
    // BuildResult instead of throwing, so a late call from another thread
    // cannot take the process down.
    virtual BuildResult Rebuild() = 0;

    // Starts watching every file the build read, plus whatever plugins asked to
    // watch, and rebuilds automatically whenever one of them changes. Watch
    // data is only collected in builds that run after this call, so enable it
    // before the first build if you want that first build to be covered.
    //
    // Input:  WatchOptions{ delay = 200 }
    // Output: nothing; changes now produce "[watch] build started"/"finished"
    //         lines on the log stream, and later Rebuild() calls return the new
    //         output.
    //
    // Throws "std::runtime_error" when the context is disposed or when watch
    // mode is already enabled.
    virtual void Watch(const WatchOptions& options) = 0;

    // Starts an HTTP server over the build's output and returns as soon as it
    // is accepting connections. Anything the server serves from the build
    // triggers a rebuild first, so editing a file and reloading the page is
    // enough to see the change. This combines with Watch(): rebuilds the
    // watcher runs on its own then reach open browsers through
    // ServeResult::notify, which the context calls for every pass that
    // finishes — that combination is what "guchho dev" is.
    //
    // Input:  ServeOptions{ servedir = "dist", port = 0 }
    // Output: ServeResult with the bound "port", the "hosts" to print, and a
    //         "stop" function that must be called to shut the server down.
    //
    // Throws "std::runtime_error" on a bad configuration (a port already in
    // use, a half-specified TLS pair, a malformed CORS origin) or on a
    // disposed context.
    virtual ServeResult Serve(const ServeOptions& options) = 0;

    // Asks the build that is running right now to stop at the next convenient
    // point, and blocks until it has actually stopped. A build that is
    // cancelled this way reports it: "errors" contains a single message saying
    // the build was cancelled and no output is produced. Calling this while
    // nothing is running is a no-op, and calling it on a disposed context is
    // also a no-op.
    virtual void Cancel() = 0;

    // Stops the watcher thread, shuts the build down and releases the caches.
    // Every other method on a disposed context either throws or does nothing,
    // so this is the last call and it must be made exactly once. Calling it
    // twice is harmless.
    virtual void Dispose() = 0;
};
```

## Watcher

Polls the file system for changes among the paths the last build reported
and triggers a rebuild whenever one is found.

The object owns a thread, so it is created, started and stopped explicitly;
nothing here begins polling on its own. A Watcher is normally owned by a
BuildContext, but it is usable on its own: give it a filesystem, a function
that performs a build and returns the new watch data, and it will drive that
function on every change.

```cpp
class Watcher {
public:
    // fs         - the filesystem to poll through, so a test can watch an
    //              in-memory tree instead of the disk.
    // rebuild    - called on the watcher thread after a change is found; it
    //              must run the build and return the paths that build read, so
    //              the next tick watches the new graph. It runs on no other
    //              thread, and must not call back into the Watcher.
    // delay_in_ms- how long to wait after noticing a change, so a burst of
    //              saves causes one rebuild. Zero means rebuild at once.
    // should_log - whether to print the "[watch]" progress lines.
    // use_color  - whether those lines may contain ANSI escapes.
    // path_style - how paths are rendered in those lines (relative or absolute).
    Watcher(
        filesystem::Fs&                                             fs,
        std::function<filesystem::WatchData()>                      rebuild,
        std::chrono::milliseconds                                   delay_in_ms,
        bool                                                        should_log,
        logger::UseColor                                            use_color,
        logger::PathStyle                                           path_style);

    // Replaces the watch list with the one the latest build produced. Call
    // this after every build, including the first: it is what makes the
    // watcher follow the graph as it changes, dropping paths that are no
    // longer part of the build and adding the ones that just became part of
    // it. Any path that was recently changed and is still present keeps its
    // place in the recent list, and the shuffled remainder is thrown away so
    // the next pass starts from the new list rather than finishing the old one.
    //
    // When logging is on, the first call also prints the "[watch] build
    // finished, watching for changes" line, which is the point at which the
    // watcher is genuinely ready to answer.
    //
    // Input:  the WatchData returned by the previous build
    // Output: nothing; the next tick polls the new set of paths.
    void SetWatchData(filesystem::WatchData data);

    // Starts the polling thread. Call it exactly once per Watcher: the thread
    // is stored by assignment, so a second Start() while the first thread is
    // still running ends the process rather than merely confusing it. A
    // Watcher cannot be restarted after Stop() either, because the stop flag
    // stays set and the new thread would exit at once.
    void Start();

    // Asks the thread to finish and waits for it. The current tick completes
    // first, so a change noticed during the final pass can still trigger one
    // last rebuild — stopping a watcher is a request to wrap up, not an
    // abort. The wait covers the settle delay and the rebuild as well, so a
    // build already under way is never abandoned half-finished. Safe to call
    // when the thread was never started, and safe to call twice.
    void Stop();

private:
    // Checks the watch list for something that changed and returns the path
    // that changed, or an empty string when nothing did. At most one path is
    // reported per call, because one rebuild answers for all of them. For a
    // file the returned path is the file itself; for a watched directory it may
    // be the directory or the child that was actually modified. The recent list
    // is checked first and the first hit ends the call, then one slice of the
    // remaining paths is taken off the shuffled scan list. Must be called with
    // "mutex_" held; refills and re-shuffles the scan list when that list has
    // been exhausted, and marks a path that changed as recently changed.
    std::string TryToFindDirtyPath();

    // The paths to poll, each paired with a closure that reports whether that
    // entry changed and, if so, which path inside it did.
    filesystem::WatchData              data_;
    filesystem::Fs&                    fs_;
    std::function<filesystem::WatchData()> rebuild_;
    std::chrono::milliseconds          delay_in_ms_;
    std::vector<std::string>           recent_items_;  // re-checked on every tick
    std::vector<std::string>           items_to_scan_; // the shuffled remainder
    std::mutex                         mutex_;
    std::size_t                        items_per_iteration_ = 0;
    std::atomic<bool>                  should_stop_{false};
    bool                               should_log_ = false;
    logger::UseColor                   use_color_{};
    logger::PathStyle                  path_style_{};

    // Thread bookkeeping: "thread_" is the polling loop, and the trio below is
    // what lets Stop() block until that loop has actually unwound rather than
    // merely having been asked to. "running_" is guarded by "stop_mutex_" and
    // cleared by the thread itself just before it returns.
    std::thread               thread_;
    std::mutex                stop_mutex_;
    std::condition_variable   stop_cv_;
    bool                      running_ = false;
};
```

## CORSOptions

Origins allowed to make cross-origin requests. An origin is a protocol, host
and port ("http://localhost:3000"); "*" matches any origin, and it may
appear at most once per entry.

```cpp
struct CORSOptions {
    std::vector<std::string> origin;
};
```
