// Watcher: the loop that turns "somebody edited a file" into "the build ran
// again". It is the whole of Guchho's watch mode, and it is deliberately small
// — a thread, a list of paths, and a clock.
//
// Why polling and not file-system notifications? Because the engine already
// knows the exact dependency set of the last build, and a poll of a known set
// is something every filesystem can answer, including the in-memory one the
// tests use. Nothing here has to be reconfigured when the graph changes: a new
// build hands over a new list of paths, and that is the entire contract
// between the bundler and this loop.
//
// The polling is spread out rather than exhaustive. Checking a few thousand
// paths every 100ms would be wasteful and would keep the disk busy for no
// reason, so paths are visited in shuffled slices: every path is looked at
// within kMaxIntervalsBeforeUpdate ticks, and the file a developer has just
// saved skips the queue entirely because it is remembered as recent. The
// constants that decide all of this — the tick length, the size of the recent
// window, the floor and ceiling on paths per tick — live next to the Watcher
// declaration in "guchho/api.hpp", so the policy is visible to whoever tunes it
// without reading this file.
//
// Threading is deliberately minimal. Exactly one thread runs the loop, and it
// is also the thread the builds happen on: the rebuild function is called from
// the loop, never the other way round, so a build and a poll can never overlap.
// The only state a second thread can reach is the watch set and the two lists
// derived from it, and each of those is behind its own mutex — so a caller that
// triggers a build by hand, from its own thread, while watching can hand over
// fresh watch data without waiting for a tick and without corrupting a scan.

#include "guchho/api.hpp"

#include <random>

#include "guchho/resolver.hpp"

