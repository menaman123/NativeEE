// gs/hle/texsrc/clut_source.cpp -- ClutSource.
#include "engine/gs/hle/texsrc/clut_source.h"

#include <vector>

#include "engine/core/hash/hash.h"
#include "engine/gs/format/clut_layout.h"
#include "engine/gs/format/psm_layout.h"

namespace ps2r {
namespace gs {

ClutSource::ClutSource(render::Backend& be) : be_(be) {}

ClutSource::~ClutSource() { DropAll(); }

void ClutSource::DropAll() {
    for (auto& kv : entries_)
        if (kv.second.tex.id) be_.DestroySurface(kv.second.tex);
    entries_.clear();
}

void ClutSource::Build(const uint8_t* clut, bool fold_single, uint8_t* out) {
    // The 1 KiB window is the CLUT buffer at cbp with buffer width 1 (64 pixels):
    // entry i at grid (g % 16, g / 16), g = its CSM1 position, addressed as CT32
    // relative to the window start.
    for (uint32_t i = 0; i < 256; i++) {
        const uint32_t g = fold_single ? ClutLayout::Csm1Index(i)
                                       : ClutLayout::Csm1Index(ClutLayout::Csm1Index(i));
        const uint32_t elem = PsmLayout::PixelAddress(Psm::CT32, g % 16, g / 16, 0, 1);
        const uint32_t off = (elem * 4) % kClutBytes;
        const uint8_t r = clut[off], gg = clut[off + 1], b = clut[off + 2], a = clut[off + 3];
        uint8_t* p0 = out + i * 4;
        uint8_t* p1 = out + 256 * 4 + i * 4;
        p0[0] = r; p0[1] = gg; p0[2] = b; p0[3] = PrescaleAlpha(a);
        p1[0] = r; p1[1] = gg; p1[2] = b; p1[3] = a;
    }
}

render::SurfaceId ClutSource::Ensure(uint32_t cbp, bool fold_single, const VramImage& vram) {
    uint8_t bytes[kClutBytes];
    vram.ReadView(cbp, kClutBytes, bytes);
    const uint64_t h = hash::Fnv1a64(bytes, kClutBytes);
    const uint32_t key = cbp | (fold_single ? 0x80000000u : 0u);
    auto it = entries_.find(key);
    if (it != entries_.end() && it->second.tex.id && it->second.hash == h) return it->second.tex;
    std::vector<uint8_t> img(256 * 2 * 4);
    Build(bytes, fold_single, img.data());
    Entry& e = entries_[key];
    if (!e.tex.id) {
        render::SurfaceDesc d;
        d.width = 256;
        d.height = 2;
        d.format = render::SurfaceFormat::Rgba8;
        d.renderable = false;
        e.tex = be_.CreateSurface(d);
        if (!e.tex.id) return e.tex;
    }
    render::Rect r;
    r.x = 0; r.y = 0; r.w = 256; r.h = 2;
    be_.Upload(e.tex, r, img.data(), 256 * 4);
    e.hash = h;
    return e.tex;
}

}  // namespace gs
}  // namespace ps2r
