#pragma once
// Interface every memory-mapped register range implements. `size` is the
// access width in bytes (1/2/4/8); the router hands a 16-byte access to the
// handler as two 8-byte halves.
#include <cstdint>

class MmioHandler {
public:
    virtual ~MmioHandler() = default;
    virtual uint64_t Read(uint32_t addr, unsigned size) = 0;
    virtual void Write(uint32_t addr, unsigned size, uint64_t value) = 0;
};
