// tests/render/test_texture_filter.cpp -- Render_TextureFilter: the A2 texture-filtering
// enhancement's pure mapping (GlDrawExecutor::EnhanceFilters over SelectFilters' title
// choice) and the lock-free selection (TextureFilterSelection).
//
//   * `title` with anisotropy 1 changes NOTHING for every TEX1 (MMAG x MMIN x chain) --
//     the deterministic lanes' identity -- and never asks for the hardware LOD;
//   * `point` / `bilinear` force nearest / linear, mip-aware exactly when the title sampled
//     the chain (or the hardware LOD is in force), and keep the retail LOD otherwise;
//   * `trilinear` forces linear with linear level blending over a chain (hardware LOD),
//     plain linear without one;
//   * anisotropy snaps to 1|2|4|8|16, is clamped to the driver limit, and engages the
//     hardware LOD only over a chain;
//   * the selection's names / order match video.texture_filter's spelling.
#include <cstdint>
#include <cstdio>
#include <string>

#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

#include "engine/render/backends/gl/gl_draw_executor.h"
#include "engine/render/texture_filter_selection.h"

using ps2r::render::GlDrawExecutor;
using ps2r::render::TextureFilter;
using ps2r::render::TextureFilterSelection;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

int main() {
    // Identity: `title` / anisotropy 1 leaves SelectFilters' choice as it is, for every TEX1.
    for (int mag_linear = 0; mag_linear < 2; ++mag_linear)
        for (uint8_t mmin = 0; mmin < 6; ++mmin)
            for (uint8_t levels = 0; levels < 4; ++levels)
                for (int title_mips = 0; title_mips < 2; ++title_mips) {
                    uint32_t mag = 0, min = 0;
                    GlDrawExecutor::SelectFilters(mag_linear != 0, mmin, levels, &mag, &min);
                    const uint32_t mag0 = mag, min0 = min;
                    int aniso = 0;
                    const bool hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Title, 1, 16, title_mips != 0,
                                                                   levels, &mag, &min, &aniso);
                    CHECK(!hw && aniso == 1 && mag == mag0 && min == min0, "title/1 is the identity");
                }
    // Point / bilinear: forced, mip-aware only when the title sampled the chain.
    uint32_t mag = GL_LINEAR, min = GL_LINEAR_MIPMAP_NEAREST;
    int aniso = 0;
    bool hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Point, 1, 16, true, 3, &mag, &min, &aniso);
    CHECK(!hw && mag == GL_NEAREST && min == GL_NEAREST_MIPMAP_NEAREST, "point over the title's chain: nearest, mip-nearest");
    mag = GL_LINEAR; min = GL_LINEAR;
    hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Point, 1, 16, false, 3, &mag, &min, &aniso);
    CHECK(!hw && mag == GL_NEAREST && min == GL_NEAREST, "point where the title sampled level 0 only: nearest, no chain");
    mag = GL_NEAREST; min = GL_NEAREST;
    hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Bilinear, 1, 16, false, 0, &mag, &min, &aniso);
    CHECK(!hw && mag == GL_LINEAR && min == GL_LINEAR, "bilinear without a chain: linear");
    mag = GL_NEAREST; min = GL_NEAREST_MIPMAP_LINEAR;
    hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Bilinear, 1, 16, true, 2, &mag, &min, &aniso);
    CHECK(!hw && mag == GL_LINEAR && min == GL_LINEAR_MIPMAP_NEAREST, "bilinear over the title's chain: linear, mip-nearest, retail LOD");
    // Trilinear: linear level blending over a chain with the hardware LOD; plain linear without one.
    mag = GL_NEAREST; min = GL_NEAREST;
    hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Trilinear, 1, 16, false, 4, &mag, &min, &aniso);
    CHECK(hw && mag == GL_LINEAR && min == GL_LINEAR_MIPMAP_LINEAR, "trilinear over a chain: mipmap-linear, hardware LOD");
    mag = GL_NEAREST; min = GL_NEAREST;
    hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Trilinear, 1, 16, false, 0, &mag, &min, &aniso);
    CHECK(!hw && mag == GL_LINEAR && min == GL_LINEAR, "trilinear without a chain: linear, retail path");
    // Anisotropy: snapped, clamped to the driver, hardware LOD only over a chain; the
    // title's filters stay (point over the chain becomes mip-aware under the hardware LOD).
    mag = GL_LINEAR; min = GL_LINEAR;
    hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Title, 16, 8, false, 2, &mag, &min, &aniso);
    CHECK(hw && aniso == 8 && mag == GL_LINEAR && min == GL_LINEAR, "title + 16x on an 8x driver: 8x, hardware LOD, filters kept");
    hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Title, 16, 16, false, 0, &mag, &min, &aniso);
    CHECK(!hw && aniso == 16, "anisotropy without a chain: no hardware LOD, the parameter still applies");
    hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Point, 4, 16, false, 3, &mag, &min, &aniso);
    CHECK(hw && aniso == 4 && mag == GL_NEAREST && min == GL_NEAREST_MIPMAP_NEAREST, "point + 4x over a chain: mip-aware nearest");
    hw = GlDrawExecutor::EnhanceFilters(TextureFilter::Bilinear, 3, 1, false, 3, &mag, &min, &aniso);
    CHECK(!hw && aniso == 1 && min == GL_LINEAR, "a driver without the extension: anisotropy 1, no hardware LOD");
    CHECK(TextureFilterSelection::ClampAnisotropy(0) == 1 && TextureFilterSelection::ClampAnisotropy(3) == 2 &&
          TextureFilterSelection::ClampAnisotropy(7) == 4 && TextureFilterSelection::ClampAnisotropy(9) == 8 &&
          TextureFilterSelection::ClampAnisotropy(64) == 16 && TextureFilterSelection::ClampAnisotropy(-5) == 1,
          "anisotropy snaps down to 1|2|4|8|16");
    // The selection: defaults, live changes, names.
    TextureFilterSelection sel;
    CHECK(sel.Filter() == TextureFilter::Title && sel.Anisotropy() == 1 && !sel.Enhances(), "default: the title's choice");
    sel.SetFilter(TextureFilter::Trilinear);
    CHECK(sel.Enhances() && sel.Filter() == TextureFilter::Trilinear, "SetFilter is live");
    sel.SetFilter(TextureFilter::Title);
    sel.SetAnisotropy(5);
    CHECK(sel.Enhances() && sel.Anisotropy() == 4, "SetAnisotropy snaps and is live");
    const TextureFilterSelection seeded(TextureFilter::Point, 16);
    CHECK(seeded.Filter() == TextureFilter::Point && seeded.Anisotropy() == 16, "seeded from Config");
    CHECK(std::string(TextureFilterSelection::Name(TextureFilter::Title)) == "title" &&
          std::string(TextureFilterSelection::Name(TextureFilter::Point)) == "point" &&
          std::string(TextureFilterSelection::Name(TextureFilter::Bilinear)) == "bilinear" &&
          std::string(TextureFilterSelection::Name(TextureFilter::Trilinear)) == "trilinear",
          "names == video.texture_filter's spelling");
    std::printf("test_texture_filter: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
