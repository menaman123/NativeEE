// Inline bodies of VuInterpreterTier (included by vu_interpreter_tier.h).
#pragma once

#include <cfloat>
#include <cmath>

#include "engine/vu/vu_decoder.h"

// The four-lane clamp every GENERIC / special upper body reads its sources
// through (two to three per word); inline so the generated blocks' calls into
// those bodies do not pay a call per lane set.
inline VR_reg VuInterpreterTier::Clamp4(const VR_reg& v) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.I[k] = VuFloat::ClampBits1(v.I[k]);
    return r;
}

inline float VuInterpreterTier::EfuSumSq(const VR_reg& v) {
    return std::fmaf(v.z, v.z, std::fmaf(v.x, v.x, v.y * v.y));
}

inline float VuInterpreterTier::EfuRecip(float v) { return v != 0.0f ? 1.0f / v : FLT_MAX; }

// Integer-compare clip judgement: |w| as an int (a zero/denormal w compares
// as the largest denormal), each of +x/-x/+y/-y/+z/-z tested "outside" as a
// signed compare with the sign bit flipped for the negative side.
inline uint32_t VuInterpreterTier::ClipJudge(const VR_reg& fs_raw, const VR_reg& ft_raw) {
    const int32_t wbits = static_cast<int32_t>(ft_raw.I[3]);
    const int32_t value = (wbits & 0x7F800000) ? (wbits & 0x7FFFFFFF) : 0x007FFFFF;
    uint32_t nf = 0;
    for (int k = 0; k < 3; ++k) {
        const uint32_t b = fs_raw.I[k];
        if (static_cast<int32_t>(b) > value) nf |= 1u << (2 * k);
        if (static_cast<int32_t>(b ^ 0x80000000u) > value) nf |= 1u << (2 * k + 1);
    }
    return nf;
}

// The FDIV quotient of clamped operands: a zero divisor yields the signed
// max float and the I (0/0) or D status bits with their sticky twins.
inline float VuInterpreterTier::DivQuotient(float num, float den, uint16_t& status) {
    if (den != 0.0f) {
        status = 0;
        return num / den;
    }
    status = (num == 0.0f) ? static_cast<uint16_t>(vu_status::I | vu_status::IS) : static_cast<uint16_t>(vu_status::D | vu_status::DS);
    return VuFloat::Bits2F(((VuFloat::F2Bits(num) ^ VuFloat::F2Bits(den)) & VuFloat::kSignBit) | VuFloat::kMaxFloatBits);
}
