// core/verify/frame_hasher.cpp
#include "engine/core/verify/frame_hasher.h"

#include "engine/core/hash/hash.h"

namespace ps2r::verify {

uint64_t FrameHasher::HashNativeScale(const uint8_t* rgba_bottomup, int pw, int ph, int scale) {
    if (!rgba_bottomup || pw <= 0 || ph <= 0) return 0;
    if (scale < 1) scale = 1;
    const int nw = pw / scale;
    const int nh = ph / scale;
    if (nw <= 0 || nh <= 0) return 0;

    uint64_t h = hash::kFnv1a64Offset;
    // Header: native dims, little-endian u32 each -- a display-mode switch
    // is a real frame event and must change the hash.
    const uint32_t dims[2] = {(uint32_t)nw, (uint32_t)nh};
    uint8_t hdr[8];
    for (int i = 0; i < 2; i++) {
        hdr[i * 4 + 0] = (uint8_t)(dims[i] & 0xFF);
        hdr[i * 4 + 1] = (uint8_t)((dims[i] >> 8) & 0xFF);
        hdr[i * 4 + 2] = (uint8_t)((dims[i] >> 16) & 0xFF);
        hdr[i * 4 + 3] = (uint8_t)((dims[i] >> 24) & 0xFF);
    }
    h = hash::Fnv1a64(hdr, sizeof(hdr), h);

    // Pixels: top-down native rows over a bottom-up buffer; native (x,y)
    // point-samples physical texel (x*scale, y*scale).
    for (int y = 0; y < nh; y++) {
        const int src_y = ph - 1 - y * scale;
        const uint8_t* row = rgba_bottomup + (size_t)src_y * pw * 4;
        for (int x = 0; x < nw; x++) {
            const uint8_t* px = row + (size_t)(x * scale) * 4;
            h ^= px[0]; h *= hash::kFnv1a64Prime;
            h ^= px[1]; h *= hash::kFnv1a64Prime;
            h ^= px[2]; h *= hash::kFnv1a64Prime;
        }
    }
    return h;
}

}  // namespace ps2r::verify
