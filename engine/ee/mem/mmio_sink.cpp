#include "engine/ee/mem/mmio_sink.h"

// Idle read values, lifted from the original read path. Rows are matched
// first-hit in this order (specific registers before their enclosing range).
// Anything not listed reads 0 (memory.cpp:977 "Unknown I/O register").
const std::vector<MmioSink::IdleRow>& MmioSink::IdleTable() {
    static const std::vector<IdleRow> table = {
        // GS_CSR: the first-match arm returns 0x8 (VSINT set) for the 32-bit
        // word at 0x12001000 so a SyncV poll completes (memory.cpp:921-929);
        // its high word and every other GS privileged register read 0 (:931).
        {0x12001000u, 0x4u, 0x8u, "GS_CSR"},
        {0x12000000u, 0x2000u, 0x0u, "GS_PRIV"},
        // EE timers T0..T3: COUNT/MODE/COMP/HOLD idle 0 (the EeTimers class
        // displaces this whole range in stage 3D2).
        {0x10000000u, 0x2000u, 0x0u, "TIMERS"},
        // IPU CMD/CTRL/BP/TOP: 0 (memory.cpp:619-627).
        {0x10002000u, 0x1000u, 0x0u, "IPU"},
        // GIF regs: GIF_STAT is a pure predicate of live state, all false at
        // idle -> 0 (gif.cpp:221-246); other GIF regs 0.
        {0x10003000u, 0x100u, 0x0u, "GIF"},
        // VIF0/VIF1: STAT falls through to 0 = idle (memory.cpp:539-580);
        // FBRST/ERR read the unit's zeroed registers (:584-618).
        {0x10003800u, 0x800u, 0x0u, "VIF"},
        // VIF0/VIF1/GIF/IPU FIFOs: 0.
        {0x10004000u, 0x4000u, 0x0u, "FIFO"},
        // DMAC channel registers: CHCR.STR=0, MADR/QWC/TADR 0 (memory.cpp:910-912,
        // dmac.cpp:1045-1125); control registers D_CTRL..D_STADR 0 (:933-935).
        {0x10008000u, 0x7000u, 0x0u, "DMAC"},
        // INTC_STAT/INTC_MASK: 0 until the Intc class displaces them (3D2).
        {0x1000F000u, 0x20u, 0x0u, "INTC"},
        // SIF MSCOM/SMCOM/MSFLAG/SMFLAG/CTL: 0 at idle (memory.cpp:962-971);
        // Phase 4 displaces.
        {0x1000F200u, 0x100u, 0x0u, "SIF"},
        // D_ENABLER reads the `enabler` latch, 0 at power-on (dmac.h:75,
        // dmac.cpp:1127); D_ENABLEW (0x1000F590) reads 0 (:1128).
        {0x1000F520u, 0x10u, 0x0u, "D_ENABLER"},
        {0x1000F590u, 0x10u, 0x0u, "D_ENABLEW"},
    };
    return table;
}

MmioSink::MmioSink() = default;

uint32_t MmioSink::IdleValue(uint32_t addr) const {
    for (const IdleRow& r : IdleTable()) {
        if (addr >= r.base && addr < static_cast<uint64_t>(r.base) + r.size) return r.value;
    }
    return 0;
}

uint64_t MmioSink::Read(uint32_t addr, unsigned size) {
    ++reads_;
    const uint32_t lo = IdleValue(addr & ~3u);
    if (size <= 4) {
        // Sub-word reads see the byte lanes of the aligned word.
        const unsigned shift = (addr & 3u) * 8u;
        const uint64_t mask = size == 4 ? 0xFFFFFFFFull : ((1ull << (size * 8u)) - 1ull);
        return (static_cast<uint64_t>(lo) >> shift) & mask;
    }
    const uint32_t hi = IdleValue((addr & ~7u) + 4u);
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

void MmioSink::Write(uint32_t, unsigned, uint64_t) { ++writes_; }
