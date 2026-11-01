// flag_pipeline.inl -- the hot MAC-ring entries of FlagPipeline, inlined for
// the tiers (every FMAC result passes through UpdateMACFlags). Included from
// flag_pipeline.h only.
#include <cstring>

#include "engine/platform/host/platform.h"

#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"

// Eager commit of one retired entry: MAC replaces, STATUS Z/S/U/O (bits 0-3)
// are rewritten from the MAC nibbles, the sticky twins (6-9) OR in; D/I and
// DS/IS (the FDIV unit's bits) are untouched.
PS2R_FORCE_INLINE void FlagPipeline::ApplyMacToStatus(VUregs& regs, uint16_t mac) {
    regs.mac_flag = mac;
    uint16_t status = static_cast<uint16_t>(regs.status_flag & 0xFF0u);
    if (mac & 0x000Fu) status |= vu_status::Z;
    if (mac & 0x00F0u) status |= vu_status::S;
    if (mac & 0x0F00u) status |= vu_status::U;
    if (mac & 0xF000u) status |= vu_status::O;
    if (status & vu_status::Z) status |= vu_status::ZS;
    if (status & vu_status::S) status |= vu_status::SS;
    if (status & vu_status::U) status |= vu_status::US;
    if (status & vu_status::O) status |= vu_status::OS;
    regs.status_flag = status;
}

PS2R_FORCE_INLINE void FlagPipeline::PushMac(VUregs& regs, uint16_t mac) {
    if (mac_pending_n_ == kRingDepth) {
        ApplyMacToStatus(regs, mac_pending_[mac_head_].mac);  // overflow: the oldest commits now
        mac_head_ = (mac_head_ + 1) & (kRingDepth - 1);
        mac_pending_n_ = kRingDepth - 1;
    }
    PendingMac& e = mac_pending_[(mac_head_ + mac_pending_n_) & (kRingDepth - 1)];
    ++mac_pending_n_;
    e.retire = vu_cycle_ + kMacDelay;
    e.mac = mac;
}

inline void FlagPipeline::PushMacRaw(const uint32_t bits[4], uint8_t mask) {
    if (mac_pending_n_ == kRingDepth) LazyRingOverflow();
    PendingMac& e = mac_pending_[(mac_head_ + mac_pending_n_) & (kRingDepth - 1)];
    ++mac_pending_n_;
    e.retire = vu_cycle_ + kMacDelay;
    std::memcpy(e.bits, bits, sizeof(e.bits));
    e.mask = mask;
}

PS2R_FORCE_INLINE void FlagPipeline::PushMacRawAt(const uint32_t bits[4], uint8_t mask, uint64_t issue_cycle) {
    if (mac_pending_n_ == kRingDepth) LazyRingOverflow();
    PendingMac& e = mac_pending_[(mac_head_ + mac_pending_n_) & (kRingDepth - 1)];
    ++mac_pending_n_;
    e.retire = issue_cycle + kMacDelay;
    std::memcpy(e.bits, bits, sizeof(e.bits));
    e.mask = mask;
}

PS2R_FORCE_INLINE void FlagPipeline::PushMacAt(VUregs& regs, uint16_t mac, uint64_t issue_cycle) {
    if (mac_pending_n_ == kRingDepth) {
        ApplyMacToStatus(regs, mac_pending_[mac_head_].mac);
        mac_head_ = (mac_head_ + 1) & (kRingDepth - 1);
        mac_pending_n_ = kRingDepth - 1;
    }
    PendingMac& e = mac_pending_[(mac_head_ + mac_pending_n_) & (kRingDepth - 1)];
    ++mac_pending_n_;
    e.retire = issue_cycle + kMacDelay;
    e.mac = mac;
}

// The lazy fold of one retired result: the Z/S/U/O lane masks OR into the
// sticky accumulators, the raw lanes become the "last retired" (the MAC word
// a reader materialises).
PS2R_FORCE_INLINE void FlagPipeline::FoldLazyRetired(const uint32_t bits_in[4], uint8_t mask) {
#if PS2R_VU_NEON
    const uint32x4_t bits = vld1q_u32(bits_in);
    const uint32x4_t expm = vdupq_n_u32(VuFloat::kExpMask);
    const uint32x4_t exp = vandq_u32(bits, expm);
    const uint32x4_t z_m = vceqq_u32(exp, vdupq_n_u32(0));
    const uint32x4_t o_m = vceqq_u32(exp, expm);
    const uint32x4_t u_m = vandq_u32(z_m, vtstq_u32(bits, vdupq_n_u32(VuFloat::kMantMask)));
    const uint32x4_t s_m = vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(bits), 31));
    const uint32x4_t lm = vld1q_u32(VuFloat::kLaneMask[mask & 0xF]);
    vst1q_u32(lazy_acc_[0], vorrq_u32(vld1q_u32(lazy_acc_[0]), vandq_u32(z_m, lm)));
    vst1q_u32(lazy_acc_[1], vorrq_u32(vld1q_u32(lazy_acc_[1]), vandq_u32(s_m, lm)));
    vst1q_u32(lazy_acc_[2], vorrq_u32(vld1q_u32(lazy_acc_[2]), vandq_u32(u_m, lm)));
    vst1q_u32(lazy_acc_[3], vorrq_u32(vld1q_u32(lazy_acc_[3]), vandq_u32(o_m, lm)));
