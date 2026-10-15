// tests/gs/savestate/test_gs_sections.cpp -- GsSections: the VRAM + GSPR + GSST
// savestate sections over GsState. Round trip into a fresh façade byte-equal (local
// memory through the committed view, the 19 privileged registers incl. CSR, the
// video-mode triple, the decoded drawing environment v1-v3); a restore emits the
// presenter's jobs (InvalidateTextures over the whole 4 MiB, SetFrontBuffer from
// DISPFB, SetWindow from DISPLAY, SetClearColor from BGCOLOR) and re-derives the
// interlace latch; wrong lengths / versions refused; all three sections REQUIRED
// through the registry; the reference's wire sizes (4,194,304 / 158 / 406 bytes).
// Usage: test_gs_sections <roundtrip|jobs|refuse|registry|gsst|all>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/gs/savestate/gs_sections.h"
#include "engine/gs/state/gs_priv_regs.h"
#include "engine/gs/state/gs_registers.h"
#include "engine/gs/state/gs_state.h"
#include "engine/gs/vram/vram_image.h"
#include "tests/lib/capture_sink.h"

using namespace ps2r::gs;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg)                                                       \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(c)) {                                                         \
            ++g_fail;                                                       \
            std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
        }                                                                   \
    } while (0)

namespace {
// The frozen anchors' CRTC tuple: both circuits at FBP 0, FBW 8 (512 px), PSMCT16,
// DISPLAY 2560/5 x 448, PMODE EN1|EN2 ALP 128, SMODE2 interlaced.
constexpr uint64_t kPmode = 0x8023;
constexpr uint64_t kSmode2 = 0x1;
constexpr uint64_t kDispfb = 0x11000;
constexpr uint64_t kDisplay1 = 0x1bfa000203227cull;
constexpr uint64_t kDisplay2 = 0x1bfa000203327cull;
constexpr uint64_t kBgcolor = 0x102030;
constexpr uint64_t kCsr = 0x2308;
constexpr uint64_t kImr = 0x7f00;

void Populate(GsState& gs) {
    GsPrivRegs& p = gs.Priv();
    p.Write(GsPrivRegs::kPmode, kPmode);
    p.Write(GsPrivRegs::kSmode2, kSmode2);
    p.Write(GsPrivRegs::kDispfb1, kDispfb);
    p.Write(GsPrivRegs::kDispfb2, kDispfb);
    p.Write(GsPrivRegs::kDisplay1, kDisplay1);
    p.Write(GsPrivRegs::kDisplay2, kDisplay2);
    p.Write(GsPrivRegs::kBgcolor, kBgcolor);
    p.Write(GsPrivRegs::kImr, kImr);
    p.RestoreCsr(kCsr);
    // Local memory: a recognisable pattern over the first display page and a
    // byte near the top of the 4 MiB.
    std::vector<uint8_t> page(VramImage::kPageBytes);
    for (size_t i = 0; i < page.size(); ++i) page[i] = static_cast<uint8_t>(i * 7 + 3);
    gs.Vram().WriteStream(0, page.data(), page.size());
    const uint8_t top = 0xA5;
    gs.Vram().WriteStream(VramImage::kBytes - 1, &top, 1);
    gs.Vram().SettleWrites();
}

size_t CountJobs(const CaptureSink& s, JobKind k) {
    size_t n = 0;
    for (const JobRec& j : s.jobs) n += (j.type == static_cast<uint32_t>(k)) ? 1 : 0;
    return n;
}
}  // namespace

