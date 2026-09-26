// Unit tests for the Watcher defined in src/api/api_watcher.cpp: the polling
// loop that turns "the previous build's dependency set reports a change" into
// "the build ran again, and here is the new dependency set".
//
// The loop is driven entirely from memory. Its change-detection closures are
// supplied by whoever calls SetWatchData, so a test supplies its own, and the
// filesystem is a mock the watcher never really reads: with logging switched off
// the filesystem is only used to name a path in a progress line, so nothing here
// touches the real disk. A "file changed" is therefore not a disk write but a
// probe being told to report itself a set number of times, which is what keeps
// every test's timing a matter of the 100ms tick rather than of the machine's
// filesystem.
//
// The claims that need care are the negative ones: "no rebuild happened" and
// "this path is no longer polled" can only be asserted over a window of time,
// so each of those tests first waits for the loop to demonstrably be running and
// then checks that nothing further occurred. Every bound below is generous
// enough that a loaded machine cannot turn a passing build into a failing one,
// and no test depends on two particular ticks landing in a particular order.

#include "test/helpers/filesystem_test.hpp"

#include "test/guchho_test.hpp"
#include "guchho/api.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace api = guchho::api;
namespace filesystem = guchho::filesystem;
namespace logger = guchho::logger;

namespace {

using namespace std::chrono_literals;

// One entry of a watch set, standing in for what a build reports about a file or
// a directory. The probe counts how often the loop asked it, and owes a hit —
// meaning "this path changed" — a set number of times: a probe armed once stands
// for a file that was saved once, which is the case that matters, because the
// rebuild that follows computes the whole graph again and there is nothing left
// to report the second time.
//
// Every method here is called from the watcher's thread, so the counters are
// atomic and the only other writer is the test arming a probe from outside.
struct Probe {
    std::string       path;
    std::atomic<int>  calls{0};
    std::atomic<int>  hits_left{0};
    std::atomic<bool> always_dirty{false};

    // Arms the probe to report "changed" the next "times" times it is asked.
    // Arming from the test thread can race a report in progress, which is why
    // Report spends its hits with a compare-exchange rather than a decrement.
    void Arm(int times = 1) {
        hits_left.store(times, std::memory_order_relaxed);
    }

    // Marks the probe as one that never settles, standing in for a file that is
    // being written to continuously.
    void SetAlwaysDirty() {
        always_dirty.store(true, std::memory_order_relaxed);
    }

    // Records the call and reports the path when this probe owes a hit. An empty
    // string is the "nothing changed" answer the loop is looking for.
    std::string Report() {
        calls.fetch_add(1, std::memory_order_relaxed);
        if (always_dirty.load(std::memory_order_relaxed)) {
            return path;
        }
        int left = hits_left.load(std::memory_order_relaxed);
        while (left > 0) {
            if (hits_left.compare_exchange_weak(left, left - 1, std::memory_order_relaxed)) {
                return path;
            }
        }
        return "";
    }
};

// Builds a watch set out of probes. The set is returned by value, so the same
// probes can be installed again later, which is what the tests that watch a path
// survive a rebuild do: the closure captured for a path is the same closure, and
// the loop recognises the path by its key.
filesystem::WatchData MakeWatchData(const std::vector<std::shared_ptr<Probe>>& probes) {
    filesystem::WatchData data;
    for (const std::shared_ptr<Probe>& probe : probes) {
        std::shared_ptr<Probe> held = probe;
        data.paths.emplace(probe->path, [held] { return held->Report(); });
    }
    return data;
}

// Creates a probe for "path". A name is all a probe needs: the loop never looks
// inside one, it only calls it.
std::shared_ptr<Probe> MakeProbe(std::string path) {
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
    probe->path = std::move(path);
    return probe;
}

// Creates "count" probes named /src/file0.js, /src/file1.js and so on. Used by
// the tests that need a watch set big enough for the rotation to matter.
std::vector<std::shared_ptr<Probe>> MakeProbeSet(int count) {
    std::vector<std::shared_ptr<Probe>> probes;
    probes.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        probes.push_back(MakeProbe("/src/file" + std::to_string(i) + ".js"));
    }
    return probes;
}

