// tests/gs/targets/test_targets.cpp -- G7 invariant tests for engine/gs/hle/targets
// over a CPU FakeBackend (GL-free). One sub-test per argv[1].
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/swizzle.h"
#include "engine/gs/format/texa_expand.h"
#include "engine/gs/hle/targets/render_target.h"
#include "engine/gs/hle/targets/scale_policy.h"
#include "engine/gs/hle/targets/render_target_cache.h"
#include "engine/gs/hle/targets/target_dims.h"
#include "engine/gs/hle/targets/target_refresher.h"
#include "engine/gs/hle/targets/vram_layout.h"
#include "engine/gs/vram/vram_image.h"
#include "tests/lib/fake_backend.h"

using namespace ps2r;
using namespace ps2r::gs;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; }  \
    } while (0)

namespace {

VramLayout FilledLayout() {
    VramLayout v;
    v.scene_accum_fbp = 0x230000;
    v.main_fb_fbp = 0x70000;
    v.fmv_stage_tbp = 0x150000;
    v.fog_lane_a_fbp = 0x280000;
    v.fog_lane_b_fbp = 0x2F0000;
    v.native_scanout_fbp_bytes = 0x70000;
    v.native_scanout_fbw_px = 512;
    v.native_scanout_psm = 0;
    return v;
}

DrawIr MakeDraw(uint32_t fbp, uint32_t fbw, uint8_t psm, uint32_t zbp = 0x150000, bool zte = true,
                bool zmsk = false) {
    DrawIr ir;
    ir.raster.frame.fbp = fbp;
    ir.raster.frame.fbw = fbw;
    ir.raster.frame.psm = psm;
    ir.raster.zbuf.zbp = zbp;
    ir.raster.zbuf.psm = 0x30;
    ir.raster.zbuf.zmsk = zmsk;
    ir.raster.test.zte = zte;
    ir.raster.test.ztst = 2;
    ir.raster.scissor = DrawIr::Scissor{0, 0, 1023, 1023};   // inclusive
    return ir;
}

// A committed draw whose triangle bbox equals its write rect.
render::DrawResult Drew(int x0, int y0, int x1, int y1, uint32_t fbmsk16 = 0) {
    render::DrawResult r;
    r.drew = true;
    r.write_rect = render::Rect{x0, y0, x1 - x0, y1 - y0};
    r.bbox_valid = true;
    r.bbox_tri[0] = float(x0); r.bbox_tri[1] = float(y0);
    r.bbox_tri[2] = float(x1); r.bbox_tri[3] = float(y1);
    for (int i = 0; i < 4; ++i) r.bbox_all[i] = r.bbox_tri[i];
    r.fbmsk16 = fbmsk16;
    return r;
}

struct World {
    test::FakeBackend be;
    VramImage vram;
    VramLayout layout = FilledLayout();
    ScaleSettings settings{1};
    ScalePolicy scale{settings, &layout};
    RenderTargetCache cache{be, vram, scale};
};

uint32_t VramWord(const VramImage& v, uint32_t addr) {
    uint8_t b[4];
    v.ReadView(addr, 4, b);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}
uint16_t VramHalf(const VramImage& v, uint32_t addr) {
    uint8_t b[2];
    v.ReadView(addr, 2, b);
    return uint16_t(b[0] | (b[1] << 8));
}
void FillColor(test::FakeBackend& be, RenderTarget& t, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (int y = 0; y < t.identity.height; ++y)
        for (int x = 0; x < t.identity.width; ++x) be.SetPixel(t.views.color, x, y, r, g, b, a);
}

// The grab -> deposit pixel sharing (consumer_b B1): a colour writeback of an
// attachment the caller just read in full takes its rows from the offered bytes --
// the same deposit, no backend readback; a different attachment, a rect outside
// the offer or a revoked / disabled offer read the GPU as before.
void OfferedPixels() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    FillColor(w.be, t, 0x10, 0x20, 0x30, 0x80);
    // The reference deposit (GPU read).
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 8, 64, 16), 1, 1);
    const int rb0 = w.be.readbacks;
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 1, nullptr, 0) == 64 * 8);
    CHECK(w.be.readbacks == rb0 + 1);
    const uint32_t ref = VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 8, 0, 1));
    CHECK(ref == 0x80302010u);
    // The same rows offered as a full-attachment grab (bottom-up rows, w*4 stride).
    std::vector<uint8_t> grab(64 * 64 * 4, 0);
    w.be.Readback(t.views.color, render::Rect{0, 0, 64, 64}, grab.data(), 64 * 4);
    FillColor(w.be, t, 0x11, 0x22, 0x33, 0x44);   // the GPU moved on; the offer is what the grab saw
    const int rb1 = w.be.readbacks;
    w.cache.Readback().OfferPixels(t.views.color, 64, 64, &grab);
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 8, 64, 16), 2, 2);
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 2, nullptr, 0) == 64 * 8);
    CHECK(w.be.readbacks == rb1);   // no GPU read
    CHECK(w.cache.Readback().OfferedReads() == 1);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 8, 0, 1)) == ref);   // the offered bytes
    // Revoked: the GPU is read again (the new content lands).
    w.cache.Readback().RevokeOffer();
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 8, 64, 16), 3, 3);
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 3, nullptr, 0) == 64 * 8);
    CHECK(w.be.readbacks == rb1 + 1);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 8, 0, 1)) == 0x44332211u);
    // Another attachment or a rect past the offer: not served by it.
    RenderTarget& u = w.cache.GetOrCreate(0x100000, 64, 64, 0x00, 0);
    FillColor(w.be, u, 1, 2, 3, 4);
    w.cache.Readback().OfferPixels(t.views.color, 64, 64, &grab);
    u.AfterDraw(MakeDraw(0x100000, 64, 0x00), Drew(0, 0, 64, 8), 4, 4);
    const int rb2 = w.be.readbacks;
    CHECK(w.cache.Readback().SyncRtToVram(w.be, u, w.vram, 4, nullptr, 0) == 64 * 8);
    CHECK(w.be.readbacks == rb2 + 1);
    w.cache.Readback().OfferPixels(t.views.color, 64, 8, &grab);   // the offer covers rows 0..8 only
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 8, 64, 16), 5, 5);
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 5, nullptr, 0) == 64 * 8);
    CHECK(w.be.readbacks == rb2 + 2);
    // Disabled: the offer is refused.
    w.cache.Readback().SetOfferEnabled(false);
    w.cache.Readback().OfferPixels(t.views.color, 64, 64, &grab);
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 8, 64, 16), 6, 6);
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 6, nullptr, 0) == 64 * 8);
    CHECK(w.be.readbacks == rb2 + 3);
    CHECK(w.cache.Readback().OfferedReads() == 1);
}

// Invariant 9: the colour writeback covers only drawn_since_sync rows, then nothing.
void Inv09() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    FillColor(w.be, t, 0x10, 0x20, 0x30, 0x80);
    // Nothing drawn -> nothing staged (a target that has not drawn since its sync writes back nothing).
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 1, nullptr, 0) == 0);
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 8, 64, 16), 1, 1);
    const uint32_t n = w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 1, nullptr, 0);
    CHECK(n == 64 * 8);
    CHECK(!t.coherence.HasDrawnSinceSync());
    // Row 8 landed (CT32 word, the alpha byte as stored -- the stored-alpha convention); row 0 did not.
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 8, 0, 1)) == 0x80302010u);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 0, 0, 1)) == 0u);
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 2, nullptr, 0) == 0);
    CHECK(w.vram.PendingPages() > 0);   // staged, never committed by this layer
    // Item 14: the entry test is the valid rect, not the content flag -- a rescue
    // of a target with valid content stages even with has_content cleared.
    t.coherence.has_content = false;
    LogicalRect all{0, 0, 64, 64};
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 1, &all, 0) == 64 * 8);
    t.coherence.has_content = true;
    // ... and a target whose valid rect is empty stages nothing even when drawn.
    t.coherence.valid_rect.Reset();
    t.coherence.drawn_since_sync.Union(0, 0, 64, 64);
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 1, nullptr, 0) == 0);
}

// Invariant 10: the Z writeback covers only z_drawn_since_sync rows.
void Inv10() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) w.be.SetDepth(t.views.depth, x, y, 0.5f);
    CHECK(w.cache.Readback().SyncZToVram(w.be, t, w.vram, 1) == 0);
    t.AfterDraw(MakeDraw(0x0, 64, 0x00, 0x20000), Drew(0, 32, 64, 40), 1, 1, 17);
    CHECK(w.cache.Readback().SyncZToVram(w.be, t, w.vram, 1) == 64 * 8);
    CHECK(!t.coherence.HasZDrawnSinceSync());
    // A Z WRITE stamps its draw ordinal and its frame together (the Z-source refresh
    // gates on the frame, the depth match orders by the seq).
    CHECK(t.coherence.last_z_seq == 17 && t.coherence.last_z_write_frame == 1);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::Z32, 5, 33, 0x20000 / 256, 1)) == 0x80000000u);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::Z32, 5, 0, 0x20000 / 256, 1)) == 0u);
    // A ZMSK draw does not extend the Z rect ...
    t.AfterDraw(MakeDraw(0x0, 64, 0x00, 0x20000, true, true), Drew(0, 0, 64, 8), 2, 2, 18);
    CHECK(!t.coherence.HasZDrawnSinceSync());
    // ... but the Z-write history stays: last_z_wrote is sticky (item 2); the
    // Z-tested-only draw does not restamp the write seq / frame.
    CHECK(t.coherence.last_z_wrote);
    CHECK(t.coherence.last_z_seq == 17 && t.coherence.last_z_write_frame == 1);
    // A draw without Z test leaves the ZBUF pairing alone (item 2).
    t.AfterDraw(MakeDraw(0x0, 64, 0x00, 0x30000, false, false), Drew(0, 0, 64, 8), 3, 3);
    CHECK(t.coherence.last_zbp == 0x20000);
    // The ZBUF PSM field is normalised to the native Z format (0x30 | low nibble).
    t.coherence.last_zpsm = 0x00;   // as a raw 4-bit field would arrive
    t.AfterDraw(MakeDraw(0x0, 64, 0x00, 0x20000), Drew(0, 8, 64, 16), 4, 4);
    t.coherence.last_zpsm = 0x00;
    CHECK(w.cache.Readback().SyncZToVram(w.be, t, w.vram, 1) == 64 * 8);
}

// Invariant 20: Z rows land at the PSMZ32 addresses, which differ from PSMCT32's.
void Inv20() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    w.be.SetDepth(t.views.depth, 9, 3, 0.25f);
    t.AfterDraw(MakeDraw(0x0, 64, 0x00, 0x20000), Drew(0, 0, 64, 32), 1, 1);
    w.cache.Readback().SyncZToVram(w.be, t, w.vram, 1);
    const uint32_t za = PsmLayout::PixelByteAddress(Psm::Z32, 9, 3, 0x20000 / 256, 1);
    const uint32_t ca = PsmLayout::PixelByteAddress(Psm::CT32, 9, 3, 0x20000 / 256, 1);
    CHECK(za != ca);
    CHECK(VramWord(w.vram, za) == 0x40000000u);
}

// Item 12: the narrower Z formats saturate (Z16 -> 0xFFFF, Z24 -> 0xFFFFFF).
void ZSaturate() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) w.be.SetDepth(t.views.depth, x, y, 0.5f);   // z = 0x80000000
    DrawIr d = MakeDraw(0x0, 64, 0x00, 0x20000);
    d.raster.zbuf.psm = 0x32;   // PSMZ16
    t.AfterDraw(d, Drew(0, 0, 64, 64), 1, 1);
    CHECK(w.cache.Readback().SyncZToVram(w.be, t, w.vram, 1) == 64 * 64);
    CHECK(VramHalf(w.vram, PsmLayout::PixelByteAddress(Psm::Z16, 7, 5, 0x20000 / 256, 1)) == 0xFFFF);
    d.raster.zbuf.psm = 0x31;   // PSMZ24
    d.raster.zbuf.zbp = 0x40000;
    t.AfterDraw(d, Drew(0, 0, 64, 64), 2, 2);
    CHECK(w.cache.Readback().SyncZToVram(w.be, t, w.vram, 2) == 64 * 64);
    CHECK((VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::Z24, 7, 5, 0x40000 / 256, 1)) & 0x00FFFFFFu) == 0x00FFFFFFu);
}

