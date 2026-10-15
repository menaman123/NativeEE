#include "tests/sound/mixer_voice_source.h"

void MixerVoiceSource::BeginMixTick() { ++ticks; }

bool MixerVoiceSource::StepVoice(uint32_t channel, float& left, float& right) {
    if (handoff || (channel != 0 && channel != 24)) return false;
    ++owned;
    left = silent ? 0 : (channel == 0 ? 0.75f : -0.5f);
    right = silent ? 0 : (channel == 0 ? -0.25f : 0.5f);
    return true;
}
