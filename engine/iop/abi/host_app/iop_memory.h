#pragma once
// Generated-code ABI forwarder (engine/iop/abi): the `iop_memory::` free functions the
// generated loads/stores call, routed to the IopMemory bound through IopAbiSurface.
#include <cstdint>

#include "engine/iop/abi/iop_abi_surface.h"
#include "engine/iop/iop_memory.h"

namespace iop_memory {

template <typename T>
inline T read(uint32_t address) {
    return ps2r::iop::IopAbiSurface::Memory().Read<T>(address);
}

template <typename T>
inline void write(uint32_t address, T value) {
    ps2r::iop::IopAbiSurface::Memory().Write<T>(address, value);
}

}  // namespace iop_memory
