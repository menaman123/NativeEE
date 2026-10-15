#pragma once

#include "engine/sound/voice_source.h"

class MixerVoiceSource final : public ps2r::sound::VoiceSource {
public:
    bool silent = false, handoff = false;
    int ticks = 0, owned = 0;
    void BeginMixTick() override;
    bool StepVoice(uint32_t channel, float& left, float& right) override;
};
