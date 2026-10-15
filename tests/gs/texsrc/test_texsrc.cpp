// tests/gs/texsrc/test_texsrc.cpp -- texsrc units over the CPU FakeBackend: the
// window-in-target geometry, the index-lane mask, the serve predicates, the L2
// entry cap / probation pool, the CLUT texture vs a CPU reference, DISPLAY
// magnification, the content key's address independence, DecodeRect == crop,
// per-call reset, the never-stale keep rule, the fmt 5 lane-live rule, the resolver
// tiers against OLD's predicates (M1-M12 of the 6D' review), the UV region
// derivation, the decode caches on a miss.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gs/format/clut_layout.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/hle/targets/render_target_cache.h"
#include "engine/gs/hle/targets/target_refresher.h"
#include "engine/gs/hle/texsrc/clut_heal_cache.h"
#include "engine/gs/hle/texsrc/clut_source.h"
#include "engine/gs/hle/texsrc/content_key.h"
#include "engine/gs/hle/texsrc/decode_cache.h"
#include "engine/gs/hle/texsrc/texture_decoder.h"
#include "engine/gs/hle/texsrc/texture_resolver.h"
#include "engine/gs/hle/texsrc/uv_bbox.h"
#include "engine/gs/hle/texsrc/uv_region.h"
#include "engine/gs/hle/texsrc/window_in_target.h"
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

void WindowInTargetPort() {
    using W = WindowInTarget;
    uint32_t off = 0xDEAD;
    const uint32_t base = 0x280000, alt = 0x2f0000;
    CHECK(W::Contains(base + 32 * 0x2000, base, 512, 448, 0x02, &off) && off == 32);
    CHECK(W::Contains(base + 40 * 0x2000, base, 512, 448, 0x02, &off) && off == 40);
    CHECK(W::Contains(base + 48 * 0x2000, base, 512, 448, 0x0A, &off) && off == 48);
    CHECK(W::Contains(alt + 8 * 0x2000, alt, 512, 448, 0x02, &off) && off == 8);
    CHECK(!W::Contains(base, base, 512, 448, 0x02, &off));
    CHECK(!W::Contains(base - 0x2000, base, 512, 448, 0x02, &off));
    CHECK(!W::Contains(base + 512 * 448 * 2, base, 512, 448, 0x02, &off));
    CHECK(!W::Contains(base + 32 * 0x2000 + 0x100, base, 512, 448, 0x02, &off));
    CHECK(!W::Contains(base + 32 * 0x2000 + 0x1000, base, 512, 448, 0x02, &off));
    CHECK(!W::Contains(base + 32 * 0x2000, base, 512, 448, 0x00, &off));
    CHECK(!W::Contains(base + 32 * 0x2000, base, 512, 448, 0x30, &off));
    CHECK(!W::Contains(base + 32 * 0x2000, base, 512, 448, 0x32, &off));
    CHECK(!W::Contains(base + 32 * 0x2000, base, 0, 448, 0x02, &off));
    CHECK(!W::Contains(base + 32 * 0x2000, base, 512, 0, 0x02, &off));
    CHECK(!W::Contains(base + 64 * 0x2000, base, 512, 1024, 0x02, &off));
    CHECK(W::Contains(base + 63 * 0x2000, base, 512, 1024, 0x02, &off) && off == 63);
    CHECK(W::Contains(base + 32 * 0x2000, base, 512, 448, 0x02, nullptr));
    const int self = W::PackSelf6(1, 6, 6, 48);
    CHECK((self & 0xF) == 1 && ((self >> 4) & 0xF) == 6 && ((self >> 8) & 0xF) == 6);
    CHECK((self & 0x1000) == 0);
    CHECK(W::UnpackPageOff(self) == 48);
    CHECK(W::UnpackPageOff(W::PackSelf6(2, 7, 7, 0)) == 0);
    const int hi = W::PackSelf6(0xF, 0xA, 0xA, 63);
    CHECK((hi & 0xF) == 0xF && ((hi >> 4) & 0xF) == 0xA && W::UnpackPageOff(hi) == 63);
    const int target_w = 512;
    for (uint32_t pgoff : {0u, 8u, 32u, 40u, 48u})
        for (int y : {0, 1, 2, 15, 16, 33, 63})
            for (int x : {0, 1, 7, 15, 16, 63}) {
                const auto b = W::Fmt6Remap(x, y, 0, 1, target_w);
                const auto o = W::Fmt6Remap(x, y, pgoff, 1, target_w);
                CHECK(o.lane == b.lane);
                const int page0 = (b.Y / 64) * 8 + (b.X / 64), page1 = (o.Y / 64) * 8 + (o.X / 64);
                CHECK(page1 == page0 + static_cast<int>(pgoff));
                CHECK((o.X % 64) == (b.X % 64) && (o.Y % 64) == (b.Y % 64));
            }
    const auto t = W::Fmt6Remap(0, 0, 8, 1, target_w);
    CHECK(t.X >= 0 && t.X < 64 && t.Y >= 64 && t.Y < 128);
    CHECK(W::Fmt6Remap(0, 0, 0, 1, target_w).lane == 0 && W::Fmt6Remap(0, 1, 0, 1, target_w).lane == 0);
    CHECK(W::Fmt6Remap(0, 2, 0, 1, target_w).lane == 1 && W::Fmt6Remap(0, 3, 0, 1, target_w).lane == 1);
    CHECK(W::Fmt6IndexFromRgba(0, 0, 0, 0, 0) == 0 && W::Fmt6IndexFromRgba(0, 0, 0, 127, 1) == 0);
    CHECK(W::Fmt6IndexFromRgba(0, 0, 0, 128, 1) == 0x80 && W::Fmt6IndexFromRgba(0, 0, 0, 255, 1) == 0x80);
    CHECK(W::Fmt6IndexFromRgba(0, 0, 0, 255, 0) == 0 && W::Fmt6IndexFromRgba(0xFF, 0, 0, 0, 0) == 0x1F);
    CHECK(W::Fmt6IndexFromRgba(0, 0xFF, 0, 0, 0) == 0xE0 && W::Fmt6IndexFromRgba(0, 0xFF, 0, 0, 1) == 0x03);
    CHECK(W::Fmt6IndexFromRgba(0, 0, 0xFF, 0, 1) == 0x7C);
    CHECK(W::Fmt6IndexFromRgba(0xFF, 0xFF, 0xFF, 255, 1) == 0xFF && W::Fmt6IndexFromRgba(0xFF, 0xFF, 0xFF, 255, 0) == 0xFF);
    CHECK(W::Fmt6IndexFromRgba(7, 0, 0, 0, 0) == 0 && W::Fmt6IndexFromRgba(8, 0, 0, 0, 0) == 1);
}

void Idx16LanePort() {
    CHECK(RenderTarget::Fbmsk16From32(0x3fffu) == 0x00FFu);
    CHECK(RenderTarget::Fbmsk16From32(0u) == 0u);
    CHECK(RenderTarget::Fbmsk16From32(0xffffffffu) == 0xFFFFu);
    const uint16_t k = RenderTarget::Fbmsk16From32(0x3fffu);
    CHECK((k & 0xFF00u) == 0u && (k & 0x00FFu) == 0x00FFu);
    int serves = 0;
    uint32_t served = 0;
    for (uint32_t m = 0; m <= 0xFFFFu; m++)
        if (static_cast<uint16_t>(m) == RenderTarget::kIndexLaneKeep16) { serves++; served = m; }
    CHECK(serves == 1 && served == RenderTarget::kIndexLaneKeep16);
    CHECK(RenderTarget::kIndexLaneKeep16 != 0x0000 && RenderTarget::kIndexLaneKeep16 != 0xFFFF);
}

void S9ShadowPredicates() {
    using R = TextureResolver;
    CHECK(R::RtKeepServe(true, true, 5, 5));
    CHECK(R::RtKeepServe(true, true, 4, 5));
    CHECK(!R::RtKeepServe(true, true, 6, 5));
    CHECK(!R::RtKeepServe(false, true, 0, 5));
    CHECK(!R::RtKeepServe(true, false, 0, 5));
    CHECK(ClutSource::PrescaleAlpha(0) == 0 && ClutSource::PrescaleAlpha(64) == 128);
    CHECK(ClutSource::PrescaleAlpha(127) == 254 && ClutSource::PrescaleAlpha(128) == 255);
    CHECK(ClutSource::PrescaleAlpha(255) == 255);
    CHECK(R::Is16BitClass(0x02) && R::Is16BitClass(0x0A) && R::Is16BitClass(0x32) && R::Is16BitClass(0x3A));
    CHECK(!R::Is16BitClass(0x00) && !R::Is16BitClass(0x01) && !R::Is16BitClass(0x30));
    CHECK(R::IsIndexedPsm(0x13) && R::IsIndexedPsm(0x1B) && !R::IsIndexedPsm(0x02));
}

render::SurfaceId Tex(test::FakeBackend& be, int w, int h) {
    render::SurfaceDesc d;
    d.width = w;
    d.height = h;
    return be.CreateSurface(d);
}

void L2EntriesPort() {
    test::FakeBackend be;
    DecodeCache c(be);
    const size_t cap = DecodeCache::kL2MaxEntries, su = DecodeCache::kL2SuMaxEntries;
    const uint64_t base = cap + 100;
    for (size_t i = 0; i < su; i++) c.L2Insert(base + i, Tex(be, 1, 1), 1, 1, true);
    CHECK(c.L2ProbCount() == su);
    CHECK(c.L2Contains(base));
    CHECK(c.L2Lookup(base, 1, 1).id != 0);
    CHECK(!c.L2IsProbation(base));
    CHECK(c.L2ProbCount() == su - 1);
    c.L2Insert(base + su, Tex(be, 1, 1), 1, 1, true);
    c.L2Insert(base + su + 1, Tex(be, 1, 1), 1, 1, true);
    CHECK(c.L2ProbCount() == su);
    CHECK(c.L2Contains(base));          // the rescued (tenured) entry survived
    CHECK(!c.L2Contains(base + 1));     // the oldest UNRESCUED entry was evicted
    CHECK(c.L2Contains(base + 2));      // exactly one eviction per overflow insert
    for (size_t i = 0; i <= su + 1; i++) (void)c.L2Lookup(base + i, 1, 1);
    CHECK(c.L2ProbCount() == 0 && c.L2ProbBytes() == 0);
    c.DropAll();
    for (size_t i = 0; i < cap; i++) c.L2Insert(i + 1, Tex(be, 1, 1), 1, 1, false);   // tenured (the cap test)
    CHECK(c.L2Size() == cap && c.L2Contains(1) && c.L2Contains(cap));
    c.L2Insert(cap + 1, Tex(be, 1, 1), 1, 1, false);
    CHECK(c.L2Size() == cap);
    CHECK(!c.L2Contains(1) && c.L2Contains(2) && c.L2Contains(cap + 1));
    // Byte budget: a single 320 MiB + 4 texel entry evicts down.
    c.DropAll();
    c.L2Insert(7, Tex(be, 1, 1), 8192, 8192, false);   // 256 MiB
    c.L2Insert(8, Tex(be, 1, 1), 8192, 4096, false);   // + 128 MiB > 320 MiB
    CHECK(c.L2Size() == 1 && c.L2Contains(8) && !c.L2Contains(7));
}

void ClutSourceVsReference() {
    test::FakeBackend be;
    VramImage vram;
    const uint32_t cbp = 0x3000;
    std::vector<uint8_t> bytes(0x400);
    for (uint32_t i = 0; i < 0x400; i++) bytes[i] = static_cast<uint8_t>(i * 7 + (i >> 3));
    vram.WriteStream(cbp, bytes.data(), bytes.size());
    ClutSource cs(be);
    for (bool fold : {false, true}) {
        render::SurfaceId t = cs.Ensure(cbp, fold, vram);
        CHECK(t.id != 0);
        const auto& s = be.At(t);
        CHECK(s.desc.width == 256 && s.desc.height == 2);
        // CPU reference: entry i at CSM1 grid position (folded once / twice).
        for (uint32_t i = 0; i < 256; i++) {
            const uint32_t g = fold ? ClutLayout::Csm1Index(i) : ClutLayout::Csm1Index(ClutLayout::Csm1Index(i));
            const uint32_t off = PsmLayout::PixelAddress(Psm::CT32, g % 16, g / 16, 0, 1) * 4;
            const uint8_t* p0 = &s.rgba[i * 4];
            const uint8_t* p1 = &s.rgba[(256 + i) * 4];
            const bool ok0 = p0[0] == bytes[off] && p0[1] == bytes[off + 1] && p0[2] == bytes[off + 2] &&
                             p0[3] == ClutSource::PrescaleAlpha(bytes[off + 3]);
            const bool ok1 = p1[0] == bytes[off] && p1[3] == bytes[off + 3];
            if (!ok0 || !ok1) { CHECK(ok0 && ok1); break; }
        }
        if (!fold) CHECK(be.At(t).rgba[0] == bytes[0]);   // double fold == identity: entry 0 at 0
    }
    CHECK(cs.Count() == 2);
    const int uploads = be.uploads;
    (void)cs.Ensure(cbp, false, vram);
    CHECK(be.uploads == uploads);   // same bytes -> no rebuild
    bytes[5] ^= 0xFF;
    vram.WriteStream(cbp, bytes.data(), bytes.size());
    (void)cs.Ensure(cbp, false, vram);
    CHECK(be.uploads == uploads + 1);
}