// Invariant 11: a 16-bit FRAME over a target with 32-bit content routes to the companion.
void Inv11() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    render::DrawParams p;
    t.PrepareDraw(w.be, MakeDraw(0x0, 64, 0x02), 0, 0x70000, &p);
    CHECK(!p.alt16_route);           // no 32-bit history yet: main route
    CHECK(!t.views.HasAlt16());
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 64), 1, 1);
    CHECK(t.identity.has_32bit_content);
    t.PrepareDraw(w.be, MakeDraw(0x0, 64, 0x02), 0, 0x70000, &p);
    CHECK(p.alt16_route);
    CHECK(t.views.HasAlt16());
    CHECK(p.alt16.id == t.views.alt16.id);
    CHECK(p.color.id == t.views.color.id);
    t.PrepareDraw(w.be, MakeDraw(0x0, 64, 0x00), 0, 0x70000, &p);
    CHECK(!p.alt16_route);           // a 32-bit FRAME always draws the main attachment
    // Item 1: a companion-routed draw describes the companion, not the main image.
    RenderTarget& u = w.cache.GetOrCreate(0x100000, 64, 64, 0x00, 0);
    u.identity.has_32bit_content = true;   // as a carried tag would leave it
    render::DrawResult ar = Drew(0, 0, 64, 64);
    ar.alt16_route = true;
    u.AfterDraw(MakeDraw(0x100000, 128, 0x02), ar, 1, 1);
    CHECK(!u.coherence.has_content);            // main image untouched
    CHECK(u.views.alt16_has_content);
    CHECK(u.identity.render_psm != 0x02 && u.identity.render_fbw != 128);
    CHECK(u.coherence.valid_rect.Empty() && !u.coherence.HasDrawnSinceSync());
    CHECK(u.identity.was_16bit);                // the frame-format history still counts
    // ... and a main-routed 16-bit draw retags it.
    u.AfterDraw(MakeDraw(0x100000, 128, 0x02), Drew(0, 0, 64, 64), 2, 2);
    CHECK(u.coherence.has_content && u.identity.render_psm == 0x02 && u.identity.render_fbw == 128);
    // Item 2: the 32-bit / 16-bit history follows the frame format's storage width
    // (Z formats included).
    RenderTarget& z = w.cache.GetOrCreate(0x200000, 64, 64, 0x00, 0);
    z.AfterDraw(MakeDraw(0x200000, 64, 0x30), Drew(0, 0, 64, 64), 1, 1);   // PSMZ32 frame
    CHECK(z.identity.was_32bit && !z.identity.was_16bit);
    CHECK(!z.identity.has_32bit_content);        // only the colour classes set the alt16 key
    z.AfterDraw(MakeDraw(0x200000, 64, 0x3A), Drew(0, 0, 64, 64), 2, 2);   // PSMZ16S frame
    CHECK(z.identity.was_16bit);
}

// Invariant 12: main-route writes make the companion stale over that rect; the
// refresh folds main through 5551 and clears the rect. Item 3: companion-route
// draws grow alt16_drawn; a covering main write drops the companion's content.
void Inv12() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 64), 1, 1);
    t.EnsureAlt16(w.be);
    CHECK(!t.coherence.alt16_stale.Empty());
    t.RefreshAlt16FromMain(w.be);
    CHECK(t.coherence.alt16_stale.Empty());
    CHECK(t.coherence.alt_author_seq == 1);      // the mirror authors the companion (item 4)
    w.be.SetPixel(t.views.color, 5, 5, 0xFF, 0x83, 0x07, 0xFF);
    render::DrawResult r = Drew(0, 4, 64, 8);
    r.alt16_route = false;
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), r, 2, 2);
    CHECK(t.coherence.alt16_stale.y0 == 4 && t.coherence.alt16_stale.y1 == 8);
    t.RefreshAlt16FromMain(w.be);
    const uint8_t* px = &w.be.At(t.views.alt16).rgba[(5 * 64 + 5) * 4];
    CHECK(px[0] == 0xFF && px[1] == 0x84 && px[2] == 0x00 && px[3] == 0xFF);   // 5-bit fold, replicated
    CHECK(t.coherence.alt16_stale.Empty());
    CHECK(t.coherence.alt_author_seq == 2);
    // The GL program's arithmetic (v5 = (c*255+0.5)>>3; e8 = (v5<<3)|(v5>>2); a = c.a*255+0.5 >= 128)
    // equals the CPU fold for every channel value.
    bool fold_ok = true;
    for (uint32_t v = 0; v < 256; ++v) {
        const uint32_t v5 = v >> 3, e8 = (v5 << 3) | (v5 >> 2);
        const uint32_t word = v | (v << 8) | (v << 16) | (v << 24);
        const uint32_t out = TexaExpand::Expand16To32(TexaExpand::Truncate32To16(word));
        const uint32_t a = (v >= 128) ? 0xFFu : 0u;
        if (out != (e8 | (e8 << 8) | (e8 << 16) | (a << 24))) fold_ok = false;
    }
    CHECK(fold_ok);
    // Companion-route writes do not stale the companion; they grow alt16_drawn.
    render::DrawResult r2 = Drew(8, 8, 24, 24);
    r2.alt16_route = true;
    t.AfterDraw(MakeDraw(0x0, 64, 0x02), r2, 3, 3);
    CHECK(t.coherence.alt16_stale.Empty());
    CHECK(t.coherence.alt16_drawn.x0 == 8 && t.coherence.alt16_drawn.y1 == 24);
    CHECK(t.views.alt16_has_content);
    // A main write that does not cover the companion's deposits keeps them ...
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 16), 4, 4);
    CHECK(t.views.alt16_has_content && t.coherence.Alt16HasDrawn());
    // ... a covering one drops them (rebuild from main on the next use).
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 64), 5, 5);
    CHECK(!t.views.alt16_has_content && !t.coherence.Alt16HasDrawn());
    // The stale rect beyond the target is clamped at refresh time and consumed.
    t.coherence.alt16_stale.Reset();
    t.coherence.alt16_stale.Union(-8, 60, 80, 90);
    t.RefreshAlt16FromMain(w.be);
    CHECK(t.coherence.alt16_stale.Empty());
    // E251: a backend with the kCopyPack5551 pass refreshes through CopyRect (no
    // Readback, no Upload) and the companion holds the SAME bytes the CPU route
    // produced -- a pseudo-random main image, both routes over the same stale rect.
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const uint32_t h = static_cast<uint32_t>(x * 73856093) ^ static_cast<uint32_t>(y * 19349663) ^ 0x5bd1e995u;
            w.be.SetPixel(t.views.color, x, y, uint8_t(h), uint8_t(h >> 8), uint8_t(h >> 16), uint8_t(h >> 24));
        }
    t.coherence.alt16_stale.Reset();
    t.coherence.alt16_stale.Union(3, 5, 61, 59);
    const int readbacks_before = w.be.readbacks, uploads_before = w.be.uploads;
    t.RefreshAlt16FromMain(w.be);                       // the CPU route
    const std::vector<uint8_t> cpu = w.be.At(t.views.alt16).rgba;
    CHECK(w.be.readbacks == readbacks_before + 1 && w.be.uploads == uploads_before + 1);
    CHECK(w.be.pack_copies == 0);
    const uint64_t seq_cpu = t.coherence.alt_author_seq;
    for (uint8_t& b : w.be.At(t.views.alt16).rgba) b = 0xA5;   // scramble the companion
    w.be.copy_pack5551 = true;
    t.coherence.alt16_stale.Union(3, 5, 61, 59);
    t.RefreshAlt16FromMain(w.be);                       // the GPU route
    CHECK(w.be.pack_copies == 1);
    CHECK(w.be.readbacks == readbacks_before + 1 && w.be.uploads == uploads_before + 1);   // no readback / upload
    CHECK(t.coherence.alt_author_seq == seq_cpu + 1 && t.views.alt16_has_content);
    const std::vector<uint8_t>& gpu = w.be.At(t.views.alt16).rgba;
    bool same = true;
    for (int y = 5; y < 59 && same; ++y)
        for (int x = 3; x < 61; ++x)
            if (std::memcmp(&cpu[(size_t(y) * 64 + x) * 4], &gpu[(size_t(y) * 64 + x) * 4], 4) != 0) { same = false; break; }
    CHECK(same);
    bool outside_untouched = true;
    for (int x = 0; x < 64; ++x)
        if (gpu[(size_t(2) * 64 + x) * 4] != 0xA5) outside_untouched = false;   // row 2: outside the stale rect
    CHECK(outside_untouched);
    w.be.copy_pack5551 = false;
}

// Item 3: the main route's valid rect is the UNCLAMPED triangle bbox, its drawn
// rect the bbox within the scissor.
void DrawnRects() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    DrawIr d = MakeDraw(0x0, 64, 0x00);
    d.raster.scissor = DrawIr::Scissor{0, 0, 63, 63};
    render::DrawResult r;
    r.drew = true;
    r.bbox_valid = true;
    r.bbox_tri[0] = -4.5f; r.bbox_tri[1] = 2.0f; r.bbox_tri[2] = 70.2f; r.bbox_tri[3] = 10.0f;
    r.bbox_all[0] = -4.5f; r.bbox_all[1] = 2.0f; r.bbox_all[2] = 70.2f; r.bbox_all[3] = 40.0f;   // tail verts
    r.write_rect = render::Rect{0, 2, 64, 38};   // bbox_all ∩ scissor ∩ target
    t.AfterDraw(d, r, 1, 1);
    const LogicalRect& v = t.coherence.valid_rect;
    CHECK(v.x0 == -4 && v.y0 == 2 && v.x1 == 71 && v.y1 == 10);          // (int) trunc, ceil, no clamp
    const LogicalRect& ds = t.coherence.drawn_since_sync;
    CHECK(ds.x0 == 0 && ds.y0 == 2 && ds.x1 == 64 && ds.y1 == 10);        // tri bbox ∩ scissor
    CHECK(t.coherence.z_drawn_since_sync.y1 == 40);                        // Z: all vertices
    // The Cd-copy dirty union follows the drawn rect (both routes).
    CHECK(t.coherence.cd_copy.dirty.Union().y0 == 2 && t.coherence.cd_copy.dirty.Union().y1 == 10);
    // A draw entirely outside the scissor grows valid_rect only.
    render::DrawResult r2 = Drew(100, 100, 120, 120);
    r2.write_rect = render::Rect{};
    t.AfterDraw(d, r2, 2, 2);
    CHECK(v.x1 == 120 && ds.x1 == 64);
    // A DEGENERATE (zero-area) triangle bbox still grows valid_rect by min/max
    // once it is non-empty (no zero-area guard there), while the drawn rect,
    // the Cd dirty union and the companion rects ignore it.
    {
        RenderTarget& g = w.cache.GetOrCreate(0x70000, 64, 64, 0x00, 0);
        DrawIr gd = MakeDraw(0x70000, 64, 0x00);
        g.AfterDraw(gd, Drew(10, 10, 20, 20), 3, 3);
        render::DrawResult dg;
        dg.drew = true;
        dg.bbox_valid = true;
        dg.bbox_tri[0] = 5.0f; dg.bbox_tri[1] = 5.0f; dg.bbox_tri[2] = 5.0f; dg.bbox_tri[3] = 30.0f;
        for (int i = 0; i < 4; ++i) dg.bbox_all[i] = dg.bbox_tri[i];
        dg.write_rect = render::Rect{5, 5, 0, 25};
        const int cd_y1 = g.coherence.cd_copy.dirty.Union().y1;
        g.AfterDraw(gd, dg, 4, 4);
        const LogicalRect& gv = g.coherence.valid_rect;
        CHECK(gv.x0 == 5 && gv.y0 == 5 && gv.x1 == 20 && gv.y1 == 30);
        const LogicalRect& gds = g.coherence.drawn_since_sync;
        CHECK(gds.x0 == 10 && gds.y0 == 10 && gds.x1 == 20 && gds.y1 == 20);
        CHECK(g.coherence.cd_copy.dirty.Union().y1 == cd_y1);
        // On an EMPTY valid rect a degenerate assignment leaves it empty.
        RenderTarget& e = w.cache.GetOrCreate(0x100000, 64, 64, 0x00, 0);
        e.AfterDraw(MakeDraw(0x100000, 64, 0x00), dg, 5, 5);
        CHECK(!e.coherence.HasValidContent() && e.coherence.valid_rect.y1 == 30);
    }
    // LogicalRect: Union keeps its degenerate guard; ExpandToInclude has none.
    {
        LogicalRect u{10, 10, 20, 20};
        u.Union(5, 5, 5, 30);
        CHECK(u.x0 == 10 && u.y1 == 20);
        u.ExpandToInclude(5, 5, 5, 30);
        CHECK(u.x0 == 5 && u.y0 == 5 && u.x1 == 20 && u.y1 == 30);
    }
    // The writeback clamps the drawn rows to the target.
    t.coherence.drawn_since_sync.Union(-10, -10, 200, 200);
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 1, nullptr, 0) == 64 * 64);
}

