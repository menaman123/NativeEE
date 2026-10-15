// render/backends/gl/gpu_vertex.h -- GpuVertex: the 32-byte vertex the PS2 draw program
// consumes (attribute 0 position xyz, 1 texcoord u v q, 2 colour rgba8 normalised,
// 3 fog u8 normalised). x/y are PS2 window pixels, z the 32-bit depth as float;
// u/v are texel-space coordinates (S*tex_w, T*tex_h for STQ; UV/16 for FST=1) and q
// the raw Q so the fragment shader can do the GS per-pixel divide (q = 1 when the
// divide must be a no-op).
#pragma once

#include <cstdint>

namespace ps2r {
namespace render {

struct GpuVertex {
    float x, y, z;
    float u, v, q;
    uint8_t r, g, b, a;
    uint8_t fog;
    uint8_t pad[3];
};

static_assert(sizeof(GpuVertex) == 32, "GpuVertex is the 32-byte attribute layout");

}  // namespace render
}  // namespace ps2r
