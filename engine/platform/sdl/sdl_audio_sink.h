// platform/sdl/sdl_audio_sink.h -- the SDL2 audio device half: opens ONE output
// device at the SPU2's native 48 kHz (no resampling on the main path), stereo
// float, 512-sample buffer (~10.7 ms), and serves its callback from an
// AudioPullRing. The device thread only ever READS the ring; it never calls
// into the engine and nothing it observes flows back (PHASE7_PLAN section 3.1).
//
// Whether to open the device at all is the CALLER's rule: under the
// deterministic profile (time.tick_sync = true) the composition root never
// calls Open(), so the harness runs with no audio device, as OLD did.
#pragma once

#include <cstdint>

#include "engine/platform/platform.h"

namespace ps2r::platform::sdl {

class AudioPullRing;

class SdlAudioSink {
public:
    SdlAudioSink() = default;
    ~SdlAudioSink();
    SdlAudioSink(const SdlAudioSink&) = delete;
    SdlAudioSink& operator=(const SdlAudioSink&) = delete;

    // Initialises SDL's audio subsystem, opens the default output device with
    // allowed_changes = 0 (SDL converts internally, so the callback format is
    // exactly the requested one) and starts the callback. Returns false, with
    // no device held, when SDL audio cannot be initialised or the device
    // cannot be opened -- the caller continues without sound. A second Open
    // while running is a no-op that returns true.
    bool Open(const AudioSpec& spec, AudioPullRing& ring);

    // Stops the callback (SDL_CloseAudioDevice waits for an in-flight
    // callback to return) and releases the audio subsystem. Safe when not open.
    void Close();
    // SDL owner only. Pause retains queued output; it does not change the
    // game clock. Restore instead closes, discards on the joined producer,
    // then reopens after approved state reconstruction.
    void SetPaused(bool paused);
    bool Paused() const { return device_ != 0 && paused_; }

    bool Running() const;

    // Format actually negotiated (equal to the request when Open returned true).
    int DeviceSampleRate() const;
    int DeviceChannels() const;
    int DeviceBufferFrames() const;

private:
    static void Callback(void* userdata, uint8_t* stream, int len);

    uint32_t       device_        = 0;        // SDL_AudioDeviceID; 0 = closed
    bool           audio_subsystem_ = false;  // SDL_INIT_AUDIO held by this sink
    bool           paused_ = false;
    AudioPullRing* ring_          = nullptr;
    int            got_freq_      = 0;
    int            got_channels_  = 0;
    int            got_samples_   = 0;
};

}  // namespace ps2r::platform::sdl
