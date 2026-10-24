// engine/iop/hle/sifman/ee_ram_source.h -- EeRamSource: the source of an
// EE->IOP SIF DMA the IOP side initiates (sceSifGetOtherData) as the sifcmd
// module sees it: a read of `size` bytes at an EE physical address. The EE
// side binds its main RAM; tests bind a byte vector. Sibling of EeRamSink.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2r::iop::hle {

class EeRamSource {
public:
    virtual ~EeRamSource() = default;
    // Copies `size` bytes from EE RAM at `ee_addr`; false when the range is not EE RAM.
    virtual bool ReadEeRam(uint32_t ee_addr, uint8_t* dst, size_t size) = 0;
};

}  // namespace ps2r::iop::hle
