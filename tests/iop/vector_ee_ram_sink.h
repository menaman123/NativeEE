// tests/iop/vector_ee_ram_sink.h -- VectorEeRamSink: a small byte-array EE RAM
// stand-in for the sifman module's SIF DMA sink in tests.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/iop/hle/sifman/ee_ram_sink.h"

class VectorEeRamSink : public ps2r::iop::hle::EeRamSink {
public:
    uint8_t bytes[64] = {};
    bool WriteEeRam(uint32_t ee_addr, const uint8_t* src, size_t size) override;
};
