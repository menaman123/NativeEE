// gs/hle/ir/alpha_fail_mask.cpp -- AlphaFailMask (see alpha_fail_mask.h).
#include "engine/gs/hle/ir/alpha_fail_mask.h"

namespace ps2r {
namespace gs {

AlphaFailMask::Result AlphaFailMask::Compute(const DrawIr::Test& t) {
    Result r;
    if (!t.ate || t.atst == kAtstAlways) {
        r.uniform = true;   // every pixel passes: the usual FBMSK / ZMSK-governed writes
        return r;
    }
    if (t.atst != kAtstNever) return r;   // per-pixel outcome: not settled here
    return FailingPixels(t.afail);        // every pixel fails: AFAIL names what is still updated
}

AlphaFailMask::Result AlphaFailMask::FailingPixels(uint8_t afail) {
    Result r;
    r.uniform = true;
    switch (afail) {
        case kAfailFbOnly:
            r.write_depth = false;
            break;
        case kAfailZbOnly:
            r.write_rgb = false;
            r.write_alpha = false;
            break;
        case kAfailRgbOnly:
            r.write_alpha = false;
            r.write_depth = false;
            break;
        case kAfailKeep:
        default:
            r.write_rgb = false;
            r.write_alpha = false;
            r.write_depth = false;
            break;
    }
    return r;
}

bool AlphaFailMask::SplitsPopulations(const DrawIr::Test& t) {
    return t.ate && t.atst != kAtstNever && t.atst != kAtstAlways && t.afail != kAfailKeep;
}

}  // namespace gs
}  // namespace ps2r
