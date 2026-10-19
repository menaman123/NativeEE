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
