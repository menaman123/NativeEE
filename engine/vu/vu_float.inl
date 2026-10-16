// vu_float.inl -- the hot scalar helpers of VuFloat, inlined for the tiers.
// Included from vu_float.h only.
#include <cmath>
#include <cstring>

#include "engine/platform/host/platform.h"

PS2R_FORCE_INLINE float VuFloat::Bits2F(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

PS2R_FORCE_INLINE uint32_t VuFloat::F2Bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

PS2R_FORCE_INLINE uint32_t VuFloat::ClampBits1(uint32_t bits) {
    const uint32_t exp = bits & kExpMask;
    if (exp == kExpMask) return (bits & kSignBit) | kMaxFloatBits;      // NaN / Inf -> +/-MAX
    if (exp == 0 && (bits & kMantMask) != 0) return bits & kSignBit;    // denormal -> +/-0
    return bits;
}

PS2R_FORCE_INLINE float VuFloat::ClampFloat(float v) {
    return Bits2F(ClampBits1(F2Bits(v)));
}

PS2R_FORCE_INLINE uint16_t VuFloat::MacFlags1(uint32_t bits, int field) {
    uint16_t flags = 0;
    const uint32_t exp = (bits >> 23) & 0xFFu;
    if (bits & kSignBit) flags |= static_cast<uint16_t>(1u << (field + 4));   // S from the raw sign, -0 included
    if (exp == 0) {
        flags |= static_cast<uint16_t>(1u << field);                            // Z: true zero and denormal
        if (bits & kMantMask) flags |= static_cast<uint16_t>(1u << (field + 8));  // U: denormal only
    } else if (exp == 0xFFu) {
        flags |= static_cast<uint16_t>(1u << (field + 12));                     // O: Inf / NaN pattern
    }
    return flags;
}

// Exponent test for a lane that needs no clamp: exp in 1..254 (neither 0 nor
// all-ones) -- one unsigned compare on the exponent field.
PS2R_FORCE_INLINE bool VuFloat::IsNormalExp(uint32_t bits) {
    return static_cast<uint32_t>((bits & kExpMask) - 0x00800000u) < 0x7F000000u;
}

#if PS2R_VU_NEON
PS2R_FORCE_INLINE void VuFloat::ClampStoreACC(VR_reg& acc) {
    vst1q_u32(acc.I, ClampBits4(vld1q_u32(acc.I)));
}

PS2R_FORCE_INLINE void VuFloat::WriteMaskClamp(VR_reg& dst, const uint32_t result_bits[4], uint8_t mask) {
    const uint32x4_t lm = vld1q_u32(kLaneMask[mask & 0xF]);
    vst1q_u32(dst.I, vbslq_u32(lm, ClampBits4(vld1q_u32(result_bits)), vld1q_u32(dst.I)));
}

PS2R_FORCE_INLINE uint16_t VuFloat::PackMacBits(const uint32_t bits[4], uint8_t mask) {
    return MacFlags4(vld1q_u32(bits), mask);
}

PS2R_FORCE_INLINE VR_reg VuFloat::ClampVec(const VR_reg& v) {
    VR_reg r;
    vst1q_u32(r.I, ClampBits4(vld1q_u32(v.I)));
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Add4(const VR_reg& a, const VR_reg& b) {
    VR_reg r;
    vst1q_f32(r.F, vaddq_f32(vld1q_f32(a.F), vld1q_f32(b.F)));
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Sub4(const VR_reg& a, const VR_reg& b) {
    VR_reg r;
    vst1q_f32(r.F, vsubq_f32(vld1q_f32(a.F), vld1q_f32(b.F)));
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Mul4(const VR_reg& a, const VR_reg& b) {
    VR_reg r;
    vst1q_f32(r.F, vmulq_f32(vld1q_f32(a.F), vld1q_f32(b.F)));
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Fma4(const VR_reg& a, const VR_reg& b, const VR_reg& c) {
    VR_reg r;
    vst1q_f32(r.F, vfmaq_f32(vld1q_f32(c.F), vld1q_f32(a.F), vld1q_f32(b.F)));  // c + a*b, one rounding
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Fms4(const VR_reg& a, const VR_reg& b, const VR_reg& c) {
    VR_reg r;
    vst1q_f32(r.F, vfmsq_f32(vld1q_f32(c.F), vld1q_f32(a.F), vld1q_f32(b.F)));  // c - a*b, one rounding = fma(-a, b, c)
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Splat4(float v) {
    VR_reg r;
    vst1q_f32(r.F, vdupq_n_f32(v));
    return r;
}
PS2R_FORCE_INLINE void VuFloat::ApplyMinMax4(VR_reg& dest, const VR_reg& fs_raw, const VR_reg& ft_raw, uint8_t mask,
                                             bool protect_vf0, bool is_max) {
    if (protect_vf0) return;
    const int32x4_t a = vreinterpretq_s32_u32(vld1q_u32(fs_raw.I));
    const int32x4_t b = vreinterpretq_s32_u32(vld1q_u32(ft_raw.I));
    const uint32x4_t both_neg = vandq_u32(vcltzq_s32(a), vcltzq_s32(b));  // two negatives order the other way
    const int32x4_t mx = vmaxq_s32(a, b), mn = vminq_s32(a, b);
    const int32x4_t out = is_max ? vbslq_s32(both_neg, mn, mx) : vbslq_s32(both_neg, mx, mn);
    const uint32x4_t lm = vld1q_u32(kLaneMask[mask & 0xF]);
    vst1q_u32(dest.I, vbslq_u32(lm, vreinterpretq_u32_s32(out), vld1q_u32(dest.I)));
}

PS2R_FORCE_INLINE VR_reg VuFloat::Ftoi4Scaled(const VR_reg& src, float scale) {
    const float32x4_t f = vmulq_f32(vld1q_f32(src.F), vdupq_n_f32(scale));
    const uint32x4_t ub = vreinterpretq_u32_f32(f);
    // |f| >= 2^31 or NaN: saturate by the sign; else truncation toward zero (FCVTZS).
    const uint32x4_t big = vcgeq_u32(vandq_u32(ub, vdupq_n_u32(kExpMask)), vdupq_n_u32(0x4F000000u));
    const uint32x4_t neg = vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(ub), 31));
    const uint32x4_t sat = vbslq_u32(neg, vdupq_n_u32(0x80000000u), vdupq_n_u32(0x7FFFFFFFu));
    VR_reg r;
    vst1q_u32(r.I, vbslq_u32(big, sat, vreinterpretq_u32_s32(vcvtq_s32_f32(f))));
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Itof4Scaled(const VR_reg& src, float scale) {
    VR_reg r;
    vst1q_f32(r.F, vdivq_f32(vcvtq_f32_s32(vreinterpretq_s32_u32(vld1q_u32(src.I))), vdupq_n_f32(scale)));
    return r;
}
#else
PS2R_FORCE_INLINE void VuFloat::ClampStoreACC(VR_reg& acc) {
    if (IsNormalExp(acc.I[0]) && IsNormalExp(acc.I[1]) && IsNormalExp(acc.I[2]) && IsNormalExp(acc.I[3])) return;
    for (int k = 0; k < 4; ++k) acc.I[k] = ClampBits1(acc.I[k]);
}

PS2R_FORCE_INLINE void VuFloat::WriteMaskClamp(VR_reg& dst, const uint32_t result_bits[4], uint8_t mask) {
    if (mask & 8) dst.I[0] = ClampBits1(result_bits[0]);
    if (mask & 4) dst.I[1] = ClampBits1(result_bits[1]);
    if (mask & 2) dst.I[2] = ClampBits1(result_bits[2]);
    if (mask & 1) dst.I[3] = ClampBits1(result_bits[3]);
}

PS2R_FORCE_INLINE uint16_t VuFloat::PackMacBits(const uint32_t bits[4], uint8_t mask) {
    uint16_t m = 0;
    if (mask & 8) m |= MacFlags1(bits[0], 3);
    if (mask & 4) m |= MacFlags1(bits[1], 2);
    if (mask & 2) m |= MacFlags1(bits[2], 1);
    if (mask & 1) m |= MacFlags1(bits[3], 0);
    return m;
}

PS2R_FORCE_INLINE VR_reg VuFloat::ClampVec(const VR_reg& v) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.I[k] = ClampBits1(v.I[k]);
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Add4(const VR_reg& a, const VR_reg& b) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.F[k] = a.F[k] + b.F[k];
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Sub4(const VR_reg& a, const VR_reg& b) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.F[k] = a.F[k] - b.F[k];
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Mul4(const VR_reg& a, const VR_reg& b) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.F[k] = a.F[k] * b.F[k];
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Fma4(const VR_reg& a, const VR_reg& b, const VR_reg& c) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.F[k] = std::fmaf(a.F[k], b.F[k], c.F[k]);
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Fms4(const VR_reg& a, const VR_reg& b, const VR_reg& c) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.F[k] = std::fmaf(-a.F[k], b.F[k], c.F[k]);
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Splat4(float v) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.F[k] = v;
    return r;
}
PS2R_FORCE_INLINE void VuFloat::ApplyMinMax4(VR_reg& dest, const VR_reg& fs_raw, const VR_reg& ft_raw, uint8_t mask,
                                             bool protect_vf0, bool is_max) {
    if (protect_vf0) return;
    uint32_t out[4];
    for (int k = 0; k < 4; ++k) out[k] = is_max ? FpMax(fs_raw.I[k], ft_raw.I[k]) : FpMin(fs_raw.I[k], ft_raw.I[k]);
    if (mask & 8) dest.I[0] = out[0];
    if (mask & 4) dest.I[1] = out[1];
    if (mask & 2) dest.I[2] = out[2];
    if (mask & 1) dest.I[3] = out[3];
}

PS2R_FORCE_INLINE VR_reg VuFloat::Ftoi4Scaled(const VR_reg& src, float scale) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.I[k] = FtoiSat(src.F[k] * scale);
    return r;
}

PS2R_FORCE_INLINE VR_reg VuFloat::Itof4Scaled(const VR_reg& src, float scale) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.F[k] = static_cast<float>(static_cast<int32_t>(src.I[k])) / scale;
    return r;
}
#endif

PS2R_FORCE_INLINE uint32_t VuFloat::FpMax(uint32_t a, uint32_t b) {
    const int32_t sa = static_cast<int32_t>(a), sb = static_cast<int32_t>(b);
    if (sa < 0 && sb < 0) return static_cast<uint32_t>(sa < sb ? sa : sb);
    return static_cast<uint32_t>(sa > sb ? sa : sb);
}

PS2R_FORCE_INLINE uint32_t VuFloat::FpMin(uint32_t a, uint32_t b) {
    const int32_t sa = static_cast<int32_t>(a), sb = static_cast<int32_t>(b);
    if (sa < 0 && sb < 0) return static_cast<uint32_t>(sa > sb ? sa : sb);
    return static_cast<uint32_t>(sa < sb ? sa : sb);
}

PS2R_FORCE_INLINE uint32_t VuFloat::FtoiSat(float f) {
    const uint32_t ub = F2Bits(f);
    if ((ub & kExpMask) >= 0x4F000000u) return (ub & kSignBit) ? 0x80000000u : 0x7FFFFFFFu;  // |f| >= 2^31, NaN
    return static_cast<uint32_t>(static_cast<int32_t>(f));                                    // truncation toward zero
}

inline uint32_t VuFloat::Itof0(uint32_t lane_bits) {
    return F2Bits(static_cast<float>(static_cast<int32_t>(lane_bits)));
}

#if PS2R_VU_NEON
PS2R_FORCE_INLINE uint32x4_t VuFloat::ClampBits4(uint32x4_t bits) {
    // On the magnitude m = bits & 0x7FFFFFFF: exponent all-ones <=> m >= 0x7F800000;
    // denormal (exponent 0, mantissa != 0) <=> 1 <= m <= 0x007FFFFF <=> (m - 1) <u 0x007FFFFF.
    const uint32x4_t m = vandq_u32(bits, vdupq_n_u32(~kSignBit));
    const uint32x4_t sign = vandq_u32(bits, vdupq_n_u32(kSignBit));
    const uint32x4_t is_naninf = vcgeq_u32(m, vdupq_n_u32(kExpMask));
    const uint32x4_t is_denorm = vcltq_u32(vsubq_u32(m, vdupq_n_u32(1u)), vdupq_n_u32(kMantMask));
    uint32x4_t r = vbslq_u32(is_naninf, vorrq_u32(sign, vdupq_n_u32(kMaxFloatBits)), bits);
    r = vbslq_u32(is_denorm, sign, r);
    return r;
}

PS2R_FORCE_INLINE uint16_t VuFloat::MacFlags4(uint32x4_t bits, uint8_t mask) {
    const uint32x4_t expm = vdupq_n_u32(kExpMask);
    const uint32x4_t exp = vandq_u32(bits, expm);
    const uint32x4_t z = vceqq_u32(exp, vdupq_n_u32(0));
    const uint32x4_t o = vceqq_u32(exp, expm);
    const uint32x4_t u = vandq_u32(z, vtstq_u32(bits, vdupq_n_u32(kMantMask)));
    const uint32x4_t s = vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(bits), 31));
    const uint32x4_t zb = {1u << 3, 1u << 2, 1u << 1, 1u << 0};
    const uint32x4_t sb = {1u << 7, 1u << 6, 1u << 5, 1u << 4};
    const uint32x4_t ub = {1u << 11, 1u << 10, 1u << 9, 1u << 8};
    const uint32x4_t ob = {1u << 15, 1u << 14, 1u << 13, 1u << 12};
    uint32x4_t acc = vorrq_u32(vorrq_u32(vandq_u32(z, zb), vandq_u32(s, sb)),
                               vorrq_u32(vandq_u32(u, ub), vandq_u32(o, ob)));
    acc = vandq_u32(acc, vld1q_u32(kLaneMask[mask & 0xF]));
    return static_cast<uint16_t>(vaddvq_u32(acc));  // lane bits are disjoint: the sum is the OR
}
#endif