void DisplayMagnification() {
    using TD = TextureDecoder;
    auto reg = [](uint32_t dx, uint32_t dy, uint32_t magh, uint32_t magv, uint32_t dw, uint32_t dh) {
        return (uint64_t)dx | ((uint64_t)dy << 12) | ((uint64_t)magh << 23) | ((uint64_t)magv << 27) |
               ((uint64_t)dw << 32) | ((uint64_t)dh << 44);
    };
    TD::Display d = TD::ParseDisplay(reg(636, 50, 3, 0, 2559, 447));
    CHECK(d.dx == 636 && d.dy == 50 && d.magh == 3 && d.magv == 0 && d.dw == 2559 && d.dh == 447);
    CHECK(d.pixel_width == 640 && d.pixel_height == 448);
    d = TD::ParseDisplay(reg(0, 0, 4, 1, 2559, 895));
    CHECK(d.pixel_width == 512 && d.pixel_height == 448);
    for (uint32_t magh = 0; magh < 6; magh++)   // DW is 12 bits: 640 px fit up to MAGH 5
        for (uint32_t magv = 0; magv < 4; magv++) {
            d = TD::ParseDisplay(reg(0, 0, magh, magv, 639 * (magh + 1) + magh, 447 * (magv + 1) + magv));
            CHECK(d.pixel_width == 640 && d.pixel_height == 448);
        }
    TD::Dispfb f = TD::ParseDispfb(0x1CE | (8u << 9) | (0u << 15));
    CHECK(f.fbp_bytes == 0x1CE * 8192u && f.fbw_px == 512 && f.psm == 0);
    f = TD::ParseDispfb((uint64_t)0x2 | (10u << 9) | (2u << 15) | ((uint64_t)5 << 32) | ((uint64_t)7 << 43));
    CHECK(f.fbp_bytes == 0x4000 && f.fbw_px == 640 && f.psm == 2 && f.dbx == 5 && f.dby == 7);
}

TexReadView View(uint32_t tbp, uint32_t psm, uint32_t tw, uint32_t th, uint32_t tbw) {
    TexReadView v;
    v.tbp = tbp; v.psm = psm; v.tw = tw; v.th = th; v.tbw = tbw;
    return v;
}

void Inv18ContentKeyAddressFree() {
    std::vector<uint8_t> win(64 * 64 * 4);
    for (size_t i = 0; i < win.size(); i++) win[i] = static_cast<uint8_t>(i * 13);
    TexReadView a = View(0x100000, 0x00, 6, 6, 64), b = View(0x300000, 0x00, 6, 6, 64);
    b.cbp = 0x2000;
    ContentKey::Rect full;
    for (bool fast : {false, true}) {
        const uint64_t ka = ContentKey::Compute(a, full, win.data(), win.size(), nullptr, 0, nullptr, 0, fast);
        const uint64_t kb = ContentKey::Compute(b, full, win.data(), win.size(), nullptr, 0, nullptr, 0, fast);
        CHECK(ka == kb);   // TBP / CBP never enter the key
        TexReadView c = a;
        c.tbw = 128;
        CHECK(ContentKey::Compute(c, full, win.data(), win.size(), nullptr, 0, nullptr, 0, fast) != ka);
        win[100] ^= 1;
        CHECK(ContentKey::Compute(a, full, win.data(), win.size(), nullptr, 0, nullptr, 0, fast) != ka);
        win[100] ^= 1;
        ContentKey::Rect sub; sub.x0 = 8; sub.y0 = 8; sub.w = 16; sub.h = 16;
        CHECK(ContentKey::IsSubRect(a, sub) && !ContentKey::IsSubRect(a, full));
        CHECK(ContentKey::Compute(a, sub, win.data(), win.size(), nullptr, 0, nullptr, 0, fast) != ka);
        ContentKey::Rect whole; whole.w = 64; whole.h = 64;
        CHECK(ContentKey::Compute(a, whole, win.data(), win.size(), nullptr, 0, nullptr, 0, fast) == ka);
    }
    // Reference values of the FNV variant over fixed inputs, computed independently
    // from the OLD key algorithm (basis, "R125CK" salt, the 7-word tuple, the 3 TEXA
    // bytes, the window bytes, the CLUT bytes -- byte-serial FNV-1a).
    std::vector<uint8_t> z(16, 0);
    const uint64_t ref = ContentKey::Compute(View(0, 0, 2, 2, 64), full, z.data(), z.size(), nullptr, 0, nullptr, 0, false);
    CHECK(ref == 14780253174488400579ull);
    std::vector<uint8_t> tw(64 * 64), tc(1024);
    for (size_t i = 0; i < tw.size(); i++) tw[i] = static_cast<uint8_t>(i * 13);
    for (size_t i = 0; i < tc.size(); i++) tc[i] = static_cast<uint8_t>(i * 7);
    const uint64_t ref8 = ContentKey::Compute(View(0x300000, 0x13, 6, 6, 64), full, tw.data(), tw.size(),
                                              tc.data(), tc.size(), nullptr, 0, false);
    CHECK(ref8 == 5842735931578600318ull);
    uint32_t s, n;
    ContentKey::Footprint(a, &s, &n);
    CHECK(s == 0x100000 && n == 64 * 64 * 4);
}

void Inv19DecodeRectIsCrop() {
    std::vector<uint8_t> vram(VramImage::kBytes, 0);
    for (size_t i = 0; i < 128 * 64 * 4; i++) vram[0x80000 + i] = static_cast<uint8_t>(i * 31 + 7);
    TexReadView v = View(0x80000, 0x00, 7, 6, 128);
    v.texa_ta0 = 0x40;
    std::vector<uint32_t> full, rect;
    TextureDecoder::Decode(vram.data(), v, nullptr, nullptr, &full);
    CHECK(full.size() == 128 * 64);
    TextureDecoder::DecodeRect(vram.data(), v, 20, 9, 40, 30, nullptr, nullptr, &rect);
    CHECK(rect.size() == 40 * 30);
    bool same = true;
    for (int y = 0; y < 30 && same; y++)
        for (int x = 0; x < 40; x++)
            if (rect[y * 40 + x] != full[(y + 9) * 128 + (x + 20)]) { same = false; break; }
    CHECK(same);
    CHECK((full[0] >> 24) == 200u);   // PS2 alpha byte 100 (vram[0x80003]) -> GPU 200
    // CT16 with TEXA and an indexed T8 view both crop too.
    TexReadView v16 = View(0x80000, 0x02, 6, 6, 64);
    TextureDecoder::Decode(vram.data(), v16, nullptr, nullptr, &full);
    TextureDecoder::DecodeRect(vram.data(), v16, 3, 5, 17, 11, nullptr, nullptr, &rect);
    same = true;
    for (int y = 0; y < 11 && same; y++)
        for (int x = 0; x < 17; x++)
            if (rect[y * 17 + x] != full[(y + 5) * 64 + (x + 3)]) { same = false; break; }
    CHECK(same);
    std::vector<uint32_t> scan;
    CHECK(TextureDecoder::DecodeScanoutBuffer(vram.data(), 0x80000, 128, 0x00, 128, 64, &scan));
    CHECK(scan.size() == 128 * 64 && (scan[0] >> 24) == 0xFF);
    CHECK(!TextureDecoder::DecodeScanoutBuffer(vram.data(), 0x80000, 128, 0x13, 8, 8, &scan));
}

struct World {
    test::FakeBackend be;
    VramImage vram;
    VramLayout layout;
    ScaleSettings ss;
    ScalePolicy scale{ss, &layout};
    RenderTargetCache targets{be, vram, scale};
    TargetRefresher refresher{be, vram, targets};
    TextureResolver resolver{be, vram, targets, refresher, layout};
};

DrawIr TexDraw(uint32_t tbp, uint32_t psm, uint32_t tw, uint32_t th, uint32_t tbw, uint32_t fbp) {
    DrawIr ir;
    ir.tex_enabled = true;
    ir.tex = View(tbp, psm, tw, th, tbw);
    ir.raster.frame.fbp = fbp;
    ir.raster.frame.fbw = 64;
    return ir;
}

void Inv17PerCallReset() {
    World w;
    RenderTarget& t = w.targets.GetOrCreate(0x100000, 64, 64, 0x00, 1);
    t.coherence.valid_rect = LogicalRect{0, 0, 64, 64};
    t.coherence.has_content = true;
    t.coherence.last_draw_frame = 5;
    t.coherence.last_zbp = 0x200000;
    t.coherence.last_zpsm = 0x30;
    t.coherence.last_z_wrote = true;
    t.identity.render_psm = 0x00;
    t.identity.render_fbw = 64;
    TextureResolver::Context ctx;
    ctx.frame_count = 5;
    // A depth read fills depth_fmt; the next plain read must not carry it.
    DrawIr z = TexDraw(0x200000, 0x30, 6, 6, 64, 0x300000);
    TextureSource a = w.resolver.ResolveRead(z, ctx);
    CHECK(a.enabled && a.owner.fmt_case == 1 && a.transform.depth_fmt == 1);
    DrawIr c = TexDraw(0x100000, 0x00, 6, 6, 64, 0x300000);
    TextureSource b = w.resolver.ResolveRead(c, ctx);
    CHECK(b.enabled && b.owner.kind == TextureSource::OwnerKind::Surface);
    CHECK(b.owner.fmt_case == 0 && b.transform.depth_fmt == 0 && b.transform.idx_fmt == 0);
    CHECK(b.owner.clut.id == 0 && b.owner.partner.id == 0 && !b.transform.region.applied);
    DrawIr none = c;
    none.tex_enabled = false;
    TextureSource n = w.resolver.ResolveRead(none, ctx);
    CHECK(!n.enabled && n.declined == TextureSource::Declined::NoTexture && n.owner.surface.id == 0);
}

// A target with valid, drawn content, last drawn at `frame` with gfx epoch `epoch`.
RenderTarget& MakeTarget(World& w, uint32_t fbp, int tw, int th, uint32_t psm, uint32_t fbw,
                         uint32_t frame, uint64_t epoch) {
    RenderTarget& t = w.targets.GetOrCreate(fbp, tw, th, psm, epoch);
    t.coherence.valid_rect = LogicalRect{0, 0, tw, th};
    t.coherence.drawn_since_sync = LogicalRect{0, 0, tw, th};
    t.coherence.has_content = true;
    t.coherence.last_draw_frame = frame;
    t.coherence.last_draw_gfx_epoch = epoch;
    t.identity.render_psm = psm;
    t.identity.render_fbw = fbw;
    return t;
}

// An EE write over [addr, addr+1) settled `times` times: the page's upload epoch
// advances past any earlier draw epoch.
void NewerUpload(World& w, uint32_t addr, int times) {
    uint8_t byte = 0x11;
    for (int i = 0; i < times; i++) { w.vram.WriteStream(addr, &byte, 1); w.vram.SettleWrites(); }
}

bool ServesSurface(const TextureSource& s, render::SurfaceId id) {
    return s.enabled && s.owner.kind == TextureSource::OwnerKind::Surface && s.owner.surface.id == id.id;
}

