// gs/hle/classrdr/target_dump.h -- TargetDump: a diagnostic readback of ONE render
// target's colour image (RGBA8 with the STORED alpha byte, the one plane no frame
// grab carries) around the draws of one frame that touch it.
//
// The frame grab is RGB and the local-memory mirror is synced lazily, so the alpha
// plane a PSMT8H index read or a DATE test consumes is otherwise unobservable. A
// composition arms the dump at a frame boundary (the lockstep boundary: the
// consumer is idle there) and DrawPipeline::Draw calls OnPreDraw / OnPostDraw for
// every draw until the next boundary disarms it. A draw qualifies when it is one
// of the requested frame-local ordinals, or -- with `classes` -- when it reads the
// target through a high-byte indexed PSM (PSMT8H / PSMT4HH / PSMT4HL) or a 16-bit
// colour PSM, draws into it with a 16-bit FRAME over its 32-bit content, or writes
// it under a partial FBMSK (neither open nor alpha-only). Each dump is one PAM (P7, RGB_ALPHA) file
// `rt_<fbp>_<ordinal>_<pre|post>.pam` plus one line in `rt_dump_index.txt`
// (ordinal, pipeline seq, the draw's FRAME / TEX / FBMSK), rows in PS2 order
// (the targets keep PS2 top at GL row 0). The dump decides nothing and is never
// consulted by the pipeline.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

struct DrawIr;
class RenderTargetCache;

class TargetDump {
public:
    struct Request {
        uint32_t fbp = 0;                 // the target's FRAME.FBP (bytes)
        std::string dir;                  // output directory (existing)
        uint32_t cap = 16;                // at most this many files
        bool classes = true;              // the qualifying-class rule above
        bool fbmsk_class = true;          // ... including the partial-FBMSK writers (often many)
        bool post = true;                 // also the image AFTER each qualifying draw
        std::vector<uint32_t> ordinals;   // frame-local draw ordinals (dump before)
    };

    void Arm(const Request& r);
    void Disarm();
    bool Armed() const { return armed_.load(std::memory_order_acquire); }
    // Frame boundary: the frame-local ordinal restarts.
    void OnFrameEnd() { ordinal_ = 0; }
    // Around one draw (the pipeline's single entry): the pre image when the draw
    // qualifies, the post image when it did and `post` is set.
    void OnPreDraw(const DrawIr& ir, RenderTargetCache& cache, render::Backend& be, uint64_t seq);
    void OnPostDraw(const DrawIr& ir, RenderTargetCache& cache, render::Backend& be, uint64_t seq);
    uint32_t Written() const { return written_; }

    // Pure: the qualifying-class rule (unit-testable).
    static bool QualifiesByClass(const DrawIr& ir, uint32_t fbp, bool fbmsk_class = true);
    static bool IsHighByteIndexed(uint8_t psm) { return psm == 0x1B || psm == 0x24 || psm == 0x2C; }
    static bool Is16BitColor(uint8_t psm) { return psm == 0x02 || psm == 0x0A; }

private:
    bool Qualifies(const DrawIr& ir) const;
    void Write(const DrawIr& ir, RenderTargetCache& cache, render::Backend& be, uint64_t seq, bool post);

    std::atomic<bool> armed_{false};
    Request req_;
    uint32_t ordinal_ = 0;
    uint32_t written_ = 0;
    bool pending_post_ = false;
    std::vector<uint8_t> scratch_;
};

}  // namespace gs
}  // namespace ps2r
