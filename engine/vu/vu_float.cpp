// VuFloat -- the non-inlined VU float helpers. Rules in the header.
#include "engine/vu/vu_float.h"

#include <cmath>

#if PS2R_VU_X86_FMA
#include <immintrin.h>

#include "engine/platform/host/platform.h"
#endif

#if PS2R_VU_X86_FMA
namespace {
bool DetectFma3() {
    bool sse4_2 = false, avx2 = false, fma3 = false, neon = false;
    const char* arch = nullptr;
    return ps2r::host::CpuDetect(&sse4_2, &avx2, &fma3, &neon, &arch) && fma3;
}
}  // namespace

const bool g_vu_x86_fma = DetectFma3();

// The only FMA-encoded code of the build: called only when g_vu_x86_fma (header rule). Clang / GCC: the
// function is compiled for FMA, so the fused builtin is the vfmadd instruction (clang-cl's <immintrin.h>
// hides the FMA intrinsics from a TU not compiled for FMA); cl.exe takes the intrinsics as they are.
#if defined(__clang__) || defined(__GNUC__)
__attribute__((target("fma"))) void VuFmadd4Hw(const float* a, const float* b, const float* c, float* r) {
    for (int k = 0; k < 4; ++k) r[k] = __builtin_fmaf(a[k], b[k], c[k]);
}
__attribute__((target("fma"))) void VuFnmadd4Hw(const float* a, const float* b, const float* c, float* r) {
    for (int k = 0; k < 4; ++k) r[k] = __builtin_fmaf(-a[k], b[k], c[k]);
}
#else
void VuFmadd4Hw(const float* a, const float* b, const float* c, float* r) {
    _mm_storeu_ps(r, _mm_fmadd_ps(_mm_loadu_ps(a), _mm_loadu_ps(b), _mm_loadu_ps(c)));
}
void VuFnmadd4Hw(const float* a, const float* b, const float* c, float* r) {
    _mm_storeu_ps(r, _mm_fnmadd_ps(_mm_loadu_ps(a), _mm_loadu_ps(b), _mm_loadu_ps(c)));   // -(a*b) + c
}
#endif
#endif

alignas(16) const uint32_t VuFloat::kLaneMask[16][4] = {
    {0u, 0u, 0u, 0u},     {0u, 0u, 0u, ~0u},     {0u, 0u, ~0u, 0u},     {0u, 0u, ~0u, ~0u},
    {0u, ~0u, 0u, 0u},    {0u, ~0u, 0u, ~0u},    {0u, ~0u, ~0u, 0u},    {0u, ~0u, ~0u, ~0u},
    {~0u, 0u, 0u, 0u},    {~0u, 0u, 0u, ~0u},    {~0u, 0u, ~0u, 0u},    {~0u, 0u, ~0u, ~0u},
    {~0u, ~0u, 0u, 0u},   {~0u, ~0u, 0u, ~0u},   {~0u, ~0u, ~0u, 0u},   {~0u, ~0u, ~0u, ~0u},
};

void VuFloat::ApplyMinMaxRaw(VR_reg& dest, const VR_reg& fs_raw, const uint32_t ft_raw[4], uint8_t mask,
                             bool protect_vf0, MinMaxFn fn) {
    if (protect_vf0) return;
    uint32_t out[4];
    for (int k = 0; k < 4; ++k) out[k] = fn(fs_raw.I[k], ft_raw[k]);
    if (mask & 8) dest.I[0] = out[0];
    if (mask & 4) dest.I[1] = out[1];
    if (mask & 2) dest.I[2] = out[2];
    if (mask & 1) dest.I[3] = out[3];
}

float VuFloat::VuDiv(float num, float den) {
    num = ClampFloat(num);
    den = ClampFloat(den);
    if (den == 0.0f) {
        const uint32_t sign = (std::signbit(num) != std::signbit(den)) ? kSignBit : 0u;
        return Bits2F(sign | kMaxFloatBits);
    }
    return ClampFloat(num / den);  // under the caller's rounding mode (FE_TOWARDZERO inside the guard)
}

