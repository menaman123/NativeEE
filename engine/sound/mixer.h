#pragma once
// Mixer: the tick-clocked pump over the selected audio owner. Every EE frame boundary
// (the Verify slot, after the maintenance duties) pumps exactly
// kFramesPerVsync = 48000 / 60 = 800 stereo frames out of the core into its
// output buffer; the buffer feeds the AudioHasher windows and, in Phase 7, the
// host audio device. The device clock is the RATE master of playback only --
// it never enters this domain: NAX/ENVX/ENDX readbacks and stream pacing are
// pure functions of the boundary count.
#include <cstdint>
#include <vector>

namespace ps2r::sound {

class SpuCore;
class VoiceSource;
class PcmSource;
class MixSource;

class Mixer {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kFramesPerVsync = kSampleRate / 60;   // 800

    explicit Mixer(SpuCore& core);
    explicit Mixer(MixSource& source);

    // One boundary's worth of samples (kFramesPerVsync frames) into Output().
    void Pump();
    // Pump an explicit frame count (tests; a PAL title would pump 960).
    void Pump(int frames);
    // Install before pumping; the execution owner keeps this provider alive.
    // Both owned and remaining channels share the existing gain/clamp order.
    void SetVoiceSource(VoiceSource* source) { voices_ = source; }
    void SetPcmSource(PcmSource* source) { pcm_ = source; }
    void SetMixSource(MixSource* source) { source_ = source; }

    const float* Output() const { return out_.data(); }
    int OutputFrames() const { return out_frames_; }
    uint64_t Pumps() const { return pumps_; }
    uint64_t FramesMixed() const { return frames_mixed_; }

private:
    SpuCore* core_ = nullptr;
    MixSource* source_ = nullptr;
    VoiceSource* voices_ = nullptr;
    PcmSource* pcm_ = nullptr;
    std::vector<float> out_;
    int out_frames_ = 0;
    uint64_t pumps_ = 0;
    uint64_t frames_mixed_ = 0;
};

}  // namespace ps2r::sound
