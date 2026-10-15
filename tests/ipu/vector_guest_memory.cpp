// tests/ipu/vector_guest_memory.cpp -- VectorGuestMemory bodies.
#include "vector_guest_memory.h"

#include <cstring>

VectorGuestMemory::VectorGuestMemory(size_t bytes) : bytes_(bytes, 0) {}

uint8_t* VectorGuestMemory::Translate(uint32_t addr, size_t len) {
    if (static_cast<size_t>(addr) > bytes_.size() || len > bytes_.size() - addr) return nullptr;
    return bytes_.data() + addr;
}

uint32_t VectorGuestMemory::Read32(uint32_t addr) {
    const uint8_t* p = Translate(addr, 4);
    if (!p) return 0;
    uint32_t v;
    std::memcpy(&v, p, 4);  // little-endian host, as the EE is
    return v;
}

void VectorGuestMemory::Write32(uint32_t addr, uint32_t value) {
    if (uint8_t* p = Translate(addr, 4)) std::memcpy(p, &value, 4);
}
