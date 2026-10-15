// Stage 3D1 timing-domain suites (one binary, section per ctest name):
//   domain  -- EmuDomainAndRegime: RegimeDetector pins (fast/full windows, the
//              arrival_gap == n-1 no-flap pin, strict thresholds, bounds),
//              BoundaryAdvance pins + property sweep, EmuDomain quantum carry /
//              top-up / lead governor / idle release / bypass / ReAnchor / pin.
//   budget  -- BudgetClock: thresh vectors 976/781/585/576/390, period rules,
//              fold pins, closed == literal over a grid + 200k random vectors,
//              the boundary-guard sliver, the synthetic per-frame Query, and
//              the PhaseClassifier truth table.
//   clocks  -- DerivedClocksPurity: the nine clocks are pure functions of the
//              EmuDomain (two runs with identical boundary sequences yield
//              identical clock streams; a "wall clock" stub fed different
//              values changes nothing), counts-per-tick facts, RTC fixed
//              epoch, heartbeat rounding, kernel clock install.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "engine/ee/hle/kernl_timer.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/timing/budget_clock.h"
#include "engine/ee/timing/derived_clocks.h"
#include "engine/ee/timing/emu_domain.h"
#include "engine/ee/timing/phase_classifier.h"
#include "engine/ee/timing/regime_detector.h"
#include "engine/ee/timing/tick_source.h"
#include "engine/ee/timing/turbo_governor.h"
#include "engine/ee/timing/warp.h"

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(cond, name) \
    do { if (cond) { ++g_pass; } else { std::printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__); g_fail++; } } while (0)
#define REPORT(name) std::printf("PASS %s\n", name)

