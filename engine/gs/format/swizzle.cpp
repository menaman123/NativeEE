// gs/format/swizzle.cpp -- the rect walker over PsmLayout (GS_SKILL chapter 8).

#include "engine/gs/format/swizzle.h"

#include "engine/gs/format/psm_layout.h"

namespace ps2r {
namespace gs {

namespace {

// ------------------------------------------------------------- element access
// A storage element at element index `e` (already wrapped to the 4 MB), read or
// written little-endian; `bits` is the mode's storage width.
inline uint32_t ReadElement(const uint8_t* vram, uint32_t bits, uint32_t e) {
    switch (bits) {
        case 32: {
            const uint8_t* p = vram + static_cast<size_t>(e) * 4u;
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        }
        case 16: {
            const uint8_t* p = vram + static_cast<size_t>(e) * 2u;
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8);
        }
        case 8:
            return vram[e];
        default:  // 4: low nibble is the even element
            return (vram[e >> 1] >> ((e & 1u) * 4u)) & 0xFu;
    }
}

inline void WriteElement(uint8_t* vram, uint32_t bits, uint32_t e, uint32_t value, uint32_t mask) {
    switch (bits) {
        case 32: {
            uint8_t* p = vram + static_cast<size_t>(e) * 4u;
            if (mask != 0xFFFFFFFFu) {
                const uint32_t old = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
                value = (old & ~mask) | (value & mask);
            }
            p[0] = uint8_t(value); p[1] = uint8_t(value >> 8); p[2] = uint8_t(value >> 16); p[3] = uint8_t(value >> 24);
            return;
        }
        case 16: {
            uint8_t* p = vram + static_cast<size_t>(e) * 2u;
            if ((mask & 0xFFFFu) != 0xFFFFu) {
                const uint32_t old = uint32_t(p[0]) | (uint32_t(p[1]) << 8);
                value = (old & ~mask) | (value & mask);
            }
            p[0] = uint8_t(value); p[1] = uint8_t(value >> 8);
            return;
        }
        case 8: {
            uint8_t* p = vram + e;
            if ((mask & 0xFFu) != 0xFFu) value = (*p & ~mask) | (value & mask);
            *p = uint8_t(value);
            return;
        }
        default: {  // 4
            uint8_t* p = vram + (e >> 1);
            const uint32_t shift = (e & 1u) * 4u;
            const uint32_t nib_mask = (mask & 0xFu) << shift;
            *p = uint8_t((*p & ~nib_mask) | ((value << shift) & nib_mask));
            return;
        }
    }
}

// -------------------------------------------------------------- linear access
// Pixel (i, j) of a linear image with `bpp` bits per pixel and `pitch` bytes per row.
inline uint32_t GetLinear(const uint8_t* img, size_t pitch, uint32_t bpp, uint32_t i, uint32_t j) {
    const uint8_t* row = img + static_cast<size_t>(j) * pitch;
    switch (bpp) {
        case 32: {
            const uint8_t* p = row + static_cast<size_t>(i) * 4u;
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        }
        case 16: {
            const uint8_t* p = row + static_cast<size_t>(i) * 2u;
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8);
        }
        case 8:
            return row[i];
        default:  // 4
            return (row[i >> 1] >> ((i & 1u) * 4u)) & 0xFu;
    }
}

inline void PutLinear(uint8_t* img, size_t pitch, uint32_t bpp, uint32_t i, uint32_t j, uint32_t v) {
    uint8_t* row = img + static_cast<size_t>(j) * pitch;
    switch (bpp) {
        case 32: {
            uint8_t* p = row + static_cast<size_t>(i) * 4u;
            p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
            return;
        }
        case 16: {
            uint8_t* p = row + static_cast<size_t>(i) * 2u;
            p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
            return;
        }
        case 8:
            row[i] = uint8_t(v);
            return;
        default: {  // 4
            uint8_t* p = row + (i >> 1);
            const uint32_t shift = (i & 1u) * 4u;
            *p = uint8_t((*p & ~(0xFu << shift)) | ((v & 0xFu) << shift));
            return;
        }
    }
}

// -------------------------------------------------- hoisted per-mode geometry
struct Mode {
    const PsmDescriptor* d;
    uint32_t bpp;            // linear bits per pixel
    uint32_t pixel_mask;     // ElementWriteMask
    uint32_t shift;          // element = pixel << shift (index-in-word modes)
    uint64_t per_block, per_page, vram_elements;
    const uint16_t* lut;     // ElementInBlock for (by, bx), row stride 32
};

// ElementInBlock tables for the 13 modes, generated from PsmLayout once (the
// block interior is at most 16 rows x 32 pixels); indexed by table position.
struct ModeTables {
    uint16_t lut[kPsmCount][16][32];
};

const ModeTables& Tables() {
    static const ModeTables tables = [] {
        ModeTables t{};
        for (int m = 0; m < kPsmCount; m++) {
            const PsmDescriptor& d = PsmLayout::Table()[m];
            for (uint32_t by = 0; by < d.block_h; by++)
                for (uint32_t bx = 0; bx < d.block_w; bx++)
                    t.lut[m][by][bx] = static_cast<uint16_t>(PsmLayout::ElementInBlock(d.psm, bx, by));
        }
        return t;
    }();
    return tables;
}

Mode Hoist(Psm psm) {
    Mode m{};
    int index = 0;
    for (int i = 0; i < kPsmCount; i++) {
        if (PsmLayout::Table()[i].psm == psm) { index = i; break; }
    }
    m.d = &PsmLayout::Table()[index];
    m.bpp = Swizzle::LinearBitsPerPixel(psm);
    m.pixel_mask = Swizzle::ElementWriteMask(psm);
    m.shift = m.d->shares_ct32_word ? m.d->index_shift : 0u;
    m.per_block = PsmLayout::ElementsPerBlock(psm);
    m.per_page = PsmLayout::ElementsPerPage(psm);
    m.vram_elements = uint64_t(PsmLayout::kVramBytes) * 8u / m.d->storage_bits;
    m.lut = &Tables().lut[index][0][0];
    return m;
}

// Walks the rect in block-aligned runs: `fn(e, i, j)` receives the wrapped
// element index of rect pixel (i, j). One BlockInPage lookup per run, no
// per-pixel division (section 8.4 steps 1-5 hoisted per run, step 6 per pixel).
template <typename Fn>
void WalkRuns(const Mode& m, const Swizzle::Buffer& buf, const Swizzle::Rect& r, Fn&& fn) {
    const PsmDescriptor& d = *m.d;
    const uint64_t ppr = PsmLayout::PagesPerRow(d.psm, buf.bw);
    const uint64_t base_bp = uint64_t(buf.bp) * m.per_block;
    for (uint32_t j = 0; j < r.h; j++) {
        const uint32_t y = r.y + j;
        const uint32_t ly = y % d.page_h;
        const uint32_t block_y = ly / d.block_h;
        const uint32_t in_block_y = ly % d.block_h;
        const uint16_t* lut_row = m.lut + in_block_y * 32u;
        const uint64_t row_base = base_bp + uint64_t(y / d.page_h) * ppr * m.per_page;
        uint32_t i = 0;
        while (i < r.w) {
            const uint32_t x = r.x + i;
            const uint32_t lx = x % d.page_w;
            const uint32_t in_block_x = lx % d.block_w;
            uint32_t run = d.block_w - in_block_x;
            if (run > r.w - i) run = r.w - i;
            const uint32_t block = PsmLayout::BlockInPage(d.psm, lx / d.block_w, block_y);
            uint64_t base = row_base + uint64_t(x / d.page_w) * m.per_page + uint64_t(block) * m.per_block;
            base %= m.vram_elements;
            for (uint32_t k = 0; k < run; k++) {
                uint64_t e = base + lut_row[in_block_x + k];
                if (e >= m.vram_elements) e -= m.vram_elements;  // one block never exceeds the 4 MB
                fn(static_cast<uint32_t>(e), i + k, j);
            }
            i += run;
        }
    }
}

}  // namespace

