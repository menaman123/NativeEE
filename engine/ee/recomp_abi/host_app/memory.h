#pragma once
// Generated-code ABI forwarder (row 3/4): `QuadWord` and the `memory::`
// free functions the generated loads/stores call, routed to the bound FastMem.
#include <cstdint>

#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/quad_word.h"
#include "engine/ee/recomp_abi/recomp_abi.h"

#ifdef PS2R_TRACE_ACCESS
// The tracing build (CMake PS2R_TRACE_ACCESS=ON): every generated load and
// store is reported to AccessTracer after it is served, so the per-call corpus
// records the bytes as the game saw them. Off: these four are the plain
// forwarders below and AccessTracer is not referenced.
#include "engine/ee/cpu/access_tracer.h"
#endif

namespace memory {

#ifdef PS2R_TRACE_ACCESS
template <typename T>
inline T read(uint32_t address) {
    T value = RecompAbi::Memory().Read<T>(address);
    AccessTracer::OnRead(address, &value, static_cast<uint32_t>(sizeof(T)));
    return value;
}

template <typename T>
inline void write(uint32_t address, T value) {
    RecompAbi::Memory().Write<T>(address, value);
    AccessTracer::OnWrite(address, &value, static_cast<uint32_t>(sizeof(T)));
}

inline void read_quad(uint32_t address, QuadWord& value) {
    RecompAbi::Memory().ReadQuad(address, value);
    AccessTracer::OnRead(address & ~0xFu, &value, 16);
}

inline void write_quad(uint32_t address, const QuadWord& value) {
    RecompAbi::Memory().WriteQuad(address, value);
    AccessTracer::OnWrite(address & ~0xFu, &value, 16);
}
#else
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
#endif

// Host pointer to `size` bytes of main RAM at a KUSEG/KSEG0/KSEG1/UCAB address
// (block copies: disc sectors landing in EE RAM). Null when the range leaves
// main RAM -- MMIO and the scratchpad have no linear host image here.
inline uint8_t* get_pointer(uint32_t address, size_t size) {
    return RecompAbi::Memory().Ram().Pointer(MainMemory::Phys(address), size);
}

}  // namespace memory
