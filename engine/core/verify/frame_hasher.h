// core/verify/frame_hasher.h -- per-present FNV-1a64 of the NATIVE-scale
// presented scanout (EQUIVALENCE_PLAN section 1.1 / 2, the frames lane).
//
// Input: the scanout colour attachment at the present seam -- PHYSICAL dims
// (native * scale), RGBA, bottom-up rows (glReadPixels order), read after
// the scanout was composed and before any window upscale / GUI overlay /
// interpolation.
//
// DOWNSAMPLE RULE: at video.scale S > 1 the native image is recovered by
// point-sampling the top-left texel of each SxS block (native pixel (x,y)
// := physical texel (x*S, y*S)). At S == 1 -- the deterministic-profile
// contract -- this is the identity and the hash covers every presented
// pixel. S > 1 hashes are a stable fingerprint, not the bit-identity claim.
//
// Hash covers: u32 native_w LE, u32 native_h LE, then R,G,B per pixel in
// TOP-DOWN row-major order (alpha excluded: the scanout presents RGB).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2r::verify {

class FrameHasher {
public:
    // rgba_bottomup: pw*ph*4 bytes, row 0 = BOTTOM row. scale: integer
    // physical/native ratio (>= 1). Returns 0 for no readable scanout
    // (null buffer / empty dims). pw/ph not multiples of scale truncate.
    static uint64_t HashNativeScale(const uint8_t* rgba_bottomup, int pw, int ph, int scale);
};

}  // namespace ps2r::verify
