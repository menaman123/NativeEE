// engine/ipu/hle/decoded_picture.h -- DecodedPicture: one decoded video picture,
// RGBA8 row-major (4 bytes per pixel, `width * 4` bytes per row), the unit the
// video decoder produces and the frame writer consumes. A method-less POD.
#pragma once

#include <cstdint>
#include <vector>

namespace ps2r::ipu {

struct DecodedPicture {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

}  // namespace ps2r::ipu
