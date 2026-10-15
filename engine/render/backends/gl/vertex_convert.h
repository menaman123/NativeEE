// render/backends/gl/vertex_convert.h -- VertexConvert: DrawIr vertices -> GpuVertex
// with the two x/y bounding boxes fused into the single pass.
//
// Arithmetic per vertex:
//   UvFixed    u = uv_u / 16.0f, v = uv_v / 16.0f, q = 1      (UV register, 12.4 fixed)
//   Stq        u = s * uv_tex_w, v = t * uv_tex_h, q = q      (STQ registers, texel space)
//   Untextured u = v = 0, q = 1
// r/g/b/a/fog are copied and the three pad bytes zeroed (one 4-byte store of the fog
// byte). Both boxes are seeded from vertex 0 and accumulated with strict compares
// ("if (x < min) min = x"): `tri` over the triangle-complete prefix [0, (n/3)*3),
// `all` extended over the <= 2 tail vertices. A NaN coordinate keeps every compare
// false, as the reference loops do.
#pragma once

#include <cstddef>

#include "engine/render/backends/gl/gpu_vertex.h"

namespace ps2r {
namespace gs {
struct VertexRec;
}

namespace render {

class VertexConvert {
public:
    enum class UvMode : int { Untextured = 0, UvFixed = 1, Stq = 2 };

    struct Bbox {
        float x0, y0, x1, y1;
    };
    struct FusedBboxes {
        Bbox tri;
        Bbox all;
        bool valid;   // n >= 3
    };

    // dst must hold >= n entries.
    static FusedBboxes Convert(const gs::VertexRec* src, size_t n, GpuVertex* dst, UvMode mode,
                               int uv_tex_w, int uv_tex_h);

private:
    static void ConvertRange(const gs::VertexRec* src, size_t i0, size_t i1, GpuVertex* dst,
                             UvMode mode, float fw, float fh, Bbox& box);
};

}  // namespace render
}  // namespace ps2r
