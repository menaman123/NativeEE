// FastMem templates and quad fast paths (sibling of fast_mem.h).
#include <cstring>

#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/scratchpad.h"

inline bool FastMem::FastPath(uint32_t addr, size_t width) {
    const uint32_t phys = MainMemory::Phys(addr);
    return static_cast<uint64_t>(phys) + width <= MainMemory::kSize;
}

template <typename T>
inline T FastMem::Read(uint32_t addr) {
    const uint32_t phys = MainMemory::Phys(addr);
    if (static_cast<uint64_t>(phys) + sizeof(T) <= MainMemory::kSize) {
        T value;
        std::memcpy(&value, ram_base_ + phys, sizeof(T));
        return value;
    }
    if (Scratchpad::Contains(addr)) {
        const uint32_t off = Scratchpad::Offset(addr);
        if (static_cast<uint64_t>(off) + sizeof(T) <= Scratchpad::kSize) {
            T value;
            std::memcpy(&value, spad_base_ + off, sizeof(T));
            return value;
        }
    }
    return ReadSlow<T>(addr);
}

template <typename T>
inline void FastMem::Write(uint32_t addr, T value) {
    const uint32_t phys = MainMemory::Phys(addr);
    if (static_cast<uint64_t>(phys) + sizeof(T) <= MainMemory::kSize) {
#ifndef NDEBUG
        if (observer_ && phys < observe_hi_ && phys + sizeof(T) > observe_lo_) ObserveWrite(phys, addr, &value, sizeof(T));
#endif
        std::memcpy(ram_base_ + phys, &value, sizeof(T));
        return;
    }
    if (Scratchpad::Contains(addr)) {
        const uint32_t off = Scratchpad::Offset(addr);
        if (static_cast<uint64_t>(off) + sizeof(T) <= Scratchpad::kSize) {
            std::memcpy(spad_base_ + off, &value, sizeof(T));
            return;
        }
    }
    WriteSlow<T>(addr, value);
}

inline void FastMem::ReadQuad(uint32_t addr, QuadWord& out) {
    const uint32_t aligned = addr & ~0xFu;
    const uint32_t phys = MainMemory::Phys(aligned);
    if (static_cast<uint64_t>(phys) + sizeof(QuadWord) <= MainMemory::kSize) {
        std::memcpy(&out, ram_base_ + phys, sizeof(QuadWord));
        return;
    }
    if (Scratchpad::Contains(aligned)) {
        // 16-aligned inside 16 KB: never runs past the end.
        std::memcpy(&out, spad_base_ + Scratchpad::Offset(aligned), sizeof(QuadWord));
        return;
    }
    ReadQuadSlow(addr, out);
}

inline void FastMem::WriteQuad(uint32_t addr, const QuadWord& value) {
    const uint32_t aligned = addr & ~0xFu;
    const uint32_t phys = MainMemory::Phys(aligned);
    if (static_cast<uint64_t>(phys) + sizeof(QuadWord) <= MainMemory::kSize) {
#ifndef NDEBUG
        // A quadword store is observed as its two 8-byte halves.
        if (observer_ && phys < observe_hi_ && phys + sizeof(QuadWord) > observe_lo_) {
            ObserveWrite(phys, aligned, &value.u64[0], 8);
            ObserveWrite(phys + 8, aligned + 8, &value.u64[1], 8);
        }
#endif
        std::memcpy(ram_base_ + phys, &value, sizeof(QuadWord));
        return;
    }
    if (Scratchpad::Contains(aligned)) {
        std::memcpy(spad_base_ + Scratchpad::Offset(aligned), &value, sizeof(QuadWord));
        return;
    }
    WriteQuadSlow(addr, value);
}

template <typename T>
inline T FastMem::ReadSlow(uint32_t addr) {
    if (MmioRouter::IsIoRegister(addr)) {
        return static_cast<T>(mmio_.Read(addr, static_cast<unsigned>(sizeof(T))));
    }
    T value{};
    if (const uint8_t* p = map_.Translate(addr, sizeof(T))) std::memcpy(&value, p, sizeof(T));
    return value;
}

template <typename T>
inline void FastMem::WriteSlow(uint32_t addr, T value) {
    if (MmioRouter::IsIoRegister(addr)) {
        uint64_t raw = 0;
        std::memcpy(&raw, &value, sizeof(T));
        mmio_.Write(addr, static_cast<unsigned>(sizeof(T)), raw);
        return;
    }
    if (uint8_t* p = map_.Translate(addr, sizeof(T))) std::memcpy(p, &value, sizeof(T));
}
