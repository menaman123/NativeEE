#include "engine/ipu/ipu_fifo_port.h"

#include <cstring>

#include "engine/ipu/ipu_device.h"

namespace ps2r::ipu {

uint64_t IpuFifoPort::Read(uint32_t addr, unsigned size) {
    const uint32_t offset = addr & (kWindowBytes - 1);
    if (offset >= kInFifo) {
        ++unhandled_;
        return 0;
    }
    uint8_t bytes[8] = {};
    const size_t want = size > 8 ? 8 : size;
    const size_t got = device_.DrainOutputFifo(bytes, want);
    out_bytes_ += got;
    uint64_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

void IpuFifoPort::Write(uint32_t addr, unsigned size, uint64_t value) {
    const uint32_t offset = addr & (kWindowBytes - 1);
    if (offset < kInFifo) {
        ++unhandled_;
        return;
    }
    uint8_t bytes[8];
    std::memcpy(bytes, &value, sizeof(bytes));
    const size_t n = size > 8 ? 8 : size;
    device_.FeedInputFifo(bytes, n);
    in_bytes_ += n;
}

}  // namespace ps2r::ipu
