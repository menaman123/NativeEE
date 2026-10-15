// gs/hle/texsrc/owned_texture_slots.h -- OwnedTextureSlots: the texture slots an
// owner UPLOADED WITHOUT BYTES, served from the owner's converted image.
//
// The title re-uploads every texture it draws into one of its GS texture slots
// each frame (a host->local IMAGE transfer of the record's storage image, then
// TEX0/MIPTBP at the slot's offsets). An owner that converted the record at load
// hands this class the slot BIND instead -- in the stream position the transfer
// held: the slot base, the footprint the bytes would have covered, the TEX0 tuple
// the title reads it with (per-level offset/width, the CLUT offset, PSM, log2
// size) and the converted top-first RGBA8 levels (GPU alpha), pinned for as long
// as the bind lives. A textured read whose register tuple names EXACTLY what the
// bind describes (base + level offset, buffer width, PSM, size, CLUT base in
// CPSM CT32 / CSM1 / CSA 0, every authored mip level the sampler captures) is
// served the image's surface: the same RGBA8 texels the decode fallback would
// have read from the VRAM mirror had the bytes been transferred, built for the
// same mip-chain depth (MipSampler::Capture) with the same backend calls
// (level 0, authored levels 1..n, FinishMipChain with the tail synthesized). Any
// other read over the slot MISSES (counted; the caller falls through to its VRAM
// tiers, which hold no bytes for an owned slot -- the tripwire row).
//
// A bind is retired like the transferred bytes would be: a later upload or GPU
// draw landing over its footprint evicts it (EvictRange), a rebind of the base
// replaces it, Release(key) drops every bind and surface of one image (the
// owner's TextureDestroy). Surfaces are cached per (image key, chain depth) and
// bounded (LRU over images no bind references, kMaxSurfaceBytes of w*h*4).
// Consumer thread only (the backend's).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/gs/hle/ir/tex_read_view.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class OwnedTextureSlots {
public:
    static constexpr uint32_t kMaxLevels = 7;
    static constexpr uint64_t kMaxSurfaceBytes = 256ull * 1024u * 1024u;   // of w*h*4, unbound images

    struct Level {
        uint32_t w = 0, h = 0;           // the authored (declared) extent the title's tuple names
        const uint8_t* rgba = nullptr;   // pw*ph*4, top row first, GPU alpha; owned by `pin`
        uint32_t pw = 0, ph = 0;         // the PIXEL extent of rgba (0 = w, h); a texture pack's
                                         // replacement at another resolution -- the sampler still
                                         // divides by the declared size (TextureSource uv_tex_w/h)
    };
    struct Texture {
        uint64_t key = 0;                  // the owner's immutable image identity
        uint32_t psm = 0;                  // TEX0.PSM the reads carry (0x00 CT32, 0x13 T8)
        uint32_t tw = 0, th = 0;           // log2 level-0 size
        uint8_t levels = 0;                // authored levels, 1..kMaxLevels
        uint32_t level_offset[kMaxLevels] = {};   // bytes from the slot base
        uint32_t level_tbw[kMaxLevels] = {};      // pixels
        uint32_t clut_offset = 0;          // bytes from the slot base (indexed only)
        uint32_t footprint = 0;            // bytes the title's transfer covers from the base
        std::vector<Level> rgba;           // top-first, `levels` entries
        std::shared_ptr<const void> pin;   // keeps every Level::rgba alive
    };
    enum class Miss : uint8_t { None, NoBind, Tuple, Clut, Mips, Size };
    struct Census {
        uint64_t binds = 0;         // Bind calls
        uint64_t rebinds = 0;       // a bind replacing a live bind at the same base
        uint64_t serves = 0;        // reads served from an owned image
        uint64_t misses = 0;        // reads over a bound slot the bind does not describe (tripwire)
        uint64_t miss_tuple = 0, miss_clut = 0, miss_mips = 0;
        uint64_t evicted_draw = 0;  // binds retired by a GPU draw over the footprint
        uint64_t evicted_cpu = 0;   // binds retired by an upload over the footprint
        uint64_t released = 0;      // binds retired by Release(key)
        uint64_t surfaces_built = 0;
        uint64_t surface_uploads = 0;   // w*h*4 bytes uploaded to the backend
        uint64_t surfaces_evicted = 0;  // by the byte budget
        uint64_t surfaces = 0;          // live
        uint64_t surface_bytes = 0;     // live, w*h*4
    };

    explicit OwnedTextureSlots(render::Backend& be);
    ~OwnedTextureSlots();
    OwnedTextureSlots(const OwnedTextureSlots&) = delete;
    OwnedTextureSlots& operator=(const OwnedTextureSlots&) = delete;

    // The owner's upload of `t` into the slot at `base` landed here in stream order.
    void Bind(uint32_t base, Texture&& t);
    // Every bind and surface of the image `key` (the owner destroyed it).
    void Release(uint64_t key);
    // A GPU draw (`draw` = true) or an EE upload landed over [start, start+size):
    // every bind whose footprint overlaps is retired.
    void EvictRange(uint32_t start, uint32_t size, bool draw);
    // The resolver's query: true = `out` carries the owned surface (Decoded) and
    // the sampler's mip fields; false = no bind at the read, or a MISS (counted).
    bool Serve(const TexReadView& v, TextureSource* out);
    Miss LastMiss() const { return last_miss_; }
    // Whether some bind covers `tbp` (a miss diagnosis helper).
    bool Covers(uint32_t tbp) const;
    void Reset();
    const Census& Stats() const { return census_; }
    size_t BoundCount() const { return bound_.size(); }

private:
    struct Image {
        Texture texture;
        render::SurfaceId surface[kMaxLevels];   // by chain depth (authored levels past 0)
        uint64_t bytes = 0;                       // w*h*4 over the built surfaces
        uint64_t last_use = 0;
        uint32_t bound = 0;                       // live binds referencing this image
    };
    struct Slot {
        uint64_t key = 0;
        uint32_t footprint = 0;
    };
    Image* Find(uint64_t key);
    render::SurfaceId Surface(Image& img, uint8_t chain);
    void DropImage(uint64_t key);
    void Unbind(std::map<uint32_t, Slot>::iterator it);
    void EnforceBudget();

    render::Backend& be_;
    std::map<uint32_t, Slot> bound_;                 // slot base -> bind
    std::unordered_map<uint64_t, Image> images_;     // image key -> surfaces
    uint64_t use_clock_ = 0;
    Miss last_miss_ = Miss::None;
    Census census_;
};

}  // namespace gs
}  // namespace ps2r
