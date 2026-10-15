// gs/state/gs_registers.cpp -- GsRegisters (see gs_registers.h). Bit positions
// follow the GS User's Manual register descriptions (chapter 5 tables).
#include "engine/gs/state/gs_registers.h"

#include <cstring>

namespace ps2r {
namespace gs {

namespace {
inline float BitsToFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
constexpr uint32_t kBlock = 256;  // TBP/CBP unit: 64 words
constexpr uint32_t kPage = 8192;  // FBP/ZBP unit: 2048 words
}  // namespace

void GsRegisters::FlushBeforeStateChange() {
    if (flush_pending) flush_pending();
}

bool GsRegisters::Write(uint8_t addr, uint64_t v) {
    switch (addr) {
        case 0x00: SetPrim(v); return true;
        case 0x01:  // RGBAQ (5.x): R 0-7 G 8-15 B 16-23 A 24-31 Q 32-63
            attr.r = v & 0xFF; attr.g = (v >> 8) & 0xFF; attr.b = (v >> 16) & 0xFF; attr.a = (v >> 24) & 0xFF;
            { const float q = BitsToFloat(static_cast<uint32_t>(v >> 32)); if (q != 0.0f) attr.q = q; }
            return true;
        case 0x02:  // ST: S 0-31 T 32-63 (no Q in the register form)
            attr.s = BitsToFloat(static_cast<uint32_t>(v & 0xFFFFFFFF));
            attr.t = BitsToFloat(static_cast<uint32_t>(v >> 32));
            attr.uv_written_since_st = false;
            return true;
        case 0x03:  // UV: U 0-13 V 16-29
            attr.u = v & 0x3FFF; attr.v = (v >> 16) & 0x3FFF; attr.uv_written_since_st = true;
            return true;
        case 0x0A:  // FOG: F 56-63
            attr.fog_coef = (v >> 56) & 0xFF;
            return true;
        case 0x1A: SetPrmodecont(v); return true;
        case 0x1B: SetPrmode(v); return true;
        default: break;
    }
    if (WriteContextReg(addr, v)) return true;
    return WriteEnvReg(addr, v);
}

// Context-pair registers: even address = context 1, odd = context 2.
bool GsRegisters::WriteContextReg(uint8_t addr, uint64_t v) {
    if (addr == 0x47 || addr == 0x48) {
        // TEST (5.x): ATE 0 ATST 1-3 AREF 4-11 AFAIL 12-13 DATE 14 DATM 15 ZTE 16 ZTST 17-18
        FlushBeforeStateChange();
        Test& t = ctx[addr - 0x47].test;
        t.ate = v & 1; t.atst = (v >> 1) & 7; t.aref = (v >> 4) & 0xFF; t.afail = (v >> 12) & 3;
        t.date = (v >> 14) & 1; t.datm = (v >> 15) & 1; t.zte = (v >> 16) & 1; t.ztst = (v >> 17) & 3;
        dirty_regs |= kDirtyTest;
        return true;
    }
    const int c = addr & 1;
    Context& x = ctx[c];
    switch (addr & ~1) {
        case 0x06:  // TEX0 (3.4.4): TBP0 0-13 TBW 14-19 PSM 20-25 TW 26-29 TH 30-33 TCC 34 TFX 35-36 CBP 37-50 CPSM 51-54 CSM 55 CSA 56-60 CLD 61-63
            FlushBeforeStateChange();
            x.tex0.tbp = static_cast<uint32_t>(v & 0x3FFF) * kBlock;
            x.tex0.tbw = static_cast<uint32_t>((v >> 14) & 0x3F) * 64;
            x.tex0.psm = (v >> 20) & 0x3F; x.tex0.tw = (v >> 26) & 0xF; x.tex0.th = (v >> 30) & 0xF;
            x.tex0.tcc = (v >> 34) & 1; x.tex0.tfx = (v >> 35) & 3;
            x.tex0.cbp = static_cast<uint32_t>((v >> 37) & 0x3FFF) * kBlock;
            x.tex0.cpsm = (v >> 51) & 0xF; x.tex0.csm = (v >> 55) & 1; x.tex0.csa = (v >> 56) & 0x1F; x.tex0.cld = (v >> 61) & 7;
            if (x.tex0.cld) { clut.cbp = x.tex0.cbp; clut.cpsm = x.tex0.cpsm; clut.csa = x.tex0.csa; clut.cld = x.tex0.cld; clut.loads++; }
            dirty_regs |= kDirtyTex0;
            return true;
        case 0x08:  // CLAMP (3.4.7): WMS 0-1 WMT 2-3 MINU 4-13 MAXU 14-23 MINV 24-33 MAXV 34-43
            FlushBeforeStateChange();
            x.clamp.wms = v & 3; x.clamp.wmt = (v >> 2) & 3;
            x.clamp.minu = (v >> 4) & 0x3FF; x.clamp.maxu = (v >> 14) & 0x3FF;
            x.clamp.minv = (v >> 24) & 0x3FF; x.clamp.maxv = (v >> 34) & 0x3FF;
            dirty_regs |= kDirtyClamp;
            return true;
        case 0x14:  // TEX1 (3.4.5): LCM 0 MXL 2-4 MMAG 5 MMIN 6-8 MTBA 9 L 19-20 K 32-43
            FlushBeforeStateChange();
            x.tex1.lcm = v & 1; x.tex1.mxl = (v >> 2) & 7; x.tex1.mmag = (v >> 5) & 1; x.tex1.mmin = (v >> 6) & 7;
            x.tex1.mtba = (v >> 9) & 1; x.tex1.l = (v >> 19) & 3; x.tex1.k = (v >> 32) & 0xFFF;
            dirty_regs |= kDirtyTex1;
            return true;
        case 0x16:  // TEX2: the CLUT part of TEX0 (PSM, CBP, CPSM, CSM, CSA, CLD); no dirty bit
            FlushBeforeStateChange();
            x.tex0.psm = (v >> 20) & 0x3F;
            x.tex0.cbp = static_cast<uint32_t>((v >> 37) & 0x3FFF) * kBlock;
            x.tex0.cpsm = (v >> 51) & 0xF; x.tex0.csm = (v >> 55) & 1; x.tex0.csa = (v >> 56) & 0x1F; x.tex0.cld = (v >> 61) & 7;
            if (x.tex0.cld) { clut.cbp = x.tex0.cbp; clut.cpsm = x.tex0.cpsm; clut.csa = x.tex0.csa; clut.cld = x.tex0.cld; clut.loads++; }
            return true;
        case 0x18:  // XYOFFSET: OFX 0-15 OFY 32-47 (12.4)
            FlushBeforeStateChange();
            x.xyoffset.x = static_cast<float>(static_cast<uint16_t>(v & 0xFFFF)) / 16.0f;
            x.xyoffset.y = static_cast<float>(static_cast<uint16_t>((v >> 32) & 0xFFFF)) / 16.0f;
            dirty_regs |= kDirtyXyoffset;
            return true;
        case 0x34:    // MIPTBP1 (3.4.11)
        case 0x36: {  // MIPTBP2
            uint64_t& slot = (addr < 0x36) ? x.miptbp1 : x.miptbp2;
            if (slot == v) return true;
            FlushBeforeStateChange();
            dirty_regs |= kDirtyTex1;
            slot = v;
            return true;
        }
        case 0x40:  // SCISSOR: SCAX0 0-10 SCAX1 16-26 SCAY0 32-42 SCAY1 48-58
            FlushBeforeStateChange();
            x.scissor.x0 = v & 0x7FF; x.scissor.x1 = (v >> 16) & 0x7FF;
            x.scissor.y0 = (v >> 32) & 0x7FF; x.scissor.y1 = (v >> 48) & 0x7FF;
            dirty_regs |= kDirtyScissor;
            return true;
        case 0x42:  // ALPHA: A 0-1 B 2-3 C 4-5 D 6-7 FIX 32-39
            FlushBeforeStateChange();
            x.alpha.a = v & 3; x.alpha.b = (v >> 2) & 3; x.alpha.c = (v >> 4) & 3; x.alpha.d = (v >> 6) & 3;
            x.alpha.fix = (v >> 32) & 0xFF;
            dirty_regs |= kDirtyAlpha;
            return true;
        case 0x4A:  // FBA
            FlushBeforeStateChange();
            x.fba = v & 1;
            return true;
        case 0x4C:  // FRAME: FBP 0-8 (2048 words) FBW 16-21 (64 px) PSM 24-29 FBMSK 32-63
            FlushBeforeStateChange();
            x.frame.fbp = static_cast<uint32_t>(v & 0x1FF) * kPage;
            x.frame.fbw = static_cast<uint32_t>((v >> 16) & 0x3F) * 64;
            x.frame.psm = (v >> 24) & 0x3F; x.frame.fbmsk = static_cast<uint32_t>(v >> 32);
            dirty_regs |= kDirtyFrame;
            return true;
        case 0x4E:  // ZBUF: ZBP 0-8 PSM 24-27 ZMSK 32
            FlushBeforeStateChange();
            x.zbuf.zbp = static_cast<uint32_t>(v & 0x1FF) * kPage;
            x.zbuf.psm = (v >> 24) & 0xF; x.zbuf.zmsk = (v >> 32) & 1;
            dirty_regs |= kDirtyZbuf;
            return true;
        default:
            break;
    }
    return false;
}

bool GsRegisters::WriteEnvReg(uint8_t addr, uint64_t v) {
    switch (addr) {
        case 0x22: FlushBeforeStateChange(); env.scanmsk = v & 3; return true;             // SCANMSK
        case 0x3B:                                                                          // TEXA: TA0 0-7 AEM 15 TA1 32-39
            FlushBeforeStateChange();
            env.texa_ta0 = v & 0xFF; env.texa_aem = (v >> 15) & 1; env.texa_ta1 = (v >> 32) & 0xFF;
            return true;
        case 0x3D:                                                                          // FOGCOL: R 0-7 G 8-15 B 16-23
            FlushBeforeStateChange();
            env.fogcol_r = v & 0xFF; env.fogcol_g = (v >> 8) & 0xFF; env.fogcol_b = (v >> 16) & 0xFF;
            return true;
        case 0x3F: env.texture_dirty = true; env.texflush_seen = true; return true;         // TEXFLUSH: no flush
        case 0x44: env.dimx = v; return true;                                               // DIMX: raw matrix
        case 0x45: env.dthe = (v & 1) != 0; return true;                                    // DTHE
        case 0x46: FlushBeforeStateChange(); env.colclamp = v & 1; return true;             // COLCLAMP
        case 0x49: FlushBeforeStateChange(); env.pabe = v & 1; return true;                 // PABE
        default: return false;
    }
}

// PRIM (5.x): PRIM 0-2 IIP 3 TME 4 FGE 5 ABE 6 AA1 7 FST 8 CTXT 9 FIX 10. Every
// PRIM write restarts the vertex queue (the kick owner observes kDirtyPrim); the
// attribute bits load only while PRMODECONT selects PRIM.
void GsRegisters::SetPrim(uint64_t v) {
    const uint8_t new_type = v & 7;
    if (new_type != prim.type && has_pending && has_pending()) FlushBeforeStateChange();
    dirty_regs |= kDirtyPrim;
    prim.type = new_type;
    if (env.prmodecont) LoadPrmodeBits(v);
}

void GsRegisters::LoadPrmodeBits(uint64_t v) {
    prim.gouraud = (v >> 3) & 1; prim.texture = (v >> 4) & 1; prim.fog = (v >> 5) & 1;
    prim.alpha_blend = (v >> 6) & 1; prim.antialiasing = (v >> 7) & 1; prim.use_uv = (v >> 8) & 1;
    prim.context = (v >> 9) & 1;
}

// PRMODECONT (5.x): AC 0. Switching to PRMODE reloads the attribute bits from
// the last PRMODE value; a queued draw is flushed first when that would change
// TME / ABE / FST / CTXT.
void GsRegisters::SetPrmodecont(uint64_t v) {
    const bool old = env.prmodecont, nw = (v & 1) != 0;
    if (old != nw && has_pending && has_pending()) {
        bool would_change = false;
        if (old && !nw) {
            const uint64_t p = env.prmode_raw;
            would_change = prim.texture != (((p >> 4) & 1) != 0) || prim.alpha_blend != (((p >> 6) & 1) != 0) ||
                           prim.use_uv != (((p >> 8) & 1) != 0) || prim.context != ((p >> 9) & 1);
        }
        if (would_change) FlushBeforeStateChange();
    }
    env.prmodecont = nw;
    if (old && !nw) LoadPrmodeBits(env.prmode_raw);
}

void GsRegisters::SetPrmode(uint64_t v) {
    if (!env.prmodecont && has_pending && has_pending()) {
        const bool would_change = prim.texture != (((v >> 4) & 1) != 0) || prim.alpha_blend != (((v >> 6) & 1) != 0) ||
                                  prim.use_uv != (((v >> 8) & 1) != 0) || prim.context != ((v >> 9) & 1);
        if (would_change) FlushBeforeStateChange();
    }
    env.prmode_raw = v;
    if (!env.prmodecont) LoadPrmodeBits(v);
}

void GsRegisters::PackedRgbaq(uint64_t lo, uint64_t hi) {
    attr.r = lo & 0xFF; attr.g = (lo >> 32) & 0xFF; attr.b = hi & 0xFF; attr.a = (hi >> 32) & 0xFF;
}

void GsRegisters::PackedSt(uint64_t lo, uint64_t hi) {
    attr.s = BitsToFloat(static_cast<uint32_t>(lo & 0xFFFFFFFF));
    attr.t = BitsToFloat(static_cast<uint32_t>(lo >> 32));
    const float q = BitsToFloat(static_cast<uint32_t>(hi & 0xFFFFFFFF));
    if (q != 0.0f) attr.q = q;
    attr.uv_written_since_st = false;
}

void GsRegisters::PackedUv(uint64_t lo) {
    attr.u = lo & 0x3FFF; attr.v = (lo >> 32) & 0x3FFF; attr.uv_written_since_st = true;
}

// PACKED FOG stores the fog BYTE into PRIM.FGE (any non-zero byte enables fog):
// the original runtime's decode, kept for identity (finding P6-FOG-BOOL).
void GsRegisters::PackedFog(uint64_t hi) {
    prim.fog = ((hi >> 36) & 0xFF) != 0;
}

bool GsRegisters::TestDrawChanged() {
    if (!dirty_regs) return false;
    if (dirty_regs & kDirtyPrim) {
        if (prev.type != prim.type || prev.context != prim.context || prev.texture != prim.texture ||
            prev.alpha_blend != prim.alpha_blend || prev.use_uv != prim.use_uv)
            return true;
        dirty_regs &= ~kDirtyPrim;
        if (!dirty_regs) return false;
    }
    if (dirty_regs & (kDirtyFrame | kDirtyZbuf | kDirtyTex0 | kDirtyAlpha | kDirtyTest | kDirtyScissor | kDirtyXyoffset))
        return true;
    if (prev.texture && (dirty_regs & (kDirtyTex1 | kDirtyClamp))) return true;
    return false;
}

void GsRegisters::SnapshotState() {
    prev.type = prim.type; prev.texture = prim.texture; prev.alpha_blend = prim.alpha_blend;
    prev.use_uv = prim.use_uv; prev.context = prim.context;
    dirty_regs = 0;
}

}  // namespace gs
}  // namespace ps2r