// Sums how often the loop has asked any of "probes". A test comparing this
// against a bound is measuring how much of the watch set a single tick covers.
int TotalCalls(const std::vector<std::shared_ptr<Probe>>& probes) {
    int total = 0;
    for (const std::shared_ptr<Probe>& probe : probes) {
        total += probe->calls.load(std::memory_order_relaxed);
    }
    return total;
}

// Polls "done" until it reports true or "timeout" has passed, and returns what it
// reported. The wait itself never decides a test: the caller asserts on the
// result, so a test that fails says which condition was not met.
bool WaitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return done();
}

// The rebuild function handed to a Watcher, together with the record of what it
// did. The loop calls it on its own thread, so the counters are atomic and the
// thread it ran on is remembered under a mutex for the test that checks which
// thread that is.
//
// "next" is what the build hands back, and by construction it is written before
// Start() and only read afterwards, so it needs no lock of its own. A test that
// wants the rebuild to report a different dependency set puts that set here.
struct RebuildLog {
    std::atomic<int>               calls{0};
    mutable std::mutex             mu;
    std::vector<std::thread::id>   thread_ids{};
    filesystem::WatchData          next{};
    std::function<void()>          on_call{};

    // The function to pass to a Watcher. Counting happens first, so a test that
    // waits for a rebuild can measure the rest of that rebuild from the moment it
    // noticed the call.
    std::function<filesystem::WatchData()> Fn() {
        return [this] {
            calls.fetch_add(1, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(mu);
                thread_ids.push_back(std::this_thread::get_id());
            }
            if (on_call) {
                on_call();
            }
            return next;
        };
    }

    // The thread each rebuild so far ran on, in call order.
    std::vector<std::thread::id> ThreadIds() const {
        std::lock_guard<std::mutex> lock(mu);
        return thread_ids;
    }
};

// A watcher with logging off, no settle delay, and a rebuild function that is
// still to be supplied. Every watcher in this file is built this way: the
// progress lines are part of the command line program's output, not part of what
// is being tested, and a zero delay keeps the tests to one tick of latency.
std::unique_ptr<api::Watcher> MakeWatcher(filesystem::Fs& fs, RebuildLog& log,
                                          std::chrono::milliseconds delay = 0ms) {
    return std::make_unique<api::Watcher>(fs, log.Fn(), delay, false,
                                          logger::UseColor::kColorNever,
                                          logger::PathStyle::kRelPath);
}

// The filesystem every watcher here polls through. One in-memory file is enough:
// the loop only reaches for the filesystem to make a path pretty for a log line,
// and every watcher below has logging switched off.
std::unique_ptr<filesystem::Fs> MakeTestFs() {
    return guchho::test::MakeMockFS({{"/src/index.js", "export default 1\n"}},
                                    filesystem::MockKind::kUnix, "/src");
}

} // namespace

// ---------------------------------------------------------------------------
// A tick that finds nothing
// ---------------------------------------------------------------------------

// A watcher with no dependency set at all has nothing to ask about, so it never
// calls the rebuild function. This is also the state a watcher is in between
// construction and the first build, which is why the loop must tolerate it.
TEST(ApiWatcher, EmptyWatchSetNeverRebuilds) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    watcher->SetWatchData(filesystem::WatchData{});
    watcher->Start();

    // Well past the three ticks that would have been enough to poll anything.
    std::this_thread::sleep_for(350ms);
    watcher->Stop();

    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 0);
}

