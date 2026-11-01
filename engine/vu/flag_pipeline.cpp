// FlagPipeline -- the CLIP / MAC-STATUS rings and the Q / P latches. Rules in
// the header.
#include "engine/vu/flag_pipeline.h"

#include <cstring>

#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"

#if defined(PS2R_SIMD) && PS2R_SIMD && defined(__aarch64__)
#define PS2R_VU_LAZY_NEON 1
#else
#define PS2R_VU_LAZY_NEON 0
#endif

FlagPipeline::FlagPipeline(bool lazy) : lazy_(lazy) {}

// ---------------------------------------------------------------- CLIP ring
// (VisibleClip / PushClip: flag_pipeline.inl -- the generated blocks call them.)

void FlagPipeline::FlushClipPipe(VUregs& regs) {
    if (clip_pending_n_ > 0) {
        regs.clip_flag = clip_pending_[clip_pending_n_ - 1].value;
        clip_pending_n_ = 0;
    }
    regs.clip_flag &= 0xFFFFFFu;
    clip_latest_ = regs.clip_flag;
}

void FlagPipeline::SetClipImmediate(VUregs& regs, uint32_t imm24) {
    regs.clip_flag = imm24 & 0xFFFFFFu;
    clip_latest_ = regs.clip_flag;
    clip_pending_n_ = 0;
}

// ------------------------------------------------------ MAC / STATUS ring

void FlagPipeline::AccumulateLazyEntry(const PendingMac& e) { FoldLazyRetired(e.bits, e.mask); }

// (MaterializeMacStatus: flag_pipeline.inl -- a STATUS / MAC reader's
// per-op entry in the generated blocks; out of line it made the caller spill
// its register file around every FSAND of the vertex loop.)

void FlagPipeline::LazyRingOverflow() {
    AccumulateLazyEntry(mac_pending_[mac_head_]);
    mac_head_ = (mac_head_ + 1) & (kRingDepth - 1);
    mac_pending_n_ = kRingDepth - 1;
}

void FlagPipeline::FlushMacPipe(VUregs& regs) {
    for (int i = 0; i < mac_pending_n_; ++i) {
        const PendingMac& e = mac_pending_[(mac_head_ + i) & (kRingDepth - 1)];
        if (lazy_) {
            AccumulateLazyEntry(e);
        } else {
            ApplyMacToStatus(regs, e.mac);
        }
    }
    mac_pending_n_ = 0;
    mac_head_ = 0;
    if (lazy_) MaterializeMacStatus(regs);
}

uint16_t FlagPipeline::PackPendingMac(int i) const {
    const PendingMac& e = mac_pending_[(mac_head_ + i) & (kRingDepth - 1)];
    return lazy_ ? VuFloat::PackMacBits(e.bits, e.mask) : e.mac;
}

uint64_t FlagPipeline::mac_retire(int i) const {
    return mac_pending_[(mac_head_ + i) & (kRingDepth - 1)].retire;
}

// ---------------------------------------------------------------- Q latch

void FlagPipeline::SetQPending(bool valid, uint64_t retire, float value, uint16_t status) {
    q_pending_valid_ = valid;
    q_pending_retire_ = retire;
    q_pending_value_ = value;
    q_pending_status_ = status;
}

// ---------------------------------------------------------------- P latch
// (VisibleP / ReleasePEarly / PushP: flag_pipeline.inl -- the generated blocks
// call them per EFU op; as out-of-line calls they made the caller spill its
// whole register file around every WAITP / MFP of the vertex loop.)

void FlagPipeline::FlushPPipe(VUregs& regs) {
    if (!p_pending_valid_) return;
    if (p_pending_retire_ > vu_cycle_) vu_cycle_ = p_pending_retire_;
    regs.P = p_pending_value_;
    p_pending_valid_ = false;
}

void FlagPipeline::SetPPending(bool valid, uint64_t retire, float value) {
    p_pending_valid_ = valid;
    p_pending_retire_ = retire;
    p_pending_value_ = value;
}
