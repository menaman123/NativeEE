// tests/ipu/vector_guest_memory.h -- VectorGuestMemory: a GuestMemory over a flat
// zero-initialised byte vector (addresses start at 0). Out-of-range accesses read 0,
// write nothing and translate to nullptr, like an unmapped EE range.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/ipu/hle/guest_memory.h"

class VectorGuestMemory : public ps2r::ipu::GuestMemory {
public:
    static constexpr size_t kDefaultBytes = 1u << 20;  // 1 MiB

    explicit VectorGuestMemory(size_t bytes = kDefaultBytes);

    uint8_t* Translate(uint32_t addr, size_t len) override;
    uint32_t Read32(uint32_t addr) override;
    void Write32(uint32_t addr, uint32_t value) override;

    size_t Size() const { return bytes_.size(); }
    const std::vector<uint8_t>& Bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};