static float F32(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// ---------------------------------------------------------------- domain
static void RegimePins() {
    {   // UP, fast window: exactly at the 15th sample.
        RegimeDetector d;
        d.Seed();
        bool early = false;
        for (int i = 0; i < 14; ++i) early |= d.OnBoundary(2, 2);
        CHECK(!early && d.n() == 1, "regime: no change before the fast window fills");
        CHECK(d.OnBoundary(2, 2) && d.n() == 2, "regime: 15th over-sample raises 1 -> 2 (fast up > 0.50)");
    }
    {   // DOWN, full window.
        RegimeDetector d;
        d.Seed();
        for (int i = 0; i < 15; ++i) d.OnBoundary(2, 2);
        bool changed = false;
        for (int i = 0; i < 120; ++i) changed = d.OnBoundary(0, 1);
        CHECK(changed && d.n() == 1, "regime: 120 under-samples (0+1 < 2) lower 2 -> 1");
    }
    {   // The no-flap pin: arrival_gap == n-1 is not DOWN evidence.
        RegimeDetector d;
        d.Seed();
        for (int i = 0; i < 15; ++i) d.OnBoundary(2, 2);
        bool changed = false;
        for (int i = 0; i < 3 * 120; ++i) changed |= d.OnBoundary(1, 2);
        CHECK(!changed && d.n() == 2, "regime: steady 30 fps (arrival 1, final 2) holds n=2 for 360 boundaries");
    }
    {   // Strict thresholds: 36/120 does not raise, 37/120 does.
        RegimeDetector d;
        d.Seed();
        for (int i = 0; i < 15; ++i) d.OnBoundary(2, 2);
        bool changed = false;
        for (int i = 0; i < 120; ++i) changed |= d.OnBoundary(2, i < 36 ? 3 : 2);
        CHECK(!changed && d.n() == 2, "regime: fo == 0.30 exactly does NOT raise");
        changed = false;
        for (int i = 0; i < 120; ++i) changed |= d.OnBoundary(2, i < 37 ? 3 : 2);
        CHECK(changed && d.n() == 3, "regime: fo > 0.30 raises 2 -> 3");
    }
    {   // Bounds.
        RegimeDetector d;
        d.Seed();
        for (int w = 0; w < 12; ++w)
            for (int i = 0; i < 120; ++i) d.OnBoundary(9, 9);
        CHECK(d.n() == 5, "regime: n saturates at kMaxN = 5");
        RegimeDetector d1;
        d1.Seed();
        bool any = false;
        for (int i = 0; i < 15; ++i) any |= d1.OnBoundary(0, 1);
        CHECK(!any && d1.n() == 1, "regime: at n=1 under-evidence can never fire");
    }
}

static void BoundaryAdvancePins() {
    int64_t la = 0;
    CHECK(EmuDomain::BoundaryAdvance(2, 0, 2, 0, &la) == 2 && la == 0, "advance: full grant when raw kept up");
    CHECK(EmuDomain::BoundaryAdvance(2, 0, 1, 0, &la) == 2 && la == 1, "advance: fast frame may lead by 1");
    CHECK(EmuDomain::BoundaryAdvance(2, 0, 1, la, &la) == 1 && la == 1, "advance: next fast frame clipped to hold lead <= 1");
    CHECK(EmuDomain::BoundaryAdvance(2, 0, 0, la, &la) == 0 && la == 1, "advance: zero-gap frame at max lead advances 0");
    CHECK(EmuDomain::BoundaryAdvance(2, 0, 6, 0, &la) == 2 && la == -4, "advance: stall frame absorbed (lag -4)");
    CHECK(EmuDomain::BoundaryAdvance(2, 0, 1, la, &la) == 2 && la == -3, "advance: lag never repaid as a burst (<= quantum)");
    CHECK(EmuDomain::BoundaryAdvance(2, 2, 0, 1, &la) == 2 && la == 3, "advance: carried ticks are never regressed");
    uint32_t rng = 0x12345678u;
    auto next = [&rng] { rng = rng * 1664525u + 1013904223u; return rng >> 16; };
    bool ok = true;
    for (int q = 1; q <= 5; ++q) {
        int64_t lead = 0;
        for (int i = 0; i < 2000; ++i) {
            uint64_t gap = next() % 8;
            uint64_t carried = next() % (q + 1);
            if (carried > gap) carried = gap;
            int64_t la2 = 0;
            const uint64_t adv = EmuDomain::BoundaryAdvance(q, carried, gap, lead, &la2);
            if (adv < carried || adv > static_cast<uint64_t>(q) || la2 > EmuDomain::kLeadSlackTicks ||
                la2 != lead + static_cast<int64_t>(adv) - static_cast<int64_t>(gap))
                ok = false;
            lead = la2;
        }
    }
    CHECK(ok, "advance: 10000-vector sweep holds adv >= carried, adv <= quantum, lead <= 1");
}

// A frame loop driven purely by counts: `raw_per_frame` raw ticks between
// boundaries, arrival after `arrival_after` of them.
static void RunFrames(EmuDomain& d, TickSource& src, int frames, int raw_per_frame, int arrival_after) {
    for (int f = 0; f < frames; ++f) {
        for (int t = 0; t < raw_per_frame; ++t) {
            if (t == arrival_after) d.NoteArrival(src.RawCount());
            d.OnRawTick(src.Tick());
        }
        if (arrival_after >= raw_per_frame) d.NoteArrival(src.RawCount());
        d.OnBoundary(src.RawCount());
    }
}

static void EmuDomainRules() {
    {   // Quantum: at regime 1, a frame that burned 3 raw ticks still advances exactly 1 emu tick.
        EmuDomain d;
        TickSource src;
        d.ReAnchor(src.RawCount());
        d.OnBoundary(src.Tick());  // first boundary: anchors the loop
        const uint64_t e0 = d.DomainTick();
        RunFrames(d, src, 1, 3, 0);
        CHECK(d.DomainTick() - e0 == 1 && d.Regime() == 1, "domain: 3 raw ticks in one frame -> 1 emu tick at regime 1 (host stall absorbed)");
        CHECK(d.Lead() == -2, "domain: the two absorbed ticks are lag (never repaid)");
        RunFrames(d, src, 1, 1, 0);
        CHECK(d.DomainTick() - e0 == 2 && d.Lead() == -2, "domain: the next 1-tick frame advances 1, lag stays");
    }
    {   // Top-up: a frame that saw 0 raw ticks still gets its quantum up to the lead cap.
        EmuDomain d;
        TickSource src;
        d.ReAnchor(0);
        d.OnBoundary(src.Tick());
        const uint64_t e0 = d.DomainTick();
        d.OnBoundary(src.RawCount());  // 0 raw ticks
        CHECK(d.DomainTick() - e0 == 1 && d.Lead() == 1, "domain: boundary tops the counter up to base+regime (lead 1)");
        d.OnBoundary(src.RawCount());  // 0 raw ticks again: governor holds lead <= 1
        CHECK(d.DomainTick() - e0 == 1 && d.Lead() == 1, "domain: emu never leads raw by more than 1 tick");
    }
    {   // Carry: intra-frame raw ticks fire emu ticks until the quantum is exhausted.
        EmuDomain d;
        TickSource src;
        d.SetRegimePin(2);
        d.ReAnchor(0);
        d.OnBoundary(src.Tick());
        const uint64_t e0 = d.DomainTick();
        d.OnRawTick(src.Tick());
        CHECK(d.DomainTick() - e0 == 1, "domain: 1st raw tick of the frame carries emu +1");
        d.OnRawTick(src.Tick());
        CHECK(d.DomainTick() - e0 == 2, "domain: 2nd raw tick carries emu +2 (quantum 2)");
        d.OnRawTick(src.Tick());
        CHECK(d.DomainTick() - e0 == 2, "domain: 3rd raw tick absorbed (quantum exhausted)");
        d.OnBoundary(src.RawCount());
        CHECK(d.DomainTick() - e0 == 2 && d.Regime() == 2, "domain: pinned regime 2 settles at exactly base+2");
    }
    {   // Idle release: > kIdleReleaseTicks raw ticks without a boundary ride 1:1; the return re-anchors.
        EmuDomain d;
        TickSource src;
        d.ReAnchor(0);
        d.OnBoundary(src.Tick());
        const uint64_t e0 = d.DomainTick();
        for (uint64_t i = 0; i < 40; ++i) d.OnRawTick(src.Tick());
        // In-loop for the first 6 raw ticks (only 1 carried: quantum 1), then 1:1 for 34.
        CHECK(d.DomainTick() - e0 == 1 + 34, "domain: load/FMV idle (no boundary) rides raw ticks 1:1 after 6 ticks");
        const uint64_t before = d.DomainTick();
        d.OnBoundary(src.RawCount());
        CHECK(d.DomainTick() == before && d.Lead() == 0 && d.Detector().fast_window(),
              "domain: re-entering the loop re-anchors (counter continuous, lead 0, detector reseeded)");
    }
    {   // Bypass (freerun/warp/turbo): 1:1 always.
        EmuDomain d;
        TickSource src;
        d.ReAnchor(0);
        d.OnBoundary(src.Tick());
        d.SetBypass(true);
        const uint64_t e0 = d.DomainTick();
        for (int i = 0; i < 5; ++i) d.OnRawTick(src.Tick());
        d.OnBoundary(src.RawCount());
        CHECK(d.DomainTick() - e0 == 5, "domain: bypass rides 1:1 (5 raw -> 5 emu) and the boundary adds nothing");
    }
    {   // Adaptive regime through the domain: a sustained 2-tick loop lifts the quantum to 2.
        EmuDomain d;
        TickSource src;
        d.ReAnchor(0);
        d.OnBoundary(src.Tick());
        RunFrames(d, src, 15, 2, 2);  // arrival at the boundary, 2 raw ticks per frame
        CHECK(d.Regime() == 2, "domain: 15 frames of final_gap 2 lift the regime to 2 (fast window)");
        const uint64_t e0 = d.DomainTick();
        RunFrames(d, src, 10, 2, 2);
        CHECK(d.DomainTick() - e0 == 20, "domain: at regime 2 each 2-tick frame reads exactly 2 emu ticks");
    }
    {   // Pin overrides the detector.
        EmuDomain d;
        TickSource src;
        d.SetRegimePin(1);
        d.ReAnchor(0);
        d.OnBoundary(src.Tick());
        RunFrames(d, src, 200, 3, 3);
        CHECK(d.Regime() == 1 && d.RegimePin() == 1, "domain: regime pin 1 holds under 200 frames of 3-tick evidence");
        d.SetRegimePin(9);
        CHECK(d.RegimePin() == -1, "domain: out-of-range pin -> adaptive");
    }
    {   // ReAnchor keeps the counter, resets boundaries.
        EmuDomain d;
        TickSource src;
        d.ReAnchor(0);
        d.OnBoundary(src.Tick());
        RunFrames(d, src, 5, 1, 1);
        const uint64_t e = d.DomainTick();
        d.ReAnchor(src.RawCount());
        CHECK(d.DomainTick() == e && d.BoundaryCount() == 0 && !d.InLoop(src.RawCount()),
              "domain: ReAnchor keeps the counter continuous, clears the loop and boundary count");
    }
}

static void TickSourcePins() {
    TickSource s;
    CHECK(s.PeriodUs() == 16667 && !s.Bypass(), "tick_source: realtime period 16667 us, no bypass");
    s.SetMode(TickSource::Mode::Turbo, 4);
    CHECK(s.PeriodUs() == 16667 / 4 && s.K() == 4 && !s.Bypass(),
          "tick_source: turbo K=4 -> 4166 us, NOT a bypass (finite K stays under the quantum)");
    s.SetMode(TickSource::Mode::Turbo, 1);
    CHECK(s.K() == 2, "tick_source: turbo K clamps up to 2");
    s.SetMode(TickSource::Mode::Turbo, 100);
    CHECK(s.K() == 64, "tick_source: turbo K clamps down to 64");
    s.SetMode(TickSource::Mode::Freerun);
    CHECK(s.PeriodUs() == 0 && s.K() == 0 && s.Bypass(), "tick_source: freerun releases at once, bypass");
    s.SetMode(TickSource::Mode::Warp);
    CHECK(s.PeriodUs() == 0 && s.Bypass(), "tick_source: warp releases at once");
    CHECK(s.Tick() == 1 && s.Tick() == 2 && s.RawCount() == 2, "tick_source: Tick() counts");
    TurboGovernor g;
    g.SetLoadTurbo(true);
    g.Update(0);
    CHECK(g.Mode() == TickSource::Mode::Realtime && g.K() == 1, "turbo: load turbo idle -> realtime");
    for (uint64_t ms = 0; ms <= 1500; ms += 100) g.NoteLoadActivity(ms);
    g.Update(1500);
    CHECK(g.LoadTurboEngaged() && g.Mode() == TickSource::Mode::Freerun && g.K() == 0,
          "turbo: 1500 ms of continuous load activity engages the FREERUN form (OLD AutoK == 0)");
    {
        TickSource ts;
        g.Apply(ts);
        CHECK(ts.mode() == TickSource::Mode::Freerun && ts.Bypass(), "turbo: engaged load turbo applies Freerun (a bypass)");
    }
    g.Update(1500 + 251);
    CHECK(!g.LoadTurboEngaged(), "turbo: releases 250 ms after activity stops");
    for (uint64_t ms = 3000; ms <= 4500; ms += 100) g.NoteLoadActivity(ms);
    g.NoteInput(4400);
    g.Update(4500);
    CHECK(!g.LoadTurboEngaged(), "turbo: recent input blocks engagement");
    TurboGovernor off;
    for (uint64_t ms = 0; ms <= 3000; ms += 100) off.NoteLoadActivity(ms);
    off.Update(3000);
    CHECK(!off.LoadTurboEngaged() && off.Mode() == TickSource::Mode::Realtime, "turbo: load turbo default OFF never engages");
    TurboGovernor manual;
    manual.SetManual(1);
    CHECK(manual.Mode() == TickSource::Mode::Freerun, "turbo: engine.turbo=1 -> freerun");
    manual.SetManual(8);
    CHECK(manual.Mode() == TickSource::Mode::Turbo && manual.K() == 8, "turbo: engine.turbo=8 -> K 8");
    Warp w;
    CHECK(!w.Active() && !w.OnFrame(5), "warp: unconfigured is a no-op");
    w.SetTarget(10);
    CHECK(w.Active() && !w.OnFrame(9), "warp: active below the target");
    TickSource ws;
    w.Apply(ws, TickSource::Mode::Realtime, 1);
    CHECK(ws.mode() == TickSource::Mode::Warp, "warp: applies Warp mode while active");
    CHECK(w.OnFrame(10) && !w.Active(), "warp: reaching the target releases once");
    w.Apply(ws, TickSource::Mode::Realtime, 1);
    CHECK(ws.mode() == TickSource::Mode::Realtime, "warp: released -> fallback mode");
}

// ---------------------------------------------------------------- budget
static void BudgetPins() {
    constexpr uint32_t kRate = 0x8CA00;  // 576000 = BUSCLK/256 per second
    constexpr uint32_t kFps = 60;
    constexpr uint64_t kPeriod = 9600;
    constexpr uint64_t kPhase = 78;
    CHECK(BudgetClock::PeriodCounts(kRate, kFps) == kPeriod, "budget: period 576000/60 = 9600");
    CHECK(BudgetClock::PeriodCounts(0xFFFFFFFFu, 1) == 0xFFFFFFFFull, "budget: period zero-extends (not sign)");
    CHECK(BudgetClock::PeriodCounts(kRate, 0) == 0xFFFFFFFFull, "budget: divu by 0 -> LO = -1 -> 0xFFFFFFFF");
    CHECK(BudgetClock::ThreshCounts(F32(0x3ADE382Fu), kRate) == 976, "budget: thresh fracA = 976");
    CHECK(BudgetClock::ThreshCounts(F32(0x3AB1C5E0u), kRate) == 781, "budget: thresh fracB = 781");
    CHECK(BudgetClock::ThreshCounts(F32(0x3A8556ECu), kRate) == 585, "budget: thresh fracC = 585 (exact single, not 586)");
    CHECK(BudgetClock::ThreshCounts(F32(0x3A83126Fu), kRate) == 576, "budget: thresh 0.001 s = 576");
    CHECK(BudgetClock::ThreshCounts(F32(0x3A31C93Bu), kRate) == 390, "budget: thresh fracD = 390");
    CHECK(BudgetClock::ThreshCounts(1.0e10f, kRate) == 0x7FFFFFFF, "budget: cvt.w.s saturates +");
    CHECK(BudgetClock::ThreshCounts(-1.0e10f, kRate) == static_cast<int32_t>(0x80000000u), "budget: cvt.w.s saturates -");
    CHECK(BudgetClock::ThreshCounts(F32(0x7FC00000u), kRate) == 0x7FFFFFFF, "budget: NaN operand clamps to +max");
    CHECK(BudgetClock::FoldClosed(0, kPeriod, kPhase) == 0, "fold: 0");
    CHECK(BudgetClock::FoldClosed(9600, kPeriod, kPhase) == 9600, "fold: == period kept");
    CHECK(BudgetClock::FoldClosed(9601, kPeriod, kPhase) == 9601, "fold: sliver kept");
    CHECK(BudgetClock::FoldClosed(9678, kPeriod, kPhase) == 9678, "fold: == lim kept");
    CHECK(BudgetClock::FoldClosed(9679, kPeriod, kPhase) == 79, "fold: one subtract");
    CHECK(BudgetClock::FoldClosed(19278, kPeriod, kPhase) == 9678, "fold: lands on lim");
    CHECK(BudgetClock::FoldClosed(19279, kPeriod, kPhase) == 79, "fold: two subtracts");
    bool sliver = true;
    for (uint64_t e = kPeriod + 1; e <= kPeriod + kPhase; ++e) {
        if (BudgetClock::FoldClosed(e, kPeriod, kPhase) != e) sliver = false;
        if (kPeriod - e <= 0x7FFFFFFFull) sliver = false;
        if (!BudgetClock::Headroom(F32(0x3ADE382Fu), kRate, kFps, e, 0, kPhase)) sliver = false;
    }
    CHECK(sliver, "budget: the (period, period+phase] sliver reads headroom TRUE (78-count guard)");
    CHECK(!BudgetClock::Headroom(F32(0x3ADE382Fu), kRate, kFps, kPeriod - 976, 0, kPhase), "budget: remaining == thresh -> no headroom (sltu strict)");
    CHECK(BudgetClock::Headroom(F32(0x3ADE382Fu), kRate, kFps, kPeriod - 977, 0, kPhase), "budget: remaining == thresh+1 -> headroom");
    CHECK(BudgetClock::Headroom(F32(0x3A31C93Bu), kRate, kFps, 12345, 12345, kPhase), "budget: frame top (elapsed 0) has headroom");
    CHECK(BudgetClock::FoldClosed(static_cast<uint64_t>(5) - 10, kPeriod, kPhase) <= kPeriod + kPhase, "fold: negative (wrapped) elapsed folds inside lim");
    bool grid = true;
    for (uint64_t period : {1ull, 2ull, 3ull, 7ull, 9600ull})
        for (uint64_t phase : {uint64_t{0}, uint64_t{1}, uint64_t{78}, period - 1, period})
            for (uint64_t e = 0; e < 6 * period + phase + 3; ++e)
                if (BudgetClock::FoldClosed(e, period, phase) != BudgetClock::FoldLiteral(e, period, phase)) grid = false;
    CHECK(grid, "fold: closed == literal over the period/phase grid");
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    auto next = [&rng] { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };
    bool rnd = true;
    for (int i = 0; i < 200000; ++i) {
        const uint64_t period = 1 + next() % 20000;
        const uint64_t phase = next() % (2 * period);
        const uint64_t e = next() % (period * 1000);
        if (BudgetClock::FoldClosed(e, period, phase) != BudgetClock::FoldLiteral(e, period, phase)) rnd = false;
    }
    CHECK(rnd, "fold: closed == literal over 200000 random vectors");
    bool huge = true;
    for (uint64_t e : {0x0000FFFFFFFFFFFFull, 0x123456789ABCDEFull, 0xFFFFFFFFFFFFFFFFull}) {
        const uint64_t folded = BudgetClock::FoldClosed(e, kPeriod, kPhase);
        const uint64_t lim = kPeriod + kPhase;
        if (folded > lim || (e - folded) % kPeriod != 0 || folded + kPeriod <= lim) huge = false;
    }
    CHECK(huge, "fold: huge elapsed folds to the minimal k (O(1))");
    // Synthetic per-frame clock (title tuning passed in: base 8800, step 96).
    BudgetClock bc;
    const uint64_t anchor = 1000000;
    CHECK(bc.Query(anchor, 8800, 96) == anchor + 8800 + 96 && bc.ReadsThisFrame() == 1, "budget clock: 1st query = anchor + base + step");
    CHECK(bc.Query(anchor, 8800, 96) == anchor + 8800 + 2 * 96, "budget clock: 2nd query = anchor + base + 2*step");
    CHECK(!BudgetClock::Headroom(F32(0x3ADE382Fu), kRate, kFps, anchor + 8800 + 96, anchor, kPhase),
          "budget clock: full frame (base 8800) rem 704 < 976: first-lap class not granted");
    CHECK(BudgetClock::Headroom(F32(0x3ADE382Fu), kRate, kFps, anchor + 0 + 96, anchor, kPhase),
          "budget clock: low-draw frame (base 0) rem 9504 > 976: granted");
    CHECK(bc.Query(anchor + 9600, 8800, 96) == anchor + 9600 + 8800 + 96 && bc.ReadsThisFrame() == 1,
          "budget clock: anchor change resets the per-frame read count");
    bc.Reset();
    CHECK(bc.AnchorChanged(anchor) && bc.ReadsThisFrame() == 0, "budget clock: Reset forgets the anchor");
}

static void PhaseTruthTable() {
    constexpr uint32_t kLow = 48, kBaseLo = 0, kBaseHi = 8800;
    {
        PhaseClassifier c;
        bool ok = c.OnFrame(5, kLow, kBaseLo, kBaseHi, true) == kBaseHi;      // 1st low: not yet
        ok &= c.OnFrame(9, kLow, kBaseLo, kBaseHi, true) == kBaseLo;          // 2nd consecutive low: enter
        ok &= c.OnFrame(3, kLow, kBaseLo, kBaseHi, true) == kBaseLo;          // stays
        ok &= c.OnFrame(900, kLow, kBaseLo, kBaseHi, true) == kBaseHi;        // first full frame leaves
        ok &= !c.low();
        ok &= c.OnFrame(2, kLow, kBaseLo, kBaseHi, true) == kBaseHi;          // single glitch frame: hi
        ok &= c.OnFrame(700, kLow, kBaseLo, kBaseHi, true) == kBaseHi;
        CHECK(ok, "phase: truth table (enter after 2 consecutive low, leave on first full, glitch ignored)");
    }
    {
        PhaseClassifier c;
        bool ok = true;
        for (int i = 0; i < 1000; ++i) ok &= c.OnFrame(400 + (i % 37), kLow, kBaseLo, kBaseHi, true) == kBaseHi;
        CHECK(ok && c.low_streak() == 0, "phase: gameplay frames never open the low phase");
    }
    {
        PhaseClassifier c;
        bool ok = c.OnFrame(kLow, kLow, kBaseLo, kBaseHi, true) == kBaseHi;        // == max is NOT low
        ok &= c.OnFrame(kLow - 1, kLow, kBaseLo, kBaseHi, true) == kBaseHi;        // 1st low
        ok &= c.OnFrame(kLow - 1, kLow, kBaseLo, kBaseHi, true) == kBaseLo;        // 2nd low
        CHECK(ok, "phase: draws < lowdraw_max is the strict low test");
    }
    {
        PhaseClassifier c;
        bool ok = true;
        for (int i = 0; i < 10; ++i) ok &= c.OnFrame(0, kLow, kBaseLo, kBaseHi, false) == kBaseHi;
        CHECK(ok && !c.low(), "phase: disabled classifier always returns base_hi");
    }
    {
        PhaseClassifier c;
        for (uint64_t i = 0; i < 0x10010; ++i) (void)c.OnFrame(1, kLow, kBaseLo, kBaseHi, true);
        CHECK(c.low() && c.low_streak() == 0xFFFFu, "phase: the streak saturates at 0xFFFF and stays low");
    }
}

// ---------------------------------------------------------------- clocks
struct ClockStream {
    std::vector<uint64_t> t0, t1, t2, t3, vb, fb, par, anchor, cop0, rtc, hb, cdvd;
    bool operator==(const ClockStream& o) const {
        return t0 == o.t0 && t1 == o.t1 && t2 == o.t2 && t3 == o.t3 && vb == o.vb && fb == o.fb && par == o.par &&
               anchor == o.anchor && cop0 == o.cop0 && rtc == o.rtc && hb == o.hb && cdvd == o.cdvd;
    }
};

// A "wall clock" stub: the value it yields is consulted by the driver (as a
// real pacer would) and must be invisible to every clock.
static ClockStream Run(const std::vector<int>& raw_per_frame, std::function<uint64_t()> wall, bool rtc_fixed) {
    EmuDomain d;
    TickSource src;
    DerivedClocks clocks(d);
    clocks.SetRtcFixed(rtc_fixed);
    d.SetRegimePin(1);
    d.ReAnchor(src.RawCount());
    ClockStream s;
    for (int n : raw_per_frame) {
        for (int t = 0; t < n; ++t) {
            (void)wall();  // the pacer looks at its clock, then ticks
            d.OnRawTick(src.Tick());
        }
        d.NoteArrival(src.RawCount());
        d.OnBoundary(src.RawCount());
        s.t0.push_back(clocks.TimerCounts(0));
        s.t1.push_back(clocks.TimerCounts(1));
        s.t2.push_back(clocks.TimerCounts(2));
        s.t3.push_back(clocks.TimerCounts(3));
        s.vb.push_back(clocks.VblankCount());
        s.fb.push_back(clocks.FrameBoundaryCount());
        s.par.push_back(clocks.SyncVFieldParity());
        s.anchor.push_back(clocks.BudgetAnchorCounts());
        s.cop0.push_back(clocks.Cop0Count());
        s.rtc.push_back(clocks.RtcEpochSeconds());
        s.hb.push_back(clocks.IopHeartbeatBeats(16667));
        s.cdvd.push_back(clocks.CdvdEpochTick());
    }
    return s;
}

static void ClocksPurity() {
    std::vector<int> seq;
    uint32_t rng = 7;
    for (int i = 0; i < 400; ++i) { rng = rng * 1664525u + 1013904223u; seq.push_back(1 + (rng >> 16) % 4); }
    uint64_t wall_a = 0, wall_b = 1'000'000'000ull;
    const ClockStream a = Run(seq, [&] { return wall_a += 16'667'000ull; }, true);
    const ClockStream b = Run(seq, [&] { return wall_b += 3'333ull; }, true);
    CHECK(a == b, "clocks: two runs with identical boundary sequences and DIFFERENT wall stubs read identical clock streams");
    const ClockStream c = Run(seq, [&] { return wall_a += 1; }, true);
    CHECK(a == c, "clocks: a third run (another wall stub) is identical too");
    // Host jitter (a different number of raw ticks between the same boundaries,
    // all inside the idle-release window) is absorbed into lag at regime 1: the
    // game-visible streams stay identical. Frame 0 is left alone: before the
    // first boundary the domain is not in the loop and rides raw ticks 1:1 (the
    // boot / idle rule), so only frames 1.. are jittered.
    std::vector<int> seq2 = seq;
    for (size_t i = 3; i < seq2.size(); i += 3) seq2[i] = 1 + (seq2[i] % 5);
    const ClockStream dd = Run(seq2, [&] { return wall_a += 1; }, true);
    CHECK(a == dd, "clocks: raw-tick jitter between the same boundaries never reaches a game-visible clock (regime 1)");
    CHECK(a.vb.size() == 400 && a.vb.back() >= 400, "clocks: the vblank tick advanced at least once per boundary");
    // Fixed facts.
    CHECK(DerivedClocks::kCountsPerTick[0] == 2457600 && DerivedClocks::kCountsPerTick[1] == 153600 &&
          DerivedClocks::kCountsPerTick[2] == 9600 && DerivedClocks::kCountsPerTick[3] == 262,
          "clocks: T*_MODE.CLKS counts per 60 Hz field = 2457600 / 153600 / 9600 / 262 (BUSCLK 147.456 MHz)");
    EmuDomain d;
    DerivedClocks k(d);
    TickSource src;
    d.ReAnchor(0);
    d.SetBypass(true);
    for (int i = 0; i < 7; ++i) d.OnRawTick(src.Tick());
    CHECK(k.T0Counts() == 7 * 9600 && k.TimerCounts(0) == 7 * 2457600ull && k.TimerCounts(1) == 7 * 153600ull &&
          k.TimerCounts(3) == 7 * 262ull, "clocks: (1) T0..T3 counts = emu ticks x counts-per-tick[CLKS]");
    CHECK(k.VblankCount() == 7, "clocks: (2) vblank count == emu tick");
    CHECK(k.SyncVFieldParity() == 1, "clocks: (3) field parity == emu tick & 1");
    d.OnBoundary(src.RawCount());
    CHECK(k.FrameBoundaryCount() == 1 && k.BudgetAnchorCounts() == 7 * 9600, "clocks: (3)/(4) boundary count and the budget anchor in T0 counts");
    CHECK(k.Cop0Count() == 7 * 4915200ull, "clocks: (5) COP0 Count = emu ticks x 4915200 (EE core 294.912 MHz / 60)");
    const DerivedClocks::RtcTime t = k.Rtc();
    CHECK(t.year == 2005 && t.month == 6 && t.day == 15 && t.hour == 12 && t.minute == 0 && t.second == 0,
          "clocks: (6) fixed RTC reads 2005-06-15 12:00:00");
    k.SetRtcFixed(false);
    // Bring the domain to 3660 s + 53 ticks (< 1 s): +1 h 01 min exactly.
    while (d.DomainTick() < 60ull * 3660ull + 53ull) d.OnRawTick(src.Tick());
    const DerivedClocks::RtcTime t2 = k.Rtc();
    CHECK(t2.hour == 13 && t2.minute == 1 && t2.second == 0 && t2.day == 15, "clocks: (6) unpinned RTC advances by emu seconds only (13:01:00)");
    const DerivedClocks::RtcTime ny = DerivedClocks::EpochToCivil(946684799);  // 1999-12-31 23:59:59
    CHECK(ny.year == 1999 && ny.month == 12 && ny.day == 31 && ny.hour == 23 && ny.minute == 59 && ny.second == 59,
          "clocks: civil conversion (1999-12-31 23:59:59)");
    CHECK(DerivedClocks::HeartbeatTicksFor(16667) == 1 && DerivedClocks::HeartbeatTicksFor(33333) == 2 &&
          DerivedClocks::HeartbeatTicksFor(24000) == 1 && DerivedClocks::HeartbeatTicksFor(25001) == 2 &&
          DerivedClocks::HeartbeatTicksFor(1) == 1, "clocks: (7) heartbeat = round(usec/16667) ticks, minimum 1");
    CHECK(k.CdvdBudgetKbSince(k.CdvdEpochTick() - 3) == 3 * 256, "clocks: (8) CDVD refill budget 256 KB per epoch tick");
    CHECK(DerivedClocks::AudioRateMasterIsDevice(), "clocks: (9) audio: the device clock is the rate master, not derived");
    // Kernel install: KernlTimer reads T0 counts, SyscallTable alarms read emu ticks.
    KernlTimer::ResetForTest();
    int waits = 0;
    k.InstallKernelClocks(SyscallTable::Instance(), [&](uint64_t) { ++waits; d.OnRawTick(src.Tick()); });
    CHECK(KernlTimer::HasClock(), "clocks: InstallKernelClocks gives KernlTimer a clock");
    // The installed closures capture &k (a stack object here): drop them before
    // it goes out of scope so no later section reads a dangling clock.
    KernlTimer::ResetForTest();
    SyscallTable::Instance().SetTickClock(nullptr);
}

int main(int argc, char** argv) {
    const std::string section = argc > 1 ? argv[1] : "all";
    if (section == "domain" || section == "all") {
        RegimePins();
        BoundaryAdvancePins();
        EmuDomainRules();
        TickSourcePins();
    }
    if (section == "budget" || section == "all") {
        BudgetPins();
        PhaseTruthTable();
    }
    if (section == "clocks" || section == "all") ClocksPurity();
    std::printf("test_timing[%s]: %d checks, %d failures\n", section.c_str(), g_pass + g_fail, g_fail);
    return g_fail ? 1 : 0;
}
