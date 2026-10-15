#pragma once

#include <cstdint>

namespace ps2r::ipu {

// EE User's Manual 8.6: IPU colour conversion, ordered dither and VQ.
class IpuColor {
public:
    static uint32_t Convert(uint8_t y, uint8_t cb, uint8_t cr);
    static uint32_t Threshold(uint32_t rgba, uint16_t th0, uint16_t th1);
    static uint16_t Pack(uint32_t rgba, unsigned x, unsigned y, bool dither);
    static uint8_t Quantize(uint16_t rgb, const uint16_t* clut);
};

}  // namespace ps2r::ipu
