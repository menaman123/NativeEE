// gs/hle/targets/target_identity.h -- TargetIdentity: what names a render
// target. A target is keyed by FRAME.FBP (bytes); its logical size is grow-only,
// its PSM / FBW tags follow the most recent draw (GS User's Manual 3.4.2 FRAME:
// the buffer is described by base, width and pixel format), and its scale is
// the physical / logical factor the ScalePolicy assigned at creation.
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

struct TargetIdentity {
    uint32_t fbp = 0;             // FRAME.FBP in bytes (the map key)
    int32_t width = 0;            // logical allocation (grow-only)
    int32_t height = 0;
    uint32_t render_psm = 0;      // FRAME.PSM of the last draw
    uint32_t render_fbw = 0;      // FRAME.FBW (pixels) of the last draw
    uint8_t scale = 1;
    bool has_32bit_content = false;  // a 32-bit-class FRAME ever drew here (alt16 bifurcation key)
    bool was_32bit = false;
    bool was_16bit = false;

    int32_t PhysicalW() const { return width * scale; }
    int32_t PhysicalH() const { return height * scale; }
};

}  // namespace gs
}  // namespace ps2r
