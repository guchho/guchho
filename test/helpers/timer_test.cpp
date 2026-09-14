#include "test/guchho_test.hpp"
#include "guchho/helpers.hpp"

#include <cstdio>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <io.h>
#endif

using guchho::helpers::Timer;

namespace {
    // Captures everything written to stdout by fn() into a temp file and
    // returns the captured text. Platform-portable via dup/dup2.
    std::string CaptureStdout(void (*fn)()) {
        const char* tmpname = "timer_test_cap.tmp";

#ifdef _WIN32
        int saved_fd = _dup(1);
        FILE* tmpf = std::fopen(tmpname, "w");
        _dup2(_fileno(tmpf), 1);
#else
        int saved_fd = dup(1);
        FILE* tmpf = std::fopen(tmpname, "w");
        dup2(fileno(tmpf), 1);
#endif

        fn();
        std::fflush(stdout);

        std::fclose(tmpf);
#ifdef _WIN32
        _dup2(saved_fd, 1);
        _close(saved_fd);
#else
        dup2(saved_fd, 1);
        close(saved_fd);
#endif

        std::ifstream ifs(tmpname);
        std::string result((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
        std::remove(tmpname);
        return result;
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(TimerTest, ConstructWithName)
{
    Timer t("bundle");

}

TEST(TimerTest, ConstructEmptyName)
{
    Timer t("");

}

// ---------------------------------------------------------------------------
// Fork
// ---------------------------------------------------------------------------

TEST(TimerTest, ForkProducesValidTimer)
{
    Timer t("parent");
    Timer forked = t.Fork();
    forked.Log("forked");

}

TEST(TimerTest, ForkDoesNotAlterParent)
{
    Timer t("parent");
    Timer forked = t.Fork();
    t.Log("parent still works");
    forked.Log("forked works");

}

// ---------------------------------------------------------------------------
// Join
// ---------------------------------------------------------------------------

TEST(TimerTest, JoinTakesEarlierStart)
{
    // t1 was created first, so t1.start_ <= t2.start_.
    // Joining t1 into t2 should set t2's start to t1's start.
    Timer t1("first");
    Timer t2("second");
    t2.Join(t1);
    t2.Log("joined");

}

TEST(TimerTest, JoinSameTimerIsNoop)
{
    Timer t("self");
    t.Join(t);
    t.Log("self-joined");

}

TEST(TimerTest, JoinEarlierIntoLater)
{
    Timer t1("early");
    Timer t2("late");
    t1.Join(t2);  // t2 is later, so t1.start_ should stay unchanged.
    t1.Log("early wins");

}

// ---------------------------------------------------------------------------
// Begin / End (profiling off — should not crash)
// ---------------------------------------------------------------------------

TEST(TimerTest, BeginEndWithoutProfiling)
{
    Timer t("test");
    t.Begin("phase1");
    t.End("phase1");
    t.Begin("phase2");
    t.End("phase2");

}

TEST(TimerTest, EndWithoutBegin)
{
    // End without a matching Begin should be safe when profiling is off.
    Timer t("test");
    t.End("orphan");

}

// ---------------------------------------------------------------------------
// Log output format
// ---------------------------------------------------------------------------

TEST(TimerTest, LogOutput)
{
    auto fn = []() {
        Timer t("mytimer");
        t.Log("finished");
    };
    std::string output = CaptureStdout(fn);

    EXPECT_NE(output.find("mytimer finished in"), std::string::npos)
        << "output: " << output;
    EXPECT_NE(output.find("ms"), std::string::npos)
        << "output: " << output;
    EXPECT_NE(output.find("finished"), std::string::npos)
        << "output: " << output;
}

TEST(TimerTest, LogOutputEmptyMessage)
{
    auto fn = []() {
        Timer t("named");
        t.Log("");
    };
    std::string output = CaptureStdout(fn);

    EXPECT_NE(output.find("named finished in"), std::string::npos)
        << "output: " << output;
    EXPECT_NE(output.find("ms"), std::string::npos)
        << "output: " << output;
}

TEST(TimerTest, LogOutputForkedTimer)
{
    auto fn = []() {
        Timer t("base");
        Timer forked = t.Fork();
        forked.Log("child");
    };
    std::string output = CaptureStdout(fn);

    EXPECT_NE(output.find("base finished in"), std::string::npos)
        << "output: " << output;
    EXPECT_NE(output.find("child"), std::string::npos)
        << "output: " << output;
}