static void Roundtrip() {
    GsState src;
    CaptureSink src_sink;
    src.SetSink(&src_sink);
    Populate(src);
    GsSections a(src);
    std::vector<uint8_t> vram, gspr;
    a.SerializeVram(vram);
    a.SerializeGspr(gspr);
    CHECK(vram.size() == GsSections::kVramBytes, "VRAM payload is the whole 4 MiB (4,194,304 bytes)");
    CHECK(gspr.size() == GsSections::kGsprBytes && gspr.size() == 158, "GSPR payload is 19 x u64 + i32 + u8 + u8 = 158 bytes");
    std::vector<uint8_t> view(VramImage::kBytes);
    src.Vram().ReadView(0, VramImage::kBytes, view.data());
    CHECK(std::memcmp(vram.data(), view.data(), VramImage::kBytes) == 0, "VRAM payload equals the committed view");

    GsState dst;
    CaptureSink dst_sink;
    dst.SetSink(&dst_sink);
    GsSections b(dst);
    std::string err;
    CHECK(b.RestoreVram(vram, &err), "VRAM restore accepted");
    CHECK(b.RestoreGspr(gspr, &err), "GSPR restore accepted");
    CHECK(b.RestoredVramCount() == 1 && b.RestoredGsprCount() == 1, "restore counters");
    std::vector<uint8_t> view2(VramImage::kBytes);
    dst.Vram().ReadView(0, VramImage::kBytes, view2.data());
    CHECK(std::memcmp(view.data(), view2.data(), VramImage::kBytes) == 0, "restored local memory byte-equal");
    CHECK(view2[0] == 3 && view2[VramImage::kBytes - 1] == 0xA5, "first and last restored bytes");
    for (size_t i = 0; i < GsSections::kPrivRegCount; ++i) {
        const uint32_t addr = GsSections::SlotAddr(i);
        CHECK(dst.Priv().Read(addr) == src.Priv().Read(addr), "privileged register restored equal");
    }
    CHECK(dst.Priv().Read(GsPrivRegs::kCsr) == kCsr, "CSR restored verbatim (0x2308)");
    CHECK(dst.Priv().Interlaced(), "SMODE2.INT re-derives the interlace latch");
    CHECK(b.DisplayMode() == 2 && b.Interlaced() && !b.FrameMode(), "video-mode triple round-trips its default");
    // Serialize again: byte-equal payloads.
    std::vector<uint8_t> vram2, gspr2;
    b.SerializeVram(vram2);
    b.SerializeGspr(gspr2);
    CHECK(vram2 == vram, "VRAM re-serialized byte-equal");
    CHECK(gspr2 == gspr, "GSPR re-serialized byte-equal");
    // The restored page is CPU-authored with a fresh upload epoch (the settle ran).
    CHECK(dst.Vram().Page(0).author == PageRecord::Author::Cpu, "restored page authored by the CPU");
    CHECK(dst.Vram().UploadEpoch() >= 1, "restore settled an upload epoch");
}

static void Jobs() {
    GsState src;
    Populate(src);
    GsSections a(src);
    std::vector<uint8_t> vram, gspr;
    a.SerializeVram(vram);
    a.SerializeGspr(gspr);

    GsState dst;
    CaptureSink sink;
    dst.SetSink(&sink);
    GsSections b(dst);
    std::string err;
    CHECK(b.RestoreVram(vram, &err), "VRAM restore");
    CHECK(CountJobs(sink, JobKind::InvalidateTextures) == 1, "one InvalidateTextures job for the restored image");
    const JobRec& inv = sink.jobs.front();
    CHECK(inv.arg1 == 0 && inv.arg2 == VramImage::kBytes, "InvalidateTextures covers [0, 4 MiB)");
    CHECK(inv.vram_epoch == dst.Vram().UploadEpoch(), "the job carries the restored image's upload epoch");
    const size_t before = sink.jobs.size();
    CHECK(b.RestoreGspr(gspr, &err), "GSPR restore");
    CHECK(CountJobs(sink, JobKind::SetFrontBuffer) == 2, "DISPFB1 + DISPFB2 emit SetFrontBuffer");
    CHECK(CountJobs(sink, JobKind::SetWindow) == 2, "DISPLAY1 + DISPLAY2 emit SetWindow");
    CHECK(CountJobs(sink, JobKind::SetClearColor) == 1, "BGCOLOR emits SetClearColor");
    bool fb_ok = false, win_ok = false;
    for (size_t i = before; i < sink.jobs.size(); ++i) {
        const JobRec& j = sink.jobs[i];
        if (j.type == static_cast<uint32_t>(JobKind::SetFrontBuffer)) fb_ok = (j.arg1 == 0 && j.arg2 == 512 && j.arg3 == 2);
        if (j.type == static_cast<uint32_t>(JobKind::SetWindow) && j.arg1 == 1) win_ok = ((j.arg2 & 0xFFFF) == 512 && (j.arg3 & 0xFFFF) == 448);
    }
    CHECK(fb_ok, "SetFrontBuffer: fbp 0, fbw 512 px, PSMCT16");
    CHECK(win_ok, "SetWindow circuit 1: content 512 x 448");
    // A second restore of the same registers is idempotent (no new display jobs).
    const size_t n = sink.jobs.size();
    CHECK(b.RestoreGspr(gspr, &err), "GSPR restore again");
    CHECK(sink.jobs.size() == n, "same register values emit no further jobs");
}

static void Refuse() {
    GsState gs;
    GsSections s(gs);
    std::string err;
    std::vector<uint8_t> short_vram(VramImage::kBytes - 1), long_gspr(GsSections::kGsprBytes + 1), empty;
    CHECK(!GsSections::ValidateVram(short_vram, &err) && err.find("VRAM") == 0, "VRAM: 4 MiB - 1 refused");
    CHECK(!GsSections::ValidateGspr(long_gspr, &err) && err.find("GSPR") == 0, "GSPR: 159 bytes refused");
    CHECK(!s.RestoreVram(empty, &err), "VRAM: empty refused");
    CHECK(!s.RestoreGspr(empty, &err), "GSPR: empty refused");
    CHECK(s.RestoredVramCount() == 0 && s.RestoredGsprCount() == 0, "no restore counted");
}

