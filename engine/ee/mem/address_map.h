#pragma once
// The EE address map: translates a guest virtual address + access size to a
// host byte pointer, in this fixed order (each arm is a PS2 address rule):
//   1. scratchpad          (addr & 0x70000000) == 0x70000000, 16 KB
//   2. hardware window     0x10000000..0x1FFFFFFF: registered byte windows
//                          (VU memories, IOP RAM, the 64 KB IO register
//                          image, BIOS) by VIRTUAL address; anything else in
//                          the window is unmapped (nullptr) -- MMIO registers
//                          are served by MmioRouter, not by pointer
//   3. main RAM + mirrors  phys = MainMemory::Phys(addr), phys < 32 MB (KUSEG/KSEG0/
//                          KSEG1 and the kernel's uncached / UCAB windows)
//   4. extra regions       registered by PHYSICAL address above RAM (a title
//                          registers its non-hardware arena here)
//   5. BIOS mirror         phys in 0x1FC00000..0x1FFFFFFF
//   6. unmapped            nullptr
// An access that straddles the end of its region is unmapped (nullptr), never
// partially served.
#include <cstddef>
#include <cstdint>
#include <vector>

class MainMemory;
class Scratchpad;

class AddressMap {
public:
    static constexpr uint32_t kHwWindowBase = 0x10000000u;
    static constexpr uint32_t kHwWindowEnd = 0x20000000u;
    static constexpr uint32_t kIoBase = 0x10000000u;
    static constexpr uint32_t kIoSize = 0x10000u;
    static constexpr uint32_t kBiosBase = 0x1FC00000u;
    static constexpr uint32_t kBiosSize = 0x400000u;

    struct Region {
        uint32_t base;
        uint32_t size;
        uint8_t* backing;
        const char* name;
    };

    AddressMap(MainMemory& ram, Scratchpad& spad);

    // Step 2 registrations (virtual base inside the hardware window).
    void RegisterHwWindow(uint32_t virt_base, uint32_t size, uint8_t* backing, const char* name);
    // Step 4 registrations (physical base at or above 32 MB).
    void RegisterExtraRegion(uint32_t phys_base, uint32_t size, uint8_t* backing, const char* name);

    uint8_t* Translate(uint32_t addr, size_t size);

    const std::vector<Region>& HwWindows() const { return hw_windows_; }
    const std::vector<Region>& ExtraRegions() const { return extra_regions_; }
    uint8_t* IoImage() { return io_image_.data(); }
    uint8_t* Bios() { return bios_.data(); }

private:
    static uint8_t* InRegion(const Region& r, uint32_t addr, size_t size);

    MainMemory& ram_;
    Scratchpad& spad_;
    std::vector<uint8_t> io_image_;
    std::vector<uint8_t> bios_;
    std::vector<Region> hw_windows_;
    std::vector<Region> extra_regions_;
};
