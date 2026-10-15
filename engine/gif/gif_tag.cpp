#include "engine/gif/gif_tag.h"

#include <type_traits>

static_assert(sizeof(GifTag) == 24, "GifTag layout is observable through GIF_TAG0-3 and the savestate");
static_assert(std::is_trivially_copyable<GifTag>::value, "GifTag is a POD register image");

// GIFtag field extraction: NLOOP 15 bits, EOP bit 15, PRE bit 46, PRIM 11 bits
// at 47, FLG 2 bits at 58, NREG 4 bits at 60 (0 encodes 16); the descriptors
// are the sixteen nibbles of the high word, low nibble first.
void GifTag::Parse(uint64_t lo, uint64_t hi) {
    nloop = static_cast<uint16_t>(lo & 0x7FFFu);
    eop = ((lo >> 15) & 1u) != 0;
    pre = ((lo >> 46) & 1u) != 0;
    prim = static_cast<uint16_t>((lo >> 47) & 0x7FFu);
    flg = static_cast<GifFormat>((lo >> 58) & 0x3u);
    nregs = static_cast<uint8_t>((lo >> 60) & 0xFu);
    if (nregs == 0) nregs = 16;
    for (int i = 0; i < 16; ++i) regs[i] = static_cast<uint8_t>((hi >> (i * 4)) & 0xFu);
}

// A descriptor the PACKED decoders handle without the 64-bit dispatcher.
static bool IsKnownPackedDescriptor(uint8_t d) {
    switch (static_cast<GsReg>(d)) {
        case GsReg::Prim:
        case GsReg::Rgbaq:
        case GsReg::St:
        case GsReg::Uv:
        case GsReg::Xyzf2:
        case GsReg::Xyz2:
        case GsReg::Fog:
        case GsReg::Xyzf3:
        case GsReg::Xyz3:
        case GsReg::Nop:
            return true;
        default:
            return false;
    }
}

// Eligibility over the first NREG descriptors: strip3 is the {ST, RGBAQ,
// XYZ2|XYZF2} triple; rowable is every other all-known descriptor row.
GifTagClass ClassifyGifTag(const GifTag& tag) {
    GifTagClass c{};
    c.all_known = true;
    for (uint32_t i = 0; i < tag.nregs; ++i) {
        const uint8_t d = tag.regs[i];
        if (d == static_cast<uint8_t>(GsReg::Ad)) c.has_ad = true;
        if (!IsKnownPackedDescriptor(d)) c.all_known = false;
    }
    c.strip3 = tag.nregs == 3 && tag.regs[0] == static_cast<uint8_t>(GsReg::St) &&
               tag.regs[1] == static_cast<uint8_t>(GsReg::Rgbaq) &&
               (tag.regs[2] == static_cast<uint8_t>(GsReg::Xyz2) ||
                tag.regs[2] == static_cast<uint8_t>(GsReg::Xyzf2));
    c.rowable = c.all_known && !c.has_ad && !c.strip3;
    return c;
}
