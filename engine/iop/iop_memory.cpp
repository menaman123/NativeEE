// engine/iop/iop_memory.cpp -- IopMemory storage and pointer access.
#include "engine/iop/iop_memory.h"

#include <algorithm>

namespace ps2r::iop {

IopMemory::IopMemory() : bytes_(kSize, 0) {}

uint8_t* IopMemory::Pointer(uint32_t addr, size_t size) {
    const uint32_t masked = addr & kMask;
    if (masked + size > kSize) {
        unrouted_++;
        return nullptr;
    }
    return bytes_.data() + masked;
}

const uint8_t* IopMemory::Pointer(uint32_t addr, size_t size) const {
    const uint32_t masked = addr & kMask;
    if (masked + size > kSize) return nullptr;
    return bytes_.data() + masked;
}

void IopMemory::Clear() { std::fill(bytes_.begin(), bytes_.end(), 0); }

}  // namespace ps2r::iop
