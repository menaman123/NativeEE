#pragma once
// Dispatch of memory-mapped register accesses to the subsystem that owns the
// range. The register window is 0x10000000..0x1000FFFF (timers, IPU, GIF,
// VIF, FIFOs, DMAC, INTC, SIF) plus the GS privileged registers
// 0x12000000..0x12001FFF. Subsystems register [base, base+size) ranges; the
// most recently registered range wins so a later phase displaces the default
// sink without editing it. Anything unclaimed goes to the `fallback` handler
// (the MmioSink) and is counted per distinct 256-byte page for run.meta's
// `unhandled_mmio=` key.
#include <cstdint>
#include <set>
#include <vector>

#include "engine/ee/mem/mmio_handler.h"

class MmioRouter {
public:
    static constexpr uint32_t kIoBase = 0x10000000u;
    static constexpr uint32_t kIoEnd = 0x10010000u;
    static constexpr uint32_t kGsPrivBase = 0x12000000u;
    static constexpr uint32_t kGsPrivEnd = 0x12002000u;

    struct Range {
        uint32_t base;
        uint32_t size;
        MmioHandler* handler;
        const char* name;
    };

    explicit MmioRouter(MmioHandler& fallback);

    static bool IsIoRegister(uint32_t addr) {
        return (addr >= kIoBase && addr < kIoEnd) || (addr >= kGsPrivBase && addr < kGsPrivEnd);
    }

    void Register(uint32_t base, uint32_t size, MmioHandler& handler, const char* name);

    uint64_t Read(uint32_t addr, unsigned size);
    void Write(uint32_t addr, unsigned size, uint64_t value);

    // The first access that reached the fallback handler (run.meta's
    // `unhandled_mmio_first=` census): address, width, direction.
    struct FirstUnhandled {
        bool seen = false;
        bool write = false;
        uint32_t addr = 0;
        unsigned size = 0;
        uint64_t value = 0;   // the written value (0 for a read)
    };
    const FirstUnhandled& FirstUnhandledAccess() const { return first_unhandled_; }

    // Distinct 256-byte pages that reached the fallback handler.
    size_t UnhandledPageCount() const { return unhandled_pages_.size(); }
    const std::set<uint32_t>& UnhandledPages() const { return unhandled_pages_; }
    const std::vector<Range>& Ranges() const { return ranges_; }

private:
    MmioHandler* Find(uint32_t addr, bool write, unsigned size, uint64_t value);

    MmioHandler& fallback_;
    std::vector<Range> ranges_;
    std::set<uint32_t> unhandled_pages_;
    FirstUnhandled first_unhandled_;
};
