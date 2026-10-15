// engine/ipu/hle/dmac_channel_face_adapter.h -- DmacChannelFaceAdapter: the
// DmacChannelFace over the DMAC's two IPU channel register files (ch3 IPU_FROM,
// ch4 IPU_TO). libipu's sceIpuStopDMA / sceIpuRestartDMA read and write
// CHCR/MADR/TADR/QWC of those channels. Register writes alias the DMAC-owned
// PODs; a CHCR write with STR set then calls the injected channel starter.
// ch3 drains IPU output to RAM; ch4 feeds RAM to IPU input. Any other channel
// number reads 0 and drops the write (counted).
#pragma once

#include <cstdint>
#include <functional>

#include "engine/dmac/dmac_channel.h"
#include "engine/ipu/hle/dmac_channel_face.h"

namespace ps2r::ipu {

class DmacChannelFaceAdapter : public DmacChannelFace {
public:
    using Kick = std::function<void(uint32_t channel)>;
    static constexpr uint32_t kChannelIpuFrom = static_cast<uint32_t>(DmacChannelId::IpuFrom);
    static constexpr uint32_t kChannelIpuTo = static_cast<uint32_t>(DmacChannelId::IpuTo);

    DmacChannelFaceAdapter(::DmacChannelRegs& ipu_from, ::DmacChannelRegs& ipu_to, Kick kick = {});

    uint32_t ReadChcr(uint32_t channel) override;
    void WriteChcr(uint32_t channel, uint32_t value) override;
    uint32_t ReadMadr(uint32_t channel) override;
    void WriteMadr(uint32_t channel, uint32_t value) override;
    uint32_t ReadTadr(uint32_t channel) override;
    void WriteTadr(uint32_t channel, uint32_t value) override;
    uint32_t ReadQwc(uint32_t channel) override;
    void WriteQwc(uint32_t channel, uint32_t value) override;

    // Accesses naming a channel other than 3 or 4 (dropped).
    uint64_t OutOfRange() const { return out_of_range_; }

private:
    ::DmacChannelRegs* Regs(uint32_t channel);

    ::DmacChannelRegs& ipu_from_;
    ::DmacChannelRegs& ipu_to_;
    Kick kick_;
    uint64_t out_of_range_ = 0;
};

}  // namespace ps2r::ipu
