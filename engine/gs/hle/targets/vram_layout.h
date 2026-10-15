// gs/hle/targets/vram_layout.h -- VramLayout: the title's VRAM map as the engine
// consults it.
//
// The engine never compares a frame-buffer address to a literal. Every decision
// that depends on WHICH buffer a draw targets or reads (the native-pinned scene
// accumulator, the FMV blit signature, the native scanout tuple, the reader band
// pages whose deposits must land eagerly, the CLUT-heal exemption window) asks
// this struct, which the title's RendererProfile fills. An empty layout (every
// field zero) answers false to every query, so an engine built without a title
// behaves as a plain GS.
#pragma once

#include <cstdint>
#include <vector>

namespace ps2r {
namespace gs {

struct VramLayout {
    uint32_t scene_accum_fbp = 0;        // native-pinned accumulator (also the scissor-height rule)
    uint32_t main_fb_fbp = 0;            // the display buffer
    uint32_t fmv_stage_tbp = 0;          // FMV blit source (CT32 1024^2 texture read)
    uint32_t stencil_zcopy_fbp = 0;           // colour copy of Z for the stencil recipe
    uint32_t fog_lane_a_fbp = 0;         // 16-bit fog strip lanes
    uint32_t fog_lane_b_fbp = 0;
    uint32_t native_scanout_fbp_bytes = 0;   // the CRTC tuple that may present straight from a target
    uint32_t native_scanout_fbw_px = 0;
    uint8_t native_scanout_psm = 0;
    std::vector<uint16_t> reader_band_pages;  // pages a texture read is known to observe
    uint32_t clut_heal_exempt_lo = 0;    // CLUT bases in [lo, hi] with (cbp & mask) == value are exempt
    uint32_t clut_heal_exempt_hi = 0;
    uint32_t clut_heal_exempt_mask = 0;
    uint32_t clut_heal_exempt_value = 0;

    bool Empty() const;
    bool IsSceneAccum(uint32_t fbp) const;
    bool IsFogLane(uint32_t fbp) const;
    // The D1 FMV blit: FRAME at the display buffer reading the stage as CT32 1024x1024.
    bool IsFmvBlitSignature(uint32_t frame_fbp, uint32_t tbp, uint32_t tpsm, uint32_t tw_log2,
                            uint32_t th_log2) const;
    bool IsNativeScanout(uint32_t fbp, uint32_t fbw_px, uint32_t psm) const;
    bool IsClutHealExempt(uint32_t cbp) const;
    bool IsReaderBandPage(uint32_t page) const;
};

}  // namespace gs
}  // namespace ps2r