// OLD's colour-serve keep rule: the keep test runs only for a target NOT drawn in
// the last two frames whose pages are CPU-dirty; then a newer EE upload over the
// read footprint rescues the window into VRAM and declines, otherwise it serves.
void Inv15NeverStale() {
    World w;
    RenderTarget& t = MakeTarget(w, 0x100000, 64, 64, 0x00, 64, 5, 3);
    w.be.SetPixel(t.views.color, 1, 1, 10, 20, 30, 255);
    TextureResolver::Context ctx;
    ctx.frame_count = 5;
    ctx.active_fbp = 0x300000;
    DrawIr c = TexDraw(0x100000, 0x00, 6, 6, 64, 0x300000);
    // No newer upload: the target serves.
    TextureSource a = w.resolver.ResolveRead(c, ctx);
    CHECK(ServesSurface(a, t.views.color));
    CHECK(w.vram.PendingPages() == 0);
    // A newer upload over the footprint AND CPU-dirty pages, but the target was
    // drawn THIS frame: the attachment is the freshest copy -- serve, no deposit.
    NewerUpload(w, 0x100000 + 100, 4);
    CHECK(w.resolver.WindowEpoch(0x100000, 64 * 64 * 4) > 3);
    w.refresher.MarkPagesCpuDirty(0x100000, 64 * 64 * 4);
    TextureSource b = w.resolver.ResolveRead(c, ctx);
    CHECK(ServesSurface(b, t.views.color));
    CHECK(w.vram.PendingPages() == 0);
    // Not recently drawn (frame 20), dirty, newer upload: rescue the read window at
    // the target's own draw epoch, then decline to the decode (drawn_since_sync
    // survives -- a rescue is not a writeback).
    ctx.frame_count = 20;
    TextureSource d = w.resolver.ResolveRead(c, ctx);
    CHECK(!ServesSurface(d, t.views.color));
    CHECK(d.declined == TextureSource::Declined::StaleTarget);
    CHECK(w.vram.PendingPages() > 0);
    CHECK(t.coherence.HasDrawnSinceSync());
    CHECK(d.enabled && d.owner.kind == TextureSource::OwnerKind::Decoded);
    // Not recent, dirty, NO newer upload (the draw epoch is newer than every page): serve.
    t.coherence.last_draw_gfx_epoch = w.vram.UploadEpoch() + 1;
    TextureSource e = w.resolver.ResolveRead(c, ctx);
    CHECK(ServesSurface(e, t.views.color));
    // Not recent, newer upload, pages NOT dirty: the keep block is skipped -- serve.
    t.coherence.last_draw_gfx_epoch = 3;
    w.refresher.ClearPagesCpuDirty(0x100000, 64 * 64 * 4);
    TextureSource f = w.resolver.ResolveRead(c, ctx);
    CHECK(ServesSurface(f, t.views.color));
    // The read-rect fallback (CX8): not recent, dirty, the footprint's max upload
    // epoch is newer than the draw ONLY through a page the read does not touch
    // (page 1 = rows 32..63) -- a 64x32 read of rows 0..31 keeps serving, the full
    // 64x64 read (both pages) rescues + declines; once page 0 is newer too, the
    // 64x32 read declines as well.
    w.vram.CommitDeposits();
    t.coherence.last_draw_gfx_epoch = w.vram.UploadEpoch() + 1;   // every page older than the draw
    NewerUpload(w, 0x102000 + 100, 4);                             // page 1 only
    CHECK(w.resolver.WindowEpoch(0x100000, 64 * 64 * 4) > t.coherence.last_draw_gfx_epoch);
    w.refresher.MarkPagesCpuDirty(0x100000, 64 * 64 * 4);
    DrawIr half = TexDraw(0x100000, 0x00, 6, 5, 64, 0x300000);
    CHECK(w.resolver.ReadRectUploadEpoch(half.tex) <= t.coherence.last_draw_gfx_epoch);
    TextureSource g = w.resolver.ResolveRead(half, ctx);
    CHECK(ServesSurface(g, t.views.color));
    CHECK(w.vram.PendingPages() == 0);
    TextureSource h = w.resolver.ResolveRead(c, ctx);
    CHECK(!ServesSurface(h, t.views.color) && h.declined == TextureSource::Declined::StaleTarget);
    CHECK(w.vram.PendingPages() > 0);
    w.vram.CommitDeposits();
    NewerUpload(w, 0x100000 + 100, 1);                             // now page 0 too
    TextureSource i = w.resolver.ResolveRead(half, ctx);
    CHECK(!ServesSurface(i, t.views.color) && i.declined == TextureSource::Declined::StaleTarget);
    CHECK(w.vram.PendingPages() > 0);
}

// M1 / M2 / M3 / M4 / M6 / M12 over the colour and indexed paths.
void ResolveTiersColor() {
    World w;
    TextureResolver::Context ctx;
    ctx.frame_count = 5;
    ctx.active_fbp = 0x300000;
    // --- M2: the keep-fall rescue encodes in the READ's format. A CT24 read of a
    // CT32 target: byte 3 of every deposited pixel is preserved from VRAM.
    RenderTarget& t = MakeTarget(w, 0x100000, 64, 64, 0x00, 64, 5, 3);
    for (int y = 32; y < 64; y++) w.be.SetPixel(t.views.color, 0, y, 0x10, 0x20, 0x30, 255);
    uint8_t marker = 0x5A;
    w.vram.WriteStream(0x102003, &marker, 1);   // byte 3 of pixel (0, 32): row 32 = page 1 of a 64-wide CT32
    w.vram.SettleWrites();
    w.vram.NoteGpuAuthor(VramImage::Span{0x100000, 0x4000}, 0x100000);   // the target drew after that upload
    NewerUpload(w, 0x100000 + 100, 4);           // page 0 newer than the draw
    w.refresher.MarkPagesCpuDirty(0x100000, 0x4000);
    ctx.frame_count = 20;
    DrawIr c24 = TexDraw(0x100000, 0x01, 6, 6, 64, 0x300000);
    // The write at 0x102003 stamped page 1 too: make the draw newer than it so the
    // rescue lands there (page 0 keeps its newer upload).
    t.coherence.last_draw_gfx_epoch = w.vram.Page(0x102000 / VramImage::kPageBytes).upload_epoch;
    TextureSource r = w.resolver.ResolveRead(c24, ctx);
    CHECK(r.declined == TextureSource::Declined::StaleTarget && r.owner.kind == TextureSource::OwnerKind::Decoded);
    uint8_t px[4];
    w.vram.ReadView(0x102000, 4, px);
    CHECK(px[0] == 0x10 && px[1] == 0x20 && px[2] == 0x30);
    CHECK(px[3] == 0x5A);   // CT24 encode: the alpha lane is not written (a CT32 encode would store 0x80)
    // --- M1 negative form: the same read recently drawn serves regardless.
    ctx.frame_count = 6;
    CHECK(ServesSurface(w.resolver.ResolveRead(c24, ctx), t.views.color));
    // --- M3: 16 <-> 32 class mismatch: plain drawn-rows sync (drawn_since_sync is
    // consumed), decline; the CT32 read of a CT16S target is the served exception.
    w.vram.CommitDeposits();
    t.coherence.drawn_since_sync = LogicalRect{0, 0, 64, 64};
    const uint32_t pend0 = w.vram.PendingPages();
    DrawIr m16 = TexDraw(0x100000, 0x02, 6, 6, 64, 0x300000);
    m16.vram_epoch = 77;
    TextureSource m = w.resolver.ResolveRead(m16, ctx);
    CHECK(m.declined == TextureSource::Declined::ClassMismatch && m.owner.kind == TextureSource::OwnerKind::Decoded);
    CHECK(w.vram.PendingPages() > pend0);
    CHECK(!t.coherence.HasDrawnSinceSync());
    CHECK(w.vram.Page(0x100000 / VramImage::kPageBytes).shadow_epoch == 77);   // the CURRENT epoch, not the draw's
    RenderTarget& s16 = MakeTarget(w, 0x200000, 64, 64, 0x0A, 64, 5, 3);
    DrawIr x32 = TexDraw(0x200000, 0x00, 6, 6, 64, 0x300000);
    TextureSource xs = w.resolver.ResolveRead(x32, ctx);
    CHECK(ServesSurface(xs, s16.views.color));
    CHECK(xs.transform.is_shuffle && xs.transform.shuffle_type == TextureSource::Transform::kShuffle16to32);
    CHECK(xs.transform.shuffle_uv_scale_u == 0.5f && xs.transform.uv_tex_w == 32 && xs.transform.uv_tex_h == 64);
    CHECK(xs.transform.uv_pre_w == 32 && xs.transform.uv_pre_h == 64);   // a shuffle blocks the READVIEW prescale
    DrawIr x02 = TexDraw(0x200000, 0x02, 6, 6, 64, 0x300000);            // same class: plain serve
    CHECK(ServesSurface(w.resolver.ResolveRead(x02, ctx), s16.views.color));
    DrawIr x01 = TexDraw(0x200000, 0x01, 6, 6, 64, 0x300000);            // CT24 of CT16S: only 0x00 is excepted
    CHECK(w.resolver.ResolveRead(x01, ctx).declined == TextureSource::Declined::ClassMismatch);
    // --- M6: a colour-class serve of the draw's own FRAME.FBP binds nothing.
    ctx.active_fbp = 0x100000;
    DrawIr self = TexDraw(0x100000, 0x00, 6, 6, 64, 0x100000);
    TextureSource sr = w.resolver.ResolveRead(self, ctx);
    CHECK(sr.enabled && sr.owner.kind == TextureSource::OwnerKind::None && sr.declined == TextureSource::Declined::SelfRead);
    CHECK(sr.transform.uv_tex_w == 64 && sr.transform.uv_pre_w == 64 && sr.owner.surface.id == 0);
    ctx.active_fbp = 0x300000;
    // --- M12: RT serves take the TARGET dims; an STQ plain serve prescales by the
    // TEX0 window; FST=1 and the alt16 companion do not.
    RenderTarget& big = MakeTarget(w, 0x280000, 512, 448, 0x00, 512, 5, 3);
    DrawIr stq = TexDraw(0x280000, 0x00, 9, 9, 512, 0x300000);
    TextureSource so = w.resolver.ResolveRead(stq, ctx);
    CHECK(ServesSurface(so, big.views.color));
    CHECK(so.transform.uv_tex_w == 512 && so.transform.uv_tex_h == 448);
    CHECK(so.transform.uv_pre_w == 512 && so.transform.uv_pre_h == 512);
    DrawIr fst = stq;
    fst.tex.fst = true;
    fst.use_fst = true;
    TextureSource fo = w.resolver.ResolveRead(fst, ctx);
    CHECK(fo.transform.uv_pre_w == 512 && fo.transform.uv_pre_h == 448);
    big.identity.has_32bit_content = true;
    DrawIr a16 = TexDraw(0x280000, 0x02, 9, 9, 512, 0x300000);
    TextureSource ao = w.resolver.ResolveRead(a16, ctx);
    CHECK(ao.enabled && ao.owner.surface.id == big.views.alt16.id && big.views.alt16.id != 0);
    CHECK(ao.transform.uv_tex_w == 512 && ao.transform.uv_tex_h == 448);
    CHECK(ao.transform.uv_pre_w == 512 && ao.transform.uv_pre_h == 448 && !ao.transform.is_shuffle);
}

