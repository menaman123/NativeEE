// tests/ipu/test_fmv_pacer.cpp -- ctest FmvPaceDeadline: the FmvPacer tick-grid
// contract. Ports the OLD pacer test cases that describe the sole (burst) core;
// the debt-clamp cases are not ported because that arm does not exist.
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>

#include "engine/ipu/hle/fmv_pacer.h"

using ps2r::ipu::FmvPacer;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failed;                                                      \
            std::printf("FAIL: %s (line %d): %s\n", msg, __LINE__, #cond);   \
        }                                                                    \
    } while (0)

static constexpr int64_t TP = 2;     // ticks per picture (NTSC ~29.97: round(60/29.97))
static constexpr int64_t TK0 = 1000; // arbitrary tick origin

// Unarmed pacer (period unset or invalid) never waits, never lates, never arms.
static void TestUnarmed() {
    FmvPacer d;  // period 0
    for (int i = 0; i < 5; ++i) {
        const auto w = d.Next(TK0 + i * TP);
        CHECK(w.wait_ticks == 0, "unarmed: no wait");
        CHECK(!w.late, "unarmed: never late");
    }
    CHECK(d.NextTick() == 0, "unarmed: never arms");
    CHECK(!d.Armed(), "unarmed: Armed() false");

    FmvPacer neg;
    neg.SetPeriod(-5);
    const auto w = neg.Next(TK0);
    CHECK(w.wait_ticks == 0 && !w.late, "negative period: disabled");
    CHECK(!neg.Armed(), "negative period: never armed");
}

// First delivery arms with no wait.
static void TestArm() {
    FmvPacer d;
    d.SetPeriod(TP);
    CHECK(!d.Armed(), "not armed before first delivery");
    const auto w = d.Next(TK0);
    CHECK(w.wait_ticks == 0, "first delivery: no wait");
    CHECK(!w.late, "first delivery: not late");
    CHECK(d.NextTick() == TK0 + TP, "first delivery arms next = now + period");
    CHECK(d.Armed(), "armed after first delivery");
}

// Steady state is drift-free: an instantaneous consumer that waits exactly
// what it is told lands every delivery at TK0 + k*TP over 1000 pictures.
static void TestSteadyDriftFree() {
    FmvPacer d;
    d.SetPeriod(TP);
    int64_t now = TK0;
    auto w = d.Next(now);
    CHECK(w.wait_ticks == 0, "steady: arming delivery immediate");
    bool ever_late = false;
    bool off_grid = false;
    for (int k = 1; k <= 1000; ++k) {
        w = d.Next(now);
        now += w.wait_ticks;
        if (w.late) ever_late = true;
        if (now != TK0 + static_cast<int64_t>(k) * TP) off_grid = true;
    }
    CHECK(!ever_late, "steady: never late across 1000 pictures");
    CHECK(!off_grid, "steady: every delivery lands exactly at TK0 + k*TP");
    CHECK(now == TK0 + 1000 * TP, "steady: 1000 pictures, zero drift");
}

// Early arrivals are absorbed: the wait stretches so the delivery still lands
// on the absolute deadline.
static void TestEarlyJitterAbsorbed() {
    FmvPacer d;
    d.SetPeriod(TP);
    (void)d.Next(TK0);  // arm; next deadline TK0 + TP
    const int64_t early_by[] = {1, 1, TP - 1, 1, TP - 1};
    int64_t deadline = TK0 + TP;
    bool ever_late = false;
    bool off_deadline = false;
    for (int i = 0; i < 5; ++i) {
        const int64_t now = deadline - early_by[i];
        const auto w = d.Next(now);
        if (w.late) ever_late = true;
        if (now + w.wait_ticks != deadline) off_deadline = true;
        deadline += TP;
    }
    CHECK(!ever_late, "jitter: early arrivals are never late");
    CHECK(!off_deadline, "jitter: every delivery lands exactly on its absolute deadline");
}

