// gs/state/vertex_kick.h -- VertexKick: the GS vertex queue and primitive
// assembly (GS User's Manual 3.2 "drawing primitives", 7.3.1.5-8 XYZ2/XYZ3/
// XYZF2/XYZF3).
//
// A kick (XYZ*/XYZF* write) assembles one vertex from the live attribute
// registers -- UV or STQ by which was written last (UV yields s=u/16, t=v/16,
// q=1), RGBAQ, the per-kick fog byte, z, and x/y in pixels minus the context's
// XYOFFSET -- and pushes it into the vertex queue. XYZ2/XYZF2 with ADC clear draw;
// XYZ3/XYZF3 or ADC set only queue (the strip / fan restart rule).
//
// Assembly per PRIM type over a 3-slot queue: Point 1, Line 2, LineStrip 2 (keep
// the last), Triangle 3, TriangleStrip 3 with even/odd parity swap of the first
// two vertices (degenerate triangles -- two equal xy -- advance parity without
// output; a non-drawing kick resets parity), TriangleFan 3 (keep first and last),
// Sprite 2 expanded to two triangles (tl tr bl, tr br bl) after corner
// normalisation (x/s/u and y/t/v of the two corners ordered min,max) with z, fog
// and colour taken flat from the second vertex. PRIM.IIP = 0 (flat shading, 3.2.1
// / 3.4.1): the RGBA of the vertex whose kick completes the primitive (the last
// one -- PCSX2's "provoking last vertex") is applied to the whole line / triangle;
// IIP = 1 keeps every vertex's own colour (Gouraud, interpolated downstream). The corners are NOT snapped to
// pixel centres (VertexKickQuirks::round_sprite_mode 1 = normalise only, the peak
// configuration; 0 = leave the corners as kicked).
//
// The first kick of a draw (empty draw buffer) latches PRIM.TME as the draw's
// texture-enable; a kick with dirty registers that change the draw state flushes
// the queued draw first (through the flush owner), then snapshots the state.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "engine/gs/hle/ir/vertex_rec.h"
#include "engine/gs/state/gs_registers.h"

namespace ps2r {
namespace gs {

struct VertexKickQuirks {
    int round_sprite_mode = 1;  // 1: order the sprite corners (peak); 0: as kicked
};

class VertexKick {
public:
    explicit VertexKick(GsRegisters* regs) : regs_(regs) {}

    void SetQuirks(const VertexKickQuirks& q) { quirks_ = q; }
    // The flush owner: called before a kick when the dirty registers changed the draw.
    void SetFlushHook(std::function<void()> hook) { flush_ = std::move(hook); }

    // Pre-decoded kick: x/y in pixels (12.4 / 16), z = float(zu), f = fog byte.
    void Kick(float x, float y, float z, uint8_t f, bool draw);

    // Register forms. 64-bit: X 0-15 Y 16-31 Z 32-55 F 56-63 (XYZF) / Z 32-63 (XYZ);
    // no ADC bit -- XYZ2/XYZF2 always draw. PACKED: X 0-15 Y 32-47, Z 68-91 F 100-107
    // ADC 111 (XYZF) / Z 64-95 ADC 111 (XYZ).
    void WriteXyzf(bool kick_reg, uint64_t v);   // 0x04 XYZF2 (true) / 0x0C XYZF3 (false)
    void WriteXyz(bool kick_reg, uint64_t v);    // 0x05 XYZ2 / 0x0D XYZ3
    void PackedXyzf(bool kick_reg, uint64_t lo, uint64_t hi);
    void PackedXyz(bool kick_reg, uint64_t lo, uint64_t hi);

    // Every PRIM write restarts the queue and the strip parity.
    void OnPrimWritten();
    void ResetStrip() { strip_count_ = 0; }

    bool HasPending() const { return !draw_buffer_.empty(); }
    std::vector<VertexRec>& DrawBuffer() { return draw_buffer_; }
    // The vertex queue's contents, oldest first (a copy: the queue is a fixed window).
    const std::vector<VertexRec>& Queue() const {
        queue_view_.assign(queue_, queue_ + queue_n_);
        return queue_view_;
    }
    bool BatchTextureEnable() const { return batch_texture_enable_; }
    int StripCount() const { return strip_count_; }

private:
    void Assemble(const VertexRec& v, bool draw);
    void AssembleStrip(bool draw);
    void FlushPrimitive();
    void EmitSprite();
    // Push one assembled primitive; flat shading copies the last vertex's RGBA.
    void PushPrimitive(const VertexRec* v, int n);
    // A triangle of three queue slots in the given winding (the strip's parity swap).
    void PushTriangle(const VertexRec& a, const VertexRec& b, const VertexRec& c);
    static int Needed(uint8_t prim);
    // The 3-slot window (one more slot for the kick before the trim).
    void QueuePush(const VertexRec& v) {
        if (queue_n_ == kQueueSlots) QueueEraseFront();   // never reached: the trim keeps <= 3 between kicks
        queue_[queue_n_++] = v;
    }
    void QueueEraseFront();
    void QueueEraseAt(int i);
    void QueueClear() { queue_n_ = 0; }

    GsRegisters* regs_;
    VertexKickQuirks quirks_;
    std::function<void()> flush_;
    // The vertex queue: at most 3 vertices between kicks, 4 right after a kick (the
    // trim follows). A fixed window: the per-kick erase-front / push of a std::vector
    // was 16 % of the GIF drain thread's busy time in a strip-heavy hub (pipeline2).
    static constexpr int kQueueSlots = 4;
    VertexRec queue_[kQueueSlots];
    int queue_n_ = 0;
    mutable std::vector<VertexRec> queue_view_;   // Queue()'s copy
    std::vector<VertexRec> draw_buffer_;
    int strip_count_ = 0;
    bool batch_texture_enable_ = false;
};

}  // namespace gs
}  // namespace ps2r
