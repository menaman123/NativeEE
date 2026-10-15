// engine/iop/hle/sifman/ee_ram_sink.h -- EeRamSink: the destination of an
// IOP->EE SIF DMA (sceSifSetDma) as the sifman module sees it: a write of
// `size` bytes at an EE physical address. The EE side binds its main RAM;
// tests bind a byte vector.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2r::iop::hle {

class EeRamSink {
public:
    virtual ~EeRamSink() = default;
    // Copies `size` bytes to EE RAM at `ee_addr`; false when the range is not EE RAM.
    virtual bool WriteEeRam(uint32_t ee_addr, const uint8_t* src, size_t size) = 0;
};

}  // namespace ps2r::iop::hle
