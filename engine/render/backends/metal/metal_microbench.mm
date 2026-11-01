// render/backends/metal/metal_microbench.mm -- the GPU ROOFLINE microbenchmark (Metal, Apple GPUs).
//
// Measures on THIS device the peaks the GS -> Metal renderer's per-pass rates are judged against
// (PERF1 roofline): colour fill rate into Rgba8 at the scale-4 surface (2048 x 1792 = 512 x 448
// x 4^2), depth fill into Depth32f, alpha-blended fill, texture sample rate (nearest and
// linear, a 1024^2 Rgba8 source), fragment ALU rate (64 FMAs per pixel), framebuffer-fetch
// read-modify-write rate (the register program's blend path), host -> GPU upload bandwidth
// (replaceRegion into a shared texture; the GS texture upload path), GPU -> host readback
// bandwidth (getBytes after completion; the frames-lane grab / the game's readbacks), the
// command-buffer round trip (commit -> completed; the fence floor) and the render-pass switch
// cost (N encoders of one draw each: the price of a target change per pass).
//
// GPU time comes from the command buffer's GPUStartTime / GPUEndTime; each row is the best of
// --reps (a roofline is the peak). Standalone (no engine objects): any thread, no window, so
// present / vsync latency is NOT here (it needs a CAMetalLayer on screen; the play lane's
// run.meta present_latency rows are that measurement). Rows print as `key=value`.
//
//   metal_microbench [--reps N] [--quick] [--scale N]
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char* kShaders = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float2 uv; };
vertex V vs_fullscreen(uint vid [[vertex_id]], constant float& depth [[buffer(0)]]) {
    // one triangle covering the clip square; uv in [0,1]
    float2 p = float2((vid == 1) ? 3.0 : -1.0, (vid == 2) ? 3.0 : -1.0);
    V o; o.pos = float4(p, depth, 1.0); o.uv = p * 0.5 + 0.5; return o;
}
fragment float4 fs_solid(V in [[stage_in]], constant float4& c [[buffer(0)]]) { return c; }
fragment float4 fs_tex(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]],
                       constant float4& c [[buffer(0)]]) {
    return t.sample(s, in.uv * c.x) * c.y;
}
fragment float4 fs_alu(V in [[stage_in]], constant float4& c [[buffer(0)]]) {
    float4 a = float4(in.uv, c.x, c.y); float4 b = a * 0.5 + 0.25;
    for (int i = 0; i < 16; ++i) { a = fma(a, b, c); b = fma(b, a, c); a = fma(a, c, b); b = fma(b, c, a); }
    return a + b;
}
struct FbIn { float4 dst [[color(0)]]; };
fragment float4 fs_fbfetch(V in [[stage_in]], FbIn fb, constant float4& c [[buffer(0)]]) {
    // the register program's shape: read the destination, blend, write
    return fb.dst * (1.0 - c.w) + c * c.w;
}
)MSL";

struct Bench {
    id<MTLDevice> dev;
    id<MTLCommandQueue> q;
    id<MTLLibrary> lib;
    uint32_t w, h;
    int reps;
    bool quick;

