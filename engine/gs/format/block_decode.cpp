// gs/format/block_decode.cpp -- page-column texture read over a page LUT generated
// from PsmLayout (GS_SKILL chapter 8), values per PixelConvert / TexaExpand /
// ClutLayout. See block_decode.h for the contract.

#include "engine/gs/format/block_decode.h"

#include <cstring>
#include <memory>
#include <mutex>

#include "engine/gs/format/clut_layout.h"
#include "engine/gs/format/pixel_convert.h"
#include "engine/gs/format/psm_layout.h"

#if defined(PS2R_SIMD) && defined(__ARM_NEON)
#include <arm_neon.h>
#define PS2R_BLOCK_DECODE_NEON 1
#else
#define PS2R_BLOCK_DECODE_NEON 0
#endif

namespace ps2r {
namespace gs {

namespace {

int PsmIndex(Psm psm) {
    const PsmDescriptor* t = PsmLayout::Table();
    for (int i = 0; i < kPsmCount; i++)
        if (t[i].psm == psm) return i;
    return 0;
}

// The per-mode page LUTs, generated once each (page_w * page_h entries of
// PsmLayout::ElementInPage). Element offsets inside a page are < 16384 (T4), so
// 16 bits hold every mode.
struct PageLutTable {
    std::once_flag once[kPsmCount];
    std::unique_ptr<uint16_t[]> lut[kPsmCount];
};
PageLutTable& Luts() {
    static PageLutTable t;
    return t;
}

// Everything the inner loop needs, resolved once per call.
struct Hoisted {
    const PsmDescriptor* d;
    const uint16_t* lut;
    uint32_t pages_per_row;
    uint64_t base_elements;    // tbp in elements
    uint64_t per_page;         // elements per page
    uint64_t vram_elements;    // 4 MB in elements
};

Hoisted Hoist(const BlockDecode::Texture& tex) {
    Hoisted h;
    h.d = &PsmLayout::Describe(tex.psm);
    h.lut = BlockDecode::PageLut(tex.psm);
    h.pages_per_row = PsmLayout::PagesPerRow(tex.psm, tex.tbw);
    h.base_elements = uint64_t(tex.tbp) * PsmLayout::ElementsPerBlock(tex.psm);
    h.per_page = PsmLayout::ElementsPerPage(tex.psm);
    h.vram_elements = uint64_t(BlockDecode::kVramBytes) * 8u / h.d->storage_bits;
    return h;
}

// Fetch the storage element at element address `e` (already wrapped).
inline uint32_t FetchElement(const uint8_t* vram, const PsmDescriptor& d, uint32_t e) {
    switch (d.storage_bits) {
        case 32: { uint32_t v; std::memcpy(&v, vram + size_t(e) * 4u, 4); return v; }
        case 16: { uint16_t v; std::memcpy(&v, vram + size_t(e) * 2u, 2); return v; }
        case 8:  return vram[e];
        default: return uint32_t(vram[e >> 1] >> ((e & 1u) * 4u)) & 0xFu;   // 4
    }
}

#if PS2R_BLOCK_DECODE_NEON
// Eight 5:5:5:1 texels -> eight RGBA32 words: replicated RGB (TexaExpand rule 1)
// and the TEXA alpha table (rule 2). Bit-identical to TexaExpand::ApplyTexa16.
inline void Expand16x8Neon(const uint16_t* in, const TexaExpand::Texa& texa, uint32_t* out) {
    const uint16x8_t v = vld1q_u16(in);
    const uint16x8_t m5 = vdupq_n_u16(0x1F);
    const uint16x8_t r5 = vandq_u16(v, m5);
    const uint16x8_t g5 = vandq_u16(vshrq_n_u16(v, 5), m5);
    const uint16x8_t b5 = vandq_u16(vshrq_n_u16(v, 10), m5);
    const uint16x8_t abit = vshrq_n_u16(v, 15);
    // replicate: (c << 3) | (c >> 2)
    const uint16x8_t r8 = vorrq_u16(vshlq_n_u16(r5, 3), vshrq_n_u16(r5, 2));
    const uint16x8_t g8 = vorrq_u16(vshlq_n_u16(g5, 3), vshrq_n_u16(g5, 2));
    const uint16x8_t b8 = vorrq_u16(vshlq_n_u16(b5, 3), vshrq_n_u16(b5, 2));
    // alpha: A=1 -> TA1; A=0 -> (AEM && rgb == 0) ? 0 : TA0
    const uint16x8_t rgb_zero = vceqq_u16(vandq_u16(v, vdupq_n_u16(0x7FFF)), vdupq_n_u16(0));
    const uint16x8_t a_is1 = vceqq_u16(abit, vdupq_n_u16(1));
    uint16x8_t a0 = vdupq_n_u16(texa.ta0);
    if (texa.aem) a0 = vbicq_u16(a0, rgb_zero);
    const uint16x8_t a = vbslq_u16(a_is1, vdupq_n_u16(texa.ta1), a0);
    // pack: r | g<<8 in the low halfword, b | a<<8 in the high halfword
    const uint16x8_t lo = vorrq_u16(r8, vshlq_n_u16(g8, 8));
    const uint16x8_t hi = vorrq_u16(b8, vshlq_n_u16(a, 8));
    const uint16x8x2_t z = vzipq_u16(lo, hi);
    vst1q_u32(out,     vreinterpretq_u32_u16(z.val[0]));
    vst1q_u32(out + 4, vreinterpretq_u32_u16(z.val[1]));
}
#endif

// The row walk shared by the scalar and SIMD paths: runs inside one page,
// element addresses through the LUT, `emit(run_out, elements, n)` converts.
template <typename Emit>
void WalkRows(const uint8_t* vram, const Hoisted& h, const BlockDecode::Rect& rect,
              uint32_t* out, Emit&& emit) {
    const PsmDescriptor& d = *h.d;
    uint32_t scratch[128];   // >= page_w of every mode
    for (uint32_t ry = 0; ry < rect.h; ry++) {
        const uint32_t y = rect.y + ry;
        const uint16_t* lrow = h.lut + (y % d.page_h) * d.page_w;
        const uint64_t row_page = uint64_t(y / d.page_h) * h.pages_per_row;
        uint32_t* orow = out + size_t(ry) * rect.w;
        uint32_t x = rect.x;
        const uint32_t xend_all = rect.x + rect.w;
        while (x < xend_all) {
            const uint32_t page_x = x / d.page_w;
            const uint32_t xend = (page_x + 1u) * d.page_w < xend_all ? (page_x + 1u) * d.page_w : xend_all;
            const uint64_t page_base = h.base_elements + (row_page + page_x) * h.per_page;
            const uint32_t n = xend - x;
            for (uint32_t i = 0; i < n; i++) {
                uint64_t e = page_base + lrow[(x + i) % d.page_w];
                if (e >= h.vram_elements) e %= h.vram_elements;
                scratch[i] = FetchElement(vram, d, uint32_t(e));
            }
            emit(orow + (x - rect.x), scratch, n);
            x = xend;
        }
    }
}

}  // namespace

uint32_t BlockDecode::PaletteEntries(Psm psm) {
    const PsmDescriptor& d = PsmLayout::Describe(psm);
    if (!d.is_indexed) return 0;
    return d.index_bits == 8 ? ClutLayout::kEntries8 : ClutLayout::kEntries4;
}

const uint16_t* BlockDecode::PageLut(Psm psm) {
    const int i = PsmIndex(psm);
    PageLutTable& t = Luts();
    std::call_once(t.once[i], [&] {
        const PsmDescriptor& d = PsmLayout::Describe(psm);
        const uint32_t n = uint32_t(d.page_w) * d.page_h;
        std::unique_ptr<uint16_t[]> lut(new uint16_t[n]);
        for (uint32_t ly = 0; ly < d.page_h; ly++)
            for (uint32_t lx = 0; lx < d.page_w; lx++)
                lut[ly * d.page_w + lx] = uint16_t(PsmLayout::ElementInPage(psm, lx, ly));
        t.lut[i] = std::move(lut);
    });
    return t.lut[i].get();
}

uint32_t BlockDecode::TexelValue(Psm psm, uint32_t element, const TexaExpand::Texa& texa,
                                 const uint32_t* palette) {
    switch (psm) {
        case Psm::CT32:  return element;
        case Psm::CT24:  return TexaExpand::ApplyTexa24(element, texa);
        case Psm::CT16:
        case Psm::CT16S: return TexaExpand::ApplyTexa16(uint16_t(element), texa);
        case Psm::T8: case Psm::T4: case Psm::T8H: case Psm::T4HL: case Psm::T4HH:
            return palette[PixelConvert::UnpackIndex(psm, element)];
        case Psm::Z32:   return element;
        case Psm::Z24:   return element & 0x00FFFFFFu;
        case Psm::Z16:
        case Psm::Z16S:  return TexaExpand::Expand16To32(uint16_t(element));
    }
    return 0;
}

void BlockDecode::ReadPalette(const uint8_t* vram, const Palette& pal, uint32_t entries,
                              const TexaExpand::Texa& texa, uint32_t* out) {
    const bool wide = PsmLayout::Describe(pal.cpsm).storage_bits == 32;
    for (uint32_t i = 0; i < entries; i++) {
        if (wide) out[i] = ClutLayout::ReadClut32(vram, pal.cbp, pal.csm2, entries, i);
        else      out[i] = TexaExpand::ApplyTexa16(ClutLayout::ReadClut16(vram, pal.cpsm, pal.cbp, pal.csm2, entries, i), texa);
    }
}

void BlockDecode::DecodeGeneric(const uint8_t* vram, const Texture& tex, const Rect& rect,
                                const TexaExpand::Texa& texa, const uint32_t* palette, uint32_t* out) {
    const PsmDescriptor& d = PsmLayout::Describe(tex.psm);
    for (uint32_t ry = 0; ry < rect.h; ry++)
        for (uint32_t rx = 0; rx < rect.w; rx++) {
            const uint32_t e = PsmLayout::PixelAddress(tex.psm, rect.x + rx, rect.y + ry, tex.tbp, tex.tbw);
            out[size_t(ry) * rect.w + rx] = TexelValue(tex.psm, FetchElement(vram, d, e), texa, palette);
        }
}

void BlockDecode::DecodeScalar(const uint8_t* vram, const Texture& tex, const Rect& rect,
                               const TexaExpand::Texa& texa, const uint32_t* palette, uint32_t* out) {
    const Hoisted h = Hoist(tex);
    const Psm psm = tex.psm;
    WalkRows(vram, h, rect, out, [&](uint32_t* o, const uint32_t* el, uint32_t n) {
        for (uint32_t i = 0; i < n; i++) o[i] = TexelValue(psm, el[i], texa, palette);
    });
}

void BlockDecode::Decode(const uint8_t* vram, const Texture& tex, const Rect& rect,
                         const TexaExpand::Texa& texa, const uint32_t* palette, uint32_t* out) {
#if PS2R_BLOCK_DECODE_NEON
    if (tex.psm == Psm::CT16 || tex.psm == Psm::CT16S) {
        const Hoisted h = Hoist(tex);
        WalkRows(vram, h, rect, out, [&](uint32_t* o, const uint32_t* el, uint32_t n) {
            uint16_t hw[128];
            for (uint32_t i = 0; i < n; i++) hw[i] = uint16_t(el[i]);
            uint32_t i = 0;
            for (; i + 8 <= n; i += 8) Expand16x8Neon(hw + i, texa, o + i);
            for (; i < n; i++) o[i] = TexaExpand::ApplyTexa16(hw[i], texa);
        });
        return;
    }
#endif
    DecodeScalar(vram, tex, rect, texa, palette, out);
}

void BlockDecode::Decode(const uint8_t* vram, const Texture& tex,
                         const TexaExpand::Texa& texa, const uint32_t* palette, uint32_t* out) {
    Decode(vram, tex, Rect{0, 0, tex.w, tex.h}, texa, palette, out);
}

bool BlockDecode::SimdEnabled() { return PS2R_BLOCK_DECODE_NEON != 0; }

BlockDecode::Footprint BlockDecode::DecodeFootprint(const Texture& tex) {
    const PsmDescriptor& d = PsmLayout::Describe(tex.psm);
    const uint32_t w = tex.w ? tex.w : 1u, hgt = tex.h ? tex.h : 1u;
    const uint64_t last_page = uint64_t((hgt - 1u) / d.page_h) * PsmLayout::PagesPerRow(tex.psm, tex.tbw) +
                               (w - 1u) / d.page_w;
    uint64_t size = (last_page + 1u) * kPageBytes;
    if (size > kVramBytes) size = kVramBytes;
    return Footprint{uint32_t((uint64_t(tex.tbp) * PsmLayout::kBlockBytes) % kVramBytes), uint32_t(size)};
}

BlockDecode::Footprint BlockDecode::DecodeRowBand(const Texture& tex, uint32_t y0, uint32_t y1) {
    const PsmDescriptor& d = PsmLayout::Describe(tex.psm);
    const uint32_t w = tex.w ? tex.w : 1u;
    if (y1 <= y0) y1 = y0 + 1u;
    const uint64_t ppr = PsmLayout::PagesPerRow(tex.psm, tex.tbw);
    const uint64_t first = uint64_t(y0 / d.page_h) * ppr;
    uint64_t last = uint64_t((y1 - 1u) / d.page_h) * ppr + (w - 1u) / d.page_w;
    if (last < first) last = first;
    uint64_t off = first * kPageBytes, size = (last - first + 1u) * kPageBytes;
    if (off > kVramBytes) off = kVramBytes;
    if (size > kVramBytes) size = kVramBytes;
    return Footprint{uint32_t(off), uint32_t(size)};
}

}  // namespace gs
}  // namespace ps2r
