// gs/hle/ir/drawlist_ring.h -- DrawlistRing: a DrawIrSink decorator that keeps
// the LAST frames of the DrawIR stream in a FIXED-SIZE ring (compact per-draw
// records + per-frame summaries) and forwards every call to the next sink.
// Compiled into every build: it is the crash path's drawlist -- the triage
// bundle writes Dump() as drawlist_last200.txt when a leg parks on a fault.
//
// Bounded by construction (BUG-083: a retaining recorder grew ~8-11 MB per title
// frame): kFrames frame summaries and kDraws draw records are allocated once in
// the constructor and overwritten circularly; a frame that pushes more draws
// than the record ring holds keeps its newest ones and counts the rest as
// dropped. No allocation after construction, no per-call growth.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "engine/gs/hle/ir/draw_ir_sink.h"

namespace ps2r {
namespace gs {

class DrawlistRing : public DrawIrSink {
public:
    static constexpr uint32_t kFrames = 200;    // frames the dump covers
    static constexpr uint32_t kDraws = 32768;   // draw records retained (~2 MB)

    // One draw, as the dump prints it (a fixed subset of DrawIr).
    struct DrawRecord {
        uint32_t seq = 0, src_frame = 0, frame_inst = 0, handler = 0;
        uint16_t kick_pc = 0, kick_ord = 0;
        uint8_t prim_type = 0, ctx = 0, frame_psm = 0, tex_psm = 0;
        bool tex_enabled = false, alpha_blend = false;
        uint32_t vertex_count = 0, fbp = 0, fbw = 0, zbp = 0, tbp0 = 0;
        float bbox[6] = {0, 0, 0, 0, 0, 0};
    };
    struct FrameSummary {
        uint32_t src_frame = 0;   // the frame the draws were stamped with
        uint32_t draws = 0, jobs = 0, dropped = 0;
        uint32_t vsync_seq = 0;   // the closing VSync job's seq (0 = still open)
    };
    // handler id -> program name (nullptr when unknown); may be empty.
    using NameResolver = std::function<const char*(uint32_t)>;

    explicit DrawlistRing(DrawIrSink* next, NameResolver names = nullptr);

    void SetNext(DrawIrSink* next) { next_ = next; }

    void OnDraw(const DrawIr& draw) override;
    void OnJob(const JobRec& job) override;
    void OnFrame(const JobRec& vsync) override;

    uint64_t Draws() const;
    uint64_t Frames() const;
    // The most recent draw (the faulting frame's last draw when a fault parks the
    // leg); false when nothing was drawn yet.
    bool Last(DrawRecord* out) const;
    // One line per frame summary and per retained draw of the last kFrames
    // frames, newest frame last: the triage bundle's drawlist_last200.txt.
    std::string Dump() const;
    // "draw seq=.. frame=.. handler=0x..[(name)] kick_pc=0x.. prim=.. verts=.. fb=0x../.. z=0x.. tex=0x../.. bbox=[..]"
    std::string Describe(const DrawRecord& d) const;

private:
    DrawIrSink* next_;
    NameResolver names_;
    mutable std::mutex mu_;
    std::vector<DrawRecord> draws_;      // kDraws, circular
    std::vector<FrameSummary> frames_;   // kFrames, circular
    uint64_t draw_total_ = 0;            // draws ever pushed (ring head = total % kDraws)
    uint64_t frame_total_ = 0;           // frames ever closed
    FrameSummary open_;                  // the frame being pushed into
    uint32_t open_first_draw_ = 0;       // draw_total_ at the open frame's first draw
};

}  // namespace gs
}  // namespace ps2r
