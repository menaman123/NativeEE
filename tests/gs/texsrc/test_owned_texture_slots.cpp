// tests/gs/texsrc/test_owned_texture_slots.cpp -- OwnedTextureSlots over the CPU
// FakeBackend and the resolver's OwnedSlot tier: a bind serves exactly the TEX0
// tuple it describes (level 0, CLUT, every captured mip level) with the decode
// fallback's surface shape (levels, mip fields), misses are counted and fall
// through to the VRAM decode, binds retire under a draw / an upload / a rebind /
// Release, surfaces are cached per chain depth and bounded.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gs/hle/targets/render_target_cache.h"
#include "engine/gs/hle/targets/target_refresher.h"
#include "engine/gs/hle/texsrc/owned_texture_slots.h"
#include "engine/gs/hle/texsrc/texture_resolver.h"
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

// A 64x64 T8 texture with three authored levels at the title's slot layout: level
// 0 at the base (TBW 64), level 1 at +0x1000 (TBW 64), level 2 at +0x1400, CLUT at
// +0x2000; the storage image is 64 x 160 CT32 (40 KiB).
struct Asset {
    std::shared_ptr<std::vector<uint8_t>> bytes;
    OwnedTextureSlots::Texture texture;
};
Asset MakeAsset(uint64_t key, uint8_t seed) {
    Asset a;
    a.bytes = std::make_shared<std::vector<uint8_t>>();
    const uint32_t dims[3][2] = {{64, 64}, {32, 32}, {16, 16}};
    size_t total = 0;
    for (auto& d : dims) total += size_t(d[0]) * d[1] * 4;
    a.bytes->resize(total);
    for (size_t i = 0; i < total; ++i) (*a.bytes)[i] = uint8_t(i * 31 + seed);
    a.texture.key = key;
    a.texture.psm = 0x13;
    a.texture.tw = 6;
    a.texture.th = 6;
    a.texture.levels = 3;
    const uint32_t offsets[3] = {0, 0x1000, 0x1400};
    size_t at = 0;
    for (unsigned i = 0; i < 3; ++i) {
        a.texture.level_offset[i] = offsets[i];
        a.texture.level_tbw[i] = 64;
        a.texture.rgba.push_back({dims[i][0], dims[i][1], a.bytes->data() + at});
        at += size_t(dims[i][0]) * dims[i][1] * 4;
    }
    a.texture.clut_offset = 0x2000;
    a.texture.footprint = 64 * 160 * 4;
    a.texture.pin = a.bytes;
    return a;
}
// A copy of the asset's bind description (the bind takes it by value).
OwnedTextureSlots::Texture Take(const Asset& a) { return a.texture; }

TexReadView Read(uint32_t base, uint8_t mxl = 0) {
    TexReadView v;
    v.tbp = base;
    v.tbw = 64;
    v.psm = 0x13;
    v.tw = 6;
    v.th = 6;
    v.cbp = base + 0x2000;
    v.cpsm = 0;
    v.csm = 0;
    v.csa = 0;
    v.mxl = mxl;
    v.mmin = mxl ? 4 : 0;   // LINEAR_MIPMAP_NEAREST: the chain is captured
    v.mip_tbp[0] = base + 0x1000;
    v.mip_tbw[0] = 64;
    v.mip_tbp[1] = base + 0x1400;
    v.mip_tbw[1] = 64;
    return v;
}

DrawIr Draw(const TexReadView& v) {
    DrawIr ir;
    ir.tex_enabled = true;
    ir.tex = v;
    ir.raster.frame.fbp = 0x70000;
    ir.raster.frame.fbw = 64;
    return ir;
}

