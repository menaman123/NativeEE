// gs/hle/targets/surface_views.h -- SurfaceViews: the backend surfaces one render
// target owns. The colour attachment holds the 32-bit view of the buffer, the
// depth attachment its Z (a 32-bit float keeps the 24 high Z bits exact), the
// Cd copy is what the shader-side blend reads the destination from, and the
// alt16 companion is the 16-bit view a 16-bit FRAME draws into once the same
// buffer has held 32-bit content (two pixel formats aliasing one address range
// cannot share one RGBA8 attachment without losing the 5551 quantisation).
#pragma once

#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

struct SurfaceViews {
    render::SurfaceId color;
    render::SurfaceId depth;
    render::SurfaceId rt_copy;
    render::SurfaceId alt16;
    bool alt16_has_content = false;

    bool HasColor() const { return color.id != 0; }
    bool HasAlt16() const { return alt16.id != 0; }
};

}  // namespace gs
}  // namespace ps2r