namespace guchho::api {

// Stores the collaborators and nothing else. No thread exists yet and no path
// is being polled, so constructing a Watcher costs nothing and a caller can hold
// one without committing to watch mode — which is how the context uses it,
// creating the watcher inside Watch() and letting the first build that the
// watcher itself triggers deliver the first set of paths.
//
// "rebuild" is the whole interface to the outside world: call it, get back the
// paths the build it just ran depends on. Returning that data instead of
// letting the watcher ask for it is what keeps the two in step — the watcher
// can never be looking at a stale graph, because the list it polls is the list
// the newest build produced.
//
// Input:  a filesystem, a function that builds and returns WatchData, a settle
//         delay, and the three settings that decide what the progress lines
//         look like (whether to print them, whether they may contain color
//         escapes, and whether paths in them are relative or absolute).
// Output: a Watcher that has not started polling. Start() begins the loop.
Watcher::Watcher(
    filesystem::Fs&                          fs,
    std::function<filesystem::WatchData()>   rebuild,
    std::chrono::milliseconds                delay_in_ms,
    bool                                     should_log,
    logger::UseColor                         use_color,
    logger::PathStyle                        path_style)
    : fs_(fs),
      rebuild_(std::move(rebuild)),
      delay_in_ms_(delay_in_ms),
      should_log_(should_log),
      use_color_(use_color),
      path_style_(path_style) {
}

// Installs the dependency set of a build, and prints the "ready" line the
// first time it is called.
//
// This is the handoff point between the bundler and the loop. The build
// reports every file it read and every directory it scanned, along with a
// closure per path that answers "did you change, and which path inside you
// did". Replacing the whole set at once, rather than adding and removing
// individual entries, is what keeps the watcher honest across rebuilds: paths
// that the new build no longer depends on stop being polled in the same call
// that discovers the new ones, so a deleted file cannot leave a stale entry
// behind and a freshly imported file is covered from the next tick onwards.
//
// Two lists are derived rather than replaced. The shuffled scan list is
// emptied, keeping its capacity, because its contents are invalidated by
// definition and reusing the allocation avoids asking for the memory again on
// every rebuild. The recent list is filtered instead: an entry that the new
// build still depends on keeps its position, so a file being edited stays in
// the fast lane across a rebuild that had nothing to do with it. The filter
// compacts in place — survivors are moved towards the front and the tail is
// resized away — so no allocation happens here either.
//
// The "ready" line is printed only when the watcher had nothing at all before,
// which is the one moment worth announcing: it tells whoever is watching the
// terminal that the first build is done and edits will now be picked up. The
// settle delay is mentioned in that line when there is one, because a delay
// means the first edit after it takes noticeably longer to appear and that is
// worth explaining before it is observed. Colours come from a policy rather
// than a detection here, so the same string is used whether or not escapes are
// emitted; the fields are simply empty when they are not.
//
// Input:  the WatchData of a build, whose "paths" maps absolute paths to
//         change-detection closures.
// Output: nothing. After the call, "data_" is exactly that set, the scan list
//         is empty and ready to be refilled and reshuffled by the next tick,
//         and the recent list holds only the entries the new build still
//         depends on.
// Example: a build that read src/app.js, src/util.js and a directory
//          node_modules/pkg yields those three keys; a recent list of
//          [src/app.js, src/old.js] collapses to [src/app.js], because the new
//          build has no reason to watch src/old.js any more.
void Watcher::SetWatchData(filesystem::WatchData data) {
    std::lock_guard<std::mutex> lock(mutex_);

    // This is the first watch data we have been given, which means the first
    // build has just finished and polling is about to become meaningful.
    if (should_log_ && data_.paths.empty()) {
        logger::PrintTextWithColor(2, use_color_,
            [&](const logger::Colors& colors) -> std::string {
                std::string result;
                result += colors.dim;
                result += "[watch] build finished";
                if (delay_in_ms_.count() > 0) {
                    result += " with a ";
                    result += std::to_string(delay_in_ms_.count());
                    result += "ms delay";
                }
                result += ", watching for changes";
                result += colors.reset;
                result += "...\n";
                return result;
            });
    }

    data_ = std::move(data);

    // The scan list was built from the previous dependency set, so every entry
    // in it is stale. Clearing keeps the capacity for the next refill.
    items_to_scan_.clear();

    // Compact the recent list down to the entries that are still part of the
    // build, preserving their order, so nothing that is still being worked on
    // loses its place in the fast lane.
    std::size_t end = 0;
    for (const std::string& path : recent_items_) {
        if (data_.paths.find(path) != data_.paths.end()) {
            recent_items_[end] = path;
            ++end;
        }
    }
    recent_items_.resize(end);
}

// Starts the polling thread. Call it once per Watcher.
//
// The thread is a plain loop with three phases per tick: sleep, look for a
// change, and if there is one, build. Sleeping before the first look means
// Start() is cheap and immediate — it never blocks on the filesystem — and that
// a Watcher started and immediately stopped does no work at all. Nothing is
// missed by starting late, either: each path's check compares the file against
// what the last build recorded, so an edit made while the watcher was not
// running is still noticed on the first tick.
//
// The three log lines this loop can print — the ready line from SetWatchData,
// the build-started line, and the build-finished line — are part of what this
// program is: people watch the terminal rather than calling the API, and they
// match those lines with their own scripts. Rewording one is a breaking change
// to that contract, so the wording here is deliberately fixed.
//
// A few properties of the loop are worth knowing before changing it. One
// reported change is enough to justify one rebuild, because the rebuild
// recomputes the whole graph and will pick up everything else that moved at the
// same time; batching is the rebuild's job, not the watcher's. The settle delay
// sits after the change is noticed and before the build starts, so a burst of
// saves from an editor that writes in several steps costs one rebuild rather
// than one per write. And the change is logged through the same filesystem the
// build itself reads, rendered with the caller's path style, so the line names
// the file exactly as the build's own diagnostics would.
//
// Starting twice while the thread is running ends the process: the thread is
// assigned to a variable that already owns a running thread, and that is a
// fatal mistake rather than a recoverable one. Restarting after Stop() does not
// work either, because the stop flag is never cleared, so the new thread would
// exit on its first check. A Watcher is a one-use object by design; build a new
// one if a new session is wanted.
//
// Input:  nothing.
// Output: a running polling thread. Nothing is logged and no build runs until
//         the first change is noticed.
void Watcher::Start() {
    std::lock_guard<std::mutex> lock(stop_mutex_);
    running_ = true;
    thread_ = std::thread([this] {
        // These log messages are part of the interface of a running build
        // session. Keep their wording stable: a breaking version change is
        // required to alter them.

        while (!should_stop_.load(std::memory_order_relaxed)) {
            // Every tick starts by waiting, so the cost of a build that finds
            // nothing to do is one sleep and nothing else.
            std::this_thread::sleep_for(kWatchIntervalSleep);

            // Look for a change among the paths of the last build. An empty
            // result means the tick found nothing and the loop simply goes
            // around again.
            std::string abs_path = TryToFindDirtyPath();
            if (abs_path.empty()) {
                continue;
            }

            // Let a burst of writes settle before building, when a delay was
            // configured. Zero means build immediately.
            if (delay_in_ms_.count() > 0) {
                std::this_thread::sleep_for(delay_in_ms_);
            }

            if (should_log_) {
                logger::PrintTextWithColor(2, use_color_,
                    [&](const logger::Colors& colors) -> std::string {
                        logger::PrettyPaths pretty_paths = resolver::MakePrettyPaths(
                            fs_, logger::Path{.text = abs_path, .namespace_ = "file"});
                        std::string result;
                        result += colors.dim;
                        result += "[watch] build started (change: \"";
                        result += pretty_paths.Select(path_style_);
                        result += "\")";
                        result += colors.reset;
                        result += "\n";
                        return result;
                    });
            }

            // Build, then install the paths that build depends on. Doing both
            // here, in that order, is what keeps the polled set and the last
            // build in agreement.
            SetWatchData(rebuild_());

            if (should_log_) {
                logger::PrintTextWithColor(2, use_color_,
                    [&](const logger::Colors& colors) -> std::string {
                        std::string result;
                        result += colors.dim;
                        result += "[watch] build finished";
                        result += colors.reset;
                        result += "\n";
                        return result;
                    });
            }
        }

        // The loop has unwound. Clear the flag under the mutex and wake Stop(),
        // which is blocked waiting for exactly this.
        std::lock_guard<std::mutex> stop_lock(stop_mutex_);
        running_ = false;
        stop_cv_.notify_all();
    });
}

// Asks the loop to finish and waits until it has.
//
// The request is cooperative: the flag is only tested at the top of the loop,
// so the tick in progress finishes. A change noticed during that final pass
// still produces a rebuild, and a settle delay that has already begun is waited
// out rather than cut short. Stop() therefore means "wrap up", not "abandon
// what you are doing" — which is the right trade for a build, since a build
// that is interrupted half-way leaves the output directory in a state nobody
// asked for. The cost of that choice is that Stop() can block for as long as one
// build plus one delay, and there is no way to interrupt a build in progress.
//
// The flag itself needs no ordering guarantees; it only ever says "stop
// eventually", and the mutex plus condition variable carry the part that has to
// be exact. Waiting on the flag being cleared rather than joining directly is
// what makes the call safe when the thread was never started: "running_" is
// already false in that case, so the wait returns at once instead of blocking
// forever. Joining afterwards reaps the thread, and a second Stop() is a no-op
// because a joined thread is no longer joinable.
//
// Input:  nothing.
// Output: nothing. When the call returns, no build is running, the thread has
//         been reaped, and the Watcher can be destroyed safely.
void Watcher::Stop() {
    should_stop_.store(true, std::memory_order_relaxed);

    std::unique_lock<std::mutex> lock(stop_mutex_);
    stop_cv_.wait(lock, [this] { return !running_; });
    lock.unlock();

    if (thread_.joinable()) {
        thread_.join();
    }
}

// Asks every path of the current watch set whether it changed, and returns the
// path that changed first. An empty string means nothing changed, which is the
// common case, and the tick then costs one sleep and nothing else.
//
// The set of candidates is the recent list followed by a slice of the shuffled
// scan list, and that is the whole scheduling idea. Recent entries are the file
// a developer has just saved, which is overwhelmingly the file they are about
// to save again, so those are checked on every tick no matter where they would
// otherwise fall in the rotation. Everything else is visited in a random order,
// a slice at a time, so that the cost of a tick is bounded and no file can be
// starved by a long run of unchanged neighbours.
//
// The refill is where the two policy constants do their work. When the scan
// list runs dry it is rebuilt from the keys of the watch set and shuffled, and
// the size of a slice is then chosen so that the whole list is covered in
// about kMaxIntervalsBeforeUpdate ticks — the list is divided into that many
// slices, with a floor of kMinItemCountPerIter so that a small project is
// checked in full on the first tick rather than spread across twenty. The
// shuffle is seeded per thread from the system entropy source, so two runs of
// the same build do not check their files in the same order; a fixed order
// would make a pathological project pay the latency of the files at the end of
// the list over and over.
//
// Once a change is found the call ends, and the two lists remember it in
// different ways. A hit in the recent list is moved to the back of that list,
// which is the most recently touched position, and the rest keep their order.
// A hit in the scan list is added to the back of the recent list, and if that
// list is already full the oldest entry is dropped from the front, so a file
// that stops being edited quietly returns to the rotation. The path that is
// returned is the one the change-detection closure reported, which for a
// watched directory may be a child of that directory rather than the directory
// itself, and it is the path a human should see in a log line.
//
// Note that everything happens under "mutex_", including the change-detection
// calls themselves, which touch the filesystem. That is deliberate: it is what
// makes the two lists and the watch set consistent with each other, and the
// bounded work per tick is what keeps the lock from being held for long.
//
// Input:  nothing; the candidates come from the installed watch data.
// Output: the absolute path of something that changed, or "" when the tick
//         found nothing.
// Example: a watch set of 2000 paths is divided into slices of 100, so a tick
//          checks 100 paths and the whole set is covered in 20 ticks — about
//          two seconds at the 100ms tick. A watch set of 5 paths hits the floor
//          instead, so all 5 are checked on the first tick. Either way a change
//          returns the path that changed, e.g. "/project/src/app.js".
std::string Watcher::TryToFindDirtyPath() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Out of candidates: take the whole watch set again, in random order, and
    // decide how much of it one tick may look at.
    if (items_to_scan_.empty()) {
        items_to_scan_.clear();
        items_to_scan_.reserve(data_.paths.size());
        for (const auto& entry : data_.paths) {
            items_to_scan_.push_back(entry.first);
        }

        // Shuffle in place, from the back, so the result covers the whole
        // vector rather than a prefix of it.
        static thread_local std::mt19937 rng{std::random_device{}()};
        for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(items_to_scan_.size()) - 1; i > 0; --i) {
            std::uniform_int_distribution<std::ptrdiff_t> dist(0, i);
            std::ptrdiff_t j = dist(rng);
            std::swap(items_to_scan_[
                          static_cast<std::size_t>(i)],
                      items_to_scan_[static_cast<std::size_t>(j)]);
        }

