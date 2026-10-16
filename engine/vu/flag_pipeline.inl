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

PS2R_FORCE_INLINE void FlagPipeline::UpdateMACFlags(VUregs& regs, const VR_reg& result, uint8_t mask) {
    if (lazy_) {
        PushMacRaw(result.I, mask);
    } else {
        PushMac(regs, VuFloat::PackMacBits(result.I, mask));
    }
}
