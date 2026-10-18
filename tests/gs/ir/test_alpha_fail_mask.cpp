// tests/gs/ir/test_alpha_fail_mask.cpp -- AlphaFailMask: the TEST register's static
// alpha-test arm as a whole-draw write mask (GS User's Manual, TEST: ATE / ATST /
// AFAIL). Pins: ATE off and ATST ALWAYS leave everything writable (uniform); ATST
// NEVER applies AFAIL -- KEEP writes nothing (the draw is dropped), FB_ONLY no depth,
// ZB_ONLY no colour (the Z-clear idiom: a frame-covering sprite with ATST NEVER /
// AFAIL ZB_ONLY must leave the frame buffer untouched), RGB_ONLY no alpha and no
// depth; every per-pixel ATST (LESS .. NOTEQUAL) is reported non-uniform with
// everything writable, whatever AFAIL says -- and SplitsPopulations names exactly the
// per-pixel draws whose failing pixels still write (AFAIL != KEEP), FailingPixels their
// mask (the same table the NEVER arm applies).
#include <cstdio>

#include "engine/gs/hle/ir/alpha_fail_mask.h"

using ps2r::gs::AlphaFailMask;
using ps2r::gs::DrawIr;

namespace {

int g_checks = 0, g_failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            std::printf("  FAIL line %d: ", __LINE__);                \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

DrawIr::Test Test(bool ate, uint8_t atst, uint8_t afail) {
    DrawIr::Test t;
    t.ate = ate;
    t.atst = atst;
    t.afail = afail;
    t.aref = 0x7e;
    return t;
}

void ExpectOpen(const AlphaFailMask::Result& r, bool uniform, const char* what) {
    CHECK(r.uniform == uniform, "%s: uniform %d", what, r.uniform ? 1 : 0);
    CHECK(r.write_rgb && r.write_alpha && r.write_depth, "%s: not fully writable", what);
    CHECK(!r.WritesNothing(), "%s: WritesNothing", what);
}

}  // namespace

int main() {
    // All-pass arms: everything writable, settled per draw.
    for (uint8_t afail = 0; afail < 4; ++afail) {
        ExpectOpen(AlphaFailMask::Compute(Test(false, AlphaFailMask::kAtstNever, afail)), true, "ATE off");
        ExpectOpen(AlphaFailMask::Compute(Test(true, AlphaFailMask::kAtstAlways, afail)), true, "ATST ALWAYS");
    }
    // Per-pixel methods (2 LESS .. 7 NOTEQUAL): not settled here, nothing vetoed.
    for (uint8_t atst = 2; atst < 8; ++atst)
        for (uint8_t afail = 0; afail < 4; ++afail)
            ExpectOpen(AlphaFailMask::Compute(Test(true, atst, afail)), false, "per-pixel ATST");
    // All-fail: AFAIL names what is still updated.
    {
        const auto r = AlphaFailMask::Compute(Test(true, AlphaFailMask::kAtstNever, AlphaFailMask::kAfailKeep));
        CHECK(r.uniform, "KEEP uniform");
        CHECK(r.WritesNothing(), "KEEP writes nothing");
    }
    {
        const auto r = AlphaFailMask::Compute(Test(true, AlphaFailMask::kAtstNever, AlphaFailMask::kAfailFbOnly));
        CHECK(r.uniform && r.write_rgb && r.write_alpha && !r.write_depth, "FB_ONLY: colour yes, depth no (%d%d%d)",
              r.write_rgb, r.write_alpha, r.write_depth);
    }
    {
        // The Z-clear idiom: the frame buffer must stay untouched.
        const auto r = AlphaFailMask::Compute(Test(true, AlphaFailMask::kAtstNever, AlphaFailMask::kAfailZbOnly));
        CHECK(r.uniform && !r.write_rgb && !r.write_alpha && r.write_depth, "ZB_ONLY: depth yes, colour no (%d%d%d)",
              r.write_rgb, r.write_alpha, r.write_depth);
        CHECK(!r.WritesNothing(), "ZB_ONLY still draws");
    }
    {
        const auto r = AlphaFailMask::Compute(Test(true, AlphaFailMask::kAtstNever, AlphaFailMask::kAfailRgbOnly));
        CHECK(r.uniform && r.write_rgb && !r.write_alpha && !r.write_depth, "RGB_ONLY: rgb yes, alpha/depth no (%d%d%d)",
              r.write_rgb, r.write_alpha, r.write_depth);
    }
    // The two-population split: per-pixel ATST with an AFAIL that still writes.
    for (uint8_t atst = 0; atst < 8; ++atst)
        for (uint8_t afail = 0; afail < 4; ++afail) {
            const bool per_pixel = atst != AlphaFailMask::kAtstNever && atst != AlphaFailMask::kAtstAlways;
            const bool want = per_pixel && afail != AlphaFailMask::kAfailKeep;
            CHECK(AlphaFailMask::SplitsPopulations(Test(true, atst, afail)) == want, "SplitsPopulations atst %u afail %u",
                  atst, afail);
            CHECK(!AlphaFailMask::SplitsPopulations(Test(false, atst, afail)), "ATE off never splits (atst %u afail %u)",
                  atst, afail);
        }
    for (uint8_t afail = 0; afail < 4; ++afail) {
        const auto f = AlphaFailMask::FailingPixels(afail);
        const auto n = AlphaFailMask::Compute(Test(true, AlphaFailMask::kAtstNever, afail));
        CHECK(f.uniform && f.write_rgb == n.write_rgb && f.write_alpha == n.write_alpha && f.write_depth == n.write_depth,
              "FailingPixels(%u) == the NEVER arm", afail);
    }
    std::printf("AlphaFailMask: %d checks, %d failures -> %s\n", g_checks, g_failures, g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
