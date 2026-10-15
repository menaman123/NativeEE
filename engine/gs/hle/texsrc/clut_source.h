// gs/hle/texsrc/clut_source.h -- ClutSource: the 256 x 2 RGBA8 palette texture an
// indexed render-target serve samples.
//
// The shader reads the index out of the target's own texels and looks the colour
// up in this texture; the palette is built from the 1 KiB CLUT image at CBP as
// the GS stores it (PSMCT32, buffer width 1, the CSM1 16x16 arrangement -- GS
// User's Manual 3.4.7 / 2.7.3). Row 0 holds the entries with alpha prescaled to
// the GPU domain (0x80 -> 0xFF, else a * 2), row 1 the raw PS2 bytes (the raw
// row serves the self-read HIGHLIGHT2 case). A PSMT8H read applies the CSM1
// reorder twice (an involution, so it is the identity); a T8-of-CT16 read folds
// it once. Entries are keyed by (cbp, fold) and rebuilt only when the CLUT bytes'
// hash changes; the bytes always come from the composed VRAM view.
#pragma once

#include <cstdint>
#include <unordered_map>

#include "engine/gs/vram/vram_image.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class ClutSource {
public:
    static constexpr uint32_t kClutBytes = 0x400;   // 256 CT32 entries
    explicit ClutSource(render::Backend& be);
    ~ClutSource();
    // The palette texture for the CLUT at `cbp` (bytes); 0 when it cannot be built.
    render::SurfaceId Ensure(uint32_t cbp, bool fold_single, const VramImage& vram);
    // CPU reference: the 256 x 2 RGBA8 image (2048 bytes) Ensure uploads.
    static void Build(const uint8_t* clut_bytes, bool fold_single, uint8_t* out_rgba_2rows);
    static uint8_t PrescaleAlpha(uint8_t a_ps2) { return a_ps2 >= 128 ? 255 : static_cast<uint8_t>(a_ps2 * 2); }
    size_t Count() const { return entries_.size(); }
    void DropAll();

private:
    struct Entry {
        render::SurfaceId tex;
        uint64_t hash = 0;
    };
    render::Backend& be_;
    std::unordered_map<uint32_t, Entry> entries_;   // key = cbp | (fold ? 0x80000000 : 0)
};

}  // namespace gs
}  // namespace ps2r
