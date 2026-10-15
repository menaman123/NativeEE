// gs/state/gs_state.cpp -- GsState façade (see gs_state.h).
#include "engine/gs/state/gs_state.h"

#include <utility>

namespace ps2r {
namespace gs {

GsState::GsState() : kick_(&regs_), flush_(&regs_, &kick_), xfer_(&vram_) {
    regs_.has_pending = [this]() { return kick_.HasPending(); };
    regs_.flush_pending = [this]() { FlushForStateChange(); };
    kick_.SetFlushHook([this]() { FlushForStateChange(); });
    xfer_.SetFlushHook([this]() { FlushForStateChange(); });
    // The flush is an observation boundary: EE writes settle, deposits commit,
    // and the draw carries the epoch that results.
    flush_.SetBeforeFlushHook([this]() {
        vram_.SettleWrites();
        vram_epoch_ = vram_.UploadEpoch();
    });
    flush_.SetJobContext(&seq_, &src_frame_, &vram_epoch_);
    xfer_.SetJobContext(&seq_, &src_frame_);
    // Privileged-register jobs (DISPFB/DISPLAY/BGCOLOR) carry the LIVE upload
    // epoch: a transfer stamp between the last draw flush and the write counts.
    priv_.SetJobContext(&seq_, &src_frame_, [this]() { return vram_.UploadEpoch(); });
}

void GsState::SetSink(DrawIrSink* sink) {
    sink_ = sink;
    flush_.SetSink(sink);
    xfer_.SetSink(sink);
    priv_.SetSink(sink);
}

void GsState::SubmitDrawIr(DrawIr draw) {
    vram_.SettleWrites();
    vram_epoch_ = vram_.UploadEpoch();
    draw.seq = seq_++;
    draw.src_frame = src_frame_;
    draw.vram_epoch = vram_epoch_;
    if (sink_) sink_->OnDrawOwned(std::move(draw));
}

void GsState::RestoreLocalMemory(const uint8_t* bytes, size_t len) {
    if (!bytes || len == 0) return;
    if (len > VramImage::kBytes) len = VramImage::kBytes;
    vram_.WriteStream(0, bytes, len);
    vram_.SettleWrites();
    vram_epoch_ = vram_.UploadEpoch();
    if (!sink_) return;
    JobRec j{};
    j.type = static_cast<uint32_t>(JobKind::InvalidateTextures);
    j.seq = seq_++;
    j.src_frame = src_frame_;
    j.arg1 = 0;
    j.arg2 = static_cast<uint32_t>(len);
    j.vram_epoch = vram_epoch_;
    sink_->OnJob(j);
}

void GsState::SetIdentity(uint32_t hle_handler, uint64_t image_hash, uint16_t kick_pc, uint16_t kick_ord,
                          uint32_t frame_inst) {
    DrawIr::Identity id;
    id.hle_handler = hle_handler;
    id.image_hash = image_hash;
    id.kick_pc = kick_pc;
    id.kick_ord = kick_ord;
    id.frame_inst = frame_inst;
    flush_.SetIdentity(id);
}

void GsState::FlushForStateChange() {
    flush_.Flush(DrawFlush::FlushKind::StateChange);
}

void GsState::KickBoundary() {
    flush_.Flush(DrawFlush::FlushKind::GifEop);
}

void GsState::WriteRegister(uint8_t addr, uint64_t v) {
    switch (addr) {
        case 0x00:
            regs_.Write(0x00, v & 0x7FF);
            kick_.OnPrimWritten();
            return;
        case 0x04: kick_.WriteXyzf(true, v); return;
        case 0x05: kick_.WriteXyz(true, v); return;
        case 0x0C: kick_.WriteXyzf(false, v); return;
        case 0x0D: kick_.WriteXyz(false, v); return;
        case 0x0B: case 0x0E: case 0x0F: return;  // reserved / A+D-as-data / NOP
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: xfer_.WriteRegister(addr, v); return;
        case 0x60: priv_.Signal(v); return;
        case 0x61: priv_.Finish(); return;
        case 0x62: priv_.Label(v); return;
        default:
            regs_.Write(addr, v);  // unknown addresses are ignored
            return;
    }
}

void GsState::WritePacked(uint8_t reg, uint64_t lo, uint64_t hi) {
    switch (reg) {
        case 0x00: WriteRegister(0x00, lo); return;
        case 0x01: regs_.PackedRgbaq(lo, hi); return;
        case 0x02: regs_.PackedSt(lo, hi); return;
        case 0x03: regs_.PackedUv(lo); return;
        case 0x04: kick_.PackedXyzf(true, lo, hi); return;
        case 0x05: kick_.PackedXyz(true, lo, hi); return;
        case 0x0A: regs_.PackedFog(hi); return;
        case 0x0C: kick_.PackedXyzf(false, lo, hi); return;
        case 0x0D: kick_.PackedXyz(false, lo, hi); return;
        case 0x0E: WriteRegister(static_cast<uint8_t>(hi & 0xFF), lo); return;  // A+D
        default: WriteRegister(reg, lo); return;
    }
}

void GsState::WriteImage(const uint8_t* data, size_t bytes) {
    xfer_.WriteImage(data, bytes);
}

}  // namespace gs
}  // namespace ps2r