// Item 4: per-view author counters and lane records.
void AuthorSeqs() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x280000, 64, 64, 0x00, 0);
    t.AfterDraw(MakeDraw(0x280000, 64, 0x00), Drew(0, 0, 64, 64), 1, 1);
    t.AfterDraw(MakeDraw(0x280000, 64, 0x00), Drew(0, 0, 64, 64), 1, 2);
    CHECK(t.coherence.main_author_seq == 2 && t.coherence.alt_author_seq == 0);
    // A Z16-view masked lane draw on the main view records itself there.
    DrawIr lane = MakeDraw(0x280000, 64, 0x02);
    lane.tex_enabled = true;
    lane.tex.psm = 0x32;
    t.AfterDraw(lane, Drew(0, 0, 64, 64, RenderTarget::kIndexLaneKeep16), 7, 3);
    CHECK(t.coherence.main_author_seq == 3);
    CHECK(t.coherence.lane_main.seq == 3 && t.coherence.lane_main.frame == 7);
    CHECK(t.coherence.lane_alt.seq == 0 && t.coherence.lane_alt.frame == 0);
    // The same draw through the companion records the alt lane with the alt counter.
    render::DrawResult ar = Drew(0, 0, 64, 64, RenderTarget::kIndexLaneKeep16);
    ar.alt16_route = true;
    t.AfterDraw(lane, ar, 8, 4);
    CHECK(t.coherence.alt_author_seq == 1 && t.coherence.main_author_seq == 3);
    CHECK(t.coherence.lane_alt.seq == 1 && t.coherence.lane_alt.frame == 8);
    // A later main-view author invalidates the main lane proof (seq != counter).
    t.AfterDraw(MakeDraw(0x280000, 64, 0x00), Drew(0, 0, 64, 64), 9, 5);
    CHECK(t.coherence.lane_main.seq != t.coherence.main_author_seq);
    // The mirror -> attachment refresh authors the main view.
    TargetRefresher rf(w.be, w.vram, w.cache);
    rf.RefreshFromVram(t, 0, 64, 0);
    CHECK(t.coherence.main_author_seq == 5);
    // A non-lane 16-bit draw (full write) does not record a lane.
    t.coherence.lane_main = CoherenceState::LaneRecord();
    t.AfterDraw(lane, Drew(0, 0, 64, 64, 0), 10, 6);
    CHECK(t.coherence.lane_main.seq == 0);
}

// Invariants 13 / 14: grow-only, a smaller or retagged request keeps the surfaces
// and their bytes; a grow carries the class tags. Item 13: a fresh target has no
// PSM / FBW tag; the rescue stride narrows only to a page-multiple FBW.
void Inv13_14() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 256, 224, 0x00, 0);
    CHECK(t.identity.render_psm == 0 && t.identity.render_fbw == 0);
    const uint32_t color_id = t.views.color.id;
    w.be.SetPixel(t.views.color, 1, 1, 1, 2, 3, 4);
    t.AfterDraw(MakeDraw(0x0, 256, 0x00), Drew(0, 0, 256, 224), 1, 1);
    CHECK(t.identity.render_fbw == 256);
    RenderTarget& t2 = w.cache.GetOrCreate(0x0, 128, 128, 0x02, 0);   // smaller + retag
    CHECK(&t2 == &t && t2.views.color.id == color_id);
    CHECK(t2.identity.width == 256 && t2.identity.height == 224);
    CHECK(w.be.At(t2.views.color).rgba[(64 * 256 * 4) * 0 + (256 + 1) * 4 + 2] == 3);
    CHECK(w.cache.Count() == 1);
    RenderTarget& t3 = w.cache.GetOrCreate(0x0, 512, 448, 0x02, 0);   // grow
    CHECK(t3.identity.width == 512 && t3.identity.height == 448);
    CHECK(t3.views.color.id != color_id);
    CHECK(t3.identity.has_32bit_content && t3.identity.was_32bit);      // invariant 14
    CHECK(t3.identity.render_psm == 0x00 && t3.identity.render_fbw == 256);
    CHECK(w.vram.PendingPages() > 0);   // the rescue deposit ran before the re-create
    CHECK(w.cache.Count() == 1);
    CHECK(RenderTargetCache::RescueStride(256) == 256);
    CHECK(RenderTargetCache::RescueStride(100) == 0 && RenderTargetCache::RescueStride(32) == 0);
    // The rescue of a 100-wide-FBW target encodes at the target width: pixel (70, 0)
    // lands at the width-stride address, not at the FBW-100 address. (A rescue
    // keeps every page this target has not authored, so a first writeback marks them.)
    World w2;
    RenderTarget& n = w2.cache.GetOrCreate(0x0, 128, 32, 0x00, 0);
    n.AfterDraw(MakeDraw(0x0, 100, 0x00), Drew(0, 0, 128, 32), 1, 1);
    w2.cache.Readback().SyncRtToVram(w2.be, n, w2.vram, 1, nullptr, 0);
    w2.be.SetPixel(n.views.color, 70, 0, 0x11, 0x22, 0x33, 0x80);
    w2.cache.GetOrCreate(0x0, 256, 32, 0x00, 0);   // grow -> rescue
    CHECK(VramWord(w2.vram, PsmLayout::PixelByteAddress(Psm::CT32, 70, 0, 0, 2)) == 0x80332211u);
    // A never-deposited target's rescue brackets its pages but writes no bytes
    // (cold pages are not this target's).
    World w3;
    RenderTarget& c = w3.cache.GetOrCreate(0x0, 64, 32, 0x00, 0);
    FillColor(w3.be, c, 0x10, 0x20, 0x30, 0xFF);
    c.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 32), 1, 1);
    w3.cache.GetOrCreate(0x0, 128, 32, 0x00, 0);
    CHECK(w3.vram.PendingPages() == 1);
    CHECK(VramWord(w3.vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 3, 0, 1)) == 0u);
}

// Item 13: SyncAll is colour only at batch epoch 0 and skips undrawn targets.
void SyncAllJob() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    FillColor(w.be, t, 0x10, 0x20, 0x30, 0xFF);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) w.be.SetDepth(t.views.depth, x, y, 0.5f);
    // An EE write settled on the target's page: with epoch 0 every stamped page
    // keeps its CPU bytes.
    const uint32_t ee_addr = PsmLayout::PixelByteAddress(Psm::CT32, 3, 8, 0, 1);
    const uint8_t ee[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    w.vram.WriteStream(ee_addr, ee, 4);
    w.vram.SettleWrites();
    t.AfterDraw(MakeDraw(0x0, 64, 0x00, 0x20000), Drew(0, 0, 64, 64), 1, 5);
    w.cache.SyncAll();
    CHECK(VramWord(w.vram, ee_addr) == 0xDDCCBBAAu);              // page kept (epoch 0)
    CHECK(!t.coherence.HasDrawnSinceSync());                        // colour consumed
    CHECK(t.coherence.HasZDrawnSinceSync());                        // Z not part of the job
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::Z32, 5, 33, 0x20000 / 256, 1)) == 0u);
    // A target with content but nothing drawn since its sync is skipped.
    const uint32_t staged_before = w.vram.PendingPages();
    w.cache.SyncAll();
    CHECK(w.vram.PendingPages() == staged_before);
    // A 16-bit target's deferred entry goes with its drawn rect: the job leaves
    // the ledger empty (the OLD sync tail's RemovePendingRTSync).
    RenderTarget& s = w.cache.GetOrCreate(0x200000, 64, 64, 0x02, 0);
    FillColor(w.be, s, 0xFF, 0, 0, 0xFF);
    DrawIr d16 = MakeDraw(0x200000, 64, 0x02);
    s.AfterDraw(d16, Drew(0, 0, 64, 64), 2, 9);
    w.cache.AfterDrawTail(s, d16, Drew(0, 0, 64, 64), 64, 9);
    CHECK(w.cache.Ledger().Has(0x200000));
    w.cache.SyncAll();
    CHECK(w.cache.Ledger().Empty() && !s.coherence.HasDrawnSinceSync());
    CHECK(VramHalf(w.vram, PsmLayout::PixelByteAddress(Psm::CT16, 0, 0, 0x200000 / 256, 1)) == 0x801F);
    CHECK(w.vram.Page(0x200000 / 8192).shadow_epoch == 0);   // batch epoch 0, not the draw's
}

// Invariant 23: scale 1 is the identity for every target.
void Inv23() {
    VramLayout l = FilledLayout();
    ScalePolicy p(ScaleSettings{1}, &l);
    CHECK(p.Factor() == 1 && !p.On());
    CHECK(p.TargetScale(0x0) == 1 && p.TargetScale(0x230000) == 1 && p.TargetScale(0x280000) == 1);
    CHECK(ScalePolicy(ScaleSettings{0}, &l).Factor() == 1);
    CHECK(ScalePolicy(ScaleSettings{9}, &l).Factor() == 4);
}

// Invariant 24: the title's scene accumulator and fog lanes stay native at scale 2;
// an empty layout pins nothing.
void Inv24() {
    VramLayout l = FilledLayout();
    ScalePolicy p(ScaleSettings{2}, &l);
    CHECK(p.TargetScale(0x230000) == 1 && p.TargetScale(0x280000) == 1 && p.TargetScale(0x2F0000) == 1);
    CHECK(p.TargetScale(0x70000) == 2 && p.TargetScale(0x0) == 2);
    VramLayout e;
    ScalePolicy q(ScaleSettings{2}, &e);
    CHECK(q.TargetScale(0x230000) == 2 && q.TargetScale(0x280000) == 2);
}

// Invariant 35: a masked 16-bit writeback preserves the kept 5551 bits from VRAM.
void Inv35() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x02, 0);
    // Pre-existing VRAM halfword at (3, 2): all ones.
    const uint32_t addr = PsmLayout::PixelByteAddress(Psm::CT16, 3, 2, 0, 1);
    const uint8_t ones[2] = {0xFF, 0xFF};
    w.vram.WriteStream(addr, ones, 2);
    w.vram.SettleWrites();
    w.be.SetPixel(t.views.color, 3, 2, 0x00, 0x00, 0x00, 0x00);   // GPU wrote black, alpha 0
    // The draw kept the low byte (index-lane mask).
    t.AfterDraw(MakeDraw(0x0, 64, 0x02), Drew(0, 0, 64, 64, RenderTarget::kIndexLaneKeep16), 1, 1);
    CHECK(t.coherence.sync_keep_mask16 == 0x00FF);
    // A newer EE write must not be clobbered: the batch epoch is the settled one.
    w.cache.Readback().SyncRtToVram(w.be, t, w.vram, w.vram.UploadEpoch(), nullptr, 0);
    CHECK(VramHalf(w.vram, addr) == 0x00FF);   // high byte written (0), low byte kept
    // Full write: nothing kept.
    t.AfterDraw(MakeDraw(0x0, 64, 0x02), Drew(0, 0, 64, 64, 0), 2, 2);
    w.cache.Readback().SyncRtToVram(w.be, t, w.vram, w.vram.UploadEpoch(), nullptr, 0);
    CHECK(VramHalf(w.vram, addr) == 0x0000);
    // A 32-bit draw resets the keep mask.
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 64, 0x00FF), 3, 3);
    CHECK(t.coherence.sync_keep_mask16 == 0);
}

