// tests/gs/present/test_present.cpp -- ViewportMath (ported vectors), CrtcClassifier
// rule, ScanoutComposer grab with the most-recent-drawn fallback and the
// local-memory arm (GrabPolicy::LocalMemory), the display quad's
// source choice + mirror maintenance, Presenter order.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/gs/hle/present/crtc_classifier.h"
#include "engine/gs/hle/present/presenter.h"
#include "engine/gs/hle/present/scanout_composer.h"
#include "engine/gs/hle/present/viewport_math.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/hle/targets/render_target_cache.h"
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

using VM = ViewportMath;

void Rect(const char* name, VM::Rect got, int x, int y, int w, int h) {
    ++g_checks;
    if (got.x != x || got.y != y || got.w != w || got.h != h) {
        std::printf("FAIL %s got=(%d,%d,%d,%d) want=(%d,%d,%d,%d)\n", name, got.x, got.y, got.w, got.h, x, y, w, h);
        ++g_fail;
    }
}

void ViewportVectors() {
    const double a107 = 10.0 / 7.0;
    using F = VM::FitMode; using A = VM::AspectSel;
    Rect("stretch identity", VM::ComputeOutputRect(512, 448, 512, 448, F::Stretch, A::Auto, 0, 100, 0, 0), 0, 0, 512, 448);
    Rect("stretch resized", VM::ComputeOutputRect(1024, 768, 512, 448, F::Stretch, A::Auto, 0, 100, 0, 0), 0, 0, 1024, 768);
    Rect("stretch W=0", VM::ComputeOutputRect(0, 448, 512, 448, F::Stretch, A::Auto, 0, 100, 0, 0), 0, 0, 0, 0);
    Rect("fit pillarbox PAR", VM::ComputeOutputRect(1024, 448, 512, 448, F::Fit, A::PAR1_1, 0, 100, 0, 0), 256, 0, 512, 448);
    Rect("fit pillarbox auto", VM::ComputeOutputRect(1024, 448, 512, 448, F::Fit, A::Auto, a107, 100, 0, 0), 192, 0, 640, 448);
    Rect("fit letterbox PAR", VM::ComputeOutputRect(512, 896, 512, 448, F::Fit, A::PAR1_1, 0, 100, 0, 0), 0, 224, 512, 448);
    Rect("fit letterbox 4:3", VM::ComputeOutputRect(800, 900, 512, 448, F::Fit, A::R4_3, 0, 100, 0, 0), 0, 150, 800, 600);
    Rect("fit odd 513", VM::ComputeOutputRect(513, 448, 512, 448, F::Fit, A::PAR1_1, 0, 100, 0, 0), 0, 0, 512, 448);
    Rect("fill wide", VM::ComputeOutputRect(1024, 448, 512, 448, F::Fill, A::PAR1_1, 0, 100, 0, 0), 0, -224, 1024, 896);
    Rect("fill tall", VM::ComputeOutputRect(512, 896, 512, 448, F::Fill, A::PAR1_1, 0, 100, 0, 0), -256, 0, 1024, 896);
    Rect("fill odd 511", VM::ComputeOutputRect(511, 448, 512, 448, F::Fill, A::PAR1_1, 0, 100, 0, 0), -1, 0, 512, 448);
    Rect("integer n=2", VM::ComputeOutputRect(1200, 1000, 512, 448, F::Integer, A::PAR1_1, 0, 100, 0, 0), 88, 52, 1024, 896);
    Rect("integer n=0 fit", VM::ComputeOutputRect(400, 300, 512, 448, F::Integer, A::PAR1_1, 0, 100, 0, 0), 28, 0, 343, 300);
    Rect("integer auto cell", VM::ComputeOutputRect(1400, 1000, 512, 448, F::Integer, A::Auto, a107, 100, 0, 0), 60, 52, 1280, 896);
    Rect("integer huge", VM::ComputeOutputRect(16384, 16384, 512, 448, F::Integer, A::PAR1_1, 0, 100, 0, 0), 0, 1024, 16384, 14336);
    Rect("custom 100%", VM::ComputeOutputRect(1024, 768, 512, 448, F::Custom, A::Auto, 0, 100, 0, 0), 256, 160, 512, 448);
    Rect("custom clamp low", VM::ComputeOutputRect(1024, 768, 512, 448, F::Custom, A::Auto, 0, 10, 0, 0), 448, 328, 128, 112);
    Rect("custom clamp high", VM::ComputeOutputRect(1024, 768, 512, 448, F::Custom, A::Auto, 0, 999, 0, 0), -512, -512, 2048, 1792);
    Rect("custom offsets", VM::ComputeOutputRect(1024, 768, 512, 448, F::Custom, A::Auto, 0, 100, 10, 20), 266, 140, 512, 448);
    Rect("custom offset clamp", VM::ComputeOutputRect(1024, 768, 512, 448, F::Custom, A::Auto, 0, 100, 99999, -99999), 8448, 8352, 512, 448);
    Rect("custom Cw=0", VM::ComputeOutputRect(1024, 768, 0, 448, F::Custom, A::Auto, 0, 100, 0, 0), 0, 0, 0, 0);
    Rect("fit H=0", VM::ComputeOutputRect(640, 0, 512, 448, F::Fit, A::PAR1_1, 0, 100, 0, 0), 0, 0, 0, 0);
    Rect("custom W=0", VM::ComputeOutputRect(0, 768, 512, 448, F::Custom, A::Auto, 0, 100, 0, 0), 0, 0, 0, 0);
    Rect("fit 1x1", VM::ComputeOutputRect(1, 1, 512, 448, F::Fit, A::PAR1_1, 0, 100, 0, 0), 0, 0, 1, 1);
    Rect("fit no aspect", VM::ComputeOutputRect(640, 448, 0, 0, F::Fit, A::PAR1_1, 0, 100, 0, 0), 0, 0, 640, 448);
    Rect("fit 2561x448", VM::ComputeOutputRect(640, 448, 2561, 448, F::Fit, A::PAR1_1, 0, 100, 0, 0), 0, 168, 640, 112);
    Rect("fit 16:9", VM::ComputeOutputRect(1920, 1080, 512, 448, F::Fit, A::R16_9, 0, 100, 0, 0), 0, 0, 1920, 1080);
    Rect("fill 4:3", VM::ComputeOutputRect(1024, 768, 512, 448, F::Fill, A::R4_3, 0, 100, 0, 0), 0, 0, 1024, 768);
    Rect("fit auto fallback", VM::ComputeOutputRect(1024, 448, 512, 448, F::Fit, A::Auto, 0, 100, 0, 0), 256, 0, 512, 448);
    CHECK(std::fabs(VM::ComputeAutoAspect(2560, 448) - 10.0 / 7.0) < 1e-6);
    CHECK(std::fabs(VM::ComputeAutoAspect(2560, 480) - 4.0 / 3.0) < 1e-6);
    CHECK(VM::ComputeAutoAspect(0, 448) == 0.0);
}

