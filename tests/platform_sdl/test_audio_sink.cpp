// tests/platform_sdl/test_audio_sink.cpp -- SdlAudioSink over SDL's dummy audio
// driver (PHASE7_PLAN section 4.4): Open succeeds, Running() reports it, the
// negotiated format is exactly the request (allowed_changes = 0), a second Open is
// a no-op, Close is idempotent. When SDL cannot initialise audio at all the test
// prints SKIP and exits 0.
#include "engine/platform/sdl/sdl_audio_sink.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/platform/sdl/audio_pull_ring.h"

using ps2r::platform::sdl::AudioPullRing;
using ps2r::platform::sdl::SdlAudioSink;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

}  // namespace

int main() {
    SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");

    AudioPullRing ring;
    SdlAudioSink  sink;
    const ps2r::platform::AudioSpec spec{};   // 48000 Hz, 2 ch, 800-frame quantum
    CHECK(!sink.Running());

    if (!sink.Open(spec, ring)) {
        // Distinguish "no audio subsystem at all" from a real defect: with the
        // dummy driver the only way Open fails is SDL refusing SDL_INIT_AUDIO.
        if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::printf("SKIP: SDL audio unavailable (%s)\n", SDL_GetError());
            return 0;
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        std::fprintf(stderr, "FAIL: Open returned false with the dummy driver: %s\n", SDL_GetError());
        return 1;
    }

    CHECK(sink.Running());
    CHECK(sink.DeviceSampleRate() == 48000);
    CHECK(sink.DeviceChannels() == 2);
    CHECK(sink.DeviceBufferFrames() == 512);
    CHECK(std::strcmp(SDL_GetCurrentAudioDriver(), "dummy") == 0);

    // Idempotent Open while running.
    CHECK(sink.Open(spec, ring));
    CHECK(sink.Running());

    // Feed a few quanta so the dummy device's callback (if it fires during the
    // short window) has something to pull; it must never block the producer.
    std::vector<float> q(static_cast<size_t>(AudioPullRing::kQuantumFrames) * AudioPullRing::kChannels, 0.25f);
    for (int i = 0; i < 4; ++i) ring.Push(q.data(), AudioPullRing::kQuantumFrames);
    CHECK(ring.pushed() == 4u * AudioPullRing::kQuantumFrames);
    SDL_Delay(30);

    sink.Close();
    CHECK(!sink.Running());
    sink.Close();                         // idempotent
    CHECK(!sink.Running());
    CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);   // the sink released the subsystem it took

    // Re-open after Close works (the subsystem is re-initialised by Open).
    CHECK(sink.Open(spec, ring));
    CHECK(sink.Running());
    sink.Close();
    CHECK(!sink.Running());

    if (g_failures != 0) {
        std::fprintf(stderr, "test_audio_sink: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_audio_sink: PASS (dummy driver: open/running/idempotent-open/close/close/reopen; "
                "ring pushed=%llu pulled=%llu)\n",
                static_cast<unsigned long long>(ring.pushed()),
                static_cast<unsigned long long>(ring.pulled()));
    return 0;
}
