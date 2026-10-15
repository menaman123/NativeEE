// tests/core/test_threads_log.cpp -- CmdRing contract (order, drop-oldest with
// an exact dropped count, concurrent producers), Park (notify wakes; deadline
// returns false; generation guards the lost-wakeup window), Logger levels /
// categories and LogBudget primitives. argv[1] = scratch dir.
#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/log/log.h"
#include "engine/core/log/log_budget.h"
#include "engine/core/threads/cmd_ring.h"
#include "engine/core/threads/park.h"

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

struct Cmd {
    uint32_t seq;
    uint32_t payload;
};

static void TestCmdRing() {
    ps2r::CmdRing<Cmd, 8> ring;
    Cmd out{};
    CHECK(!ring.Pop(out));
    for (uint32_t i = 0; i < 5; i++) ring.Push(Cmd{i, i * 10});
    CHECK(ring.SizeApprox() == 5);
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(ring.Pop(out));
        CHECK(out.seq == i && out.payload == i * 10);
    }
    CHECK(!ring.Pop(out));
    // Overflow: 8 slots, 11 pushes -> the 3 oldest are dropped.
    for (uint32_t i = 0; i < 11; i++) ring.Push(Cmd{i, 0});
    CHECK(ring.Dropped() == 3);
    CHECK(ring.SizeApprox() == 8);
    uint32_t expect = 3;
    while (ring.Pop(out)) CHECK(out.seq == expect++);
    CHECK(expect == 11);

    // Concurrent producers vs one consumer: every popped element is intact and
    // per-producer sequences are increasing.
    ps2r::CmdRing<Cmd, 1024> big;
    constexpr int kProducers = 3;
    constexpr uint32_t kPer = 20000;
    std::atomic<bool> done{false};
    std::vector<std::thread> ts;
    for (int p = 0; p < kProducers; p++)
        ts.emplace_back([&, p] {
            for (uint32_t i = 0; i < kPer; i++) big.Push(Cmd{i, (uint32_t)p});
        });
    uint32_t last[kProducers] = {0, 0, 0};
    uint64_t popped = 0;
    bool ordered = true;
    std::thread consumer([&] {
        Cmd c{};
        for (;;) {
            if (!big.Pop(c)) {
                if (done.load()) break;
                continue;
            }
            popped++;
            if (c.payload >= kProducers) { ordered = false; continue; }
            if (c.seq < last[c.payload]) ordered = false;
            last[c.payload] = c.seq;
        }
    });
    for (auto& t : ts) t.join();
    done.store(true);
    consumer.join();
    Cmd c{};
    while (big.Pop(c)) popped++;
    CHECK(ordered);
    CHECK(popped + big.Dropped() == (uint64_t)kProducers * kPer);
}

static void TestPark() {
    ps2r::Park park;
    const uint64_t gen = park.Generation();
    const uint64_t t0 = ps2r::Park::NowNs();
    CHECK(!park.WaitFor(5 * 1000 * 1000ull, gen));            // 5 ms deadline, nobody notifies
    CHECK(ps2r::Park::NowNs() - t0 >= 4 * 1000 * 1000ull);
    // Notify before wait: the generation makes it visible (no lost wakeup).
    park.Notify();
    CHECK(park.WaitFor(1000ull * 1000 * 1000, gen));
    // Notify from another thread wakes an absolute-deadline wait early.
    const uint64_t gen2 = park.Generation();
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        park.Notify();
    });
    const uint64_t t1 = ps2r::Park::NowNs();
    CHECK(park.WaitUntil(t1 + 5ull * 1000 * 1000 * 1000, gen2));
    CHECK(ps2r::Park::NowNs() - t1 < 4ull * 1000 * 1000 * 1000);
    t.join();
    CHECK(park.Generation() == gen2 + 1);
}

static void TestLogger(const std::string& dir) {
    using namespace ps2r::log;
    CHECK(Enabled(Category::VIF) && Enabled(Category::HARNESS));
    CHECK(std::string(CategoryName(Category::DMAC)) == "DMAC");
    Category c;
    CHECK(CategoryFromName("gif", &c) && c == Category::GIF);
    CHECK(!CategoryFromName("bogus", &c));
    Level lv;
    CHECK(LevelFromName("narrate", &lv) && lv == Level::Narrate);
    CHECK(std::string(LevelName(Level::Warn)) == "warn");

    const std::string path = dir + "/runtime_log.txt";
    Logger& L = Logger::Instance();
    L.Start(path, Level::Info, false);
    CHECK(L.FileOpen());
    PS2R_LOG(GS, Info, "hello %d", 42);
    PS2R_LOG(GS, Narrate, "narration %d", 1);   // above the level: dropped
    PS2R_LOG(VU, Error, "boom");
    CHECK(L.LinesWritten() == 2);
    CHECK(PS2R_LOG_ON(GS, Warn) && !PS2R_LOG_ON(GS, Narrate));
    L.Stop();
    std::ifstream f(path);
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(all == "[GS] hello 42\n[VU] boom\n");
    // Quiet: no file is opened, nothing is written.
    L.Start(dir + "/quiet.txt", Level::Quiet, false);
    CHECK(!L.FileOpen());
    PS2R_LOG(GS, Error, "silent");
    CHECK(L.LinesWritten() == 2);
    L.Stop();

    LogBudget& b = LogBudget::Instance();
    b.Reset();
    int n = 0;
    for (int i = 0; i < 10; i++) n += b.FirstN("k", 3) ? 1 : 0;
    CHECK(n == 3);
    n = 0;
    for (int i = 0; i < 10; i++) n += b.EveryNth("e", 4) ? 1 : 0;
    CHECK(n == 3);   // calls 0, 4, 8
    n = 0;
    for (int i = 0; i < 10; i++) n += b.FrameWindow("f", 2, 100) ? 1 : 0;
    CHECK(n == 2);
    CHECK(b.FrameWindow("f", 2, 101));       // new frame resets
    CHECK(b.Suppressed("f") == 8);
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "/tmp";
    TestCmdRing();
    TestPark();
    TestLogger(dir);
    if (g_fail) {
        std::printf("test_threads_log: %d FAILURE(S)\n", g_fail);
        return 1;
    }
    std::printf("test_threads_log: all checks passed\n");
    return 0;
}
