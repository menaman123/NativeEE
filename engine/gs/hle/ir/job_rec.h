// gs/hle/ir/job_rec.h -- a non-draw render job as the GS state machine emits it
// to its sink (and as the `.gsdl` kJob / kFrame record stores it).
//
// JobKind values are the wire encoding of JobRec::type; they are pinned by the
// recorded corpus and never renumbered. The arguments per kind:
//   SetFrontBuffer     DISPFB: arg1 = fbp (bytes), arg2 = fbw (pixels), arg3 = psm
//   SetWindow          DISPLAY: arg1 = circuit (1|2), arg2 = window_w<<16 | content_w,
//                      arg3 = window_h<<16 | content_h, arg4 = (dw+1)<<16 | (dh+1)
//   SetClearColor      BGCOLOR: arg1..3 = r, g, b
//   VSync              arg1 = 1 marks an EE-pushed game-frame boundary
//   InvalidateTextures arg1 = VRAM byte address, arg2 = byte size
//   UploadVRAMTexture  arg1 = VRAM byte address, arg2 = w, arg3 = h, arg4 = VRAM byte
//                      footprint (w*h*bpp/8); payload = w*h*4 RGBA8 linear pixels
//   SyncAllRTsToVRAM   no arguments
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

enum class JobKind : uint32_t {
    DrawBatch = 0,
    SetScissor = 1,
    SetBlendMode = 2,
    SetDepthTest = 3,
    SetFrontBuffer = 4,
    SetWindow = 5,
    SetClearColor = 6,
    VSync = 7,
    InvalidateTextures = 8,
    UploadVRAMTexture = 9,
    SyncAllRTsToVRAM = 10,
};

constexpr uint32_t kJobKindCount = 11;

#pragma pack(push, 1)
struct JobRec {
    uint32_t type;           // JobKind
    uint32_t seq;            // global push ordinal
    uint32_t src_frame;      // vsync frame at push
    uint32_t arg1, arg2, arg3, arg4;
    uint64_t vram_epoch;     // VRAM epoch at push
    uint32_t payload_bytes;  // upload pixel byte count (w*h*4)
    uint32_t payload_incl;   // 1 iff the pixel bytes follow this record on disk
    uint64_t payload_hash;   // GsdlFormat::Fnv1aSampled of the pixels, 0 when none
};
#pragma pack(pop)

static_assert(sizeof(JobRec) == 52, "JobRec is 52 bytes on disk");

}  // namespace gs
}  // namespace ps2r