void Crtc() {
    using C = CrtcClassifier;
    const uint64_t dispfb = 56ull | (8ull << 9) | (0ull << 15);   // FBP 56, FBW 8, CT32
    const uint64_t disp1 = (50ull << 12) | 0x2A0ull | (2559ull << 32) | (447ull << 44);
    const uint64_t disp2 = (51ull << 12) | 0x2A0ull | (2559ull << 32) | (447ull << 44);
    C::Snapshot s = C::Classify(0x8023, dispfb, dispfb, disp1, disp2);
    CHECK(s.valid && s.cls == C::Class::DualIdentical1px);
    CHECK(s.fbp_bytes == 0x70000 && s.fbw_px == 512 && s.psm == 0);
    CHECK(C::Classify(0x0001, dispfb, dispfb, disp1, disp2).cls == C::Class::Other);      // one circuit
    CHECK(C::Classify(0x0003, dispfb, dispfb + 1, disp1, disp2).cls == C::Class::Other);  // distinct buffers
    CHECK(C::Classify(0x0003, dispfb, dispfb, disp1, disp1 | (52ull << 12)).cls == C::Class::Other);  // DY delta 2
    CHECK(C::Classify(0x0003, dispfb, dispfb, disp1, disp2 | 1ull).cls == C::Class::Other);  // DX differs
    CHECK(C::Classify(0x0003, dispfb, dispfb, disp1, disp1).cls == C::Class::DualIdentical1px);  // DY delta 0
    const C::Snapshot u = C::Unpack(C::Pack(s));
    CHECK(u.valid && u.cls == s.cls && u.fbp_bytes == s.fbp_bytes && u.fbw_px == s.fbw_px && u.psm == s.psm);
    VramLayout l;
    l.native_scanout_fbp_bytes = 0x70000; l.native_scanout_fbw_px = 512; l.native_scanout_psm = 0;
    CHECK(C::IsNativeConfig(s, l));
    CHECK(!C::IsNativeConfig(s, VramLayout()));   // empty layout: never native
    C::Snapshot o = C::Classify(0x0003, dispfb | (1ull << 15), dispfb | (1ull << 15), disp1, disp2);
    CHECK(!C::IsNativeConfig(o, l));   // CT24 display
}