// Small lateness: one instant catch-up serve flagged late, then pacing resumes
// on the ORIGINAL grid (absolute deadlines).
static void TestSmallLateKeepsGrid() {
    FmvPacer d;
    d.SetPeriod(TP);
    (void)d.Next(TK0);            // arm; next = TK0 + TP
    auto w = d.Next(TK0);         // instant loop: waits TP, delivery @ TK0+TP
    CHECK(w.wait_ticks == TP, "small-late: steady wait is one period");
    // next = TK0 + 2TP. Consumer stalls one period past it: arrives TK0 + 3TP.
    const int64_t now = TK0 + 3 * TP;
    w = d.Next(now);
    CHECK(w.late, "small-late: flagged late");
    CHECK(w.wait_ticks == 0, "small-late: no wait when behind");
    // next -> TK0+3TP: exactly on deadline, instant, on time.
    w = d.Next(now);
    CHECK(!w.late && w.wait_ticks == 0, "small-late: exactly-on-deadline catch-up serve is instant, on time");
    // next -> TK0+4TP: back on the original grid.
    w = d.Next(now);
    CHECK(w.wait_ticks == TP, "small-late: pacing resumes on the original grid");
}

// A K-picture backlog drains with K+1 instant serves (K late + the one exactly
// on `now`), then a full one-period wait resumes on the original grid.
static void TestBurstDrainsBacklogInstantly() {
    FmvPacer d;
    d.SetPeriod(TP);
    (void)d.Next(TK0);           // arm: next = TK0 + TP
    const int64_t stall_periods = 100;
    const int64_t now = TK0 + (1 + stall_periods) * TP;  // 100 pictures behind
    int instant = 0;
    int late = 0;
    for (int i = 0; i < 200; ++i) {  // hammer at a frozen 'now' (tight pull loop)
        const auto w = d.Next(now);
        if (w.late) ++late;
        if (w.wait_ticks == 0) {
            ++instant;
        } else {
            CHECK(w.wait_ticks == TP, "burst backlog: first paced wait is one full period");
            break;
        }
    }
    CHECK(instant == stall_periods + 1, "burst backlog: backlog+late serve all drain instantly (no clamp)");
    CHECK(late == stall_periods, "burst backlog: exactly the behind-schedule serves are flagged late");
    CHECK(d.NextTick() == now + 2 * TP, "burst backlog: grid unchanged (pure authored schedule)");
}

// NO debt-clamp arithmetic: after one late serve the deadline advances exactly
// one period from wherever the grid was, however far behind.
static void TestBurstNoClampFloor() {
    FmvPacer d;
    d.SetPeriod(TP);
    (void)d.Next(TK0);           // arm: next = TK0 + TP
    const int64_t far = TK0 + 1000 * TP;
    const auto w = d.Next(far);
    CHECK(w.late && w.wait_ticks == 0, "burst no-clamp: far-late serve is instant and flagged");
    CHECK(d.NextTick() == TK0 + 2 * TP,
          "burst no-clamp: deadline stays on the original grid (TK0+2TP), not clamped to now - period");
}

// The tick twin of the OLD burst case: a 49-picture backlog at 2 ticks/picture.
static void TestBurstTickTwin() {
    FmvPacer t;
    t.SetPeriod(2);
    (void)t.Next(1000);              // arm: next = 1002
    const int64_t now = 1000 + 2 * 50;
    int instant = 0;
    for (int i = 0; i < 200; ++i) {
        const auto w = t.Next(now);
        if (w.wait_ticks == 0) ++instant; else break;
    }
    CHECK(instant == 50, "burst tick: backlog drains instantly (no clamp)");
    CHECK(t.NextTick() == now + 2 * 2, "burst tick: grid unchanged (pure authored schedule)");
}

