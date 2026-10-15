#include "engine/ipu/hle/guest_memory_adapter.h"

#include <cstring>
#include <utility>

namespace ps2r::ipu {

GuestMemoryAdapter::GuestMemoryAdapter(Translator translate) : translate_(std::move(translate)) {}

uint8_t* GuestMemoryAdapter::Translate(uint32_t addr, size_t len) {
    return translate_ ? translate_(addr, len) : nullptr;
}

uint32_t GuestMemoryAdapter::Read32(uint32_t addr) {
    uint8_t* p = Translate(addr, sizeof(uint32_t));
    if (!p) {
        ++unbacked_;
        return 0;
    }
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));   // the EE is little-endian, as the host is
    return v;
}

void GuestMemoryAdapter::Write32(uint32_t addr, uint32_t value) {
    uint8_t* p = Translate(addr, sizeof(uint32_t));
    if (!p) {
        ++unbacked_;
        return;
    }
    std::memcpy(p, &value, sizeof(value));
}

}  // namespace ps2r::ipu