void Grab() {
    test::FakeBackend be;
    VramImage vram;
    VramLayout layout;
    ScalePolicy scale(ScaleSettings{1}, &layout);
    RenderTargetCache cache(be, vram, scale);
    ScanoutComposer sc(be, cache);
    Presenter pr(sc);
    std::vector<uint8_t> fb;
    int w = 0, h = 0;
    sc.SetScanoutFbp(0x70000);
    CHECK(!sc.GrabDisplay(&fb, &w, &h) && w == 0 && h == 0);   // no target yet
    RenderTarget& t = cache.GetOrCreate(0x0, 64, 32, 0x00, 0);
    cache.NoteDrawn(0x0);
    be.SetPixel(t.views.color, 1, 0, 9, 8, 7, 6);
    CHECK(sc.GrabDisplay(&fb, &w, &h) && w == 64 && h == 32);   // fallback: most recent drawn
    CHECK(fb.size() == 64u * 32 * 4 && fb[4] == 9 && fb[7] == 6);
    RenderTarget& d = cache.GetOrCreate(0x70000, 128, 64, 0x00, 0);
    (void)d;
    CHECK(sc.GrabDisplay(&fb, &w, &h) && w == 128 && h == 64);   // scanout target wins
    sc.SetContentDimensions(0, 448);
    CHECK(sc.ContentW() == 0);
    sc.SetContentDimensions(640, 448);
    CHECK(sc.ContentW() == 640 && sc.ContentH() == 448);
    pr.MarkEeFrameEnd();
    CHECK(pr.EeFrameCount() == 1);
    d.coherence.has_content = true;
    d.coherence.valid_rect.Union(0, 0, 128, 64);
    pr.Present(vram, layout, CrtcClassifier::Snapshot{}, 0, 640, 448);
    CHECK(be.presents == 1 && be.last_present.source.id == d.views.color.id && pr.FrameCount() == 1);
    CHECK(pr.LastPresent().source == ScanoutComposer::Source::Target && pr.LastPresent().fbp == 0x70000);
    // The blank present: the swap alone, no quad, no mirror byte moved, the counter advances.
    const uint64_t epoch_before = vram.Page(0x70000 / 8192).shadow_epoch;
    pr.PresentBlank(640, 448);
    CHECK(be.presents == 2 && be.last_present.source.id == 0 && pr.FrameCount() == 2);
    CHECK(pr.LastPresent().source == ScanoutComposer::Source::None);
    CHECK(vram.Page(0x70000 / 8192).shadow_epoch == epoch_before && cache.Ledger().Empty());
}

DrawIr FrameDraw(uint32_t fbp, uint32_t fbw, uint8_t psm) {
    DrawIr ir;
    ir.raster.frame.fbp = fbp;
    ir.raster.frame.fbw = fbw;
    ir.raster.frame.psm = psm;
    ir.raster.scissor = DrawIr::Scissor{0, 0, 1023, 1023};
    return ir;
}

render::DrawResult DrewAll(int w, int h) {
    render::DrawResult r;
    r.drew = true;
    r.write_rect = render::Rect{0, 0, w, h};
    r.bbox_valid = true;
    r.bbox_tri[0] = 0; r.bbox_tri[1] = 0; r.bbox_tri[2] = float(w); r.bbox_tri[3] = float(h);
    return r;
}

