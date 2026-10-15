#pragma once
// Guest memory access for the recompiled code. The fast path serves plain
// main-RAM accesses inline: phys = addr & 0x1FFFFFFF, and when phys + width
// fits inside the 32 MB the bytes are copied directly. Everything else --
// scratchpad, MMIO registers, hardware windows, registered extra regions,
// unmapped addresses -- takes the slow path through MmioRouter / AddressMap.
// Quadword accesses apply the R5900 LQ/SQ rule first: the low four address
// bits are ignored (addr & ~0xF). Reads of unmapped memory return 0; writes to
// unmapped memory are dropped. Templates live in fast_mem.inl.
#include <cstddef>
#include <cstdint>

#include "engine/ee/mem/quad_word.h"

class AddressMap;
class MainMemory;
class MmioRouter;

class FastMem {
public:
    FastMem(MainMemory& ram, AddressMap& map, MmioRouter& mmio);

    template <typename T>
    T Read(uint32_t addr);
    template <typename T>
    void Write(uint32_t addr, T value);
    void ReadQuad(uint32_t addr, QuadWord& out);
    void WriteQuad(uint32_t addr, const QuadWord& value);

    // The slow path, callable directly (the fast/slow equivalence test drives
    // both).
    template <typename T>
    T ReadSlow(uint32_t addr);
    template <typename T>
    void WriteSlow(uint32_t addr, T value);
    void ReadQuadSlow(uint32_t addr, QuadWord& out);
    void WriteQuadSlow(uint32_t addr, const QuadWord& value);

    // True when the fast path would serve this access.
    static bool FastPath(uint32_t addr, size_t width);

    AddressMap& Map() { return map_; }
    MmioRouter& Mmio() { return mmio_; }
    MainMemory& Ram() { return ram_; }

    // Write observer (Debug builds only; Release compiles the check out): a
    // diagnostics consumer installed by the composition root sees every main-RAM
    // store overlapping the physical range [lo, hi) before the bytes land --
    // (user, guest address, width, old bits, new bits). With no observer the
    // fast path pays one load and a branch. Process-wide (one guest memory).
    using WriteObserver = void (*)(void* user, uint32_t addr, unsigned width, uint64_t old_bits, uint64_t new_bits);
    static void SetWriteObserver(WriteObserver fn, void* user, uint32_t lo, uint32_t hi);
    static bool HasWriteObserver() { return observer_ != nullptr; }

private:
    // Out of line: reads the old bytes and calls the observer.
    void ObserveWrite(uint32_t phys, uint32_t addr, const void* value, unsigned width);

    MainMemory& ram_;
    AddressMap& map_;
    MmioRouter& mmio_;
    static WriteObserver observer_;
    static void* observer_user_;
    static uint32_t observe_lo_, observe_hi_;
};

#include "fast_mem.inl"
