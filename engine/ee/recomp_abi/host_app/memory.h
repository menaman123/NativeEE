#pragma once
// Generated-code ABI forwarder (row 3/4): `QuadWord` and the `memory::`
// free functions the generated loads/stores call, routed to the bound FastMem.
#include <cstdint>

#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/quad_word.h"
#include "engine/ee/recomp_abi/recomp_abi.h"

namespace memory {

template <typename T>
inline T read(uint32_t address) {
    return RecompAbi::Memory().Read<T>(address);
}

template <typename T>
inline void write(uint32_t address, T value) {
    RecompAbi::Memory().Write<T>(address, value);
}

inline void read_quad(uint32_t address, QuadWord& value) {
    RecompAbi::Memory().ReadQuad(address, value);
}

inline void write_quad(uint32_t address, const QuadWord& value) {
    RecompAbi::Memory().WriteQuad(address, value);
}

// Host pointer to `size` bytes of main RAM at a KUSEG/KSEG0/KSEG1 address
// (block copies: disc sectors landing in EE RAM). Null when the range leaves
// main RAM -- MMIO and the scratchpad have no linear host image here.
inline uint8_t* get_pointer(uint32_t address, size_t size) {
    return RecompAbi::Memory().Ram().Pointer(address & 0x1FFFFFFFu, size);
}

}  // namespace memory
