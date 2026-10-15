#pragma once
// TimerCodec: the pure bit-level rules of the EE timers T0-T3 -- Tn_MODE
// bitfield (EE Users Manual, timers), the write-1-to-clear flag acks, the
// TIMR savestate payload (section version 1: 4 x 34 bytes), and the 64-bit
// clock composition (acc << 16) | COUNT that an SDK-style timer reader forms
// from its overflow accumulator and the 16-bit hardware count. No state, no
// clock: EeTimers owns the registers and drives these rules.
//
// Tn_MODE: CLKS bits 0-1 (0 BUSCLK, 1 BUSCLK/16, 2 BUSCLK/256, 3 HBLANK),
// GATE bit 2, GATS bit 3, GATM bits 4-5, ZRET bit 6 (count returns to 0 on
// reaching COMP), CUE bit 7 (count enable), CMPE bit 8 (compare interrupt
// enable), OVFE bit 9 (overflow interrupt enable), EQUF bit 10 (compare flag,
// W1C), OVFF bit 11 (overflow flag, W1C).
#include <cstddef>
#include <cstdint>
#include <vector>

class TimerCodec {
public:
    static constexpr uint16_t kModeClksMask = 0x0003;
    static constexpr uint16_t kModeZret = 0x0040;
    static constexpr uint16_t kModeCue = 0x0080;
    static constexpr uint16_t kModeCmpe = 0x0100;
    static constexpr uint16_t kModeOvfe = 0x0200;
    static constexpr uint16_t kModeEquf = 0x0400;
    static constexpr uint16_t kModeOvff = 0x0800;
    static constexpr uint16_t kModeFlags = kModeEquf | kModeOvff;

    // One timer's register file + settle bookkeeping (the TIMR record).
    struct Regs {
        uint16_t mode = 0;
        uint16_t comp = 0;
        uint16_t hold = 0;
        uint32_t count_base = 0;     // count at baseline_tick (< period)
        uint64_t baseline_tick = 0;  // emu tick of the last settle / rebase
        uint64_t pending_ovf = 0;    // overflow events awaiting dispatch
        uint64_t pending_cmp = 0;    // compare events awaiting dispatch
    };
    static constexpr size_t kRegsPayloadLen = 2 + 2 + 2 + 4 + 8 + 8 + 8;
    static constexpr size_t kTimrPayloadLen = 4 * kRegsPayloadLen;

    // MODE write: bits 10/11 are write-1-to-clear acks, the rest store;
    // un-acked flag bits survive.
    static uint16_t ApplyModeWrite(uint16_t old_mode, uint32_t value);
    static uint32_t Clks(uint16_t mode) { return mode & kModeClksMask; }

    // The reader's 64-bit clock from an overflow accumulator + COUNT.
    static uint64_t ComposeClock(uint64_t acc, uint16_t count) {
        return (acc << 16) | static_cast<uint64_t>(count);
    }

    // TIMR payload (little-endian, field order as declared; 136 bytes).
    static void Encode(const Regs t[4], std::vector<uint8_t>& out);
    static bool Decode(const uint8_t* p, size_t len, Regs t[4]);
};
