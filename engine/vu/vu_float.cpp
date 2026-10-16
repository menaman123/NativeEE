// VuFloat -- the non-inlined VU float helpers. Rules in the header.
#include "engine/vu/vu_float.h"

#include <cmath>

alignas(16) const uint32_t VuFloat::kLaneMask[16][4] = {
    {0u, 0u, 0u, 0u},     {0u, 0u, 0u, ~0u},     {0u, 0u, ~0u, 0u},     {0u, 0u, ~0u, ~0u},
    {0u, ~0u, 0u, 0u},    {0u, ~0u, 0u, ~0u},    {0u, ~0u, ~0u, 0u},    {0u, ~0u, ~0u, ~0u},
    {~0u, 0u, 0u, 0u},    {~0u, 0u, 0u, ~0u},    {~0u, 0u, ~0u, 0u},    {~0u, 0u, ~0u, ~0u},
    {~0u, ~0u, 0u, 0u},   {~0u, ~0u, 0u, ~0u},   {~0u, ~0u, ~0u, 0u},   {~0u, ~0u, ~0u, ~0u},
};

void VuFloat::CommitQStatus(VUregs& regs, uint16_t q_status) {
    regs.status_flag = static_cast<uint16_t>((regs.status_flag & 0xFCFu) | (q_status & 0xC30u));
}

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
