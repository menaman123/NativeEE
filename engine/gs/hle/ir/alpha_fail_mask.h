// gs/hle/ir/alpha_fail_mask.h -- AlphaFailMask: the whole-draw write mask the TEST
// register's alpha test settles STATICALLY.
//
// GS User's Manual, TEST register: when ATE = 1 every pixel's alpha is compared against
// AREF by ATST; a pixel that FAILS is handled by AFAIL -- KEEP (0) updates neither the
// frame buffer nor the Z buffer, FB_ONLY (1) updates the frame buffer only, ZB_ONLY (2)
// the Z buffer only, RGB_ONLY (3) the RGB of the frame buffer only (no alpha, no Z).
// Two methods decide the SAME way for every pixel of the draw: ATST = NEVER (0) fails
// all of them, ATST = ALWAYS (1) passes all of them (as does ATE = 0). For those the
// AFAIL arm is a per-draw write mask that composes with FBMSK / ZMSK -- the GS idiom
// "fail every pixel, Z only" is a Z clear, "fail every pixel, frame buffer only" a
// colour-only pass. Every other ATST is per pixel: the draw then has TWO populations,
// the passing pixels (FBMSK / ZMSK writes) and the failing ones (the AFAIL mask), which
// a renderer without a per-fragment depth mask draws as two passes (`SplitsPopulations`,
// `FailingPixels`); AFAIL KEEP alone needs no second pass (its failing pixels write
// nothing: the shader discards them).
#pragma once

#include "engine/gs/hle/ir/draw_ir.h"

namespace ps2r {
namespace gs {

class AlphaFailMask {
public:
    struct Result {
        bool uniform = false;     // the test's outcome is the same for every pixel of the draw
        bool write_rgb = true;    // the frame buffer's RGB may be updated
        bool write_alpha = true;  // the frame buffer's alpha may be updated
        bool write_depth = true;  // the Z buffer may be updated
        bool WritesNothing() const { return !write_rgb && !write_alpha && !write_depth; }
    };

    static constexpr uint8_t kAtstNever = 0, kAtstAlways = 1;
    static constexpr uint8_t kAfailKeep = 0, kAfailFbOnly = 1, kAfailZbOnly = 2, kAfailRgbOnly = 3;

    // The static arm for `t`: all-pass (ATE = 0 / ATST = ALWAYS) leaves everything
    // writable; all-fail (ATST = NEVER) applies AFAIL; a per-pixel ATST is reported
    // non-uniform with everything writable (the shader decides per fragment).
    static Result Compute(const DrawIr::Test& t);
    // The write mask of the pixels that FAIL the test under `afail` (uniform = true).
    static Result FailingPixels(uint8_t afail);
    // A per-pixel ATST (not NEVER / ALWAYS, ATE on) whose AFAIL is not KEEP: the failing
    // pixels still update something, under a mask the passing pixels do not share.
    static bool SplitsPopulations(const DrawIr::Test& t);
};

}  // namespace gs
}  // namespace ps2r