#else
    lazy_or_mac_ |= VuFloat::PackMacBits(bits_in, mask);
#endif
    std::memcpy(lazy_last_bits_, bits_in, sizeof(lazy_last_bits_));
    lazy_last_mask_ = mask;
    lazy_have_last_ = true;
}

// The caller-owned fold (FoldLazyRetired's sticky step on a register-resident
// accumulator; the lane mask is a compile-time constant, so a full mask costs
// nothing and a partial one is a vector constant, not a table load).
PS2R_FORCE_INLINE void FlagPipeline::LazyAcc::Reset() {
#if PS2R_VU_NEON
    s_or = vdupq_n_u32(0);
    abs_max = vdupq_n_u32(0);
    abs_min = vdupq_n_u32(~0u);
    absm1_min = vdupq_n_u32(~0u);
#else
    or_mac = 0;
#endif
}

template <int M>
PS2R_FORCE_INLINE void FlagPipeline::LazyAcc::Fold(const uint32_t bits_in[4]) {
    static_assert(M >= 0 && M <= 15, "a dest mask");
#if PS2R_VU_NEON
    uint32x4_t bits = vld1q_u32(bits_in);
    uint32x4_t abs = vbicq_u32(bits, vdupq_n_u32(VuFloat::kSignBit));
    uint32x4_t abs_n = abs;  // the MIN view: unselected lanes neutral (all ones)
    if constexpr (M != 15) {
        const uint32x4_t lm = {(M & 8) ? ~0u : 0u, (M & 4) ? ~0u : 0u, (M & 2) ? ~0u : 0u, (M & 1) ? ~0u : 0u};
        bits = vandq_u32(bits, lm);
        abs = vandq_u32(abs, lm);
        abs_n = vornq_u32(abs_n, lm);
    }
    s_or = vorrq_u32(s_or, bits);
    abs_max = vmaxq_u32(abs_max, abs);
    abs_min = vminq_u32(abs_min, abs_n);
    absm1_min = vminq_u32(absm1_min, vsubq_u32(abs_n, vdupq_n_u32(1)));
#else
    or_mac = static_cast<uint16_t>(or_mac | VuFloat::PackMacBits(bits_in, static_cast<uint8_t>(M)));
#endif
}

PS2R_FORCE_INLINE void FlagPipeline::FoldLazyBatch(LazyAcc& acc, const uint32_t last_bits[4], uint8_t last_mask) {
#if PS2R_VU_NEON
    // The accumulators -> the ring's lane sets (FoldLazyRetired's z_m / s_m / u_m / o_m over the batch).
    const uint32x4_t z_m = vcltq_u32(acc.abs_min, vdupq_n_u32(0x00800000u));
    const uint32x4_t s_m = vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(acc.s_or), 31));
    const uint32x4_t u_m = vcltq_u32(acc.absm1_min, vdupq_n_u32(0x007FFFFFu));
    const uint32x4_t o_m = vcgeq_u32(acc.abs_max, vdupq_n_u32(VuFloat::kExpMask));
    vst1q_u32(lazy_acc_[0], vorrq_u32(vld1q_u32(lazy_acc_[0]), z_m));
    vst1q_u32(lazy_acc_[1], vorrq_u32(vld1q_u32(lazy_acc_[1]), s_m));
    vst1q_u32(lazy_acc_[2], vorrq_u32(vld1q_u32(lazy_acc_[2]), u_m));
    vst1q_u32(lazy_acc_[3], vorrq_u32(vld1q_u32(lazy_acc_[3]), o_m));
#else
    lazy_or_mac_ = static_cast<uint16_t>(lazy_or_mac_ | acc.or_mac);
#endif
    std::memcpy(lazy_last_bits_, last_bits, sizeof(lazy_last_bits_));
    lazy_last_mask_ = last_mask;
    lazy_have_last_ = true;
    acc.Reset();
}

// The ring is in retire order (pushes carry the monotonic clock + the delay):
// the due entries are the prefix from the head, folded oldest first; the head
// advances past them and nothing is copied. Inline: the generated blocks call
// it at every edge (the common case folds 0-3 entries).
PS2R_FORCE_INLINE void FlagPipeline::RetireDue(VUregs& regs) {
    while (mac_pending_n_ > 0) {
        const PendingMac& e = mac_pending_[mac_head_];
        if (e.retire > vu_cycle_) break;
        if (lazy_) {
            FoldLazyRetired(e.bits, e.mask);
        } else {
            ApplyMacToStatus(regs, e.mac);
        }
        mac_head_ = (mac_head_ + 1) & (kRingDepth - 1);
        --mac_pending_n_;
    }
}