        // Spread the list over about kMaxIntervalsBeforeUpdate ticks, rounding
        // up so a list that does not divide evenly still gets fully covered,
        // and never checking fewer than kMinItemCountPerIter per tick, so a
        // small project is checked in full on the very first tick.
        std::size_t per_iter =
            (items_to_scan_.size() + kMaxIntervalsBeforeUpdate - 1) / kMaxIntervalsBeforeUpdate;
        if (per_iter < kMinItemCountPerIter) {
            per_iter = kMinItemCountPerIter;
        }
        items_per_iteration_ = per_iter;
    }

    // The fast lane: every recently-changed path, every tick, first hit wins.
    for (std::size_t i = 0; i < recent_items_.size(); ++i) {
        const std::string& path = recent_items_[i];
        auto it = data_.paths.find(path);
        if (it != data_.paths.end()) {
            std::string dirty_path = it->second();
            if (!dirty_path.empty()) {
                // Promote this path to the most recently touched position by
                // closing the gap, which keeps the order of the others.
                std::string moved = recent_items_[i];
                for (std::size_t k = i + 1; k < recent_items_.size(); ++k) {
                    recent_items_[k - 1] = recent_items_[k];
                }
                recent_items_.back() = moved;
                return dirty_path;
            }
        }
    }

    // The rotation: take one slice off the end of the scan list and leave the
    // rest for later ticks. Taking from the end is what makes the list a queue.
    std::size_t remaining_count = items_to_scan_.size() > items_per_iteration_
        ? items_to_scan_.size() - items_per_iteration_ : 0;
    std::vector<std::string> to_check{
        items_to_scan_.begin() + static_cast<std::ptrdiff_t>(remaining_count),
        items_to_scan_.end()};
    items_to_scan_.resize(remaining_count);

    // Ask the slice whether any of it moved. The first change ends the tick and
    // is reported to the caller, which turns it into a rebuild.
    for (const std::string& path : to_check) {
        auto it = data_.paths.find(path);
        if (it != data_.paths.end()) {
            std::string dirty_path = it->second();
            if (!dirty_path.empty()) {
                // Remember this path, so the next few ticks check it first.
                recent_items_.push_back(path);
                if (recent_items_.size() > kMaxRecentItemCount) {
                    // The window is full: drop the oldest entry, closing the
                    // gap so the rest keep their order.
                    for (std::size_t k = 1; k < recent_items_.size(); ++k) {
                        recent_items_[k - 1] = recent_items_[k];
                    }
                    recent_items_.resize(kMaxRecentItemCount);
                }
                return dirty_path;
            }
        }
    }
    return "";
}

} // namespace guchho::api
