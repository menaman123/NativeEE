#pragma once

#include <array>
#include <cstdint>

#include "engine/sound/mix_source.h"

namespace ps2r::sound {
class PcmSource;
class VoiceSource;

// Ordered channel summation and Movie PCM on the game owner. This output
// path has no register storage, command ring, sound RAM or retained voices.
class AudioMixer final : public MixSource {
public:
    static constexpr uint32_t kGroups = 2, kChannelsPerGroup = 24;
    struct State {
        float master = 1;
        std::array<float, kGroups> left{1, 1}, right{1, 1};
    };
    struct Counters {
        uint64_t ticks = 0, frames = 0, channel_samples = 0, unowned_samples = 0;
        uint64_t movie_frames = 0, audible_movie_frames = 0;
    };

    explicit AudioMixer(VoiceSource& voices) : voices_(voices) {}
    // Movie PCM and its gain belong to the Movie owner (title::MovieAudio,
    // persisted in MOVI). This sink only pops and scales; it stores no Movie
    // gain and no Movie PCM, so it has no Movie checkpoint state of its own.
    void SetMovieSource(PcmSource* source) { movie_ = source; }
    const PcmSource* MovieSource() const { return movie_; }
    bool SetMaster(float gain);
    bool SetGroup(uint32_t group, float left, float right);
    const State& Inspect() const { return state_; }
    const Counters& Count() const { return count_; }
    static bool Validate(const State& state);
    bool Restore(const State& state);
    void MixInto(float* stereo, int frames) override;

private:
    VoiceSource& voices_;
    PcmSource* movie_ = nullptr;
    State state_;
    Counters count_;
};
} // namespace ps2r::sound
