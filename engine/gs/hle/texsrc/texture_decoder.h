// gs/hle/texsrc/texture_decoder.h -- TextureDecoder: a TexReadView over a VRAM
// view -> the RGBA8 texels the GPU samples.
//
// The format layer (gs/format) does the addressing and the value rules; this
// class binds a draw's register view to it: level size from TW/TH, the palette
// read from CBP in CPSM/CSM (with the CLUT heal workaround applied when a cache is
// given) and TEXA; the alpha byte is stored as the GS stores it (0x80 = 1.0, no GPU
// rescale -- the ONE alpha convention of the pipeline). DecodeRect
// decodes a UV-bounded sub-rect and equals the crop of the full decode.
// DecodeScanoutBuffer reads a frame buffer (CT32 / CT24 / CT16) as the CRTC does,
// alpha forced opaque. ParseDispfb / ParseDisplay decode the privileged DISPFB
// and DISPLAY registers (GS User's Manual 4.4 / 4.5): DISPLAY's DW / DH are in
// VCK units and the magnification fields MAGH / MAGV give the pixel counts as
// (DW + 1) / (MAGH + 1) and (DH + 1) / (MAGV + 1).
#pragma once

#include <cstdint>
#include <vector>

#include "engine/gs/hle/ir/tex_read_view.h"
#include "engine/gs/hle/texsrc/clut_heal_cache.h"

namespace ps2r {
namespace gs {

class TextureDecoder {
public:
    struct Dispfb {
        uint32_t fbp_bytes = 0;
        uint32_t fbw_px = 0;
        uint32_t psm = 0;
        uint32_t dbx = 0, dby = 0;
    };
    struct Display {
        uint32_t dx = 0, dy = 0, magh = 0, magv = 0, dw = 0, dh = 0;
        int pixel_width = 0, pixel_height = 0;
    };

    // Full level-0 decode: out receives (1<<tw) * (1<<th) RGBA8 texels (GPU alpha).
    static void Decode(const uint8_t* vram, const TexReadView& v, ClutHealCache* heal,
                       const VramLayout* layout, std::vector<uint32_t>* out);
    // Sub-rect [x0, x0+w) x [y0, y0+h) of the level-0 texture; pitch w.
    static void DecodeRect(const uint8_t* vram, const TexReadView& v, int x0, int y0, int w, int h,
                           ClutHealCache* heal, const VramLayout* layout, std::vector<uint32_t>* out);
    // A mip level of the same view at (tbp_bytes, tbw_px, w x h).
    static void DecodeLevel(const uint8_t* vram, const TexReadView& v, uint32_t tbp_bytes,
                            uint32_t tbw_px, uint32_t w, uint32_t h, ClutHealCache* heal,
                            const VramLayout* layout, std::vector<uint32_t>* out);
    // The palette (256 / 16 entries, PS2 alpha) the view's CLUT holds; false for non-indexed.
    static bool ReadPalette(const uint8_t* vram, const TexReadView& v, ClutHealCache* heal,
                            const VramLayout* layout, uint32_t* out256);
    // Frame-buffer read for the display: w x h RGBA8, alpha 255. Returns false for a PSM
    // the CRTC cannot display.
    static bool DecodeScanoutBuffer(const uint8_t* vram, uint32_t fbp_bytes, uint32_t fbw_px,
                                    uint32_t psm, int w, int h, std::vector<uint32_t>* out);
    static Dispfb ParseDispfb(uint64_t v);
    static Display ParseDisplay(uint64_t v);
    // PS2 alpha byte -> GPU alpha byte (the one scaling site the decoder applies).
};

}  // namespace gs
}  // namespace ps2r
