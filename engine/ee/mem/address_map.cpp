#include "engine/ee/mem/address_map.h"

#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"

AddressMap::AddressMap(MainMemory& ram, Scratchpad& spad)
    : ram_(ram), spad_(spad), io_image_(kIoSize, 0), bios_(kBiosSize, 0) {
    // The two windows every EE has: the IO register image (pointer-grade
    // access only; register semantics live in MmioRouter) and the BIOS ROM.
    RegisterHwWindow(kIoBase, kIoSize, io_image_.data(), "io");
    RegisterHwWindow(kBiosBase, kBiosSize, bios_.data(), "bios");
}

void AddressMap::RegisterHwWindow(uint32_t virt_base, uint32_t size, uint8_t* backing, const char* name) {
    hw_windows_.push_back(Region{virt_base, size, backing, name});
}

void AddressMap::RegisterExtraRegion(uint32_t phys_base, uint32_t size, uint8_t* backing, const char* name) {
    extra_regions_.push_back(Region{phys_base, size, backing, name});
}

uint8_t* AddressMap::InRegion(const Region& r, uint32_t addr, size_t size) {
    if (addr < r.base) return nullptr;
    const uint64_t off = static_cast<uint64_t>(addr) - r.base;
    if (off >= r.size) return nullptr;
    if (off + size > r.size) return nullptr;
    return r.backing + off;
}

uint8_t* AddressMap::Translate(uint32_t addr, size_t size) {
    // 1. scratchpad
    if (Scratchpad::Contains(addr)) return spad_.Pointer(Scratchpad::Offset(addr), size);

    // 2. hardware window, by virtual address
    if (addr >= kHwWindowBase && addr < kHwWindowEnd) {
        for (const Region& r : hw_windows_) {
            if (addr >= r.base && addr < static_cast<uint64_t>(r.base) + r.size) return InRegion(r, addr, size);
        }
        return nullptr;
    }

    // 3. main RAM and its KSEG0/KSEG1 mirrors
    const uint32_t phys = MainMemory::Phys(addr);
    if (phys < MainMemory::kSize) return ram_.Pointer(phys, size);

    // 4. registered extra regions (physical)
    for (const Region& r : extra_regions_) {
        if (phys >= r.base && phys < static_cast<uint64_t>(r.base) + r.size) return InRegion(r, phys, size);
    }

    // 5. BIOS mirror (physical)
    if (phys >= kBiosBase && phys < kBiosBase + kBiosSize) {
        return InRegion(Region{kBiosBase, kBiosSize, bios_.data(), "bios"}, phys, size);
    }

    // 6. unmapped
    return nullptr;
}