// Item 10: the per-page "EE wrote later" veto applies to the CT32 route too; an
// uploaded texture with an upload epoch >= the batch protects its byte range
// (CT16: only under a full-write mask); a rescue keeps pages this target did not author.
void EncodeVetoes() {
    World w;
    TargetRefresher rf(w.be, w.vram, w.cache);
    RenderTarget& t = w.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    FillColor(w.be, t, 0x10, 0x20, 0x30, 0xFF);
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 64), 1, 1);
    // (a) page_newer on CT32: the EE writes AFTER the draw's epoch (2 > 1).
    const uint32_t ee_addr = PsmLayout::PixelByteAddress(Psm::CT32, 3, 8, 0, 1);
    const uint8_t ee[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    w.vram.WriteStream(ee_addr, ee, 4);
    w.vram.SettleWrites();                                           // upload epoch 1
    w.cache.Readback().SyncRtToVram(w.be, t, w.vram, /*batch_epoch=*/0, nullptr, 0);
    CHECK(VramWord(w.vram, ee_addr) == 0xDDCCBBAAu);                 // kept
    // (b) batch newer than the page: the GPU is the last author.
    t.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 64), 2, 2);
    w.cache.Readback().SyncRtToVram(w.be, t, w.vram, /*batch_epoch=*/5, nullptr, 0);
    CHECK(VramWord(w.vram, ee_addr) == 0xFF302010u);   // the stored-alpha convention: the GL alpha byte 0xFF deposits as 0xFF
    // (c) protected range: an uploaded texture over rows 32.. of the footprint whose
    // page epoch (fresh settle -> 2) is >= the batch epoch (2) keeps its bytes.
    World w2;
    TargetRefresher rf2(w2.be, w2.vram, w2.cache);
    RenderTarget& u = w2.cache.GetOrCreate(0x0, 64, 64, 0x00, 0);
    FillColor(w2.be, u, 0x10, 0x20, 0x30, 0xFF);
    const uint32_t up_addr = 0x1000;                                 // rows 16.. of a 64-wide CT32 page
    std::vector<uint8_t> px(16 * 16 * 4, 0x77);
    const uint8_t marker[4] = {1, 2, 3, 4};
    w2.vram.WriteStream(up_addr + 0x40, marker, 4);
    w2.vram.SettleWrites();                                          // epoch 1
    rf2.UploadDirect(up_addr, 16, 16, px.data(), 0x400);            // footprint [0x1000, 0x1400)
    u.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 64), 1, 1);
    w2.cache.Readback().SyncRtToVram(w2.be, u, w2.vram, /*batch_epoch=*/1, nullptr, 0);
    CHECK(VramWord(w2.vram, up_addr + 0x40) == 0x04030201u);         // protected (epoch 1 >= 1)
    CHECK(VramWord(w2.vram, 0x0800) == 0xFF302010u);                 // outside the range: encoded
    // A later batch (2 > page epoch 1): no protection.
    u.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 64), 2, 2);
    w2.cache.Readback().SyncRtToVram(w2.be, u, w2.vram, /*batch_epoch=*/2, nullptr, 0);
    CHECK(VramWord(w2.vram, up_addr + 0x40) == 0xFF302010u);
    // Evicting the upload removes the protection source.
    rf2.EvictUploadedInRange(up_addr, 0x400);
    CHECK(w2.cache.Readback().UploadedSpans().empty());
    // (d) CT16 with a partial keep mask ignores protected ranges (page_newer only).
    World w3;
    TargetRefresher rf3(w3.be, w3.vram, w3.cache);
    RenderTarget& c = w3.cache.GetOrCreate(0x0, 64, 64, 0x02, 0);
    FillColor(w3.be, c, 0x00, 0x00, 0x00, 0x00);
    const uint8_t ones[2] = {0xFF, 0xFF};
    const uint32_t haddr = PsmLayout::PixelByteAddress(Psm::CT16, 3, 40, 0, 1);
    w3.vram.WriteStream(haddr, ones, 2);
    w3.vram.SettleWrites();                                          // epoch 1
    rf3.UploadDirect(haddr & ~0xFFu, 8, 8, px.data(), 0x100);
    c.AfterDraw(MakeDraw(0x0, 64, 0x02), Drew(0, 0, 64, 64, RenderTarget::kIndexLaneKeep16), 1, 1);
    w3.cache.Readback().SyncRtToVram(w3.be, c, w3.vram, 1, nullptr, 0);
    CHECK(VramHalf(w3.vram, haddr) == 0x00FF);                       // masked merge, not protected
    c.AfterDraw(MakeDraw(0x0, 64, 0x02), Drew(0, 0, 64, 64, 0), 2, 2);
    w3.cache.Readback().SyncRtToVram(w3.be, c, w3.vram, 1, nullptr, 0);
    CHECK(VramHalf(w3.vram, haddr) == 0x00FF);                       // full write: protected, kept
    // (e) rescue authorship veto: a page this target never deposited into keeps
    // its mirror bytes; a page it authored is overwritten.
    World w4;
    RenderTarget& r = w4.cache.GetOrCreate(0x0, 64, 128, 0x00, 0);   // two CT32 pages
    FillColor(w4.be, r, 0x10, 0x20, 0x30, 0xFF);
    r.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 32), 1, 1);  // page 0 only
    w4.cache.Readback().SyncRtToVram(w4.be, r, w4.vram, 1, nullptr, 0);
    r.AfterDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 64, 64, 128), 2, 2);   // valid covers page 1 too
    LogicalRect all{0, 0, 64, 128};
    w4.cache.Readback().SyncRtToVram(w4.be, r, w4.vram, 1, &all, 0);
    CHECK(VramWord(w4.vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 8, 0, 1)) == 0xFF302010u);
    CHECK(VramWord(w4.vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 72, 0, 1)) == 0u);   // page 1 vetoed
}

// Item 11: the touched pages are those of the LINEAR row span; a swizzled pixel
// on another footprint page reaches the shadow only when that page is pending.
void DepositSpan() {
    // rows [0, 8) x 128 px x 4 B = [0, 0x1000): page 0 only; x >= 64 lives on page 1.
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x0, 128, 32, 0x00, 0);   // fbw 128: two pages per row
    FillColor(w.be, t, 0x10, 0x20, 0x30, 0xFF);
    t.AfterDraw(MakeDraw(0x0, 128, 0x00), Drew(0, 0, 128, 8), 1, 1);
    CHECK(w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 1, nullptr, 0) == 128 * 8);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 10, 2, 0, 2)) == 0xFF302010u);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 70, 2, 0, 2)) == 0u);   // not staged
    CHECK(w.vram.PendingPages() == 1);
    // With page 1 already pending (a span that reaches it), the stray pixels land.
    World w2;
    RenderTarget& u = w2.cache.GetOrCreate(0x0, 128, 32, 0x00, 0);
    FillColor(w2.be, u, 0x10, 0x20, 0x30, 0xFF);
    u.AfterDraw(MakeDraw(0x0, 128, 0x00), Drew(0, 8, 128, 32), 1, 1);   // span [0x1000, 0x4000): pages 0, 1
    CHECK(w2.cache.Readback().SyncRtToVram(w2.be, u, w2.vram, 1, nullptr, 0) == 128 * 24);
    CHECK(w2.vram.PendingPages() == 2);
    CHECK(VramWord(w2.vram, PsmLayout::PixelByteAddress(Psm::CT32, 70, 2, 0, 2)) == 0u);   // row 2 not yet drawn
    u.AfterDraw(MakeDraw(0x0, 128, 0x00), Drew(0, 0, 128, 8), 2, 2);    // span page 0; page 1 pending
    CHECK(w2.cache.Readback().SyncRtToVram(w2.be, u, w2.vram, 1, nullptr, 0) == 128 * 8);
    CHECK(VramWord(w2.vram, PsmLayout::PixelByteAddress(Psm::CT32, 70, 2, 0, 2)) == 0xFF302010u);
}

// Invariant 37: a target consumed by a composite clears when marked, others once per EE frame.
void Inv37() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x230000, 256, 224, 0x0A, 0);
    render::DrawParams p;
    t.PrepareDraw(w.be, MakeDraw(0x230000, 256, 0x0A), 7, 0x70000, &p);
    CHECK(p.do_frame_clear && !p.clear_color);   // first draw of EE frame 7: depth-only clear
    t.PrepareDraw(w.be, MakeDraw(0x230000, 256, 0x0A), 7, 0x70000, &p);
    CHECK(!p.do_frame_clear);                    // same frame: no second clear
    t.PrepareDraw(w.be, MakeDraw(0x230000, 256, 0x0A), 8, 0x70000, &p);
    CHECK(p.do_frame_clear);                     // next frame
    t.MarkCompositeConsumed();
    CHECK(t.coherence.ever_composited && t.coherence.needs_frame_clear);
    t.PrepareDraw(w.be, MakeDraw(0x230000, 256, 0x0A), 8, 0x70000, &p);
    CHECK(p.do_frame_clear);                     // keyed on the consumption, not the frame
    t.PrepareDraw(w.be, MakeDraw(0x230000, 256, 0x0A), 9, 0x70000, &p);
    CHECK(!p.do_frame_clear);                    // a new frame alone no longer clears
    // The scanout target clears colour as well and voids the Cd copy.
    RenderTarget& d = w.cache.GetOrCreate(0x70000, 512, 448, 0x00, 0);
    d.coherence.cd_copy.valid = true;
    d.PrepareDraw(w.be, MakeDraw(0x70000, 512, 0x00), 1, 0x70000, &p);
    CHECK(p.do_frame_clear && p.clear_color && !d.coherence.cd_copy.valid);
}

// Invariant 38 (E330): the GS never clears a buffer by itself -- the engine's per-frame
// clear stands in for the game's own clear at the START of a target's frame, so it fires
// only on a frame-OPENING draw and a due clear waits for one. A 16-bit FRAME over 32-bit
// content (the companion route) and a self-read (TEX0.TBP0 == FRAME.FBP) open nothing:
// a double-buffered title writes its DISPLAYED buffer with exactly those mid-frame.
void Inv38() {
    World w;
    render::DrawParams p;
    // (a) The displayed 32-bit buffer touched mid-frame by a 16-bit shuffle: no clear, and
    // the frame key is NOT consumed (an opening draw of the same frame still clears).
    RenderTarget& t = w.cache.GetOrCreate(0x70000, 512, 224, 0x00, 0);
    t.PrepareDraw(w.be, MakeDraw(0x70000, 512, 0x00), 10, 0x0, &p);
    CHECK(p.do_frame_clear && !p.clear_color);        // frame 10 opens on a 32-bit draw (not the scanout)
    t.AfterDraw(MakeDraw(0x70000, 512, 0x00), Drew(0, 0, 512, 224), 1, 1);   // 32-bit content committed
    t.PrepareDraw(w.be, MakeDraw(0x70000, 512, 0x02), 11, 0x70000, &p);
    CHECK(p.alt16_route);                             // the 16-bit view over 32-bit content
    CHECK(!p.do_frame_clear);                         // E330: not an opening draw -> no clear of the displayed buffer
    DrawIr self = MakeDraw(0x70000, 512, 0x00);
    self.tex_enabled = true;
    self.tex.tbp = 0x70000;
    self.tex.psm = 0x1B;                              // the PSMT8H-of-FRAME post pass reads itself
    t.PrepareDraw(w.be, self, 11, 0x70000, &p);
    CHECK(!p.do_frame_clear);                         // a self-read opens nothing either
    DrawIr other = MakeDraw(0x70000, 512, 0x00);
    other.tex_enabled = true;
    other.tex.tbp = 0x108000;                         // a textured scene draw
    t.PrepareDraw(w.be, other, 11, 0x70000, &p);
    CHECK(p.do_frame_clear && p.clear_color);         // the due clear fires on the first opening draw (scanout: colour too)
    t.PrepareDraw(w.be, other, 11, 0x70000, &p);
    CHECK(!p.do_frame_clear);                         // consumed
    // (b) The pure rules.
    CHECK(RenderTarget::FrameOpeningDraw(MakeDraw(0x0, 512, 0x00), false));
    CHECK(!RenderTarget::FrameOpeningDraw(MakeDraw(0x0, 512, 0x02), true));
    CHECK(!RenderTarget::FrameOpeningDraw(self, false));
    CHECK(RenderTarget::FrameOpeningDraw(other, false));
    // A composite reads the source's colour in its own class.
    CHECK(RenderTarget::IsCompositeRead(0x00, true));     // CT32 read of 32-bit content
    CHECK(RenderTarget::IsCompositeRead(0x01, true));     // CT24
    CHECK(!RenderTarget::IsCompositeRead(0x02, true));    // a 16-bit VIEW of 32-bit content: a shuffle
    CHECK(!RenderTarget::IsCompositeRead(0x0A, true));
    CHECK(!RenderTarget::IsCompositeRead(0x1B, true));    // PSMT8H: a palette lookup of the alpha byte
    CHECK(!RenderTarget::IsCompositeRead(0x24, true) && !RenderTarget::IsCompositeRead(0x2C, true));
    CHECK(RenderTarget::IsCompositeRead(0x0A, false));    // CT16S read of a 16-bit accumulator (the Crash composite)
    CHECK(RenderTarget::IsCompositeRead(0x02, false) && RenderTarget::IsCompositeRead(0x00, false));
    CHECK(!RenderTarget::IsCompositeRead(0x13, false) && !RenderTarget::IsCompositeRead(0x14, false));
    // (c) A consumption-keyed clear also waits for an opening draw.
    RenderTarget& s2 = w.cache.GetOrCreate(0x0, 512, 224, 0x00, 0);
    s2.PrepareDraw(w.be, MakeDraw(0x0, 512, 0x00), 20, 0x70000, &p);
    CHECK(p.do_frame_clear);
    s2.AfterDraw(MakeDraw(0x0, 512, 0x00), Drew(0, 0, 512, 224), 1, 1);
    s2.MarkCompositeConsumed();
    s2.PrepareDraw(w.be, MakeDraw(0x0, 512, 0x02), 20, 0x70000, &p);
    CHECK(p.alt16_route && !p.do_frame_clear && s2.coherence.needs_frame_clear);
    s2.PrepareDraw(w.be, MakeDraw(0x0, 512, 0x00), 20, 0x70000, &p);
    CHECK(p.do_frame_clear && !s2.coherence.needs_frame_clear);
}