uint32_t Swizzle::LinearBitsPerPixel(Psm psm) {
    const PsmDescriptor& d = PsmLayout::Describe(psm);
    return d.is_indexed ? d.index_bits : d.storage_bits;
}

size_t Swizzle::MinPitch(Psm psm, uint32_t w) {
    return (static_cast<size_t>(w) * LinearBitsPerPixel(psm) + 7u) / 8u;
}

uint32_t Swizzle::ElementWriteMask(Psm psm) {
    const PsmDescriptor& d = PsmLayout::Describe(psm);
    if (d.shares_ct32_word) return ((1u << d.index_bits) - 1u) << d.index_shift;
    switch (psm) {
        case Psm::CT24: case Psm::Z24: return 0x00FFFFFFu;
        default: break;
    }
    return d.storage_bits == 32 ? 0xFFFFFFFFu : (1u << d.storage_bits) - 1u;
}

void Swizzle::LinearToVram(uint8_t* vram, const Buffer& buf, const Rect& rect,
                           const uint8_t* src, size_t src_pitch, uint32_t mask) {
    const Mode m = Hoist(buf.psm);
    const uint32_t bits = m.d->storage_bits;
    const uint32_t write_mask = m.pixel_mask & mask;
    const uint32_t shift = m.shift;
    const uint32_t bpp = m.bpp;
    WalkRuns(m, buf, rect, [&](uint32_t e, uint32_t i, uint32_t j) {
        WriteElement(vram, bits, e, GetLinear(src, src_pitch, bpp, i, j) << shift, write_mask);
    });
}

