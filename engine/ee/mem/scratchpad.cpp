#include "engine/ee/mem/scratchpad.h"

Scratchpad::Scratchpad() : bytes_(kSize, 0) {}

uint8_t* Scratchpad::Pointer(uint32_t offset, size_t size) {
    if (static_cast<uint64_t>(offset) + size > bytes_.size()) return nullptr;
    return bytes_.data() + offset;
}
