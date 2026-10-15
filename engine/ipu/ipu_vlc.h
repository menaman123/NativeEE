#pragma once

#include <cstdint>

namespace ps2r::ipu {

// EE User's Manual 8.5.3. Result: code length in 31:16, value in 15:0.
class IpuVlc {
public:
    static uint32_t Decode(uint32_t bits, unsigned table, unsigned picture_type, bool mpeg1);
};

}  // namespace ps2r::ipu
