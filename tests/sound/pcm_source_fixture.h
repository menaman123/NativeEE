#pragma once

#include <array>
#include <cstdint>

#include "engine/sound/fmv_pcm_input.h"
#include "engine/sound/pcm_source.h"

// Test stand-in for the Movie PCM owner: the decoder FIFO plus an explicit
// stereo gain, so an output sink can be compared against the retained core's
// BVOL arithmetic with asymmetric gains. The title's real owner is
// title::MovieAudio (one 15-bit gain); this fixture widens it for the pin.
class PcmSourceFixture final : public ps2r::sound::PcmSource {
public:
    explicit PcmSourceFixture(ps2r::sound::FmvPcmInput& input) : input_(input) {}
    std::array<float, 2> gains{0, 0};
    uint64_t pops = 0, frames = 0;
    std::array<float, 2> Gains() const override { return gains; }
    int Pop(int16_t* stereo, int n) override;

private:
    ps2r::sound::FmvPcmInput& input_;
};
