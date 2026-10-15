// gs/state/gs_registers.h -- GsRegisters: the GS general-register file
// (GS User's Manual chapter 5 / ps2tek "GS registers"): PRIM and the PRMODE /
// PRMODECONT selection, the two drawing contexts (FRAME, ZBUF, XYOFFSET, SCISSOR,
// TEST, TEX0, TEX1, MIPTBP1/2, CLAMP, ALPHA, FBA), the environment (TEXA, FOGCOL,
// DTHE, DIMX, COLCLAMP, PABE, SCANMSK, TEXFLUSH), the vertex attribute registers
// (RGBAQ, ST, UV, FOG) and the CLUT load state, with every field decoded from
// the 64-bit register value at write time.
//
// Deferred-flush model: a register write that changes the pipeline state of a
// draw already queued must first flush that draw. Setters of the raster/texture
// state call the pending-draw hook unconditionally (the hook flushes when
// vertices are queued); PRIM flushes only when the primitive TYPE changes,
// PRMODE / PRMODECONT only when TME / ABE / FST / CTXT would change. The
// per-register DIRTY bits + TestDrawChanged / SnapshotState implement the "does
// the next kick belong to a new draw" test the kick path asks.
#pragma once

#include <cstdint>
#include <functional>

namespace ps2r {
namespace gs {

class GsRegisters {
public:
    enum Dirty : uint32_t {
        kDirtyPrim = 1u << 0, kDirtyFrame = 1u << 1, kDirtyZbuf = 1u << 2, kDirtyTex0 = 1u << 3,
        kDirtyTex1 = 1u << 4, kDirtyAlpha = 1u << 5, kDirtyTest = 1u << 6, kDirtyScissor = 1u << 7,
        kDirtyClamp = 1u << 8, kDirtyXyoffset = 1u << 9,
    };

    struct Prim {
        uint8_t type = 0;          // PRIM 0..6
        bool gouraud = false;      // IIP
        bool texture = false;      // TME
        bool fog = false;          // FGE
        bool alpha_blend = false;  // ABE
        bool antialiasing = false; // AA1
        bool use_uv = false;       // FST
        uint8_t context = 0;       // CTXT
    };
    struct Frame { uint32_t fbp = 0, fbw = 0, psm = 0, fbmsk = 0; };  // bytes / pixels
    struct Zbuf { uint32_t zbp = 0, psm = 0; bool zmsk = false; };
    struct XyOffset { float x = 0.0f, y = 0.0f; };  // pixels (12.4 / 16)
    struct Scissor { uint16_t x0 = 0, x1 = 0, y0 = 0, y1 = 0; };
    struct Test {
        bool ate = false; uint8_t atst = 0, aref = 0, afail = 0;
        bool date = false, datm = false; bool zte = true; uint8_t ztst = 1;
    };
    struct Tex0 {
        uint32_t tbp = 0, tbw = 0, psm = 0, tw = 0, th = 0, tcc = 0, tfx = 0;
        uint32_t cbp = 0, cpsm = 0, csm = 0, csa = 0, cld = 0;
    };
    struct Tex1 { bool lcm = false; uint8_t mxl = 0; bool mmag = false; uint8_t mmin = 0; bool mtba = false; uint8_t l = 0; uint16_t k = 0; };
    struct Clamp { uint8_t wms = 0, wmt = 0; uint16_t minu = 0, maxu = 0, minv = 0, maxv = 0; };
    struct Alpha { uint8_t a = 0, b = 1, c = 0, d = 1, fix = 0; };
    struct Context {
        Frame frame; Zbuf zbuf; XyOffset xyoffset; Scissor scissor; Test test;
        Tex0 tex0; Tex1 tex1; uint64_t miptbp1 = 0, miptbp2 = 0; Clamp clamp; Alpha alpha; bool fba = false;
    };
    struct Env {
        uint8_t texa_ta0 = 0x00; bool texa_aem = false; uint8_t texa_ta1 = 0x80;
        uint8_t fogcol_r = 0, fogcol_g = 0, fogcol_b = 0;
        bool dthe = false; uint64_t dimx = 0;
        bool colclamp = true; bool pabe = false; uint8_t scanmsk = 0;
        bool texflush_seen = false;   // TEXFLUSH written since the last flush
        bool prmodecont = true; uint64_t prmode_raw = 0;
        bool texture_dirty = true;
    };
    struct Attr {  // RGBAQ / ST / UV / FOG
        uint8_t r = 0x80, g = 0x80, b = 0x80, a = 0x80;
        float s = 0.0f, t = 0.0f, q = 1.0f;
        uint16_t u = 0, v = 0;
        bool uv_written_since_st = false;
        uint8_t fog_coef = 0;
    };
    struct ClutState { uint32_t cbp = 0, cpsm = 0, csa = 0, cld = 0; uint32_t loads = 0; };

    Prim prim;
    Context ctx[2];
    Env env;
    Attr attr;
    ClutState clut;
    uint32_t dirty_regs = 0;
    Prim prev;  // SnapshotState copy for TestDrawChanged

    // The pending-draw hooks the flush owner installs.
    std::function<bool()> has_pending;   // vertices queued for a draw
    std::function<void()> flush_pending; // flush them now

    // 64-bit register write for the general registers this file owns (the A+D
    // address table). Returns false for addresses it does not own (vertex kicks
    // 0x04/0x05/0x0C/0x0D, transfers 0x50..0x54, SIGNAL/FINISH/LABEL, no-ops).
    bool Write(uint8_t addr, uint64_t value);

    // PACKED-layout decodes that differ from the 64-bit register form
    // (GS User's Manual 7.3.1: RGBAQ colour bytes at 0/32/64/96, ST with Q in the
    // upper half, UV at 0/32, FOG byte at 100). Q is stored only when not 0.
    void PackedRgbaq(uint64_t lo, uint64_t hi);
    void PackedSt(uint64_t lo, uint64_t hi);
    void PackedUv(uint64_t lo);
    void PackedFog(uint64_t hi);

    bool TestDrawChanged();
    void SnapshotState();

    const Context& Cur() const { return ctx[prim.context & 1]; }

private:
    void FlushBeforeStateChange();
    bool WriteContextReg(uint8_t addr, uint64_t v);
    bool WriteEnvReg(uint8_t addr, uint64_t v);
    void SetPrim(uint64_t v);
    void SetPrmodecont(uint64_t v);
    void SetPrmode(uint64_t v);
    void LoadPrmodeBits(uint64_t v);
};

}  // namespace gs
}  // namespace ps2r
