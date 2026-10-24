#pragma once
// MainMemoryEeRamSource: the EE side of an IOP-initiated EE->IOP SIF DMA --
// the sifcmd module's sceSifGetOtherData reads its bytes from the EE's main
// RAM (physical addresses; the kernel-segment bits are masked as the DMAC does).
#include <cstddef>
#include <cstdint>

#include "engine/iop/hle/sifman/ee_ram_source.h"

class MainMemory;

class MainMemoryEeRamSource : public ps2r::iop::hle::EeRamSource {
public:
    explicit MainMemoryEeRamSource(MainMemory& ram) : ram_(ram) {}
    bool ReadEeRam(uint32_t ee_addr, uint8_t* dst, size_t size) override;
    uint64_t Bytes() const { return bytes_; }

private:
    MainMemory& ram_;
    uint64_t bytes_ = 0;
};
