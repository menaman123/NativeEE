// gs/hle/ir/counting_draw_ir_sink.cpp -- CountingDrawIrSink (see the header).
#include "engine/gs/hle/ir/counting_draw_ir_sink.h"

#include <utility>

#include "engine/gs/hle/ir/gsdl_format.h"

namespace ps2r {
namespace gs {

namespace {
constexpr uint64_t kFnv1a64Prime = 0x100000001b3ull;

// FNV-1a 64 continued from `seed` over `n` bytes.
uint64_t Chain(uint64_t seed, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    uint64_t h = seed;
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= kFnv1a64Prime;
    }
    return h;
}
}  // namespace

void CountingDrawIrSink::OnDraw(const DrawIr& draw) {
    ++draws_;
    const DrawRec rec = draw.ToDrawRec();
    draw_hash_ = Chain(draw_hash_, &rec, sizeof(rec));
    if (next_) next_->OnDraw(draw);
}

void CountingDrawIrSink::OnDrawOwned(DrawIr&& draw) {
    ++draws_;
    const DrawRec rec = draw.ToDrawRec();
    draw_hash_ = Chain(draw_hash_, &rec, sizeof(rec));
    if (next_) next_->OnDrawOwned(std::move(draw));
}

void CountingDrawIrSink::OnJob(const JobRec& job) {
    ++jobs_;
    job_hash_ = Chain(job_hash_, &job, sizeof(job));
    if (next_) next_->OnJob(job);
}

void CountingDrawIrSink::OnFrame(const JobRec& vsync) {
    ++frames_;
    job_hash_ = Chain(job_hash_, &vsync, sizeof(vsync));
    if (next_) next_->OnFrame(vsync);
}

}  // namespace gs
}  // namespace ps2r
