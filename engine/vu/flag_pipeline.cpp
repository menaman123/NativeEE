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

uint32_t FlagPipeline::VisibleClip(VUregs& regs) {
    int keep = 0;
    for (int i = 0; i < clip_pending_n_; ++i) {
        if (clip_pending_[i].retire <= vu_cycle_) {
            regs.clip_flag = clip_pending_[i].value;  // later due entries overwrite earlier ones
        } else {
            clip_pending_[keep++] = clip_pending_[i];
        }
    }
    clip_pending_n_ = keep;
    return regs.clip_flag;
}

void FlagPipeline::PushClip(VUregs& regs, uint32_t value) {
    clip_latest_ = value & 0xFFFFFFu;
    if (clip_pending_n_ == kRingDepth) {
        regs.clip_flag = clip_pending_[0].value;  // overflow: the oldest retires now
        std::memmove(&clip_pending_[0], &clip_pending_[1], sizeof(PendingClip) * (kRingDepth - 1));
        clip_pending_n_ = kRingDepth - 1;
    }
    clip_pending_[clip_pending_n_++] = PendingClip{vu_cycle_ + kClipDelay, clip_latest_};
}

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

void FlagPipeline::AccumulateLazyEntry(const PendingMac& e) {
#if PS2R_VU_LAZY_NEON
    const uint32x4_t bits = vld1q_u32(e.bits);
    const uint32x4_t expm = vdupq_n_u32(VuFloat::kExpMask);
    const uint32x4_t exp = vandq_u32(bits, expm);
    const uint32x4_t z_m = vceqq_u32(exp, vdupq_n_u32(0));
    const uint32x4_t o_m = vceqq_u32(exp, expm);
    const uint32x4_t u_m = vandq_u32(z_m, vtstq_u32(bits, vdupq_n_u32(VuFloat::kMantMask)));
    const uint32x4_t s_m = vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(bits), 31));
    const uint32x4_t lm = vld1q_u32(VuFloat::kLaneMask[e.mask & 0xF]);
    vst1q_u32(lazy_acc_[0], vorrq_u32(vld1q_u32(lazy_acc_[0]), vandq_u32(z_m, lm)));
    vst1q_u32(lazy_acc_[1], vorrq_u32(vld1q_u32(lazy_acc_[1]), vandq_u32(s_m, lm)));
    vst1q_u32(lazy_acc_[2], vorrq_u32(vld1q_u32(lazy_acc_[2]), vandq_u32(u_m, lm)));
    vst1q_u32(lazy_acc_[3], vorrq_u32(vld1q_u32(lazy_acc_[3]), vandq_u32(o_m, lm)));
#else
    lazy_or_mac_ |= VuFloat::PackMacBits(e.bits, e.mask);
#endif
    std::memcpy(lazy_last_bits_, e.bits, sizeof(lazy_last_bits_));
    lazy_last_mask_ = e.mask;
    lazy_have_last_ = true;
}

void FlagPipeline::MaterializeMacStatus(VUregs& regs) {
    if (!lazy_have_last_) return;
    bool any_z, any_s, any_u, any_o;
    uint16_t mac;
#if PS2R_VU_LAZY_NEON
    mac = VuFloat::MacFlags4(vld1q_u32(lazy_last_bits_), lazy_last_mask_);
    any_z = vmaxvq_u32(vld1q_u32(lazy_acc_[0])) != 0;
    any_s = vmaxvq_u32(vld1q_u32(lazy_acc_[1])) != 0;
    any_u = vmaxvq_u32(vld1q_u32(lazy_acc_[2])) != 0;
    any_o = vmaxvq_u32(vld1q_u32(lazy_acc_[3])) != 0;
#else
    mac = VuFloat::PackMacBits(lazy_last_bits_, lazy_last_mask_);
    any_z = (lazy_or_mac_ & 0x000Fu) != 0;
    any_s = (lazy_or_mac_ & 0x00F0u) != 0;
    any_u = (lazy_or_mac_ & 0x0F00u) != 0;
    any_o = (lazy_or_mac_ & 0xF000u) != 0;
#endif
    regs.mac_flag = mac;
    uint16_t status = static_cast<uint16_t>(regs.status_flag & 0xFF0u);
    if (mac & 0x000Fu) status |= vu_status::Z;
    if (mac & 0x00F0u) status |= vu_status::S;
    if (mac & 0x0F00u) status |= vu_status::U;
    if (mac & 0xF000u) status |= vu_status::O;
    if (any_z) status |= vu_status::ZS;
    if (any_s) status |= vu_status::SS;
    if (any_u) status |= vu_status::US;
    if (any_o) status |= vu_status::OS;
    regs.status_flag = status;
    std::memset(lazy_acc_, 0, sizeof(lazy_acc_));
    lazy_or_mac_ = 0;
    lazy_have_last_ = false;
}

