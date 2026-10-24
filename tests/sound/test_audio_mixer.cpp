#include <array>
#include <cstdio>
#include <limits>

#include "engine/sound/audio_mixer.h"
#include "engine/sound/fmv_pcm_input.h"
#include "engine/sound/mixer.h"
#include "engine/sound/spu_core.h"
#include "tests/sound/mixer_voice_source.h"
#include "tests/sound/pcm_source_fixture.h"

int main() {
    int checks = 0, failures = 0;
    const auto check = [&](bool value, const char* label) {
        ++checks;
        if (!value) { ++failures; std::printf("FAIL %s\n", label); }
    };
    ps2r::sound::SpuCore spu;
    MixerVoiceSource retained_voices, owned_voices;
    ps2r::sound::AudioMixer audio(owned_voices);
    ps2r::sound::Mixer model(spu), selected(audio);
    model.SetVoiceSource(&retained_voices);
    ps2r::sound::FmvPcmInput model_pcm(4096), owned_pcm(4096);
    PcmSourceFixture owned_movie(owned_pcm);
    spu.SetFmvInput(&model_pcm); audio.SetMovieSource(&owned_movie);
    check(audio.MovieSource() == &owned_movie, "sink reports its single Movie owner");
    std::array<int16_t, 2048 * 2> pcm{};
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<int16_t>(int(i * 97 % 65536) - 32768);
    for (int mode = 0; mode < 6; ++mode) {
        const float master = mode == 2 ? 0 : .8f;
        const float left = mode == 1 ? 0 : 32767.0f / 32768, right = mode == 1 ? 0 : 24575.0f / 32768;
        retained_voices.silent = owned_voices.silent = mode == 3;
        retained_voices.handoff = owned_voices.handoff = mode == 4;
        spu.SetMasterVolume(master); check(audio.SetMaster(master), "master gain");
        for (uint32_t group = 0; group < 2; ++group) {
            const float l = group ? 1 : .75f, r = group ? .5f : .25f;
            spu.SetMvolL(group, l); spu.SetMvolR(group, r);
            check(audio.SetGroup(group, l, r), "group gain");
        }
        spu.SetParam(0xf80, mode == 1 ? 0 : 0x7fff);
        spu.SetParam(0x1080, mode == 1 ? 0 : 0x5fff);
        owned_movie.gains = {left, right}; // the Movie owner's gain, never the sink's
        model_pcm.Flush(); owned_pcm.Flush();
        model_pcm.Push(pcm.data(), 2048); owned_pcm.Push(pcm.data(), 2048);
        if (mode != 5) { model_pcm.GateOpen(); owned_pcm.GateOpen(); }
        for (const int frames : {0, -1, 1, 127, 128, 129, 800, 960, 2048}) {
            const auto calls = spu.MixCalls();
            model.Pump(frames);
            const auto after_model = spu.MixCalls();
            selected.Pump(frames);
            check(after_model == calls + (frames > 0) && spu.MixCalls() == after_model,
                  "only comparison arm executes SpuCore mixing");
            check(model.OutputFrames() == selected.OutputFrames(), "output length");
            for (int sample = 0; sample < selected.OutputFrames() * 2; ++sample)
                check(model.Output()[sample] == selected.Output()[sample], "bit exact ordered output");
            check(model_pcm.Queued() == owned_pcm.Queued() &&
                model_pcm.Consumed() == owned_pcm.Consumed(), "PCM clock including mute and closed gate");
            check(retained_voices.ticks == owned_voices.ticks &&
                retained_voices.owned == owned_voices.owned, "one tick and same voice stepping");
        }
        if (mode == 5) {
            check(owned_pcm.Queued() == 2048, "closed gate keeps backlog");
            owned_pcm.Flush();
        }
    }
    auto saved = audio.Inspect();
    check(audio.SetGroup(0, .4f, .6f), "change owned gains");
    saved = audio.Inspect();
    check(audio.SetMaster(0) && audio.SetGroup(0, 0, 0) && audio.Restore(saved), "restore gain owner");
    check(audio.Inspect().master == saved.master && audio.Inspect().left == saved.left &&
        audio.Inspect().right == saved.right,
        "gain state restored");
    auto invalid = saved;
    invalid.left[1] = std::numeric_limits<float>::quiet_NaN();
    owned_pcm.Push(pcm.data(), 129); owned_pcm.GateOpen();
    check(!audio.Restore(invalid) && audio.Inspect().left == saved.left && owned_pcm.Queued() == 129,
          "invalid snapshot preserves future PCM and gains");
    check(audio.Restore(saved) && owned_pcm.Queued() == 129 && owned_pcm.GateIsOpen(),
          "output restore leaves Movie PCM lifetime to the Movie owner");
    owned_pcm.Flush();
    check(!audio.SetGroup(2, 1, 1) && !audio.SetMaster(-1),
          "invalid gains cannot enter output");
    check(audio.Count().frames > 0 && audio.Count().movie_frames > 0 &&
        audio.Count().channel_samples > 0, "positive owned voices and PCM");
    check(owned_movie.frames == audio.Count().movie_frames && owned_movie.pops > 0,
          "every Movie frame came through the owner's Pop");
    audio.SetMovieSource(nullptr);
    owned_pcm.Push(pcm.data(), 128); owned_pcm.GateOpen();
    ps2r::sound::Mixer detached(audio);
    detached.Pump(128);
    check(owned_pcm.Queued() == 128 && audio.MovieSource() == nullptr, "detached owner is not popped");
    owned_pcm.Flush();
    // An output owner has no retained channel to fall back to. A false
    // provider answer cannot replay an old keyed voice or queued command.
    ps2r::sound::Mixer switched(spu);
    switched.SetMixSource(&audio);
    owned_voices.handoff = true;
    const auto calls = spu.MixCalls();
    switched.Pump(1);
    check(spu.MixCalls() == calls && switched.Output()[0] == 0 && switched.Output()[1] == 0,
          "selected output never enters retained mixer");
    std::printf("AudioMixer %d checks %d failures; frames=%llu Movie=%llu\n", checks, failures,
        static_cast<unsigned long long>(audio.Count().frames),
        static_cast<unsigned long long>(audio.Count().movie_frames));
    return failures ? 1 : 0;
}
