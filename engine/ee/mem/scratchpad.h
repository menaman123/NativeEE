#pragma once
// EE scratchpad RAM: 16 KB at 0x70000000. An address belongs to it when
// (addr & 0x70000000) == 0x70000000; the offset is the low 14 bits.
#include <cstddef>
#include <cstdint>
#include <vector>

class Scratchpad {
public:
    static constexpr uint32_t kSize = 16u * 1024u;
    static constexpr uint32_t kBase = 0x70000000u;
    static constexpr uint32_t kOffsetMask = 0x3FFFu;

    Scratchpad();

    static bool Contains(uint32_t addr) { return (addr & kBase) == kBase; }
    static uint32_t Offset(uint32_t addr) { return addr & kOffsetMask; }

    // Host pointer for `size` bytes at scratchpad `offset`, or nullptr when
    // the range runs past the 16 KB end.
    uint8_t* Pointer(uint32_t offset, size_t size);

    uint8_t* Data() { return bytes_.data(); }
    const uint8_t* Data() const { return bytes_.data(); }
    size_t Size() const { return bytes_.size(); }

private:
    std::vector<uint8_t> bytes_;
};