// M4 (indexed decline deposit rule) and item 1 (no snapshot).
void ResolveTiersIndexed() {
    World w;
    TextureResolver::Context ctx;
    ctx.frame_count = 5;
    ctx.active_fbp = 0x70000;
    // A T8H read with an unservable CLUT (CPSM 2) of a recent CT24 target: decline,
    // deposit the drawn rows in CT24 (byte 3 preserved), arm uv_native.
    RenderTarget& t24 = MakeTarget(w, 0x100000, 64, 64, 0x01, 64, 5, 3);
    for (int y = 0; y < 64; y++) w.be.SetPixel(t24.views.color, 0, y, 0x40, 0x50, 0x60, 255);
    uint8_t marker = 0xA5;
    w.vram.WriteStream(0x100003, &marker, 1);
    w.vram.SettleWrites();
    DrawIr d = TexDraw(0x100000, 0x1B, 6, 6, 64, 0x70000);
    d.tex.cbp = 0x3F0000;
    d.tex.cpsm = 0x02;
    d.vram_epoch = w.vram.UploadEpoch() + 1;
    TextureSource a = w.resolver.ResolveRead(d, ctx);
    CHECK(a.declined == TextureSource::Declined::ClutUnservable && a.owner.kind == TextureSource::OwnerKind::Decoded);
    CHECK(a.transform.uv_native);
    CHECK(w.vram.PendingPages() > 0);
    uint8_t px[4];
    w.vram.ReadView(0x100000, 4, px);
    CHECK(px[0] == 0x40 && px[1] == 0x50 && px[2] == 0x60 && px[3] == 0xA5);
    // A 16-bit-class target deposits NOTHING on an indexed decline.
    w.vram.CommitDeposits();
    RenderTarget& t16 = MakeTarget(w, 0x200000, 64, 64, 0x02, 64, 5, 3);
    (void)t16;
    const uint32_t pend0 = w.vram.PendingPages();
    DrawIr d16 = TexDraw(0x200000, 0x1B, 6, 6, 64, 0x70000);
    d16.tex.cbp = 0x3F0000;
    d16.tex.cpsm = 0x02;
    TextureSource b = w.resolver.ResolveRead(d16, ctx);
    CHECK(b.declined == TextureSource::Declined::ClutUnservable && b.transform.uv_native);
    CHECK(w.vram.PendingPages() == pend0);
    // A CT32 target encodes CT32 (alpha written: GPU 255 -> PS2 0x80).
    RenderTarget& t32 = MakeTarget(w, 0x300000, 64, 64, 0x00, 64, 5, 3);
    for (int y = 0; y < 64; y++) w.be.SetPixel(t32.views.color, 0, y, 1, 2, 3, 255);
    DrawIr d32 = TexDraw(0x300000, 0x1B, 6, 6, 64, 0x70000);
    d32.tex.cbp = 0x3F0000;
    d32.tex.cpsm = 0x02;
    d32.vram_epoch = w.vram.UploadEpoch() + 1;
    (void)w.resolver.ResolveRead(d32, ctx);
    w.vram.ReadView(0x300000, 4, px);
    CHECK(px[0] == 1 && px[3] == 0x80);
    // A stale (not recent) target deposits nothing either.
    w.vram.CommitDeposits();
    ctx.frame_count = 30;
    const uint32_t pend1 = w.vram.PendingPages();
    CHECK(w.resolver.ResolveRead(d32, ctx).declined == TextureSource::Declined::ClutUnservable);
    CHECK(w.vram.PendingPages() == pend1);
    // Item 1: no snapshot -> textured UV mode, nothing bound, nothing decoded.
    ctx.has_snapshot = false;
    const int creates = w.be.creates;
    DrawIr plain = TexDraw(0x380000, 0x00, 6, 5, 64, 0x70000);
    TextureSource n = w.resolver.ResolveRead(plain, ctx);
    CHECK(n.enabled && n.owner.kind == TextureSource::OwnerKind::None && n.declined == TextureSource::Declined::NoSnapshot);
    CHECK(n.transform.uv_tex_w == 64 && n.transform.uv_tex_h == 32 && w.be.creates == creates);
    ctx.has_snapshot = true;
    CHECK(w.resolver.ResolveRead(plain, ctx).owner.kind == TextureSource::OwnerKind::Decoded);
}

// M5 / M7 / M8 / M9 / M10: the depth tier and the tier-1 terminal decline.
void ResolveTiersDepth() {
    World w;
    TextureResolver::Context ctx;
    ctx.frame_count = 5;
    ctx.active_fbp = 0x70000;
    // Two targets pair ZBP 0x200000 (Z32, 64x64 = 16 KiB); A wrote Z at seq 3, B at seq 5.
    RenderTarget& a = MakeTarget(w, 0x300000, 64, 64, 0x00, 64, 5, 3);
    RenderTarget& b = MakeTarget(w, 0x400000 - 0x10000, 64, 64, 0x00, 64, 5, 3);
    for (RenderTarget* x : {&a, &b}) {
        x->coherence.last_zbp = 0x200000;
        x->coherence.last_zpsm = 0x30;
        x->coherence.last_z_wrote = true;
        x->coherence.z_drawn_since_sync = LogicalRect{0, 0, 64, 64};
    }
    a.coherence.last_z_seq = 3;
    b.coherence.last_z_seq = 5;
    // M7: containment (one page row in), max seq -> B, fmt 1 with zoff 32 (M9); no Z sync (M10).
    DrawIr z = TexDraw(0x200000 + 0x2000, 0x30, 6, 6, 64, 0x70000);
    TextureSource r = w.resolver.ResolveRead(z, ctx);
    CHECK(ServesSurface(r, b.views.depth) && r.owner.fmt_case == 1 && r.transform.depth_fmt == 1);
    CHECK(r.transform.z_row_offset == 32);
    CHECK(r.transform.uv_tex_w == 64 && r.transform.uv_pre_w == 64);
    CHECK(w.vram.PendingPages() == 0);
    // A target that only Z-TESTED against the ZBP (last_z_wrote false) is not a
    // writer, however high its seq: the search still picks B.
    RenderTarget& tester = MakeTarget(w, 0x500000, 64, 64, 0x00, 64, 5, 3);
    tester.coherence.last_zbp = 0x200000;
    tester.coherence.last_zpsm = 0x30;
    tester.coherence.last_z_wrote = false;
    tester.coherence.last_z_seq = 100;
    CHECK(ServesSurface(w.resolver.ResolveRead(z, ctx), b.views.depth));
    // The ordering is by last_z_seq, not by lookup order: swapping the seqs swaps the pick.
    a.coherence.last_z_seq = 7;
    CHECK(ServesSurface(w.resolver.ResolveRead(z, ctx), a.views.depth));
    a.coherence.last_z_seq = 3;
    // B without valid content -> A.
    b.coherence.valid_rect.Reset();
    CHECK(ServesSurface(w.resolver.ResolveRead(z, ctx), a.views.depth));
    b.coherence.valid_rect = LogicalRect{0, 0, 64, 64};
    // Self veto (P2): B is the draw's own target -> decline, Z synced (M10 decline arm).
    ctx.active_fbp = b.identity.fbp;
    TextureSource sv = w.resolver.ResolveRead(z, ctx);
    CHECK(!sv.enabled || sv.owner.kind == TextureSource::OwnerKind::Decoded);
    CHECK(sv.declined == TextureSource::Declined::ClassMismatch && w.vram.PendingPages() > 0);
    ctx.active_fbp = 0x70000;
    w.vram.CommitDeposits();
    b.coherence.z_drawn_since_sync = LogicalRect{0, 0, 64, 64};
    // TBW mismatch (P3) declines.
    DrawIr zt = TexDraw(0x200000, 0x30, 6, 6, 128, 0x70000);
    CHECK(w.resolver.ResolveRead(zt, ctx).owner.kind == TextureSource::OwnerKind::Decoded);
    // M8: a Z24 (0x31) read never serves; a Z32 read of a Z24-native buffer neither.
    DrawIr z24 = TexDraw(0x200000, 0x31, 6, 6, 64, 0x70000);
    CHECK(w.resolver.ResolveRead(z24, ctx).owner.kind == TextureSource::OwnerKind::Decoded);
    b.coherence.last_zpsm = a.coherence.last_zpsm = 0x31;
    CHECK(w.resolver.ResolveRead(TexDraw(0x200000, 0x30, 6, 6, 64, 0x70000), ctx).owner.kind ==
          TextureSource::OwnerKind::Decoded);
    b.coherence.last_zpsm = a.coherence.last_zpsm = 0x30;
    // fmt 3: Z16 view of the Z32 buffer at base (zoff 0) and at +2 page rows (zoff 64 < 64 fails).
    DrawIr z16v = TexDraw(0x200000, 0x32, 6, 6, 64, 0x70000);
    TextureSource v3 = w.resolver.ResolveRead(z16v, ctx);
    CHECK(v3.owner.fmt_case == 3 && v3.transform.z_row_offset == 0);
    DrawIr z16o = TexDraw(0x200000 + 0x4000, 0x32, 6, 6, 64, 0x70000);   // zoff 64 == height: range fall
    CHECK(w.resolver.ResolveRead(z16o, ctx).owner.kind == TextureSource::OwnerKind::Decoded);
    DrawIr z16m = TexDraw(0x200000 + 0x100, 0x32, 6, 6, 64, 0x70000);    // misaligned: range fall
    CHECK(w.resolver.ResolveRead(z16m, ctx).owner.kind == TextureSource::OwnerKind::Decoded);
    // A wider buffer: ppr 2 -> page row 16 KiB -> zoff 32 at +0x4000.
    RenderTarget& wide = MakeTarget(w, 0x100000, 128, 64, 0x00, 128, 5, 3);
    wide.coherence.last_zbp = 0x240000;
    wide.coherence.last_zpsm = 0x30;
    wide.coherence.last_z_wrote = true;
    wide.coherence.last_z_seq = 9;
    DrawIr zw = TexDraw(0x240000 + 0x4000, 0x32, 7, 6, 128, 0x70000);
    TextureSource vw = w.resolver.ResolveRead(zw, ctx);
    CHECK(vw.owner.fmt_case == 3 && vw.transform.z_row_offset == 32);
    // fmt 2: Z16-native buffer read as Z16 at its base; any offset declines.
    RenderTarget& z16t = MakeTarget(w, 0x180000, 64, 64, 0x02, 64, 5, 3);
    z16t.coherence.last_zbp = 0x260000;
    z16t.coherence.last_zpsm = 0x32;
    z16t.coherence.last_z_wrote = true;
    z16t.coherence.last_z_seq = 11;
    TextureSource n2 = w.resolver.ResolveRead(TexDraw(0x260000, 0x3A, 6, 6, 64, 0x70000), ctx);
    CHECK(n2.owner.fmt_case == 2 && n2.transform.z_row_offset == 0);
    CHECK(w.resolver.ResolveRead(TexDraw(0x260000 + 0x2000, 0x32, 6, 6, 64, 0x70000), ctx).owner.kind ==
          TextureSource::OwnerKind::Decoded);
    CHECK(w.resolver.ResolveRead(TexDraw(0x260000, 0x30, 6, 6, 64, 0x70000), ctx).owner.kind ==
          TextureSource::OwnerKind::Decoded);   // Z32 read of a Z16 buffer: zpsm fall
    // M5: a target exists at TBP without valid content while another target's Z
    // buffer contains TBP: the tier-1 decline terminates -- decode, never depth.
    RenderTarget& at = w.targets.GetOrCreate(0x200000 + 0x2000, 64, 64, 0x00, 1);
    CHECK(!at.coherence.HasValidContent());
    TextureSource m5 = w.resolver.ResolveRead(z, ctx);
    CHECK(m5.owner.kind == TextureSource::OwnerKind::Decoded && m5.transform.depth_fmt == 0);
    // No Z history at all: decode.
    CHECK(w.resolver.ResolveRead(TexDraw(0x3C0000, 0x30, 6, 6, 64, 0x70000), ctx).owner.kind ==
          TextureSource::OwnerKind::Decoded);
    // The Z-source refresh: a COLOUR target keyed at TBP (the Z-alias chain) takes
    // the tier-1 class-mismatch decline (its rows are deposited), then the Z buffer
    // containing TBP whose writer is <= 2 frames old is deposited over the same
    // bytes -- the decode sees Z, not colour.
    w.vram.CommitDeposits();
    at.coherence.valid_rect = LogicalRect{0, 0, 64, 64};
    at.coherence.drawn_since_sync = LogicalRect{0, 0, 64, 64};
    at.coherence.has_content = true;
    at.coherence.last_draw_frame = 5;
    at.identity.render_psm = 0x00;
    at.identity.render_fbw = 64;
    w.be.SetPixel(at.views.color, 0, 0, 0x01, 0x02, 0x03, 0x04);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) w.be.SetDepth(b.views.depth, x, y, 0.5f);   // Z32 0x80000000
    b.coherence.z_drawn_since_sync = LogicalRect{0, 0, 64, 64};
    b.coherence.last_z_write_frame = 5;
    auto z_dwords = [&](uint32_t want) {
        uint8_t win[0x2000];
        w.vram.ReadView(0x200000 + 0x2000, sizeof(win), win);
        int hits = 0;
        for (size_t i = 0; i < sizeof(win); i += 4) {
            uint32_t d;
            std::memcpy(&d, win + i, 4);
            hits += (d == want);
        }
        return hits;
    };
    DrawIr zc = TexDraw(0x200000 + 0x2000, 0x32, 6, 6, 64, 0x70000);
    TextureSource rz = w.resolver.ResolveRead(zc, ctx);
    CHECK(rz.owner.kind == TextureSource::OwnerKind::Decoded && rz.transform.depth_fmt == 0);
    CHECK(rz.declined == TextureSource::Declined::ClassMismatch);
    CHECK(!at.coherence.HasDrawnSinceSync());          // the colour rows were deposited ...
    CHECK(!b.coherence.HasZDrawnSinceSync());          // ... and the Z buffer after them
    CHECK(z_dwords(0x80000000u) == 0x2000 / 4);        // every dword of the window is Z
    CHECK(z_dwords(0x04030201u) == 0);
    // A Z writer older than two frames is not a source: nothing is staged for Z.
    w.vram.CommitDeposits();
    at.coherence.drawn_since_sync = LogicalRect{0, 0, 64, 64};
    b.coherence.z_drawn_since_sync = LogicalRect{0, 0, 64, 64};
    b.coherence.last_z_write_frame = 2;
    TextureSource ro = w.resolver.ResolveRead(zc, ctx);
    CHECK(ro.owner.kind == TextureSource::OwnerKind::Decoded);
    CHECK(!at.coherence.HasDrawnSinceSync() && b.coherence.HasZDrawnSinceSync());
    CHECK(z_dwords(0x80000000u) < 0x2000 / 4);
    // A recent writer that only Z-tested (last_z_wrote false) is no source either.
    b.coherence.last_z_write_frame = 5;
    b.coherence.last_z_wrote = false;
    (void)w.resolver.ResolveRead(zc, ctx);
    CHECK(b.coherence.HasZDrawnSinceSync());
    b.coherence.last_z_wrote = true;
    // The refresh also follows a depth-tier decline (no target at TBP): the most
    // recent RECENT writer is deposited even when an older-frame writer has the
    // higher seq (the tier's own decline sync took that one).
    w.vram.CommitDeposits();
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) w.be.SetDepth(a.views.depth, x, y, 0.25f);   // Z32 0x40000000
    a.coherence.last_z_seq = 9;                       // a wins the tier's pick ...
    a.coherence.last_z_write_frame = 1;               // ... but is 4 frames old
    a.coherence.z_drawn_since_sync = LogicalRect{0, 0, 64, 64};
    b.coherence.last_z_seq = 5;
    b.coherence.last_z_write_frame = 5;
    b.coherence.z_drawn_since_sync = LogicalRect{0, 0, 64, 64};
    ctx.active_fbp = a.identity.fbp;                  // self veto declines the tier (syncs a)
    TextureSource rd = w.resolver.ResolveRead(TexDraw(0x200000, 0x30, 6, 6, 64, a.identity.fbp), ctx);
    CHECK(rd.owner.kind == TextureSource::OwnerKind::Decoded && rd.transform.depth_fmt == 0);
    CHECK(!a.coherence.HasZDrawnSinceSync() && !b.coherence.HasZDrawnSinceSync());
    CHECK(z_dwords(0x80000000u) == 0x2000 / 4);        // b's deposit landed after a's
    ctx.active_fbp = 0x70000;
    a.coherence.last_z_seq = 3;
    // Two RECENT writers contain TBP: the refresh deposits the one with the higher
    // seq, whichever way the targets iterate (both orders checked).
    for (int order = 0; order < 2; order++) {
        w.vram.CommitDeposits();
        at.coherence.drawn_since_sync = LogicalRect{0, 0, 64, 64};
        a.coherence.last_z_write_frame = b.coherence.last_z_write_frame = 5;
        a.coherence.z_drawn_since_sync = b.coherence.z_drawn_since_sync = LogicalRect{0, 0, 64, 64};
        a.coherence.last_z_seq = order ? 9 : 3;
        b.coherence.last_z_seq = 5;
        (void)w.resolver.ResolveRead(zc, ctx);
        CHECK(z_dwords(order ? 0x40000000u : 0x80000000u) == 0x2000 / 4);
        CHECK(order ? b.coherence.HasZDrawnSinceSync() : a.coherence.HasZDrawnSinceSync());
    }
    a.coherence.last_z_seq = 3;
}

