#pragma once

#include <cstdint>

namespace ps2r::sound {

// Samples supplied by an owner on the game's audio clock. A handled silent
// channel still owns the slot; it must not revive a retained voice.
class VoiceSource {
public:
    virtual ~VoiceSource() = default;
    virtual void BeginMixTick() = 0;
    virtual bool StepVoice(uint32_t channel, float& left, float& right) = 0;
};

}  // namespace ps2r::sound
