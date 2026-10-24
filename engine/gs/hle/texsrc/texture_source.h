// gs/hle/texsrc/texture_source.h -- TextureSource: what a draw samples and how.
//
// The resolver turns a DrawIr's TexReadView plus the live target set into this
// record: the OWNER of the texels (a render-target surface served directly, a
// decoded texture, or nothing), the coordinate TRANSFORM the
// backend applies to the recorded S/T/UV before rasterisation (texel-space scale,
// shuffle scale, region rebase, the depth / indexed serve formats and their
// per-draw parameters), the API-agnostic SAMPLER state, and -- when the read was
// declined -- why. The backend consumes this struct and never resolves anything
// itself. Declarations only; every member is plain data.
#pragma once

#include <cstdint>

#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

struct TextureSource {
    enum class OwnerKind : uint8_t { None = 0, Surface = 1, Decoded = 2 };

    struct Owner {
        OwnerKind kind = OwnerKind::None;
        render::SurfaceId surface;   // Surface: the target attachment (colour / alt16 / depth /
                                     // the fmt 5 lane surface); Decoded: the texture
        uint8_t fmt_case = 0;        // 0 colour, 1..3 depth view (Z32 / Z16 / Z16 view of Z32),
                                     // 4..6 indexed view (T8H of CT32 / T8H of CT16 pair / T8 of CT16),
                                     // 7 depth-byte palette: clut[(Z >> idx_self) & 0xFF] of a
                                     // Depth32f surface (an owned effect's intent; NEAREST)
        bool self_serve = false;     // fmt 4: the index source is the draw's own target
        bool owned = false;          // Decoded: the texture is an asset the engine owns at load
                                     // (textures-at-load / the typed renderer), never a VRAM-mirror
                                     // decode -- the player's filtering enhancement applies to it
        uint8_t levels = 0;          // authored mip levels on the texture (0 = none)
        uint8_t synth_levels = 0;    // synthesized tail levels past the authored chain
        render::SurfaceId clut;      // fmt 4..6: the 256x2 palette texture
        render::SurfaceId partner;   // fmt 5: the partner target's colour attachment
        bool alt16_serve = false;    // Surface: the companion (5551 mirror) of a 32-bit-content target
        render::SurfaceId alt16_main;   // ... and that target's 32-bit colour image (the half-word
                                        // copy reads the bytes the companion cannot carry)
    };

    struct RegionRebase {
        bool applied = false;        // a UV-bounded sub-rect was decoded
        int32_t u0 = 0, v0 = 0;      // texel origin of the decoded region
        int32_t w = 0, h = 0;        // decoded region size (the u_tex_size operands)
    };

    struct Transform {
        // shuffle_type values (the .gsdl DrawRec numbering): 0 none, 1 rendered 16 bpp read
        // as 32 (u halves), 2 rendered 32 read as 16 (u doubles), 3 depth read as colour,
        // 4 PSMT8 read of a 32-bit target (channel extract).
        enum Shuffle : uint8_t { kShuffleNone = 0, kShuffle16to32 = 1, kShuffle32to16 = 2,
                                 kShuffleDepthToColor = 3, kShuffleChannel = 4 };
        uint8_t shuffle_type = 0;
        float shuffle_uv_scale_u = 1.0f;   // FST=0: multiplies converted u; FST=1: folded into uv_tex_w
        bool is_shuffle = false;
        bool shuffle_snap = false;         // column-snap the converted u/v (8-texel grid)
        int32_t uv_tex_w = 0;              // u_tex_size operands (texel space the shader divides by)
        int32_t uv_tex_h = 0;
        int32_t uv_pre_w = 0;              // STQ prescale operands for the vertex conversion
        int32_t uv_pre_h = 0;
        bool uv_native = false;            // native-centre requantisation on the decode path
        int32_t z_row_offset = 0;          // fmt 3 page-row offset, in Z32 rows
        uint8_t depth_fmt = 0;             // 0 colour fetch, 1..3 depth views (Owner.fmt_case mirror)
        uint8_t idx_fmt = 0;               // 0 off, 4..6 indexed views, 7 depth-byte palette
        uint32_t idx_self = 0;             // fmt 4: 1 self-read; fmt 6: remap metadata word; fmt 7: Z bit shift
        uint8_t mip_levels = 0;            // u_mip_ctl.x (0 = the non-mip texture() arm)
        bool mip_lcm = false;              // TEX1.LCM
        int16_t mip_k = 0;                 // TEX1.K, s7.4
        uint8_t mip_l = 0;                 // TEX1.L
        uint8_t draw_scale = 1;            // draw-target scale for the serve arms
        RegionRebase region;
    };

    struct Sampler {
        bool mag_linear = false;           // TEX1.MMAG
        uint8_t mmin = 0;                  // TEX1.MMIN 0..5 (mapped by the backend with `levels`)
        uint8_t wms = 0, wmt = 0;          // CLAMP wrap modes (0 REPEAT 1 CLAMP 2 RCLAMP 3 RREPEAT)
        uint16_t minu = 0, maxu = 0, minv = 0, maxv = 0;
        // Wrap and filter overrides are NOT fields: render-target reads always clamp
        // and depth / indexed serves always sample NEAREST -- the backend derives both
        // from Owner.kind / fmt_case, so the record cannot express a non-PS2 draw.
    };

    enum class Declined : uint8_t {
        None = 0,
        NoTexture,       // PRIM.TME off
        StaleTarget,     // target at TBP: no valid content, or a newer EE upload over the read (rescued)
        ClassMismatch,   // 16 <-> 32 bit class mismatch (synced), or a Z class the serve has no view for
        ClutUnservable,  // indexed read of a target the serve chain declined (deposited when recent)
        NoDepthHistory,  // Z-format read with no Z-writing target containing TBP
        SelfRead,        // colour serve of the draw's own FRAME.FBP: nothing is bound
        NoSnapshot,      // the driver carries no VRAM snapshot: textured UV mode, nothing bound
        Other
    };

    Owner owner;
    Transform transform;
    Sampler sampler;
    Declined declined = Declined::None;
    // PRIM.TME with the UV conversion in textured mode (uv_tex_w/h > 0). The
    // sampler binds only when owner.kind != None; `enabled` with owner None is the
    // "textured draw with nothing to sample" case (u_use_texture = 0).
    bool enabled = false;
};

}  // namespace gs
}  // namespace ps2r