void FlagPipeline::RetireDue(VUregs& regs) {
    PendingMac kept[kRingDepth];
    int nk = 0;
    for (int i = 0; i < mac_pending_n_; ++i) {
        const PendingMac& e = mac_pending_[(mac_head_ + i) & (kRingDepth - 1)];
        if (e.retire <= vu_cycle_) {
            if (lazy_) {
                AccumulateLazyEntry(e);
            } else {
                ApplyMacToStatus(regs, e.mac);
            }
        } else {
            kept[nk++] = e;
        }
    }
    for (int i = 0; i < nk; ++i) mac_pending_[i] = kept[i];
    mac_head_ = 0;
    mac_pending_n_ = nk;
}

void FlagPipeline::RetireMacStatus(VUregs& regs) {
    RetireDue(regs);
    if (lazy_) MaterializeMacStatus(regs);
}

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

float FlagPipeline::VisibleQ(VUregs& regs) {
    if (q_pending_valid_ && q_pending_retire_ <= vu_cycle_) {
        regs.Q = q_pending_value_;
        VuFloat::CommitQStatus(regs, q_pending_status_);
        q_pending_valid_ = false;
    }
    return VuFloat::ClampFloat(regs.Q);
}

void FlagPipeline::FlushQPipe(VUregs& regs) {
    if (!q_pending_valid_) return;
    if (q_pending_retire_ > vu_cycle_) vu_cycle_ = q_pending_retire_;
    regs.Q = q_pending_value_;
    VuFloat::CommitQStatus(regs, q_pending_status_);
    q_pending_valid_ = false;
}

void FlagPipeline::PushQ(VUregs& regs, float value, int delay, uint16_t status_bits) {
    value = VuFloat::ClampFloat(value);
    FlushQPipe(regs);  // back-to-back FDIV: wait for and commit the in-flight quotient
    q_pending_valid_ = true;
    q_pending_retire_ = vu_cycle_ + static_cast<uint64_t>(delay);
    q_pending_value_ = value;
    q_pending_status_ = status_bits;
}

void FlagPipeline::SetQPending(bool valid, uint64_t retire, float value, uint16_t status) {
    q_pending_valid_ = valid;
    q_pending_retire_ = retire;
    q_pending_value_ = value;
    q_pending_status_ = status;
}

// ---------------------------------------------------------------- P latch

float FlagPipeline::VisibleP(VUregs& regs) {
    if (p_pending_valid_ && p_pending_retire_ <= vu_cycle_) {
        regs.P = p_pending_value_;
        p_pending_valid_ = false;
    }
    return regs.P;
}

void FlagPipeline::ReleasePEarly(VUregs& regs) {
    if (!p_pending_valid_) return;
    if (p_pending_retire_ - 1 > vu_cycle_) vu_cycle_ = p_pending_retire_ - 1;
    regs.P = p_pending_value_;
    p_pending_valid_ = false;
}

void FlagPipeline::PushP(VUregs& regs, float value, int cycles) {
    ReleasePEarly(regs);  // back-to-back EFU: the in-flight P is released one cycle early
    p_pending_valid_ = true;
    p_pending_retire_ = vu_cycle_ + static_cast<uint64_t>(cycles);
    p_pending_value_ = value;
}

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
