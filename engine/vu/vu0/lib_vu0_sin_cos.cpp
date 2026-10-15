// LibVu0SinCos -- fingerprint match + the host polynomial substitution. Rules in the header.
#include "engine/vu/vu0/lib_vu0_sin_cos.h"

#include <cstring>

#include "engine/platform/host/platform.h"
#include "engine/vu/vu_float.h"

// SineCosine runs under the caller's chop guard: every FP operation depends on
// the dynamic rounding mode (no folding, no hoisting across fesetround).
PS2R_FENV_ACCESS_ON

namespace {

uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

}  // namespace

bool LibVu0SinCos::Matches(const uint8_t* micro, size_t micro_size, uint32_t pc) {
    if (pc != kEntryPc || micro_size < kEntryPc + 3 * 8) return false;
    for (int i = 0; i < 3; ++i) {
        const uint8_t* b = micro + kEntryPc + i * 8;
        if (Le32(b) != kFingerprint[i][0] || Le32(b + 4) != kFingerprint[i][1]) return false;
    }
    return true;
}

bool LibVu0SinCos::TryExecute(const uint8_t* micro, size_t micro_size, VUregs& regs, uint32_t pc) {
    if (!Matches(micro, micro_size, pc)) return false;
    const float a0 = VuFloat::ClampFloat(regs.VF[20].x);
    const float a1 = VuFloat::ClampFloat(regs.VF[20].z);
    float s0, c0, s1, c1;
    SineCosine(a0, s0, c0);
    SineCosine(a1, s1, c1);
    regs.VF[31].x = VuFloat::ClampFloat(s0);
    regs.VF[31].y = VuFloat::ClampFloat(c0);
    regs.VF[31].z = VuFloat::ClampFloat(s1);
    regs.VF[31].w = VuFloat::ClampFloat(c1);
    regs.VF[0].x = 0.0f;
    regs.VF[0].y = 0.0f;
    regs.VF[0].z = 0.0f;
    regs.VF[0].w = 1.0f;
    regs.VI[0] = 0;
    regs.TPC = kExitTpc;
    return true;
}
