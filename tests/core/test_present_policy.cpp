// tests/core/test_present_policy.cpp -- row 6.7: PresentPolicy's decision table.
#include <cstdio>

#include "engine/gs/hle/present/present_policy.h"

namespace {
int g_failures = 0;
#define CHECK(cond, what)                                                   \
    do {                                                                    \
        if (cond) std::printf("  PASS %s\n", what);                         \
        else { std::printf("  FAIL %s\n", what); ++g_failures; }            \
    } while (0)

using ps2r::gs::PresentPolicy;
using D = PresentPolicy::Decision;
}  // namespace

int main() {
    {
        PresentPolicy p;   // uncapped
        CHECK(p.Decide(false, false) == D::None && !p.HasImage(), "no image yet, nothing live: None");
        CHECK(p.Decide(false, true) == D::None, "no image yet during FMV: still None (no black frame invented)");
        CHECK(p.Decide(true, false) == D::Live && p.HasImage(), "the first live image: Live");
        CHECK(p.Decide(false, false) == D::Hold, "no new image after the first: Hold");
        CHECK(p.Decide(false, true) == D::Hold, "between FMV pictures: Hold");
        CHECK(p.Decide(true, true) == D::Live, "an FMV picture on the GS: Live");
        CHECK(p.Decide(true, false) == D::Live, "every live field uncapped: Live");
        CHECK(p.Lives() == 3 && p.Holds() == 2 && p.Nones() == 2 && p.Boundaries() == 7 && p.FmvBoundaries() == 3,
              "counters: live=3 hold=2 none=2, fmv boundaries=3");
        CHECK(p.Census() == "live=3,hold=2,none=2,cap=0,cap_skips=0,fmv=3", "census string shape");
    }
    {
        PresentPolicy p;
        p.SetFpsCap(30);
        int lives = 0, holds = 0;
        for (int i = 0; i < 60; ++i) {
            const D d = p.Decide(true, false);
            if (d == D::Live) ++lives;
            if (d == D::Hold) ++holds;
        }
        CHECK(lives == 30 && holds == 30, "fps_cap 30 over 60 live fields: 30 Live, 30 Hold (an even share)");
        CHECK(p.CapSkips() == 30, "cap_skips counts the fields the cap turned into Holds");
    }
    {
        PresentPolicy p;
        p.SetFpsCap(20);
        int lives = 0;
        for (int i = 0; i < 60; ++i) lives += p.Decide(true, false) == D::Live;
        CHECK(lives == 20, "fps_cap 20: 20 of 60 live fields shown");
        PresentPolicy q;
        q.SetFpsCap(1);
        CHECK(q.Decide(true, false) == D::Live, "the first image is always shown, whatever the cap");
        CHECK(q.Decide(true, false) == D::Hold, "then the cap takes its share");
    }
    {
        PresentPolicy p;
        p.SetFpsCap(60);
        CHECK(p.FpsCap() == 0, "a cap at or above the display rate is uncapped (0)");
        p.SetFpsCap(144);
        CHECK(p.FpsCap() == 0, "a cap above the display rate is uncapped (0)");
        int lives = 0;
        for (int i = 0; i < 10; ++i) lives += p.Decide(true, false) == D::Live;
        CHECK(lives == 10, "uncapped: every live field is Live");
    }
    std::printf("PresentPolicyTable: %s\n", g_failures ? "FAIL" : "ALL PASS");
    return g_failures ? 1 : 0;
}