// ticks_per_pic = 1 degenerate: one picture per tick, 100 pictures over 100 ticks.
static void TestOnePerTick() {
    FmvPacer t;
    t.SetPeriod(1);
    int64_t now = TK0;
    (void)t.Next(now);  // arm
    for (int k = 1; k <= 100; ++k) {
        const auto w = t.Next(now);
        CHECK(w.wait_ticks <= 1, "tick 1/pic: wait never exceeds one tick");
        now += w.wait_ticks;
    }
    CHECK(now == TK0 + 100, "tick 1/pic: 100 pictures over exactly 100 ticks");
}

// Reset disarms, keeps the period, re-arms on the new origin.
static void TestResetRearms() {
    FmvPacer d;
    d.SetPeriod(TP);
    (void)d.Next(TK0);
    (void)d.Next(TK0);
    CHECK(d.Armed(), "reset: armed before Reset");
    d.Reset();
    CHECK(!d.Armed(), "reset: disarmed");
    CHECK(d.Period() == TP, "reset: period survives (rate unchanged)");
    const int64_t later = TK0 + 55 * TP;
    const auto w = d.Next(later);
    CHECK(w.wait_ticks == 0 && !w.late, "reset: re-arm delivery immediate");
    CHECK(d.NextTick() == later + TP, "reset: re-armed on the new origin");
}

// round(60 / fps) with a floor of 1: the authored-rate arming of the pacer.
static void TestAuthoredRateTicks() {
    FmvPacer d;
    d.SetAuthoredRateTicks(29.97);
    CHECK(d.Period() == 2, "rate 29.97 -> 2 ticks/picture");
    d.SetAuthoredRateTicks(30.0);
    CHECK(d.Period() == 2, "rate 30 -> 2 ticks/picture");
    d.SetAuthoredRateTicks(23.976);
    CHECK(d.Period() == 3, "rate 23.976 -> round(2.502) = 3 ticks/picture");
    d.SetAuthoredRateTicks(24.0);
    CHECK(d.Period() == 3, "rate 24 -> round(2.5) = 3 ticks/picture");
    d.SetAuthoredRateTicks(15.0);
    CHECK(d.Period() == 4, "rate 15 -> 4 ticks/picture");
    d.SetAuthoredRateTicks(60.0);
    CHECK(d.Period() == 1, "rate 60 -> 1 tick/picture");
    d.SetAuthoredRateTicks(120.0);
    CHECK(d.Period() == 1, "rate 120 -> floor at 1 tick/picture");
    d.SetAuthoredRateTicks(0.0);
    CHECK(d.Period() == 2, "rate 0 (header lacked a rate) -> 29.97 fallback -> 2 ticks");
    d.SetAuthoredRateTicks(-3.0);
    CHECK(d.Period() == 2, "negative rate -> 29.97 fallback -> 2 ticks");
    CHECK(FmvPacer::kFallbackFps == 29.97, "fallback rate is NTSC 29.97");
    CHECK(!FmvPacer::ShouldPace(0) && !FmvPacer::ShouldPace(1) && FmvPacer::ShouldPace(2),
          "the two priming serves are instant; the third is paced");
}

// Opt-in bounded catch-up (play mode): a grid >= N periods behind re-anchors at
// `now` -- the serve is instant and late, the NEXT serve waits a full period
// again. Below the bound the reference contract holds (no re-anchor).
static void TestBoundedReanchorOptIn() {
    FmvPacer d;
    d.SetPeriod(TP);
    (void)d.Next(TK0);                       // arm: next = TK0 + TP
    CHECK(d.ReanchorPeriods() == 0, "re-anchor: off by default");
    d.SetReanchorPeriods(4);
    // 3 periods behind: late, instant, grid kept (contract).
    auto w = d.Next(TK0 + 4 * TP);           // next was TK0+TP -> 3 periods behind
    CHECK(w.late && w.wait_ticks == 0, "re-anchor: 3 behind -> late, instant");
    CHECK(d.Reanchors() == 0 && d.NextTick() == TK0 + 2 * TP, "re-anchor: below the bound the grid is kept");
    // Now 100 periods behind: late, instant, grid re-anchored at now.
    const int64_t now = TK0 + 102 * TP;
    w = d.Next(now);
    CHECK(w.late && w.wait_ticks == 0, "re-anchor: far behind -> late, instant");
    CHECK(d.Reanchors() == 1 && d.NextTick() == now + TP, "re-anchor: grid re-anchored at now + TP");
    w = d.Next(now);                         // the next picture paces again
    CHECK(!w.late && w.wait_ticks == TP, "re-anchor: the following serve waits one full period");
    // Off again: the reference contract (TestBurstDrainsBacklogInstantly) applies.
    FmvPacer ref;
    ref.SetPeriod(TP);
    (void)ref.Next(TK0);
    (void)ref.Next(TK0 + 101 * TP);
    CHECK(ref.Reanchors() == 0 && ref.NextTick() == TK0 + 2 * TP, "re-anchor off: a 100-period backlog keeps the grid");
}

