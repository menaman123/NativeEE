#pragma once
// vu0_native.h -- the VU0 macro-mode (COP2) lane kernels the recompiler emits
// under `--vu0-native`, as host vector code: NEON on AArch64, SSE2 on x86-64,
// a scalar arm everywhere else (and under PS2R_VU0_NATIVE_SCALAR=1, the twin
// the tests compare against). Portable: no OS macro, architecture macros only.
//
// CONTRACT (the acceptance test is the determinism lane, mem.mhash identical):
// every kernel here is the BIT-FOR-BIT twin of the scalar emission the
// recompiler produces without the flag, which is
//   * the VR_reg operators of ../host_app/cpu_state.h: per lane
//     ps2_float_clamp(ps2_float_clamp(a) op ps2_float_clamp(b)) -- the clamp
//     turns an all-ones exponent (NaN / Inf) into +-MAX_FLOAT with the sign
//     kept and leaves denormals ALONE (this is the EE-side Ps2Float clamp, not
//     the microprogram tiers' VuFloat::ClampBits1, which flushes denormals);
//     the operation itself rounds under the thread's current mode (a guest EE
//     thread runs FE_TOWARDZERO), which the vector units honour as the scalar
//     unit does (AArch64 FPCR.RMode applies to ASIMD; MXCSR.RC applies to SSE);
//     MADD/MSUB are TWO roundings (clamp(mul) then clamp(add)) -- the clamp in
//     between forbids any contraction, so no FMA can form;
//   * the masked write: the dest lanes the xyzw mask selects take the result;
//   * VuMacroCommitFlags (engine/vu/macro/vu_macro_bridge.cpp): MAC = the
//     Z/S/U/O nibbles of the selected lanes of the (clamped) result, judged on
//     the raw bits (Z: exponent 0 -- true zero AND denormal; S: sign bit, -0
//     included; U: denormal; O: exponent all-ones -- never after the clamp);
//     STATUS Z/S/U/O (bits 0-3) are rewritten from MAC, their sticky twins
//     (bits 6-9) OR in, D/I (bits 4-5, 10-11) untouched.
// The flag commit is FOLDED by the emitter: a straight-line run of FMAC ops
// keeps the last op's lane flags (`_vu0_fl`) and the OR over the run
// (`_vu0_fo`) in two VR_reg locals and commits once at the run's end
// (Commit): mac = pack(last); status = (status & 0xFF0) | nib(last) |
// nib(OR) << 6 -- the exact fold of k successive VuMacroCommitFlags calls,
// since each call rewrites MAC and the non-sticky nibble wholesale and the
// sticky bits only ever OR in (nib(a | b) == nib(a) | nib(b)).
//
// Lane flag encoding (LaneFlags): one uint32 per lane, bit0 Z, bit1 S, bit2 U,
// bit3 O; a lane the mask does not select reads 0. PackMac maps lane x (index
// 0) to MAC field 3, y to 2, z to 1, w to 0 (VuFloat::MacFlags1's `field`).
#include <cstdint>
#include <cstring>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/ps2_float.h"
#include "engine/platform/host/platform.h"

#if !defined(PS2R_VU0_NATIVE_SCALAR)
#define PS2R_VU0_NATIVE_SCALAR 0
#endif

