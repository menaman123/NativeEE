#include "engine/gif/gif_regs.h"

GifRegs::GifRegs() { Reset(); }

void GifRegs::Reset() {
    ctrl_ = 0;
    mode_ = 0;
    fifo_count_ = 0;
    path3_masked_.store(false, std::memory_order_relaxed);
    transfer_ = GifTransferState{};
}

void GifRegs::RestoreRegisters(uint32_t ctrl, uint32_t mode, bool path3_masked_by_vif) {
    ctrl_ = ctrl;
    mode_ = mode;
    fifo_count_ = 0;
    path3_masked_.store(path3_masked_by_vif, std::memory_order_relaxed);
    transfer_ = GifTransferState{};
}

// GIF_STAT is a live predicate of the unit: M3R (bit 0) = MODE.M3R, M3P (bit 1)
// = the VIF mask, IMT (bit 2) = MODE.IMT, PSE (bit 3) = CTRL.PSE, OPH (bit 9)
// and APATH (bits 10-11, always 3 = PATH3) while a tag is in transfer, FQC
// (bits 24-28) = FIFO quadwords. P3Q/P2Q/P1Q (bits 6-8) are never set.
uint32_t GifRegs::ComposeStat() const {
    uint32_t status = 0;
    if (mode_ & 1u) status |= 1u << 0;
    if (path3_masked_.load(std::memory_order_relaxed)) status |= 1u << 1;
    if (mode_ & 4u) status |= 1u << 2;
    if (ctrl_ & 8u) status |= 1u << 3;
    if (transfer_.in_transfer) status |= 1u << 9;
    if (transfer_.in_transfer) status |= 3u << 10;
    status |= ((fifo_count_ / 16u) & 0x1Fu) << 24;
    return status;
}

uint64_t GifRegs::Read(uint32_t addr, unsigned /*size*/) {
    const GifTag& tag = transfer_.tag;
    switch (addr - kBase) {
        case kCtrl:
            return ctrl_;
        case kMode:
            return mode_;
        case kStat:
            return ComposeStat();
        // TAG0: NLOOP bits 0-14, EOP bit 15.
        case kTag0:
            return static_cast<uint32_t>(tag.nloop) | (static_cast<uint32_t>(tag.eop ? 1u : 0u) << 15);
        // TAG1: PRE bit 14, PRIM bits 15-25, FLG bits 26-27, NREG bits 28-31
        // (the normalised 16 shifts out and reads 0, the hardware encoding).
        case kTag1:
            return (static_cast<uint32_t>(tag.pre ? 1u : 0u) << 14) | (static_cast<uint32_t>(tag.prim) << 15) |
                   (static_cast<uint32_t>(tag.flg) << 26) | (static_cast<uint32_t>(tag.nregs) << 28);
        // TAG2/TAG3: descriptors 0-7 / 8-15 as nibbles.
        case kTag2: {
            uint32_t v = 0;
            for (int i = 0; i < 8; ++i) v |= static_cast<uint32_t>(tag.regs[i] & 0xFu) << (i * 4);
            return v;
        }
        case kTag3: {
            uint32_t v = 0;
            for (int i = 0; i < 8; ++i) v |= static_cast<uint32_t>(tag.regs[8 + i] & 0xFu) << (i * 4);
            return v;
        }
        // CNT: loops remaining in bits 0-14, current descriptor index in bits 16-19.
        case kCnt:
            return (static_cast<uint32_t>(tag.nloop) - transfer_.loop) | (transfer_.reg << 16);
        case kP3Cnt:
        case kP3Tag:
            return 0;
        default:
            return 0;  // unmapped offset inside the window reads 0
    }
}

void GifRegs::Write(uint32_t addr, unsigned /*size*/, uint64_t value) {
    const uint32_t v = static_cast<uint32_t>(value);
    switch (addr - kBase) {
        // CTRL: RST (bit 0) resets the unit, then PSE (bit 3) is latched from
        // the same write; no other CTRL bit is stored.
        case kCtrl:
            if (v & 1u) Reset();
            if (v & 8u) {
                ctrl_ |= 8u;
            } else {
                ctrl_ &= ~8u;
            }
            break;
        // MODE: M3R bit 0, IMT bit 2 (bit 1 stored, unused).
        case kMode:
            mode_ = v & 0x7u;
            break;
        default:
            break;  // STAT, TAG0-3, CNT, P3CNT, P3TAG and unmapped offsets are read-only
    }
}
