// gs/format/pixel_convert.cpp -- see pixel_convert.h for the rules and their sources.
#include "engine/gs/format/pixel_convert.h"

#include <cassert>

#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/texa_expand.h"

namespace ps2r {
namespace gs {

uint32_t PixelConvert::StorageBitsPerPixel(Psm psm) {
    return PsmLayout::Describe(psm).storage_bits;
}

uint32_t PixelConvert::BytesPerPixel(Psm psm) {
    const uint32_t bits = StorageBitsPerPixel(psm);
    assert(bits >= 8 && "PSMT4 has no whole byte per pixel; use StorageBitsPerPixel");
    return bits / 8;
}

uint32_t PixelConvert::StreamBitsPerPixel(Psm psm) {
    // Section 3.9 host -> local transfer: RGB24 / Z24 pixels are packed at 24 bits
    // in the IMAGE data; every other mode streams at its storage width.
    if (psm == Psm::CT24 || psm == Psm::Z24) return 24;
    return StorageBitsPerPixel(psm);
}

bool PixelConvert::IsColor(Psm psm) {
    return psm == Psm::CT32 || psm == Psm::CT24 || psm == Psm::CT16 || psm == Psm::CT16S;
}

bool PixelConvert::IsZ(Psm psm) {
    return PsmLayout::Describe(psm).is_z;
}

bool PixelConvert::IsIndexed(Psm psm) {
    return PsmLayout::Describe(psm).is_indexed;
}

uint32_t PixelConvert::ElementPixelMask(Psm psm) {
    const PsmDescriptor& d = PsmLayout::Describe(psm);
    if (d.is_indexed) {
        const uint32_t width_mask = (1u << d.index_bits) - 1u;
        return width_mask << d.index_shift;                       // T8 0xFF, T4 0xF, T8H 0xFF000000, ...
    }
    if (psm == Psm::CT24 || psm == Psm::Z24) return 0x00FFFFFFu;  // bits 31:24 are not the pixel
    if (d.storage_bits == 32) return 0xFFFFFFFFu;
    return (1u << d.storage_bits) - 1u;                           // 0xFFFF for the 16-bit class
}

uint32_t PixelConvert::UnpackColor(Psm psm, uint32_t element) {
    assert(IsColor(psm) && "UnpackColor: not a colour mode (Z -> UnpackZ, indexed -> UnpackIndex)");
    switch (psm) {
        case Psm::CT32:  return element;
        case Psm::CT24:  return element & 0x00FFFFFFu;            // alpha 0: TEXA's job
        case Psm::CT16:
        case Psm::CT16S: return TexaExpand::Expand16To32(uint16_t(element & 0xFFFFu));
        default:         return 0;
    }
}

uint32_t PixelConvert::PackColor(Psm psm, uint32_t rgba32) {
    assert(IsColor(psm) && "PackColor: not a colour mode");
    switch (psm) {
        case Psm::CT32:  return rgba32;
        case Psm::CT24:  return rgba32 & 0x00FFFFFFu;
        case Psm::CT16:
        case Psm::CT16S: return TexaExpand::Truncate32To16(rgba32);
        default:         return 0;
    }
}

PixelConvert::Depth PixelConvert::UnpackZ(Psm psm, uint32_t element) {
    assert(IsZ(psm) && "UnpackZ: not a Z mode (colour -> UnpackColor)");
    return Depth{element & ElementPixelMask(psm)};
}

uint32_t PixelConvert::PackZ(Psm psm, Depth depth) {
    assert(IsZ(psm) && "PackZ: not a Z mode");
    return depth.value & ElementPixelMask(psm);
}

PixelConvert::Depth PixelConvert::DepthFromColorWord(uint32_t rgba32) {
    return Depth{rgba32};
}

uint32_t PixelConvert::UnpackIndex(Psm psm, uint32_t element) {
    const PsmDescriptor& d = PsmLayout::Describe(psm);
    assert(d.is_indexed && "UnpackIndex: not an indexed mode");
    return (element >> d.index_shift) & ((1u << d.index_bits) - 1u);
}

uint32_t PixelConvert::PackIndex(Psm psm, uint32_t element, uint32_t index) {
    const PsmDescriptor& d = PsmLayout::Describe(psm);
    assert(d.is_indexed && "PackIndex: not an indexed mode");
    const uint32_t mask = ElementPixelMask(psm);
    return (element & ~mask) | ((index << d.index_shift) & mask);
}

uint8_t PixelConvert::ScaleAlphaPs2ToGpu(uint8_t ps2_alpha) {
    return uint8_t(ps2_alpha >= 0x80 ? 0xFF : ps2_alpha * 2);
}

uint8_t PixelConvert::ScaleAlphaGpuToPs2(uint8_t gpu_alpha) {
    return uint8_t(gpu_alpha >= 0xFF ? 0x80 : gpu_alpha / 2);
}

}  // namespace gs
}  // namespace ps2r
