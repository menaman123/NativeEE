// gs/savestate/gs_sections.h -- GsSections: the GS-owned savestate sections over
// GsState -- VRAM (the 4 MiB local memory image), GSPR (the privileged register
// file + the video-mode triple) and GSST (the decoded drawing-environment
// register file). All three REQUIRED: the reference loader reads them
// unconditionally and every frozen anchor carries them.
//
// Wire layout (little-endian, the frozen reference format):
//   VRAM  4,194,304 bytes -- local memory as the GS held it at the save (the
//         save ran every live render target's readback into it first, so the
//         image IS the frame the CRTC was scanning out).
//   GSPR  19 x u64 in register order PMODE SMODE1 SMODE2 SRFSH SYNCH1 SYNCH2
//         SYNCV DISPFB1 DISPLAY1 DISPFB2 DISPLAY2 EXTBUF EXTDATA EXTWRITE BGCOLOR
//         CSR IMR BUSDIR SIGLBLID, then i32 display_mode, u8 interlaced,
//         u8 frame_mode (the sceGsSetCrt triple) = 158 bytes.
//   GSST  version 1-3: the 358-byte body (PRIM + the previous primitive's latch,
//         both contexts' XYOFFSET SCISSOR FRAME ZBUF TEX0 TEX1 TEST ALPHA CLAMP,
//         TEXA COLCLAMP PRMODECONT PRMODE FBA PABE SCANMSK FOGCOL, the transfer
//         block (idle at a boundary), the attribute latches RGBAQ/ST/UV/FOG) +
//         at v2 u32 last_rgbaq_pc and two u32-counted 34-byte vertex-record
//         arrays (empty at a boundary save) + at v3 per context u16 TEX1.K and
//         u64 MIPTBP1/2 (constants below).
//
// Restore rule (what the hardware shows): the console's next vblank after a
// restore scans out the buffer DISPFB names out of the restored local memory --
// the frame the title last finished -- while the title renders its next frame
// into the other buffer. So the image lands as EE truth with one full-range
// invalidate (GsState::RestoreLocalMemory) and the privileged registers are
// written through GsPrivRegs::Write, which emits the SetFrontBuffer / SetWindow
// / SetClearColor jobs the presenter needs to compose that scanout; CSR lands
// verbatim. GSST lands as the drawing environment the GS held at the save; the
// title's frame start rewrites it, so it changes no pixel of the anchors' frames
// (DP-26 INT-P813A-GSST-NO-PIXEL-CHANGE) -- it is the console's state all the same.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

namespace ps2r {
namespace gs {

class GsState;
class VramImage;

class GsSections {
public:
    static constexpr size_t kVramBytes = 4u * 1024u * 1024u;
    static constexpr size_t kPrivRegCount = 19;
    static constexpr size_t kGsprBytes = kPrivRegCount * 8 + 4 + 1 + 1;
    // GSST: the decoded drawing-environment register file in the reference's
    // field order (section version 3 = v1 body + the latched-vertex extension +
    // the TEX1.K / MIPTBP tail). The body is 358 bytes; a v2/v3 payload adds
    // u32 last_rgbaq_pc, two u32-counted arrays of 34-byte vertex records (empty
    // at a boundary save) and, at v3, per context u16 K + two u64 MIPTBP.
    static constexpr uint32_t kGsstVersion = 3;
    static constexpr size_t kGsstBodyBytes = 358;
    static constexpr size_t kGsstVertexRecBytes = 34;
    static constexpr uint32_t kGsstVertexArrayMax = 1u << 20;

    explicit GsSections(GsState& gs);

    // The image the VRAM section is READ from at a save: the GS state's own
    // unless the host names another -- the pipelined GS queue's split image
    // keeps the committed GPU deposits on the consumer image; the hook drains
    // that queue and returns it. The restore always lands on the GS state's.
    void SetVramSource(std::function<const VramImage*()> source) { vram_source_ = std::move(source); }

    void Register(savestate::SectionRegistry& registry);
    savestate::SectionSpec VramSpec();
    savestate::SectionSpec GsprSpec();
    savestate::SectionSpec GsstSpec();

    void SerializeVram(std::vector<uint8_t>& out) const;
    void SerializeGspr(std::vector<uint8_t>& out) const;
    void SerializeGsst(std::vector<uint8_t>& out) const;
    static bool ValidateVram(const std::vector<uint8_t>& payload, std::string* err);
    static bool ValidateGspr(const std::vector<uint8_t>& payload, std::string* err);
    static bool ValidateGsst(const std::vector<uint8_t>& payload, uint32_t version, std::string* err);
    bool RestoreVram(const std::vector<uint8_t>& payload, std::string* err);
    bool RestoreGspr(const std::vector<uint8_t>& payload, std::string* err);
    bool RestoreGsst(const std::vector<uint8_t>& payload, uint32_t version, std::string* err);

    unsigned RestoredVramCount() const { return restored_vram_; }
    unsigned RestoredGsprCount() const { return restored_gspr_; }
    unsigned RestoredGsstCount() const { return restored_gsst_; }
    int32_t DisplayMode() const { return display_mode_; }
    bool Interlaced() const { return interlaced_; }
    bool FrameMode() const { return frame_mode_; }

    // Register address (GsPrivRegs::Addr) of slot i in the wire order.
    static uint32_t SlotAddr(size_t i);

private:
    GsState& gs_;
    std::function<const VramImage*()> vram_source_;
    // The sceGsSetCrt triple the reference kept beside the registers (NTSC
    // interlaced field mode when no image was restored).
    int32_t display_mode_ = 2;
    bool interlaced_ = true;
    bool frame_mode_ = false;
    unsigned restored_vram_ = 0;
    unsigned restored_gspr_ = 0;
    unsigned restored_gsst_ = 0;
};

}  // namespace gs
}  // namespace ps2r
