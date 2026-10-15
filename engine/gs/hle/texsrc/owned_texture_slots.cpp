// gs/hle/texsrc/owned_texture_slots.cpp -- OwnedTextureSlots.
#include "engine/gs/hle/texsrc/owned_texture_slots.h"

#include <algorithm>
#include <utility>

#include "engine/gs/hle/texsrc/mip_sampler.h"

namespace ps2r {
namespace gs {

namespace {

bool Overlaps(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
    return static_cast<uint64_t>(a) < static_cast<uint64_t>(b) + bn &&
           static_cast<uint64_t>(b) < static_cast<uint64_t>(a) + an;
}

}  // namespace

OwnedTextureSlots::OwnedTextureSlots(render::Backend& be) : be_(be) {}

OwnedTextureSlots::~OwnedTextureSlots() { Reset(); }

OwnedTextureSlots::Image* OwnedTextureSlots::Find(uint64_t key) {
    auto it = images_.find(key);
    return it == images_.end() ? nullptr : &it->second;
}

void OwnedTextureSlots::Unbind(std::map<uint32_t, Slot>::iterator it) {
    if (Image* img = Find(it->second.key); img && img->bound) --img->bound;
    bound_.erase(it);
}

void OwnedTextureSlots::DropImage(uint64_t key) {
    auto it = images_.find(key);
    if (it == images_.end()) return;
    for (render::SurfaceId& s : it->second.surface) {
        if (!s.id) continue;
        be_.DestroySurface(s);
        s = render::SurfaceId{};
        --census_.surfaces;
    }
    census_.surface_bytes -= it->second.bytes;
    images_.erase(it);
}

void OwnedTextureSlots::Bind(uint32_t base, Texture&& t) {
    ++census_.binds;
    if (t.levels == 0 || t.levels > kMaxLevels || t.rgba.size() < t.levels || t.footprint == 0) return;
    // A bind over live binds: the transferred bytes would have overwritten them.
    for (auto it = bound_.lower_bound(0); it != bound_.end();) {
        if (it->first != base && Overlaps(it->first, it->second.footprint, base, t.footprint)) {
            ++census_.evicted_cpu;
            auto next = std::next(it);
            Unbind(it);
            it = next;
        } else {
            ++it;
        }
    }
    if (auto it = bound_.find(base); it != bound_.end()) {
        ++census_.rebinds;
        Unbind(it);
    }
    const uint64_t key = t.key;
    auto found = images_.find(key);
    if (found == images_.end()) {
        Image img;
        img.texture = std::move(t);
        found = images_.emplace(key, std::move(img)).first;
    }
    found->second.last_use = ++use_clock_;
    ++found->second.bound;
    bound_[base] = Slot{key, found->second.texture.footprint};
    EnforceBudget();
}

void OwnedTextureSlots::Release(uint64_t key) {
    for (auto it = bound_.begin(); it != bound_.end();) {
        if (it->second.key == key) {
            ++census_.released;
            it = bound_.erase(it);
        } else {
            ++it;
        }
    }
    DropImage(key);
}

void OwnedTextureSlots::EvictRange(uint32_t start, uint32_t size, bool draw) {
    if (size == 0 || bound_.empty()) return;
    for (auto it = bound_.begin(); it != bound_.end();) {
        if (Overlaps(it->first, it->second.footprint, start, size)) {
            if (draw) ++census_.evicted_draw; else ++census_.evicted_cpu;
            auto next = std::next(it);
            Unbind(it);
            it = next;
        } else {
            ++it;
        }
    }
}

bool OwnedTextureSlots::Covers(uint32_t tbp) const {
    auto it = bound_.upper_bound(tbp);
    if (it == bound_.begin()) return false;
    --it;
    return tbp < it->first + it->second.footprint;
}

render::SurfaceId OwnedTextureSlots::Surface(Image& img, uint8_t chain) {
    render::SurfaceId& slot = img.surface[chain];
    if (slot.id) return slot;
    const Texture& t = img.texture;
    const Level& l0 = t.rgba[0];
    const auto pw = [](const Level& L) { return L.pw ? L.pw : L.w; };
    const auto ph = [](const Level& L) { return L.ph ? L.ph : L.h; };
    render::SurfaceDesc d;
    d.width = pw(l0);
    d.height = ph(l0);
    d.format = render::SurfaceFormat::Rgba8;
    const render::SurfaceId tex = be_.CreateSurface(d);
    render::Rect r;
    r.w = static_cast<int>(pw(l0));
    r.h = static_cast<int>(ph(l0));
    be_.Upload(tex, r, l0.rgba, static_cast<size_t>(pw(l0)) * 4);
    uint64_t bytes = static_cast<uint64_t>(pw(l0)) * ph(l0) * 4;
    // The authored levels the sampler captures, then the synthesized tail -- the
    // decode fallback's exact sequence for a chain of this depth.
    for (uint8_t i = 0; i < chain; ++i) {
        const Level& L = t.rgba[i + 1];
        be_.UploadLevel(tex, i + 1, static_cast<int>(pw(L)), static_cast<int>(ph(L)), L.rgba);
        bytes += static_cast<uint64_t>(pw(L)) * ph(L) * 4;
    }
    if (chain > 0) be_.FinishMipChain(tex, chain, true);
    slot = tex;
    img.bytes += bytes;
    ++census_.surfaces_built;
    ++census_.surfaces;
    census_.surface_bytes += bytes;
    census_.surface_uploads += bytes;
    return tex;
}

bool OwnedTextureSlots::Serve(const TexReadView& v, TextureSource* out) {
    last_miss_ = Miss::None;
    auto it = bound_.upper_bound(v.tbp);
    if (it == bound_.begin()) { last_miss_ = Miss::NoBind; return false; }
    --it;
    const uint32_t base = it->first;
    if (v.tbp >= base + it->second.footprint) { last_miss_ = Miss::NoBind; return false; }
    Image* img = Find(it->second.key);
    if (!img) { last_miss_ = Miss::NoBind; return false; }
    const Texture& t = img->texture;
    const auto miss = [&](Miss why) {
        last_miss_ = why;
        ++census_.misses;
        if (why == Miss::Tuple || why == Miss::Size) ++census_.miss_tuple;
        if (why == Miss::Clut) ++census_.miss_clut;
        if (why == Miss::Mips) ++census_.miss_mips;
        return false;
    };
    if (v.tbp != base + t.level_offset[0] || v.tbw != t.level_tbw[0] || v.psm != t.psm) return miss(Miss::Tuple);
    if (v.tw != t.tw || v.th != t.th || (1u << v.tw) != t.rgba[0].w || (1u << v.th) != t.rgba[0].h)
        return miss(Miss::Size);
    if (t.psm == 0x13 && (v.cbp != base + t.clut_offset || v.cpsm != 0 || v.csm != 0 || v.csa != 0))
        return miss(Miss::Clut);
    const MipSampler::Chain chain = MipSampler::Capture(v);
    if (chain.levels + 1u > t.levels) return miss(Miss::Mips);
    for (uint8_t i = 0; i < chain.levels; ++i) {
        const MipSampler::Level& L = chain.level[i];
        const Level& A = t.rgba[i + 1];
        if (L.tbp_bytes != base + t.level_offset[i + 1] || L.tbw_px != t.level_tbw[i + 1] ||
            L.w != A.w || L.h != A.h)
            return miss(Miss::Mips);
    }
    const render::SurfaceId tex = Surface(*img, chain.levels);
    img->last_use = ++use_clock_;
    ++census_.serves;
    out->owner.kind = TextureSource::OwnerKind::Decoded;
    out->owner.owned = true;
    out->owner.surface = tex;
    out->owner.levels = chain.levels;
    out->transform.mip_levels = chain.levels;
    if (chain.levels) {
        out->transform.mip_lcm = chain.lcm;
        out->transform.mip_k = chain.k;
        out->transform.mip_l = chain.l;
    }
    out->enabled = true;
    return true;
}

void OwnedTextureSlots::EnforceBudget() {
    while (census_.surface_bytes > kMaxSurfaceBytes) {
        // The least recently used image no bind references.
        uint64_t victim = 0, oldest = ~0ull;
        bool found = false;
        for (const auto& [key, img] : images_) {
            if (img.bound || img.bytes == 0) continue;
            if (img.last_use < oldest) { oldest = img.last_use; victim = key; found = true; }
        }
        if (!found) return;
        ++census_.surfaces_evicted;
        DropImage(victim);
    }
}

void OwnedTextureSlots::Reset() {
    bound_.clear();
    std::vector<uint64_t> keys;
    keys.reserve(images_.size());
    for (const auto& [key, img] : images_) keys.push_back(key);
    for (uint64_t key : keys) DropImage(key);
}

}  // namespace gs
}  // namespace ps2r
