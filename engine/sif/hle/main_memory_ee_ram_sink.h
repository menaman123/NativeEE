#pragma once
// MainMemoryEeRamSink: the EE side of an IOP->EE SIF DMA -- the sifman
// module's sceSifSetDma lands its bytes in the EE's main RAM (physical
// addresses; the kernel-segment bits are masked as the DMAC does).
#include <cstddef>
#include <cstdint>
#include <functional>

#include "engine/iop/hle/sifman/ee_ram_sink.h"

class MainMemory;

class MainMemoryEeRamSink : public ps2r::iop::hle::EeRamSink {
public:
    explicit MainMemoryEeRamSink(MainMemory& ram) : ram_(ram) {}
    bool WriteEeRam(uint32_t ee_addr, const uint8_t* src, size_t size) override;
    uint64_t Bytes() const { return bytes_; }
    // Diagnostic tap (census-class): every accepted write (EE physical address, bytes).
    using Tracer = std::function<void(uint32_t ee_addr, const uint8_t* src, size_t size)>;
    void SetTracer(Tracer t) { tracer_ = std::move(t); }

private:
    MainMemory& ram_;
    uint64_t bytes_ = 0;
    Tracer tracer_;
};
