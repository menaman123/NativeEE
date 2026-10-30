#pragma once
// TimeStretch: a pitch-preserving tempo change (WSOLA) for interleaved stereo float frames. The play
// host runs the mixer's boundary quanta through it on their way to the audio device: when the game
// thread delivers fewer than 60 boundaries a second, each quantum is lengthened to the device's rate
// instead of the device repeating old audio. Every frame the mixer produced is heard once and in
// order; only its duration changes.
//
//   Process(in, frames, ratio, out) appends about frames * ratio frames to `out`, where ratio is
//   output frames per input frame, clamped to [kMinRatio, kMaxRatio]. The ratio may change on every
//   call. At ratio 1 the output is the input delayed (two Hann windows at 50% overlap sum to one).
//
// Every kHop output frames, the next kWindow-frame input segment is taken near its nominal position
// (advancing kHop / ratio per hop) at the offset within +-kSeek whose first kHop frames best match
// the natural continuation of the previous segment, and is overlap-added under a Hann window. The
// match is a normalised cross-correlation over both channels, searched on every 4th offset and
// sample, then refined at full rate around the best coarse offset.
#include <cstdint>
#include <vector>

namespace ps2r::sound {

class TimeStretch {
public:
    static constexpr int kChannels = 2;
    static constexpr int kWindow = 960;         // 20 ms at 48 kHz
    static constexpr int kHop = kWindow / 2;
    static constexpr int kSeek = 240;           // 5 ms either side of the nominal position
    static constexpr double kMinRatio = 0.5;
    static constexpr double kMaxRatio = 2.0;

    TimeStretch();

    void Process(const float* interleaved, int frames, double ratio, std::vector<float>& out);
    // Drops buffered input and the overlap tail; the next Process starts a new stream.
    void Reset();

    uint64_t FramesIn() const { return frames_in_; }
    uint64_t FramesOut() const { return frames_out_; }

private:
    // The input frame index (absolute) where the next segment starts.
    int64_t PickStart(int64_t nominal) const;
    // Normalised correlation of both channels at `start` against the continuation template, using
    // every `step`-th frame of the first kHop.
    double Match(int64_t start, int step) const;
    int64_t BufferedEnd() const;

    std::vector<float> in_;        // pending input; in_[0] is absolute frame base_
    int64_t base_ = 0;
    double pos_ = 0.0;             // nominal start of the next segment
    int64_t prev_ = -1;            // actual start of the previous segment (-1: none yet)
    std::vector<float> ola_;       // kWindow frames of overlap-add accumulator
    std::vector<float> window_;    // periodic Hann, kWindow
    uint64_t frames_in_ = 0;
    uint64_t frames_out_ = 0;
};

}  // namespace ps2r::sound
