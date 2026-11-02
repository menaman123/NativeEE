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
// (advancing kHop / ratio per hop) at the offset within +-seek whose first kHop frames best match
// the natural continuation of the previous segment, and is overlap-added under a Hann window. The
// exact continuation scores 1 and is taken whenever it lies within the range, so the output is the
// input until the nominal position has drifted by the seek range; then the segment jumps to the best
// match near the nominal position (a splice: a repeat for ratio > 1, a skip below). The seek range
// bounds a splice's size. In practice a steady ratio rides the range's edge: the continuation sits just
// outside it and every hop takes the nearest reachable offset, a splice of kHop |1 - 1/ratio| frames
// (5 at 1.01, 20 at 1.035, 130 at 1.3) at a match of 0.95-0.99; a ratio that jumps by tens of percent for
// one quantum makes splices of hundreds of frames at a poorer match. Splices()/LargeSplices() count them. The
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
    static constexpr int kSeek = 240;           // 5 ms either side of the nominal position (the default)
    static constexpr double kMinRatio = 0.5;
    static constexpr double kMaxRatio = 2.0;

    // `seek`: the search range either side of the nominal position (frames, >= kCoarseStep).
    explicit TimeStretch(int seek = kSeek);

    void Process(const float* interleaved, int frames, double ratio, std::vector<float>& out);
    // Drops buffered input and the overlap tail; the next Process starts a new stream.
    void Reset();

    uint64_t FramesIn() const { return frames_in_; }
    uint64_t FramesOut() const { return frames_out_; }
    int Seek() const { return seek_; }
    // Census: segments that were not the exact continuation of the previous one (the splices), the sum of
    // their match scores (a normalised correlation, 1 = seamless) and of their offsets from the continuation.
    uint64_t Splices() const { return splices_; }
    double SpliceScoreSum() const { return splice_score_sum_; }
    uint64_t SpliceOffsetSum() const { return splice_offset_sum_; }
    uint64_t SpliceOffsetMax() const { return splice_offset_max_; }
    static constexpr int kLargeSplice = 96;     // 2 ms: a splice this long is a repeat / skip one can hear on speech
    uint64_t LargeSplices() const { return large_splices_; }

private:
    // The input frame index (absolute) where the next segment starts, and its match score.
    int64_t PickStart(int64_t nominal, double* score) const;
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
    int seek_ = kSeek;
    uint64_t splices_ = 0;
    double splice_score_sum_ = 0.0;
    uint64_t splice_offset_sum_ = 0, splice_offset_max_ = 0, large_splices_ = 0;
};

}  // namespace ps2r::sound