// Probes that report nothing changed are polled and then left alone: the loop
// asks, gets an empty answer, and goes back to sleep. The same test pins down
// that the loop really was asking, because a small watch set is checked in full
// on every tick — the floor of 64 paths per tick is above its size — so a probe
// that was never asked would mean the loop never ran.
TEST(ApiWatcher, UnchangedProbesArePolledWithoutRebuilding) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::vector<std::shared_ptr<Probe>> probes = MakeProbeSet(3);
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    watcher->SetWatchData(MakeWatchData(probes));
    watcher->Start();

    ASSERT_TRUE(WaitFor([&probes] { return TotalCalls(probes) >= 3; }, 2s));
    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 0);

    // Nothing changed in the meantime, so the extra ticks must not add rebuilds.
    std::this_thread::sleep_for(250ms);
    watcher->Stop();

    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 0);
    EXPECT_GE(TotalCalls(probes), 3);
}

// ---------------------------------------------------------------------------
// A tick that finds something
// ---------------------------------------------------------------------------

// One change is worth one rebuild, and one rebuild is enough. The probe reports
// its change once, so even though the loop keeps asking it afterwards there is
// nothing left to report: the build that ran in between already recomputed
// everything. A loop that rebuilt per poll would show up here as a second call.
TEST(ApiWatcher, OneChangeCausesExactlyOneRebuild) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::shared_ptr<Probe> probe = MakeProbe("/src/index.js");
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    filesystem::WatchData data = MakeWatchData({probe});
    log.next = data;
    probe->Arm();

    watcher->SetWatchData(data);
    watcher->Start();

    ASSERT_TRUE(WaitFor([&log] { return log.calls.load(std::memory_order_relaxed) >= 1; }, 2s));

    // Several more ticks, and the probe is still being asked but has nothing
    // left to report.
    std::this_thread::sleep_for(300ms);
    watcher->Stop();

    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 1);
    EXPECT_GE(probe->calls.load(std::memory_order_relaxed), 1);
}

// The rebuild is what produces the next dependency set, and the loop installs it,
// so a path that only appears in the rebuild's answer is watched from then on.
// Without that hand-over the watcher would keep polling the graph of the previous
// build and would go quiet after the first change.
TEST(ApiWatcher, TheRebuildDecidesWhatIsWatchedNext) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::shared_ptr<Probe> first = MakeProbe("/src/first.js");
    std::shared_ptr<Probe> second = MakeProbe("/src/second.js");
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    log.next = MakeWatchData({second});
    first->Arm();

    watcher->SetWatchData(MakeWatchData({first}));
    watcher->Start();

    ASSERT_TRUE(WaitFor([&log] { return log.calls.load(std::memory_order_relaxed) >= 1; }, 2s));

    // The new path is now part of the watch set, so the loop must start asking
    // it even though no build has mentioned it since.
    ASSERT_TRUE(WaitFor([&second] { return second->calls.load(std::memory_order_relaxed) > 0; }, 2s));
    watcher->Stop();

    // "second" never reported a change, so knowing about it must not have caused
    // a rebuild of its own.
    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 1);
}

// The dependency set is replaced wholesale, not merged into, so a path the new
// build dropped stops being polled in the same call that discovers the new ones.
// The count is read after the rebuild is noticed, and the loop cannot be polling
// "first" at that point or afterwards, because the rebuild and the swap happen on
// the same thread in that order — so the count is free to stay exactly where it
// was.
TEST(ApiWatcher, PathsDroppedByTheRebuildAreNoLongerPolled) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::shared_ptr<Probe> dropped = MakeProbe("/src/dropped.js");
    std::shared_ptr<Probe> kept = MakeProbe("/src/kept.js");
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    log.next = MakeWatchData({kept});
    dropped->Arm();

    watcher->SetWatchData(MakeWatchData({dropped, kept}));
    watcher->Start();

    ASSERT_TRUE(WaitFor([&log] { return log.calls.load(std::memory_order_relaxed) >= 1; }, 2s));
    const int dropped_calls = dropped->calls.load(std::memory_order_relaxed);

    std::this_thread::sleep_for(350ms);
    watcher->Stop();

    EXPECT_EQ(dropped->calls.load(std::memory_order_relaxed), dropped_calls);
    EXPECT_GT(kept->calls.load(std::memory_order_relaxed), 0);
}