    id<MTLTexture> MakeTex(MTLPixelFormat f, uint32_t tw, uint32_t th, MTLTextureUsage usage, MTLStorageMode st) {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:tw height:th mipmapped:NO];
        d.usage = usage; d.storageMode = st;
        return [dev newTextureWithDescriptor:d];
    }

    // blend: 0 = opaque (a TBDR's hidden-surface removal may discard the overdrawn fragments: the
    // row is then the HSR rate, not the shading rate), 1 = src-alpha, 2 = additive one/one (every
    // fragment shaded and merged: the honest fill/shade rate).
    id<MTLRenderPipelineState> Pipe(const char* fs, MTLPixelFormat colour, MTLPixelFormat depth, int blend) {
        MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
        d.vertexFunction = [lib newFunctionWithName:@"vs_fullscreen"];
        d.fragmentFunction = [lib newFunctionWithName:[NSString stringWithUTF8String:fs]];
        if (colour != MTLPixelFormatInvalid) {
            d.colorAttachments[0].pixelFormat = colour;
            d.colorAttachments[0].blendingEnabled = blend != 0;
            if (blend == 1) {
                d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
                d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
            } else if (blend == 2) {
                d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
                d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
                d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
                d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
            }
        }
        if (depth != MTLPixelFormatInvalid) d.depthAttachmentPixelFormat = depth;
        NSError* err = nil;
        id<MTLRenderPipelineState> p = [dev newRenderPipelineStateWithDescriptor:d error:&err];
        if (!p) { std::fprintf(stderr, "pipeline %s: %s\n", fs, err.localizedDescription.UTF8String); std::exit(1); }
        return p;
    }

    // GPU seconds of one command buffer, best of reps, with `encode` filling it.
    template <class F>
    double GpuSeconds(F encode) {
        double best = 1e9;
        for (int r = 0; r < reps; ++r) {
            id<MTLCommandBuffer> cb = [q commandBuffer];
            encode(cb);
            [cb commit];
            [cb waitUntilCompleted];
            const double s = cb.GPUEndTime - cb.GPUStartTime;
            if (s > 0) best = std::min(best, s);
        }
        return best;
    }

    // N full-screen draws into (colour, depth) with the given pipeline; returns pixels/s.
    double FillRate(id<MTLRenderPipelineState> p, id<MTLTexture> colour, id<MTLTexture> depth, id<MTLTexture> tex,
                    bool depth_write, uint32_t draws, float* consts, uint32_t cw, uint32_t ch) {
        id<MTLDepthStencilState> ds = nil;
        if (depth) {
            MTLDepthStencilDescriptor* dd = [[MTLDepthStencilDescriptor alloc] init];
            dd.depthCompareFunction = MTLCompareFunctionAlways; dd.depthWriteEnabled = depth_write;
            ds = [dev newDepthStencilStateWithDescriptor:dd];
        }
        MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter = consts[2] > 0 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        sd.magFilter = sd.minFilter;
        sd.sAddressMode = MTLSamplerAddressModeRepeat; sd.tAddressMode = MTLSamplerAddressModeRepeat;
        id<MTLSamplerState> samp = [dev newSamplerStateWithDescriptor:sd];
        const double s = GpuSeconds([&](id<MTLCommandBuffer> cb) {
            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            if (colour) { rp.colorAttachments[0].texture = colour; rp.colorAttachments[0].loadAction = MTLLoadActionLoad; rp.colorAttachments[0].storeAction = MTLStoreActionStore; }
            if (depth) { rp.depthAttachment.texture = depth; rp.depthAttachment.loadAction = MTLLoadActionLoad; rp.depthAttachment.storeAction = MTLStoreActionStore; }
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            [enc setRenderPipelineState:p];
            if (ds) [enc setDepthStencilState:ds];
            float d = 0.5f;
            [enc setVertexBytes:&d length:sizeof d atIndex:0];
            [enc setFragmentBytes:consts length:16 atIndex:0];
            if (tex) { [enc setFragmentTexture:tex atIndex:0]; [enc setFragmentSamplerState:samp atIndex:0]; }
            for (uint32_t i = 0; i < draws; ++i) {
                float cc[4] = {consts[0], consts[1], consts[2], consts[3] * (1.0f - 0.001f * float(i))};
                [enc setFragmentBytes:cc length:16 atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            }
            [enc endEncoding];
        });
        return double(cw) * double(ch) * double(draws) / s;
    }
};

}  // namespace

