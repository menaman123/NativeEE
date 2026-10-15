// engine/iop/iop_memory.inl -- IopMemory templates (included by iop_memory.h).
#pragma once

#include <cstring>

namespace ps2r::iop {

template <typename T>
T IopMemory::Read(uint32_t addr) const {
    const uint32_t masked = addr & kMask;
    if (masked + sizeof(T) > kSize) {
        const_cast<IopMemory*>(this)->unrouted_++;
        return T{};
    }
    T value;
    std::memcpy(&value, bytes_.data() + masked, sizeof(T));
    return value;
}

template <typename T>
void IopMemory::Write(uint32_t addr, T value) {
    const uint32_t masked = addr & kMask;
    if (masked + sizeof(T) > kSize) {
        unrouted_++;
        return;
    }
    std::memcpy(bytes_.data() + masked, &value, sizeof(T));
}

}  // namespace ps2r::iop
