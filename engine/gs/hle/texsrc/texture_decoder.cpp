// gs/hle/texsrc/texture_decoder.cpp -- TextureDecoder.
#include "engine/gs/hle/texsrc/texture_decoder.h"

#include "engine/gs/format/block_decode.h"
#include "engine/gs/format/pixel_convert.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/texa_expand.h"

namespace ps2r {
namespace gs {

namespace {

bool ViewTexture(const TexReadView& v, uint32_t tbp_bytes, uint32_t tbw_px, uint32_t w, uint32_t h,
                 BlockDecode::Texture* t) {
    Psm psm;
    if (!PsmLayout::FromField(v.psm, &psm)) return false;
    t->psm = psm;
    t->tbp = tbp_bytes / PsmLayout::kBlockBytes;
    t->tbw = tbw_px / 64;
    t->w = w;
    t->h = h;
    return true;
}

TexaExpand::Texa ViewTexa(const TexReadView& v) {
    TexaExpand::Texa t;
    t.ta0 = v.texa_ta0;
    t.ta1 = v.texa_ta1;
    t.aem = v.texa_aem;
    return t;
}

void ScaleAlpha(std::vector<uint32_t>* out) {
    for (uint32_t& p : *out) p = TextureDecoder::GpuAlpha(p);
}

}  // namespace

uint32_t TextureDecoder::GpuAlpha(uint32_t rgba) {
    const uint8_t a = PixelConvert::ScaleAlphaPs2ToGpu(PixelConvert::A(rgba));
    return (rgba & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);
}

bool TextureDecoder::ReadPalette(const uint8_t* vram, const TexReadView& v, ClutHealCache* heal,
                                 const VramLayout* layout, uint32_t* out) {
    Psm psm;
    if (!PsmLayout::FromField(v.psm, &psm)) return false;
    const uint32_t entries = BlockDecode::PaletteEntries(psm);
    if (entries == 0) return false;
    BlockDecode::Palette pal;
    Psm cpsm = Psm::CT32;
    PsmLayout::FromField(v.cpsm, &cpsm);
    pal.cpsm = cpsm;
    pal.cbp = v.cbp / PsmLayout::kBlockBytes;
    pal.csm2 = v.csm != 0;
    BlockDecode::ReadPalette(vram, pal, entries, ViewTexa(v), out);
    if (heal && layout && entries == 256) heal->Heal(v.cbp, out, *layout);
    return true;
}

void TextureDecoder::DecodeLevel(const uint8_t* vram, const TexReadView& v, uint32_t tbp_bytes,
                                 uint32_t tbw_px, uint32_t w, uint32_t h, ClutHealCache* heal,
                                 const VramLayout* layout, std::vector<uint32_t>* out) {
    out->assign(static_cast<size_t>(w) * h, 0u);
    BlockDecode::Texture t;
    if (!ViewTexture(v, tbp_bytes, tbw_px, w, h, &t)) return;
    uint32_t palette[256];
    const bool indexed = ReadPalette(vram, v, heal, layout, palette);
    BlockDecode::Decode(vram, t, ViewTexa(v), indexed ? palette : nullptr, out->data());
    ScaleAlpha(out);
}

void TextureDecoder::Decode(const uint8_t* vram, const TexReadView& v, ClutHealCache* heal,
                            const VramLayout* layout, std::vector<uint32_t>* out) {
    DecodeLevel(vram, v, v.tbp, v.tbw, 1u << v.tw, 1u << v.th, heal, layout, out);
}

void TextureDecoder::DecodeRect(const uint8_t* vram, const TexReadView& v, int x0, int y0, int w,
                                int h, ClutHealCache* heal, const VramLayout* layout,
                                std::vector<uint32_t>* out) {
    out->assign(static_cast<size_t>(w > 0 ? w : 0) * (h > 0 ? h : 0), 0u);
    if (w <= 0 || h <= 0) return;
    BlockDecode::Texture t;
    if (!ViewTexture(v, v.tbp, v.tbw, 1u << v.tw, 1u << v.th, &t)) return;
    uint32_t palette[256];
    const bool indexed = ReadPalette(vram, v, heal, layout, palette);
    BlockDecode::Rect r;
    r.x = static_cast<uint32_t>(x0);
    r.y = static_cast<uint32_t>(y0);
    r.w = static_cast<uint32_t>(w);
    r.h = static_cast<uint32_t>(h);
    BlockDecode::Decode(vram, t, r, ViewTexa(v), indexed ? palette : nullptr, out->data());
    ScaleAlpha(out);
}

bool TextureDecoder::DecodeScanoutBuffer(const uint8_t* vram, uint32_t fbp_bytes, uint32_t fbw_px,
                                         uint32_t psm, int w, int h, std::vector<uint32_t>* out) {
    Psm p;
    if (!PsmLayout::FromField(psm, &p)) return false;
    if (p != Psm::CT32 && p != Psm::CT24 && p != Psm::CT16 && p != Psm::CT16S) return false;
    out->assign(static_cast<size_t>(w > 0 ? w : 0) * (h > 0 ? h : 0), 0u);
    if (w <= 0 || h <= 0) return true;
    BlockDecode::Texture t;
    t.psm = p;
    t.tbp = fbp_bytes / PsmLayout::kBlockBytes;
    t.tbw = fbw_px / 64;
    t.w = static_cast<uint32_t>(w);
    t.h = static_cast<uint32_t>(h);
    TexaExpand::Texa opaque;   // frame read: alpha is not part of the displayed pixel
    opaque.ta0 = 0x80;
    opaque.ta1 = 0x80;
    BlockDecode::Decode(vram, t, opaque, nullptr, out->data());
    for (uint32_t& px : *out) px |= 0xFF000000u;
    return true;
}

TextureDecoder::Dispfb TextureDecoder::ParseDispfb(uint64_t v) {
    Dispfb d;
    d.fbp_bytes = static_cast<uint32_t>(v & 0x1FF) * 8192u;
    d.fbw_px = static_cast<uint32_t>((v >> 9) & 0x3F) * 64u;
    d.psm = static_cast<uint32_t>((v >> 15) & 0x1F);
    d.dbx = static_cast<uint32_t>((v >> 32) & 0x7FF);
    d.dby = static_cast<uint32_t>((v >> 43) & 0x7FF);
    return d;
}

TextureDecoder::Display TextureDecoder::ParseDisplay(uint64_t v) {
    Display d;
    d.dx = static_cast<uint32_t>(v & 0xFFF);
    d.dy = static_cast<uint32_t>((v >> 12) & 0x7FF);
    d.magh = static_cast<uint32_t>((v >> 23) & 0xF);
    d.magv = static_cast<uint32_t>((v >> 27) & 0x3);
    d.dw = static_cast<uint32_t>((v >> 32) & 0xFFF);
    d.dh = static_cast<uint32_t>((v >> 44) & 0x7FF);
    d.pixel_width = static_cast<int>((d.dw + 1) / (d.magh + 1));
    d.pixel_height = static_cast<int>((d.dh + 1) / (d.magv + 1));
    return d;
}

}  // namespace gs
}  // namespace ps2r
