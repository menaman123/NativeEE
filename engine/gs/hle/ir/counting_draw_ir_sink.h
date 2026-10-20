// gs/hle/ir/counting_draw_ir_sink.h -- CountingDrawIrSink: a DrawIrSink that
// counts the draws, jobs and frames it receives and chains an FNV-1a 64 over
// their on-disk record bytes (DrawIr::ToDrawRec() for draws, JobRec for jobs and
// frames), then forwards each call to an optional next sink. It is both the
// stand-in consumer while no render queue exists (the hash is the DrawIR
// observable of a leg) and the decorator shape a recorder tap takes: it sees
// exactly what the sink behind it sees.
#pragma once

#include <cstdint>

#include "engine/gs/hle/ir/draw_ir_sink.h"

namespace ps2r {
namespace gs {

class CountingDrawIrSink : public DrawIrSink {
public:
    static constexpr uint64_t kHashOffset = 0xcbf29ce484222325ull;  // FNV-1a 64 offset basis

    explicit CountingDrawIrSink(DrawIrSink* next = nullptr) : next_(next) {}

    void SetNext(DrawIrSink* next) { next_ = next; }
    // The FNV chains (DrawHash / JobHash) are the DrawIR observable of a leg under
    // test; a play session that reads only Draws()/Frames() turns them off (the
    // DrawRec copy + a byte-wise FNV per draw, ~1 ms/frame on the drain thread).
    // On by default; the hashes stay at kHashOffset while off.
    void SetHashing(bool on) { hashing_ = on; }
    bool Hashing() const { return hashing_; }

    void OnDraw(const DrawIr& draw) override;
    void OnDrawOwned(DrawIr&& draw) override;
    void OnJob(const JobRec& job) override;
    void OnFrame(const JobRec& vsync) override;

    uint64_t Draws() const { return draws_; }
    uint64_t Jobs() const { return jobs_; }
    uint64_t Frames() const { return frames_; }
    // FNV-1a 64 chained over every DrawRec received (kHashOffset when none).
    uint64_t DrawHash() const { return draw_hash_; }
    // FNV-1a 64 chained over every JobRec received (jobs and frames, in order).
    uint64_t JobHash() const { return job_hash_; }

private:
    DrawIrSink* next_;
    bool hashing_ = true;
    uint64_t draws_ = 0;
    uint64_t jobs_ = 0;
    uint64_t frames_ = 0;
    uint64_t draw_hash_ = kHashOffset;
    uint64_t job_hash_ = kHashOffset;
};

}  // namespace gs
}  // namespace ps2r