// Uploaded(addr) textures: exact key, containing span, in-place same-size replace,
// re-create on a size change, evict-in-range, the 512 / 64 MiB budget before insert.
void UploadedMap() {
    World w;
    TargetRefresher rf(w.be, w.vram, w.cache);
    std::vector<uint8_t> px(16 * 16 * 4, 0xAB);
    rf.UploadDirect(0x300000, 16, 16, px.data(), 16 * 16 * 4);
    CHECK(rf.UploadedCount() == 1);
    const TargetRefresher::Uploaded* u = rf.FindUploaded(0x300000);
    CHECK(u && u->w == 16 && u->h == 16 && u->size_bytes == 1024);
    CHECK(rf.FindUploaded(0x300200) == u);        // inside the span
    CHECK(rf.FindUploaded(0x300400) == nullptr);   // one past
    CHECK(w.be.At(u->tex).rgba[7] == 0xAB);
    CHECK(w.cache.Readback().UploadedSpans().size() == 1);
    // Same size: texels replaced in place, the surface and footprint stand.
    const uint32_t tex_id = u->tex.id;
    std::vector<uint8_t> px2(16 * 16 * 4, 0x5C);
    rf.UploadDirect(0x300000, 16, 16, px2.data(), 0x2000);
    CHECK(rf.UploadedCount() == 1 && rf.FindUploaded(0x300000)->tex.id == tex_id);
    CHECK(rf.FindUploaded(0x300000)->size_bytes == 1024);
    CHECK(w.be.At(rf.FindUploaded(0x300000)->tex).rgba[7] == 0x5C);
    // A different size re-creates.
    rf.UploadDirect(0x300000, 8, 8, px.data(), 256);
    CHECK(rf.UploadedCount() == 1 && rf.FindUploaded(0x300000)->w == 8);
    CHECK(rf.FindUploaded(0x300000)->tex.id != tex_id);
    rf.EvictUploadedInRange(0x2FFF00, 0x200);
    CHECK(rf.UploadedCount() == 0);
    CHECK(w.cache.Readback().UploadedSpans().empty());
    rf.MarkPagesCpuDirty(0x2000, 0x100);
    CHECK(rf.ArePagesCpuDirty(0x2000, 0x10) && !rf.ArePagesCpuDirty(0x4000, 0x10));
    rf.ClearPagesCpuDirty(0x2000, 0x100);
    CHECK(!rf.ArePagesCpuDirty(0x2000, 0x10));
    // Budget: the 513th distinct entry evicts the least recently used one BEFORE
    // inserting (512 stays the ceiling).
    std::vector<uint8_t> one(4, 0);
    for (uint32_t i = 0; i < 512; ++i) rf.UploadDirect(0x1000 * i, 1, 1, one.data(), 4);
    CHECK(rf.UploadedCount() == 512);
    rf.FindUploaded(0x0);                                             // touch the oldest
    rf.UploadDirect(0x1000 * 512, 1, 1, one.data(), 4);
    CHECK(rf.UploadedCount() == 512);
    CHECK(rf.FindUploaded(0x0) != nullptr && rf.FindUploaded(0x1000) == nullptr);   // LRU = 0x1000
    // Bytes budget counts w*h*4 texels (strictly over 64 MiB), not the footprint:
    // an oversize entry is admitted into an empty map; the next insert evicts it.
    rf.EvictUploadedInRange(0, 0x400000);
    CHECK(rf.UploadedCount() == 0);
    std::vector<uint8_t> big(size_t(4096) * 4097 * 4, 0);
    rf.UploadDirect(0x0, 4096, 4097, big.data(), 16);
    CHECK(rf.UploadedCount() == 1 && rf.UploadedTexelBytes() > TargetRefresher::kUploadedMaxBytes);
    rf.UploadDirect(0x100000, 1, 1, one.data(), 4);
    CHECK(rf.UploadedCount() == 1 && rf.FindUploaded(0x0) == nullptr);
}

// Item 6: a draw evicts the upload records under its rows (page-rounded), except a
// partial-bit-masked 16-bit draw.
void EvictUnderDraw() {
    World w;
    TargetRefresher rf(w.be, w.vram, w.cache);
    std::vector<uint8_t> px(4, 0);
    rf.UploadDirect(0x0000, 1, 1, px.data(), 4);      // page 0
    rf.UploadDirect(0x2000, 1, 1, px.data(), 4);      // page 1
    rf.UploadDirect(0x6000, 1, 1, px.data(), 4);      // page 3
    // CT32 fbw 64: row bytes 256; rows [0, 40) -> [0, 0x2800) -> pages 0..1.
    render::DrawResult r = Drew(0, 0, 64, 40);
    rf.EvictUnderDraw(MakeDraw(0x0, 64, 0x00), r, 448);
    CHECK(rf.UploadedCount() == 1 && rf.FindUploaded(0x6000) != nullptr);
    // Rows clamp to the render height.
    rf.UploadDirect(0x2000, 1, 1, px.data(), 4);
    rf.EvictUnderDraw(MakeDraw(0x0, 64, 0x00), Drew(0, 0, 64, 40), 16);   // [0, 0x1000) -> page 0 only
    CHECK(rf.FindUploaded(0x2000) != nullptr);
    // A partial-bit-masked 16-bit draw keeps the records; a full-write one evicts.
    // (CT16 fbw 64: row bytes 128; rows [0, 256) -> [0, 0x8000).)
    rf.EvictUnderDraw(MakeDraw(0x0, 64, 0x02), Drew(0, 0, 64, 256, 0x3FFF), 448);
    CHECK(rf.UploadedCount() == 2);
    rf.EvictUnderDraw(MakeDraw(0x0, 64, 0x02), Drew(0, 0, 64, 256, 0), 448);
    CHECK(rf.UploadedCount() == 0);
    // A rejected draw evicts nothing.
    rf.UploadDirect(0x0000, 1, 1, px.data(), 4);
    render::DrawResult none;
    rf.EvictUnderDraw(MakeDraw(0x0, 64, 0x00), none, 448);
    CHECK(rf.UploadedCount() == 1);
}

// Refresh from the mirror (item 8): the CT16 arm decodes and uploads; the CT16S arm
// moves no texels but takes every bookkeeping effect; both author the main image.
// Item 7: the page-tile fold survives as a unit-tested helper only.
void RefreshFromVram() {
    World w;
    TargetRefresher rf(w.be, w.vram, w.cache);
    RenderTarget& t = w.cache.GetOrCreate(0x280000, 64, 64, 0x02, 0);
    t.AfterDraw(MakeDraw(0x280000, 64, 0x02), Drew(0, 0, 64, 64), 1, 1);
    const uint32_t addr = PsmLayout::PixelByteAddress(Psm::CT16, 2, 3, 0x280000 / 256, 1);
    const uint8_t red[2] = {0x1F, 0x80};   // R = 31, A bit set
    w.vram.WriteStream(addr, red, 2);
    int y0 = 0, y1 = 0;
    CHECK(TargetRefresher::UploadRowBand(addr, 2, t, &y0, &y1));
    CHECK(y0 == 0 && y1 == 64);            // one CT16 page tile = 64 rows
    CHECK(!TargetRefresher::UploadRowBand(0x300000, 2, t, &y0, &y1));
    const int uploads0 = w.be.uploads;
    rf.RefreshFromVram(t, y0, y1, 0);
    const uint8_t* px = &w.be.At(t.views.color).rgba[(3 * 64 + 2) * 4];
    CHECK(px[0] == 0xFF && px[1] == 0x00 && px[3] == 0xFF);
    CHECK(w.be.uploads == uploads0 + 1);
    CHECK(t.coherence.main_author_seq == 2);
    CHECK(t.coherence.valid_rect.x1 == 64 && t.coherence.valid_rect.y1 == 64);
    // The decode arm's shared tail: a CT16 target WITH a companion marks the
    // whole companion stale (the main image was rewritten from the mirror);
    // without one nothing is marked.
    CHECK(t.coherence.alt16_stale.Empty());
    t.EnsureAlt16(w.be);
    t.coherence.alt16_stale.Reset();
    rf.RefreshFromVram(t, 0, 8, 0);
    CHECK(t.coherence.alt16_stale.x0 == 0 && t.coherence.alt16_stale.y0 == 0 &&
          t.coherence.alt16_stale.x1 == 64 && t.coherence.alt16_stale.y1 == 64);
    CHECK(t.coherence.main_author_seq == 3);
    // CT16S: no decode, no upload; has_content, full valid rect, Cd void, full
    // companion stale, author bump.
    RenderTarget& s = w.cache.GetOrCreate(0x230000, 64, 64, 0x0A, 0);
    s.identity.render_psm = 0x0A;
    s.EnsureAlt16(w.be);
    s.coherence.alt16_stale.Reset();
    s.coherence.cd_copy.valid = true;
    w.be.SetPixel(s.views.color, 1, 1, 9, 9, 9, 9);
    const int uploads1 = w.be.uploads;
    rf.RefreshFromVram(s, 0, 64, 0);
    CHECK(w.be.uploads == uploads1);
    CHECK(w.be.At(s.views.color).rgba[(1 * 64 + 1) * 4] == 9);   // texels untouched
    CHECK(s.coherence.has_content && s.coherence.HasValidContent());
    CHECK(!s.coherence.cd_copy.valid);
    CHECK(s.coherence.alt16_stale.y1 == 64);
    CHECK(s.coherence.main_author_seq == 1);
}


// The refresh arms read the COMMITTED mirror: CT32 keeps the attachment RGB and
// takes alpha from the mirror byte VERBATIM (the stored-alpha convention) except
// where the main route drew since the target's bytes last reached the mirror;
// CT16 decodes the 5551 image, rows under a newer EE upload keep the attachment;
// a pending deposit shadow is never observed; the tail stamps the voucher and
// clears the aliased pages.
void RefreshArms() {
    World w;
    TargetRefresher rf(w.be, w.vram, w.cache);
    RenderTarget& t = w.cache.GetOrCreate(0x100000, 64, 64, 0x00, 0);
    t.AfterDraw(MakeDraw(0x100000, 64, 0x00), Drew(0, 0, 64, 64), 1, 1);
    t.coherence.main_drawn_since_deposit.Reset();   // the draw's bytes reached the mirror
    FillColor(w.be, t, 0x11, 0x22, 0x33, 0xFF);
    // Mirror alpha 0x40 at (5,9); the pending shadow says 0x7F there (a staged,
    // uncommitted deposit) -- the merge must read the mirror.
    const uint32_t a = PsmLayout::PixelByteAddress(Psm::CT32, 5, 9, 0x100000 / 256, 1);
    const uint8_t word[4] = {0xAA, 0xBB, 0xCC, 0x40};
    w.vram.WriteStream(a, word, 4);
    uint8_t page[8192];
    w.vram.ReadView(a & ~8191u, 8192, page);
    page[(a & 8191u) + 3] = 0x7F;
    w.vram.StageDeposit(a & ~8191u, page, 8192, 5);
    w.cache.AliasPages().MarkAliased(0x100000, 64 * 64 * 4);
    rf.RefreshFromVram(t, 0, 64, 0);
    const uint8_t* px = &w.be.At(t.views.color).rgba[(9 * 64 + 5) * 4];
    CHECK(px[0] == 0x11 && px[1] == 0x22 && px[2] == 0x33);   // RGB from the attachment
    CHECK(px[3] == 0x40);                                      // the MIRROR's byte verbatim, not 0x7F, not 2x
    const uint8_t* px2 = &w.be.At(t.views.color).rgba[(0 * 64 + 0) * 4];
    CHECK(px2[3] == 0x00);                                     // mirror alpha 0 elsewhere
    CHECK(t.coherence.vram_coherent_class == 1);
    CHECK(!w.cache.AliasPages().AnyAliased(0x100000, 64 * 64 * 4));
    // The whole byte range survives (0x90 > 0x80 is a legal GS alpha, no cap).
    const uint8_t word2[4] = {0, 0, 0, 0x90};
    w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, 1, 1, 0x100000 / 256, 1), word2, 4);
    rf.RefreshFromVram(t, 0, 64, 0);
    CHECK(w.be.At(t.views.color).rgba[(1 * 64 + 1) * 4 + 3] == 0x90);
    // The later writer owns the byte: a main-route draw over rows 8..16 x 0..32 after
    // the deposit keeps the attachment's alpha there; everything else imports.
    FillColor(w.be, t, 0x11, 0x22, 0x33, 0x25);
    t.AfterDraw(MakeDraw(0x100000, 64, 0x00), Drew(0, 8, 32, 16), 1, 2);
    rf.RefreshFromVram(t, 0, 64, 0);
    CHECK(w.be.At(t.views.color).rgba[(9 * 64 + 5) * 4 + 3] == 0x25);     // drawn since: kept
    CHECK(w.be.At(t.views.color).rgba[(9 * 64 + 40) * 4 + 3] == 0x00);    // same row, outside: imported
    CHECK(w.be.At(t.views.color).rgba[(1 * 64 + 1) * 4 + 3] == 0x90);     // other rows: imported
    CHECK(w.be.At(t.views.color).rgba[(9 * 64 + 5) * 4 + 0] == 0x11);     // RGB always the attachment's
    // A writeback of the target's own bytes resets the rect: the next import takes the mirror.
    w.cache.SyncDrawn(t, 0);
    CHECK(t.coherence.main_drawn_since_deposit.Empty());
    // A scoped band stamps no voucher.
    t.coherence.vram_coherent_class = 0;
    rf.RefreshFromVram(t, 0, 8, 0);
    CHECK(t.coherence.vram_coherent_class == 0);
    // CT16: full decode from the mirror; a row whose page carries an upload epoch
    // newer than the job epoch keeps the attachment bytes.
    RenderTarget& s = w.cache.GetOrCreate(0x200000, 64, 128, 0x02, 0);
    s.AfterDraw(MakeDraw(0x200000, 64, 0x02), Drew(0, 0, 64, 128), 1, 1);
    FillColor(w.be, s, 9, 9, 9, 9);
    const uint8_t red[2] = {0x1F, 0x80};
    w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT16, 2, 3, 0x200000 / 256, 1), red, 2);
    w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT16, 2, 70, 0x200000 / 256, 1), red, 2);
    w.vram.SettleWrites();                     // both pages stamped with the same epoch
    const uint64_t up = w.vram.Page(0x200000 / 8192).upload_epoch;
    CHECK(up != 0);
    // Job epoch newer than the upload: nothing kept, whole image absorbed.
    rf.RefreshFromVram(s, 0, 128, up + 1);
    CHECK(w.be.At(s.views.color).rgba[(3 * 64 + 2) * 4] == 0xFF);
    CHECK(w.be.At(s.views.color).rgba[(70 * 64 + 2) * 4] == 0xFF);
    CHECK(s.coherence.vram_coherent_class == 2);
    // Job epoch older than the upload of page 1 (rows 64..127) only: those rows keep.
    FillColor(w.be, s, 9, 9, 9, 9);
    w.vram.MutablePage(0x200000 / 8192).upload_epoch = 1;
    w.vram.MutablePage(0x200000 / 8192 + 1).upload_epoch = 50;
    s.coherence.vram_coherent_class = 0;
    rf.RefreshFromVram(s, 0, 128, 10);
    CHECK(w.be.At(s.views.color).rgba[(3 * 64 + 2) * 4] == 0xFF);   // page 0 decoded
    CHECK(w.be.At(s.views.color).rgba[(70 * 64 + 2) * 4] == 9);     // page 1 kept
    CHECK(s.coherence.vram_coherent_class == 0);                     // not absorbed
    // Job epoch 0 (no epoch): never keeps.
    FillColor(w.be, s, 9, 9, 9, 9);
    rf.RefreshFromVram(s, 0, 128, 0);
    CHECK(w.be.At(s.views.color).rgba[(70 * 64 + 2) * 4] == 0xFF);
}