static void TestClockMigration() {
    for (int64_t reanchor : {int64_t{0}, int64_t{4}}) {
        for (int64_t gap : {int64_t{0}, int64_t{2}, int64_t{24}}) {
            FmvPacer reference;
            reference.SetPeriod(2);
            reference.SetReanchorPeriods(reanchor);
            reference.Next(1000);
            FmvPacer::State state;
            CHECK(reference.Capture(1000 + gap, state), "capture positive/zero/late remaining deadline");
            FmvPacer restored;
            CHECK(restored.Restore(state, 0), "restore deadline at a new domain origin");
            for (int64_t offset = 0; offset < 32; ++offset) {
                const auto a = reference.Next(1000 + gap + offset);
                const auto b = restored.Next(offset);
                CHECK(a.wait_ticks == b.wait_ticks && a.late == b.late,
                      "restored deadline preserves every catch-up/steady delivery");
            }
            CHECK(reference.Reanchors() == restored.Reanchors(), "reanchor count and policy persist");
        }
    }
    FmvPacer unarmed, restored;
    FmvPacer::State state;
    unarmed.SetPeriod(2);
    CHECK(unarmed.Capture(1000, state) && !state.armed, "unused rate keeps its unarmed origin");
    CHECK(restored.Restore(state, 4000) && restored.Next(4000).wait_ticks == 0 &&
          restored.Next(4000).wait_ticks == 2, "unarmed restored context starts on its new origin");
    const auto next = restored.NextTick();
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        auto corrupt = state;
        if (invalid == 0) corrupt.ticks_to_next = 1;
        if (invalid == 1) corrupt.reanchor_periods = -1;
        if (invalid == 2) { corrupt.armed = true; corrupt.ticks_to_next = INT64_MIN; }
        if (invalid == 3) { corrupt.period = INT64_MAX; corrupt.reanchor_periods = 2; }
        CHECK(!restored.Restore(corrupt, 4000) && restored.NextTick() == next,
              "invalid clock cannot mutate a live deadline");
    }
    state.armed = true;
    state.ticks_to_next = INT64_MAX;
    CHECK(!restored.Restore(state, 1), "restored deadline overflow rejects");
    CHECK(!restored.Restore(state, -1), "negative domain origin rejects");
}

int main() {
    TestUnarmed();
    TestArm();
    TestSteadyDriftFree();
    TestEarlyJitterAbsorbed();
    TestSmallLateKeepsGrid();
    TestBurstDrainsBacklogInstantly();
    TestBurstNoClampFloor();
    TestBurstTickTwin();
    TestOnePerTick();
    TestResetRearms();
    TestAuthoredRateTicks();
    TestBoundedReanchorOptIn();
    TestClockMigration();

    std::printf("test_fmv_pacer: %d/%d checks passed\n", g_checks - g_failed, g_checks);
    if (g_failed == 0) {
        std::printf("test_fmv_pacer: PASS (unarmed, arm, steady, jitter, small-late, burst-drain, "
                    "no-clamp, tick-twin, one-per-tick, reset, authored-rate, bounded-reanchor)\n");
        return 0;
    }
    std::printf("test_fmv_pacer: %d FAILED\n", g_failed);
    return 1;
}