// M11: the lane-live rescue is separate from the pair chain.
void ResolveTiersLaneLive() {
    World w;
    const uint32_t fbp = 0x280000, partner = fbp + 448u * 512u * 2u;
    RenderTarget& t = MakeTarget(w, fbp, 512, 448, 0x02, 256, 9, 3);        // FBW retagged to 256 (fall_tbw)
    RenderTarget& p = MakeTarget(w, partner, 512, 448, 0x02, 256, 9, 3);
    TextureResolver::Context ctx;
    ctx.frame_count = 9;
    ctx.active_fbp = 0x70000;
    DrawIr d = TexDraw(fbp, 0x1B, 9, 9, 512, 0x70000);
    d.tex.cbp = 0x3F0000;
    // Pair chain falls (tbw 512 != fbw 256); lane records absent: decode.
    CHECK(w.resolver.ResolveRead(d, ctx).owner.fmt_case != 5);
    // Both halves lane-live on the MAIN view this frame: rescued as fmt 5 binding
    // the main images; the CPU-dirty veto does not apply to the rescue.
    for (RenderTarget* x : {&t, &p}) {
        x->coherence.main_author_seq = 4;
        x->coherence.lane_main.seq = 4;
        x->coherence.lane_main.frame = 9;
    }
    w.refresher.MarkPagesCpuDirty(fbp, 448u * 512u * 2u);
    TextureSource r = w.resolver.ResolveRead(d, ctx);
    CHECK(r.enabled && r.owner.fmt_case == 5 && r.owner.surface.id == t.views.color.id &&
          r.owner.partner.id == p.views.color.id && r.owner.clut.id != 0);
    // The ALT lane authored the base half: the rescue binds its companion.
    t.EnsureAlt16(w.be);
    t.coherence.alt_author_seq = 2;
    t.coherence.lane_alt.seq = 2;
    t.coherence.lane_alt.frame = 9;
    TextureSource ra = w.resolver.ResolveRead(d, ctx);
    CHECK(ra.owner.fmt_case == 5 && ra.owner.surface.id == t.views.alt16.id && ra.owner.partner.id == p.views.color.id);
    // A scaled half is never rescued; a half narrower than TBW neither.
    t.identity.scale = 2;
    CHECK(w.resolver.ResolveRead(d, ctx).owner.fmt_case != 5);
    t.identity.scale = 1;
    p.identity.width = 256;
    CHECK(w.resolver.ResolveRead(d, ctx).owner.fmt_case != 5);
    p.identity.width = 512;
    // An unservable CLUT blocks the rescue; a self-read never rescues.
    d.tex.cpsm = 0x02;
    CHECK(w.resolver.ResolveRead(d, ctx).owner.fmt_case != 5);
    d.tex.cpsm = 0x00;
    ctx.active_fbp = fbp;
    CHECK(w.resolver.ResolveRead(d, ctx).owner.fmt_case != 5);
    ctx.active_fbp = 0x70000;
    // The pair chain proper: FBW == TBW, keep16 on both halves admits, then the
    // SH5 CPU-dirty veto declines it (the rescue then still applies while lane-live).
    for (RenderTarget* x : {&t, &p}) {
        x->identity.render_fbw = 512;
        x->coherence.sync_keep_mask16 = RenderTarget::kIndexLaneKeep16;
        x->coherence.lane_main.frame = 8;   // lane not live any more
        x->coherence.lane_alt.frame = 8;
    }
    w.refresher.ClearPagesCpuDirty(fbp, 448u * 512u * 2u);
    TextureSource k = w.resolver.ResolveRead(d, ctx);
    CHECK(k.owner.fmt_case == 5 && k.owner.surface.id == t.views.color.id);
    w.refresher.MarkPagesCpuDirty(partner, 448u * 512u * 2u);
    CHECK(w.resolver.ResolveRead(d, ctx).owner.fmt_case != 5);   // dirty partner: chain declines, no live lane
    for (RenderTarget* x : {&t, &p}) x->coherence.lane_main.frame = 9;
    CHECK(w.resolver.ResolveRead(d, ctx).owner.fmt_case == 5);   // ... but the live lane rescues it
}

VertexRec Uv(int u, int v) {
    VertexRec r;
    r.uv_u = static_cast<uint16_t>(u * 16);
    r.uv_v = static_cast<uint16_t>(v * 16);
    return r;
}

// Item 7: the UV region derivation, vectors worked by hand from the OLD rules.
void UvRegionDerive() {
    using R = UvRegion;
    TexReadView v = View(0x100000, 0x00, 8, 8, 256);
    v.wms = v.wmt = 1;   // CLAMP
    auto bb = [](float u0, float u1, float v0, float v1) {
        UvBbox::Result b; b.state = UvBbox::kOk; b.u0 = u0; b.u1 = u1; b.v0 = v0; b.v1 = v1; return b;
    };
    R::Result r = R::Derive(bb(10, 50, 100, 120), v, 256, 256);
    CHECK(r.cls == R::kRect && r.x0 == 0 && r.y0 == 64 && r.w == 64 && r.h == 64);
    // Bilinear taps widen by one texel each side: [64.2, 64.8] nearest -> [64,128); linear -> [0,128).
    r = R::Derive(bb(64.2f, 64.8f, 0, 10), v, 256, 256);
    CHECK(r.cls == R::kRect && r.x0 == 64 && r.w == 64);
    v.mmag = true;
    r = R::Derive(bb(64.2f, 64.8f, 0, 10), v, 256, 256);
    CHECK(r.cls == R::kRect && r.x0 == 0 && r.w == 128);
    v.mmag = false;
    v.mmin = 1;   // odd MMIN = linear minification
    r = R::Derive(bb(64.2f, 64.8f, 0, 10), v, 256, 256);
    CHECK(r.cls == R::kRect && r.x0 == 0 && r.w == 128);
    v.mmin = 0;
    // REPEAT: a single tile rebases, a straddle takes the axis.
    v.wms = 0;
    r = R::Derive(bb(300, 310, 0, 10), v, 256, 256);
    CHECK(r.cls == R::kRect && r.x0 == 0 && r.w == 64);
    r = R::Derive(bb(250, 260, 0, 10), v, 256, 256);
    CHECK(r.cls == R::kRect && r.x0 == 0 && r.w == 256 && r.y0 == 0 && r.h == 64);
    // REGION_CLAMP intersects [minu, maxu + 1]; a degenerate result is the full axis.
    v.wms = 2; v.minu = 10; v.maxu = 20;
    r = R::Derive(bb(0, 100, 0, 10), v, 256, 256);
    CHECK(r.cls == R::kRect && r.x0 == 0 && r.w == 64);
    v.minu = 100; v.maxu = 50;
    r = R::Derive(bb(0, 10, 0, 10), v, 256, 256);
    CHECK(r.cls == R::kRect && r.x0 == 0 && r.w == 256 && r.h == 64);
    // REGION_REPEAT: full axis; with a full v axis too the rect IS the texture.
    v.wms = 3; v.wmt = 3;
    r = R::Derive(bb(0, 10, 0, 10), v, 256, 256);
    CHECK(r.cls == R::kFullIsFull);
    v.wms = v.wmt = 1;
    // Area rule: 192x192 of 256x256 is more than half -> full.
    r = R::Derive(bb(0, 140, 0, 140), v, 256, 256);
    CHECK(r.cls == R::kFullAreaRule);
    // Quantisation grew a proper sub-rect to the whole texture.
    r = R::Derive(bb(0, 250, 0, 250), v, 256, 256);
    CHECK(r.cls == R::kFullQuant);
    // hi <= lo takes one texel; the quantised rect covers it.
    r = R::Derive(bb(5, 5, 70, 70), v, 256, 256);
    CHECK(r.cls == R::kRect && r.x0 == 0 && r.w == 64 && r.y0 == 64 && r.h == 64);
    // Half exactly qualifies (2 * area <= full).
    r = R::Derive(bb(0, 120, 0, 250), v, 256, 256);   // [0,128) x [0,256) = half
    CHECK(r.cls == R::kRect && r.w == 128 && r.h == 256);
    UvBbox::Result bail; bail.state = UvBbox::kBail;
    CHECK(R::Derive(bail, v, 256, 256).cls == R::kBail);
    CHECK(R::Eligible(0x00, 3, false) && R::Eligible(0x1B, 4, false) && R::Eligible(0x13, 3, false));
    CHECK(!R::Eligible(0x30, 3, false) && !R::Eligible(0x00, 2, false) && !R::Eligible(0x00, 3, true));
    CHECK(!R::Eligible(0x0A, 3, false) && !R::Eligible(0x24, 3, false));
    // Through the resolver: an FST read of 256x256 CT32 with u in [10,50], v in
    // [100,120] decodes the 64x64 rect at (0, 64) and rebases; the same tuple
    // fragmenting into 9 distinct rects is forced back to full decodes.
    World w;
    TextureResolver::Context ctx;
    DrawIr d = TexDraw(0x100000, 0x00, 8, 8, 256, 0x70000);
    d.tex.fst = true; d.use_fst = true;
    d.tex.wms = d.tex.wmt = 1;
    d.verts = {Uv(10, 100), Uv(50, 120), Uv(30, 110)};
    TextureSource s = w.resolver.ResolveRead(d, ctx);
    CHECK(s.enabled && s.owner.kind == TextureSource::OwnerKind::Decoded && s.transform.region.applied);
    CHECK(s.transform.region.u0 == 0 && s.transform.region.v0 == 64 && s.transform.region.w == 64 && s.transform.region.h == 64);
    CHECK(w.be.At(s.owner.surface).desc.width == 64 && w.be.At(s.owner.surface).desc.height == 64);
    CHECK(s.transform.uv_tex_w == 256 && s.transform.uv_pre_w == 256);   // R114: the TEX0 dims stay the divisor
    for (int i = 1; i <= 8; i++) {   // 8 more distinct 64x64 tiles (9 total) trip the guard on the 9th
        const int ax = 64 * (i % 4), ay = 64 * (1 + i / 4);   // never the original (0, 64) tile
        d.verts = {Uv(ax + 10, ay + 10), Uv(ax + 50, ay + 50), Uv(ax + 30, ay + 30)};
        s = w.resolver.ResolveRead(d, ctx);
        if (i < 8) CHECK(s.transform.region.applied && s.transform.region.u0 == ax && s.transform.region.v0 == ay);
    }
    CHECK(!s.transform.region.applied);
    d.verts = {Uv(10, 100), Uv(50, 120), Uv(30, 110)};
    CHECK(!w.resolver.ResolveRead(d, ctx).transform.region.applied);   // sticky
    // A two-vertex draw or a Z read is ineligible: full decode.
    DrawIr two = TexDraw(0x180000, 0x00, 8, 8, 256, 0x70000);
    two.tex.fst = true;
    two.verts = {Uv(10, 100), Uv(50, 120)};
    CHECK(!w.resolver.ResolveRead(two, ctx).transform.region.applied);
}

