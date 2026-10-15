// render/surface_format.h -- the pixel formats a backend surface can have.
//
// Rgba8 backs every colour target and every decoded / uploaded texture (the GS
// colour formats are expanded to 8-bit channels before they reach a backend);
// Depth32f backs the Z attachment (the vertex shader maps the GS 32-bit Z to
// window depth z / 2^32, so a 32-bit float attachment keeps the 24 high bits
// exact); R8 is reserved for single-channel index views; DepthStencil24_8 is the
// packed depth + 8-bit stencil attachment the stencil-counting passes draw into (its
// depth half is never read: the passes force the depth test ALWAYS).
#pragma once

#include <cstdint>

namespace ps2r {
namespace render {

enum class SurfaceFormat : uint8_t { Rgba8 = 0, Depth32f = 1, R8 = 2, DepthStencil24_8 = 3 };

}  // namespace render
}  // namespace ps2r