// The alias import's alpha merge has two routes and one result: with the backend's
// alpha-lane pass (Caps::alpha_lane_write) the band's mirror alpha goes up as an R8
// lane and a colour-masked write lands it -- no readback of the attachment, no RGBA
// re-upload; without it the attachment is read back, merged and re-uploaded. Over a
// pseudo-random attachment and mirror, with a main_drawn_since_deposit rect, a scoped
// band and a whole-image import, the two routes leave byte-identical attachments
// (RGB untouched, alpha = the mirror byte verbatim, the newer rect kept); the GPU
// route's census counts it and performs no readback; a scale-2 target folds the
// lane onto every physical texel of the logical pixel.
void AlphaMergeRoutes() {
    auto run = [](bool gpu, int scale, std::vector<uint8_t>* out, int* readbacks, int* uploads, uint64_t* merges_gpu) {
        World w;
        w.scale.SetFactor(scale);
        w.be.alpha_lane_write = gpu;
        TargetRefresher rf(w.be, w.vram, w.cache);
        RenderTarget& t = w.cache.GetOrCreate(0x100000, 64, 96, 0x00, 0);
        t.AfterDraw(MakeDraw(0x100000, 64, 0x00), Drew(0, 0, 64, 96), 1, 1);
        t.coherence.main_drawn_since_deposit.Reset();
        // Pseudo-random attachment (physical texels) and mirror words.
        uint32_t seed = 0x1234567u;
        auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return uint8_t(seed >> 24); };
        test::FakeBackend::Surface& att = w.be.At(t.views.color);
        for (uint8_t& b : att.rgba) b = rnd();
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 64; ++x) {
                const uint8_t word[4] = {rnd(), rnd(), rnd(), rnd()};
                w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, uint32_t(x), uint32_t(y), 0x100000 / 256, 1), word, 4);
            }
        w.vram.SettleWrites();
        // A main-route draw since the deposit over rows 20..40 x 8..48 keeps the attachment there.
        t.AfterDraw(MakeDraw(0x100000, 64, 0x00), Drew(8, 20, 48, 40), 1, 2);
        const int rb0 = w.be.readbacks, up0 = w.be.uploads;
        rf.RefreshFromVram(t, 16, 64, 0);    // a scoped band
        rf.RefreshFromVram(t, 0, 96, 0);     // the whole image
        *readbacks = w.be.readbacks - rb0;
        *uploads = w.be.uploads - up0;
        *merges_gpu = rf.AlphaMergesGpu();
        *out = w.be.At(t.views.color).rgba;
    };
    std::vector<uint8_t> cpu, gpu;
    int rb_cpu = 0, up_cpu = 0, rb_gpu = 0, up_gpu = 0;
    uint64_t mg_cpu = 0, mg_gpu = 0;
    run(false, 1, &cpu, &rb_cpu, &up_cpu, &mg_cpu);
    run(true, 1, &gpu, &rb_gpu, &up_gpu, &mg_gpu);
    CHECK(cpu.size() == gpu.size() && cpu == gpu);        // byte-identical attachments
    CHECK(rb_cpu == 2 && rb_gpu == 0);                    // the GPU route reads nothing back
    CHECK(up_cpu == 2 && up_gpu == 2);                    // RGBA rows vs the R8 lane: one upload each
    CHECK(mg_cpu == 0 && mg_gpu == 2);
    // The merge's semantics on the GPU route alone: alpha = mirror byte, RGB kept, the newer rect kept.
    {
        World w;
        w.be.alpha_lane_write = true;
        TargetRefresher rf(w.be, w.vram, w.cache);
        RenderTarget& t = w.cache.GetOrCreate(0x100000, 64, 64, 0x00, 0);
        t.AfterDraw(MakeDraw(0x100000, 64, 0x00), Drew(0, 0, 64, 64), 1, 1);
        t.coherence.main_drawn_since_deposit.Reset();
        FillColor(w.be, t, 0x11, 0x22, 0x33, 0x25);
        const uint8_t word[4] = {0xAA, 0xBB, 0xCC, 0x40};
        w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, 5, 9, 0x100000 / 256, 1), word, 4);
        w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, 40, 9, 0x100000 / 256, 1), word, 4);
        t.AfterDraw(MakeDraw(0x100000, 64, 0x00), Drew(0, 8, 32, 16), 1, 2);
        rf.RefreshFromVram(t, 0, 64, 0);
        const uint8_t* kept = &w.be.At(t.views.color).rgba[(9 * 64 + 5) * 4];
        const uint8_t* imported = &w.be.At(t.views.color).rgba[(9 * 64 + 40) * 4];
        CHECK(kept[3] == 0x25 && imported[3] == 0x40);
        CHECK(imported[0] == 0x11 && imported[1] == 0x22 && imported[2] == 0x33);
        CHECK(w.be.At(t.views.color).rgba[(1 * 64 + 1) * 4 + 3] == 0x00);
        CHECK(w.be.alpha_lane_writes == 1 && rf.AlphaMergesGpu() == 1 && rf.AlphaMergesCpu() == 0);
        CHECK(t.coherence.vram_coherent_class == 1);
    }
    // Scale 2: every physical texel of a logical pixel takes that pixel's lane byte; RGB untouched.
    {
        World w;
        w.scale.SetFactor(2);
        w.be.alpha_lane_write = true;
        TargetRefresher rf(w.be, w.vram, w.cache);
        RenderTarget& t = w.cache.GetOrCreate(0x100000, 64, 64, 0x00, 0);
        CHECK(t.identity.scale == 2);
        t.AfterDraw(MakeDraw(0x100000, 64, 0x00), Drew(0, 0, 64, 64), 1, 1);
        t.coherence.main_drawn_since_deposit.Reset();
        test::FakeBackend::Surface& att = w.be.At(t.views.color);
        for (size_t i = 0; i < att.rgba.size(); i += 4) { att.rgba[i] = uint8_t(i); att.rgba[i + 1] = 7; att.rgba[i + 2] = 9; att.rgba[i + 3] = 0x25; }
        const uint8_t word[4] = {0, 0, 0, 0x77};
        w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, 5, 9, 0x100000 / 256, 1), word, 4);
        rf.RefreshFromVram(t, 0, 64, 0);
        const uint32_t pw = att.desc.width;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const uint8_t* px = &att.rgba[((9 * 2 + dy) * pw + (5 * 2 + dx)) * 4];
                CHECK(px[3] == 0x77 && px[1] == 7 && px[2] == 9);
            }
        CHECK(att.rgba[((9 * 2) * pw + (6 * 2)) * 4 + 3] == 0x00);
    }
}

// The band read and its de-swizzle walk share one page-row stride (track 1d review
// F1): RefreshAlphaMerge addresses the mirror at the TARGET width, RefreshCt16 at
// the render FBW. With render_fbw != width and a band starting past the first page
// row, every imported pixel equals a reference computed from the full committed
// mirror by PsmLayout::PixelByteAddress at the walk's stride -- including after an
// earlier whole-image import left other bytes in the scratch (the stale-byte case).
void RefreshBandStride() {
    auto pattern = [](VramImage& v, uint32_t base, uint32_t bytes, uint32_t seed) {
        std::vector<uint8_t> buf(bytes);
        uint32_t s = seed;
        for (uint8_t& b : buf) { s = s * 1664525u + 1013904223u; b = uint8_t(s >> 24); }
        v.WriteStream(base, buf.data(), bytes);
    };
    // CT32: width 64, render FBW 128 -- the walk's stride is the width (1 page per
    // row); the old read took max(FBW, width) = 2 pages per row, so page row 1 of
    // the walk (rows 32..63 -> page 1) lay outside the refreshed window (pages 2..3).
    {
        World w;
        TargetRefresher rf(w.be, w.vram, w.cache);
        RenderTarget& t = w.cache.GetOrCreate(0x100000, 64, 64, 0x00, 0);
        t.AfterDraw(MakeDraw(0x100000, 128, 0x00), Drew(0, 0, 64, 64), 1, 1);
        t.coherence.main_drawn_since_deposit.Reset();
        CHECK(t.identity.render_fbw == 128 && t.identity.width == 64);
        FillColor(w.be, t, 0x11, 0x22, 0x33, 0xFF);
        pattern(w.vram, 0x100000, 4 * 8192, 7);
        rf.RefreshFromVram(t, 0, 64, 0);              // whole image: the scratch holds pattern 7
        pattern(w.vram, 0x100000, 4 * 8192, 99);      // the EE rewrites every page
        FillColor(w.be, t, 0x11, 0x22, 0x33, 0xFF);
        rf.RefreshFromVram(t, 32, 64, 0);             // band = page row 1
        int bad = 0;
        for (int y = 32; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                uint8_t m[4];
                w.vram.ReadMirror(PsmLayout::PixelByteAddress(Psm::CT32, x, y, 0x100000 / 256, 64 / 64), 4, m);
                const uint8_t want = m[3];
                const uint8_t* px = &w.be.At(t.views.color).rgba[(y * 64 + x) * 4];
                if (px[0] != 0x11 || px[1] != 0x22 || px[2] != 0x33 || px[3] != want) ++bad;
            }
        CHECK(bad == 0);
        if (bad) std::printf("  CT32 band stride: %d of %d pixels differ from the full-mirror reference\n", bad, 64 * 32);
    }
    // CT16: width 128, render FBW 64 -- the walk's stride is the FBW (1 page per
    // row); the old read took max(FBW, width) = 2 pages per row, so page row 1 of
    // the walk (rows 64..127 -> pages 1..2) was only half inside the refreshed
    // window (pages 2..3).
    {
        World w;
        TargetRefresher rf(w.be, w.vram, w.cache);
        RenderTarget& s = w.cache.GetOrCreate(0x200000, 128, 128, 0x02, 0);
        s.AfterDraw(MakeDraw(0x200000, 64, 0x02), Drew(0, 0, 128, 128), 1, 1);
        CHECK(s.identity.render_fbw == 64 && s.identity.width == 128);
        FillColor(w.be, s, 9, 9, 9, 9);
        pattern(w.vram, 0x200000, 4 * 8192, 7);
        rf.RefreshFromVram(s, 0, 128, 0);
        pattern(w.vram, 0x200000, 4 * 8192, 99);
        FillColor(w.be, s, 9, 9, 9, 9);
        rf.RefreshFromVram(s, 64, 128, 0);            // band = page row 1 (CT16 page = 64 rows)
        int bad = 0;
        for (int y = 64; y < 128; ++y)
            for (int x = 0; x < 128; ++x) {
                uint8_t m[2];
                w.vram.ReadMirror(PsmLayout::PixelByteAddress(Psm::CT16, x, y, 0x200000 / 256, 64 / 64), 2, m);
                const uint32_t want = TexaExpand::Expand16To32(uint16_t(m[0] | (m[1] << 8)));
                uint32_t got;
                std::memcpy(&got, &w.be.At(s.views.color).rgba[(y * 128 + x) * 4], 4);
                if (got != want) ++bad;
            }
        CHECK(bad == 0);
        if (bad) std::printf("  CT16 band stride: %d of %d pixels differ from the full-mirror reference\n", bad, 128 * 64);
        // Rows above the band are untouched by a band import.
        CHECK(w.be.At(s.views.color).rgba[(10 * 128 + 10) * 4] == 9);
    }
    // The strides agree (render FBW == width): the band import at page row 1 still
    // equals the reference (no regression of the common case), CT32 and CT16.
    {
        World w;
        TargetRefresher rf(w.be, w.vram, w.cache);
        RenderTarget& t = w.cache.GetOrCreate(0x300000, 128, 64, 0x00, 0);
        t.AfterDraw(MakeDraw(0x300000, 128, 0x00), Drew(0, 0, 128, 64), 1, 1);
        t.coherence.main_drawn_since_deposit.Reset();
        FillColor(w.be, t, 1, 2, 3, 0xFF);
        pattern(w.vram, 0x300000, 4 * 8192, 5);
        rf.RefreshFromVram(t, 32, 64, 0);
        int bad = 0;
        for (int y = 32; y < 64; ++y)
            for (int x = 0; x < 128; ++x) {
                uint8_t m[4];
                w.vram.ReadMirror(PsmLayout::PixelByteAddress(Psm::CT32, x, y, 0x300000 / 256, 2), 4, m);
                if (w.be.At(t.views.color).rgba[(y * 128 + x) * 4 + 3] != m[3]) ++bad;
            }
        CHECK(bad == 0);
    }
}

