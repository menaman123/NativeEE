#pragma once
// MainMemoryEeRamSink: the EE side of an IOP->EE SIF DMA -- the sifman
// module's sceSifSetDma lands its bytes in the EE's main RAM (physical
// addresses; the kernel-segment bits are masked as the DMAC does).
#include <cstddef>
#include <cstdint>

#include "engine/iop/hle/sifman/ee_ram_sink.h"

class MainMemory;

class MainMemoryEeRamSink : public ps2r::iop::hle::EeRamSink {
public:
    explicit MainMemoryEeRamSink(MainMemory& ram) : ram_(ram) {}
    bool WriteEeRam(uint32_t ee_addr, const uint8_t* src, size_t size) override;
    uint64_t Bytes() const { return bytes_; }

private:
    MainMemory& ram_;
    uint64_t bytes_ = 0;
};
