// tests/ipu/recording_dmac_channel_face.h -- RecordingDmacChannelFace: a
// DmacChannelFace over a plain register file that records every access in order
// (test support for LibipuApi).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/ipu/hle/dmac_channel_face.h"

namespace ps2r::ipu {

struct DmacChannelRegs {
    uint32_t chcr = 0;
    uint32_t madr = 0;
    uint32_t tadr = 0;
    uint32_t qwc = 0;
};

class RecordingDmacChannelFace : public DmacChannelFace {
public:
    static constexpr uint32_t kChannels = 10;

    uint32_t ReadChcr(uint32_t channel) override;
    void WriteChcr(uint32_t channel, uint32_t value) override;
    uint32_t ReadMadr(uint32_t channel) override;
    void WriteMadr(uint32_t channel, uint32_t value) override;
    uint32_t ReadTadr(uint32_t channel) override;
    void WriteTadr(uint32_t channel, uint32_t value) override;
    uint32_t ReadQwc(uint32_t channel) override;
    void WriteQwc(uint32_t channel, uint32_t value) override;

    DmacChannelRegs& Channel(uint32_t channel) { return regs_[channel % kChannels]; }
    // Access log: "R3.CHCR", "W4.MADR=0x..." in call order.
    const std::vector<std::string>& Log() const { return log_; }
    void ClearLog() { log_.clear(); }

private:
    void Note(char kind, uint32_t channel, const char* reg, uint32_t value);

    DmacChannelRegs regs_[kChannels];
    std::vector<std::string> log_;
};

}  // namespace ps2r::ipu
