// gs/hle/ir/vertex_rec.h -- one post-kick vertex, in flight AND on disk.
//
// The DrawIr vertex is the VertexKick output after XYOFFSET subtraction: x/y in
// pixels (the 12.4 XYZ fields divided by 16, minus the context's XYOFFSET), z as
// the unsigned 24/32-bit depth converted to float, the STQ registers as written,
// the UV register as raw 12.4 halfwords, RGBAQ's colour bytes, the per-kick fog
// coefficient and whether the kick drew (XYZ2/XYZF2 with ADC clear) or only
// queued (XYZ3/XYZF3 or ADC set). The same 36-byte little-endian record is the
// `.gsdl` VertexRec, so recorder and reader copy it verbatim.
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

#pragma pack(push, 1)
struct VertexRec {
    float x, y, z;
    float s, t, q;
    uint16_t uv_u, uv_v;
    uint8_t r, g, b, a;
    uint8_t fog;
    uint8_t draw_kick;
    uint8_t pad[2];
};
#pragma pack(pop)

static_assert(sizeof(VertexRec) == 36, "VertexRec is 36 bytes on disk");

}  // namespace gs
}  // namespace ps2r