int main(int argc, char** argv) {
    int reps = 5, scale = 4; bool quick = false;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--reps" && i + 1 < argc) reps = std::atoi(argv[++i]);
        else if (s == "--scale" && i + 1 < argc) scale = std::atoi(argv[++i]);
        else if (s == "--quick") quick = true;
        else { std::fprintf(stderr, "usage: metal_microbench [--reps N] [--quick] [--scale N]\n"); return 2; }
    }
    @autoreleasepool {
        Bench b;
        b.dev = MTLCreateSystemDefaultDevice();
        if (!b.dev) { std::fprintf(stderr, "no Metal device\n"); return 77; }
        b.q = [b.dev newCommandQueue];
        NSError* err = nil;
        b.lib = [b.dev newLibraryWithSource:[NSString stringWithUTF8String:kShaders] options:nil error:&err];
        if (!b.lib) { std::fprintf(stderr, "shaders: %s\n", err.localizedDescription.UTF8String); return 1; }
        b.w = 512u * uint32_t(scale); b.h = 448u * uint32_t(scale); b.reps = reps; b.quick = quick;
        const uint32_t draws = quick ? 16 : 64;
        std::printf("metal_microbench device=%s scale=%d surface=%ux%u reps=%d draws_per_row=%u\n",
                    b.dev.name.UTF8String, scale, b.w, b.h, reps, draws);
        const MTLTextureUsage rt = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        id<MTLTexture> colour = b.MakeTex(MTLPixelFormatRGBA8Unorm, b.w, b.h, rt, MTLStorageModeShared);
        id<MTLTexture> colour_priv = b.MakeTex(MTLPixelFormatRGBA8Unorm, b.w, b.h, rt, MTLStorageModePrivate);
        id<MTLTexture> depth = b.MakeTex(MTLPixelFormatDepth32Float, b.w, b.h, rt, MTLStorageModePrivate);
        id<MTLTexture> src = b.MakeTex(MTLPixelFormatRGBA8Unorm, 1024, 1024, MTLTextureUsageShaderRead, MTLStorageModeShared);
        {
            std::vector<uint32_t> px(1024 * 1024);
            for (size_t i = 0; i < px.size(); ++i) px[i] = uint32_t(i * 2654435761u);
            [src replaceRegion:MTLRegionMake2D(0, 0, 1024, 1024) mipmapLevel:0 withBytes:px.data() bytesPerRow:4096];
        }
        float c_solid[4] = {0.5f, 0.25f, 0.0f, 1.0f};
        float c_tex_nearest[4] = {1.0f, 1.0f, 0.0f, 1.0f};   // uv scale 1, gain 1, nearest
        float c_tex_linear[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float c_tex_mag[4] = {0.125f, 1.0f, 1.0f, 1.0f};     // 8x magnified (the scale-4 texel reuse case)
        float c_blend[4] = {0.5f, 0.25f, 0.0f, 0.5f};

        const double gpix = 1e-9;
        // fill. Opaque overdraw first (the TBDR hidden-surface-removal rate: what an opaque pass that is
        // overdrawn costs), then additive (every fragment shaded + merged in tile memory: the shade rate).
        auto p_opaque = b.Pipe("fs_solid", MTLPixelFormatRGBA8Unorm, MTLPixelFormatInvalid, 0);
        std::printf("gpu_fill_rgba8_opaque_overdraw_hsr_gpix_s=%.2f\n", b.FillRate(p_opaque, colour, nil, nil, false, draws, c_solid, b.w, b.h) * gpix);
        auto p_solid = b.Pipe("fs_solid", MTLPixelFormatRGBA8Unorm, MTLPixelFormatInvalid, 2);
        std::printf("gpu_fill_rgba8_shared_gpix_s=%.2f\n", b.FillRate(p_solid, colour, nil, nil, false, draws, c_solid, b.w, b.h) * gpix);
        std::printf("gpu_fill_rgba8_private_gpix_s=%.2f\n", b.FillRate(p_solid, colour_priv, nil, nil, false, draws, c_solid, b.w, b.h) * gpix);
        auto p_solid_d = b.Pipe("fs_solid", MTLPixelFormatRGBA8Unorm, MTLPixelFormatDepth32Float, 2);
        std::printf("gpu_fill_rgba8_depth32f_write_gpix_s=%.2f\n", b.FillRate(p_solid_d, colour, depth, nil, true, draws, c_solid, b.w, b.h) * gpix);
        std::printf("gpu_fill_rgba8_depth32f_test_only_gpix_s=%.2f\n", b.FillRate(p_solid_d, colour, depth, nil, false, draws, c_solid, b.w, b.h) * gpix);
        auto p_depth_only = b.Pipe("fs_solid", MTLPixelFormatInvalid, MTLPixelFormatDepth32Float, 0);
        std::printf("gpu_fill_depth32f_only_write_gpix_s=%.2f\n", b.FillRate(p_depth_only, nil, depth, nil, true, draws, c_solid, b.w, b.h) * gpix);
        auto p_blend = b.Pipe("fs_solid", MTLPixelFormatRGBA8Unorm, MTLPixelFormatInvalid, 1);
        std::printf("gpu_blend_rgba8_srcalpha_gpix_s=%.2f\n", b.FillRate(p_blend, colour, nil, nil, false, draws, c_blend, b.w, b.h) * gpix);
        auto p_fb = b.Pipe("fs_fbfetch", MTLPixelFormatRGBA8Unorm, MTLPixelFormatInvalid, 0);
        std::printf("gpu_fbfetch_rmw_rgba8_gpix_s=%.2f\n", b.FillRate(p_fb, colour, nil, nil, false, draws, c_blend, b.w, b.h) * gpix);
        auto p_tex = b.Pipe("fs_tex", MTLPixelFormatRGBA8Unorm, MTLPixelFormatInvalid, 2);
        std::printf("gpu_tex_nearest_1024sq_gsamples_s=%.2f\n", b.FillRate(p_tex, colour, nil, src, false, draws, c_tex_nearest, b.w, b.h) * gpix);
        std::printf("gpu_tex_linear_1024sq_gsamples_s=%.2f\n", b.FillRate(p_tex, colour, nil, src, false, draws, c_tex_linear, b.w, b.h) * gpix);
        std::printf("gpu_tex_linear_8x_magnified_gsamples_s=%.2f\n", b.FillRate(p_tex, colour, nil, src, false, draws, c_tex_mag, b.w, b.h) * gpix);
        auto p_alu = b.Pipe("fs_alu", MTLPixelFormatRGBA8Unorm, MTLPixelFormatInvalid, 2);
        const double alu_pix = b.FillRate(p_alu, colour, nil, nil, false, draws, c_solid, b.w, b.h);
        std::printf("gpu_alu_fma_tflops=%.2f (64 vec4 fma/pixel at %.2f Gpix/s)\n", alu_pix * 64.0 * 4.0 * 2.0 / 1e12, alu_pix * gpix);

        // upload: host -> shared texture (replaceRegion), 512x512 Rgba8 x N; and 2048x1792 x1
        {
            std::vector<uint8_t> bytes(size_t(b.w) * b.h * 4, 0x5a);
            id<MTLTexture> up = b.MakeTex(MTLPixelFormatRGBA8Unorm, 512, 512, MTLTextureUsageShaderRead, MTLStorageModeShared);
            double best = 0;
            for (int r = 0; r < reps; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                const int n = quick ? 16 : 64;
                for (int i = 0; i < n; ++i) [up replaceRegion:MTLRegionMake2D(0, 0, 512, 512) mipmapLevel:0 withBytes:bytes.data() bytesPerRow:2048];
                const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                best = std::max(best, double(n) * 512 * 512 * 4 / s);
            }
            std::printf("gpu_upload_replaceregion_512sq_gbps=%.1f\n", best / 1e9);
            best = 0;
            for (int r = 0; r < reps; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                [colour replaceRegion:MTLRegionMake2D(0, 0, b.w, b.h) mipmapLevel:0 withBytes:bytes.data() bytesPerRow:b.w * 4];
                const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                best = std::max(best, double(bytes.size()) / s);
            }
            std::printf("gpu_upload_replaceregion_fullsurface_gbps=%.1f\n", best / 1e9);
            // blit upload from a shared buffer into a private texture (the staging-ring path)
            id<MTLBuffer> stage = [b.dev newBufferWithBytes:bytes.data() length:bytes.size() options:MTLResourceStorageModeShared];
            const double s = b.GpuSeconds([&](id<MTLCommandBuffer> cb) {
                id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
                for (int i = 0; i < 8; ++i)
                    [bl copyFromBuffer:stage sourceOffset:0 sourceBytesPerRow:b.w * 4 sourceBytesPerImage:bytes.size() sourceSize:MTLSizeMake(b.w, b.h, 1)
                             toTexture:colour_priv destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
                [bl endEncoding];
            });
            std::printf("gpu_blit_buffer_to_private_tex_gbps=%.1f\n", 8.0 * double(bytes.size()) / s / 1e9);
            // readback: shared texture getBytes after an idle queue (the Metal arm's readback shape)
            best = 0;
            for (int r = 0; r < reps; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                [colour getBytes:bytes.data() bytesPerRow:b.w * 4 fromRegion:MTLRegionMake2D(0, 0, b.w, b.h) mipmapLevel:0];
                const double s2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                best = std::max(best, double(bytes.size()) / s2);
            }
            std::printf("gpu_readback_getbytes_fullsurface_gbps=%.1f (%.2f ms per %ux%u)\n", best / 1e9, double(bytes.size()) / best * 1e3, b.w, b.h);
            // readback including a 1-draw render + commit + wait: the synchronous readback's full latency
            double best_ms = 1e9, best_wait_ms = 0, best_get_ms = 0;
            for (int r = 0; r < reps; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                id<MTLCommandBuffer> cb = [b.q commandBuffer];
                MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = colour; rp.colorAttachments[0].loadAction = MTLLoadActionLoad; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
                [enc setRenderPipelineState:p_solid];
                float d = 0.5f; [enc setVertexBytes:&d length:sizeof d atIndex:0]; [enc setFragmentBytes:c_solid length:16 atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                [enc endEncoding];
                [cb commit]; [cb waitUntilCompleted];
                const auto t1 = std::chrono::steady_clock::now();
                [colour getBytes:bytes.data() bytesPerRow:b.w * 4 fromRegion:MTLRegionMake2D(0, 0, b.w, b.h) mipmapLevel:0];
                const auto t2 = std::chrono::steady_clock::now();
                const double ms = std::chrono::duration<double>(t2 - t0).count() * 1e3;
                if (ms < best_ms) { best_ms = ms; best_wait_ms = std::chrono::duration<double>(t1 - t0).count() * 1e3; best_get_ms = std::chrono::duration<double>(t2 - t1).count() * 1e3; }
            }
            std::printf("gpu_sync_readback_1draw_fullsurface_ms=%.2f (encode+commit+wait %.2f, getBytes %.2f)\n", best_ms, best_wait_ms, best_get_ms);
        }
        // command buffer round trip: empty commit -> completed (the fence / wait floor)
        {
            double best_us = 1e9;
            for (int r = 0; r < reps * 4; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                id<MTLCommandBuffer> cb = [b.q commandBuffer];
                id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder]; [bl endEncoding];
                [cb commit]; [cb waitUntilCompleted];
                best_us = std::min(best_us, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1e6);
            }
            std::printf("gpu_cmdbuf_commit_wait_roundtrip_us=%.0f\n", best_us);
        }
        // render-pass switch cost: N encoders of one small draw each (load/store of the scale-4 target per pass)
        {
            const int passes = quick ? 32 : 128;
            const double s = b.GpuSeconds([&](id<MTLCommandBuffer> cb) {
                for (int i = 0; i < passes; ++i) {
                    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
                    rp.colorAttachments[0].texture = (i & 1) ? colour : colour_priv;
                    rp.colorAttachments[0].loadAction = MTLLoadActionLoad; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                    rp.depthAttachment.texture = depth; rp.depthAttachment.loadAction = MTLLoadActionLoad; rp.depthAttachment.storeAction = MTLStoreActionStore;
                    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
                    [enc setRenderPipelineState:p_solid_d];
                    [enc setScissorRect:(MTLScissorRect){0, 0, 8, 8}];
                    float d = 0.5f; [enc setVertexBytes:&d length:sizeof d atIndex:0]; [enc setFragmentBytes:c_solid length:16 atIndex:0];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                    [enc endEncoding];
                }
            });
            std::printf("gpu_renderpass_switch_rgba8_depth32f_us=%.1f (per encoder, load+store, %ux%u)\n", s / passes * 1e6, b.w, b.h);
        }
        // small-draw rate: 4096 draws of an 8x8 quad in one encoder (the per-draw fixed cost)
        {
            const int n = quick ? 1024 : 4096;
            const double s = b.GpuSeconds([&](id<MTLCommandBuffer> cb) {
                MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = colour; rp.colorAttachments[0].loadAction = MTLLoadActionLoad; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
                [enc setRenderPipelineState:p_solid];
                [enc setScissorRect:(MTLScissorRect){0, 0, 8, 8}];
                float d = 0.5f; [enc setVertexBytes:&d length:sizeof d atIndex:0];
                for (int i = 0; i < n; ++i) { [enc setFragmentBytes:c_solid length:16 atIndex:0]; [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; }
                [enc endEncoding];
            });
            std::printf("gpu_small_draw_us=%.3f (per 8x8 draw, %d draws, one encoder)\n", s / n * 1e6, n);
        }
    }
    return 0;
}
