// gs/hle/targets/vram_layout.cpp -- VramLayout queries. Every query is false on
// an empty layout: a zero field never matches because the compared address is
// required to be non-zero where zero is a legal buffer address (the frame at 0x0
// is a real target, so the FMV/scanout queries key on the NON-zero half of their
// tuple).
#include "engine/gs/hle/targets/vram_layout.h"

namespace ps2r {
namespace gs {

bool VramLayout::Empty() const {
    return scene_accum_fbp == 0 && main_fb_fbp == 0 && fmv_stage_tbp == 0 && stencil_zcopy_fbp == 0 &&
           fog_lane_a_fbp == 0 && fog_lane_b_fbp == 0 && native_scanout_fbp_bytes == 0 &&
           native_scanout_fbw_px == 0 && reader_band_pages.empty() && clut_heal_exempt_hi == 0;
}

bool VramLayout::IsSceneAccum(uint32_t fbp) const {
    return scene_accum_fbp != 0 && fbp == scene_accum_fbp;
}

bool VramLayout::IsFogLane(uint32_t fbp) const {
    return (fog_lane_a_fbp != 0 && fbp == fog_lane_a_fbp) ||
           (fog_lane_b_fbp != 0 && fbp == fog_lane_b_fbp);
}

bool VramLayout::IsFmvBlitSignature(uint32_t frame_fbp, uint32_t tbp, uint32_t tpsm,
                                    uint32_t tw_log2, uint32_t th_log2) const {
    // The stage address is the non-zero key; the frame is the display buffer,
    // the read is PSMCT32 at 1024 x 1024 (TEX0.TW = TEX0.TH = 10).
    return fmv_stage_tbp != 0 && tbp == fmv_stage_tbp && frame_fbp == main_fb_fbp && tpsm == 0 &&
           tw_log2 == 10 && th_log2 == 10;
}

bool VramLayout::IsNativeScanout(uint32_t fbp, uint32_t fbw_px, uint32_t psm) const {
    return native_scanout_fbw_px != 0 && fbp == native_scanout_fbp_bytes &&
           fbw_px == native_scanout_fbw_px && psm == native_scanout_psm;
}

bool VramLayout::IsClutHealExempt(uint32_t cbp) const {
    return clut_heal_exempt_hi != 0 && cbp >= clut_heal_exempt_lo && cbp <= clut_heal_exempt_hi &&
           (cbp & clut_heal_exempt_mask) == clut_heal_exempt_value;
}

bool VramLayout::IsReaderBandPage(uint32_t page) const {
    for (uint16_t p : reader_band_pages)
        if (p == page) return true;
    return false;
}

}  // namespace gs
}  // namespace ps2r
