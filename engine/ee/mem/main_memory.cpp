#include "engine/ee/mem/main_memory.h"

#include <algorithm>

MainMemory::MainMemory() : bytes_(kSize, 0) {}

uint8_t* MainMemory::Pointer(uint32_t phys, size_t size) {
    if (static_cast<uint64_t>(phys) + size > bytes_.size()) return nullptr;
    return bytes_.data() + phys;
}

const uint8_t* MainMemory::Pointer(uint32_t phys, size_t size) const {
    if (static_cast<uint64_t>(phys) + size > bytes_.size()) return nullptr;
    return bytes_.data() + phys;
}

void MainMemory::Clear() { std::fill(bytes_.begin(), bytes_.end(), 0); }
