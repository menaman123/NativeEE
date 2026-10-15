#include "engine/ee/mem/mmio_router.h"

MmioRouter::MmioRouter(MmioHandler& fallback) : fallback_(fallback) {}

void MmioRouter::Register(uint32_t base, uint32_t size, MmioHandler& handler, const char* name) {
    ranges_.push_back(Range{base, size, &handler, name});
}

MmioHandler* MmioRouter::Find(uint32_t addr, bool write, unsigned size, uint64_t value) {
    // Last registration wins: walk newest to oldest.
    for (auto it = ranges_.rbegin(); it != ranges_.rend(); ++it) {
        if (addr >= it->base && addr < static_cast<uint64_t>(it->base) + it->size) return it->handler;
    }
    unhandled_pages_.insert(addr & ~0xFFu);
    if (!first_unhandled_.seen) first_unhandled_ = FirstUnhandled{true, write, addr, size, write ? value : 0};
    return &fallback_;
}

uint64_t MmioRouter::Read(uint32_t addr, unsigned size) { return Find(addr, false, size, 0)->Read(addr, size); }

void MmioRouter::Write(uint32_t addr, unsigned size, uint64_t value) { Find(addr, true, size, value)->Write(addr, size, value); }