// The grab's local-memory arm (GrabPolicy::LocalMemory): with no colour target at
// the scanout base the DISPFB tuple is decoded out of local memory -- rows flipped
// into the attachment's bottom-up order, the 16-bit channels widened by the
// tree's one 5 -> 8 rule -- and the most-recently-drawn fallback is taken only when
// the tuple is unknown; a colour target at the base still wins. The default policy
// (TargetFallback) ignores the image entirely.
void GrabLocalMemory() {
    test::FakeBackend be;
    VramImage vram;
    VramLayout layout;
    ScalePolicy scale(ScaleSettings{1}, &layout);
    RenderTargetCache cache(be, vram, scale);
    ScanoutComposer sc(be, cache);
    std::vector<uint8_t> fb;
    int w = 0, h = 0;
    // A 64 x 4 PSMCT16 display buffer at base 0x2000 (FBW 64 px): row y = 5-bit value y + 1
    // in R, x in G; pixel (x, y) at PsmLayout's address.
    const uint32_t base = 0x2000;
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 64; ++x) {
            const uint16_t px = static_cast<uint16_t>((y + 1) | ((x & 31) << 5) | 0x8000u);
            const uint8_t two[2] = {static_cast<uint8_t>(px & 0xFF), static_cast<uint8_t>(px >> 8)};
            vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT16, x, y, base / PsmLayout::kBlockBytes, 1), two, 2);
        }
    vram.SettleWrites();
    sc.SetScanoutFbp(base);
    CHECK(sc.GetGrabPolicy() == ScanoutComposer::GrabPolicy::TargetFallback);
    CHECK(!sc.GrabDisplay(&fb, &w, &h, &vram) && w == 0 && h == 0);   // default policy: targets only, none yet
    CHECK(sc.LastGrabSource() == ScanoutComposer::Source::None);
    sc.SetGrabPolicy(ScanoutComposer::GrabPolicy::LocalMemory);
    CHECK(!sc.GrabDisplay(&fb, &w, &h, &vram));   // tuple unknown (no DISPFB job, no dims): nothing
    sc.SetDispfb(64, 0x02);
    CHECK(!sc.GrabDisplay(&fb, &w, &h, &vram));   // dims unknown
    sc.SetContentDimensions(64, 4);
    CHECK(sc.GrabDisplay(&fb, &w, &h, &vram) && w == 64 && h == 4 && fb.size() == 64u * 4 * 4);
    CHECK(sc.LastGrabSource() == ScanoutComposer::Source::Mirror);
    // bottom-up: buffer row 0 = PS2 row 3 (R = 4 -> 0x21 by replication), row 3 = PS2 row 0 (R = 1 -> 0x08)
    CHECK(fb[0] == ((4 << 3) | (4 >> 2)) && fb[3 * 64 * 4 + 0] == ((1 << 3) | (1 >> 2)));
    CHECK(fb[3 * 64 * 4 + 7 * 4 + 1] == ((7 << 3) | (7 >> 2)) && fb[3] == 0xFF);   // G = x, alpha opaque
    CHECK(!sc.GrabDisplay(&fb, &w, &h) && sc.LastGrabSource() == ScanoutComposer::Source::None);   // no image given
    // A most-recently-drawn target elsewhere does not pre-empt the local-memory decode ...
    RenderTarget& other = cache.GetOrCreate(0x70000, 128, 64, 0x00, 0);
    cache.NoteDrawn(0x70000);
    (void)other;
    CHECK(sc.GrabDisplay(&fb, &w, &h, &vram) && w == 64 && h == 4 && sc.LastGrabSource() == ScanoutComposer::Source::Mirror);
    // ... but an undisplayable PSM falls back to it, and so does the default policy.
    sc.SetDispfb(64, 0x13);   // PSMT8: the CRTC cannot display it
    CHECK(sc.GrabDisplay(&fb, &w, &h, &vram) && w == 128 && h == 64 && sc.LastGrabSource() == ScanoutComposer::Source::Target);
    sc.SetDispfb(64, 0x02);
    sc.SetGrabPolicy(ScanoutComposer::GrabPolicy::TargetFallback);
    CHECK(sc.GrabDisplay(&fb, &w, &h, &vram) && w == 128 && sc.LastGrabSource() == ScanoutComposer::Source::Target);
    sc.SetGrabPolicy(ScanoutComposer::GrabPolicy::LocalMemory);
    // A colour target AT the scanout base wins over local memory.
    RenderTarget& at_base = cache.GetOrCreate(base, 64, 4, 0x02, 0);
    (void)at_base;
    CHECK(sc.GrabDisplay(&fb, &w, &h, &vram) && w == 64 && h == 4 && sc.LastGrabSource() == ScanoutComposer::Source::Target);
}

void Fill(test::FakeBackend& be, RenderTarget& t, uint8_t r, uint8_t g, uint8_t b) {
    for (int y = 0; y < t.identity.height; ++y)
        for (int x = 0; x < t.identity.width; ++x) be.SetPixel(t.views.color, x, y, r, g, b, 0xFF);
}

uint32_t Word(const VramImage& v, uint32_t addr) {
    uint8_t b[4];
    v.ReadView(addr, 4, b);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}

