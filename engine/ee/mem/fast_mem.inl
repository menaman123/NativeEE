// FastMem templates (sibling of fast_mem.h).
#include <cstring>

#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"

inline bool FastMem::FastPath(uint32_t addr, size_t width) {
    const uint32_t phys = MainMemory::Phys(addr);
    return static_cast<uint64_t>(phys) + width <= MainMemory::kSize;
}

template <typename T>
inline T FastMem::Read(uint32_t addr) {
    const uint32_t phys = MainMemory::Phys(addr);
    if (static_cast<uint64_t>(phys) + sizeof(T) <= MainMemory::kSize) {
        T value;
        std::memcpy(&value, ram_.Data() + phys, sizeof(T));
        return value;
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
        std::memcpy(ram_.Data() + phys, &value, sizeof(T));
        return;
    }
    WriteSlow<T>(addr, value);
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
