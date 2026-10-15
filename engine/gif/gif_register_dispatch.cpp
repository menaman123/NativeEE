#include "engine/gif/gif_register_dispatch.h"

#include <cstring>

#include "engine/gif/gif_tag.h"
#include "engine/gif/gs_sink.h"

namespace {

float F32FromBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// PACKED XYZF2/XYZF3: X 12.4 unsigned at lo 0-15, Y at lo 32-47, Z 24 bits at
// hi 4-27, F at hi 36-43, ADC at hi 47. PACKED XYZ2/XYZ3: X, Y as above, Z 32
// bits at hi 0-31, no fog, ADC at hi 47. `kick_reg` is XYZF2/XYZ2 (a drawing
// register); the draw happens only without ADC.
void KickPackedXyzf(GsSink& sink, uint64_t lo, uint64_t hi, bool kick_reg) {
    const float x = static_cast<float>(static_cast<uint16_t>(lo & 0xFFFFu)) / 16.0f;
    const float y = static_cast<float>(static_cast<uint16_t>((lo >> 32) & 0xFFFFu)) / 16.0f;
    const uint32_t z = static_cast<uint32_t>((hi >> 4) & 0xFFFFFFu);
    const uint8_t f = static_cast<uint8_t>((hi >> 36) & 0xFFu);
    const bool adc = ((hi >> 47) & 1u) != 0;
    sink.KickVertex(x, y, static_cast<float>(z), f, kick_reg && !adc);
}

void KickPackedXyz(GsSink& sink, uint64_t lo, uint64_t hi, bool kick_reg) {
    const float x = static_cast<float>(static_cast<uint16_t>(lo & 0xFFFFu)) / 16.0f;
    const float y = static_cast<float>(static_cast<uint16_t>((lo >> 32) & 0xFFFFu)) / 16.0f;
    const uint32_t z = static_cast<uint32_t>(hi & 0xFFFFFFFFu);
    const bool adc = ((hi >> 47) & 1u) != 0;
    sink.KickVertex(x, y, static_cast<float>(z), 0, kick_reg && !adc);
}

// The 64-bit vertex forms (REGLIST half or A+D pair): X at 0-15, Y at 16-31,
// Z at 32-63 (XYZ) or 32-55 with F at 56-63 (XYZF).
void Kick64Xyzf(GsSink& sink, uint64_t value, bool draw) {
    const float x = static_cast<float>(static_cast<uint16_t>(value & 0xFFFFu)) / 16.0f;
    const float y = static_cast<float>(static_cast<uint16_t>((value >> 16) & 0xFFFFu)) / 16.0f;
    const uint32_t z = static_cast<uint32_t>((value >> 32) & 0xFFFFFFu);
    const uint8_t f = static_cast<uint8_t>((value >> 56) & 0xFFu);
    sink.KickVertex(x, y, static_cast<float>(z), f, draw);
}

void Kick64Xyz(GsSink& sink, uint64_t value, bool draw) {
    const float x = static_cast<float>(static_cast<uint16_t>(value & 0xFFFFu)) / 16.0f;
    const float y = static_cast<float>(static_cast<uint16_t>((value >> 16) & 0xFFFFu)) / 16.0f;
    const uint32_t z = static_cast<uint32_t>(value >> 32);
    sink.KickVertex(x, y, static_cast<float>(z), 0, draw);
}

// General registers the GS applies when written through REGLIST or A+D.
bool RegisterIsForwarded(uint8_t reg) {
    switch (static_cast<GsReg>(reg)) {
        case GsReg::Rgbaq:
        case GsReg::St:
        case GsReg::Uv:
        case GsReg::Tex0_1:
        case GsReg::Tex0_2:
        case GsReg::Clamp_1:
        case GsReg::Clamp_2:
        case GsReg::Fog:
        case GsReg::Tex1_1:
        case GsReg::Tex1_2:
        case GsReg::Tex2_1:
        case GsReg::Tex2_2:
        case GsReg::XyOffset_1:
        case GsReg::XyOffset_2:
        case GsReg::Prmodecont:
        case GsReg::Prmode:
        case GsReg::Scanmsk:
        case GsReg::Miptbp1_1:
        case GsReg::Miptbp1_2:
        case GsReg::Miptbp2_1:
        case GsReg::Miptbp2_2:
        case GsReg::Texa:
        case GsReg::Fogcol:
        case GsReg::Texflush:
        case GsReg::Scissor_1:
        case GsReg::Scissor_2:
        case GsReg::Alpha_1:
        case GsReg::Alpha_2:
        case GsReg::Dimx:
        case GsReg::Dthe:
        case GsReg::Colclamp:
        case GsReg::Test_1:
        case GsReg::Test_2:
        case GsReg::Pabe:
        case GsReg::Fba_1:
        case GsReg::Fba_2:
        case GsReg::Frame_1:
        case GsReg::Frame_2:
        case GsReg::Zbuf_1:
        case GsReg::Zbuf_2:
        case GsReg::Bitbltbuf:
        case GsReg::Trxpos:
        case GsReg::Trxreg:
        case GsReg::Trxdir:
        case GsReg::Hwreg:
        case GsReg::Signal:
        case GsReg::Finish:
        case GsReg::Label:
            return true;
        default:
            return false;
    }
}

}  // namespace

