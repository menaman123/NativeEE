// engine/ipu/hle/guest_memory.h -- GuestMemory: the EE-RAM face the sceMpeg HLE
// reads and writes through. A pure interface so the HLE bodies (and their tests)
// never depend on engine/ee/mem; the composition root adapts the real EE memory
// to it at integration.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2r::ipu {

class GuestMemory {
public:
    virtual ~GuestMemory() = default;

    // Host pointer to `len` contiguous guest bytes at EE address `addr`, or
    // nullptr when the range is not backed by RAM (the caller skips the write
    // and reports it, as libmpeg's DMA into an unmapped buffer would).
    virtual uint8_t* Translate(uint32_t addr, size_t len) = 0;

    // Little-endian 32-bit guest word access (the SceMpeg context fields the
    // libmpeg library keeps in EE RAM are all 32-bit words).
    virtual uint32_t Read32(uint32_t addr) = 0;
    virtual void Write32(uint32_t addr, uint32_t value) = 0;
};

}  // namespace ps2r::ipu
