#include "engine/sound/audio_mixer.h"

#include <algorithm>
#include <cmath>

#include "engine/sound/pcm_source.h"
#include "engine/sound/voice_source.h"

namespace ps2r::sound {
namespace {
bool Gain(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
}

bool AudioMixer::SetMaster(float gain) {
    if (!Gain(gain)) return false;
    state_.master = gain;
    return true;
}

bool AudioMixer::SetGroup(uint32_t group, float left, float right) {
    if (group >= kGroups || !Gain(left) || !Gain(right)) return false;
    state_.left[group] = left; state_.right[group] = right;
    return true;
}

bool AudioMixer::Validate(const State& state) {
    if (!Gain(state.master)) return false;
    for (uint32_t group = 0; group < kGroups; ++group)
        if (!Gain(state.left[group]) || !Gain(state.right[group])) return false;
    return true;
}

bool AudioMixer::Restore(const State& state) {
    if (!Validate(state)) return false;
    // Movie PCM lifetime (flush of an abandoned timeline, closed gain) is
    // restored by the Movie owner from MOVI; this sink holds none of it.
    state_ = state;
    return true;
}

void AudioMixer::MixInto(float* stereo, int frames) {
    if (frames <= 0) return;
    voices_.BeginMixTick();
    const auto gain = state_;
    // The Movie owner's gain is read once per pump, like the retained core
    // reads its BVOL shadows once per MixInto.
    const auto movie_gain = movie_ ? movie_->Gains() : std::array<float, 2>{0, 0};
    constexpr int chunk = 128;
    int16_t pcm[chunk * 2];
    for (int base = 0; base < frames; base += chunk) {
        const int n = std::min(chunk, frames - base);
        // Popped before the voices step so a muted output still consumes and
        // the movie's A/V clock keeps flowing (OLD SOUND_P5).
        const int movie_frames = movie_ ? movie_->Pop(pcm, n) : 0;
        count_.movie_frames += movie_frames;
        if (movie_gain[0] != 0 || movie_gain[1] != 0) count_.audible_movie_frames += movie_frames;
        for (int frame = 0; frame < n; ++frame) {
            // The SPU2 chain (SpuCore::MixInto, bit-exact): group 0 = core 0 (+ the movie input),
            // clamped and scaled by its gain, enters core 1 as its external input at full scale;
            // group 1's voices add, the sum is clamped and scaled by core 1's gain, then the master.
            float core_l[kGroups] = {0, 0}, core_r[kGroups] = {0, 0};
            for (uint32_t channel = 0; channel < kGroups * kChannelsPerGroup; ++channel) {
                float l = 0, r = 0;
                if (!voices_.StepVoice(channel, l, r)) {
                    ++count_.unowned_samples;
                    continue;
                }
                ++count_.channel_samples;
                core_l[channel / kChannelsPerGroup] += l;
                core_r[channel / kChannelsPerGroup] += r;
            }
            if (frame < movie_frames) {
                core_l[0] += static_cast<float>(pcm[frame * 2]) / 32768.0f * movie_gain[0];
                core_r[0] += static_cast<float>(pcm[frame * 2 + 1]) / 32768.0f * movie_gain[1];
            }
            const float ext_l = std::clamp(core_l[0], -1.0f, 1.0f) * gain.left[0] * kExternalInputGain;
            const float ext_r = std::clamp(core_r[0], -1.0f, 1.0f) * gain.right[0] * kExternalInputGain;
            const float left = std::clamp(core_l[1] + ext_l, -1.0f, 1.0f) * gain.left[1];
            const float right = std::clamp(core_r[1] + ext_r, -1.0f, 1.0f) * gain.right[1];
            stereo[(base + frame) * 2] = std::clamp(left * gain.master, -1.0f, 1.0f);
            stereo[(base + frame) * 2 + 1] = std::clamp(right * gain.master, -1.0f, 1.0f);
        }
    }
    ++count_.ticks;
    count_.frames += frames;
}
} // namespace ps2r::sound