// Item 8: every miss records the texture in L2; L1 eviction keeps a surface L2
// holds; a re-hit rebinds without a decode; a tuple whose bytes moved on demotes
// both generations to probation.
void DecodeCacheOnMiss() {
    World w;
    TextureResolver::Context ctx;
    std::vector<uint8_t> bytes(64 * 64 * 4);
    for (size_t i = 0; i < bytes.size(); i++) bytes[i] = static_cast<uint8_t>(i * 3);
    w.vram.WriteStream(0x100000, bytes.data(), bytes.size());
    w.vram.SettleWrites();
    DrawIr d = TexDraw(0x100000, 0x00, 6, 6, 64, 0x70000);
    TextureSource a = w.resolver.ResolveRead(d, ctx);
    DecodeCache& c = w.resolver.Cache();
    CHECK(a.owner.kind == TextureSource::OwnerKind::Decoded && c.L1Size() == 1 && c.L2Size() == 1);
    CHECK(c.L2ProbCount() == 0);   // first generation: not a suspect
    const int creates = w.be.creates;
    CHECK(w.resolver.ResolveRead(d, ctx).owner.surface.id == a.owner.surface.id && w.be.creates == creates);
    // L1 evicted by an upload over the footprint: the surface survives in L2 ...
    c.EvictInRange(0x100000, 64 * 64 * 4);
    CHECK(c.L1Size() == 0 && c.L2Size() == 1 && w.be.destroys == 0 && w.be.Has(a.owner.surface));
    // ... and the same bytes at a ROTATED address rebind it without a decode.
    w.vram.WriteStream(0x300000, bytes.data(), bytes.size());
    w.vram.SettleWrites();
    DrawIr r = TexDraw(0x300000, 0x00, 6, 6, 64, 0x70000);
    TextureSource b = w.resolver.ResolveRead(r, ctx);
    CHECK(b.owner.surface.id == a.owner.surface.id && w.be.creates == creates && c.L1Size() == 1);
    // The tuple's content moves on while the old generation is still in L2: the
    // old entry is demoted and the new one enters in probation.
    bytes[17] ^= 0xFF;
    w.vram.WriteStream(0x300000, bytes.data(), bytes.size());
    w.vram.SettleWrites();
    TextureSource m = w.resolver.ResolveRead(r, ctx);
    CHECK(m.owner.surface.id != a.owner.surface.id && c.L2Size() == 2 && c.L2ProbCount() == 2);
    CHECK(w.be.creates == creates + 1);
    // An L1 hit leaves L2 untouched; an L2 hit (after an L1 eviction) earns tenure.
    (void)w.resolver.ResolveRead(r, ctx);
    CHECK(c.L2ProbCount() == 2 && w.be.creates == creates + 1);
    c.EvictInRange(0x300000, 64 * 64 * 4);
    CHECK(c.L2Size() == 2 && w.be.destroys == 0);
    CHECK(w.resolver.ResolveRead(r, ctx).owner.surface.id == m.owner.surface.id && w.be.creates == creates + 1);
    CHECK(c.L2ProbCount() == 1);
    // Dropping both caches destroys each surface exactly once.
    const int live = w.be.creates - w.be.destroys;
    c.DropAll();
    CHECK(w.be.destroys == live && !w.be.Has(a.owner.surface) && !w.be.Has(m.owner.surface));
}

// The decode fallback prepares nothing but the key's inputs before the lookup:
// N identical textured draws cost ONE decode (a composed-view read of the
// texture's footprint -- the pages it touches, plus the CLUT page of an indexed
// read -- never of all of VRAM: track 1d), ONE window read (the key's bytes) and
// N-1 L1 hits that read NOTHING --
// the pages the key hashed kept their view generations (VramImage::ViewSignature),
// so the register tuple's last key is reused without touching the bytes. A write
// into the footprint moves a generation: the next read hashes the window again
// and finds the content changed. An indexed read adds exactly its CLUT page the
// first time and nothing after; a write to the CLUT page alone re-reads both. A
// rotated copy of the bytes (L2 rebind after an L1 eviction) decodes nothing.
void DecodeFallbackPrepBytes() {
    World w;
    TextureResolver::Context ctx;
    std::vector<uint8_t> bytes(64 * 64 * 4);
    for (size_t i = 0; i < bytes.size(); i++) bytes[i] = static_cast<uint8_t>(i * 7 + 1);
    w.vram.WriteStream(0x100000, bytes.data(), bytes.size());
    w.vram.SettleWrites();
    const TextureResolver::Census& s = w.resolver.Stats();
    CHECK(s.lookups == 0 && s.decodes == 0 && s.prep_bytes == 0 && s.view_bytes == 0 && s.sig_hits == 0);
    DrawIr d = TexDraw(0x100000, 0x00, 6, 6, 64, 0x70000);
    uint32_t fp_start = 0, fp_size = 0;
    ContentKey::Footprint(d.tex, &fp_start, &fp_size);
    CHECK(fp_size > 0 && fp_size < VramImage::kBytes / 64);
    TextureSource first = w.resolver.ResolveRead(d, ctx);
    CHECK(first.owner.kind == TextureSource::OwnerKind::Decoded);
    CHECK(s.lookups == 1 && s.decodes == 1 && s.l1_hits == 0 && s.l2_hits == 0 && s.sig_hits == 0);
    CHECK(s.prep_bytes == fp_size);                       // the hashed window only
    CHECK(s.view_bytes == fp_size);                       // the one decode's view: its footprint, not 4 MiB
    const int N = 64;
    for (int i = 1; i < N; ++i) {
        TextureSource again = w.resolver.ResolveRead(d, ctx);
        CHECK(again.owner.surface.id == first.owner.surface.id);
    }
    CHECK(s.lookups == uint64_t(N) && s.decodes == 1 && s.l1_hits == uint64_t(N - 1) && s.l2_hits == 0);
    CHECK(s.sig_hits == uint64_t(N - 1));                 // every hit reused the key ...
    CHECK(s.prep_bytes == fp_size);                       // ... and read no bytes at all
    CHECK(s.view_bytes == fp_size);                       // none read the view
    // A write into the footprint (one byte of the window changes): the signature
    // moves, the window is hashed again, the content differs -> a second decode.
    bytes[100] ^= 0xFF;
    w.vram.WriteStream(0x100000 + 100, &bytes[100], 1);
    w.vram.SettleWrites();
    TextureSource changed = w.resolver.ResolveRead(d, ctx);
    CHECK(changed.owner.surface.id != first.owner.surface.id);
    CHECK(s.lookups == uint64_t(N + 1) && s.decodes == 2 && s.sig_hits == uint64_t(N - 1));
    CHECK(s.prep_bytes == 2 * uint64_t(fp_size) && s.view_bytes == 2 * uint64_t(fp_size));
    // A settle with no write under the footprint moves no generation of its pages.
    w.vram.SettleWrites();
    (void)w.resolver.ResolveRead(d, ctx);
    CHECK(s.sig_hits == uint64_t(N) && s.prep_bytes == 2 * uint64_t(fp_size) && s.decodes == 2);
    // An L1 eviction over the footprint: the next read rebinds from L2 (no decode,
    // no view read) -- the key is reused (the pages did not move), so no window read either.
    w.resolver.Cache().EvictInRange(0x100000, 64 * 64 * 4);
    (void)w.resolver.ResolveRead(d, ctx);
    CHECK(s.lookups == uint64_t(N + 3) && s.decodes == 2 && s.l2_hits == 1 && s.view_bytes == 2 * uint64_t(fp_size));
    CHECK(s.prep_bytes == 2 * uint64_t(fp_size) && s.sig_hits == uint64_t(N + 1));
    // An indexed read (T8, CLUT at 0x300000) adds exactly its 8 KiB CLUT page to the
    // pre-lookup bytes; its first call decodes once, the second reuses the key.
    std::vector<uint8_t> idx(64 * 64);
    for (size_t i = 0; i < idx.size(); i++) idx[i] = static_cast<uint8_t>(i);
    w.vram.WriteStream(0x200000, idx.data(), idx.size());
    w.vram.WriteStream(0x300000, bytes.data(), 256 * 4);
    w.vram.SettleWrites();
    DrawIr t8 = TexDraw(0x200000, 0x13, 6, 6, 64, 0x70000);
    t8.tex.cbp = 0x300000;
    t8.tex.cpsm = 0x00;
    uint32_t i_start = 0, i_size = 0;
    ContentKey::Footprint(t8.tex, &i_start, &i_size);
    const uint64_t before = s.prep_bytes;
    TextureSource ti = w.resolver.ResolveRead(t8, ctx);
    CHECK(ti.owner.kind == TextureSource::OwnerKind::Decoded && s.decodes == 3);
    CHECK(s.prep_bytes == before + i_size + VramImage::kPageBytes);
    CHECK(s.view_bytes == 2 * uint64_t(fp_size) + i_size + VramImage::kPageBytes);   // the T8 footprint + its CLUT page
    (void)w.resolver.ResolveRead(t8, ctx);
    CHECK(s.decodes == 3 && s.prep_bytes == before + i_size + VramImage::kPageBytes && s.sig_hits == uint64_t(N + 2));
    // A write to the CLUT page alone (the indices untouched) moves the signature:
    // both the window and the CLUT page are read again and the palette change decodes.
    bytes[4] ^= 0x80;
    w.vram.WriteStream(0x300000 + 4, &bytes[4], 1);
    w.vram.SettleWrites();
    TextureSource ti2 = w.resolver.ResolveRead(t8, ctx);
    CHECK(ti2.owner.surface.id != ti.owner.surface.id && s.decodes == 4);
    CHECK(s.prep_bytes == before + 2 * (i_size + VramImage::kPageBytes) && s.sig_hits == uint64_t(N + 2));
}

// A 16-bit-class target whose rows are deferred in the ledger (solid red: 5551 0x801F).
RenderTarget& PendingTarget16(World& w, uint32_t fbp, uint64_t epoch) {
    RenderTarget& p = w.targets.GetOrCreate(fbp, 64, 64, 0x02, epoch);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) w.be.SetPixel(p.views.color, x, y, 0xFF, 0x00, 0x00, 0xFF);
    DrawIr d;
    d.raster.frame.fbp = fbp;
    d.raster.frame.fbw = 64;
    d.raster.frame.psm = 0x02;
    d.raster.scissor = DrawIr::Scissor{0, 0, 63, 63};
    render::DrawResult r;
    r.drew = true;
    r.write_rect = render::Rect{0, 0, 64, 64};
    r.bbox_valid = true;
    r.bbox_tri[0] = 0; r.bbox_tri[1] = 0; r.bbox_tri[2] = 64; r.bbox_tri[3] = 64;
    p.AfterDraw(d, r, 1, epoch);
    w.targets.AfterDrawTail(p, d, r, 64, epoch);
    return p;
}

uint16_t Half(const VramImage& v, uint32_t addr) {
    uint8_t b[2];
    v.ReadView(addr, 2, b);
    return uint16_t(b[0] | (b[1] << 8));
}