// ---------------------------------------------------------------------------
// How much of the watch set a tick covers
// ---------------------------------------------------------------------------

// A watch set of 200 paths is not checked in one go. The list is shuffled and
// divided into slices so that a tick only looks at a bounded number of them and
// the whole set comes round again after about 20 ticks; with the floor of 64
// paths per tick, the first pass over 200 paths needs four ticks rather than one.
// The bound below allows for a second tick having started, which is what a
// scheduler hiccup would look like, and still fails a loop that scanned
// everything every time.
TEST(ApiWatcher, ATickChecksOneSliceOfALargeWatchSet) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::vector<std::shared_ptr<Probe>> probes = MakeProbeSet(200);
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    watcher->SetWatchData(MakeWatchData(probes));
    watcher->Start();

    ASSERT_TRUE(WaitFor([&probes] { return TotalCalls(probes) > 0; }, 2s));
    const int after_first_tick = TotalCalls(probes);
    watcher->Stop();

    EXPECT_GT(after_first_tick, 0);
    EXPECT_LE(after_first_tick, 128);
    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 0);
}

// A path that changed recently is promoted to the front of the rotation and
// checked on every tick, because the file a developer has just saved is the one
// they are about to save again. With 200 paths in the set, a path that stayed in
// the rotation would only be asked about once every four ticks, so the growth in
// its call count over two ticks is what separates the two behaviours. The
// rebuild answers with the same set, so the promotion is not undone by the swap.
TEST(ApiWatcher, ARecentlyChangedPathIsCheckedOnEveryTick) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::vector<std::shared_ptr<Probe>> probes = MakeProbeSet(200);
    std::shared_ptr<Probe> active = probes[100];
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    filesystem::WatchData data = MakeWatchData(probes);
    log.next = data;
    active->Arm();

    watcher->SetWatchData(data);
    watcher->Start();

    ASSERT_TRUE(WaitFor([&log] { return log.calls.load(std::memory_order_relaxed) >= 1; }, 2s));
    const int after_rebuild = active->calls.load(std::memory_order_relaxed);

    // Two ticks' worth of asking, which a rotation of this size could not deliver.
    ASSERT_TRUE(WaitFor([&active, after_rebuild] {
        return active->calls.load(std::memory_order_relaxed) >= after_rebuild + 2;
    }, 3s));
    watcher->Stop();

    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 1);
}

// ---------------------------------------------------------------------------
// The settle delay
// ---------------------------------------------------------------------------

// The delay sits between noticing a change and starting the build, so a burst of
// saves from an editor that writes in several steps costs one rebuild instead of
// one per write. The lower bound is the property: the rebuild cannot have happened
// before the delay had elapsed, and the tick that noticed the change only adds to
// that. The upper bound is not a property but a guard, so a watcher that never
// rebuilt would fail as a missing rebuild rather than as a hung test.
TEST(ApiWatcher, TheSettleDelayHoldsTheRebuildBack) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::shared_ptr<Probe> probe = MakeProbe("/src/index.js");
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log, 400ms);

    filesystem::WatchData data = MakeWatchData({probe});
    log.next = data;

    watcher->SetWatchData(data);
    watcher->Start();

    // Let one tick go by untouched, so the change is noticed by a later tick and
    // the measured interval contains the delay rather than racing it.
    std::this_thread::sleep_for(150ms);
    const std::chrono::steady_clock::time_point changed_at = std::chrono::steady_clock::now();
    probe->Arm();

    ASSERT_TRUE(WaitFor([&log] { return log.calls.load(std::memory_order_relaxed) >= 1; }, 5s));
    const long long elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - changed_at)
            .count();
    watcher->Stop();

    EXPECT_GE(elapsed_ms, 350);
    EXPECT_LT(elapsed_ms, 2000);
    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 1);
}

// ---------------------------------------------------------------------------
// Stopping
// ---------------------------------------------------------------------------

