// render/texture_filter_selection.h -- TextureFilterSelection: the player's
// texture-filtering enhancement over the OWNED textures (video.texture_filter +
// video.anisotropy; Charter 2 A2).
//
// The title chooses its own filter per draw (TEX1.MMAG / MMIN: point or bilinear,
// mip-nearest where it authored a chain): that choice is the BASELINE and the
// `title` mode leaves every sampler at it -- the deterministic lanes' identity.
// The other modes are enhancements the backend applies to textures the engine
// owns at load (TextureSource::Owner::owned): `point` forces nearest, `bilinear`
// forces linear, `trilinear` forces linear with linear level blending over the
// owned mip chain (authored levels + the synthesized tail), and anisotropy > 1
// adds anisotropic sampling. Render-target reads, depth / indexed serves and the
// VRAM-decoded model textures are never touched. Lock-free (one atomic per
// field): the session's main thread writes from the in-game Display page, the
// GL owner reads per draw. This header includes no Config and no GL.
#pragma once

#include <atomic>
#include <cstdint>

namespace ps2r {
namespace render {

// video.texture_filter's order (title|point|bilinear|trilinear); the composition
// root maps the Config enum onto this one.
enum class TextureFilter : uint8_t { Title = 0, Point, Bilinear, Trilinear };

class TextureFilterSelection {
public:
    static constexpr int kMaxAnisotropy = 16;

    explicit TextureFilterSelection(TextureFilter filter = TextureFilter::Title, int anisotropy = 1)
        : filter_(filter), anisotropy_(static_cast<uint8_t>(ClampAnisotropy(anisotropy))) {}

    TextureFilter Filter() const { return filter_.load(std::memory_order_relaxed); }
    int Anisotropy() const { return anisotropy_.load(std::memory_order_relaxed); }
    // A live change (the in-game Display page): the next draw samples with it.
    void SetFilter(TextureFilter filter) { filter_.store(filter, std::memory_order_relaxed); }
    void SetAnisotropy(int anisotropy) {
        anisotropy_.store(static_cast<uint8_t>(ClampAnisotropy(anisotropy)), std::memory_order_relaxed);
    }
    // Whether the selection changes any sampler at all (false = the title's choice everywhere).
    bool Enhances() const { return Filter() != TextureFilter::Title || Anisotropy() > 1; }

    // The admitted anisotropy values are the powers of two 1..16; anything else
    // snaps DOWN to the nearest admitted value (0 and negatives to 1).
    static int ClampAnisotropy(int requested) {
        return requested >= 16 ? 16 : requested >= 8 ? 8 : requested >= 4 ? 4 : requested >= 2 ? 2 : 1;
    }
    // video.texture_filter's spelling of a mode (an out-of-range value reads "title").
    static const char* Name(TextureFilter filter) {
        return static_cast<uint8_t>(filter) < 4 ? kNames[static_cast<uint8_t>(filter)] : kNames[0];
    }

private:
    static constexpr const char* kNames[4] = {"title", "point", "bilinear", "trilinear"};
    std::atomic<TextureFilter> filter_;
    std::atomic<uint8_t> anisotropy_;
};

}  // namespace render
}  // namespace ps2r
