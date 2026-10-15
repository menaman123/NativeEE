// gs/state/vertex_kick.cpp -- VertexKick (see vertex_kick.h).
#include "engine/gs/state/vertex_kick.h"

#include <utility>

namespace ps2r {
namespace gs {

namespace {
enum PrimType : uint8_t { kPoint = 0, kLine = 1, kLineStrip = 2, kTriangle = 3, kTriangleStrip = 4, kTriangleFan = 5, kSprite = 6 };
}  // namespace

void VertexKick::WriteXyzf(bool kick_reg, uint64_t v) {
    const float x = static_cast<float>(v & 0xFFFF) / 16.0f;
    const float y = static_cast<float>((v >> 16) & 0xFFFF) / 16.0f;
    const uint32_t z = static_cast<uint32_t>((v >> 32) & 0xFFFFFF);
    const uint8_t f = static_cast<uint8_t>((v >> 56) & 0xFF);
    Kick(x, y, static_cast<float>(z), f, kick_reg);
}

void VertexKick::WriteXyz(bool kick_reg, uint64_t v) {
    const float x = static_cast<float>(v & 0xFFFF) / 16.0f;
    const float y = static_cast<float>((v >> 16) & 0xFFFF) / 16.0f;
    const uint32_t z = static_cast<uint32_t>(v >> 32);
    Kick(x, y, static_cast<float>(z), 0, kick_reg);
}

void VertexKick::PackedXyzf(bool kick_reg, uint64_t lo, uint64_t hi) {
    const float x = static_cast<float>(static_cast<uint16_t>(lo & 0xFFFF)) / 16.0f;
    const float y = static_cast<float>(static_cast<uint16_t>((lo >> 32) & 0xFFFF)) / 16.0f;
    const uint32_t z = static_cast<uint32_t>((hi >> 4) & 0xFFFFFF);
    const uint8_t f = static_cast<uint8_t>((hi >> 36) & 0xFF);
    const bool adc = ((hi >> 47) & 1) != 0;
    Kick(x, y, static_cast<float>(z), f, kick_reg && !adc);
}

void VertexKick::PackedXyz(bool kick_reg, uint64_t lo, uint64_t hi) {
    const float x = static_cast<float>(static_cast<uint16_t>(lo & 0xFFFF)) / 16.0f;
    const float y = static_cast<float>(static_cast<uint16_t>((lo >> 32) & 0xFFFF)) / 16.0f;
    const uint32_t z = static_cast<uint32_t>(hi & 0xFFFFFFFF);
    const bool adc = ((hi >> 47) & 1) != 0;
    Kick(x, y, static_cast<float>(z), 0, kick_reg && !adc);
}

void VertexKick::OnPrimWritten() {
    vertex_queue_.clear();
    strip_count_ = 0;
}

void VertexKick::Kick(float x, float y, float z, uint8_t f, bool draw) {
    GsRegisters& r = *regs_;
    if (r.dirty_regs && !draw_buffer_.empty() && r.TestDrawChanged() && flush_) flush_();
    if (draw_buffer_.empty()) batch_texture_enable_ = r.prim.texture;  // the draw's TME
    if (r.dirty_regs) r.SnapshotState();
    const GsRegisters::Attr& a = r.attr;
    VertexRec v{};
    v.uv_u = a.u;
    v.uv_v = a.v;
    if (a.uv_written_since_st) {
        v.s = a.u / 16.0f;
        v.t = a.v / 16.0f;
        v.q = 1.0f;
    } else {
        v.s = a.s;
        v.t = a.t;
        v.q = a.q;
    }
    v.r = a.r; v.g = a.g; v.b = a.b; v.a = a.a;
    v.fog = f;
    v.z = z;
    const GsRegisters::XyOffset& off = r.ctx[r.prim.context & 1].xyoffset;
    v.x = x - off.x;
    v.y = y - off.y;
    v.draw_kick = draw ? 1 : 0;
    Assemble(v, draw);
}

int VertexKick::Needed(uint8_t prim) {
    switch (prim) {
        case kPoint: return 1;
        case kLine: case kLineStrip: case kSprite: return 2;
        default: return 3;
    }
}

void VertexKick::AssembleStrip(bool draw) {
    while (vertex_queue_.size() > 3) vertex_queue_.erase(vertex_queue_.begin());
    if (!draw) {
        strip_count_ = 0;  // strip restart; the vertex stays in the window
        return;
    }
    if (vertex_queue_.size() != 3) return;
    const VertexRec& q0 = vertex_queue_[0];
    const VertexRec& q1 = vertex_queue_[1];
    const VertexRec& q2 = vertex_queue_[2];
    const bool degen = (q0.x == q1.x && q0.y == q1.y) || (q1.x == q2.x && q1.y == q2.y) ||
                       (q0.x == q2.x && q0.y == q2.y);
    if (degen) {
        strip_count_++;
        return;
    }
    if (strip_count_ % 2 == 0) {
        draw_buffer_.push_back(q0);
        draw_buffer_.push_back(q1);
        draw_buffer_.push_back(q2);
    } else {
        draw_buffer_.push_back(q1);
        draw_buffer_.push_back(q0);
        draw_buffer_.push_back(q2);
    }
    strip_count_++;
}

void VertexKick::Assemble(const VertexRec& v, bool draw) {
    vertex_queue_.push_back(v);
    const uint8_t prim = regs_->prim.type;
    if (prim == kTriangleStrip) {
        AssembleStrip(draw);
        return;
    }
    const int needed = Needed(prim);
    if (draw) {
        if (static_cast<int>(vertex_queue_.size()) >= needed) FlushPrimitive();
    } else {
        strip_count_ = 0;
        if (static_cast<int>(vertex_queue_.size()) >= needed) {
            // ADC consume: the primitive completes without drawing.
            switch (prim) {
                case kLineStrip: vertex_queue_.erase(vertex_queue_.begin()); break;
                case kTriangleFan: vertex_queue_.erase(vertex_queue_.begin() + 1); break;
                default: vertex_queue_.clear(); break;
            }
        }
    }
    if (vertex_queue_.size() > 3) vertex_queue_.erase(vertex_queue_.begin());
}

void VertexKick::FlushPrimitive() {
    switch (regs_->prim.type) {
        case kPoint:
            draw_buffer_.push_back(vertex_queue_.back());
            vertex_queue_.clear();
            break;
        case kLine:
            draw_buffer_.push_back(vertex_queue_[0]);
            draw_buffer_.push_back(vertex_queue_[1]);
            vertex_queue_.clear();
            break;
        case kLineStrip:
            draw_buffer_.push_back(vertex_queue_[0]);
            draw_buffer_.push_back(vertex_queue_[1]);
            vertex_queue_.erase(vertex_queue_.begin());
            break;
        case kTriangle:
            draw_buffer_.push_back(vertex_queue_[0]);
            draw_buffer_.push_back(vertex_queue_[1]);
            draw_buffer_.push_back(vertex_queue_[2]);
            vertex_queue_.clear();
            break;
        case kTriangleFan:
            draw_buffer_.push_back(vertex_queue_[0]);
            draw_buffer_.push_back(vertex_queue_[1]);
            draw_buffer_.push_back(vertex_queue_[2]);
            vertex_queue_.erase(vertex_queue_.begin() + 1);
            break;
        case kSprite:
            EmitSprite();
            break;
        default:
            break;
    }
}

// Sprite (3.2.6): the two kicked vertices are opposite corners; the GS fills the
// axis-aligned rectangle with the second vertex's z, fog and (flat) colour.
void VertexKick::EmitSprite() {
    VertexRec v0 = vertex_queue_[0];
    VertexRec v1 = vertex_queue_[1];
    if (quirks_.round_sprite_mode == 1) {
        if (v0.x > v1.x) {
            std::swap(v0.x, v1.x);
            std::swap(v0.s, v1.s);
            std::swap(v0.uv_u, v1.uv_u);
        }
        if (v0.y > v1.y) {
            std::swap(v0.y, v1.y);
            std::swap(v0.t, v1.t);
            std::swap(v0.uv_v, v1.uv_v);
        }
    }
    const float sz = v1.z;
    const uint8_t sf = v1.fog;
    const uint8_t cr = v1.r, cg = v1.g, cb = v1.b, ca = v1.a;
    auto flat = [&](VertexRec& v) { v.z = sz; v.fog = sf; v.r = cr; v.g = cg; v.b = cb; v.a = ca; };
    VertexRec tl = v0;
    flat(tl);
    VertexRec tr = v0;
    tr.x = v1.x; tr.s = v1.s; tr.uv_u = v1.uv_u;
    flat(tr);
    VertexRec bl = v1;
    bl.x = v0.x; bl.s = v0.s; bl.uv_u = v0.uv_u;
    flat(bl);
    VertexRec br = v1;
    flat(br);
    draw_buffer_.push_back(tl);
    draw_buffer_.push_back(tr);
    draw_buffer_.push_back(bl);
    draw_buffer_.push_back(tr);
    draw_buffer_.push_back(br);
    draw_buffer_.push_back(bl);
    vertex_queue_.clear();
}

}  // namespace gs
}  // namespace ps2r
