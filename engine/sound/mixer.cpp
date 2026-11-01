#include "engine/sound/mixer.h"

#include <chrono>

#include "engine/sound/mix_source.h"
#include "engine/sound/spu_core.h"
#include "engine/sound/voice_source.h"

namespace ps2r::sound {

Mixer::Mixer(SpuCore& core) : core_(&core) { out_.resize(static_cast<size_t>(kFramesPerVsync) * 2, 0.0f); }
Mixer::Mixer(MixSource& source) : source_(&source) { out_.resize(static_cast<size_t>(kFramesPerVsync) * 2, 0.0f); }

void Mixer::Pump() { Pump(kFramesPerVsync); }

void Mixer::Pump(int frames) {
    if (frames <= 0) {
        out_frames_ = 0;
        return;
    }
    if (out_.size() < static_cast<size_t>(frames) * 2) out_.resize(static_cast<size_t>(frames) * 2);
    const auto t0 = std::chrono::steady_clock::now();
    if (source_) source_->MixInto(out_.data(), frames);
    else if (core_) {
        if (voices_) voices_->BeginMixTick();
        core_->MixInto(out_.data(), frames, voices_, pcm_);
    } else {
        // A standalone output owner cannot be detached into a stale buffer.
        out_frames_ = 0;
        return;
    }
    const auto ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    pump_ns_ += ns;
    if (ns > pump_max_ns_) pump_max_ns_ = ns;
    out_frames_ = frames;
    pumps_++;
    frames_mixed_ += static_cast<uint64_t>(frames);
}

}  // namespace ps2r::sound
