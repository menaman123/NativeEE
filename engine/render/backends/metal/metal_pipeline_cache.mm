// render/backends/metal/metal_pipeline_cache.mm -- MetalPipelineCache implementation
// (Objective-C++, manual reference counting: every object stored is retained once).
#include "engine/render/backends/metal/metal_pipeline_cache.h"

#import <Metal/Metal.h>

#include <cstdio>

#include "engine/render/backends/metal/metal_shader_sources.h"

namespace ps2r {
namespace render {

namespace {

id<MTLFunction> Function(id<MTLLibrary> lib, const char* name) {
    id<MTLFunction> f = [lib newFunctionWithName:[NSString stringWithUTF8String:name]];
    if (!f) std::fprintf(stderr, "[metal] function %s not found in the library\n", name);
    return f;   // +1
}

MTLCompareFunction Compare(uint8_t c) {
    switch (c) {
        case 0: return MTLCompareFunctionNever;
        case 2: return MTLCompareFunctionGreaterEqual;
        case 3: return MTLCompareFunctionGreater;
        default: return MTLCompareFunctionAlways;
    }
}

}  // namespace

MetalPipelineCache::~MetalPipelineCache() { Destroy(); }

bool MetalPipelineCache::Build(void* device) {
    Destroy();
    @autoreleasepool {
        id<MTLDevice> dev = (id<MTLDevice>)device;
        MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
        // IEEE semantics: the register program floors / truncates exact integer products.
        opts.mathMode = MTLMathModeSafe;
        NSError* err = nil;
        NSString* src = [NSString stringWithUTF8String:MetalShaderSources::Library()];
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:opts error:&err];
        [opts release];
        if (!lib) {
            std::fprintf(stderr, "[metal] MSL compile failed: %s\n",
                         err ? [[err localizedDescription] UTF8String] : "(no log)");
            return false;
        }
        device_ = (void*)[dev retain];
        library_ = (void*)lib;   // +1 from newLibraryWithSource
    }
    return true;
}

void MetalPipelineCache::Destroy() {
    for (auto& kv : pipelines_) [(id)kv.second release];
    for (auto& kv : depth_states_) [(id)kv.second release];
    for (auto& kv : samplers_) [(id)kv.second release];
    pipelines_.clear();
    depth_states_.clear();
    samplers_.clear();
    if (library_) { [(id)library_ release]; library_ = nullptr; }
    if (device_) { [(id)device_ release]; device_ = nullptr; }
}

void* MetalPipelineCache::Pipeline(MetalProgram program, bool color, bool depth) {
    const uint32_t key = static_cast<uint32_t>(program) | (color ? 0x100u : 0u) | (depth ? 0x200u : 0u);
    auto it = pipelines_.find(key);
    if (it != pipelines_.end()) return it->second;
    if (!library_) return nullptr;
    void* result = nullptr;
    @autoreleasepool {
        id<MTLLibrary> lib = (id<MTLLibrary>)library_;
        id<MTLDevice> dev = (id<MTLDevice>)device_;
        const char* vs = MetalShaderSources::kFullscreenVertex;
        const char* fs = nullptr;
        switch (program) {
            case MetalProgram::Ps2: vs = MetalShaderSources::kPs2Vertex; fs = MetalShaderSources::kPs2Fragment; break;
            case MetalProgram::Scanout: vs = MetalShaderSources::kScanoutVertex; fs = MetalShaderSources::kScanoutFragment; break;
            case MetalProgram::Pack5551: fs = MetalShaderSources::kPack5551Fragment; break;
            case MetalProgram::AlphaLane: fs = MetalShaderSources::kAlphaLaneFragment; break;
            case MetalProgram::Halfword: vs = MetalShaderSources::kPs2Vertex; fs = MetalShaderSources::kHalfwordFragment; break;
            case MetalProgram::Const: fs = MetalShaderSources::kConstFragment; break;
            default: return nullptr;
        }
        id<MTLFunction> vf = Function(lib, vs);
        id<MTLFunction> ff = Function(lib, fs);
        if (!vf || !ff) { [vf release]; [ff release]; return nullptr; }
        MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = vf;
        d.fragmentFunction = ff;
        if (color) {
            d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
            d.colorAttachments[0].blendingEnabled = NO;   // the program blends (framebuffer fetch)
            d.colorAttachments[0].writeMask = MTLColorWriteMaskAll;
        }
        if (depth) d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        NSError* err = nil;
        id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:d error:&err];
        [d release];
        [vf release];
        [ff release];
        if (!ps) {
            std::fprintf(stderr, "[metal] pipeline %d (color %d depth %d) failed: %s\n", static_cast<int>(program), color, depth,
                         err ? [[err localizedDescription] UTF8String] : "(no log)");
            return nullptr;
        }
        result = (void*)ps;   // +1
    }
    pipelines_[key] = result;
    return result;
}

void* MetalPipelineCache::DepthStencil(const MetalDepthKey& key) {
    const uint32_t k = key.Packed();
    auto it = depth_states_.find(k);
    if (it != depth_states_.end()) return it->second;
    void* result = nullptr;
    @autoreleasepool {
        MTLDepthStencilDescriptor* d = [[MTLDepthStencilDescriptor alloc] init];
        d.depthCompareFunction = Compare(key.compare);
        d.depthWriteEnabled = key.write ? YES : NO;
        id<MTLDepthStencilState> s = [(id<MTLDevice>)device_ newDepthStencilStateWithDescriptor:d];
        [d release];
        result = (void*)s;   // +1
    }
    depth_states_[k] = result;
    return result;
}

void* MetalPipelineCache::Sampler(const MetalSamplerKey& key) {
    const uint32_t k = key.Packed();
    auto it = samplers_.find(k);
    if (it != samplers_.end()) return it->second;
    void* result = nullptr;
    @autoreleasepool {
        MTLSamplerDescriptor* d = [[MTLSamplerDescriptor alloc] init];
        d.magFilter = key.mag_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.minFilter = key.min_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.mipFilter = key.mip == 0 ? MTLSamplerMipFilterNotMipmapped
                                   : (key.mip == 1 ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear);
        d.sAddressMode = key.wrap_s ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        d.tAddressMode = key.wrap_t ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        d.maxAnisotropy = key.anisotropy > 1 ? key.anisotropy : 1;
        d.normalizedCoordinates = YES;
        id<MTLSamplerState> s = [(id<MTLDevice>)device_ newSamplerStateWithDescriptor:d];
        [d release];
        result = (void*)s;   // +1
    }
    samplers_[k] = result;
    return result;
}

}  // namespace render
}  // namespace ps2r
