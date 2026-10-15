#pragma once

#include <functional>
#include <string>

namespace ps2r::platform::sdl { class SdlAudioSink; class AudioPullRing; }
namespace ps2r::app {
// Output lifecycle only; logical voices/cursors remain with their service.
class SessionAudio {
public:
    using Main = std::function<bool(std::function<void()>)>;
    SessionAudio(platform::sdl::SdlAudioSink&, platform::sdl::AudioPullRing&, Main);
    // Supervisor after producer join: main closes/joins the callback, then
    // obsolete buffered output can be discarded without racing either end.
    bool StopForRestore(std::string&);
    bool ResumeAfterRestore(bool paused, std::string&);
private:
    platform::sdl::SdlAudioSink& sink_;
    platform::sdl::AudioPullRing& ring_;
    Main main_;
    bool stopped_ = false, reopen_ = false;
};
} // namespace ps2r::app
