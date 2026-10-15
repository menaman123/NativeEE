// engine/ipu/hle/dmac_channel_face.h -- DmacChannelFace: the register face of the
// two IPU DMA channels (ch3 = IPU_FROM, ch4 = IPU_TO) that the libipu HLE needs.
// sceIpuStopDMA/sceIpuRestartDMA snapshot and restore MADR/TADR/QWC/CHCR of
// both channels and clear/set CHCR.STR around the IPU drain; the DMAC module
// implements this face at integration, the HLE never touches DMAC state directly.
#pragma once

#include <cstdint>

namespace ps2r::ipu {

class DmacChannelFace {
public:
    virtual ~DmacChannelFace() = default;

    // `channel` is the DMAC channel number: 3 (IPU_FROM) or 4 (IPU_TO).
    virtual uint32_t ReadChcr(uint32_t channel) = 0;
    virtual void WriteChcr(uint32_t channel, uint32_t value) = 0;
    virtual uint32_t ReadMadr(uint32_t channel) = 0;
    virtual void WriteMadr(uint32_t channel, uint32_t value) = 0;
    virtual uint32_t ReadTadr(uint32_t channel) = 0;
    virtual void WriteTadr(uint32_t channel, uint32_t value) = 0;
    virtual uint32_t ReadQwc(uint32_t channel) = 0;
    virtual void WriteQwc(uint32_t channel, uint32_t value) = 0;
};

}  // namespace ps2r::ipu
