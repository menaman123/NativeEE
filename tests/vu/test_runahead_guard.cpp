// VuRunaheadGuard: the runahead bound -- depth clamp math, the over/edge
// threshold (depth > cap+1), the 30-present persistence window (real hitches
// never alarm), the edge-triggered latch (one report per episode), recovery
// re-arm, the 5-episode report cap, the cap-0 ratchet counterfactual (MUST
// alarm), immediate cap changes; plus EffMaxFrames (2 lifts to 3 under the
// injected low-band predicate only), Over(), and the 100 ms frame valve on a
// fake clock.
#include <cstdint>
#include <cstdio>

#include "engine/vu/tiers/mtvu_clock.h"
#include "engine/vu/tiers/runahead_guard.h"

namespace {

int g_failures = 0;
#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

using Tripwire = RunaheadGuard::Tripwire;

struct FakeClock : MtvuClock {
    uint64_t now = 1'000'000'000;
    uint64_t NowNs() const override { return now; }
};

}  // namespace

int main() {
    // 1. depth clamp math.
    CHECK(RunaheadGuard::DepthFrames(10, 8) == 2, "simple depth = declared - presented");
    CHECK(RunaheadGuard::DepthFrames(8, 8) == 0, "equal counters = depth 0");
    CHECK(RunaheadGuard::DepthFrames(7, 8) == 0, "presented ahead of declared clamps to 0, never wraps");
    CHECK(RunaheadGuard::DepthFrames(0, 0) == 0, "boot state = depth 0");

    // 2. threshold edge: over means depth > cap+1.
    {
        Tripwire t;
        for (uint64_t d = 0; d <= 3; ++d) {
            const Tripwire::Result r = t.Feed(d, 2);
            CHECK(!r.over && !r.fire, "cap 2: depth 0..3 is in-bound");
        }
        CHECK(t.Feed(4, 2).over, "cap 2: depth 4 is over the bound");
    }

    // 3. steady healthy stream never alarms.
    {
        Tripwire t;
        bool fired = false;
        for (int i = 0; i < 5000; ++i)
            if (t.Feed(static_cast<uint64_t>(i % 3), 2).fire) fired = true;
        CHECK(!fired, "steady depth <= 2 at cap 2: no alarm over 5000 presents");
        CHECK(t.reports == 0, "no episodes reported");
    }

    // 4. persistence window: a hitch of exactly the window length never alarms.
    {
        Tripwire t;
        bool fired = false;
        for (uint32_t i = 0; i < Tripwire::kPersistPresents; ++i)
            if (t.Feed(5, 2).fire) fired = true;
        CHECK(!fired, "30 over-presents (== window) do not alarm");
        CHECK(!t.Feed(2, 2).fire, "recovery present does not alarm");
        CHECK(t.over_run == 0, "recovery resets the window");
        for (int h = 0; h < 20; ++h) {
            for (uint32_t i = 0; i < Tripwire::kPersistPresents; ++i)
                if (t.Feed(9, 2).fire) fired = true;
            if (t.Feed(1, 2).fire) fired = true;
        }
        CHECK(!fired, "repeated window-length hitches never alarm");
    }

    // 5. a standing violation fires once, on the 31st over-present.
    {
        Tripwire t;
        for (uint32_t i = 0; i < Tripwire::kPersistPresents; ++i) CHECK(!t.Feed(6, 2).fire, "no fire inside the window");
        const Tripwire::Result r = t.Feed(7, 2);
        CHECK(r.fire, "31st consecutive over-present fires");
        CHECK(r.peak == 7, "episode peak depth reported");
        CHECK(r.run == Tripwire::kPersistPresents + 1, "overrun length reported");
        bool refired = false;
        for (int i = 0; i < 200; ++i)
            if (t.Feed(8, 2).fire) refired = true;
        CHECK(!refired, "continuing episode does not re-fire");
        CHECK(t.reports == 1, "exactly one report for the episode");
    }

    // 6. the cap-0 ratchet MUST alarm exactly once.
    {
        Tripwire t;
        uint32_t fires = 0;
        uint64_t peak = 0;
        uint64_t depth = 0;
        for (int present = 0; present < 200; ++present) {
            if (depth < 54) ++depth;
            const Tripwire::Result r = t.Feed(depth, 0);
            if (r.fire) {
                ++fires;
                peak = r.peak;
            }
        }
        CHECK(fires == 1, "ratchet class alarms exactly once");
        CHECK(peak >= 2 + Tripwire::kPersistPresents, "reported peak reflects the ratchet at fire time");
    }

    // 7. recovery re-arms; the report cap bounds the flood.
    {
        Tripwire t;
        uint32_t fires = 0;
        for (int ep = 0; ep < 8; ++ep) {
            for (uint32_t i = 0; i < Tripwire::kPersistPresents + 5; ++i)
                if (t.Feed(10, 2).fire) ++fires;
            t.Feed(0, 2);
        }
        CHECK(fires == Tripwire::kMaxReports, "episode reports are hard-capped at kMaxReports");
    }

    // 8. cap changes take effect immediately.
    {
        Tripwire t;
        CHECK(!t.Feed(3, 2).over, "depth 3 in-bound at cap 2");
        CHECK(t.Feed(3, 1).over, "depth 3 over at cap 1");
    }

    // 9. EffMaxFrames: the setting, lifted 2 -> 3 only while low_band holds.
    {
        bool low = false;
        auto low_band = [&low] { return low; };
        RunaheadGuard g2(2, low_band);
        CHECK(g2.DepthFramesSetting() == 2 && g2.EffMaxFrames() == 2, "depth 2, low band off -> 2");
        low = true;
        CHECK(g2.EffMaxFrames() == 3, "depth 2, low band on -> lifted to 3");
        RunaheadGuard g0(0, low_band), g1(1, low_band), g3(3, low_band), g4(4, low_band);
        CHECK(g0.EffMaxFrames() == 0 && g1.EffMaxFrames() == 1 && g3.EffMaxFrames() == 3 && g4.EffMaxFrames() == 4,
              "other depths unchanged under low band");
        RunaheadGuard gnull(2);
        CHECK(gnull.EffMaxFrames() == 2, "no predicate: never lifted");
        RunaheadGuard gneg(-5);
        CHECK(gneg.DepthFramesSetting() == 0 && gneg.EffMaxFrames() == 0, "negative depth clamps to 0");
    }

    // 10. Over(): cap armed and depth above it; cap 0 never.
    {
        RunaheadGuard g2(2);
        CHECK(!g2.Over(2, 0) && g2.Over(3, 0) && !g2.Over(5, 3) && g2.Over(10, 7), "cap 2 park condition");
        RunaheadGuard g0(0);
        CHECK(!g0.Over(50, 0), "cap 0: the frame park is never armed");
        bool low = true;
        RunaheadGuard lifted(2, [&low] { return low; });
        CHECK(!lifted.Over(3, 0) && lifted.Over(4, 0), "lifted cap 3 park condition");
    }

    // 11. FramePark on a fake clock: parks while over; the valve releases at +100 ms.
    {
        FakeClock clock;
        RunaheadGuard g(2, {}, clock);
        const uint64_t t0 = clock.now;
        RunaheadFramePark r = g.FramePark(2, 0, t0);
        CHECK(!r.park && !r.valve_hit, "depth == cap: no park");
        r = g.FramePark(10, 0, t0);
        CHECK(r.park && !r.valve_hit, "depth over cap at t0: park");
        clock.now = t0 + RunaheadGuard::kFrameValveNs - 1;
        r = g.FramePark(10, 0, t0);
        CHECK(r.park && !r.valve_hit, "just before the valve: still parked");
        clock.now = t0 + RunaheadGuard::kFrameValveNs;
        r = g.FramePark(10, 0, t0);
        CHECK(!r.park && r.valve_hit, "at +100 ms the valve releases the frozen depth");
        clock.now = t0 + 5 * RunaheadGuard::kFrameValveNs;
        r = g.FramePark(10, 9, t0);
        CHECK(!r.park && !r.valve_hit, "presents caught up: released by progress, no valve");
        CHECK(g.NowNs() == clock.now, "the guard reads the injected clock");
        RunaheadGuard g0(0, {}, clock);
        CHECK(!g0.FramePark(100, 0, t0).park && !g0.FramePark(100, 0, t0).valve_hit, "cap 0: never parks");
    }

    if (g_failures == 0) {
        std::printf("test_runahead_guard: ALL PASS\n");
        return 0;
    }
    std::printf("test_runahead_guard: %d FAILURE(S)\n", g_failures);
    return 1;
}