void DispatchGifRegister(GsSink& sink, uint8_t reg, uint64_t value) {
    switch (static_cast<GsReg>(reg)) {
        case GsReg::Prim:
            sink.WriteRegister(reg, value & 0x7FFu);
            break;
        case GsReg::Xyzf2:
        case GsReg::Xyzf3:
            Kick64Xyzf(sink, value, reg == static_cast<uint8_t>(GsReg::Xyzf2));
            break;
        case GsReg::Xyz2:
        case GsReg::Xyz3:
            Kick64Xyz(sink, value, reg == static_cast<uint8_t>(GsReg::Xyz2));
            break;
        default:
            if (RegisterIsForwarded(reg)) sink.WriteRegister(reg, value);
            break;
    }
}

void DispatchPackedQuadword(GsSink& sink, uint8_t reg, uint64_t lo, uint64_t hi) {
    switch (static_cast<GsReg>(reg)) {
        case GsReg::Prim:
            sink.WriteRegister(static_cast<uint8_t>(GsReg::Prim), lo & 0x7FFu);
            break;
        case GsReg::Rgbaq:
            // One byte per 32-bit lane, Q untouched.
            sink.PackedRgbaq(static_cast<uint8_t>(lo & 0xFFu), static_cast<uint8_t>((lo >> 32) & 0xFFu),
                             static_cast<uint8_t>(hi & 0xFFu), static_cast<uint8_t>((hi >> 32) & 0xFFu));
            break;
        case GsReg::St:
            // S at lo 0-31, T at lo 32-63, Q at hi 0-31 (raw; the sink applies the non-zero rule).
            sink.PackedSt(F32FromBits(static_cast<uint32_t>(lo & 0xFFFFFFFFu)),
                          F32FromBits(static_cast<uint32_t>(lo >> 32)),
                          F32FromBits(static_cast<uint32_t>(hi & 0xFFFFFFFFu)));
            break;
        case GsReg::Uv:
            // U at lo 0-13, V at lo 32-45.
            sink.PackedUv(static_cast<uint16_t>(lo & 0x3FFFu), static_cast<uint16_t>((lo >> 32) & 0x3FFFu));
            break;
        case GsReg::Fog:
            // F at hi 36-43.
            sink.PackedFog(static_cast<uint8_t>((hi >> 36) & 0xFFu));
            break;
        case GsReg::Xyzf2:
        case GsReg::Xyzf3:
            KickPackedXyzf(sink, lo, hi, reg == static_cast<uint8_t>(GsReg::Xyzf2));
            break;
        case GsReg::Xyz2:
        case GsReg::Xyz3:
            KickPackedXyz(sink, lo, hi, reg == static_cast<uint8_t>(GsReg::Xyz2));
            break;
        case GsReg::Nop:
            // The quadword is consumed and ignored.
            break;
        case GsReg::Ad:
            DispatchGifRegister(sink, static_cast<uint8_t>(hi & 0xFFu), lo);
            break;
        default:
            DispatchGifRegister(sink, reg, lo);
            break;
    }
}
