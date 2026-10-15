#pragma once
// FmvPcmInput: the mixer's FMV PCM source -- the sound module's FmvPcmSink.
// A movie's program stream carries raw PCM16LE 48 kHz stereo (the SPU2
// output rate) in its private_stream_1 packets; the sceMpeg HLE pushes those
// frames here and the SPU2 core pops them chunk by chunk from MixInto, adding
// them AFTER the voice mix at the core-0 BVOLL/BVOLR gain (the title programs
// 0x7FFF into both at FMV start) and BEFORE the master/mute chain.
//
// Rules (the OLD sound core's FMV FIFO, kept for equivalence):
//   capacity    1 << 22 stereo frames (87.4 s): the title demuxes small movies
//               WHOLE in one load loop before the first picture serves, so the
//               queue must hold the largest PSS audio body outright (the
//               disc's largest = 3,948,032 frames);
//   overflow    drop-NEWEST + counter (a push that does not fit is truncated;
//               queued frames are never overwritten);
//   gate        consumption is enabled by GateOpen (the first served picture
//               once audio was pushed, so A/V start together) and disabled by
//               Flush; while the gate is closed Pop yields nothing and the
//               frames stay queued;
//   flush       closes the gate and discards everything queued (a skipped
//               movie's sound stops within one mix chunk).
// Single-threaded: the producer (the sceMpeg rows on the EE thread) and the
// consumer (the mixer pump at the boundary's Verify step) are the same thread.
#include <cstdint>
#include <memory>

#include "engine/ipu/hle/fmv_pcm_sink.h"

namespace ps2r::sound {

class FmvPcmInput : public ps2r::ipu::FmvPcmSink {
public:
    static constexpr uint32_t kDefaultCapacityFrames = 1u << 22;

    // `capacity_frames` must be a power of two (tests shrink it).
    explicit FmvPcmInput(uint32_t capacity_frames = kDefaultCapacityFrames);

    void Push(const int16_t* stereo, uint32_t frames) override;
    void GateOpen() override;
    void Flush() override;

    // Consumer side (MixInto): up to `want` interleaved stereo frames into
    // `out`; returns the count popped (0 while the gate is closed or the queue
    // is empty). A partial pop leaves the rest queued.
    int Pop(int16_t* out, int want);

    bool GateIsOpen() const { return gate_; }
    uint32_t Queued() const { return static_cast<uint32_t>(tail_ - head_); }
    uint32_t Capacity() const { return capacity_; }
    uint64_t Pushed() const { return pushed_; }
    // Pushed frames with a non-zero sample (the census tells silence in the
    // movie's track from a zero gain).
    uint64_t PushedNonZero() const { return pushed_nonzero_; }
    uint64_t Consumed() const { return consumed_; }
    uint64_t Dropped() const { return dropped_; }
    uint64_t GateOpens() const { return gate_opens_; }
    uint64_t Flushes() const { return flushes_; }

private:
    std::unique_ptr<int16_t[]> buf_;   // capacity_ * 2 samples
    uint32_t capacity_;
    uint32_t mask_;
    uint64_t head_ = 0;   // consumer cursor (frames, monotonic)
    uint64_t tail_ = 0;   // producer cursor (frames, monotonic)
    bool gate_ = false;
    uint64_t pushed_ = 0;
    uint64_t pushed_nonzero_ = 0;
    uint64_t consumed_ = 0;
    uint64_t dropped_ = 0;
    uint64_t gate_opens_ = 0;
    uint64_t flushes_ = 0;
};

}  // namespace ps2r::sound