void ServeAndMiss() {
    World w;
    OwnedTextureSlots& o = w.resolver.Owned();
    Asset a = MakeAsset(0x1001, 3);
    const uint32_t base = 0x3f0000;
    o.Bind(base, Take(a));
    CHECK(o.BoundCount() == 1 && o.Stats().binds == 1);
    TextureSource out;
    // Level 0 alone (no chain): a surface with no authored mips.
    CHECK(o.Serve(Read(base), &out));
    CHECK(out.enabled && out.owner.kind == TextureSource::OwnerKind::Decoded && out.owner.surface.id);
    CHECK(out.owner.levels == 0 && out.transform.mip_levels == 0);
    CHECK(o.Stats().serves == 1 && o.Stats().surfaces == 1 && o.Stats().surface_bytes == 64 * 64 * 4);
    // The same read again: the same surface, nothing rebuilt.
    TextureSource again;
    CHECK(o.Serve(Read(base), &again) && again.owner.surface.id == out.owner.surface.id);
    CHECK(o.Stats().surfaces_built == 1);
    // A two-level chain: a second surface built with levels 1..2 and the sampler fields.
    TextureSource mip;
    TexReadView vm = Read(base, 2);
    vm.k = 0x10;
    vm.l = 1;
    CHECK(o.Serve(vm, &mip) && mip.owner.levels == 2 && mip.transform.mip_levels == 2);
    CHECK(mip.transform.mip_k == 0x10 && mip.transform.mip_l == 1 && !mip.transform.mip_lcm);
    CHECK(mip.owner.surface.id != out.owner.surface.id && o.Stats().surfaces == 2);
    CHECK(o.Stats().surface_bytes == 64 * 64 * 4 + (64 * 64 * 4 + 32 * 32 * 4 + 16 * 16 * 4));
    const auto& fake = w.be.At(mip.owner.surface);
    CHECK(fake.desc.width == 64 && fake.desc.height == 64);
    CHECK(std::memcmp(fake.rgba.data(), a.bytes->data(), 64 * 64 * 4) == 0);
    // A chain deeper than the authored levels: a miss.
    TextureSource deep;
    TexReadView vd = Read(base, 3);
    vd.mip_tbp[2] = base + 0x1500;
    vd.mip_tbw[2] = 64;
    CHECK(!o.Serve(vd, &deep) && o.LastMiss() == OwnedTextureSlots::Miss::Mips && o.Stats().miss_mips == 1);
    // A mip level at another address: a miss.
    TexReadView vo = Read(base, 1);
    vo.mip_tbp[0] = base + 0x1800;
    CHECK(!o.Serve(vo, &deep) && o.LastMiss() == OwnedTextureSlots::Miss::Mips);
    // The CLUT elsewhere / another CSA: a miss.
    TexReadView vc = Read(base);
    vc.cbp = base + 0x2100;
    CHECK(!o.Serve(vc, &deep) && o.LastMiss() == OwnedTextureSlots::Miss::Clut);
    vc = Read(base);
    vc.csa = 1;
    CHECK(!o.Serve(vc, &deep) && o.LastMiss() == OwnedTextureSlots::Miss::Clut);
    // Another PSM / size / TBW: a miss; a read inside the footprint not at level 0: a miss.
    TexReadView vp = Read(base);
    vp.psm = 0x00;
    CHECK(!o.Serve(vp, &deep) && o.LastMiss() == OwnedTextureSlots::Miss::Tuple);
    vp = Read(base);
    vp.tw = 5;
    CHECK(!o.Serve(vp, &deep) && o.LastMiss() == OwnedTextureSlots::Miss::Size);
    vp = Read(base);
    vp.tbw = 128;
    CHECK(!o.Serve(vp, &deep) && o.LastMiss() == OwnedTextureSlots::Miss::Tuple);
    vp = Read(base);
    vp.tbp = base + 0x100;
    CHECK(!o.Serve(vp, &deep) && o.LastMiss() == OwnedTextureSlots::Miss::Tuple);
    // Outside every bind: no miss counted.
    const uint64_t misses = o.Stats().misses;
    CHECK(!o.Serve(Read(0x100000), &deep) && o.LastMiss() == OwnedTextureSlots::Miss::NoBind);
    CHECK(o.Stats().misses == misses && o.Covers(base + 0x2000) && !o.Covers(base + a.texture.footprint));
}

