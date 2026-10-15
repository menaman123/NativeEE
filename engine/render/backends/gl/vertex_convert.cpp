// render/backends/gl/vertex_convert.cpp -- VertexConvert implementation.
#include "engine/render/backends/gl/vertex_convert.h"

#include <cstdint>
#include <cstring>

#include "engine/gs/hle/ir/vertex_rec.h"

namespace ps2r {
namespace render {

void VertexConvert::ConvertRange(const gs::VertexRec* src, size_t i0, size_t i1, GpuVertex* dst,
                                 UvMode mode, float fw, float fh, Bbox& box) {
    float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
    for (size_t i = i0; i < i1; i++) {
        const gs::VertexRec& v = src[i];
        GpuVertex& gv = dst[i];
        const float x = v.x, y = v.y;
        gv.x = x;
        gv.y = y;
        gv.z = v.z;
        switch (mode) {
            case UvMode::UvFixed:
                gv.u = static_cast<float>(v.uv_u) / 16.0f;
                gv.v = static_cast<float>(v.uv_v) / 16.0f;
                gv.q = 1.0f;
                break;
            case UvMode::Stq:
                gv.u = v.s * fw;
                gv.v = v.t * fh;
                gv.q = v.q;
                break;
            case UvMode::Untextured:
            default:
                gv.u = 0.0f;
                gv.v = 0.0f;
                gv.q = 1.0f;
                break;
        }
        gv.r = v.r;
        gv.g = v.g;
        gv.b = v.b;
        gv.a = v.a;
        const uint32_t fogpad = v.fog;   // little-endian: byte 0 = fog, bytes 1..3 = 0
        std::memcpy(&gv.fog, &fogpad, 4);
        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (x > x1) x1 = x;
        if (y > y1) y1 = y;
    }
    box = {x0, y0, x1, y1};
}

VertexConvert::FusedBboxes VertexConvert::Convert(const gs::VertexRec* src, size_t n,
                                                  GpuVertex* dst, UvMode mode, int uv_tex_w,
                                                  int uv_tex_h) {
    FusedBboxes out{};
    out.valid = false;
    if (n == 0) return out;
    const size_t tri_n = (n / 3) * 3;
    const float fw = static_cast<float>(uv_tex_w);
    const float fh = static_cast<float>(uv_tex_h);
    Bbox box{src[0].x, src[0].y, src[0].x, src[0].y};
    ConvertRange(src, 0, tri_n, dst, mode, fw, fh, box);
    out.tri = box;
    ConvertRange(src, tri_n, n, dst, mode, fw, fh, box);
    out.all = box;
    out.valid = (n >= 3);
    return out;
}

}  // namespace render
}  // namespace ps2r
