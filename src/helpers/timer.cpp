#include "guchho/helpers.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace guchho::helpers {

    // Guchho's minimal timing facility. It measures how long each bundling phase
    // takes - parsing, resolving, linking, generating - to back performance
    // diagnostics and the GUCHHO_PROFILE traces. A Timer holds a name and two
    // steady_clock instants (start and last), prints rounded elapsed
    // milliseconds, and can be duplicated and merged for parallel work.

    namespace {

        // ProfileEnabled - true when GUCHHO_PROFILE is present in the
        // environment. Guchho gates Timer::Begin / Timer::End on this so normal
        // builds stay quiet while a developer can set the variable to get
        // verbose begin/end traces on stderr for slow phases.
        // On Windows the value is duplicated with _dupenv_s (freed afterward);
        // elsewhere std::getenv suffices. Merely being set - even to an empty
        // string - turns profiling on, since the check is about existence.
        // Example: GUCHHO_PROFILE=1 -> true
        //          GUCHHO_PROFILE="" -> true (present)
        //          unset -> false
        bool ProfileEnabled()
        {
#ifdef _WIN32
            char* value = nullptr;
            size_t length = 0;

            const errno_t error =
                _dupenv_s(&value, &length, "GUCHHO_PROFILE");

            const bool enabled =
                error == 0 && value != nullptr;

            if (value != nullptr) {
                std::free(value);
            }

            return enabled;
#else
            return std::getenv("GUCHHO_PROFILE") != nullptr;
#endif
        }

    }

    // Timer constructor - remembers when a Guchho phase started. The bundler
    // makes one Timer per top-level operation ("scan", "link", "generate") and
    // later calls Log or Begin/End to report durations.
    // The name is moved in and both start_ and last_time_ are set to the same
    // steady_clock instant. No I/O happens here.
    // Example: Timer t("bundle"); // t.name_ == "bundle", start_ == last_time_
    Timer::Timer(std::string name)
        : name_(std::move(name)),
          start_(std::chrono::steady_clock::now()),
          last_time_(start_)
    {
    }

    // Timer::Log - print total elapsed time since construction.
    // Guchho calls this at the end of a top-level run to print a one-line
    // summary like "bundle finished in 42ms <message>" on stdout.
    // The delta from start_ is converted to microseconds and rounded to the
    // nearest millisecond with (diff+500)/1000 (or -500 for a negative
    // defensive branch), then printed with the message's exact length.
    // Example: Timer t("bundle"); /* 1.6ms */ t.Log("done")
    //          -> stdout: "bundle finished in 2ms done"
    void Timer::Log(std::string_view message) const
    {
        using namespace std::chrono;

        auto end = steady_clock::now();
        auto diff = duration_cast<microseconds>(end - start_).count();

        int64_t ms =
            (diff >= 0 ? diff + 500 : diff - 500) / 1000;

        std::fprintf(
            stdout,
            "%s finished in %lldms %.*s\n",
            name_.c_str(),
            static_cast<long long>(ms),
            static_cast<int>(message.size()),
            message.data());
    }

    // Timer::Fork - duplicate this timer for a parallel subtask.
    // Guchho runs bundle subtasks concurrently; each worker starts from the
    // same origin so a later Join can keep the earliest start and report the
    // true wall-clock span of the whole operation.
    // Returns *this by value via the implicit copy constructor - the copy
    // carries the exact same timestamps, with no extra clock read.
    // Example: Timer forked = base.Fork(); // same start_/last_time_/name_
    Timer Timer::Fork() const
    {
        return *this;
    }

    // Timer::Join - merge another worker's start into this timer.
    // Guchho creates a timer per thread with Fork, runs the workers, then
    // Joins them back so the final Log reflects the earliest start time.
    // If other.start_ is earlier, start_ is overwritten; last_time_ and name_
    // are left alone since per-section deltas stay local to each worker.
    // Example: a.start_ = 10, b.start_ = 5; a.Join(b); // a.start_ == 5
    void Timer::Join(const Timer& other)
    {
        if (other.start_ < start_) {
            start_ = other.start_;
        }
    }

    // Timer::Begin - mark the start of a profiled section when enabled.
    // Emits a "begin" trace on stderr, but only when GUCHHO_PROFILE is set;
    // otherwise it returns right away with no I/O or clock read. It then
    // records the current instant into last_time_ for the matching End.
    // Example: t.Begin("parse"); // stderr: "bundle begin: parse"
    void Timer::Begin(const std::string& name)
    {
        if (!ProfileEnabled()) {
            return;
        }

        std::fprintf(
            stderr,
            "%s begin: %s\n",
            name_.c_str(),
            name.c_str());

        last_time_ = std::chrono::steady_clock::now();
    }

    // Timer::End - mark the end of a profiled section and report its duration.
    // Completes a section started with Begin and, when profiling is enabled,
    // prints how long it took on stderr. The delta since last_time_ is rounded
    // to milliseconds the same way as Log, then last_time_ advances so paired
    // Begin/End calls chain cleanly.
    // Example: t.Begin("generate"); /* 2.3ms */ t.End("generate");
    //          -> stderr: "bundle end (2ms): generate"
    void Timer::End(const std::string& name)
    {
        if (!ProfileEnabled()) {
            return;
        }

        using namespace std::chrono;

        auto now = steady_clock::now();
        auto diff = duration_cast<microseconds>(now - last_time_).count();

        int64_t ms =
            (diff >= 0 ? diff + 500 : diff - 500) / 1000;

        std::fprintf(
            stderr,
            "%s end (%lldms): %s\n",
            name_.c_str(),
            static_cast<long long>(ms),
            name.c_str());

        last_time_ = now;
    }

} // namespace guchho::helpers