void ResolverTier() {
    World w;
    Asset a = MakeAsset(0x2002, 9);
    const uint32_t base = 0x3e0000;
    TextureResolver::Context ctx;
    // Before the bind: the VRAM decode (the mirror holds zeros).
    TextureSource plain = w.resolver.ResolveRead(Draw(Read(base)), ctx);
    CHECK(plain.owner.kind == TextureSource::OwnerKind::Decoded && w.resolver.Stats().decodes == 1);
    CHECK(w.resolver.Stats().owned_serves == 0);
    w.resolver.Owned().Bind(base, Take(a));
    TextureSource owned = w.resolver.ResolveRead(Draw(Read(base)), ctx);
    CHECK(owned.enabled && owned.owner.kind == TextureSource::OwnerKind::Decoded);
    CHECK(owned.owner.surface.id != plain.owner.surface.id);
    CHECK(w.resolver.Stats().owned_serves == 1 && w.resolver.Stats().decodes == 1 && w.resolver.Stats().lookups == 1);
    CHECK(owned.transform.uv_tex_w == 64 && owned.transform.uv_tex_h == 64);
    // A miss over the bind falls through to the decode and is counted.
    TexReadView vc = Read(base);
    vc.csa = 2;
    (void)w.resolver.ResolveRead(Draw(vc), ctx);
    CHECK(w.resolver.Stats().owned_misses == 1 && w.resolver.Stats().lookups == 2);
    // An EE upload over the footprint retires the bind (the bytes are the texture again).
    w.resolver.OnCpuWrite(base + 0x3000, 0x100);
    CHECK(w.resolver.Owned().BoundCount() == 0 && w.resolver.Owned().Stats().evicted_cpu == 1);
    (void)w.resolver.ResolveRead(Draw(Read(base)), ctx);
    CHECK(w.resolver.Stats().owned_serves == 1 && w.resolver.Stats().owned_misses == 1);
    // A draw landing over the footprint retires it too.
    w.resolver.Owned().Bind(base, Take(a));
    CHECK(w.resolver.Owned().BoundCount() == 1);
    DrawIr d;
    d.raster.frame.fbp = base;
    d.raster.frame.fbw = 64;
    d.raster.frame.psm = 0;
    render::DrawResult res;
    res.drew = true;
    res.bbox_valid = true;
    res.bbox_tri[0] = 0; res.bbox_tri[1] = 0; res.bbox_tri[2] = 64; res.bbox_tri[3] = 64;
    w.resolver.EvictUnderDraw(d, res, 64);
    CHECK(w.resolver.Owned().BoundCount() == 0 && w.resolver.Owned().Stats().evicted_draw == 1);
}

void LifecycleAndBudget() {
    World w;
    OwnedTextureSlots& o = w.resolver.Owned();
    Asset a = MakeAsset(0x3003, 1), b = MakeAsset(0x3004, 2);
    // A rebind of the base replaces the bind; the first image's surface stays cached
    // (unbound) until the budget or its release.
    o.Bind(0x3f0000, Take(a));
    TextureSource out;
    CHECK(o.Serve(Read(0x3f0000), &out));
    o.Bind(0x3f0000, Take(b));
    CHECK(o.Stats().rebinds == 1 && o.BoundCount() == 1);
    CHECK(o.Serve(Read(0x3f0000), &out) && std::memcmp(w.be.At(out.owner.surface).rgba.data(), b.bytes->data(), 64) == 0);
    CHECK(o.Stats().surfaces == 2);
    // The same image bound in two slots shares its surfaces.
    o.Bind(0x3e0000, Take(b));
    CHECK(o.Serve(Read(0x3e0000), &out) && o.Stats().surfaces == 2 && o.Stats().surfaces_built == 2);
    // A bind whose footprint overlaps another live bind retires it (the transfer
    // would have overwritten those bytes).
    Asset c = MakeAsset(0x3005, 7);
    o.Bind(0x3e4000, Take(c));
    CHECK(o.BoundCount() == 2 && !o.Covers(0x3e0000 + 0x100) && o.Covers(0x3e4000) && o.Stats().evicted_cpu == 1);
    // Release drops every bind and surface of one image.
    o.Release(0x3004);
    CHECK(o.Stats().released == 1 && o.BoundCount() == 1 && o.Stats().surfaces == 1);
    CHECK(!w.be.Has(out.owner.surface));
    // Reset destroys everything.
    o.Reset();
    CHECK(o.BoundCount() == 0 && o.Stats().surfaces == 0 && o.Stats().surface_bytes == 0);
    // A bind with no levels is refused (counted as a bind, nothing bound).
    OwnedTextureSlots::Texture empty;
    empty.key = 9;
    empty.footprint = 16;
    o.Bind(0x100000, std::move(empty));
    CHECK(o.BoundCount() == 0);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    if (which == "all" || which == "serve") ServeAndMiss();
    if (which == "all" || which == "tier") ResolverTier();
    if (which == "all" || which == "lifecycle") LifecycleAndBudget();
    std::printf("OwnedTextureSlots %s: %d checks, %d failures\n", which.c_str(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