static void Registry() {
    GsState gs;
    GsSections s(gs);
    ps2r::savestate::SectionRegistry reg;
    s.Register(reg);
    CHECK(reg.Has(ps2r::savestate::TAG_VRAM) && reg.Has(ps2r::savestate::TAG_GSPR), "VRAM and GSPR registered");
    CHECK(reg.Has(ps2r::savestate::TAG_GSST), "GSST registered");
    for (const auto& spec : reg.Specs()) CHECK(spec.required, "all three sections REQUIRED");
    const std::vector<ps2r::savestate::Blob> blobs = reg.Capture();
    CHECK(blobs.size() == 3, "capture yields three sections");
    for (const auto& b : blobs) {
        if (b.tag == ps2r::savestate::TAG_VRAM) CHECK(b.payload.size() == GsSections::kVramBytes, "captured VRAM is 4 MiB");
        if (b.tag == ps2r::savestate::TAG_GSPR) CHECK(b.payload.size() == GsSections::kGsprBytes, "captured GSPR is 158 bytes");
        if (b.tag == ps2r::savestate::TAG_GSST) CHECK(b.version == GsSections::kGsstVersion && b.payload.size() == 406, "captured GSST is v3, 406 bytes (358 body + 12 + 36 tail)");
    }
}

// The drawing environment: the anchors' init-once tuple (TEXA ta0 128 / ta1 0,
// PRMODECONT 0 with PRMODE 0x150, FBA ctx0) plus per-context registers and the
// attribute latches round-trip into a fresh façade; a v3 payload carrying three
// latched vertex records (the strip window the reference saves) restores with
// the records skipped; a payload below the body is refused.
static void Gsst() {
    GsState src;
    GsRegisters& g = src.Regs();
    g.prim.type = 4; g.prim.texture = true; g.prim.alpha_blend = true; g.prim.use_uv = true; g.prim.context = 0;
    g.env.texa_ta0 = 128; g.env.texa_aem = false; g.env.texa_ta1 = 0;
    g.env.prmodecont = false; g.env.prmode_raw = 0x150; g.env.colclamp = true; g.env.pabe = false;
    g.env.scanmsk = 0; g.env.fogcol_r = 1; g.env.fogcol_g = 2; g.env.fogcol_b = 3; g.env.texture_dirty = false;
    g.ctx[0].fba = true; g.ctx[1].fba = false;
    g.ctx[0].xyoffset = {1792.0f, 1824.0f}; g.ctx[0].scissor = {0, 511, 0, 447};
    g.ctx[0].frame = {458752, 512, 0, 0}; g.ctx[0].zbuf = {1376256, 0, true};
    g.ctx[0].tex0 = {3276800, 128, 19, 7, 7, 1, 0, 1234, 0, 0, 0, 1};
    g.ctx[0].tex1 = {false, 2, true, 4, false, 1, 0x7ff};
    g.ctx[0].miptbp1 = 0x1122334455667788ull; g.ctx[0].miptbp2 = 0x99aabbccddeeff00ull;
    g.ctx[0].test = {true, 5, 5, 0, false, false, true, 2}; g.ctx[0].alpha = {0, 2, 0, 1, 0};
    g.ctx[1].alpha = {2, 0, 2, 1, 32}; g.ctx[1].clamp = {1, 1, 0, 0, 0, 0}; g.ctx[1].scissor = {0, 1024, 0, 1024};
    g.attr.r = 10; g.attr.g = 20; g.attr.b = 30; g.attr.a = 40; g.attr.s = 0.25f; g.attr.t = 0.5f; g.attr.q = 2.0f;
    g.attr.u = 7; g.attr.v = 9; g.attr.uv_written_since_st = true; g.attr.fog_coef = 77;
    g.dirty_regs = 0x3f;
    GsSections a(src);
    std::vector<uint8_t> gsst;
    a.SerializeGsst(gsst);
    CHECK(gsst.size() == 406, "GSST v3 serialized size 406");

    GsState dst;
    GsSections b(dst);
    std::string err;
    CHECK(b.RestoreGsst(gsst, GsSections::kGsstVersion, &err), err.c_str());
    const GsRegisters& d = dst.Regs();
    CHECK(d.prim.type == 4 && d.prim.texture && d.prim.alpha_blend && d.prim.use_uv && !d.prim.gouraud, "PRIM restored");
    CHECK(d.env.texa_ta0 == 128 && d.env.texa_ta1 == 0 && !d.env.texa_aem, "TEXA restored (ta0 128, ta1 0)");
    CHECK(!d.env.prmodecont && d.env.prmode_raw == 0x150, "PRMODECONT 0 / PRMODE 0x150 restored");
    CHECK(d.env.fogcol_r == 1 && d.env.fogcol_g == 2 && d.env.fogcol_b == 3 && !d.env.texture_dirty, "FOGCOL / texture_dirty restored");
    CHECK(d.ctx[0].fba && !d.ctx[1].fba, "FBA per context restored");
    CHECK(d.ctx[0].frame.fbp == 458752 && d.ctx[0].frame.fbw == 512 && d.ctx[0].zbuf.zbp == 1376256 && d.ctx[0].zbuf.zmsk, "FRAME / ZBUF restored");
    CHECK(d.ctx[0].xyoffset.x == 1792.0f && d.ctx[0].scissor.x1 == 511 && d.ctx[0].scissor.y1 == 447, "XYOFFSET / SCISSOR restored");
    CHECK(d.ctx[0].tex0.tbp == 3276800 && d.ctx[0].tex0.psm == 19 && d.ctx[0].tex0.cbp == 1234 && d.ctx[0].tex0.cld == 1, "TEX0 restored");
    CHECK(d.ctx[0].tex1.k == 0x7ff && d.ctx[0].tex1.mxl == 2 && d.ctx[0].tex1.mmag && d.ctx[0].tex1.mmin == 4, "TEX1 restored incl. the 12-bit K from the v3 tail");
    CHECK(d.ctx[0].miptbp1 == 0x1122334455667788ull && d.ctx[0].miptbp2 == 0x99aabbccddeeff00ull, "MIPTBP restored");
    CHECK(d.ctx[0].test.ate && d.ctx[0].test.atst == 5 && d.ctx[0].test.ztst == 2 && d.ctx[1].test.ztst == 1, "TEST restored");
    CHECK(d.ctx[1].alpha.a == 2 && d.ctx[1].alpha.fix == 32 && d.ctx[1].clamp.wms == 1 && d.ctx[1].scissor.x1 == 1024, "ALPHA / CLAMP ctx1 restored");
    CHECK(d.attr.r == 10 && d.attr.a == 40 && d.attr.s == 0.25f && d.attr.q == 2.0f && d.attr.u == 7 && d.attr.v == 9 &&
          d.attr.uv_written_since_st && d.attr.fog_coef == 77, "RGBAQ / ST / UV / FOG latches restored");
    CHECK(d.dirty_regs == 0x3f, "dirty_regs restored");
    std::vector<uint8_t> again;
    b.SerializeGsst(again);
    CHECK(again == gsst, "GSST re-serialized byte-equal");

    // A v3 payload with three latched vertex records in the first array (the
    // reference's strip window): 406 + 3 * 34 = 508 bytes, the anchors' size.
    std::vector<uint8_t> with_verts(gsst.begin(), gsst.begin() + 358 + 4);
    const uint32_t three = 3;
    ps2r::savestate::PutPod<uint32_t>(with_verts, three);
    with_verts.resize(with_verts.size() + 3 * GsSections::kGsstVertexRecBytes, 0xEE);
    with_verts.insert(with_verts.end(), gsst.begin() + 358 + 8, gsst.end());
    CHECK(with_verts.size() == 508, "anchor-shaped GSST is 508 bytes");
    GsState dst2;
    GsSections c(dst2);
    CHECK(c.RestoreGsst(with_verts, 3, &err), "GSST with latched vertex records restores");
    CHECK(dst2.Regs().ctx[0].tex1.k == 0x7ff && dst2.Regs().env.texa_ta0 == 128, "the v3 tail is read past the records");
    // A v1 body alone restores (K from the legacy byte, MIPTBP left at reset).
    std::vector<uint8_t> v1(gsst.begin(), gsst.begin() + 358);
    GsState dst3;
    GsSections e(dst3);
    CHECK(e.RestoreGsst(v1, 1, &err), "GSST v1 body restores");
    CHECK(dst3.Regs().ctx[0].tex1.k == 0xff && dst3.Regs().ctx[0].miptbp1 == 0, "v1: legacy K byte, MIPTBP at reset");
    std::vector<uint8_t> short_body(gsst.begin(), gsst.begin() + 357);
    CHECK(!e.RestoreGsst(short_body, 1, &err) && err.find("GSST") == 0, "GSST below the body refused");
    CHECK(!e.RestoreGsst(gsst, 4, &err), "GSST version 4 refused");
    CHECK(e.RestoredGsstCount() == 1, "refusals not counted");
}

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    if (which == "roundtrip" || which == "all") Roundtrip();
    if (which == "jobs" || which == "all") Jobs();
    if (which == "refuse" || which == "all") Refuse();
    if (which == "registry" || which == "all") Registry();
    if (which == "gsst" || which == "all") Gsst();
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
