// engine/ipu/ipu_fifo_port.h -- the IPU's two FIFO ports on the EE bus.
//
// Besides the DMAC channels (ch4 toIPU, ch3 fromIPU) the IPU's FIFOs are reachable
// by programmed I/O (EE User's Manual, IPU chapter): IPU_out_FIFO at the port base
// (+0x00, read: the next quadword of decoded output) and IPU_in_FIFO at +0x10
// (write: a quadword of bitstream). The SDK's libipu uses the write port for the
// small transfers that do not warrant a DMA -- sceIpuSetIQ / sceIpuSetVQ push the
// 64-byte quantiser matrices this way before SETIQ / SETVQ -- so a device without
// the port drops the matrices and every decode after them is wrong.
//
// The EE stores a quadword as two 64-bit halves (FastMem::WriteQuadSlow: base and
// base+8); narrower accesses are accepted too. The window is 0x20 bytes; the class
// decodes the port from the low five bits of the address, so the composition root
// registers it over any 32-byte-aligned base (on the PS2 that base is the fixed
// IPU FIFO I/O address) and this file names no absolute address.
#pragma once

#include <cstdint>

#include "engine/ee/mem/mmio_handler.h"

namespace ps2r::ipu {

class IpuDevice;

class IpuFifoPort : public ::MmioHandler {
public:
    static constexpr uint32_t kWindowBytes = 0x20;
    static constexpr uint32_t kOutFifo = 0x00;   // read port
    static constexpr uint32_t kInFifo = 0x10;    // write port

    explicit IpuFifoPort(IpuDevice& device) : device_(device) {}

    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    uint64_t InBytes() const { return in_bytes_; }      // bytes pushed through the write port
    uint64_t OutBytes() const { return out_bytes_; }    // bytes popped through the read port
    uint64_t Unhandled() const { return unhandled_; }   // writes to the read port / reads of the write port

private:
    IpuDevice& device_;
    uint64_t in_bytes_ = 0;
    uint64_t out_bytes_ = 0;
    uint64_t unhandled_ = 0;
};

}  // namespace ps2r::ipu