// The display quad's source choice and mirror maintenance (OLD DrawScanout live
// arms): no target -> nothing; a picked target -> its colour attachment over the
// content rect; native (CRTC class + queue tuple + ledger resolve, no fallback) ->
// the deposit is deferred (ledger entry + owed rows), else the drawn rows are
// deposited at the SCANOUT base; the fallback / stale-display picks; the
// stale-display format round trip presents the mirror decode.
void DrawScanoutSource() {
    using C = CrtcClassifier;
    using S = ScanoutComposer;
    test::FakeBackend be;
    VramImage vram;
    VramLayout layout;
    layout.main_fb_fbp = 0x70000;
    layout.native_scanout_fbp_bytes = 0x70000; layout.native_scanout_fbw_px = 512; layout.native_scanout_psm = 0;
    ScalePolicy scale(ScaleSettings{1}, &layout);
    RenderTargetCache cache(be, vram, scale);
    ScanoutComposer sc(be, cache);
    const uint64_t dispfb = 56ull | (8ull << 9);
    const uint64_t disp1 = (50ull << 12) | 0x2A0ull | (2559ull << 32) | (447ull << 44);
    const uint64_t disp2 = (51ull << 12) | 0x2A0ull | (2559ull << 32) | (447ull << 44);
    const C::Snapshot native = C::Classify(0x8023, dispfb, dispfb, disp1, disp2);
    const C::Snapshot other = C::Classify(0x0001, dispfb, dispfb, disp1, disp2);
    const render::Rect win{0, 0, 1024, 896};
    sc.SetScanoutFbp(0x70000);
    sc.SetDispfb(512, 0x00);
    sc.SetContentDimensions(512, 448);
    // No target: nothing presented.
    S::DrawResult r = sc.DrawScanout(vram, layout, native, 1, 1, win);
    CHECK(r.source == S::Source::None && be.presents == 0);
    // The display target, drawn; its pages are not yet GPU-authored -> NOT native:
    // the drawn rows are deposited now, the quad samples the attachment.
    RenderTarget& t = cache.GetOrCreate(0x70000, 512, 448, 0x00, 0);
    Fill(be, t, 0x11, 0x22, 0x33);
    DrawIr d = FrameDraw(0x70000, 512, 0x00);
    t.AfterDraw(d, DrewAll(512, 448), 1, 5);
    cache.AfterDrawTail(t, d, DrewAll(512, 448), 448, 5);
    cache.NoteDrawn(0x70000);
    CHECK(!S::ResolvesToTarget(vram, 0x70000, S::ScanoutFootprintBytes(t)));   // cold head page
    r = sc.DrawScanout(vram, layout, native, 2, 6, win);
    CHECK(r.source == S::Source::Target && !r.native && !r.used_fallback && r.fbp == 0x70000);
    CHECK(be.presents == 1 && be.last_present.source.id == t.views.color.id);
    CHECK(be.last_present.src.w == 512 && be.last_present.src.h == 448 && be.last_present.dst.w == 1024);
    CHECK(!t.coherence.HasDrawnSinceSync() && vram.PendingPages() > 0);
    CHECK(Word(vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 3, 0x70000 / 256, 8)) == 0x80332211u);
    CHECK(cache.Ledger().Empty());
    // The deposit authored the footprint: the next present resolves NATIVE -> no
    // deposit, a ledger entry at the scanout base, the rows owed to the pages.
    CHECK(S::ResolvesToTarget(vram, 0x70000, S::ScanoutFootprintBytes(t)));
    t.AfterDraw(d, DrewAll(512, 448), 2, 7);
    cache.AfterDrawTail(t, d, DrewAll(512, 448), 448, 7);
    const uint32_t pend = vram.PendingPages();
    r = sc.DrawScanout(vram, layout, native, 3, 8, win);
    CHECK(r.source == S::Source::Target && r.native && be.presents == 2);
    CHECK(vram.PendingPages() == pend && t.coherence.HasDrawnSinceSync());
    CHECK(cache.Ledger().Has(0x70000) && cache.Ledger().Entries()[0].epoch == 8);
    CHECK(!vram.Page(0x70000 / 8192).BandEmpty());
    // The CRTC class alone decides nothing without the tuple: a CT16 DISPFB is not
    // native (deposit runs, flushing the entry first); class Other likewise.
    sc.SetDispfb(512, 0x02);
    r = sc.DrawScanout(vram, layout, native, 4, 9, win);
    CHECK(r.source == S::Source::Target && !r.native && cache.Ledger().Empty());
    CHECK(cache.Ledger().Flushes(PendingRtSyncLedger::Reason::Scanout) == 1);
    sc.SetDispfb(512, 0x00);
    t.AfterDraw(d, DrewAll(512, 448), 5, 10);
    cache.AfterDrawTail(t, d, DrewAll(512, 448), 448, 10);
    r = sc.DrawScanout(vram, layout, other, 6, 11, win);
    CHECK(!r.native && !t.coherence.HasDrawnSinceSync());
    // A foreign GPU author inside the footprint: the resolve declines.
    vram.NoteGpuAuthor(VramImage::Span{0x80000, 0x2000}, 0x300000);
    CHECK(!S::ResolvesToTarget(vram, 0x70000, S::ScanoutFootprintBytes(t)));
    vram.NoteGpuAuthor(VramImage::Span{0x80000, 0x2000}, 0x70000);
    CHECK(S::ResolvesToTarget(vram, 0x70000, S::ScanoutFootprintBytes(t)));
    vram.NoteCpuWrite(VramImage::Span{0x80000, 0x2000});          // a CPU page inside: fine
    CHECK(S::ResolvesToTarget(vram, 0x70000, S::ScanoutFootprintBytes(t)));
    // Fallback: no target at the scanout base -> the most recently drawn one, never
    // native, its rows deposited AT THE SCANOUT BASE.
    sc.SetScanoutFbp(0x200000);
    t.AfterDraw(d, DrewAll(512, 448), 7, 12);
    cache.AfterDrawTail(t, d, DrewAll(512, 448), 448, 12);
    r = sc.DrawScanout(vram, layout, native, 8, 13, win);
    CHECK(r.source == S::Source::Target && r.used_fallback && !r.native && r.fbp == 0x70000);
    CHECK(be.last_present.source.id == t.views.color.id);
    CHECK(Word(vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 3, 0x200000 / 256, 8)) == 0x80332211u);
    // Stale-display pick: a display target (CT16 at 0x0) last drawn at frame 1 while
    // the main buffer was drawn at frame 40; at frame 45 the main buffer is presented
    // through the DISPFB format round trip (CT16 encode at 0x0 -> mirror decode).
    sc.SetScanoutFbp(0x0);
    sc.SetDispfb(512, 0x02);
    RenderTarget& f = cache.GetOrCreate(0x0, 512, 448, 0x02, 0);
    DrawIr d16 = FrameDraw(0x0, 512, 0x02);
    f.AfterDraw(d16, DrewAll(512, 448), 1, 1);
    cache.AfterDrawTail(f, d16, DrewAll(512, 448), 448, 1);
    t.AfterDraw(d, DrewAll(512, 448), 40, 40);
    cache.AfterDrawTail(t, d, DrewAll(512, 448), 448, 40);
    const int presents = be.presents;
    r = sc.DrawScanout(vram, layout, native, 45, 41, win);
    CHECK(r.source == S::Source::Mirror && r.used_fallback && r.stale_display && r.fbp == 0x70000);
    CHECK(be.presents == presents + 1 && be.last_present.source.id != t.views.color.id);
    CHECK(be.last_present.src.w == 512 && be.last_present.src.h == 448);
    uint8_t hb[2];
    vram.ReadView(PsmLayout::PixelByteAddress(Psm::CT16, 3, 3, 0, 8), 2, hb);
    CHECK((hb[0] | (hb[1] << 8)) == 0x9882);                        // 5551 of (0x11, 0x22, 0x33, opaque)
    const test::FakeBackend::Surface& ms = be.At(be.last_present.source);
    CHECK(ms.desc.width == 512 && ms.desc.height == 448);
    CHECK(ms.rgba[(3 * 512 + 3) * 4 + 0] == 0x10 && ms.rgba[(3 * 512 + 3) * 4 + 1] == 0x21 && ms.rgba[(3 * 512 + 3) * 4 + 3] == 0xFF);
    // ... the display target too young (frame 20: age 19 <= 30) -> the display target itself.
    r = sc.DrawScanout(vram, layout, native, 20, 42, win);
    CHECK(r.source == S::Source::Target && !r.used_fallback && r.fbp == 0x0);
    // ... the main buffer too old (frame 80: age 40 > 16) -> the display target.
    r = sc.DrawScanout(vram, layout, native, 80, 43, win);
    CHECK(r.fbp == 0x0 && !r.used_fallback);
    // ... a matching class (DISPFB CT32) is the stand-in without the round trip.
    sc.SetDispfb(512, 0x00);
    t.AfterDraw(d, DrewAll(512, 448), 44, 44);
    cache.AfterDrawTail(t, d, DrewAll(512, 448), 448, 44);
    r = sc.DrawScanout(vram, layout, native, 45, 45, win);
    CHECK(r.source == S::Source::Target && r.used_fallback && !r.stale_display && r.fbp == 0x70000);
    CHECK(be.last_present.source.id == t.views.color.id);
    // A deferred 16-bit target inside the display footprint lands before the display sync.
    sc.SetScanoutFbp(0x70000);
    sc.SetDispfb(512, 0x00);
    RenderTarget& p16 = cache.GetOrCreate(0x100000, 64, 64, 0x02, 0);
    Fill(be, p16, 0xFF, 0, 0);
    DrawIr dp = FrameDraw(0x100000, 64, 0x02);
    p16.AfterDraw(dp, DrewAll(64, 64), 46, 46);
    cache.AfterDrawTail(p16, dp, DrewAll(64, 64), 64, 46);
    CHECK(cache.Ledger().Has(0x100000));
    t.coherence.drawn_since_sync = LogicalRect{0, 0, 512, 32};   // the display sync stays off 0x100000
    const uint32_t flushes = cache.Ledger().Flushes(PendingRtSyncLedger::Reason::Scanout);
    r = sc.DrawScanout(vram, layout, other, 47, 47, win);
    CHECK(!cache.Ledger().Has(0x100000) && cache.Ledger().Flushes(PendingRtSyncLedger::Reason::Scanout) == flushes + 1);
    vram.ReadView(PsmLayout::PixelByteAddress(Psm::CT16, 0, 0, 0x100000 / 256, 1), 2, hb);
    CHECK((hb[0] | (hb[1] << 8)) == 0x801F);
    CHECK(vram.Page(0x100000 / 8192).shadow_epoch == 46);
    // A fallback pick is never native even when the class, the tuple and the page
    // resolve (the pages still authored by the destroyed display target) all hold.
    cache.Destroy(0x70000);
    RenderTarget& z = cache.GetOrCreate(0x300000, 512, 448, 0x00, 0);
    Fill(be, z, 0x44, 0x55, 0x66);
    DrawIr dz = FrameDraw(0x300000, 512, 0x00);
    z.AfterDraw(dz, DrewAll(512, 448), 48, 48);
    cache.AfterDrawTail(z, dz, DrewAll(512, 448), 448, 48);
    cache.NoteDrawn(0x300000);
    CHECK(S::ResolvesToTarget(vram, 0x70000, 512u * 448u * 4u));
    r = sc.DrawScanout(vram, layout, native, 49, 49, win);
    CHECK(r.source == S::Source::Target && r.used_fallback && !r.native && r.fbp == 0x300000);
    CHECK(!cache.Ledger().Has(0x70000) && !z.coherence.HasDrawnSinceSync());
    CHECK(Word(vram, PsmLayout::PixelByteAddress(Psm::CT32, 3, 3, 0x70000 / 256, 8)) == 0x80665544u);
}

