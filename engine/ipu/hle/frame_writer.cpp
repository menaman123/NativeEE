// engine/ipu/hle/frame_writer.cpp -- FrameWriter strip layouts (see frame_writer.h).
#include "engine/ipu/hle/frame_writer.h"

namespace ps2r::ipu {

namespace {

uint32_t AlignMacroblock(uint32_t v) { return (v + 15u) & ~15u; }

}  // namespace

size_t FrameWriter::Rgb32Bytes(uint32_t w, uint32_t h) {
    return static_cast<size_t>(AlignMacroblock(w)) * static_cast<size_t>(AlignMacroblock(h)) * 4u;
}

size_t FrameWriter::Rgb16Bytes(uint32_t w, uint32_t h) {
    return static_cast<size_t>(AlignMacroblock(w)) * static_cast<size_t>(AlignMacroblock(h)) * 2u;
}

void FrameWriter::WriteRgb32Strips(uint8_t* dst_base, const DecodedPicture& pic) {
    if (!dst_base || pic.width <= 0 || pic.height <= 0 || pic.rgba.empty()) return;

    const uint32_t W = static_cast<uint32_t>(pic.width);
    const uint32_t H = static_cast<uint32_t>(pic.height);
    const uint32_t aligned_w = AlignMacroblock(W);
    const uint32_t aligned_h = AlignMacroblock(H);
    const uint32_t mb_cols = aligned_w / 16u;

    // For each 16-wide column strip, `aligned_h` rows of 16*4 bytes.
    for (uint32_t mbx = 0; mbx < mb_cols; ++mbx) {
        const size_t strip_off = static_cast<size_t>(mbx) * static_cast<size_t>(aligned_h) * 16u * 4u;
        for (uint32_t y = 0; y < aligned_h; ++y) {
            uint8_t* dst = dst_base + strip_off + static_cast<size_t>(y) * 16u * 4u;
            for (uint32_t x = 0; x < 16; ++x) {
                const uint32_t sx = mbx * 16u + x;
                const uint32_t sy = y;
                if (sx < W && sy < H) {
                    const uint8_t* src = pic.rgba.data() +
                        (static_cast<size_t>(sy) * static_cast<size_t>(W) + sx) * 4u;
                    dst[x * 4 + 0] = src[0];
                    dst[x * 4 + 1] = src[1];
                    dst[x * 4 + 2] = src[2];
                    dst[x * 4 + 3] = 0x80;
                } else {
                    dst[x * 4 + 0] = 0;
                    dst[x * 4 + 1] = 0;
                    dst[x * 4 + 2] = 0;
                    dst[x * 4 + 3] = 0x80;
                }
            }
        }
    }
}

void FrameWriter::WriteRgb16Strips(uint8_t* dst_base, uint32_t target_w, uint32_t target_h,
                                   const DecodedPicture& pic) {
    if (!dst_base || target_w == 0 || target_h == 0 || pic.rgba.empty()) return;

    const uint32_t aligned_w = AlignMacroblock(target_w);
    const uint32_t aligned_h = AlignMacroblock(target_h);
    const uint32_t mb_cols = aligned_w / 16u;
    const uint32_t fw = static_cast<uint32_t>(pic.width);
    const uint32_t fh = static_cast<uint32_t>(pic.height);

    // For each 16-wide column strip, `aligned_h` rows of 16*2 bytes.
    for (uint32_t mbx = 0; mbx < mb_cols; ++mbx) {
        const size_t strip_off = static_cast<size_t>(mbx) * static_cast<size_t>(aligned_h) * 16u * 2u;
        for (uint32_t y = 0; y < aligned_h; ++y) {
            uint8_t* dst = dst_base + strip_off + static_cast<size_t>(y) * 16u * 2u;
            for (uint32_t x = 0; x < 16; ++x) {
                const uint32_t sx = mbx * 16u + x;
                uint16_t px;
                if (sx < fw && y < fh) {
                    const uint8_t* src = pic.rgba.data() +
                        (static_cast<size_t>(y) * static_cast<size_t>(fw) + sx) * 4u;
                    const uint16_t r5 = static_cast<uint16_t>(src[0] >> 3);
                    const uint16_t g5 = static_cast<uint16_t>(src[1] >> 3);
                    const uint16_t b5 = static_cast<uint16_t>(src[2] >> 3);
                    px = static_cast<uint16_t>(r5 | (g5 << 5) | (b5 << 10) | 0x8000u);
                } else {
                    px = 0x8000u;
                }
                dst[x * 2 + 0] = static_cast<uint8_t>(px & 0xFF);
                dst[x * 2 + 1] = static_cast<uint8_t>((px >> 8) & 0xFF);
            }
        }
    }
}

}  // namespace ps2r::ipu
