// gs/savestate/gs_sections.cpp -- GsSections (see gs_sections.h).
#include "engine/gs/savestate/gs_sections.h"

#include <cstring>

#include "engine/core/savestate/payload_reader.h"
#include "engine/gs/state/gs_priv_regs.h"
#include "engine/gs/state/gs_state.h"
#include "engine/gs/vram/vram_image.h"

using ps2r::savestate::Blob;
using ps2r::savestate::SectionSpec;

namespace ps2r {
namespace gs {

static_assert(GsSections::kVramBytes == VramImage::kBytes, "the VRAM section is the whole local memory");

GsSections::GsSections(GsState& gs) : gs_(gs) {}

uint32_t GsSections::SlotAddr(size_t i) {
    static constexpr uint32_t kOrder[kPrivRegCount] = {
        GsPrivRegs::kPmode,   GsPrivRegs::kSmode1,   GsPrivRegs::kSmode2,  GsPrivRegs::kSrfsh,
        GsPrivRegs::kSynch1,  GsPrivRegs::kSynch2,   GsPrivRegs::kSyncv,   GsPrivRegs::kDispfb1,
        GsPrivRegs::kDisplay1, GsPrivRegs::kDispfb2, GsPrivRegs::kDisplay2, GsPrivRegs::kExtbuf,
        GsPrivRegs::kExtdata, GsPrivRegs::kExtwrite, GsPrivRegs::kBgcolor, GsPrivRegs::kCsr,
        GsPrivRegs::kImr,     GsPrivRegs::kBusdir,   GsPrivRegs::kSiglblid,
    };
    return kOrder[i < kPrivRegCount ? i : 0];
}

void GsSections::Register(savestate::SectionRegistry& registry) {
    registry.Register(VramSpec());
    registry.Register(GsprSpec());
    registry.Register(GsstSpec());
}

SectionSpec GsSections::GsstSpec() {
    SectionSpec s;
    s.tag = savestate::TAG_GSST;
    s.version = kGsstVersion;
    s.required = true;
    s.owner = "GsSections/GSST";
    s.serialize = [this](std::vector<uint8_t>& out) { SerializeGsst(out); };
    s.validate = [](const Blob& b, std::string* err) { return ValidateGsst(b.payload, b.version, err); };
    s.restore = [this](const Blob& b, std::string* err) { return RestoreGsst(b.payload, b.version, err); };
    return s;
}

namespace {
// The reference wrote its decoded register structs as PODs; these are their
// byte-exact wire shapes (packed), copied field by field into GsRegisters.
#pragma pack(push, 1)
struct WireFrame { uint32_t fbp, fbw, psm, fbmsk; };
struct WireZbuf { uint32_t zbp, psm; uint8_t zmsk; uint8_t pad[3]; };
struct WireTex0 { uint32_t tbp, tbw, psm, tw, th, tcc, tfx, cbp, cpsm, csm, csa, cld; };
struct WireTest { uint8_t ate, atst, aref, afail, date, datm, zte, ztst; };
struct WireAlpha { uint8_t a, b, c, d, fix; };
struct WireClamp { uint8_t wms, wmt; uint16_t minu, maxu, minv, maxv; };
struct WireScissor { uint16_t x0, x1, y0, y1; };
#pragma pack(pop)
static_assert(sizeof(WireFrame) == 16 && sizeof(WireZbuf) == 12 && sizeof(WireTex0) == 48 && sizeof(WireTest) == 8 &&
                  sizeof(WireAlpha) == 5 && sizeof(WireClamp) == 10 && sizeof(WireScissor) == 8,
              "GSST wire shapes");

void PutU8(std::vector<uint8_t>& v, bool b) { savestate::PutPod<uint8_t>(v, b ? 1 : 0); }

bool GetBool(savestate::PayloadReader& r, bool* dst) {
    uint8_t t = 0;
    if (!r.Get(&t)) return false;
    *dst = t != 0;
    return true;
}

bool SkipVertexArray(savestate::PayloadReader& r) {
    uint32_t count = 0;
    if (!r.Get(&count) || count > GsSections::kGsstVertexArrayMax) return false;
    std::vector<uint8_t> skip(static_cast<size_t>(count) * GsSections::kGsstVertexRecBytes);
    return count == 0 || r.GetBytes(skip.data(), skip.size());
}
}  // namespace

// The v3 shape: the body, an empty latched-vertex extension (a boundary save
// holds no queued vertices), the K / MIPTBP tail.
void GsSections::SerializeGsst(std::vector<uint8_t>& out) const {
    const GsRegisters& g = gs_.Regs();
    savestate::PutPod<uint8_t>(out, g.prim.type);
    PutU8(out, g.prim.gouraud); PutU8(out, g.prim.texture); PutU8(out, g.prim.texture);
    PutU8(out, g.prim.fog); PutU8(out, g.prim.alpha_blend); PutU8(out, g.prim.antialiasing); PutU8(out, g.prim.use_uv);
    savestate::PutPod<int32_t>(out, 0);  // strip_count: the flush owner's, 0 at a boundary
    savestate::PutPod<uint32_t>(out, g.dirty_regs);
    savestate::PutPod<uint8_t>(out, g.prev.type);
    PutU8(out, g.prev.texture); PutU8(out, g.prev.alpha_blend); PutU8(out, g.prev.use_uv);
    savestate::PutPod<int32_t>(out, g.prev.context);
    savestate::PutPod<int32_t>(out, g.prim.context);
    for (int i = 0; i < 2; ++i) { savestate::PutPod<float>(out, g.ctx[i].xyoffset.x); savestate::PutPod<float>(out, g.ctx[i].xyoffset.y); }
    for (int i = 0; i < 2; ++i) savestate::PutPod(out, WireScissor{g.ctx[i].scissor.x0, g.ctx[i].scissor.x1, g.ctx[i].scissor.y0, g.ctx[i].scissor.y1});
    for (int i = 0; i < 2; ++i) savestate::PutPod(out, WireFrame{g.ctx[i].frame.fbp, g.ctx[i].frame.fbw, g.ctx[i].frame.psm, g.ctx[i].frame.fbmsk});
    for (int i = 0; i < 2; ++i) savestate::PutPod(out, WireZbuf{g.ctx[i].zbuf.zbp, g.ctx[i].zbuf.psm, g.ctx[i].zbuf.zmsk ? uint8_t(1) : uint8_t(0), {0, 0, 0}});
    for (int i = 0; i < 2; ++i) {
        const GsRegisters::Tex0& t = g.ctx[i].tex0;
        savestate::PutPod(out, WireTex0{t.tbp, t.tbw, t.psm, t.tw, t.th, t.tcc, t.tfx, t.cbp, t.cpsm, t.csm, t.csa, t.cld});
    }
    for (int i = 0; i < 2; ++i) {
        const GsRegisters::Tex1& t = g.ctx[i].tex1;
        PutU8(out, t.lcm); savestate::PutPod<uint8_t>(out, t.mxl); PutU8(out, t.mmag); savestate::PutPod<uint8_t>(out, t.mmin);
        PutU8(out, t.mtba); savestate::PutPod<uint8_t>(out, t.l); savestate::PutPod<uint8_t>(out, static_cast<uint8_t>(t.k & 0xFF));
    }
    for (int i = 0; i < 2; ++i) {
        const GsRegisters::Test& t = g.ctx[i].test;
        savestate::PutPod(out, WireTest{t.ate, t.atst, t.aref, t.afail, t.date, t.datm, t.zte, t.ztst});
    }
    for (int i = 0; i < 2; ++i) savestate::PutPod(out, WireAlpha{g.ctx[i].alpha.a, g.ctx[i].alpha.b, g.ctx[i].alpha.c, g.ctx[i].alpha.d, g.ctx[i].alpha.fix});
    for (int i = 0; i < 2; ++i) {
        const GsRegisters::Clamp& c = g.ctx[i].clamp;
        savestate::PutPod(out, WireClamp{c.wms, c.wmt, c.minu, c.maxu, c.minv, c.maxv});
    }
    savestate::PutPod<uint8_t>(out, g.env.texa_ta0); PutU8(out, g.env.texa_aem); savestate::PutPod<uint8_t>(out, g.env.texa_ta1);
    PutU8(out, g.env.colclamp); PutU8(out, g.env.prmodecont);
    savestate::PutPod<uint64_t>(out, g.env.prmode_raw);
    PutU8(out, g.env.texture_dirty);
    PutU8(out, g.ctx[0].fba); PutU8(out, g.ctx[1].fba); PutU8(out, g.env.pabe);
    savestate::PutPod<uint8_t>(out, g.env.scanmsk);
    savestate::PutPod<uint8_t>(out, g.env.fogcol_r); savestate::PutPod<uint8_t>(out, g.env.fogcol_g); savestate::PutPod<uint8_t>(out, g.env.fogcol_b);
    // The transfer registers: the engine's own state; a boundary save is idle.
    for (int k = 0; k < 6; ++k) savestate::PutPod<uint32_t>(out, 0u);          // sbp sbw spsm dbp dbw dpsm
    for (int k = 0; k < 4; ++k) savestate::PutPod<uint16_t>(out, 0u);          // ssax ssay dsax dsay
    savestate::PutPod<uint8_t>(out, 0u);                                        // dir
    for (int k = 0; k < 2; ++k) savestate::PutPod<uint16_t>(out, 0u);          // rrw rrh
    savestate::PutPod<uint8_t>(out, 3u);                                        // xdir: deactivated
    for (int k = 0; k < 2; ++k) savestate::PutPod<uint16_t>(out, 0u);          // cur_x cur_y
    PutU8(out, false);                                                          // active
    savestate::PutPod<uint8_t>(out, g.attr.r); savestate::PutPod<uint8_t>(out, g.attr.g);
    savestate::PutPod<uint8_t>(out, g.attr.b); savestate::PutPod<uint8_t>(out, g.attr.a);
    savestate::PutPod<float>(out, g.attr.s); savestate::PutPod<float>(out, g.attr.t); savestate::PutPod<float>(out, g.attr.q);
    savestate::PutPod<uint16_t>(out, g.attr.u); savestate::PutPod<uint16_t>(out, g.attr.v);
    PutU8(out, g.attr.uv_written_since_st);
    savestate::PutPod<uint8_t>(out, g.attr.fog_coef);
    // v2 extension: last_rgbaq_pc + two empty vertex arrays.
    savestate::PutPod<uint32_t>(out, 0u);
    savestate::PutPod<uint32_t>(out, 0u);
    savestate::PutPod<uint32_t>(out, 0u);
    // v3 tail.
    for (int i = 0; i < 2; ++i) {
        savestate::PutPod<uint16_t>(out, g.ctx[i].tex1.k);
        savestate::PutPod<uint64_t>(out, g.ctx[i].miptbp1);
        savestate::PutPod<uint64_t>(out, g.ctx[i].miptbp2);
    }
}

bool GsSections::ValidateGsst(const std::vector<uint8_t>& payload, uint32_t version, std::string* err) {
    if (version < 1 || version > kGsstVersion) {
        if (err) *err = "GSST: section version " + std::to_string(version) + " not in 1.." + std::to_string(kGsstVersion);
        return false;
    }
    if (payload.size() < kGsstBodyBytes) {
        if (err) *err = "GSST: payload is " + std::to_string(payload.size()) + " bytes, below the " + std::to_string(kGsstBodyBytes) + "-byte body";
        return false;
    }
    return true;
}

// The body in the reference's field order (ParseGsState), copied into the
// decoded register file; the transfer block is read and discarded (the engine
// is idle at a boundary), the latched vertex arrays are skipped.
bool GsSections::RestoreGsst(const std::vector<uint8_t>& payload, uint32_t version, std::string* err) {
    if (!ValidateGsst(payload, version, err)) return false;
    savestate::PayloadReader r(payload);
    GsRegisters g = gs_.Regs();  // copy: a short read leaves the live file untouched
    bool ok = true;
    uint8_t u8 = 0; int32_t i32 = 0; bool batch_tex = false;
    ok = ok && r.Get(&g.prim.type);
    ok = ok && GetBool(r, &g.prim.gouraud) && GetBool(r, &g.prim.texture) && GetBool(r, &batch_tex) && GetBool(r, &g.prim.fog) &&
         GetBool(r, &g.prim.alpha_blend) && GetBool(r, &g.prim.antialiasing) && GetBool(r, &g.prim.use_uv);
    ok = ok && r.Get(&i32);                 // strip_count (the flush owner's)
    ok = ok && r.Get(&g.dirty_regs);
    ok = ok && r.Get(&g.prev.type) && GetBool(r, &g.prev.texture) && GetBool(r, &g.prev.alpha_blend) && GetBool(r, &g.prev.use_uv);
    ok = ok && r.Get(&i32); if (ok) g.prev.context = static_cast<uint8_t>(i32 & 1);
    ok = ok && r.Get(&i32); if (ok) g.prim.context = static_cast<uint8_t>(i32 & 1);
    for (int i = 0; i < 2 && ok; ++i) ok = r.Get(&g.ctx[i].xyoffset.x) && r.Get(&g.ctx[i].xyoffset.y);
    for (int i = 0; i < 2 && ok; ++i) { WireScissor w{}; ok = r.Get(&w); g.ctx[i].scissor = {w.x0, w.x1, w.y0, w.y1}; }
    for (int i = 0; i < 2 && ok; ++i) { WireFrame w{}; ok = r.Get(&w); g.ctx[i].frame = {w.fbp, w.fbw, w.psm, w.fbmsk}; }
    for (int i = 0; i < 2 && ok; ++i) { WireZbuf w{}; ok = r.Get(&w); g.ctx[i].zbuf = {w.zbp, w.psm, w.zmsk != 0}; }
    for (int i = 0; i < 2 && ok; ++i) {
        WireTex0 w{}; ok = r.Get(&w);
        g.ctx[i].tex0 = {w.tbp, w.tbw, w.psm, w.tw, w.th, w.tcc, w.tfx, w.cbp, w.cpsm, w.csm, w.csa, w.cld};
    }
    for (int i = 0; i < 2 && ok; ++i) {
        GsRegisters::Tex1& t = g.ctx[i].tex1;
        ok = GetBool(r, &t.lcm) && r.Get(&t.mxl) && GetBool(r, &t.mmag) && r.Get(&t.mmin) && GetBool(r, &t.mtba) && r.Get(&t.l) && r.Get(&u8);
        t.k = u8;  // the v3 tail overrides with the full 12-bit field
    }
    for (int i = 0; i < 2 && ok; ++i) {
        WireTest w{}; ok = r.Get(&w);
        g.ctx[i].test = {w.ate != 0, w.atst, w.aref, w.afail, w.date != 0, w.datm != 0, w.zte != 0, w.ztst};
    }
    for (int i = 0; i < 2 && ok; ++i) { WireAlpha w{}; ok = r.Get(&w); g.ctx[i].alpha = {w.a, w.b, w.c, w.d, w.fix}; }
    for (int i = 0; i < 2 && ok; ++i) { WireClamp w{}; ok = r.Get(&w); g.ctx[i].clamp = {w.wms, w.wmt, w.minu, w.maxu, w.minv, w.maxv}; }
    ok = ok && r.Get(&g.env.texa_ta0) && GetBool(r, &g.env.texa_aem) && r.Get(&g.env.texa_ta1);
    ok = ok && GetBool(r, &g.env.colclamp) && GetBool(r, &g.env.prmodecont) && r.Get(&g.env.prmode_raw) && GetBool(r, &g.env.texture_dirty);
    ok = ok && GetBool(r, &g.ctx[0].fba) && GetBool(r, &g.ctx[1].fba) && GetBool(r, &g.env.pabe) && r.Get(&g.env.scanmsk);
    ok = ok && r.Get(&g.env.fogcol_r) && r.Get(&g.env.fogcol_g) && r.Get(&g.env.fogcol_b);
    {   // the transfer block (sbp .. active): 6 x u32, 4 x u16, u8, 2 x u16, u8, 2 x u16, u8
        uint32_t x32 = 0; uint16_t x16 = 0; uint8_t x8 = 0;
        for (int k = 0; k < 6 && ok; ++k) ok = r.Get(&x32);
        for (int k = 0; k < 4 && ok; ++k) ok = r.Get(&x16);
        ok = ok && r.Get(&x8);
        for (int k = 0; k < 2 && ok; ++k) ok = r.Get(&x16);
        ok = ok && r.Get(&x8);
        for (int k = 0; k < 2 && ok; ++k) ok = r.Get(&x16);
        ok = ok && r.Get(&x8);
    }
    ok = ok && r.Get(&g.attr.r) && r.Get(&g.attr.g) && r.Get(&g.attr.b) && r.Get(&g.attr.a);
    ok = ok && r.Get(&g.attr.s) && r.Get(&g.attr.t) && r.Get(&g.attr.q) && r.Get(&g.attr.u) && r.Get(&g.attr.v);
    ok = ok && GetBool(r, &g.attr.uv_written_since_st) && r.Get(&g.attr.fog_coef);
    if (ok && version >= 2) {
        uint32_t last_rgbaq_pc = 0;
        ok = r.Get(&last_rgbaq_pc) && SkipVertexArray(r) && SkipVertexArray(r);
        if (ok && version >= 3)
            for (int i = 0; i < 2 && ok; ++i) ok = r.Get(&g.ctx[i].tex1.k) && r.Get(&g.ctx[i].miptbp1) && r.Get(&g.ctx[i].miptbp2);
    }
    if (!ok) {
        if (err) *err = "GSST: truncated payload (version " + std::to_string(version) + ")";
        return false;
    }
    GsRegisters& live = gs_.Regs();
    live.prim = g.prim; live.prev = g.prev; live.dirty_regs = g.dirty_regs; live.attr = g.attr;
    live.ctx[0] = g.ctx[0]; live.ctx[1] = g.ctx[1];
    // env: every field but the flush-side TEXFLUSH latch.
    const bool texflush_seen = live.env.texflush_seen;
    live.env = g.env;
    live.env.texflush_seen = texflush_seen;
    ++restored_gsst_;
    return true;
}

SectionSpec GsSections::VramSpec() {
    SectionSpec s;
    s.tag = savestate::TAG_VRAM;
    s.required = true;
    s.owner = "GsSections/VRAM";
    s.serialize = [this](std::vector<uint8_t>& out) { SerializeVram(out); };
    s.validate = [](const Blob& b, std::string* err) { return ValidateVram(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return RestoreVram(b.payload, err); };
    return s;
}

SectionSpec GsSections::GsprSpec() {
    SectionSpec s;
    s.tag = savestate::TAG_GSPR;
    s.required = true;
    s.owner = "GsSections/GSPR";
    s.serialize = [this](std::vector<uint8_t>& out) { SerializeGspr(out); };
    s.validate = [](const Blob& b, std::string* err) { return ValidateGspr(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return RestoreGspr(b.payload, err); };
    return s;
}

// The save reads the committed view (bytes overlaid with every pending GPU
// deposit): what a later commit would produce, the same bytes a reader sees.
void GsSections::SerializeVram(std::vector<uint8_t>& out) const {
    const size_t at = out.size();
    out.resize(at + kVramBytes);
    const VramImage* source = vram_source_ ? vram_source_() : nullptr;
    if (!source) source = &gs_.Vram();
    source->ReadView(0, kVramBytes, out.data() + at);
}

void GsSections::SerializeGspr(std::vector<uint8_t>& out) const {
    out.reserve(out.size() + kGsprBytes);
    for (size_t i = 0; i < kPrivRegCount; ++i) savestate::PutPod<uint64_t>(out, gs_.Priv().Read(SlotAddr(i)));
    savestate::PutPod<int32_t>(out, display_mode_);
    savestate::PutPod<uint8_t>(out, interlaced_ ? 1 : 0);
    savestate::PutPod<uint8_t>(out, frame_mode_ ? 1 : 0);
}

bool GsSections::ValidateVram(const std::vector<uint8_t>& payload, std::string* err) {
    if (payload.size() != kVramBytes) {
        if (err) *err = "VRAM: payload is " + std::to_string(payload.size()) + " bytes, expected " + std::to_string(kVramBytes);
        return false;
    }
    return true;
}

bool GsSections::ValidateGspr(const std::vector<uint8_t>& payload, std::string* err) {
    if (payload.size() != kGsprBytes) {
        if (err) *err = "GSPR: payload is " + std::to_string(payload.size()) + " bytes, expected " + std::to_string(kGsprBytes);
        return false;
    }
    return true;
}

bool GsSections::RestoreVram(const std::vector<uint8_t>& payload, std::string* err) {
    if (!ValidateVram(payload, err)) return false;
    gs_.RestoreLocalMemory(payload.data(), payload.size());
    ++restored_vram_;
    return true;
}

// Every register but CSR goes through the write path: DISPFB / DISPLAY / BGCOLOR
// emit their presenter jobs when the value differs from the live one, SMODE2
// re-derives the interlace latch. CSR is GS-held status and lands verbatim.
bool GsSections::RestoreGspr(const std::vector<uint8_t>& payload, std::string* err) {
    if (!ValidateGspr(payload, err)) return false;
    savestate::PayloadReader r(payload);
    uint64_t regs[kPrivRegCount] = {};
    for (size_t i = 0; i < kPrivRegCount; ++i) {
        if (!r.Get(&regs[i])) {
            if (err) *err = "GSPR: truncated register file";
            return false;
        }
    }
    int32_t dm = 0;
    uint8_t il = 0, fm = 0;
    if (!r.Get(&dm) || !r.Get(&il) || !r.Get(&fm)) {
        if (err) *err = "GSPR: truncated video-mode tail";
        return false;
    }
    GsPrivRegs& priv = gs_.Priv();
    for (size_t i = 0; i < kPrivRegCount; ++i) {
        const uint32_t addr = SlotAddr(i);
        if (addr == GsPrivRegs::kCsr) priv.RestoreCsr(regs[i]);
        else priv.Write(addr, regs[i]);
    }
    display_mode_ = dm;
    interlaced_ = il != 0;
    frame_mode_ = fm != 0;
    ++restored_gspr_;
    return true;
}

}  // namespace gs
}  // namespace ps2r
