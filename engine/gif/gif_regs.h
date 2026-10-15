#pragma once
// The GIF register file at 0x10003000 (CTRL, MODE, STAT, TAG0-3, CNT, P3CNT,
// P3TAG; 32-bit registers at 16-byte stride). Implements MmioHandler but is
// NOT registered with the router in this phase. Owns the in-flight transfer
// state the parser advances, because STAT/TAG/CNT are read back from it.
#include <atomic>
#include <cstdint>

#include "engine/ee/mem/mmio_handler.h"
#include "engine/gif/gif_tag.h"

class GifRegs : public MmioHandler {
public:
    static constexpr uint32_t kBase = 0x10003000u;
    static constexpr uint32_t kSize = 0x100u;
    static constexpr uint32_t kCtrl = 0x00u;
    static constexpr uint32_t kMode = 0x10u;
    static constexpr uint32_t kStat = 0x20u;
    static constexpr uint32_t kTag0 = 0x40u;
    static constexpr uint32_t kTag1 = 0x50u;
    static constexpr uint32_t kTag2 = 0x60u;
    static constexpr uint32_t kTag3 = 0x70u;
    static constexpr uint32_t kCnt = 0x80u;
    static constexpr uint32_t kP3Cnt = 0x90u;
    static constexpr uint32_t kP3Tag = 0xA0u;

    GifRegs();

    // Power-on / CTRL.RST: ctrl, mode, fifo, PATH3 mask and the transfer are zeroed.
    void Reset();
    // Savestate restore: CTRL, MODE and the VIF PATH3 mask take the saved
    // words; the FIFO count and the in-flight transfer are cleared (a state
    // is captured between transfers, so nothing is in flight after a load).
    void RestoreRegisters(uint32_t ctrl, uint32_t mode, bool path3_masked_by_vif);

    // 32-bit register read; `size` is ignored (every GIF register is 32 bits).
    uint64_t Read(uint32_t addr, unsigned size) override;
    // CTRL: RST then PSE from the same write; MODE: low three bits; others read-only.
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    uint32_t Ctrl() const { return ctrl_; }
    uint32_t Mode() const { return mode_; }
    // MODE.M3R: while set, received data is held in the FIFO and not parsed.
    bool Path3Masked() const { return (mode_ & 1u) != 0; }
    // VIF MSKPATH3 state, visible as STAT.M3P; written by the VIF, read here.
    bool Path3MaskedByVif() const { return path3_masked_.load(std::memory_order_relaxed); }
    void SetPath3MaskedByVif(bool masked) { path3_masked_.store(masked, std::memory_order_relaxed); }

    // The in-flight transfer (tag / loop / reg / in_transfer) the parser advances.
    GifTransferState& Transfer() { return transfer_; }
    const GifTransferState& Transfer() const { return transfer_; }

private:
    uint32_t ComposeStat() const;

    uint32_t ctrl_ = 0;
    uint32_t mode_ = 0;
    uint32_t fifo_count_ = 0;  // bytes in the GIF FIFO; nothing fills it, so FQC reads 0
    std::atomic<bool> path3_masked_{false};
    GifTransferState transfer_{};
};
