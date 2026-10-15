// gs/state/gs_state.h -- GsState: the façade the GIF side and the integration
// feed. It owns the register file, the vertex kick, the flush, the transfer
// engine, the privileged registers and the VRAM mirror, and routes every write
// to its owner:
//   WriteRegister(addr, v)  the 64-bit A+D / REGLIST register write (GS User's
//                           Manual 7.3.2, 7.3.3): kicks -> VertexKick, transfer
//                           registers -> TransferEngine, SIGNAL/FINISH/LABEL ->
//                           GsPrivRegs, 0x0B/0x0E/0x0F no-ops, everything else ->
//                           GsRegisters.
//   WritePacked(reg, lo, hi) the PACKED-layout items whose decode differs from
//                           the register form (7.3.1: PRIM RGBAQ ST UV XYZF2 XYZ2
//                           FOG XYZF3 XYZ3); other descriptors are register writes
//                           of `lo`.
//   WriteImage(bytes)       IMAGE-mode data (7.3.4) -> the transfer engine.
//   KickBoundary()          end of a GIF packet with EOP: the GifEop flush.
//   GifTagRead()            a GIFtag was read: Q = 1.0 (7.2).
//   WritePrivileged / ReadPrivileged   0x12000000.. registers.
//   SetSink / SetIdentity   the DrawIr consumer and the producer identity stamp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gs/hle/ir/draw_ir_sink.h"
#include "engine/gs/state/draw_flush.h"
#include "engine/gs/state/gs_priv_regs.h"
#include "engine/gs/state/gs_registers.h"
#include "engine/gs/state/vertex_kick.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/gs/xfer/transfer_engine.h"

namespace ps2r {
namespace gs {

class GsState {
public:
    GsState();

    void SetSink(DrawIrSink* sink);
    // A complete library draw joins retained producers through their common
    // sequence/VRAM ownership. Caller has already flushed preceding assembly.
    // This does not consume commands or mutate register/primitive state.
    void SubmitDrawIr(DrawIr draw);
    void SetQuirks(const VertexKickQuirks& q) { kick_.SetQuirks(q); }
    void SetIdentity(uint32_t hle_handler, uint64_t image_hash, uint16_t kick_pc, uint16_t kick_ord,
                     uint32_t frame_inst);
    void ClearIdentity() { SetIdentity(0, 0, 0, 0, 0); }
    void SetSrcFrame(uint32_t frame) { src_frame_ = frame; }

    void WriteRegister(uint8_t addr, uint64_t value);
    void WritePacked(uint8_t reg, uint64_t lo, uint64_t hi);
    void WriteImage(const uint8_t* data, size_t bytes);
    void KickBoundary();
    void GifTagRead() { regs_.attr.q = 1.0f; }
    // Pre-decoded kick for producers that bypass the register encodings.
    void KickVertex(float x, float y, float z, uint8_t f, bool draw) { kick_.Kick(x, y, z, f, draw); }

    void WritePrivileged(uint32_t addr, uint64_t value) { priv_.Write(addr, value); }
    uint64_t ReadPrivileged(uint32_t addr) const { return priv_.Read(addr); }
    // A savestate restore of local memory: the whole 4 MiB image lands as EE
    // (CPU) truth, settles at once, and the consumer is told every page changed
    // (one InvalidateTextures job over the full range) -- the targets and the
    // decoded textures refresh from the restored bytes, as after a power-on
    // upload. `len` is clamped to VramImage::kBytes.
    void RestoreLocalMemory(const uint8_t* bytes, size_t len);

    GsRegisters& Regs() { return regs_; }
    const GsRegisters& Regs() const { return regs_; }
    VertexKick& Kick() { return kick_; }
    DrawFlush& Flush() { return flush_; }
    TransferEngine& Xfer() { return xfer_; }
    GsPrivRegs& Priv() { return priv_; }
    VramImage& Vram() { return vram_; }
    uint32_t Seq() const { return seq_; }

private:
    void FlushForStateChange();

    VramImage vram_;
    GsRegisters regs_;
    VertexKick kick_;
    DrawFlush flush_;
    TransferEngine xfer_;
    GsPrivRegs priv_;
    uint32_t seq_ = 0;
    uint32_t src_frame_ = 0;
    uint64_t vram_epoch_ = 0;
    DrawIrSink* sink_ = nullptr;
};

}  // namespace gs
}  // namespace ps2r
