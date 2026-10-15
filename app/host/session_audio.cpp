#include "app/host/session_audio.h"

#include <utility>

#include "engine/platform/sdl/audio_pull_ring.h"
#include "engine/platform/sdl/sdl_audio_sink.h"

namespace ps2r::app {
SessionAudio::SessionAudio(platform::sdl::SdlAudioSink& sink, platform::sdl::AudioPullRing& ring, Main main)
    : sink_(sink), ring_(ring), main_(std::move(main)) {}

bool SessionAudio::StopForRestore(std::string& error) {
    if (stopped_) { error = "Audio output is already stopped for restore."; return false; }
    if (!main_ || !main_([&] {
            reopen_ = sink_.Running();
            sink_.Close();
        })) { error = "Audio owner unavailable during restore join."; return false; }
    ring_.DiscardStopped();
    stopped_ = true;
    return true;
}

bool SessionAudio::ResumeAfterRestore(bool paused, std::string& error) {
    if (!stopped_) { error = "Audio restore requires a joined preceding output."; return false; }
    bool ready = true;
    if (reopen_ && ring_.PadWithTail()) ring_.Prime();   // the clock contract: the lead the discard dropped
    if (!main_ || !main_([&] {
            if (reopen_) {
                platform::AudioSpec spec;
                ready = sink_.Open(spec, ring_);
                sink_.SetPaused(paused);
            }
        }) || !ready) { error = "Audio output could not resume."; return false; }
    stopped_ = false;
    return true;
}
} // namespace ps2r::app