uint32_t VuFloat::Ftoi0(uint32_t lane_bits) {
    return FtoiSat(ClampFloat(Bits2F(lane_bits)));
}

uint32_t VuFloat::Ftoi4(uint32_t lane_bits) {
    return FtoiSat(Bits2F(lane_bits) * 16.0f);
}

uint32_t VuFloat::Ftoi12(uint32_t lane_bits) {
    return FtoiSat(Bits2F(lane_bits) * 4096.0f);
}

uint32_t VuFloat::Ftoi15(uint32_t lane_bits) {
    return FtoiSat(Bits2F(lane_bits) * 32768.0f);
}

uint32_t VuFloat::ClipNf(uint32_t xb_u, uint32_t yb_u, uint32_t zb_u, uint32_t wb_u) {
    const int32_t wb = (int32_t)wb_u;
    const int32_t value = (wb & 0x7f800000) ? (int32_t)(wb_u & 0x7fffffff) : 0x007fffff;
    uint32_t nf = 0;
    const int32_t xb = (int32_t)xb_u, yb = (int32_t)yb_u, zb = (int32_t)zb_u;
    if ((int32_t)(xb ^ (int32_t)0x00000000) > value) nf |= 0x01;
    if ((int32_t)(xb ^ (int32_t)0x80000000) > value) nf |= 0x02;
    if ((int32_t)(yb ^ (int32_t)0x00000000) > value) nf |= 0x04;
    if ((int32_t)(yb ^ (int32_t)0x80000000) > value) nf |= 0x08;
    if ((int32_t)(zb ^ (int32_t)0x00000000) > value) nf |= 0x10;
    if ((int32_t)(zb ^ (int32_t)0x80000000) > value) nf |= 0x20;
    return nf;
}

// ------------------------------------------------------------------ exponent-255 lanes
// The VU value of a raw lane: sign * 2^(exp-127) * 1.mantissa for exp >= 1 (exp 255 = the 2^128
// binade, no NaN/Inf), +/-0 for exp 0 (denormals flush).
double VuFloat::LaneToDouble(uint32_t bits) {
    const int exp = static_cast<int>((bits >> 23) & 0xFFu);
    const double sign = (bits & kSignBit) ? -1.0 : 1.0;
    if (exp == 0) return sign * 0.0;
    const double mant = static_cast<double>(0x00800000u | (bits & kMantMask));  // 1.m * 2^23
    return sign * std::ldexp(mant, exp - 127 - 23);
}

// Re-encode with the VU's rules: the exact magnitude truncated toward zero to 24 bits; the binade
// 2^128 kept as exponent 255; anything past it the engine's +/-MAX_FLOAT; a result below the normal
// range +/-0 (sign kept).
uint32_t VuFloat::DoubleToLane(double r) {
    const uint32_t sign = std::signbit(r) ? kSignBit : 0u;
    const double a = std::fabs(r);
    if (!(a > 0.0)) return sign;                      // zero (a NaN cannot arise from finite operands)
    int e = 0;
    const double m = std::frexp(a, &e);               // a = m * 2^e, m in [0.5, 1)
    const int expf = e - 1 + 127;                     // a = (2m) * 2^(e-1), 2m in [1, 2)
    if (expf > 255) return sign | kMaxFloatBits;      // overflow
    if (expf < 1) return sign;                        // denormal / underflow -> +/-0
    const uint32_t mant24 = static_cast<uint32_t>(std::ldexp(m, 24));  // truncation: [2^23, 2^24)
    return sign | (static_cast<uint32_t>(expf) << 23) | (mant24 & kMantMask);
}

uint32_t VuFloat::ExactLane(FmacKind kind, uint32_t a, uint32_t b, uint32_t c) {
    const double da = LaneToDouble(a), db = LaneToDouble(b);
    double r = 0.0;
    switch (kind) {
        case kFmacAdd: r = da + db; break;
        case kFmacSub: r = da - db; break;
        case kFmacMul: r = da * db; break;                       // 24 x 24 bits: exact in double
        case kFmacFma: r = LaneToDouble(c) + da * db; break;
        case kFmacFms: r = LaneToDouble(c) - da * db; break;
    }
    return DoubleToLane(r);
}
