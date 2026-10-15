#include "engine/ipu/ipu_color.h"

#include <algorithm>

namespace ps2r::ipu {

uint32_t IpuColor::Convert(uint8_t y, uint8_t cb, uint8_t cr) {
    // Manual 8.6.1: products lose six fractional bits before addition,
    // then the remaining fractional bit is rounded. Use floor division
    // explicitly for negative chroma products.
    const auto half = [](int value) { return value >= 0 ? value / 64 : -((-value + 63) / 64); };
    const int yy = half(149 * std::max(0, static_cast<int>(y) - 16));
    const int cc = static_cast<int>(cb) - 128;
    const int rr = static_cast<int>(cr) - 128;
    const auto channel = [](int value) {
        return static_cast<uint32_t>(std::clamp((value + 1) / 2, 0, 255));
    };
    return 0x80000000u | channel(yy + half(204 * rr)) |
           (channel(yy - half(50 * cc) - half(104 * rr)) << 8) |
           (channel(yy + half(258 * cc)) << 16);
}

uint32_t IpuColor::Threshold(uint32_t rgba, uint16_t th0, uint16_t th1) {
    const uint32_t peak = std::max({rgba & 255u, (rgba >> 8) & 255u, (rgba >> 16) & 255u});
    if (peak < th0) return 0;
    return (rgba & 0x00ffffffu) | (peak < th1 ? 0x40000000u : 0x80000000u);
}

uint16_t IpuColor::Pack(uint32_t rgba, unsigned x, unsigned y, bool dither) {
    // Manual 8.6.2 gives half-unit coefficients. Work in doubled units so
    // the half steps are retained before truncating to five bits.
    static constexpr int kDither[4][4] = {
        {-8, 0, -6, 2}, {4, -4, 6, -2}, {-5, 3, -7, 1}, {7, -1, 5, -3}
    };
    const int offset = dither ? kDither[y & 3][x & 3] : 0;
    const auto channel = [offset](uint32_t value) {
        return std::clamp(static_cast<int>(value & 255u) * 2 + offset, 0, 510) / 16;
    };
    // The IPU's A1 is the *translucency* flag, not the high bit of A8.
    return static_cast<uint16_t>(channel(rgba) | (channel(rgba >> 8) << 5) |
                                 (channel(rgba >> 16) << 10) |
                                 ((rgba >> 24) == 0x40u ? 0x8000 : 0));
}

uint8_t IpuColor::Quantize(uint16_t rgb, const uint16_t* clut) {
    unsigned best_distance = 3 * 31 * 31 + 1;
    uint8_t best = 0;
    for (uint8_t i = 0; i < 16; ++i) {
        unsigned distance = 0;
        for (unsigned shift : {0u, 5u, 10u}) {
            const int delta = ((rgb >> shift) & 31) - ((clut[i] >> shift) & 31);
            distance += static_cast<unsigned>(delta * delta);
        }
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

}  // namespace ps2r::ipu
