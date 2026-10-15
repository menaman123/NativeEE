// gs/hle/ir/draw_ir_lift.cpp -- DrawIrLift::PacketToDrawIR.
#include "engine/gs/hle/ir/draw_ir_lift.h"

#include <cstring>

namespace ps2r {
namespace gs {

namespace {

using Item = DrawIrLift::Item;
using Vert = DrawIrLift::Vert;
using Stream = DrawIrLift::Stream;

// Lift state: the output stream, the attribute shadow (replays the PACKED
// attribute stores abstractly and tracks which fields are pending since the last
// kick or boundary) and the open draw run.
struct LiftState {
    Stream& out;
    uint8_t r = 0, g = 0, b = 0, a = 0;
    float s = 0, t = 0, q = 0;
    uint16_t u = 0, v = 0;
    bool uvw = false, fogf = false;
    uint8_t gap = 0;                // pending kMask* stores
    size_t open_run = SIZE_MAX;     // index of the open kDrawRun item, or none
};

void OnRgbaq(LiftState& st, uint64_t lo, uint64_t hi) {  // RGBAQ packed: R[7:0] G[39:32] B[71:64] A[103:96]
    st.r = lo & 0xFF;
    st.g = (lo >> 32) & 0xFF;
    st.b = hi & 0xFF;
    st.a = (hi >> 32) & 0xFF;
    st.gap |= DrawIrLift::kMaskRGBA;
}

void OnSt(LiftState& st, uint64_t lo, uint64_t hi) {  // ST packed: S[31:0] T[63:32] Q[95:64]
    float ns, nt, nq;
    std::memcpy(&ns, &lo, 4);
    std::memcpy(&nt, reinterpret_cast<const uint8_t*>(&lo) + 4, 4);
    std::memcpy(&nq, &hi, 4);
    st.s = ns;
    st.t = nt;
    if (nq != 0.0f) {  // Q is stored only when the payload Q is not 0
        st.q = nq;
        st.gap |= DrawIrLift::kMaskQ;
    }
    st.uvw = false;
    st.gap |= DrawIrLift::kMaskST | DrawIrLift::kMaskUVW;
}

void OnUv(LiftState& st, uint64_t lo) {  // UV packed: U[13:0] V[45:32]
    st.u = lo & 0x3FFF;
    st.v = (lo >> 32) & 0x3FFF;
    st.uvw = true;
    st.gap |= DrawIrLift::kMaskUV | DrawIrLift::kMaskUVW;
}

void OnFog(LiftState& st, uint64_t hi) {  // FOG packed: F[107:100], stored as a bool
    st.fogf = ((hi >> 36) & 0xFF) != 0;
    st.gap |= DrawIrLift::kMaskFOGF;
}

// Snapshot the pending stores into a Vert's masked fields.
void Harvest(LiftState& st, Vert& o) {
    o.mask = st.gap;
    o.flags = static_cast<uint8_t>((st.uvw ? DrawIrLift::kFlagUVWVal : 0) |
                                   (st.fogf ? DrawIrLift::kFlagFOGFVal : 0));
    o.r = st.r; o.g = st.g; o.b = st.b; o.a = st.a;
    o.s = st.s; o.t = st.t; o.q = st.q;
    o.u = st.u; o.v = st.v;
    st.gap = 0;
}

// Pending attribute stores at a boundary become a kAttr item.
void FlushGap(LiftState& st) {
    if (!st.gap) return;
    Item it;
    it.kind = DrawIrLift::kAttr;
    it.first = static_cast<uint32_t>(st.out.verts.size());
    Vert v{};
    Harvest(st, v);
    st.out.verts.push_back(v);
    st.out.items.push_back(it);
}

// Close the run, flush pending stores, emit the item.
void Boundary(LiftState& st, const Item& it) {
    st.open_run = SIZE_MAX;
    FlushGap(st);
    st.out.items.push_back(it);
}

void StateItem(LiftState& st, uint16_t reg, uint64_t lo) {
    Item it;
    it.kind = DrawIrLift::kState;
    it.reg = reg;
    it.lo = lo;
    Boundary(st, it);
}

void KindItem(LiftState& st, uint16_t kind, uint64_t lo) {
    Item it;
    it.kind = kind;
    it.lo = lo;
    Boundary(st, it);
}

void PushVert(LiftState& st, uint64_t qlo, uint64_t qhi, uint8_t d) {
    Vert v{};
    v.x = static_cast<float>(static_cast<uint16_t>(qlo & 0xFFFF)) / 16.0f;
    v.y = static_cast<float>(static_cast<uint16_t>((qlo >> 32) & 0xFFFF)) / 16.0f;
    const bool xyzf = (d == 0x04) || (d == 0x0C);
    uint32_t zu;
    if (xyzf) {
        zu = static_cast<uint32_t>((qhi >> 4) & 0xFFFFFF);
        v.f = static_cast<uint8_t>((qhi >> 36) & 0xFF);
    } else {
        zu = static_cast<uint32_t>(qhi & 0xFFFFFFFF);
        v.f = 0;
    }
    const bool adc = (qhi >> 47) & 1;
    v.z = static_cast<float>(zu);
    const bool draw = ((d == 0x04) || (d == 0x05)) && !adc;
    Harvest(st, v);
    if (draw) v.flags |= DrawIrLift::kFlagDraw;
    if (st.open_run == SIZE_MAX) {
        Item it;
        it.kind = DrawIrLift::kDrawRun;
        it.first = static_cast<uint32_t>(st.out.verts.size());
        it.count = 0;
        st.open_run = st.out.items.size();
        st.out.items.push_back(it);
    }
    st.out.verts.push_back(v);
    st.out.items[st.open_run].count++;
}

void PackedItem(LiftState& st, uint8_t d, uint64_t qlo, uint64_t qhi) {
    switch (d) {
        case 0x0F:  // NOP
            break;
        case 0x0E:  // A+D: register address in bits 71:64
            StateItem(st, static_cast<uint16_t>(qhi & 0xFF), qlo);
            break;
        case 0x00:  // PRIM
            KindItem(st, DrawIrLift::kPrim, qlo);
            break;
        case 0x01: OnRgbaq(st, qlo, qhi); break;
        case 0x02: OnSt(st, qlo, qhi); break;
        case 0x03: OnUv(st, qlo); break;
        case 0x0A: OnFog(st, qhi); break;
        case 0x04: case 0x05: case 0x0C: case 0x0D:
            PushVert(st, qlo, qhi, d);
            break;
        default:  // TEX0_1/2, CLAMP_1/2, 0x0B: raw register write of the low 64 bits
            StateItem(st, d, qlo);
            break;
    }
}

bool PackedPayload(LiftState& st, const uint8_t* p, size_t n, size_t& off, uint32_t nloop, uint32_t nreg,
                   const uint8_t* regs) {
    const size_t need = static_cast<size_t>(nloop) * nreg * 16;
    if (off + need > n) return false;
    for (uint32_t l = 0; l < nloop; l++) {
        for (uint32_t rr = 0; rr < nreg; rr++, off += 16) {
            uint64_t qlo, qhi;
            std::memcpy(&qlo, p + off, 8);
            std::memcpy(&qhi, p + off + 8, 8);
            PackedItem(st, regs[rr], qlo, qhi);
        }
    }
    return true;
}

bool ReglistPayload(LiftState& st, const uint8_t* p, size_t n, size_t& off, uint32_t nloop, uint32_t nreg,
                    const uint8_t* regs) {
    const size_t total = static_cast<size_t>(nloop) * nreg;
    const size_t qws = (total + 1) / 2;
    if (off + qws * 16 > n) return false;
    for (size_t i = 0; i < total; i++, off += 8) {
        uint64_t v64;
        std::memcpy(&v64, p + off, 8);
        const uint8_t d = regs[i % nreg];
        if (d == 0x0E || d == 0x0F) continue;  // A+D / NOP have no meaning in REGLIST
        StateItem(st, d, v64);
    }
    if (total & 1) off += 8;  // REGLIST pads to a quadword
    return true;
}

}  // namespace

bool DrawIrLift::PacketToDrawIR(const uint8_t* p, size_t n, Stream& out) {
    LiftState st{out};
    size_t off = 0;
    while (off + 16 <= n) {
        uint64_t lo, hi;
        std::memcpy(&lo, p + off, 8);
        std::memcpy(&hi, p + off + 8, 8);
        // GIFtag: NLOOP[14:0] EOP[15] PRE[46] PRIM[57:47] FLG[59:58] NREG[63:60]
        const uint32_t nloop = static_cast<uint32_t>(lo & 0x7FFF);
        const bool eop = (lo >> 15) & 1;
        const bool pre = (lo >> 46) & 1;
        const uint32_t prim = static_cast<uint32_t>((lo >> 47) & 0x7FF);
        const uint32_t flg = static_cast<uint32_t>((lo >> 58) & 3);
        uint32_t nreg = static_cast<uint32_t>((lo >> 60) & 0xF);
        if (nreg == 0) nreg = 16;
        uint8_t regs[16];
        for (int i = 0; i < 16; i++) regs[i] = static_cast<uint8_t>((hi >> (i * 4)) & 0xF);
        off += 16;

        if (flg >= 2) return false;  // IMAGE / DISABLE: not draw content

        if (pre) KindItem(st, kPrim, prim);  // PRIM is written before the payload
        if (nloop == 0) {                    // every field but EOP is ignored
            if (eop) KindItem(st, kFlush, 0);
            continue;
        }
        const bool ok = (flg == 0) ? PackedPayload(st, p, n, off, nloop, nreg, regs)
                                   : ReglistPayload(st, p, n, off, nloop, nreg, regs);
        if (!ok) return false;
        if (eop) KindItem(st, kFlush, 0);  // EOP takes effect after the payload
    }
    if (off != n) return false;  // a complete packet consumes exactly its bytes
    FlushGap(st);                // trailing attribute writes keep their stores
    return true;
}

}  // namespace gs
}  // namespace ps2r
