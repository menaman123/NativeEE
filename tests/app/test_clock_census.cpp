// tests/app/test_clock_census.cpp -- the clock contract's census (rule 5):
// boundaries, fields and presents fall into 1 s WALL windows opened at the first
// boundary; the series lists completed windows only; an idle second yields a zero
// window; a late tick's extra fields count in field_hz while game_hz counts the
// boundary once; presents on another thread land in the window of their time.
#include <chrono>
#include <cstdio>
#include <string>

#include "app/host/clock_census.h"

namespace {
int g_failures = 0;
#define CHECK(cond, what)                                                   \
    do {                                                                    \
        if (cond) std::printf("  PASS %s\n", what);                         \
        else { std::printf("  FAIL %s\n", what); ++g_failures; }            \
    } while (0)
}  // namespace

int main() {
    using ps2r::app::ClockCensus;
    using namespace std::chrono;
    ClockCensus c;
    CHECK(c.GameHzSeries().empty() && c.LatestGameHz() == 0 && c.LatestRenderFps() == 0, "no window before the first boundary");
    const auto t0 = ClockCensus::Clock::time_point(seconds(1000));
    c.NotePresentAt(t0 - milliseconds(5));   // before the stream: belongs to no window
    // Second 1: 60 boundaries, one field each, 45 presents.
    uint64_t tick = 100;
    for (int i = 0; i < 60; ++i) c.NoteBoundaryAt(++tick, t0 + microseconds(16667) * i);
    for (int i = 0; i < 45; ++i) c.NotePresentAt(t0 + milliseconds(22) * i);
    CHECK(c.GameHzSeries().empty(), "the partial first window is not in the series yet");
    // Second 2: 58 boundaries, two of them late by a field (field_hz 60), 25 presents.
    const auto t1 = t0 + seconds(1);
    for (int i = 0; i < 58; ++i) {
        tick += (i == 10 || i == 40) ? 2 : 1;
        c.NoteBoundaryAt(tick, t1 + microseconds(17000) * i);
    }
    for (int i = 0; i < 25; ++i) c.NotePresentAt(t1 + milliseconds(40) * i);
    // Second 3 idle; a boundary at second 4 closes windows 2 and 3.
    c.NoteBoundaryAt(++tick, t0 + seconds(3) + milliseconds(1));
    CHECK(c.GameHzSeries() == "60,58,0", ("game_hz series 60,58,0: " + c.GameHzSeries()).c_str());
    CHECK(c.FieldHzSeries() == "59,60,0", ("field_hz series 59,60,0 (fields between boundaries; the late ticks' extra fields counted): " + c.FieldHzSeries()).c_str());
    CHECK(c.RenderFpsSeries() == "45,25,0", ("render_fps series 45,25,0: " + c.RenderFpsSeries()).c_str());
    CHECK(c.LatestGameHz() == 0 && c.LatestRenderFps() == 0, "latest = the last completed window (the idle one)");
    CHECK(c.Windows().size() == 3 && c.Windows()[1].boundaries == 58, "Windows() exposes the completed windows");
    // Edge attribution (review loop 1): with an edge lookup a boundary belongs to
    // the window of its first field's wall edge. Edges exactly 1/60 s apart from
    // e0; the game answers edge k at e_k + latency, where the latency of the first
    // boundary is 9 ms and every later one 0.5 ms -- by completion time the 60th
    // edge's boundary (at e0 + 1 s + 0.5 ms < window end e0 + 1 s + 9 ms) would
    // fall into window 1 (61) and window 2 would read 59; by edge it reads 60/60.
    // One late frame (edges 70 and 71 answered by one boundary) reads 59 with
    // field_hz 60 in its window; an evicted edge (lookup false) falls back to now.
    {
        ClockCensus e;
        const auto e0 = ClockCensus::Clock::time_point(seconds(2000));
        // The pacer's train: edge t at e0 + (t-1) * 1e9 / 60 ns (rational per edge).
        auto edge_of = [&](uint64_t t) { return e0 + nanoseconds((static_cast<int64_t>(t - 1) * 1000000000LL) / 60); };
        e.SetEdgeLookup([&](uint64_t t, ClockCensus::Clock::time_point* edge) {
            if (t > 500) return false;
            *edge = edge_of(t);
            return true;
        });
        uint64_t k = 0;
        auto answer = [&](uint64_t fields, microseconds latency) {
            k += fields;
            e.NoteBoundaryAt(k, edge_of(k) + latency);
        };
        answer(1, milliseconds(9));                            // edge 1, slow first answer
        for (int i = 1; i < 60; ++i) answer(1, microseconds(500));   // edges 2..60
        for (int i = 0; i < 9; ++i) answer(1, microseconds(500));    // edges 61..69
        answer(2, microseconds(500));                          // edges 70+71 in one boundary (late frame)
        for (int i = 0; i < 49; ++i) answer(1, microseconds(500));   // edges 72..120
        answer(1, microseconds(500));                          // edge 121 closes window 2
        CHECK(e.GameHzSeries() == "60,59", ("edge attribution: game_hz 60,59 (no 61/59 straddle; the late frame's window reads 59): " + e.GameHzSeries()).c_str());
        CHECK(e.FieldHzSeries() == "59,60", ("edge attribution: field_hz 59,60 (the first boundary opens the count): " + e.FieldHzSeries()).c_str());
        // Completion-time attribution of the same stream (no lookup) prints the straddle.
        ClockCensus n;
        k = 0;
        auto answer_n = [&](uint64_t fields, microseconds latency) {
            k += fields;
            n.NoteBoundaryAt(k, edge_of(k) + latency);
        };
        answer_n(1, milliseconds(9));
        for (int i = 1; i < 60; ++i) answer_n(1, microseconds(500));
        for (int i = 0; i < 9; ++i) answer_n(1, microseconds(500));
        answer_n(2, microseconds(500));
        for (int i = 0; i < 49; ++i) answer_n(1, microseconds(500));
        answer_n(1, microseconds(500));
        answer_n(1, microseconds(500));   // edge 122 closes the completion-time window 2
        CHECK(n.GameHzSeries() == "61,59", ("completion-time attribution of the same stream straddles (61 then 59): " + n.GameHzSeries()).c_str());
    }
    {
        // ee_cpu_ms: the game thread's CPU clock delta between boundaries credited to
        // the boundary's window in whole ms, the remainder carried; the total is exact.
        ClockCensus c;
        const auto t0 = steady_clock::now();
        uint64_t tick = 0, cpu = 5'000'000'000ull;   // 5 s of prior CPU: only deltas count
        c.NoteBoundaryAt(++tick, t0, cpu);
        for (int i = 1; i < 60; ++i) { cpu += 10'500'000ull; c.NoteBoundaryAt(++tick, t0 + microseconds(16667) * i, cpu); }   // 59 x 10.5 ms
        for (int i = 0; i < 60; ++i) { cpu += 4'000'000ull; c.NoteBoundaryAt(++tick, t0 + seconds(1) + microseconds(16667) * i, cpu); }   // 60 x 4 ms
        c.NoteBoundaryAt(++tick, t0 + seconds(2) + milliseconds(1), cpu);   // closes window 2
        CHECK(c.EeCpuMsSeries() == "619,240", ("ee_cpu_ms per window (59 x 10.5 = 619.5 -> 619 + carry; 60 x 4 + 0.5 carry = 240): " + c.EeCpuMsSeries()).c_str());
        CHECK(c.EeCpuTotalMs() == 859, "the total is the exact sum of the deltas (859.5 ms -> 859)");
        CHECK(c.GameHzSeries() == "60,60", "the boundaries themselves are unchanged by the CPU reading");
        // The default form reads the calling thread's CPU clock: monotone, never negative.
        ClockCensus d;
        d.NoteBoundary(1);
        volatile uint64_t sink = 0;
        for (uint64_t i = 0; i < 20'000'000ull; ++i) sink += i;   // some CPU
        d.NoteBoundary(2);
        CHECK(d.EeCpuTotalMs() < 60000, "the calling thread's CPU delta is a sane figure");
    }
    {
        // Fast-forward (review C2 loop 1): an engaged cutscene fast-forward adds
        // Deferred fields the pacer never ticked, so the DOMAIN tick runs ahead of
        // the pacer's. With the running fast-forward total passed in, the census
        // keys the edge lookup by the pacer tick: attribution stays by wall edge
        // through and after the stretch (60/60/60, no straddle), field_hz carries
        // the extra fields (60 + 3 x 20 in the engaged window). Without the
        // subtraction the lookup misses (tick > noted) from the first engaged
        // boundary on and the census falls to completion time for the rest of
        // the run -- the straddle the header warns about.
        const auto e0 = ClockCensus::Clock::time_point(seconds(4000));
        auto edge_of = [&](uint64_t t) { return e0 + nanoseconds((static_cast<int64_t>(t - 1) * 1000000000LL) / 60); };
        auto lookup = [&](uint64_t t, ClockCensus::Clock::time_point* edge) {
            if (t > 500) return false;
            *edge = edge_of(t);
            return true;
        };
        // The stream: 60 plain boundaries; 60 boundaries of which the first 20 are
        // fast-forwarded by 3 fields each (factor 4); 60 plain; one closing boundary.
        // Each answers its pacer edge 0.5 ms late, except pacer edge 120 (the last
        // of window 2) which completes 20 ms late -- past the window's end.
        auto drive = [&](ClockCensus& c, bool subtract) {
            uint64_t pacer = 0, ff = 0;
            auto answer = [&](uint64_t extra) {
                pacer += 1;
                ff += extra;
                const auto latency = pacer == 120 ? milliseconds(20) : milliseconds(0) + microseconds(500);
                c.NoteBoundaryAt(pacer + ff, edge_of(pacer) + latency, 0, subtract ? ff : 0);
            };
            for (int i = 0; i < 60; ++i) answer(0);
            for (int i = 0; i < 20; ++i) answer(3);
            for (int i = 0; i < 40; ++i) answer(0);
            for (int i = 0; i < 60; ++i) answer(0);
            answer(0);   // pacer edge 181 closes window 3
        };
        ClockCensus with;
        with.SetEdgeLookup(lookup);
        drive(with, true);
        CHECK(with.GameHzSeries() == "60,60,60", ("fast-forward subtracted: game_hz 60,60,60 by wall edge through the engaged stretch: " + with.GameHzSeries()).c_str());
        CHECK(with.FieldHzSeries() == "59,120,60", ("fast-forward subtracted: field_hz 59,120,60 (the 60 extra fields land in their window): " + with.FieldHzSeries()).c_str());
        ClockCensus without;
        without.SetEdgeLookup(lookup);
        drive(without, false);
        CHECK(without.GameHzSeries() == "60,59,61", ("fast-forward NOT subtracted: the lookup misses from the engaged stretch on and the late edge-120 answer straddles (60,59,61): " + without.GameHzSeries()).c_str());
    }
    std::printf("ClockCensus: %s\n", g_failures ? "FAIL" : "ALL PASS");
    return g_failures ? 1 : 0;
}