// Stop() is a request to wrap up, not an abort: the rebuild that is already
// running is waited for rather than abandoned half-way, because a build cut off
// mid-flight leaves an output directory nobody asked for. The rebuild here holds
// the thread for 300ms after the counter moves, and Stop() is called once the test
// can see the call, so the wait it takes is that remainder and nothing else.
TEST(ApiWatcher, StopWaitsForTheRebuildInFlight) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::shared_ptr<Probe> running = MakeProbe("/src/running.js");
    std::shared_ptr<Probe> untouched = MakeProbe("/src/untouched.js");
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    filesystem::WatchData data = MakeWatchData({running, untouched});
    log.next = data;
    log.on_call = [] { std::this_thread::sleep_for(300ms); };
    running->Arm();

    watcher->SetWatchData(data);
    watcher->Start();

    ASSERT_TRUE(WaitFor([&log] { return log.calls.load(std::memory_order_relaxed) >= 1; }, 5s));
    const std::chrono::steady_clock::time_point stop_at = std::chrono::steady_clock::now();
    watcher->Stop();
    const long long waited_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stop_at)
            .count();

    EXPECT_GE(waited_ms, 250);
    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 1);

    // And the thread is really gone: arming the path that was never touched
    // produces no further rebuild.
    untouched->Arm();
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 1);
}

// Stopping a watcher that was never started is safe and returns at once, because
// there is no thread to wait for. Without that, a host that disposes a session
// it never watched would block for ever.
TEST(ApiWatcher, StopWithoutStartReturnsImmediately) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    watcher->SetWatchData(MakeWatchData({MakeProbe("/src/index.js")}));

    const std::chrono::steady_clock::time_point stop_at = std::chrono::steady_clock::now();
    watcher->Stop();
    const long long waited_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stop_at)
            .count();

    EXPECT_LT(waited_ms, 500);
    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 0);
}

// A second Stop() is a no-op, since the thread has already been reaped and there
// is nothing left to join. This is the property that lets a host call Stop from a
// shutdown path without knowing whether something else already did.
TEST(ApiWatcher, ASecondStopIsHarmless) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::shared_ptr<Probe> probe = MakeProbe("/src/index.js");
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    watcher->SetWatchData(MakeWatchData({probe}));
    watcher->Start();
    watcher->Stop();

    const std::chrono::steady_clock::time_point stop_at = std::chrono::steady_clock::now();
    watcher->Stop();
    const long long waited_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stop_at)
            .count();

    EXPECT_LT(waited_ms, 500);
    EXPECT_EQ(log.calls.load(std::memory_order_relaxed), 0);
}

// ---------------------------------------------------------------------------
// Which thread builds
// ---------------------------------------------------------------------------

// The rebuild runs on the watcher's own thread and nowhere else. That is what
// makes a build and a poll unable to overlap, and it is why the loop can be this
// small: there is exactly one thread doing the work, and the caller's thread is
// not it. Two rebuilds are provoked so that "the same thread every time" is
// checked as well as "not the caller's thread".
TEST(ApiWatcher, TheRebuildRunsOnTheWatcherThread) {
    std::unique_ptr<filesystem::Fs> fs = MakeTestFs();
    std::shared_ptr<Probe> probe = MakeProbe("/src/index.js");
    RebuildLog log;
    std::unique_ptr<api::Watcher> watcher = MakeWatcher(*fs, log);

    filesystem::WatchData data = MakeWatchData({probe});
    log.next = data;
    probe->Arm(2);

    const std::thread::id test_thread = std::this_thread::get_id();

    watcher->SetWatchData(data);
    watcher->Start();

    ASSERT_TRUE(WaitFor([&log] { return log.calls.load(std::memory_order_relaxed) >= 2; }, 3s));
    watcher->Stop();

    const std::vector<std::thread::id> build_threads = log.ThreadIds();
    ASSERT_GE(build_threads.size(), 2u);
    for (const std::thread::id& build_thread : build_threads) {
        EXPECT_NE(build_thread, test_thread);
        EXPECT_EQ(build_thread, build_threads.front());
    }
}
