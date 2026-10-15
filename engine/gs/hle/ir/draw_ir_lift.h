// gs/hle/ir/draw_ir_lift.h -- DrawIrLift: one GIF packet lifted to draw level.
//
// The register-op form (DirectOps) stops at register writes: each vertex is
// three items (ST, RGBAQ, XYZ*) the consumer re-decodes. This lift resolves the
// attribute writes between two kicks into the later vertex's write mask and
// final values, so a consumer performs masked attribute stores and one vertex
// kick per Vert -- state evolution store-for-store identical to the op form at
// every observation point (each kick, each register write, each flush).
//
// Exactness rules:
//   * Only fields written since the previous kick (or packet start) are masked
//     in; unmasked fields inherit the live GS state exactly as the op replay
//     would. ST's Q store is conditional on the payload Q != 0.0f (GS manual
//     7.3.1.3 ST: Q is updated only when not 0), evaluated at lift time with the
//     same scalar compare. The UV-vs-ST recency flag is carried explicitly.
//   * Attribute writes NOT followed by a kick before a non-vertex boundary (a
//     register write, PRIM, EOP flush, packet end) are emitted as a kAttr item
//     AT that boundary, so their stores keep their stream position.
//   * PRIM (PRE bit or PACKED descriptor 0x00) is its own kPrim item; A+D,
//     PACKED default descriptors and REGLIST items stay kState register writes.
//   * XYZ2/XYZF2/XYZ3/XYZF3 decode: x/y unsigned 12.4 -> float/16; z 24-bit at
//     bits 67:44 (XYZF) or 32-bit at 63:32 (XYZ) -> float; fog byte 75:68; ADC bit
//     111. A kick draws only for XYZ2/XYZF2 with ADC clear (manual 7.3.1.5-8).
//   * Decline set (return false) identical to DirectOps::PacketToOps.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2r {
namespace gs {

class DrawIrLift {
public:
    // Attribute write-mask bits: which fields a Vert / kAttr item stores.
    enum : uint8_t {
        kMaskRGBA = 1 << 0,
        kMaskST = 1 << 1,
        kMaskQ = 1 << 2,
        kMaskUV = 1 << 3,
        kMaskUVW = 1 << 4,   // the uv-written-since-st flag
        kMaskFOGF = 1 << 5,  // the fog flag (a bool: FOG byte != 0)
    };
    enum : uint8_t {
        kFlagDraw = 1 << 0,     // the kick draws
        kFlagUVWVal = 1 << 1,   // value stored for kMaskUVW
        kFlagFOGFVal = 1 << 2,  // value stored for kMaskFOGF
    };

    struct Vert {
        float x, y, z;       // kick arguments (12.4 -> float; z = float(zu))
        float s, t, q;       // resolved STQ stores (valid per mask)
        uint16_t u, v;       // resolved UV stores, raw 12.4 (valid per mask)
        uint8_t r, g, b, a;  // resolved RGBA stores (valid per mask)
        uint8_t f;           // per-kick fog argument (XYZF*; 0 for XYZ*)
        uint8_t mask;        // kMask* stores preceding this kick
        uint8_t flags;       // kFlag*
        uint8_t pad;
    };

    enum Kind : uint16_t {
        kState = 1,    // register write of `lo` to `reg`
        kPrim = 2,     // PRIM write of `lo`
        kDrawRun = 3,  // verts[first .. first+count)
        kAttr = 4,     // masked attribute stores with no kick; payload verts[first]
        kFlush = 5,    // GIFtag EOP
        kSrcId = 6,    // lo = image hash; hi = handler<<32 | kick_ord<<16 | pc
        kSrcName = 7,  // lo = same-process pointer to the static producer name
    };

    struct Item {
        uint64_t lo = 0;
        uint64_t hi = 0;
        uint32_t first = 0;
        uint32_t count = 0;
        uint16_t reg = 0;
        uint16_t kind = 0;
    };

    struct Stream {
        std::vector<Item> items;
        std::vector<Vert> verts;
        bool empty() const { return items.empty(); }
    };

    // Convert ONE complete packet, appending to `out`. False = declined.
    static bool PacketToDrawIR(const uint8_t* p, size_t n, Stream& out);
};

}  // namespace gs
}  // namespace ps2r
