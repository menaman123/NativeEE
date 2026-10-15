#pragma once
// Generated-code ABI forwarder (PHASE3_PLAN 2.9 / 3.4 rows 1, 2, 5): the frozen
// recompiler emits `#include "../host_app/cpu_state.h"` and expects the register
// file AND the PS2-float helpers (OLD declared ps2_float_clamp / ps2_div_s /
// ps2_sqrt_s / ps2_rsqrt_s in cpu_state.h:9-54) from it.
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/ps2_float.h"

// VU0 macro-mode vector arithmetic the generated VADD/VSUB/VMUL/VDIV sites
// spell as `VF[a] op VF[b]` (OLD cpu_state.h:82-109 declared these as member
// operators of VR_reg). Component-wise; every operand and the result pass
// through the PS2 float clamp (no NaN/Inf on the VU datapath).
inline VR_reg operator+(const VR_reg& a, const VR_reg& b) {
    VR_reg r{};   // zero-initialised: every F[i] is written below; GCC 13 -O3 otherwise reports the union as maybe-uninitialized
    for (int i = 0; i < 4; ++i) r.F[i] = ps2_float_clamp(ps2_float_clamp(a.F[i]) + ps2_float_clamp(b.F[i]));
    return r;
}
inline VR_reg operator-(const VR_reg& a, const VR_reg& b) {
    VR_reg r{};   // zero-initialised: every F[i] is written below; GCC 13 -O3 otherwise reports the union as maybe-uninitialized
    for (int i = 0; i < 4; ++i) r.F[i] = ps2_float_clamp(ps2_float_clamp(a.F[i]) - ps2_float_clamp(b.F[i]));
    return r;
}
inline VR_reg operator*(const VR_reg& a, const VR_reg& b) {
    VR_reg r{};   // zero-initialised: every F[i] is written below; GCC 13 -O3 otherwise reports the union as maybe-uninitialized
    for (int i = 0; i < 4; ++i) r.F[i] = ps2_float_clamp(ps2_float_clamp(a.F[i]) * ps2_float_clamp(b.F[i]));
    return r;
}
inline VR_reg operator/(const VR_reg& a, const VR_reg& b) {
    VR_reg r{};   // zero-initialised: every F[i] is written below; GCC 13 -O3 otherwise reports the union as maybe-uninitialized
    for (int i = 0; i < 4; ++i) r.F[i] = ps2_float_clamp(ps2_float_clamp(a.F[i]) / ps2_float_clamp(b.F[i]));
    return r;
}
