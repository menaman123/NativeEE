#pragma once
// EE FPU / VU "PS2 float" arithmetic rules.
//
// The PS2 float pipeline has no NaN and no Inf: an exponent of 0xFF is clamped
// to +-MAX_FLOAT (0x7F7FFFFF with the sign kept), so NaN x 0 == 0 as on the
// hardware. div.s / sqrt.s / rsqrt.s round to NEAREST while the EE thread
// otherwise runs FE_TOWARDZERO; the divide must be performed in SINGLE
// precision under that rounding mode (computing in double and truncating is
// not equivalent -- double rounding). The generated code calls the free
// functions ps2_float_clamp / ps2_div_s / ps2_sqrt_s / ps2_rsqrt_s.
#include "engine/core/types.h"

class Ps2Float {
public:
    static float Clamp(float v);
    static float DivS(float a, float b);
    static float SqrtS(float v);
    static float RsqrtS(float a, float b);

    // Rounding mode a guest EE thread runs under (FE_TOWARDZERO); applied by
    // the thread manager when it starts a guest thread.
    static void SetGuestThreadRounding();
};

// The ABI names the generated code uses (inline bodies in ps2_float.inl).
inline float ps2_float_clamp(float v);
inline float ps2_div_s(float a, float b);
inline float ps2_sqrt_s(float v);
inline float ps2_rsqrt_s(float a, float b);

#include "ps2_float.inl"