PS2R_FORCE_INLINE void FlagPipeline::UpdateMACFlags(VUregs& regs, const VR_reg& result, uint8_t mask) {
    if (lazy_) {
        PushMacRaw(result.I, mask);
    } else {
        PushMac(regs, VuFloat::PackMacBits(result.I, mask));
    }
}

// ---- the readers / pushers the generated blocks call per op (inline so a
// generated TU does not go through a dyld stub for each: RetireMacStatus,
// VisibleQ, VisibleClip, PushQ, PushClip, FlushQPipe were 9-11 % of the
// worker's busy time as out-of-line calls). Bodies unchanged.

// The entries retire in issue order (a push's retire is the monotonic clock
// + the delay), so the due ones are a PREFIX: the last due value is the
// visible flag and the rest shift down once.
PS2R_FORCE_INLINE uint32_t FlagPipeline::VisibleClip(VUregs& regs) {
    int due = 0;
    while (due < clip_pending_n_ && clip_pending_[due].retire <= vu_cycle_) ++due;
    if (due > 0) {
        regs.clip_flag = clip_pending_[due - 1].value;
        clip_pending_n_ -= due;
        // The shift as a bounded loop (<= kRingDepth entries): a variable-length
        // memmove is an out-of-line call, and this runs per FCAND of a vertex loop.
        for (int i = 0; i < clip_pending_n_; ++i) clip_pending_[i] = clip_pending_[i + due];
    }
    return regs.clip_flag;
}

PS2R_FORCE_INLINE void FlagPipeline::PushClip(VUregs& regs, uint32_t value) {
    clip_latest_ = value & 0xFFFFFFu;
    if (clip_pending_n_ == kRingDepth) {
        regs.clip_flag = clip_pending_[0].value;  // overflow: the oldest retires now
        std::memmove(&clip_pending_[0], &clip_pending_[1], sizeof(PendingClip) * (kRingDepth - 1));
        clip_pending_n_ = kRingDepth - 1;
    }
    clip_pending_[clip_pending_n_++] = PendingClip{vu_cycle_ + kClipDelay, clip_latest_};
}

PS2R_FORCE_INLINE void FlagPipeline::MaterializeMacStatus(VUregs& regs) {
    if (!lazy_have_last_) return;
    bool any_z, any_s, any_u, any_o;
    uint16_t mac;
#if PS2R_VU_NEON
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

// THE reader entry (FS* / FM*): retire, then materialise the lazy fold (a
// no-op when nothing retired since the last materialisation).
PS2R_FORCE_INLINE void FlagPipeline::RetireMacStatus(VUregs& regs) {
    RetireDue(regs);
    if (lazy_ && lazy_have_last_) MaterializeMacStatus(regs);
}

PS2R_FORCE_INLINE float FlagPipeline::VisibleQ(VUregs& regs) {
    if (q_pending_valid_ && q_pending_retire_ <= vu_cycle_) {
        regs.Q = q_pending_value_;
        VuFloat::CommitQStatus(regs, q_pending_status_);
        q_pending_valid_ = false;
    }
    return VuFloat::ClampFloat(regs.Q);
}

PS2R_FORCE_INLINE float FlagPipeline::VisibleP(VUregs& regs) {
    if (p_pending_valid_ && p_pending_retire_ <= vu_cycle_) {
        regs.P = p_pending_value_;
        p_pending_valid_ = false;
    }
    return regs.P;
}

PS2R_FORCE_INLINE void FlagPipeline::ReleasePEarly(VUregs& regs) {
    if (!p_pending_valid_) return;
    if (p_pending_retire_ - 1 > vu_cycle_) vu_cycle_ = p_pending_retire_ - 1;
    regs.P = p_pending_value_;
    p_pending_valid_ = false;
}

PS2R_FORCE_INLINE void FlagPipeline::PushP(VUregs& regs, float value, int cycles) {
    ReleasePEarly(regs);  // back-to-back EFU: the in-flight P is released one cycle early
    p_pending_valid_ = true;
    p_pending_retire_ = vu_cycle_ + static_cast<uint64_t>(cycles);
    p_pending_value_ = value;
}

PS2R_FORCE_INLINE void FlagPipeline::FlushQPipe(VUregs& regs) {
    if (!q_pending_valid_) return;
    if (q_pending_retire_ > vu_cycle_) vu_cycle_ = q_pending_retire_;
    regs.Q = q_pending_value_;
    VuFloat::CommitQStatus(regs, q_pending_status_);
    q_pending_valid_ = false;
}

PS2R_FORCE_INLINE void FlagPipeline::PushQ(VUregs& regs, float value, int delay, uint16_t status_bits) {
    value = VuFloat::ClampFloat(value);
    FlushQPipe(regs);  // back-to-back FDIV: wait for and commit the in-flight quotient
    q_pending_valid_ = true;
    q_pending_retire_ = vu_cycle_ + static_cast<uint64_t>(delay);
    q_pending_value_ = value;
    q_pending_status_ = status_bits;
}