// The order inside DrawScanout (OLD :24769 native, THEN the :24788-24798 flush,
// THEN the :24799-24815 record): the native resolve reads the page authors BEFORE
// the deferred foreign 16-bit rows under the footprint land, so a natively
// resolving display stays native for this present while those rows are written
// back; the flipped order would decline the resolve and deposit the display
// eagerly. And the deposit-base tail: a 16-bit stand-in presented via the
// fallback deposits at the SCANOUT base and loses the entry of THAT base (none),
// keeping its own.
void DrawScanoutOrderAndTail() {
    using C = CrtcClassifier;
    using S = ScanoutComposer;
    test::FakeBackend be;
    VramImage vram;
    VramLayout layout;
    layout.main_fb_fbp = 0x70000;
    layout.native_scanout_fbp_bytes = 0x70000; layout.native_scanout_fbw_px = 512; layout.native_scanout_psm = 0;
    ScalePolicy scale(ScaleSettings{1}, &layout);
    RenderTargetCache cache(be, vram, scale);
    ScanoutComposer sc(be, cache);
    const uint64_t dispfb = 56ull | (8ull << 9);
    const uint64_t disp1 = (50ull << 12) | 0x2A0ull | (2559ull << 32) | (447ull << 44);
    const uint64_t disp2 = (51ull << 12) | 0x2A0ull | (2559ull << 32) | (447ull << 44);
    const C::Snapshot native = C::Classify(0x8023, dispfb, dispfb, disp1, disp2);
    const render::Rect win{0, 0, 1024, 896};
    sc.SetScanoutFbp(0x70000);
    sc.SetDispfb(512, 0x00);
    sc.SetContentDimensions(512, 448);
    // The display target's first present deposits and authors the whole footprint
    // [0x70000, 0x230000) for 0x70000.
    RenderTarget& t = cache.GetOrCreate(0x70000, 512, 448, 0x00, 0);
    Fill(be, t, 0x11, 0x22, 0x33);
    DrawIr d = FrameDraw(0x70000, 512, 0x00);
    t.AfterDraw(d, DrewAll(512, 448), 1, 5);
    cache.AfterDrawTail(t, d, DrewAll(512, 448), 448, 5);
    cache.NoteDrawn(0x70000);
    S::DrawResult r = sc.DrawScanout(vram, layout, native, 2, 6, win);
    CHECK(r.source == S::Source::Target && !r.native);
    const uint32_t foot = S::ScanoutFootprintBytes(t);
    CHECK(S::ResolvesToTarget(vram, 0x70000, foot));
    // A deferred 16-bit target inside that footprint, drawn once, never synced: its
    // pages still carry the display's authorship (no deposit of its own yet).
    RenderTarget& p16 = cache.GetOrCreate(0x80000, 64, 64, 0x02, 0);
    Fill(be, p16, 0xFF, 0, 0);
    DrawIr dp = FrameDraw(0x80000, 64, 0x02);
    p16.AfterDraw(dp, DrewAll(64, 64), 3, 7);
    cache.AfterDrawTail(p16, dp, DrewAll(64, 64), 64, 7);
    CHECK(cache.Ledger().Has(0x80000) && S::ResolvesToTarget(vram, 0x70000, foot));
    CHECK(vram.Page(0x80000 / 8192).author_fbp == 0x70000);
    t.AfterDraw(d, DrewAll(512, 448), 4, 8);
    cache.AfterDrawTail(t, d, DrewAll(512, 448), 448, 8);
    const uint32_t flushes = cache.Ledger().Flushes(PendingRtSyncLedger::Reason::Scanout);
    r = sc.DrawScanout(vram, layout, native, 5, 9, win);
    CHECK(r.source == S::Source::Target && r.native && !r.used_fallback && r.fbp == 0x70000);
    CHECK(cache.Ledger().Flushes(PendingRtSyncLedger::Reason::Scanout) == flushes + 1);
    CHECK(!cache.Ledger().Has(0x80000));
    uint8_t hb[2];
    vram.ReadView(PsmLayout::PixelByteAddress(Psm::CT16, 0, 0, 0x80000 / 256, 1), 2, hb);
    CHECK((hb[0] | (hb[1] << 8)) == 0x801F);                        // the foreign rows landed ...
    CHECK(vram.Page(0x80000 / 8192).shadow_epoch == 7);              // ... at the entry's epoch
    CHECK(cache.Ledger().Has(0x70000) && cache.Ledger().Entries()[0].epoch == 9);   // ... the display deferred
    CHECK(t.coherence.HasDrawnSinceSync());                           // its drawn rect not consumed
    // The elided mark (OLD NoteGPUElided over the footprint, AFTER the flush)
    // re-authors every footprint page for the display with the owed band -- the
    // flushed rows included -- so the next present resolves native again.
    CHECK(vram.Page(0x80000 / 8192).author_fbp == 0x70000 && !vram.Page(0x80000 / 8192).BandEmpty());
    CHECK(S::ResolvesToTarget(vram, 0x70000, foot));
    r = sc.DrawScanout(vram, layout, native, 6, 10, win);
    CHECK(r.native && cache.Ledger().Has(0x70000) && cache.Ledger().Entries()[0].epoch == 10);
    CHECK(cache.Ledger().Flushes(PendingRtSyncLedger::Reason::Scanout) == flushes + 2);   // its own entry re-recorded
    // The tail on the deposit base: a 16-bit most-recent-drawn stand-in with its
    // own entry, presented via the fallback at a scanout base with no target,
    // deposits AT the scanout base (0x801F there) and keeps its own entry -- the
    // tail removes the entry of the base it deposited to, exactly OLD's argument.
    sc.SetScanoutFbp(0x200000);
    RenderTarget& s16 = cache.GetOrCreate(0x300000, 64, 64, 0x02, 0);
    Fill(be, s16, 0xFF, 0, 0);
    DrawIr ds = FrameDraw(0x300000, 64, 0x02);
    s16.AfterDraw(ds, DrewAll(64, 64), 7, 11);
    cache.AfterDrawTail(s16, ds, DrewAll(64, 64), 64, 11);
    cache.NoteDrawn(0x300000);
    CHECK(cache.Ledger().Has(0x300000));
    const uint32_t flushes2 = cache.Ledger().Flushes(PendingRtSyncLedger::Reason::Scanout);
    r = sc.DrawScanout(vram, layout, native, 8, 12, win);
    CHECK(r.source == S::Source::Target && r.used_fallback && !r.native && r.fbp == 0x300000);
    CHECK(be.last_present.source.id == s16.views.color.id);
    CHECK(cache.Ledger().Flushes(PendingRtSyncLedger::Reason::Scanout) == flushes2);   // nothing under 0x200000
    CHECK(cache.Ledger().Has(0x300000));                              // its own entry survives the tail
    CHECK(!s16.coherence.HasDrawnSinceSync());                        // the drawn rect was consumed
    vram.ReadView(PsmLayout::PixelByteAddress(Psm::CT16, 0, 0, 0x200000 / 256, 1), 2, hb);
    CHECK((hb[0] | (hb[1] << 8)) == 0x801F);                        // deposited at the scanout base
    CHECK(vram.Page(0x200000 / 8192).author_fbp == 0x200000 && vram.Page(0x200000 / 8192).shadow_epoch == 12);
    vram.ReadView(PsmLayout::PixelByteAddress(Psm::CT16, 0, 0, 0x300000 / 256, 1), 2, hb);
    CHECK((hb[0] | (hb[1] << 8)) == 0);                             // nothing at its own base
}

}  // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    const bool all = which == "all";
    if (all || which == "viewport") ViewportVectors();
    if (all || which == "crtc") Crtc();
    if (all || which == "grab") Grab();
    if (all || which == "grab_local") GrabLocalMemory();
    if (all || which == "drawscanout") { DrawScanoutSource(); DrawScanoutOrderAndTail(); }
    std::printf("test_present %s: %d checks, %d failures\n", which.c_str(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
