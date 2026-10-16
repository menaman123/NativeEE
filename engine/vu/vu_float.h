#pragma once
// The VU's single-precision arithmetic rules, single-sourced for every tier
// and for the VU1 HLE library.
//
// PS2 rules carried here:
//  * the VU has no NaN/Inf/denormal: a result whose exponent is all-ones is
//    clamped to +/-MAX_FLOAT (sign kept) and a denormal is flushed to +/-0;
//  * MAC flags (Z/S/U/O per lane) are judged on the RAW result bits before the
//    clamp: S = sign bit (so -0 sets S), Z = exponent zero (true zero AND
//    denormal), U = denormal, O = exponent all-ones;
//  * MAX/MINI compare the raw bit patterns as signed integers (no clamp, no
//    flags), which orders -0 below +0;
//  * FDIV by zero yields +/-MAX_FLOAT with the sign = XOR of the operand
//    signs; FTOI saturates at +/-2^31 by the sign of the (scaled) source and
//    otherwise truncates toward zero; ITOF converts the raw integer.
// All float<->bits conversions go through memcpy. Never use `Ps2Float` here
// (its clamp keeps denormals and its divide switches rounding mode).
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

// The VU lane kernels are NEON under PS2R_SIMD on AArch64 (the project's
// opt-in; every NEON kernel is the scalar kernel's twin, proven by the
// side-by-side tests and the capture corpus) and scalar otherwise.
#if defined(PS2R_SIMD) && PS2R_SIMD && defined(__aarch64__)
#include <arm_neon.h>
#define PS2R_VU_NEON 1
#else
#define PS2R_VU_NEON 0
#endif

class VuFloat {
public:
    using MinMaxFn = uint32_t (*)(uint32_t, uint32_t);

    // Lane select table indexed by the 4-bit dest mask (bit3=X -> lane 0 ...
    // bit0=W -> lane 3); each entry is all-ones for a selected lane.
    alignas(16) static const uint32_t kLaneMask[16][4];

    static constexpr uint32_t kMaxFloatBits = 0x7F7FFFFFu;
    static constexpr uint32_t kSignBit = 0x80000000u;
    static constexpr uint32_t kExpMask = 0x7F800000u;
    static constexpr uint32_t kMantMask = 0x007FFFFFu;

    static float Bits2F(uint32_t bits);
    static uint32_t F2Bits(float f);

    // Clamp: NaN/Inf -> +/-MAX_FLOAT, denormal -> +/-0, else unchanged.
    static uint32_t ClampBits1(uint32_t bits);
    // True when ClampBits1 is the identity for sure (exponent neither 0 nor
    // all-ones); a true zero also clamps to itself but is not claimed here.
    static bool IsNormalExp(uint32_t bits);
    static float ClampFloat(float v);
    // ACC is re-clamped in place before every upper instruction.
    static void ClampStoreACC(VR_reg& acc);
    // dst lanes selected by `mask` take the clamped result bits.
    static void WriteMaskClamp(VR_reg& dst, const uint32_t result_bits[4], uint8_t mask);
    // The four lanes clamped (an FMAC source copy).
    static VR_reg ClampVec(const VR_reg& v);

    // Four-lane arithmetic, each lane the scalar IEEE operation under the
    // caller's rounding mode: a+b, a-b, a*b, fma(a,b,c) and fma(-a,b,c)
    // (single rounding, the MADD/MSUB family), and the broadcast of one lane.
    static VR_reg Add4(const VR_reg& a, const VR_reg& b);
    static VR_reg Sub4(const VR_reg& a, const VR_reg& b);
    static VR_reg Mul4(const VR_reg& a, const VR_reg& b);
    static VR_reg Fma4(const VR_reg& a, const VR_reg& b, const VR_reg& c);
    static VR_reg Fms4(const VR_reg& a, const VR_reg& b, const VR_reg& c);
    static VR_reg Splat4(float v);
    // MAX / MINI over four raw lanes (FpMax / FpMin per lane) written to the
    // dest lanes `mask` selects; a VF00 destination is left alone.
    static void ApplyMinMax4(VR_reg& dest, const VR_reg& fs_raw, const VR_reg& ft_raw, uint8_t mask, bool protect_vf0,
                             bool is_max);
    // FTOIn: raw lanes * scale then FtoiSat; ITOFn: raw integer lanes -> float / scale.
    static VR_reg Ftoi4Scaled(const VR_reg& src, float scale);
    static VR_reg Itof4Scaled(const VR_reg& src, float scale);

    // MAC flag nibble bits for one lane (`field`: x=3, y=2, z=1, w=0) from the
    // raw result bits, and the 16-bit pack over the lanes `mask` selects.
    static uint16_t MacFlags1(uint32_t bits, int field);
    static uint16_t PackMacBits(const uint32_t bits[4], uint8_t mask);

    // The FDIV unit's D/I (bits 4-5) replace, DS/IS (bits 10-11) OR in.
    static void CommitQStatus(VUregs& regs, uint16_t q_status);

    // Integer-compare MAX/MINI on raw bits.
    static uint32_t FpMax(uint32_t a, uint32_t b);
    static uint32_t FpMin(uint32_t a, uint32_t b);
    static void ApplyMinMaxRaw(VR_reg& dest, const VR_reg& fs_raw, const uint32_t ft_raw[4], uint8_t mask,
                               bool protect_vf0, MinMaxFn fn);

    // Divide with the VU's by-zero rule; operands and quotient clamped.
    static float VuDiv(float num, float den);

    // Float -> int32 with the VU saturation rule (source already scaled).
    static uint32_t FtoiSat(float f);
    // FTOI0 of a clamped source lane (the HLE library form; equals the raw
    // form bit-for-bit for scale 1). FTOI4/12/15 scale the RAW source lane.
    static uint32_t Ftoi0(uint32_t lane_bits);
    static uint32_t Ftoi4(uint32_t lane_bits);
    static uint32_t Ftoi12(uint32_t lane_bits);
    static uint32_t Ftoi15(uint32_t lane_bits);
    // ITOF0 of a raw integer lane (no clamp).
    static uint32_t Itof0(uint32_t lane_bits);

    // The CLIP judgement on raw lane bits: the 6-bit outside mask (+x,-x,+y,
    // -y,+z,-z) from signed-integer compares against |w| (a zero/denormal w
    // compares as the largest denormal).
    static uint32_t ClipNf(uint32_t xb, uint32_t yb, uint32_t zb, uint32_t wb);

#if PS2R_VU_NEON
    static uint32x4_t ClampBits4(uint32x4_t bits);
    static uint16_t MacFlags4(uint32x4_t bits, uint8_t mask);
#endif
};

#include "engine/vu/vu_float.inl"