// The kFTT / kDecode flush spans (review 6D-double-prime items 1, 3, 4): a
// target-serve's deferred-writeback flush covers the SERVING TARGET's footprint
// (width x height x render bpp from TBP), not the TEX0 window; the decode fallback
// flushes the read footprint; every direct sync leaves the ledger without the
// target's entry; the L1 eviction overlap wraps at 4 MiB.
void ResolveTiersFlushes() {
    World w;
    TextureResolver::Context ctx;
    ctx.frame_count = 5;
    ctx.active_fbp = 0x300000;
    // Serving target: 256x256 CT32 at 0x100000 -> footprint [0x100000, 0x140000).
    RenderTarget& big = MakeTarget(w, 0x100000, 256, 256, 0x00, 256, 5, 3);
    big.coherence.drawn_since_sync = LogicalRect{0, 0, 256, 32};   // its own sync covers [0x100000, 0x108000)
    // A pending 16-bit target INSIDE that footprint but OUTSIDE the 64x64 CT16
    // TEX0 window ([0x100000, 0x102000)) of the read below -- and outside the
    // serving target's own drawn rows, which its sync deposits AFTER the flush.
    PendingTarget16(w, 0x130000, 5);
    CHECK(w.targets.Ledger().Has(0x130000) && Half(w.vram, 0x130000) == 0);
    // A CT16 read of the CT32 target: class mismatch -> Ftt flush over the serving
    // footprint lands the deferred rows, then the target's own drawn-rows sync.
    DrawIr m16 = TexDraw(0x100000, 0x02, 6, 6, 64, 0x300000);
    m16.vram_epoch = 40;
    TextureSource m = w.resolver.ResolveRead(m16, ctx);
    CHECK(m.declined == TextureSource::Declined::ClassMismatch);
    CHECK(w.targets.Ledger().Flushes(PendingRtSyncLedger::Reason::Ftt) == 1);
    CHECK(!w.targets.Ledger().Has(0x130000));
    CHECK(Half(w.vram, 0x130000) == 0x801F);                        // written back ...
    CHECK(w.vram.Page(0x130000 / VramImage::kPageBytes).shadow_epoch == 5);   // ... at ITS epoch
    CHECK(!big.coherence.HasDrawnSinceSync());
    CHECK(w.targets.Ledger().Empty());
    // The direct sync's tail: a 16-bit serving target with its own entry loses it
    // through the class-mismatch sync (a CT32 read of a CT16 target).
    RenderTarget& s16 = PendingTarget16(w, 0x200000, 7);
    s16.coherence.last_draw_frame = 5;
    CHECK(w.targets.Ledger().Has(0x200000));
    DrawIr x32 = TexDraw(0x200000, 0x00, 6, 6, 64, 0x300000);
    CHECK(w.resolver.ResolveRead(x32, ctx).declined == TextureSource::Declined::ClassMismatch);
    CHECK(w.targets.Ledger().Empty() && !s16.coherence.HasDrawnSinceSync());
    // The indexed-decline deposit of a recently drawn 32-bit target flushes the
    // serving footprint first (a deferred entry under it lands before the deposit).
    RenderTarget& idx = MakeTarget(w, 0x180000, 128, 128, 0x00, 128, 5, 3);
    idx.coherence.drawn_since_sync = LogicalRect{0, 0, 128, 16};   // [0x180000, 0x182000)
    PendingTarget16(w, 0x188000, 9);
    DrawIr t8 = TexDraw(0x180000, 0x13, 6, 6, 64, 0x300000);   // T8 never serves: decline + deposit
    t8.tex.cbp = 0x3F0000;
    t8.vram_epoch = 41;
    CHECK(w.resolver.ResolveRead(t8, ctx).declined == TextureSource::Declined::ClutUnservable);
    CHECK(w.targets.Ledger().Flushes(PendingRtSyncLedger::Reason::Ftt) == 3);
    CHECK(!w.targets.Ledger().Has(0x188000) && Half(w.vram, 0x188000) == 0x801F);
    CHECK(!idx.coherence.HasDrawnSinceSync());
    // The keep-fall RESCUE site (the third kFTT flush): a same-class CT32 read of a
    // 256x256 CT32 target NOT drawn recently whose pages are CPU-dirty with an EE
    // upload newer than its last draw over the read window (keep fails, the read
    // window is rescued at the target's draw epoch and the read declines to the
    // decode). The pending 16-bit target at 0x130000 -- inside the serving footprint
    // [0x100000, 0x140000), outside the 64x64 CT32 TEX0 window [0x100000, 0x104000)
    // -- lands at ITS epoch before that rescue; a rescue is not a writeback.
    {
        World k;
        TextureResolver::Context kc;
        kc.frame_count = 20;
        kc.active_fbp = 0x300000;
        RenderTarget& srv = MakeTarget(k, 0x100000, 256, 256, 0x00, 256, 5, 3);
        k.refresher.MarkPagesCpuDirty(0x100000, 256 * 256 * 4);
        PendingTarget16(k, 0x130000, 50);
        NewerUpload(k, 0x100000 + 100, 4);                           // page 0: newer than the draw (epoch 3)
        CHECK(k.resolver.WindowEpoch(0x100000, 256 * 256 * 4) > 3);
        CHECK(k.targets.Ledger().Has(0x130000) && Half(k.vram, 0x130000) == 0);
        DrawIr rd = TexDraw(0x100000, 0x00, 6, 6, 64, 0x300000);
        rd.vram_epoch = 60;
        TextureSource rs = k.resolver.ResolveRead(rd, kc);
        CHECK(rs.declined == TextureSource::Declined::StaleTarget);
        CHECK(rs.enabled && rs.owner.kind == TextureSource::OwnerKind::Decoded);
        CHECK(k.targets.Ledger().Flushes(PendingRtSyncLedger::Reason::Ftt) == 1);
        CHECK(!k.targets.Ledger().Has(0x130000) && k.targets.Ledger().Empty());
        CHECK(Half(k.vram, 0x130000) == 0x801F);
        CHECK(k.vram.Page(0x130000 / VramImage::kPageBytes).shadow_epoch == 50);   // the entry's epoch, not the rescue's 3
        CHECK(srv.coherence.HasDrawnSinceSync());                                   // the rescue left the drawn rect
        CHECK(k.vram.PendingPages() > 0);                                           // ... and staged the read window
    }
    // The decode fallback: a read with NO target at TBP whose footprint covers a
    // pending target flushes it (Decode). Page 1 of the 64-wide CT32 read at
    // 0x1FE000 IS the pending target's first page; the deferred rows were recorded
    // at epoch 0 while the EE zeroed the word and settled (a newer upload epoch), so
    // the flush RUNS (counted, entry gone) and the newer-upload veto keeps the CPU bytes.
    PendingTarget16(w, 0x200000, 0);
    w.vram.CommitDeposits();
    const uint8_t zero[2] = {0, 0};
    w.vram.WriteStream(0x200000, zero, 2);
    w.vram.SettleWrites();
    CHECK(Half(w.vram, 0x200000) == 0);
    DrawIr dec = TexDraw(0x1FE000, 0x00, 6, 6, 64, 0x300000);
    dec.vram_epoch = 50;
    TextureSource d = w.resolver.ResolveRead(dec, ctx);
    CHECK(d.enabled && d.owner.kind == TextureSource::OwnerKind::Decoded);
    CHECK(w.targets.Ledger().Flushes(PendingRtSyncLedger::Reason::Decode) == 1);
    CHECK(w.targets.Ledger().Empty());
    CHECK(Half(w.vram, 0x200000) == 0);
    const test::FakeBackend::Surface& sf = w.be.At(d.owner.surface);
    CHECK(sf.rgba[(32 * 64 + 0) * 4 + 0] == 0 && sf.rgba[(32 * 64 + 0) * 4 + 1] == 0);
    // ... and with the deferred rows NEWER than the EE write the flush lands them
    // and the decode reads red at pixel (0, 32) = VRAM 0x200000 (CT32 word 0x801F801F).
    PendingTarget16(w, 0x200000, w.vram.UploadEpoch() + 5);
    dec.vram_epoch = 60;
    TextureSource d2 = w.resolver.ResolveRead(dec, ctx);
    CHECK(w.targets.Ledger().Flushes(PendingRtSyncLedger::Reason::Decode) == 2 && w.targets.Ledger().Empty());
    CHECK(Half(w.vram, 0x200000) == 0x801F);
    const test::FakeBackend::Surface& sf2 = w.be.At(d2.owner.surface);
    CHECK(sf2.rgba[(32 * 64 + 0) * 4 + 0] == 0x1F && sf2.rgba[(32 * 64 + 0) * 4 + 1] == 0x80);
    // Wrap-aware L1 eviction (item 3): a footprint past 4 MiB is hit by a low range
    // and a range past 4 MiB hits a low footprint; a disjoint entry survives.
    DecodeCache& c = w.resolver.Cache();
    const size_t l1 = c.L1Size();
    render::SurfaceDesc sd;
    sd.width = 8; sd.height = 8; sd.format = render::SurfaceFormat::Rgba8;
    c.L1Insert(0xA1, w.be.CreateSurface(sd), 8, 8, 0x3FE000, 0x4000);
    c.L1Insert(0xA2, w.be.CreateSurface(sd), 8, 8, 0x1000, 0x1000);
    c.L1Insert(0xA3, w.be.CreateSurface(sd), 8, 8, 0x300000, 0x1000);
    c.EvictInRange(0x0, 0x800);                  // hits the wrapped tail of 0xA1
    CHECK(c.L1Size() == l1 + 2 && c.L1Find(0xA1, 8, 8) == nullptr && c.L1Find(0xA2, 8, 8) != nullptr);
    c.EvictInRange(0x3FF000, 0x3000);            // wraps to [0, 0x2000): hits 0xA2, not 0xA3
    CHECK(c.L1Find(0xA2, 8, 8) == nullptr && c.L1Find(0xA3, 8, 8) != nullptr);
    c.EvictInRange(0x3FF000, 0);                 // size 0: nothing
    CHECK(c.L1Find(0xA3, 8, 8) != nullptr);
}

// One stage-texture world for the FMV cases: column 49 = 0xAA, columns 55 / 63 =
// 0x55 over rows 64..127 of the CT32 1024-wide stage at 0x150000.
void StageWorld(World& w) {
    w.layout.fmv_stage_tbp = 0x150000;
    w.layout.main_fb_fbp = 0x70000;
    for (int y = 64; y < 128; ++y) {
        const uint32_t a = 0x800000AAu, b = 0x80000055u;
        w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, 49, y, 0x150000 / 256, 16), reinterpret_cast<const uint8_t*>(&a), 4);
        w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, 55, y, 0x150000 / 256, 16), reinterpret_cast<const uint8_t*>(&b), 4);
        w.vram.WriteStream(PsmLayout::PixelByteAddress(Psm::CT32, 63, y, 0x150000 / 256, 16), reinterpret_cast<const uint8_t*>(&b), 4);
    }
    w.vram.SettleWrites();
}

uint8_t TexelR(const test::FakeBackend::Surface& s, int x, int y) { return s.rgba[(size_t(y) * s.desc.width + x) * 4]; }