// The pre-draw alias demand: a colour writeback marks the linear rows aliased +
// CPU-dirty; a later frame over any aliased page (or a 16-bit frame over a CPU-
// dirty page) imports the band bbox ∩ scissor; masked-off / zero-width / non-colour
// frames and composite draws into the scanout target behave as OLD.
void AliasPreDraw() {
    World w;
    TargetRefresher rf(w.be, w.vram, w.cache);
    RenderTarget& t = w.cache.GetOrCreate(0x100000, 64, 64, 0x00, 0);
    t.AfterDraw(MakeDraw(0x100000, 64, 0x00), Drew(0, 32, 64, 48), 1, 1);
    CHECK(!w.cache.AliasPages().AnyAliased(0x100000, 64 * 64 * 4));
    w.cache.Readback().SyncRtToVram(w.be, t, w.vram, 1, nullptr, 0);
    // Rows 32..48 at 64*4 B/row = bytes 0x2000..0x3000 -> page 1 only.
    CHECK(w.cache.AliasPages().IsAliased(0x100000 / 8192 + 1));
    CHECK(!w.cache.AliasPages().IsAliased(0x100000 / 8192));
    CHECK(w.cache.AliasPages().IsCpuDirty(0x100000 / 8192 + 1));
    DrawIr ir = MakeDraw(0x100000, 64, 0x00);
    ir.raster.frame.fbmsk = 0xFF000000;
    ir.verts.resize(3);
    ir.verts[0].x = 0; ir.verts[0].y = 8; ir.verts[1].x = 64; ir.verts[1].y = 8; ir.verts[2].x = 64; ir.verts[2].y = 24;
    CHECK(TargetRefresher::AliasRegionBytes(ir, 64) == 64 * 64 * 4);
    const int up0 = w.be.uploads;
    CHECK(rf.AliasPreDraw(t, ir, 64, 0x70000, 0));
    CHECK(w.be.uploads == up0 + 1);                            // one band import
    // A scoped band clears only the pages it fully covers: rows 8..24 cover none,
    // so the page stays aliased and a second demand imports again; a band over
    // rows 32..64 (page 1 whole) clears it.
    CHECK(w.cache.AliasPages().IsAliased(0x100000 / 8192 + 1));
    CHECK(rf.AliasPreDraw(t, ir, 64, 0x70000, 0));
    ir.verts[0].y = 32; ir.verts[1].y = 32; ir.verts[2].y = 64;
    CHECK(rf.AliasPreDraw(t, ir, 64, 0x70000, 0));
    CHECK(!w.cache.AliasPages().IsAliased(0x100000 / 8192 + 1));
    CHECK(!rf.AliasPreDraw(t, ir, 64, 0x70000, 0));             // nothing aliased now
    // Negative probes: fbmsk 0 / fbw 0 / Z frame psm / render_h 0 -> no region.
    DrawIr n1 = ir; n1.raster.frame.fbmsk = 0;
    CHECK(TargetRefresher::AliasRegionBytes(n1, 64) == 0);
    DrawIr n2 = ir; n2.raster.frame.fbw = 0;
    CHECK(TargetRefresher::AliasRegionBytes(n2, 64) == 0);
    DrawIr n3 = ir; n3.raster.frame.psm = 0x30;
    CHECK(TargetRefresher::AliasRegionBytes(n3, 64) == 0);
    CHECK(TargetRefresher::AliasRegionBytes(ir, 0) == 0);
    // No aliased page and a 32-bit frame over CPU-dirty pages -> no import.
    w.cache.AliasPages().ClearAliased(0x100000, 64 * 64 * 4);
    w.cache.AliasPages().MarkCpuDirty(0x100000, 64 * 64 * 4);
    const int up1 = w.be.uploads;
    CHECK(!rf.AliasPreDraw(t, ir, 64, 0x70000, 0));
    CHECK(w.be.uploads == up1);
    // ... but a 16-bit frame over CPU-dirty pages imports.
    RenderTarget& s = w.cache.GetOrCreate(0x200000, 64, 64, 0x02, 0);
    s.AfterDraw(MakeDraw(0x200000, 64, 0x02), Drew(0, 0, 64, 64), 1, 1);
    DrawIr i16 = MakeDraw(0x200000, 64, 0x02);
    i16.raster.frame.fbmsk = 0x8000;
    w.cache.AliasPages().MarkCpuDirty(0x200000, 64 * 64 * 2);
    CHECK(rf.AliasPreDraw(s, i16, 64, 0x70000, 0));
    // Composite into the scanout target (textured from elsewhere) imports whole:
    // an aliased page + composite -> full-image import clears every page.
    w.cache.AliasPages().MarkAliased(0x100000, 64 * 64 * 4);
    DrawIr comp = ir;
    comp.tex_enabled = true;
    comp.tex.tbp = 0x300000;
    CHECK(rf.AliasPreDraw(t, comp, 64, 0x100000, 0));
    CHECK(!w.cache.AliasPages().AnyAliased(0x100000, 64 * 64 * 4));
}

// The deferred 16-bit writeback ledger: every 16-bit-class draw records (latest
// epoch wins, footprint grows); flushes take the overlapping entries and write
// each back at ITS epoch; the present flushes reader-band entries only; a fog-lane
// companion draw deposits at once; the destroy is the rescue form + flush.
void PendingLedger() {
    World w;
    RenderTarget& t = w.cache.GetOrCreate(0x200000, 64, 64, 0x02, 0);
    FillColor(w.be, t, 0xFF, 0x00, 0x00, 0xFF);
    DrawIr d = MakeDraw(0x200000, 64, 0x02);
    t.AfterDraw(d, Drew(0, 0, 64, 64), 1, 7);
    w.cache.AfterDrawTail(t, d, Drew(0, 0, 64, 64), 64, 7);
    CHECK(w.cache.Ledger().Count() == 1);
    CHECK(w.cache.Ledger().Entries()[0].epoch == 7 && w.cache.Ledger().Entries()[0].size == 64 * 64 * 2);
    CHECK(w.vram.PendingPages() == 0);                          // deferred, nothing staged
    t.AfterDraw(d, Drew(0, 0, 64, 64), 2, 9);
    w.cache.AfterDrawTail(t, d, Drew(0, 0, 64, 64), 64, 9);
    CHECK(w.cache.Ledger().Count() == 1 && w.cache.Ledger().Entries()[0].epoch == 9);
    // A 32-bit draw records nothing.
    RenderTarget& c = w.cache.GetOrCreate(0x100000, 64, 64, 0x00, 0);
    DrawIr d32 = MakeDraw(0x100000, 64, 0x00);
    c.AfterDraw(d32, Drew(0, 0, 64, 64), 2, 9);
    w.cache.AfterDrawTail(c, d32, Drew(0, 0, 64, 64), 64, 9);
    CHECK(w.cache.Ledger().Count() == 1);
    // GPU-valid clears the CPU-dirty pages of the draw's rows.
    w.cache.AliasPages().MarkCpuDirty(0x100000, 64 * 64 * 4);
    w.cache.AfterDrawTail(c, d32, Drew(0, 0, 64, 64), 64, 9);
    CHECK(!w.cache.AliasPages().AnyCpuDirty(0x100000, 64 * 64 * 4));
    // A flush over a disjoint span takes nothing; over the footprint it writes
    // back at the recorded epoch (9) and empties the ledger.
    CHECK(w.cache.FlushPending(PendingRtSyncLedger::Reason::Decode, 0x300000, 0x1000) == 0);
    CHECK(w.cache.Ledger().Count() == 1);
    CHECK(w.cache.FlushPending(PendingRtSyncLedger::Reason::Decode, 0x200000, 2) == 1);
    CHECK(w.cache.Ledger().Empty());
    CHECK(w.vram.PendingPages() > 0);
    CHECK(w.vram.Page(0x200000 / 8192).shadow_epoch == 9);
    CHECK(VramHalf(w.vram, PsmLayout::PixelByteAddress(Psm::CT16, 0, 0, 0x200000 / 256, 1)) == 0x801F);
    CHECK(w.cache.Ledger().Flushes(PendingRtSyncLedger::Reason::Decode) == 1);
    // Present: reader-pages-only keeps an entry no reader observes...
    t.AfterDraw(d, Drew(0, 0, 64, 64), 3, 11);
    w.cache.AfterDrawTail(t, d, Drew(0, 0, 64, 64), 64, 11);
    CHECK(w.cache.FlushPending(PendingRtSyncLedger::Reason::Present, 0, 0, true) == 0);
    CHECK(w.cache.Ledger().Count() == 1);
    // ... and takes it once its page is in the reader band.
    w.vram.ReaderPagesSeed(VramImage::Span{0x200000, 8192});
    CHECK(w.cache.FlushPending(PendingRtSyncLedger::Reason::Present, 0, 0, true) == 1);
    CHECK(w.cache.Ledger().Empty());
    // Flush-all (0, 0) without the reader gate takes everything.
    t.AfterDraw(d, Drew(0, 0, 64, 64), 4, 12);
    w.cache.AfterDrawTail(t, d, Drew(0, 0, 64, 64), 64, 12);
    CHECK(w.cache.FlushPending(PendingRtSyncLedger::Reason::Scanout, 0, 0) == 1);
    // Fog-lane companion deposit: a CT16 keep-mask draw through the companion of a
    // 32-bit target writes the high byte of each halfword at once.
    RenderTarget& f = w.cache.GetOrCreate(0x280000, 64, 64, 0x00, 0);
    f.AfterDraw(MakeDraw(0x280000, 64, 0x00), Drew(0, 0, 64, 64), 1, 1);
    f.EnsureAlt16(w.be);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) w.be.SetPixel(f.views.alt16, x, y, 0, 0, 0xFF, 0xFF);   // B=31, A=1
    DrawIr lane = MakeDraw(0x280000, 64, 0x02);
    render::DrawResult lr = Drew(0, 0, 64, 64, RenderTarget::kIndexLaneKeep16);
    lr.alt16_route = true;
    const uint32_t la = PsmLayout::PixelByteAddress(Psm::CT16, 3, 3, 0x280000 / 256, 1);
    const uint8_t low[2] = {0x5A, 0x00};
    w.vram.WriteStream(la, low, 2);
    f.AfterDraw(lane, lr, 5, 20);
    w.cache.AfterDrawTail(f, lane, lr, 64, 20);
    CHECK(w.cache.Ledger().Empty());                            // the 32-bit target is not recorded
    CHECK(VramHalf(w.vram, la) == 0xFC5A);                      // high byte 0xFC, low byte kept
    CHECK(w.cache.AliasPages().IsAliased(0x280000 / 8192));
    // Destroy: rescue at the last-draw epoch (full extent) + ledger flush.
    t.AfterDraw(d, Drew(0, 0, 64, 64), 6, 30);
    w.cache.AfterDrawTail(t, d, Drew(0, 0, 64, 64), 64, 30);
    CHECK(w.cache.Ledger().Has(0x200000));
    w.cache.Destroy(0x200000);
    CHECK(!w.cache.Has(0x200000));
    CHECK(!w.cache.Ledger().Has(0x200000));
    CHECK(w.vram.Page(0x200000 / 8192).shadow_epoch == 30);
    // The rescue epoch pinned WITHOUT a ledger entry to mask it: a 32-bit target
    // (never recorded) drawn at epoch 40 and destroyed -> its full extent lands at
    // 40 (the kDestroy flush has nothing to take).
    RenderTarget& g = w.cache.GetOrCreate(0x300000, 64, 64, 0x00, 0);
    FillColor(w.be, g, 0x11, 0x22, 0x33, 0xFF);
    DrawIr g32 = MakeDraw(0x300000, 64, 0x00);
    g.AfterDraw(g32, Drew(0, 0, 64, 64), 7, 40);
    w.cache.AfterDrawTail(g, g32, Drew(0, 0, 64, 64), 64, 40);
    CHECK(w.cache.Ledger().Empty());
    // The rescue keeps every page the target is not the recorded GPU author of
    // (a never-synced target owns nothing yet): rows 0..31 (page 384) authored by
    // it, rows 32..63 (page 385) CPU-authored.
    w.vram.NoteGpuAuthor(VramImage::Span{0x300000, 0x2000}, 0x300000);
    w.vram.NoteCpuWrite(VramImage::Span{0x302000, 0x2000});
    w.cache.Destroy(0x300000);
    CHECK(!w.cache.Has(0x300000) && w.vram.Page(0x300000 / 8192).shadow_epoch == 40);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 5, 5, 0x300000 / 256, 1)) == 0xFF332211u);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 5, 40, 0x300000 / 256, 1)) == 0u);   // page 385: CPU-authored, kept
    // ... and the rescue is a FOOTPRINT deposit, not a drawn-rows one: with the drawn
    // rect already consumed by a sync (nothing drawn since), the rows still land.
    RenderTarget& h = w.cache.GetOrCreate(0x310000, 64, 64, 0x00, 0);
    FillColor(w.be, h, 0x44, 0x55, 0x66, 0xFF);
    DrawIr h32 = MakeDraw(0x310000, 64, 0x00);
    h.AfterDraw(h32, Drew(0, 0, 64, 64), 8, 50);
    w.cache.AfterDrawTail(h, h32, Drew(0, 0, 64, 64), 64, 50);
    w.vram.NoteGpuAuthor(VramImage::Span{0x310000, 0x4000}, 0x310000);
    CHECK(w.cache.SyncDrawn(h, 50) > 0 && !h.coherence.HasDrawnSinceSync());
    w.be.SetPixel(h.views.color, 9, 9, 0x77, 0x88, 0x99, 0xFF);   // changed after the sync, nothing "drawn"
    CHECK(w.cache.SyncDrawn(h, 50) == 0);                          // a plain sync writes nothing now
    w.cache.Destroy(0x310000);
    CHECK(VramWord(w.vram, PsmLayout::PixelByteAddress(Psm::CT32, 9, 9, 0x310000 / 256, 1)) == 0xFF998877u);
    // The alias page walks CLAMP at page 511 (no 4 MiB wrap): a region running past
    // the end marks the tail pages only, never a low page; one starting past the
    // end marks nothing.
    AliasPageSet& ap = w.cache.AliasPages();
    ap.MarkAliased(0x3FE000, 0x8000);              // pages 511, (512, 513, 514 -> clamped)
    CHECK(ap.IsAliased(511) && !ap.IsAliased(0) && !ap.IsAliased(1) && !ap.IsAliased(2));
    CHECK(ap.AnyAliased(0x3FE000, 0x8000) && !ap.AnyAliased(0x0, 0x6000));
    ap.MarkCpuDirty(0x400000, 0x2000);             // starts past the end: nothing
    CHECK(!ap.AnyCpuDirty(0x0, 0x2000) && !ap.AnyCpuDirty(0x3FE000, 0x2000));
    CHECK(!ap.AnyCpuDirty(0x400000, 0x2000));      // a query past the end is false, not page 0
    ap.ClearAliased(0x3FE000, 0x8000);
    CHECK(!ap.IsAliased(511));
    // Word-range marks (the per-draw ClearCpuDirty is masks, not a bit walk): a span
    // inside one word, one crossing word boundaries, one ending on a word's last page;
    // every page in the span is set and the neighbours outside it are not.
    AliasPageSet ws;
    auto span_ok = [&](uint32_t first, uint32_t last) {
        for (uint32_t p = 0; p < VramImage::kPageCount; ++p)
            if (ws.IsCpuDirty(p) != (p >= first && p <= last)) return false;
        return true;
    };
    ws.MarkCpuDirty(3 * VramImage::kPageBytes, 5 * VramImage::kPageBytes);          // pages 3..7
    CHECK(span_ok(3, 7));
    CHECK(ws.AnyCpuDirty(7 * VramImage::kPageBytes, 1) && !ws.AnyCpuDirty(8 * VramImage::kPageBytes, 64 * VramImage::kPageBytes));
    ws.ClearCpuDirty(0, 512 * VramImage::kPageBytes);
    CHECK(span_ok(1, 0));                                                           // all clear
    ws.MarkCpuDirty(60 * VramImage::kPageBytes, 70 * VramImage::kPageBytes);        // pages 60..129: three words
    CHECK(span_ok(60, 129));
    CHECK(ws.AnyCpuDirty(63 * VramImage::kPageBytes, 2 * VramImage::kPageBytes) && !ws.AnyCpuDirty(130 * VramImage::kPageBytes, 1));
    ws.ClearCpuDirty(64 * VramImage::kPageBytes, 64 * VramImage::kPageBytes);       // the middle word exactly
    CHECK(ws.IsCpuDirty(60) && ws.IsCpuDirty(63) && !ws.IsCpuDirty(64) && !ws.IsCpuDirty(127) && ws.IsCpuDirty(128) && ws.IsCpuDirty(129));
    ws.MarkCpuDirty(0, 64 * VramImage::kPageBytes);                                 // pages 0..63 = word 0 whole
    CHECK(ws.IsCpuDirty(0) && ws.IsCpuDirty(63) && !ws.IsCpuDirty(64));
    ws.ClearCpuDirty(0, 512 * VramImage::kPageBytes);
    ws.MarkCpuDirty(511 * VramImage::kPageBytes, 3 * VramImage::kPageBytes);        // the last page, clamped
    CHECK(span_ok(511, 511));
}

