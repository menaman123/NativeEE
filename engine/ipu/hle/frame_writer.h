// engine/ipu/hle/frame_writer.h -- FrameWriter: lays a decoded picture out the way
// the IPU's CSC output lands in EE RAM for the libmpeg driver: 16-pixel-wide
// vertical macroblock-column strips, each `aligned_h` rows tall, one after the
// other. Pure byte layout into a caller-supplied span (the caller translates the
// guest address and sizes the span with Rgb32Bytes / Rgb16Bytes).
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/ipu/hle/decoded_picture.h"

namespace ps2r::ipu {

class FrameWriter {
public:
    // Bytes the RGB32 layout of a `w`x`h` picture occupies: both dimensions
    // rounded up to the 16-pixel macroblock, 4 bytes per pixel.
    static size_t Rgb32Bytes(uint32_t w, uint32_t h);
    // Same for the PSMCT16 layout: 2 bytes per pixel.
    static size_t Rgb16Bytes(uint32_t w, uint32_t h);

    // RGB32 (PSMCT32): R,G,B from the picture, A = 0x80 (the PS2 "fully opaque"
    // alpha; the decoder's 0xFF is not a PS2 value). Pixels outside the picture
    // but inside the aligned rectangle are opaque black. `dst` holds
    // Rgb32Bytes(pic.width, pic.height). No-op on an empty picture.
    static void WriteRgb32Strips(uint8_t* dst, const DecodedPicture& pic);

    // PSMCT16: R bits 0-4, G 5-9, B 10-14, A bit 15 set; little-endian 16-bit
    // stores. The strip geometry is the `target_w`x`target_h` plane the game
    // allocated; picture samples exist only inside the decoded extent, the
    // rest of the plane is 0x8000 (opaque black). `dst` holds
    // Rgb16Bytes(target_w, target_h). No-op on a zero target or empty picture.
    static void WriteRgb16Strips(uint8_t* dst, uint32_t target_w, uint32_t target_h,
                                 const DecodedPicture& pic);
};

}  // namespace ps2r::ipu
