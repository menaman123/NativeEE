// gs/hle/ir/draw_ir_verifier.cpp -- the two canonicalising interpreters.
#include "engine/gs/hle/ir/draw_ir_verifier.h"

#include <cstring>

namespace ps2r {
namespace gs {

using AttrState = DrawIrVerifier::AttrState;
using Event = DrawIrVerifier::Event;

uint32_t DrawIrVerifier::FloatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

bool AttrState::operator==(const AttrState& o) const {
    if (k_rgba != o.k_rgba || k_st != o.k_st || k_q != o.k_q || k_uv != o.k_uv ||
        k_uvw != o.k_uvw || k_fogf != o.k_fogf)
        return false;
    if (k_rgba && (r != o.r || g != o.g || b != o.b || a != o.a)) return false;
    if (k_st && (DrawIrVerifier::FloatBits(s) != DrawIrVerifier::FloatBits(o.s) ||
                 DrawIrVerifier::FloatBits(t) != DrawIrVerifier::FloatBits(o.t)))
        return false;
    if (k_q && DrawIrVerifier::FloatBits(q) != DrawIrVerifier::FloatBits(o.q)) return false;
    if (k_uv && (u != o.u || v != o.v)) return false;
    if (k_uvw && uvw != o.uvw) return false;
    if (k_fogf && fogf != o.fogf) return false;
    return true;
}

bool Event::operator==(const Event& o) const {
    if (t != o.t) return false;
    switch (t) {
        case Kick:
            return xb == o.xb && yb == o.yb && zb == o.zb && f == o.f && draw == o.draw && at == o.at;
        case State:
            return reg == o.reg && value == o.value;
        case Prim:
            return prim == o.prim;
        case Flush:
            return true;
        case SrcId:
            return id_lo == o.id_lo && id_hi == o.id_hi;
        case SrcName:
            return id_lo == o.id_lo;
        case Final:
            return at == o.at;
    }
    return false;
}

namespace {

Event KickEvent(uint64_t lo, uint64_t hi, bool xyzf, bool draws_when_clear, const AttrState& st) {
    Event e;
    e.t = Event::Kick;
    const float x = static_cast<float>(static_cast<uint16_t>(lo & 0xFFFF)) / 16.0f;
    const float y = static_cast<float>(static_cast<uint16_t>((lo >> 32) & 0xFFFF)) / 16.0f;
    const uint32_t zu = xyzf ? static_cast<uint32_t>((hi >> 4) & 0xFFFFFF)
                             : static_cast<uint32_t>(hi & 0xFFFFFFFF);
    const bool adc = (hi >> 47) & 1;
    e.xb = DrawIrVerifier::FloatBits(x);
    e.yb = DrawIrVerifier::FloatBits(y);
    e.zb = DrawIrVerifier::FloatBits(static_cast<float>(zu));
    e.f = xyzf ? static_cast<uint8_t>((hi >> 36) & 0xFF) : 0;
    e.draw = draws_when_clear && !adc;
    e.at = st;
    return e;
}

void OpsPackedAttr(AttrState& st, const DirectOps::Op& op) {
    switch (static_cast<uint8_t>(op.reg)) {
        case 0x01:
            st.r = op.lo & 0xFF;
            st.g = (op.lo >> 32) & 0xFF;
            st.b = op.hi & 0xFF;
            st.a = (op.hi >> 32) & 0xFF;
            st.k_rgba = true;
            break;
        case 0x02: {
            float s2, t2, q2;
            std::memcpy(&s2, &op.lo, 4);
            std::memcpy(&t2, reinterpret_cast<const uint8_t*>(&op.lo) + 4, 4);
            std::memcpy(&q2, &op.hi, 4);
            st.s = s2;
            st.t = t2;
            st.k_st = true;
            if (q2 != 0.0f) {
                st.q = q2;
                st.k_q = true;
            }
            st.uvw = false;
            st.k_uvw = true;
            break;
        }
        case 0x03:
            st.u = op.lo & 0x3FFF;
            st.v = (op.lo >> 32) & 0x3FFF;
            st.k_uv = true;
            st.uvw = true;
            st.k_uvw = true;
            break;
        case 0x0A:
            st.fogf = ((op.hi >> 36) & 0xFF) != 0;
            st.k_fogf = true;
            break;
        default:
            break;
    }
}

void ApplyAttrs(AttrState& st, const DrawIrLift::Vert& v) {
    const uint8_t m = v.mask;
    if (m & DrawIrLift::kMaskST) { st.s = v.s; st.t = v.t; st.k_st = true; }
    if (m & DrawIrLift::kMaskQ) { st.q = v.q; st.k_q = true; }
    if (m & DrawIrLift::kMaskUV) { st.u = v.u; st.v = v.v; st.k_uv = true; }
    if (m & DrawIrLift::kMaskUVW) {
        st.uvw = (v.flags & DrawIrLift::kFlagUVWVal) != 0;
        st.k_uvw = true;
    }
    if (m & DrawIrLift::kMaskRGBA) {
        st.r = v.r; st.g = v.g; st.b = v.b; st.a = v.a;
        st.k_rgba = true;
    }
    if (m & DrawIrLift::kMaskFOGF) {
        st.fogf = (v.flags & DrawIrLift::kFlagFOGFVal) != 0;
        st.k_fogf = true;
    }
}

}  // namespace

std::vector<Event> DrawIrVerifier::RunOps(const std::vector<DirectOps::Op>& ops) {
    std::vector<Event> ev;
    AttrState st;
    for (const auto& op : ops) {
        Event e;
        if (op.reg == DirectOps::kOpFlush) {
            e.t = Event::Flush;
            ev.push_back(e);
            continue;
        }
        if (op.reg == DirectOps::kOpSrcId) {
            e.t = Event::SrcId; e.id_lo = op.lo; e.id_hi = op.hi;
            ev.push_back(e);
            continue;
        }
        if (op.reg == DirectOps::kOpSrcName) {
            e.t = Event::SrcName; e.id_lo = op.lo;
            ev.push_back(e);
            continue;
        }
        if (op.reg & DirectOps::kModeReglist) {
            e.t = Event::State; e.reg = static_cast<uint16_t>(op.reg & 0xFF); e.value = op.lo;
            ev.push_back(e);
            continue;
        }
        switch (static_cast<uint8_t>(op.reg)) {
            case 0x00:
                e.t = Event::Prim; e.prim = op.lo & 0x7FF;
                ev.push_back(e);
                break;
            case 0x01: case 0x02: case 0x03: case 0x0A:
                OpsPackedAttr(st, op);
                break;
            case 0x04: case 0x0C:
                ev.push_back(KickEvent(op.lo, op.hi, true, static_cast<uint8_t>(op.reg) == 0x04, st));
                break;
            case 0x05: case 0x0D:
                ev.push_back(KickEvent(op.lo, op.hi, false, static_cast<uint8_t>(op.reg) == 0x05, st));
                break;
            default:
                e.t = Event::State; e.reg = op.reg; e.value = op.lo;
                ev.push_back(e);
                break;
        }
    }
    Event fin;
    fin.t = Event::Final;
    fin.at = st;
    ev.push_back(fin);
    return ev;
}

std::vector<Event> DrawIrVerifier::RunIR(const DrawIrLift::Stream& s) {
    std::vector<Event> ev;
    AttrState st;
    for (const auto& it : s.items) {
        Event e;
        switch (it.kind) {
            case DrawIrLift::kDrawRun:
                for (uint32_t i = 0; i < it.count; i++) {
                    const DrawIrLift::Vert& v = s.verts[it.first + i];
                    ApplyAttrs(st, v);
                    Event k;
                    k.t = Event::Kick;
                    k.xb = FloatBits(v.x); k.yb = FloatBits(v.y); k.zb = FloatBits(v.z);
                    k.f = v.f;
                    k.draw = (v.flags & DrawIrLift::kFlagDraw) != 0;
                    k.at = st;
                    ev.push_back(k);
                }
                break;
            case DrawIrLift::kAttr:
                ApplyAttrs(st, s.verts[it.first]);
                break;
            case DrawIrLift::kPrim:
                e.t = Event::Prim; e.prim = it.lo & 0x7FF;
                ev.push_back(e);
                break;
            case DrawIrLift::kState:
                e.t = Event::State; e.reg = it.reg; e.value = it.lo;
                ev.push_back(e);
                break;
            case DrawIrLift::kFlush:
                e.t = Event::Flush;
                ev.push_back(e);
                break;
            case DrawIrLift::kSrcId:
                e.t = Event::SrcId; e.id_lo = it.lo; e.id_hi = it.hi;
                ev.push_back(e);
                break;
            case DrawIrLift::kSrcName:
                e.t = Event::SrcName; e.id_lo = it.lo;
                ev.push_back(e);
                break;
            default:
                break;
        }
    }
    Event fin;
    fin.t = Event::Final;
    fin.at = st;
    ev.push_back(fin);
    return ev;
}

int DrawIrVerifier::VerifyKickEquivalence(const uint8_t* p, size_t n) {
    std::vector<DirectOps::Op> ops;
    const bool ok_ops = DirectOps::PacketToOps(p, n, ops);
    DrawIrLift::Stream ir;
    const bool ok_ir = DrawIrLift::PacketToDrawIR(p, n, ir);
    if (ok_ops != ok_ir) return -1;
    if (!ok_ops) return 0;
    const std::vector<Event> a = RunOps(ops);
    const std::vector<Event> b = RunIR(ir);
    if (a.size() != b.size()) return -1;
    for (size_t i = 0; i < a.size(); i++)
        if (!(a[i] == b[i])) return -1;
    return 1;
}

}  // namespace gs
}  // namespace ps2r