#if !PS2R_VU0_NATIVE_SCALAR && (defined(__aarch64__) || defined(_M_ARM64))
#include <arm_neon.h>
#define PS2R_VU0N_ARM 1
#define PS2R_VU0N_NEON 1
#define PS2R_VU0N_SSE2 0
#elif !PS2R_VU0_NATIVE_SCALAR && (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || \
                                  (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#include <emmintrin.h>
#define PS2R_VU0N_ARM 1
#define PS2R_VU0N_NEON 0
#define PS2R_VU0N_SSE2 1
#else
#define PS2R_VU0N_ARM 0
#define PS2R_VU0N_NEON 0
#define PS2R_VU0N_SSE2 0
#endif

namespace vu0n {

constexpr uint32_t kExpMask = 0x7F800000u;
constexpr uint32_t kMantMask = 0x007FFFFFu;
constexpr uint32_t kSignBit = 0x80000000u;
constexpr uint32_t kMaxFloatBits = 0x7F7FFFFFu;

// Lane select table: entry = all-ones for the lanes the 4-bit dest mask
// (bit3 = x -> lane 0 ... bit0 = w -> lane 3) selects.
alignas(16) inline constexpr uint32_t kLaneMask[16][4] = {
    {0u, 0u, 0u, 0u},     {0u, 0u, 0u, ~0u},     {0u, 0u, ~0u, 0u},     {0u, 0u, ~0u, ~0u},
    {0u, ~0u, 0u, 0u},    {0u, ~0u, 0u, ~0u},    {0u, ~0u, ~0u, 0u},    {0u, ~0u, ~0u, ~0u},
    {~0u, 0u, 0u, 0u},    {~0u, 0u, 0u, ~0u},    {~0u, 0u, ~0u, 0u},    {~0u, 0u, ~0u, ~0u},
    {~0u, ~0u, 0u, 0u},   {~0u, ~0u, 0u, ~0u},   {~0u, ~0u, ~0u, 0u},   {~0u, ~0u, ~0u, ~0u},
};

// nibble n (Z=bit0 S=bit1 U=bit2 O=bit3) -> bit i at position 4*i (the MAC
// register's per-lane spread); shifted by the lane's field afterwards.
inline constexpr uint16_t kSpread[16] = {
    0x0000, 0x0001, 0x0010, 0x0011, 0x0100, 0x0101, 0x0110, 0x0111,
    0x1000, 0x1001, 0x1010, 0x1011, 0x1100, 0x1101, 0x1110, 0x1111,
};

// ---------------------------------------------------------------- scalar arm
// The reference bodies (also the fallback). Kept as plain functions so the
// vector arms can be compared against them lane for lane.
namespace scalar {

PS2R_FORCE_INLINE uint32_t ClampBits(uint32_t bits) {
    if ((bits & kExpMask) == kExpMask) return (bits & kSignBit) | kMaxFloatBits;
    return bits;
}

PS2R_FORCE_INLINE VR_reg Clamp(const VR_reg& v) {
    VR_reg r{};
    for (int i = 0; i < 4; ++i) r.I[i] = ClampBits(v.I[i]);
    return r;
}

PS2R_FORCE_INLINE VR_reg Add(const VR_reg& a, const VR_reg& b) {
    VR_reg r{};
    for (int i = 0; i < 4; ++i) r.F[i] = ps2_float_clamp(ps2_float_clamp(a.F[i]) + ps2_float_clamp(b.F[i]));
    return r;
}
PS2R_FORCE_INLINE VR_reg Sub(const VR_reg& a, const VR_reg& b) {
    VR_reg r{};
    for (int i = 0; i < 4; ++i) r.F[i] = ps2_float_clamp(ps2_float_clamp(a.F[i]) - ps2_float_clamp(b.F[i]));
    return r;
}
PS2R_FORCE_INLINE VR_reg Mul(const VR_reg& a, const VR_reg& b) {
    VR_reg r{};
    for (int i = 0; i < 4; ++i) r.F[i] = ps2_float_clamp(ps2_float_clamp(a.F[i]) * ps2_float_clamp(b.F[i]));
    return r;
}

PS2R_FORCE_INLINE VR_reg Splat(float v) {
    VR_reg r{};
    r.F[0] = r.F[1] = r.F[2] = r.F[3] = v;
    return r;
}

template <int kLane>
PS2R_FORCE_INLINE VR_reg Bc(const VR_reg& v) {
    static_assert(kLane >= 0 && kLane < 4, "lane");
    return Splat(v.F[kLane]);
}

template <unsigned kMask>
PS2R_FORCE_INLINE VR_reg Blend(const VR_reg& dst, const VR_reg& res) {
    static_assert(kMask < 16u, "dest mask");
    VR_reg r{};
    for (int i = 0; i < 4; ++i) r.I[i] = (kLaneMask[kMask][i] & res.I[i]) | (~kLaneMask[kMask][i] & dst.I[i]);
    return r;
}

PS2R_FORCE_INLINE uint32_t LaneFlagBits(uint32_t bits) {
    const uint32_t exp = bits & kExpMask;
    uint32_t f = 0;
    if (exp == 0) {
        f |= 1u;                                 // Z: true zero and denormal
        if (bits & kMantMask) f |= 4u;           // U: denormal
    } else if (exp == kExpMask) {
        f |= 8u;                                 // O: Inf / NaN pattern
    }
    if (bits & kSignBit) f |= 2u;                // S: raw sign, -0 included
    return f;
}

template <unsigned kMask>
PS2R_FORCE_INLINE VR_reg LaneFlags(const VR_reg& res) {
    static_assert(kMask < 16u, "dest mask");
    VR_reg r{};
    for (int i = 0; i < 4; ++i) r.I[i] = LaneFlagBits(res.I[i]) & kLaneMask[kMask][i];
    return r;
}

PS2R_FORCE_INLINE VR_reg Or(const VR_reg& a, const VR_reg& b) {
    VR_reg r{};
    for (int i = 0; i < 4; ++i) r.I[i] = a.I[i] | b.I[i];
    return r;
}

}  // namespace scalar

// ---------------------------------------------------------------- vector arms
#if PS2R_VU0N_NEON

PS2R_FORCE_INLINE uint32x4_t ClampBits4(uint32x4_t bits) {
    const uint32x4_t exp_mask = vdupq_n_u32(kExpMask);
    const uint32x4_t is_special = vceqq_u32(vandq_u32(bits, exp_mask), exp_mask);
    const uint32x4_t clamped = vorrq_u32(vandq_u32(bits, vdupq_n_u32(kSignBit)), vdupq_n_u32(kMaxFloatBits));
    return vbslq_u32(is_special, clamped, bits);
}
PS2R_FORCE_INLINE float32x4_t ClampF4(float32x4_t v) {
    return vreinterpretq_f32_u32(ClampBits4(vreinterpretq_u32_f32(v)));
}
PS2R_FORCE_INLINE float32x4_t Load(const VR_reg& v) { return vld1q_f32(v.F); }
PS2R_FORCE_INLINE VR_reg Store(float32x4_t v) {
    VR_reg r;
    vst1q_f32(r.F, v);
    return r;
}
PS2R_FORCE_INLINE VR_reg StoreU(uint32x4_t v) {
    VR_reg r;
    vst1q_u32(r.I, v);
    return r;
}

PS2R_FORCE_INLINE VR_reg Clamp(const VR_reg& v) { return Store(ClampF4(Load(v))); }
PS2R_FORCE_INLINE VR_reg Add(const VR_reg& a, const VR_reg& b) {
    return Store(ClampF4(vaddq_f32(ClampF4(Load(a)), ClampF4(Load(b)))));
}
PS2R_FORCE_INLINE VR_reg Sub(const VR_reg& a, const VR_reg& b) {
    return Store(ClampF4(vsubq_f32(ClampF4(Load(a)), ClampF4(Load(b)))));
}
PS2R_FORCE_INLINE VR_reg Mul(const VR_reg& a, const VR_reg& b) {
    return Store(ClampF4(vmulq_f32(ClampF4(Load(a)), ClampF4(Load(b)))));
}
PS2R_FORCE_INLINE VR_reg Splat(float v) { return Store(vdupq_n_f32(v)); }
template <int kLane>
PS2R_FORCE_INLINE VR_reg Bc(const VR_reg& v) {
    static_assert(kLane >= 0 && kLane < 4, "lane");
    return Store(vdupq_laneq_f32(Load(v), kLane));
}
template <unsigned kMask>
PS2R_FORCE_INLINE VR_reg Blend(const VR_reg& dst, const VR_reg& res) {
    static_assert(kMask < 16u, "dest mask");
    const uint32x4_t lm = vld1q_u32(kLaneMask[kMask]);
    return StoreU(vbslq_u32(lm, vld1q_u32(res.I), vld1q_u32(dst.I)));
}
template <unsigned kMask>
PS2R_FORCE_INLINE VR_reg LaneFlags(const VR_reg& res) {
    static_assert(kMask < 16u, "dest mask");
    const uint32x4_t bits = vld1q_u32(res.I);
    const uint32x4_t exp = vandq_u32(bits, vdupq_n_u32(kExpMask));
    const uint32x4_t exp_zero = vceqzq_u32(exp);
    const uint32x4_t exp_ones = vceqq_u32(exp, vdupq_n_u32(kExpMask));
    const uint32x4_t mant_nz = vtstq_u32(bits, vdupq_n_u32(kMantMask));
    const uint32x4_t z = vandq_u32(exp_zero, vdupq_n_u32(1u));
    const uint32x4_t s = vshrq_n_u32(bits, 30);                   // sign bit -> bit1 (bit0 of the result is 0 for a
    const uint32x4_t s_only = vandq_u32(s, vdupq_n_u32(2u));      // negative lane only through this mask)
    const uint32x4_t u = vandq_u32(vandq_u32(exp_zero, mant_nz), vdupq_n_u32(4u));
    const uint32x4_t o = vandq_u32(exp_ones, vdupq_n_u32(8u));
    const uint32x4_t f = vorrq_u32(vorrq_u32(z, s_only), vorrq_u32(u, o));
    return StoreU(vandq_u32(f, vld1q_u32(kLaneMask[kMask])));
}
PS2R_FORCE_INLINE VR_reg Or(const VR_reg& a, const VR_reg& b) {
    return StoreU(vorrq_u32(vld1q_u32(a.I), vld1q_u32(b.I)));
}

#elif PS2R_VU0N_SSE2

PS2R_FORCE_INLINE __m128i ClampBits4(__m128i bits) {
    const __m128i exp_mask = _mm_set1_epi32(static_cast<int>(kExpMask));
    const __m128i is_special = _mm_cmpeq_epi32(_mm_and_si128(bits, exp_mask), exp_mask);
    const __m128i clamped =
        _mm_or_si128(_mm_and_si128(bits, _mm_set1_epi32(static_cast<int>(kSignBit))),
                     _mm_set1_epi32(static_cast<int>(kMaxFloatBits)));
    return _mm_or_si128(_mm_and_si128(is_special, clamped), _mm_andnot_si128(is_special, bits));
}
PS2R_FORCE_INLINE __m128 ClampF4(__m128 v) { return _mm_castsi128_ps(ClampBits4(_mm_castps_si128(v))); }
PS2R_FORCE_INLINE __m128 Load(const VR_reg& v) { return _mm_load_ps(v.F); }
PS2R_FORCE_INLINE VR_reg Store(__m128 v) {
    VR_reg r;
    _mm_store_ps(r.F, v);
    return r;
}
PS2R_FORCE_INLINE VR_reg StoreU(__m128i v) {
    VR_reg r;
    _mm_store_si128(reinterpret_cast<__m128i*>(r.I), v);
    return r;
}
PS2R_FORCE_INLINE __m128i LoadU(const VR_reg& v) { return _mm_load_si128(reinterpret_cast<const __m128i*>(v.I)); }

PS2R_FORCE_INLINE VR_reg Clamp(const VR_reg& v) { return Store(ClampF4(Load(v))); }
PS2R_FORCE_INLINE VR_reg Add(const VR_reg& a, const VR_reg& b) {
    return Store(ClampF4(_mm_add_ps(ClampF4(Load(a)), ClampF4(Load(b)))));
}
PS2R_FORCE_INLINE VR_reg Sub(const VR_reg& a, const VR_reg& b) {
    return Store(ClampF4(_mm_sub_ps(ClampF4(Load(a)), ClampF4(Load(b)))));
}
PS2R_FORCE_INLINE VR_reg Mul(const VR_reg& a, const VR_reg& b) {
    return Store(ClampF4(_mm_mul_ps(ClampF4(Load(a)), ClampF4(Load(b)))));
}
PS2R_FORCE_INLINE VR_reg Splat(float v) { return Store(_mm_set1_ps(v)); }
template <int kLane>
PS2R_FORCE_INLINE VR_reg Bc(const VR_reg& v) {
    static_assert(kLane >= 0 && kLane < 4, "lane");
    const __m128 x = Load(v);
    return Store(_mm_shuffle_ps(x, x, _MM_SHUFFLE(kLane, kLane, kLane, kLane)));
}
template <unsigned kMask>
PS2R_FORCE_INLINE VR_reg Blend(const VR_reg& dst, const VR_reg& res) {
    static_assert(kMask < 16u, "dest mask");
    const __m128i lm = _mm_load_si128(reinterpret_cast<const __m128i*>(kLaneMask[kMask]));
    return StoreU(_mm_or_si128(_mm_and_si128(lm, LoadU(res)), _mm_andnot_si128(lm, LoadU(dst))));
}
template <unsigned kMask>
PS2R_FORCE_INLINE VR_reg LaneFlags(const VR_reg& res) {
    static_assert(kMask < 16u, "dest mask");
    const __m128i bits = LoadU(res);
    const __m128i exp_mask = _mm_set1_epi32(static_cast<int>(kExpMask));
    const __m128i exp = _mm_and_si128(bits, exp_mask);
    const __m128i exp_zero = _mm_cmpeq_epi32(exp, _mm_setzero_si128());
    const __m128i exp_ones = _mm_cmpeq_epi32(exp, exp_mask);
    const __m128i mant = _mm_and_si128(bits, _mm_set1_epi32(static_cast<int>(kMantMask)));
    const __m128i mant_nz = _mm_xor_si128(_mm_cmpeq_epi32(mant, _mm_setzero_si128()), _mm_set1_epi32(-1));
    const __m128i z = _mm_and_si128(exp_zero, _mm_set1_epi32(1));
    const __m128i s = _mm_and_si128(_mm_srli_epi32(bits, 30), _mm_set1_epi32(2));
    const __m128i u = _mm_and_si128(_mm_and_si128(exp_zero, mant_nz), _mm_set1_epi32(4));
    const __m128i o = _mm_and_si128(exp_ones, _mm_set1_epi32(8));
    const __m128i f = _mm_or_si128(_mm_or_si128(z, s), _mm_or_si128(u, o));
    return StoreU(_mm_and_si128(f, _mm_load_si128(reinterpret_cast<const __m128i*>(kLaneMask[kMask]))));
}
PS2R_FORCE_INLINE VR_reg Or(const VR_reg& a, const VR_reg& b) { return StoreU(_mm_or_si128(LoadU(a), LoadU(b))); }

#else

using scalar::Clamp;
using scalar::Add;
using scalar::Sub;
using scalar::Mul;
using scalar::Splat;
using scalar::Bc;
using scalar::Blend;
using scalar::LaneFlags;
using scalar::Or;

#endif

// ---------------------------------------------------------------- the commit
// mac = the 16-bit MAC register from the last op's lane flags; status = the
// non-sticky nibble from the same, the sticky twins OR'd from `or_lanes` (the
// OR over the folded run; equal to `last_lanes` for a run of one). Identical
// to k successive VuMacroCommitFlags calls (header comment).
PS2R_FORCE_INLINE uint16_t PackMac(const VR_reg& lanes) {
    return static_cast<uint16_t>((kSpread[lanes.I[0] & 15u] << 3) | (kSpread[lanes.I[1] & 15u] << 2) |
                                 (kSpread[lanes.I[2] & 15u] << 1) | kSpread[lanes.I[3] & 15u]);
}
PS2R_FORCE_INLINE uint16_t Nibble(const VR_reg& lanes) {
    return static_cast<uint16_t>((lanes.I[0] | lanes.I[1] | lanes.I[2] | lanes.I[3]) & 15u);
}
PS2R_FORCE_INLINE void Commit(VUregs& regs, const VR_reg& last_lanes, const VR_reg& or_lanes) {
    regs.mac_flag = PackMac(last_lanes);
    const uint16_t sticky = static_cast<uint16_t>(Nibble(or_lanes) | (regs.status_flag >> 6));
    regs.status_flag = static_cast<uint16_t>((regs.status_flag & 0xFF0u) | Nibble(last_lanes) |
                                             (static_cast<uint16_t>(sticky & 0xFu) << 6));
}

}  // namespace vu0n