// The 6D-triple-prime remainder: the R198 D1 FMV edge clamp (texel edit of the
// decoded region), authored mip levels uploaded, the post-draw eviction, OnCpuWrite.
void ResolveTiersRemainder() {
    TextureResolver::Context ctx;
    // --- FMV: the stage read as CT32 1024x1024 under REPEAT (wms 0), CLAMP (wmt 1),
    // UVs u in [10, 50], v in [100, 120] -> the 64x64 region at (0, 64); columns
    // >= cw = 50 repeat column 49; rows are untouched (wmt = 1).
    DrawIr fmv = TexDraw(0x150000, 0x00, 10, 10, 1024, 0x70000);
    fmv.tex.fst = true; fmv.use_fst = true;
    fmv.tex.wms = 0; fmv.tex.wmt = 1;
    fmv.verts = {Uv(10, 100), Uv(50, 120), Uv(30, 110)};
    {
        World w;
        StageWorld(w);
        TextureSource f = w.resolver.ResolveRead(fmv, ctx);
        CHECK(f.enabled && f.transform.region.applied && f.transform.region.u0 == 0 && f.transform.region.v0 == 64);
        CHECK(f.transform.region.w == 64 && f.transform.region.h == 64);
        const test::FakeBackend::Surface& fs = w.be.At(f.owner.surface);
        CHECK(TexelR(fs, 49, 0) == 0xAA && TexelR(fs, 55, 0) == 0xAA && TexelR(fs, 63, 0) == 0xAA);   // replicated edge
        CHECK(TexelR(fs, 49, 63) == 0xAA && TexelR(fs, 55, 63) == 0xAA);
        CHECK(fs.rgba[(0 * 64 + 49) * 4 + 3] == 0xFF);   // PS2 0x80 -> GPU 0xFF
        // The edit is applied AFTER the content key: the same bytes read again HIT
        // the clamped decode (OLD :6581 after r118_tex_key) -- even from another frame.
        DrawIr again = fmv;
        again.raster.frame.fbp = 0x300000;
        const int cr = w.be.creates;
        TextureSource a = w.resolver.ResolveRead(again, ctx);
        CHECK(a.owner.surface.id == f.owner.surface.id && w.be.creates == cr);
    }
    {   // Not the signature (frame is not the display buffer): the region decodes verbatim.
        World w;
        StageWorld(w);
        DrawIr nf = fmv;
        nf.raster.frame.fbp = 0x300000;
        TextureSource n = w.resolver.ResolveRead(nf, ctx);
        CHECK(n.transform.region.applied && TexelR(w.be.At(n.owner.surface), 55, 0) == 0x55);
    }
    {   // CLAMP on both axes: the signature holds but nothing is edited.
        World w;
        StageWorld(w);
        DrawIr cl = fmv;
        cl.tex.wms = 1;
        TextureSource c = w.resolver.ResolveRead(cl, ctx);
        CHECK(c.transform.region.applied && TexelR(w.be.At(c.owner.surface), 55, 0) == 0x55);
    }
    {   // An empty layout (no title): never the signature.
        World w;
        StageWorld(w);
        w.layout.fmv_stage_tbp = 0;
        TextureSource e = w.resolver.ResolveRead(fmv, ctx);
        CHECK(e.transform.region.applied && TexelR(w.be.At(e.owner.surface), 55, 0) == 0x55);
    }
    World w;
    // --- Mip levels: MMIN 2 + MXL 2 with two authored bases -> two UploadLevel
    // calls (32x32, 16x16), the synth tail, owner.levels 2; a level window change
    // is a different content key (a new decode).
    DrawIr mip = TexDraw(0x100000, 0x00, 6, 6, 64, 0x300000);
    mip.tex.mmin = 2; mip.tex.mxl = 2;
    mip.tex.mip_tbp[0] = 0x110000; mip.tex.mip_tbw[0] = 64;
    mip.tex.mip_tbp[1] = 0x118000; mip.tex.mip_tbw[1] = 64;
    const int lv0 = w.be.level_uploads, cr0 = w.be.creates;
    TextureSource ms = w.resolver.ResolveRead(mip, ctx);
    CHECK(ms.enabled && ms.owner.kind == TextureSource::OwnerKind::Decoded);
    CHECK(w.be.level_uploads == lv0 + 2 && w.be.last_level == 2 && w.be.last_level_w == 16 && w.be.last_level_h == 16);
    CHECK(w.be.mip_finishes == 1 && w.be.last_mip_authored == 2);
    CHECK(ms.owner.levels == 2 && ms.transform.mip_levels == 2);
    CHECK(!ms.transform.region.applied);   // a mip read is not region-eligible
    CHECK(w.resolver.ResolveRead(mip, ctx).owner.levels == 2 && w.be.creates == cr0 + 1);   // the hit remembers
    const uint8_t mark = 0x77;
    w.vram.WriteStream(0x118000 + 5, &mark, 1);   // level 2's window moved on
    w.vram.SettleWrites();
    (void)w.resolver.ResolveRead(mip, ctx);
    CHECK(w.be.creates == cr0 + 2 && w.be.level_uploads == lv0 + 4);
    DrawIr nomip = mip;
    nomip.tex.mmin = 1;   // a filter that never selects a level: no chain
    CHECK(w.resolver.ResolveRead(nomip, ctx).owner.levels == 0 && w.be.level_uploads == lv0 + 4);
    // --- Evict under draw: the L1 entry over the drawn rows goes, L2 keeps the surface.
    DecodeCache& c = w.resolver.Cache();
    const uint8_t distinct[4] = {1, 2, 3, 4};   // the key is address-free: give 0x200000 its own bytes
    w.vram.WriteStream(0x200000, distinct, 4);
    w.vram.SettleWrites();
    DrawIr plain = TexDraw(0x200000, 0x00, 6, 6, 64, 0x300000);
    TextureSource ps = w.resolver.ResolveRead(plain, ctx);
    const size_t l1 = c.L1Size(), l2 = c.L2Size();
    DrawIr over;
    over.raster.frame.fbp = 0x200000;
    over.raster.frame.fbw = 64;
    over.raster.frame.psm = 0x00;
    render::DrawResult r;
    r.drew = true;
    r.bbox_valid = true;
    r.bbox_tri[0] = 0; r.bbox_tri[1] = 0; r.bbox_tri[2] = 64; r.bbox_tri[3] = 16;   // rows 0..16 -> page 0
    w.resolver.EvictUnderDraw(over, r, 64);
    CHECK(c.L1Size() == l1 - 1 && c.L2Size() == l2 && w.be.Has(ps.owner.surface));
    const int cr1 = w.be.creates;
    CHECK(w.resolver.ResolveRead(plain, ctx).owner.surface.id == ps.owner.surface.id && w.be.creates == cr1);   // L2 rebind
    CHECK(c.L1Size() == l1);
    over.raster.frame.psm = 0x02;   // a partial-bit-masked 16-bit draw evicts nothing
    r.fbmsk16 = 0x00FF;
    w.resolver.EvictUnderDraw(over, r, 64);
    CHECK(c.L1Size() == l1);
    // --- OnCpuWrite: the InvalidateTextures job retires the decodes under the
    // write, marks the pages CPU-authored (no owed band) and CPU-dirty.
    w.vram.NoteGpuElided(VramImage::Span{0x200000, 0x2000}, 0x200000, 0, 8);
    CHECK(w.vram.Page(0x200000 / 8192).author == PageRecord::Author::GpuTarget && !w.vram.Page(0x200000 / 8192).BandEmpty());
    CHECK(!w.refresher.ArePagesCpuDirty(0x200000, 0x2000));
    w.resolver.OnCpuWrite(0x200000, 0x100);
    CHECK(c.L1Size() == l1 - 1);
    CHECK(w.vram.Page(0x200000 / 8192).author == PageRecord::Author::Cpu && w.vram.Page(0x200000 / 8192).author_fbp == 0);
    CHECK(w.vram.Page(0x200000 / 8192).BandEmpty());
    CHECK(w.refresher.ArePagesCpuDirty(0x200000, 0x2000) && !w.refresher.ArePagesCpuDirty(0x202000, 0x2000));
    w.resolver.OnCpuWrite(0x300000, 0);   // size 0: nothing
    CHECK(!w.refresher.ArePagesCpuDirty(0x300000, 0x2000));
}

void Inv33FogLaneLive() {
    World w;
    const uint32_t fbp = 0x280000, partner = fbp + 448u * 512u * 2u;
    RenderTarget& t = w.targets.GetOrCreate(fbp, 512, 448, 0x02, 1);
    RenderTarget& p = w.targets.GetOrCreate(partner, 512, 448, 0x02, 1);
    for (RenderTarget* x : {&t, &p}) {
        x->coherence.valid_rect = LogicalRect{0, 0, 512, 448};
        x->coherence.has_content = true;
        x->coherence.last_draw_frame = 9;
        x->identity.render_psm = 0x02;
        x->identity.render_fbw = 512;
    }
    TextureResolver::Context ctx;
    ctx.frame_count = 9;
    ctx.active_fbp = 0x70000;
    DrawIr d = TexDraw(fbp, 0x1B, 9, 9, 512, 0x70000);
    d.tex.cbp = 0x3F0000;
    // Neither the keep mask nor a live lane: no fmt 5 serve.
    TextureSource a = w.resolver.ResolveRead(d, ctx);
    CHECK(a.owner.fmt_case != 5);
    // Lane draw is the last author of both main views in this frame: serve.
    for (RenderTarget* x : {&t, &p}) {
        x->coherence.main_author_seq = 4;
        x->coherence.lane_main.seq = 4;
        x->coherence.lane_main.frame = 9;
    }
    TextureSource b = w.resolver.ResolveRead(d, ctx);
    CHECK(b.enabled && b.owner.fmt_case == 5 && b.owner.partner.id == p.views.color.id && b.owner.clut.id != 0);
    // A later author of the partner's main view (seq moves on): the lane is stale.
    p.coherence.main_author_seq = 5;
    TextureSource c = w.resolver.ResolveRead(d, ctx);
    CHECK(c.owner.fmt_case != 5);
    // The keep-mask route admits the pair regardless of the lane record.
    t.coherence.sync_keep_mask16 = RenderTarget::kIndexLaneKeep16;
    p.coherence.sync_keep_mask16 = RenderTarget::kIndexLaneKeep16;
    TextureSource e = w.resolver.ResolveRead(d, ctx);
    CHECK(e.owner.fmt_case == 5);
    // A stale frame (not this frame) is not live.
    p.coherence.sync_keep_mask16 = 0;
    p.coherence.main_author_seq = 4;
    p.coherence.lane_main.frame = 8;
    CHECK(w.resolver.ResolveRead(d, ctx).owner.fmt_case != 5);
}

void ClutHeal() {
    ClutHealCache h;
    VramLayout layout;
    layout.clut_heal_exempt_lo = 0x388000;
    layout.clut_heal_exempt_hi = 0x3F8000;
    layout.clut_heal_exempt_mask = 0xFFFF;
    layout.clut_heal_exempt_value = 0x8000;
    uint32_t good[256], zero[256] = {0};
    for (int i = 0; i < 256; i++) good[i] = 0x80000000u | static_cast<uint32_t>(i);
    CHECK(!h.Heal(0x1000, zero, layout));      // nothing cached yet: stays zero
    CHECK(!h.Heal(0x1000, good, layout) && h.HasCached(0x1000));
    uint32_t z2[256] = {0};
    CHECK(h.Heal(0x1000, z2, layout) && z2[7] == good[7]);
    uint32_t few[256] = {0};
    for (int i = 0; i < 199; i++) few[i] = 0x80000000u | 1u;
    CHECK(!h.Heal(0x2000, few, layout) && !h.HasCached(0x2000));   // < 200 good entries never caches
    CHECK(!h.Heal(0x388000, good, layout) && !h.HasCached(0x388000));   // exempt base: untouched
    uint32_t z3[256] = {0};
    CHECK(!h.Heal(0x388000, z3, layout) && z3[0] == 0);
}

void UvBboxScan() {
    std::vector<VertexRec> v(3);
    v[0].uv_u = 16 * 4; v[0].uv_v = 16 * 8;
    v[1].uv_u = 16 * 20; v[1].uv_v = 16 * 2;
    v[2].uv_u = 16 * 10; v[2].uv_v = 16 * 30;
    UvBbox::Result r = UvBbox::Scan(v.data(), 3, true, 64, 64);
    CHECK(r.state == UvBbox::kOk && r.u0 == 4 && r.u1 == 20 && r.v0 == 2 && r.v1 == 30);
    for (auto& x : v) { x.q = 2.0f; x.s = 0.5f; x.t = 0.25f; }
    r = UvBbox::Scan(v.data(), 3, false, 64, 32);
    CHECK(r.state == UvBbox::kOk && r.u0 == 16 && r.u1 == 16 && r.v0 == 4 && r.v1 == 4);
    v[1].q = 0.0f;
    CHECK(UvBbox::Scan(v.data(), 3, false, 64, 32).state == UvBbox::kBail);
    CHECK(UvBbox::Scan(v.data(), 0, true, 64, 32).state == UvBbox::kBail);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    auto run = [&](const char* name, void (*fn)(), const char* group = nullptr) {
        if (which == "all" || which == name || (group && which == group)) fn();
    };
    run("inrt", WindowInTargetPort);
    run("idx16", Idx16LanePort);
    run("s9", S9ShadowPredicates);
    run("l2", L2EntriesPort);
    run("clut", ClutSourceVsReference);
    run("magnif", DisplayMagnification);
    run("inv18", Inv18ContentKeyAddressFree);
    run("inv19", Inv19DecodeRectIsCrop);
    run("inv17", Inv17PerCallReset);
    run("inv15", Inv15NeverStale);
    run("inv33", Inv33FogLaneLive);
    run("tiers_color", ResolveTiersColor, "tiers");
    run("tiers_indexed", ResolveTiersIndexed, "tiers");
    run("tiers_depth", ResolveTiersDepth, "tiers");
    run("tiers_lane", ResolveTiersLaneLive, "tiers");
    run("tiers_flush", ResolveTiersFlushes, "tiers");
    run("tiers_remainder", ResolveTiersRemainder, "tiers");
    run("uvregion", UvRegionDerive);
    run("cache_miss", DecodeCacheOnMiss);
    run("prep_bytes", DecodeFallbackPrepBytes);
    run("heal", ClutHeal);
    run("uvbbox", UvBboxScan);
    std::printf("test_texsrc %s: %d checks, %d failures\n", which.c_str(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
