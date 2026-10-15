#pragma once
// EE main RAM: 32 MB of physical memory. KUSEG (0x0xxxxxxx), KSEG0 (0x8...)
// and KSEG1 (0xA...) all map onto it through `addr & 0x1FFFFFFF`; the RAM
// itself is the low 32 MB of that physical window.
#include <cstddef>
#include <cstdint>
#include <vector>

class MainMemory {
public:
    static constexpr uint32_t kSize = 32u * 1024u * 1024u;
    static constexpr uint32_t kPhysMask = 0x1FFFFFFFu;

    MainMemory();

    // Physical address of a KUSEG/KSEG0/KSEG1 virtual address.
    static uint32_t Phys(uint32_t addr) { return addr & kPhysMask; }

    // Host pointer for `size` bytes at physical `phys`, or nullptr when the
    // range is not entirely inside RAM.
    uint8_t* Pointer(uint32_t phys, size_t size);
    const uint8_t* Pointer(uint32_t phys, size_t size) const;

    uint8_t* Data() { return bytes_.data(); }
    const uint8_t* Data() const { return bytes_.data(); }
    size_t Size() const { return bytes_.size(); }

    void Clear();

private:
    std::vector<uint8_t> bytes_;
};
