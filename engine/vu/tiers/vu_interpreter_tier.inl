// Inline bodies of VuInterpreterTier (included by vu_interpreter_tier.h).
#pragma once

// The four-lane clamp every GENERIC / special upper body reads its sources
// through (two to three per word); inline so the generated blocks' calls into
// those bodies do not pay a call per lane set.
inline VR_reg VuInterpreterTier::Clamp4(const VR_reg& v) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.I[k] = VuFloat::ClampBits1(v.I[k]);
    return r;
}
