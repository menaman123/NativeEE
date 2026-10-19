// render/backend.h -- Backend: the contract between the GS HLE layers and a
// graphics API.
//
// Everything above this line speaks in DrawIr, TextureSource and opaque SurfaceIds;
// everything below it speaks GL / Vulkan / software. No PS2 VRAM address, no PSM
// layout arithmetic and no page / epoch bookkeeping crosses this boundary: the
// target cache owns surfaces by SurfaceId, the texture-source resolver names a
// surface (or decoded bytes) in a TextureSource, and the class renderers lower to
// Draw calls. DrawParams carries the per-draw decisions made above the interface
// (which surfaces, the logical target size, the alt16 route, the per-frame clear);
// DrawResult returns what the coherence tail above needs (the fused bounding boxes,
// the blend route taken, the 5551 write mask, the Cd copy performed).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/gs/hle/ir/cd_dirty_set.h"
#include "engine/render/surface_format.h"

namespace ps2r {
namespace gs {
struct DrawIr;
struct TextureSource;
}  // namespace gs

namespace render {

struct Caps {
    int max_texture_size = 0;
    bool staging_readback = false;   // BeginReadback / PollReadback are asynchronous
    bool reverse_subtract = false;   // GL_FUNC_REVERSE_SUBTRACT-class blend equation
    bool depth_sample = false;       // depth attachment sampleable as a texture
    bool texture_barrier = false;
    bool compute = false;
    bool copy_pack5551 = false;      // CopyRect honours kCopyPack5551 (a GPU pass); false = the caller's CPU route
    bool alpha_lane_write = false;   // WriteAlphaLane is a GPU pass; false = the caller's CPU route
};

struct SurfaceId {
    uint32_t id = 0;                 // 0 = none
};

struct FenceId {
    uint64_t id = 0;                 // 0 = none
};

struct Rect {
    int32_t x = 0, y = 0, w = 0, h = 0;
};

struct SurfaceDesc {
    uint32_t width = 0;              // physical texels
    uint32_t height = 0;
    SurfaceFormat format = SurfaceFormat::Rgba8;
    bool renderable = false;         // colour attachment with a framebuffer object
    uint8_t scale = 1;               // physical / logical factor (1 in the harness profile)
};

// Clear selection for Backend::Clear.
enum ClearFlags : uint32_t { kClearColor = 1u, kClearDepth = 2u };

// Stencil test + update for one draw (API-agnostic; the GL backend maps them to
// glStencilFunc / glStencilMask / glStencilOp). The stencil-fail and depth-fail ops
// are always KEEP; `pass_op` applies to fragments that pass both tests.
struct StencilState {
    enum Func : uint8_t { kAlways = 0, kEqual = 1 };
    enum Op : uint8_t { kKeep = 0, kInvert = 1, kIncrWrap = 2, kDecrWrap = 3 };
    bool enabled = false;
    Func func = kAlways;
    uint8_t ref = 0;
    uint8_t cmp_mask = 0xFF;
    uint8_t write_mask = 0xFF;
    Op pass_op = kKeep;
};

// Which program a draw runs: the PS2 register program (None) or one of the
// stencil-composite specialisations -- StripCount draws the geometry into a
// stencil-only attachment (DATE + colour-authored Z discards in the shader, no
// colour output; the caller's StencilState counts the survivors), Invert is the
// folded byte-NOT self-read of a 32-bit target through its Cd copy.
enum class StencilProgram : uint8_t { None = 0, StripCount = 1, Invert = 2 };

// Register-program uniforms a draw LEAVES AS THE PREVIOUS DRAW WROTE THEM. The
// reference's class-native generic-program arms (skinned_native.inc
// DrawSkinnedLitNative, sprite_hud_native.inc DrawSpriteHudNative,
// particles_native.inc DrawParticlesNative, fmv_blit_native.inc DrawFmvBlitNative --
// each replicates DrawBatch's SetU* sites minus these) never write them, so a
// class-routed draw observes the values of the last full-set draw (a 16-bit frame's
// 5-bit quantisation carried onto the next CT32 accumulator draws, opengl_backend.cpp
// :10817-10823 vs skinned_native.inc:1145-1170). Bits compose per class shape; 0 = the
// full register program (DrawBatch, opengl_backend.cpp:8288-14398).
enum InheritedUniform : uint32_t {
    kInheritCt16Quant = 1u << 0,   // u_ct16q
    kInheritDither = 1u << 1,      // u_dthe, u_dimx_lo, u_dimx_hi
    kInheritDrawScale = 1u << 2,   // u_draw_scale
    kInheritUvNative = 1u << 3,    // u_uv_native
    kInheritBlendAbcd = 1u << 4,   // u_blend_a..d, u_fix_alpha
    kInheritZsrcMap = 1u << 5,     // u_zsrc_num, u_zsrc_den
};

// Cd-copy tracker state a colour target owns (the shader-side blend reads the
// destination from a copy of the attachment; the copy is refreshed lazily over
// the rectangles written since it was last taken -- CdCopyPolicy). Half-open device
// rectangles; last_route records which attachment (main 0 / alt16 companion 1)
// the copy mirrors.
struct CdCopyState {
    bool valid = false;
    // The rects written since the last copy (gs::CdDirtySet: the union and the rects
    // themselves; CdCopyPolicy decides on them, the target's post-draw tail records them).
    gs::CdDirtySet dirty;
    int8_t last_route = -1;
};

struct DrawParams {
    SurfaceId color;                 // the target colour attachment (always set)
    SurfaceId depth;                 // its depth attachment (may be none)
    SurfaceId rt_copy;               // the target's Cd copy texture (none => no sw blend copy)
    SurfaceId alt16;                 // 16-bit companion attachment (bound when alt16_route)
    SurfaceId clut;                  // 256x2 CLUT texture for the indexed serve (unit 2)
    SurfaceId idx_partner;           // fmt=5 partner target (unit 3)
    SurfaceId zsrc;                  // colour-authored Z source for the in-shader compare (unit 4)
    int32_t render_w = 0;            // logical target size this draw rasterises into
    int32_t render_h = 0;
    int32_t target_w = 0;            // logical allocation size of the target (grow-only)
    int32_t target_h = 0;
    uint8_t scale = 1;               // target scale (physical = logical * scale)
    bool has_32bit_content = false;
    bool alt16_route = false;        // draw into the companion instead of the main attachment
    bool rt_copy_available = false;  // rt_copy names a live texture
    CdCopyState* cd_copy = nullptr;  // the target's tracker (mutated by the draw)
    bool zsrc_armed = false;         // the S9 colour-authored Z compare route
    uint8_t zsrc_scale = 1;          // scale of the zsrc surface
    bool do_frame_clear = false;     // per-game-frame clear before this draw
    bool clear_color = false;        // the clear also clears colour (scanout target)
    StencilProgram program = StencilProgram::None;
    SurfaceId stencil;               // the DepthStencil24_8 attachment (StripCount draws into it alone)
    StencilState stencil_state;      // applied when program == StripCount
    uint32_t inherited_uniforms = 0; // InheritedUniform bits this draw does not write (0 = full set)
};

struct DrawResult {
    bool drew = false;               // false when the guards rejected the draw
    bool bbox_valid = false;
    float bbox_tri[4] = {0, 0, 0, 0};   // x0 y0 x1 y1 over the triangle-complete vertices
    float bbox_all[4] = {0, 0, 0, 0};   // extended over the <= 2 tail vertices
    bool alt16_route = false;
    bool sw_blend_active = false;    // shader-side blend engaged (rt_copy bound)
    bool needs_rt_copy = false;
    bool route_rt_blend = false;     // blended RT read mapped to exact fixed-function
    bool wrap_route = false;         // COLCLAMP=0 wrap routed to the shader blend
    bool fbmsk16_exact = false;      // 5551 mask not channel-aligned
    bool fbmsk16_engaged = false;    // the exact merge ran in the shader
    uint32_t fbmsk16 = 0;            // FRAME.FBMSK folded to the 5551 layout
    bool cd_copy_performed = false;
    Rect cd_copy_rect;               // logical rect copied (w == 0 => whole target)
    bool halfword_shuffle = false;   // the 16-bit column pass ran as a half-word copy over the
                                     // 32-bit image (the target keeps its 32-bit tags)
    Rect write_rect;                 // bbox_all ∩ scissor ∩ target, logical, half-open
};

// The backend's call census (diagnostics only; never consulted by a draw): one
// counter per class of API call the backend ISSUED (a call its state shadow
// skipped is not counted), monotonic over the backend's life. A reader takes two
// snapshots and subtracts. `readbacks` are the GPU -> host pixel reads (each a
// full pipeline drain on an immediate-mode API), `copies` the attachment ->
// texture copies, `uploads` the host -> texture transfers (bytes as handed to the
// API), `buffer_maps` the vertex-stream maps, `draw_calls` every rasterising call
// (draws, quads, presents).
struct CallCensus {
    uint64_t draw_calls = 0;
    uint64_t texture_binds = 0;
    uint64_t program_binds = 0;
    uint64_t framebuffer_binds = 0;
    uint64_t readbacks = 0;
    uint64_t readback_bytes = 0;
    uint64_t copies = 0;
    uint64_t copy_bytes = 0;
    uint64_t uploads = 0;
    uint64_t upload_bytes = 0;
    uint64_t buffer_maps = 0;
    uint64_t clears = 0;
    uint64_t barriers = 0;           // texture barriers issued in place of a destination copy
};

// Surface preserves retained callers' sampling. Owned presentation declares
// its window filter explicitly; this never changes a draw texture's sampler.
enum class PresentationFilter : uint8_t { Surface, Nearest, Linear };

struct ScanoutDesc {
    SurfaceId source;
    Rect src;                        // physical texels; all-zero means the complete source
    Rect dst;                        // window pixels
    bool flip_y = true;              // targets store PS2 top at GL bottom
    PresentationFilter filter = PresentationFilter::Surface;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual Caps QueryCaps() const = 0;
    virtual SurfaceId CreateSurface(const SurfaceDesc& desc) = 0;
    virtual void DestroySurface(SurfaceId id) = 0;
    virtual SurfaceDesc DescribeSurface(SurfaceId id) const = 0;
    virtual void Clear(SurfaceId color, SurfaceId depth, uint32_t rgba, float z,
                       uint32_t flags = kClearColor | kClearDepth) = 0;
    // Attachment row order, stride in bytes (zero means tightly packed):
    // RGBA8/R8 bytes or Depth32f floats, matching DescribeSurface's format.
    virtual void Upload(SurfaceId id, const Rect& rect, const void* bytes, size_t stride) = 0;
    // Mip levels of a texture surface (level 0 is the surface itself).
    virtual void UploadLevel(SurfaceId id, int level, int w, int h, const void* rgba) = 0;
    virtual void FinishMipChain(SurfaceId id, int authored_levels, bool synthesize_tail) = 0;
    virtual DrawResult Draw(const gs::DrawIr& ir, const gs::TextureSource& tex,
                            const DrawParams& params) = 0;
    // Stencil-composite primitives. ClearStencil writes `value` to every stencil
    // texel of a DepthStencil24_8 surface. DrawConstQuad rasterises a constant
    // colour over `scissor` (logical texels of the stencil surface) under the
    // stencil test -- into `color` with `stencil` attached when `color` is set
    // (alpha written only when `write_alpha`), or into the stencil attachment
    // alone when `color` is none (colour writes off; the pass_op updates the
    // stencil). Blend off, depth test ALWAYS without writes.
    virtual void ClearStencil(SurfaceId stencil, uint8_t value) = 0;
    virtual void DrawConstQuad(SurfaceId color, SurfaceId stencil, const Rect& scissor,
                               uint32_t rgba, bool write_alpha, const StencilState& st) = 0;
    // Copy `src_rect` of `src` to `dst_rect` of `dst` (same size, physical texels).
    // `transform` kCopyPlain = the bytes as they are; kCopyPack5551 = every texel
    // through the 16-bit round trip (TexaExpand::Truncate32To16 then Expand16To32:
    // 5-bit channels replicated, alpha bit -> 0x00 / 0xFF) -- the 16-bit companion
    // refresh, on the GPU; a backend without the pass falls back to the plain copy.
    static constexpr uint32_t kCopyPlain = 0;
    static constexpr uint32_t kCopyPack5551 = 1;
    virtual void CopyRect(SurfaceId src, const Rect& src_rect, SurfaceId dst,
                          const Rect& dst_rect, uint32_t transform) = 0;
    // Write ONLY the alpha channel of `dst` over `dst_rect` (physical texels) from
    // the R8 surface `lane`: texel (x, y) of dst_rect takes lane texel ((x - dst_rect.x)
    // / scale, (y - dst_rect.y) / scale) where `scale` = dst's physical / logical
    // factor -- the lane is LOGICAL (one byte per logical texel of the rect); RGB is
    // untouched; texels inside `exclude` (physical, half-open; w or h <= 0 = none)
    // keep their alpha. The alias import's stored-alpha merge on the GPU (a byte
    // round-trips exactly through UNORM8). A backend without the pass does nothing
    // (Caps::alpha_lane_write false): the caller takes its CPU route.
    virtual void WriteAlphaLane(SurfaceId, const Rect&, SurfaceId, const Rect&) {}
    // Synchronous readback of `rect` (physical texels), rows bottom-up as the
    // attachment stores them, RGBA8 (colour) or float (depth).
    virtual void Readback(SurfaceId id, const Rect& rect, void* out, size_t stride) = 0;
    virtual FenceId BeginReadback(SurfaceId id, const Rect& rect) = 0;
    virtual bool PollReadback(FenceId fence, void* out, size_t stride) = 0;
    // A missing nonzero source or invalid explicit source rectangle refuses
    // before clear/swap; source 0 retains the cold no-image presentation.
    virtual void Present(const ScanoutDesc& desc) = 0;
    // The presenter's OUTPUT for a frame dump: while armed, Present keeps the
    // window image it drew (the default framebuffer after the display quad,
    // before the swap) and ReadPresented hands over that RGBA8 image (rows
    // bottom-up, the destination rectangle's size). Diagnostics only: never a
    // draw, scanout or mirror input. A backend without a window reads false.
    virtual void ArmPresentCapture(bool) {}
    virtual bool ReadPresented(std::vector<uint8_t>*, int*, int*) { return false; }
    // The call census (diagnostics; null = the backend keeps none).
    virtual const CallCensus* Census() const { return nullptr; }
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
};

}  // namespace render
}  // namespace ps2r