void Swizzle::VramToLinear(const uint8_t* vram, const Buffer& buf, const Rect& rect,
                           uint8_t* dst, size_t dst_pitch) {
    const Mode m = Hoist(buf.psm);
    const uint32_t bits = m.d->storage_bits;
    const uint32_t pixel_mask = m.pixel_mask;
    const uint32_t shift = m.shift;
    const uint32_t bpp = m.bpp;
    WalkRuns(m, buf, rect, [&](uint32_t e, uint32_t i, uint32_t j) {
        PutLinear(dst, dst_pitch, bpp, i, j, (ReadElement(vram, bits, e) & pixel_mask) >> shift);
    });
}

void Swizzle::LinearToVramGeneric(uint8_t* vram, const Buffer& buf, const Rect& rect,
                                  const uint8_t* src, size_t src_pitch, uint32_t mask) {
    const PsmDescriptor& d = PsmLayout::Describe(buf.psm);
    const uint32_t bpp = LinearBitsPerPixel(buf.psm);
    const uint32_t write_mask = ElementWriteMask(buf.psm) & mask;
    const uint32_t shift = d.shares_ct32_word ? d.index_shift : 0u;
    for (uint32_t j = 0; j < rect.h; j++)
        for (uint32_t i = 0; i < rect.w; i++) {
            const uint32_t e = PsmLayout::PixelAddress(buf.psm, rect.x + i, rect.y + j, buf.bp, buf.bw);
            WriteElement(vram, d.storage_bits, e, GetLinear(src, src_pitch, bpp, i, j) << shift, write_mask);
        }
}

void Swizzle::VramToLinearGeneric(const uint8_t* vram, const Buffer& buf, const Rect& rect,
                                  uint8_t* dst, size_t dst_pitch) {
    const PsmDescriptor& d = PsmLayout::Describe(buf.psm);
    const uint32_t bpp = LinearBitsPerPixel(buf.psm);
    const uint32_t pixel_mask = ElementWriteMask(buf.psm);
    const uint32_t shift = d.shares_ct32_word ? d.index_shift : 0u;
    for (uint32_t j = 0; j < rect.h; j++)
        for (uint32_t i = 0; i < rect.w; i++) {
            const uint32_t e = PsmLayout::PixelAddress(buf.psm, rect.x + i, rect.y + j, buf.bp, buf.bw);
            PutLinear(dst, dst_pitch, bpp, i, j, (ReadElement(vram, d.storage_bits, e) & pixel_mask) >> shift);
        }
}

}  // namespace gs
}  // namespace ps2r
