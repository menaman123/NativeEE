// render/backends/metal/metal_pipeline_cache.h -- MetalPipelineCache: the compiled MSL
// library and the cached pipeline / depth-stencil / sampler state objects of the Metal
// backend.
//
// The library is compiled from MetalShaderSources at Build (fast math OFF: the register
// program's integer GS arithmetic relies on IEEE floor / trunc / divide). Render pipelines
// are keyed by program and attachment layout (every register-program pipeline has the
// RGBA8 colour attachment with blending OFF -- the program blends through the framebuffer
// fetch -- and a Depth32Float depth attachment, DepthStencil variants carry the stencil);
// depth-stencil states by (compare, write); samplers by (mag, min, mip, wrap s/t,
// anisotropy). Metal objects are held as opaque pointers (retained, manual reference
// counting in the .mm) so this header compiles in a plain C++ translation unit.
#pragma once

#include <cstdint>
#include <unordered_map>

namespace ps2r {
namespace render {

enum class MetalProgram : uint8_t {
    Ps2 = 0,        // the register program (ps2_vertex + ps2_fragment)
    Scanout,        // the display quad
    Pack5551,       // CopyRect(kCopyPack5551)
    AlphaLane,      // WriteAlphaLane
    Halfword,       // the half-word shuffle (ps2_vertex + halfword_fragment)
    Const,          // DrawConstQuad
    kCount
};

// Sampler selection (the GL arm's texture parameters, as a key).
struct MetalSamplerKey {
    uint8_t mag_linear = 0;
    uint8_t min_linear = 0;
    uint8_t mip = 0;          // 0 none, 1 nearest, 2 linear
    uint8_t wrap_s = 0;       // 0 clamp-to-edge, 1 repeat
    uint8_t wrap_t = 0;
    uint8_t anisotropy = 1;
    uint32_t Packed() const {
        return mag_linear | (min_linear << 1) | (mip << 2) | (wrap_s << 4) | (wrap_t << 5) |
               (static_cast<uint32_t>(anisotropy) << 8);
    }
};

// Depth compare: 0 Never, 1 Always, 2 GreaterEqual, 3 Greater (the GS ZTST order).
struct MetalDepthKey {
    uint8_t compare = 1;
    uint8_t write = 0;
    uint32_t Packed() const { return compare | (write << 8); }
};

class MetalPipelineCache {
public:
    MetalPipelineCache() = default;
    ~MetalPipelineCache();
    MetalPipelineCache(const MetalPipelineCache&) = delete;
    MetalPipelineCache& operator=(const MetalPipelineCache&) = delete;

    // Compiles the library on `device` (id<MTLDevice>); false with the compiler log on stderr.
    bool Build(void* device);
    void Destroy();
    bool Built() const { return library_ != nullptr; }

    // id<MTLRenderPipelineState> for the program over the attachment layout: `color` = an
    // RGBA8 colour attachment is present, `depth` = a Depth32Float depth attachment is present.
    // Null (and a log line) when the pipeline cannot be built.
    void* Pipeline(MetalProgram program, bool color, bool depth);
    // id<MTLDepthStencilState>.
    void* DepthStencil(const MetalDepthKey& key);
    // id<MTLSamplerState>.
    void* Sampler(const MetalSamplerKey& key);

private:
    void* device_ = nullptr;
    void* library_ = nullptr;
    std::unordered_map<uint32_t, void*> pipelines_;
    std::unordered_map<uint32_t, void*> depth_states_;
    std::unordered_map<uint32_t, void*> samplers_;
};

}  // namespace render
}  // namespace ps2r
