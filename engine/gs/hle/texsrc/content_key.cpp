// gs/hle/texsrc/content_key.cpp -- ContentKey.
#include "engine/gs/hle/texsrc/content_key.h"

#include "engine/core/hash/hash.h"
#include "engine/gs/format/block_decode.h"
#include "engine/gs/format/psm_layout.h"

namespace ps2r {
namespace gs {

namespace {

constexpr uint64_t kBasis = 1469598103934665603ull;   // the key's seed (fixed by the pinned values)

BlockDecode::Texture Level0(const TexReadView& v) {
    Psm psm = Psm::CT32;
    PsmLayout::FromField(v.psm, &psm);
    BlockDecode::Texture t;
    t.psm = psm;
    t.tbp = v.tbp / PsmLayout::kBlockBytes;
    t.tbw = v.tbw / 64;
    t.w = 1u << v.tw;
    t.h = 1u << v.th;
    return t;
}

uint64_t Mix64(uint64_t h, uint64_t v) {
    return h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2));
}

uint64_t Content(uint64_t h, const uint8_t* p, size_t n, uint64_t seed, bool fast) {
    if (fast) return Mix64(h, hash::Xxh64(p, n, seed));
    return ContentKey::Fnv1aStep(h, p, n);
}

}  // namespace

uint64_t ContentKey::Fnv1aStep(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

void ContentKey::Footprint(const TexReadView& v, uint32_t* start, uint32_t* size) {
    const BlockDecode::Footprint fp = BlockDecode::DecodeFootprint(Level0(v));
    *start = fp.start;
    *size = fp.size;
}

void ContentKey::RowBand(const TexReadView& v, int y0, int y1, uint32_t* rel, uint32_t* size) {
    const BlockDecode::Footprint fp =
        BlockDecode::DecodeRowBand(Level0(v), static_cast<uint32_t>(y0 < 0 ? 0 : y0),
                                   static_cast<uint32_t>(y1 < 0 ? 0 : y1));
    *rel = fp.start;
    *size = fp.size;
}

bool ContentKey::IsSubRect(const TexReadView& v, const Rect& r) {
    const int tex_w = 1 << v.tw, tex_h = 1 << v.th;
    return r.w > 0 && r.h > 0 && (r.x0 != 0 || r.y0 != 0 || r.w != tex_w || r.h != tex_h);
}

uint64_t ContentKey::Compute(const TexReadView& v, const Rect& rgn, const uint8_t* window,
                             size_t window_len, const uint8_t* clut, size_t clut_len,
                             const MipInput* mips, size_t mip_count, bool fast_hash) {
    uint64_t h = kBasis;
    static const char kSalt[6] = {'R', '1', '2', '5', 'C', 'K'};
    h = Fnv1aStep(h, kSalt, sizeof(kSalt));
    const uint32_t tuple[7] = {v.tbw, v.psm, v.tw, v.th, v.cpsm, v.csm, v.csa};
    h = Fnv1aStep(h, tuple, sizeof(tuple));
    const uint8_t texa[3] = {v.texa_ta0, v.texa_ta1, static_cast<uint8_t>(v.texa_aem ? 1 : 0)};
    h = Fnv1aStep(h, texa, sizeof(texa));
    if (IsSubRect(v, rgn)) {
        static const char kRgnSalt[6] = {'R', '1', '5', '9', 'R', 'G'};
        h = Fnv1aStep(h, kRgnSalt, sizeof(kRgnSalt));
        const int32_t rect[4] = {rgn.x0, rgn.y0, rgn.w, rgn.h};
        h = Fnv1aStep(h, rect, sizeof(rect));
    }
    if (window && window_len > 0) h = Content(h, window, window_len, 0, fast_hash);
    if (clut && clut_len > 0) h = Content(h, clut, clut_len, 2, fast_hash);
    if (mips && mip_count > 0) {
        static const char kMipSalt[6] = {'S', '8', '2', 'M', 'I', 'P'};
        h = Fnv1aStep(h, kMipSalt, sizeof(kMipSalt));
        const uint32_t nlv = static_cast<uint32_t>(mip_count);
        h = Fnv1aStep(h, &nlv, sizeof(nlv));
        for (uint32_t i = 0; i < nlv; i++) {
            h = Fnv1aStep(h, &mips[i].tbw_px, sizeof(uint32_t));
            if (mips[i].window && mips[i].window_len > 0)
                h = Content(h, mips[i].window, mips[i].window_len, 3 + i, fast_hash);
        }
    }
    return h;
}

}  // namespace gs
}  // namespace ps2r
