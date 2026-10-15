#include "engine/platform/sdl/audio_pull_ring.h"
#include "engine/platform/sdl/sdl_audio_sink.h"
#include "app/host/session_audio.h"

#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

static unsigned checks = 0, failures = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (false)

int main() {
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    ps2r::platform::sdl::AudioPullRing ring;
    ps2r::platform::sdl::SdlAudioSink sink;
    ps2r::platform::AudioSpec spec;
    spec.sample_rate = 48000; spec.channels = 2;
    CHECK(sink.Open(spec, ring));
    if (!sink.Running()) return 1;
    sink.SetPaused(true);
    CHECK(sink.Paused());
    std::vector<float> before(800 * 2, 0.5f), after(800 * 2, -0.25f), actual(800 * 2);
    ring.Push(before.data(), 800);
    const auto pulls = ring.pulled();
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    CHECK(ring.Available() == 800 && ring.pulled() == pulls);
    CHECK(sink.Open(spec, ring) && sink.Paused()); // idempotent open never resumes a paused session
    sink.SetPaused(false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (ring.Available() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(!sink.Paused() && ring.Available() == 0 && ring.pulled() > pulls);

    sink.SetPaused(true);
    ring.Push(before.data(), 800);
    sink.Close(); // joins the callback before lifecycle discards its old output
    CHECK(!sink.Running() && !sink.Paused());
    const auto pushed = ring.pushed(), consumed = ring.pulled(), dropped = ring.dropped();
    CHECK(ring.DiscardStopped() == 800 && ring.Available() == 0);
    CHECK(ring.pushed() == pushed && ring.pulled() == consumed && ring.dropped() == dropped + 800);
    CHECK(ring.DiscardStopped() == 0); // repeated teardown adds no false work
    ring.Push(after.data(), 800);
    ring.Pull(actual.data(), 800);
    CHECK(actual == after); // no sample from the discarded timeline leaks
    CHECK(sink.Open(spec, ring));
    sink.SetPaused(true);
    CHECK(sink.Paused());
    sink.Close();
    sink.SetPaused(true); // no device is not an advertised paused device
    CHECK(!sink.Paused() && !sink.Running());
    spec.channels = 1;
    CHECK(!sink.Open(spec, ring) && !sink.Running());
    unsigned handoffs = 0;
    bool owner_available = true;
    const auto main = std::this_thread::get_id();
    ps2r::app::SessionAudio audio(sink, ring, [&](auto operation) {
        CHECK(std::this_thread::get_id() == main);
        if (!owner_available) return false;
        ++handoffs;
        operation();
        return true;
    });
    std::string error;
    CHECK(!audio.ResumeAfterRestore(false, error)); // no unmatched reconstruction
    spec.channels = 2;
    CHECK(sink.Open(spec, ring));
    sink.SetPaused(true);
    ring.Push(before.data(), 800);
    owner_available = false;
    CHECK(!audio.StopForRestore(error) && sink.Running() && ring.Available() == 800);
    owner_available = true;
    CHECK(audio.StopForRestore(error) && !sink.Running() && ring.Available() == 0);
    CHECK(!audio.StopForRestore(error)); // preserve the original reopen decision
    CHECK(audio.ResumeAfterRestore(true, error) && sink.Running() && sink.Paused());
    CHECK(handoffs == 2);
    CHECK(audio.StopForRestore(error));
    CHECK(audio.ResumeAfterRestore(false, error) && sink.Running() && !sink.Paused());
    sink.Close();
    CHECK(audio.StopForRestore(error));
    CHECK(audio.ResumeAfterRestore(false, error) && !sink.Running()); // no invented device
    std::printf("SessionAudio: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