// The per-draw render size rule (TargetDims): FBW in (0, 1024] else the window
// width; the window height; the scanout target with content dims takes them; else
// the scene accumulator shrinks to SCISSOR.Y1 + 1.
void Dims() {
    VramLayout layout = FilledLayout();
    TargetDims::Display disp;
    int w = 0, h = 0;
    DrawIr d = MakeDraw(0x100000, 640, 0x00);
    TargetDims::Compute(d, 512, 448, disp, layout, &w, &h);
    CHECK(w == 640 && h == 448);
    d.raster.frame.fbw = 0;
    TargetDims::Compute(d, 512, 448, disp, layout, &w, &h);
    CHECK(w == 512 && h == 448);                                 // FBW 0: window width
    d.raster.frame.fbw = 1025;
    TargetDims::Compute(d, 512, 448, disp, layout, &w, &h);
    CHECK(w == 512);                                             // > 1024: window width
    d.raster.frame.fbw = 1024;
    TargetDims::Compute(d, 512, 448, disp, layout, &w, &h);
    CHECK(w == 1024);
    // The scanout target with content dims: both follow the content.
    disp.scanout_fbp = 0x100000;
    disp.content_w = 512; disp.content_h = 224;
    d.raster.frame.fbw = 640;
    TargetDims::Compute(d, 512, 448, disp, layout, &w, &h);
    CHECK(w == 512 && h == 224);
    disp.content_h = 0;                                          // unknown content: the FBW rule
    TargetDims::Compute(d, 512, 448, disp, layout, &w, &h);
    CHECK(w == 640 && h == 448);
    disp.content_h = 224;
    d.raster.frame.fbp = 0x200000;                               // not the scanout target
    TargetDims::Compute(d, 512, 448, disp, layout, &w, &h);
    CHECK(w == 640 && h == 448);
    // The scene accumulator: SCISSOR.Y1 + 1 when positive and smaller than the window.
    DrawIr a = MakeDraw(0x230000, 256, 0x00);
    a.raster.scissor = DrawIr::Scissor{0, 0, 255, 223};
    TargetDims::Compute(a, 512, 448, disp, layout, &w, &h);
    CHECK(w == 256 && h == 224);
    a.raster.scissor.y1 = 224;
    TargetDims::Compute(a, 512, 448, disp, layout, &w, &h);
    CHECK(h == 225);                                             // the alternating 225-row scissor
    a.raster.scissor.y1 = 447;
    TargetDims::Compute(a, 512, 448, disp, layout, &w, &h);
    CHECK(h == 448);                                             // equal: not smaller, unchanged
    a.raster.scissor.y1 = 1023;
    TargetDims::Compute(a, 512, 448, disp, layout, &w, &h);
    CHECK(h == 448);                                             // never grows
    DrawIr n = MakeDraw(0x240000, 256, 0x00);                    // another buffer: no scissor rule
    n.raster.scissor = DrawIr::Scissor{0, 0, 255, 223};
    TargetDims::Compute(n, 512, 448, disp, layout, &w, &h);
    CHECK(h == 448);
    TargetDims::Compute(a, 512, 448, disp, VramLayout(), &w, &h);   // empty layout: plain GS
    a.raster.scissor.y1 = 223;
    TargetDims::Compute(a, 512, 448, disp, VramLayout(), &w, &h);
    CHECK(h == 448);
    // The accumulator AS the scanout target with content dims: content wins (else-if).
    disp.scanout_fbp = 0x230000;
    disp.content_w = 512; disp.content_h = 448;
    TargetDims::Compute(a, 512, 448, disp, layout, &w, &h);
    CHECK(w == 512 && h == 448);
    disp.content_w = 0;                                          // ... unknown content: the scissor rule again
    TargetDims::Compute(a, 512, 448, disp, layout, &w, &h);
    CHECK(w == 256 && h == 224);
}

// An empty VramLayout answers false to every query.
void EmptyLayout() {
    VramLayout e;
    CHECK(e.Empty());
    CHECK(!e.IsSceneAccum(0) && !e.IsSceneAccum(0x230000));
    CHECK(!e.IsFogLane(0));
    CHECK(!e.IsFmvBlitSignature(0, 0, 0, 10, 10));
    CHECK(!e.IsNativeScanout(0, 0, 0));
    CHECK(!e.IsClutHealExempt(0) && !e.IsClutHealExempt(0x388000));
    CHECK(!e.IsReaderBandPage(0));
    VramLayout f = FilledLayout();
    f.clut_heal_exempt_lo = 0x388000; f.clut_heal_exempt_hi = 0x3F8000;
    f.clut_heal_exempt_mask = 0xFFFF; f.clut_heal_exempt_value = 0x8000;
    CHECK(!f.Empty());
    CHECK(f.IsSceneAccum(0x230000) && !f.IsSceneAccum(0x70000));
    CHECK(f.IsFmvBlitSignature(0x70000, 0x150000, 0, 10, 10));
    CHECK(!f.IsFmvBlitSignature(0x0, 0x150000, 0, 10, 10));
    CHECK(!f.IsFmvBlitSignature(0x70000, 0x150000, 0, 9, 10));
    CHECK(f.IsNativeScanout(0x70000, 512, 0) && !f.IsNativeScanout(0x70000, 640, 0));
    CHECK(f.IsClutHealExempt(0x388000) && f.IsClutHealExempt(0x3F8000) && !f.IsClutHealExempt(0x380C00));
    CHECK(!f.IsClutHealExempt(0x398400));
}

}  // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    const bool all = which == "all";
    if (all || which == "inv09") Inv09();
    if (all || which == "offer") OfferedPixels();
    if (all || which == "inv10") Inv10();
    if (all || which == "inv11") Inv11();
    if (all || which == "inv12") Inv12();
    if (all || which == "alphamerge") AlphaMergeRoutes();
    if (all || which == "inv13") Inv13_14();
    if (all || which == "inv14") Inv13_14();
    if (all || which == "inv20") Inv20();
    if (all || which == "inv23") Inv23();
    if (all || which == "inv24") Inv24();
    if (all || which == "inv35") Inv35();
    if (all || which == "inv37") Inv37();
    if (all || which == "inv38") Inv38();
    if (all || which == "rects") DrawnRects();
    if (all || which == "authors") AuthorSeqs();
    if (all || which == "zsat") ZSaturate();
    if (all || which == "syncall") SyncAllJob();
    if (all || which == "vetoes") EncodeVetoes();
    if (all || which == "span") DepositSpan();
    if (all || which == "uploaded") UploadedMap();
    if (all || which == "evict") EvictUnderDraw();
    if (all || which == "refresh") RefreshFromVram();
    if (all || which == "arms") RefreshArms();
    if (all || which == "stride") RefreshBandStride();
    if (all || which == "alias") AliasPreDraw();
    if (all || which == "ledger") PendingLedger();
    if (all || which == "layout") EmptyLayout();
    if (all || which == "dims") Dims();
    std::printf("test_targets %s: %d checks, %d failures\n", which.c_str(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
