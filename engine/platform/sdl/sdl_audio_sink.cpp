// platform/sdl/sdl_audio_sink.cpp -- see sdl_audio_sink.h.
#include "engine/platform/sdl/sdl_audio_sink.h"

#include <SDL.h>

#include <cstdio>

#include "engine/platform/sdl/audio_pull_ring.h"

namespace ps2r::platform::sdl {

namespace {
constexpr int kDeviceBufferSamples = 512;   // per callback, ~10.7 ms at 48 kHz
}  // namespace

SdlAudioSink::~SdlAudioSink() { Close(); }

void SdlAudioSink::Callback(void* userdata, uint8_t* stream, int len) {
    auto* self = static_cast<SdlAudioSink*>(userdata);
    float* out = reinterpret_cast<float*>(stream);
    const int frames = len / (AudioPullRing::kChannels * static_cast<int>(sizeof(float)));
    self->ring_->Pull(out, frames);
}

bool SdlAudioSink::Open(const AudioSpec& spec, AudioPullRing& ring) {
    if (device_ != 0) return true;
    if (spec.channels != AudioPullRing::kChannels) {
        std::fprintf(stderr, "audio_sink: %d channels requested, the ring is stereo\n",
                     spec.channels);
        return false;
    }

    if (!audio_subsystem_) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::fprintf(stderr, "audio_sink: SDL_InitSubSystem(AUDIO) failed: %s\n",
                         SDL_GetError());
            return false;
        }
        audio_subsystem_ = true;
    }

    ring_ = &ring;

    SDL_AudioSpec want{};
    want.freq     = spec.sample_rate;
    want.format   = AUDIO_F32SYS;
    want.channels = static_cast<Uint8>(spec.channels);
    want.samples  = kDeviceBufferSamples;
    want.callback = &SdlAudioSink::Callback;
    want.userdata = this;

    SDL_AudioSpec got{};
    // allowed_changes = 0: SDL converts internally if the hardware differs,
    // so the callback always sees exactly `want`.
    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (dev == 0) {
        std::fprintf(stderr, "audio_sink: SDL_OpenAudioDevice failed: %s (continuing without sound)\n",
                     SDL_GetError());
        ring_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audio_subsystem_ = false;
        return false;
    }
    device_       = dev;
    got_freq_     = got.freq;
    got_channels_ = got.channels;
    got_samples_  = got.samples;
    paused_ = false;
    SDL_PauseAudioDevice(dev, 0);   // start the callback
    return true;
}

void SdlAudioSink::Close() {
    if (device_ != 0) {
        // Returns only after any in-flight callback has finished: after this
        // the device thread no longer touches the ring.
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        ring_   = nullptr;
    }
    if (audio_subsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audio_subsystem_ = false;
    }
    paused_ = false;
}

void SdlAudioSink::SetPaused(bool paused) {
    if (!device_ || paused_ == paused) return;
    SDL_PauseAudioDevice(device_, paused ? 1 : 0);
    paused_ = paused;
}

bool SdlAudioSink::Running() const { return device_ != 0; }

int SdlAudioSink::DeviceSampleRate() const   { return got_freq_; }
int SdlAudioSink::DeviceChannels() const     { return got_channels_; }
int SdlAudioSink::DeviceBufferFrames() const { return got_samples_; }

}  // namespace ps2r::platform::sdl
