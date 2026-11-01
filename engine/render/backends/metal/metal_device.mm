// render/backends/metal/metal_device.mm -- MetalDevice implementation (Objective-C++,
// manual reference counting: every Metal object the device stores is retained once and
// released where it is dropped; autoreleased temporaries live inside @autoreleasepool).
#include "engine/render/backends/metal/metal_device.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "engine/render/backends/metal/metal_draw_executor.h"
#include "engine/render/backends/metal/metal_shader_sources.h"

namespace ps2r {
namespace render {

namespace {

constexpr size_t kVertexRingBytes = 32u * 1024u * 1024u;
constexpr size_t kStagingRingBytes = 64u * 1024u * 1024u;
constexpr size_t kVertexBytes = 32;

id<MTLTexture> Tex(void* p) { return (id<MTLTexture>)p; }

MTLPixelFormat Format(SurfaceFormat f) {
    switch (f) {
        case SurfaceFormat::R8: return MTLPixelFormatR8Unorm;
        case SurfaceFormat::Depth32f: return MTLPixelFormatDepth32Float;
        case SurfaceFormat::DepthStencil24_8: return MTLPixelFormatDepth32Float_Stencil8;
        default: return MTLPixelFormatRGBA8Unorm;
    }
}

size_t Bpp(SurfaceFormat f) { return f == SurfaceFormat::R8 ? 1 : 4; }

// Box-average halving of an RGBA8 level (the GL arm's GlTexturePool::BoxHalve rule).
void BoxHalve(const std::vector<uint8_t>& src, int w, int h, std::vector<uint8_t>& dst, int* nw, int* nh) {
    *nw = std::max(1, w / 2);
    *nh = std::max(1, h / 2);
    dst.assign(static_cast<size_t>(*nw) * *nh * 4, 0);
    for (int y = 0; y < *nh; ++y)
        for (int x = 0; x < *nw; ++x)
            for (int c = 0; c < 4; ++c) {
                int sum = 0, n = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
                        sum += src[(static_cast<size_t>(sy) * w + sx) * 4 + c];
                        ++n;
                    }
                dst[(static_cast<size_t>(y) * *nw + x) * 4 + c] = static_cast<uint8_t>((sum + n / 2) / n);
            }
}

}  // namespace

MetalDevice::MetalDevice(const MetalDeviceSettings& settings) : settings_(settings) {}

MetalDevice::~MetalDevice() { Shutdown(); }

const CallCensus* MetalDevice::Census() const {
    census_.gpu_ns = gpu_ns_.load(std::memory_order_relaxed);
    return &census_;
}

bool MetalDevice::Recommended(std::string* name) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) return false;
        const bool ok = [dev supportsFamily:MTLGPUFamilyApple7];
        if (ok && name) *name = [[dev name] UTF8String];
        [dev release];
        return ok;
    }
}

bool MetalDevice::Init(std::function<void()> swap) {
    Shutdown();
    swap_ = std::move(swap);
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            std::fprintf(stderr, "[metal] no Metal device\n");
            return false;
        }
        if (![dev supportsFamily:MTLGPUFamilyApple4]) {
            std::fprintf(stderr, "[metal] device without framebuffer fetch (Apple GPU family required)\n");
            return false;
        }
        device_ = (void*)dev;   // +1 (MTLCreateSystemDefaultDevice returns a retained object)
        device_name_ = [[dev name] UTF8String];
        id<MTLCommandQueue> q = [dev newCommandQueue];
        if (!q) return false;
        queue_ = (void*)q;
        if (!pipelines_.Build(device_, settings_.shader_depth)) return false;
        id<MTLBuffer> ring = [dev newBufferWithLength:kVertexRingBytes options:MTLResourceStorageModeShared];
        id<MTLBuffer> staging = [dev newBufferWithLength:kStagingRingBytes options:MTLResourceStorageModeShared];
        if (!ring || !staging) return false;
        vertex_ring_ = (void*)ring;
        staging_ = (void*)staging;
        // 1x1 white dummy on every unused texture slot; a 1x1 depth for the depth slot.
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                      width:1 height:1 mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> dummy = [dev newTextureWithDescriptor:td];
        const uint8_t white[4] = {255, 255, 255, 255};
        [dummy replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:white bytesPerRow:4];
        dummy_tex_ = (void*)dummy;
        MTLTextureDescriptor* dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                      width:1 height:1 mipmapped:NO];
        dd.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModeShared;
        dummy_depth_ = (void*)[dev newTextureWithDescriptor:dd];
        max_texture_size_ = 16384;
        max_anisotropy_ = 16;
        MetalSamplerKey sn;
        present_sampler_nearest_ = pipelines_.Sampler(sn);
        MetalSamplerKey sl;
        sl.mag_linear = sl.min_linear = 1;
        present_sampler_linear_ = pipelines_.Sampler(sl);
    }
    executor_ = std::make_unique<MetalDrawExecutor>(*this);
    initialized_ = true;
    frame_ordinal_ = 0;
    frame_census_base_ = census_;
    timing_on_ = false;
    if (settings_.gpu_timing) {
        @autoreleasepool {
            id<MTLDevice> dev = (id<MTLDevice>)device_;
            if ([dev supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {
                for (id<MTLCounterSet> cs in dev.counterSets) {
                    if ([cs.name isEqualToString:MTLCommonCounterSetTimestamp]) timestamp_counter_set_ = (void*)[cs retain];
                    else if ([cs.name isEqualToString:MTLCommonCounterSetStatistic]) statistic_counter_set_ = (void*)[cs retain];
                }
            }
            if (timestamp_counter_set_) {
                // The GPU timestamp clock against the CPU's (ns): two correlated samples ~2 ms apart.
                MTLTimestamp cpu0 = 0, gpu0 = 0, cpu1 = 0, gpu1 = 0;
                [dev sampleTimestamps:&cpu0 gpuTimestamp:&gpu0];
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                [dev sampleTimestamps:&cpu1 gpuTimestamp:&gpu1];
                gpu_ticks_to_ns_ = (gpu1 > gpu0 && cpu1 > cpu0) ? double(cpu1 - cpu0) / double(gpu1 - gpu0) : 1.0;
                timing_on_ = true;
                std::fprintf(stderr, "[metal] gpu_timing on: stage-boundary timestamps, %.4f ns/tick%s\n", gpu_ticks_to_ns_,
                             statistic_counter_set_ ? ", statistics (fragment invocations)" : "");
            } else {
                std::fprintf(stderr, "[metal] gpu_timing requested but stage-boundary counter sampling is unsupported\n");
            }
        }
    }
    return true;
}

std::vector<MetalFrameStats> MetalDevice::FrameStats() const {
    std::lock_guard<std::mutex> lk(stats_mu_);
    return frames_;
}

std::vector<MetalPassStats> MetalDevice::PassStats() const {
    std::lock_guard<std::mutex> lk(stats_mu_);
    std::vector<MetalPassStats> out;
    out.reserve(pass_stats_.size());
    for (const auto& kv : pass_stats_) out.push_back(kv.second);
    return out;
}

void MetalDevice::AddFrameGpu(uint32_t ordinal, uint64_t gpu_ns, double start_s, double end_s, uint64_t frag_ns, uint64_t vtx_ns,
                              uint64_t blit_ns, uint64_t frag_invocations) {
    std::lock_guard<std::mutex> lk(stats_mu_);
    if (frames_.size() <= ordinal) frames_.resize(static_cast<size_t>(ordinal) + 1);
    MetalFrameStats& f = frames_[ordinal];
    f.gpu_ns += gpu_ns;
    f.pass_frag_ns += frag_ns;
    f.pass_vtx_ns += vtx_ns;
    f.pass_blit_ns += blit_ns;
    f.frag_invocations += frag_invocations;
    if (start_s > 0 && end_s > start_s) {
        if (f.gpu_start_s == 0 || start_s < f.gpu_start_s) f.gpu_start_s = start_s;
        if (end_s > f.gpu_end_s) f.gpu_end_s = end_s;
        f.gpu_span_ns = static_cast<uint64_t>((f.gpu_end_s - f.gpu_start_s) * 1e9);
    }
}

// Present: the census delta since the frame's start closes frame frame_ordinal_; the next
// command buffer belongs to the following frame.
void MetalDevice::CloseFrameStats() {
    const CallCensus& c = census_;
    const CallCensus& b = frame_census_base_;
    auto d32 = [](uint64_t x, uint64_t y) { return static_cast<uint32_t>(y >= x ? y - x : 0); };
    MetalFrameStats s;
    s.draw_calls = d32(b.draw_calls, c.draw_calls);
    s.program_binds = d32(b.program_binds, c.program_binds);
    s.framebuffer_binds = d32(b.framebuffer_binds, c.framebuffer_binds);
    s.texture_binds = d32(b.texture_binds, c.texture_binds);
    s.uploads = d32(b.uploads, c.uploads);
    s.upload_bytes = d32(b.upload_bytes, c.upload_bytes);
    s.readbacks = d32(b.readbacks, c.readbacks);
    s.readback_bytes = d32(b.readback_bytes, c.readback_bytes);
    s.copies = d32(b.copies, c.copies);
    s.submits = d32(b.submits, c.submits);
    s.clears = d32(b.clears, c.clears);
    s.gpu_wait_ns = c.gpu_wait_ns >= b.gpu_wait_ns ? c.gpu_wait_ns - b.gpu_wait_ns : 0;
    {
        std::lock_guard<std::mutex> lk(stats_mu_);
        if (frames_.size() <= frame_ordinal_) frames_.resize(static_cast<size_t>(frame_ordinal_) + 1);
        MetalFrameStats& f = frames_[frame_ordinal_];
        const MetalFrameStats keep = f;
        f = s;
        f.gpu_ns = keep.gpu_ns;
        f.gpu_span_ns = keep.gpu_span_ns;
        f.gpu_start_s = keep.gpu_start_s;
        f.gpu_end_s = keep.gpu_end_s;
        f.pass_frag_ns = keep.pass_frag_ns;
        f.pass_vtx_ns = keep.pass_vtx_ns;
        f.pass_blit_ns = keep.pass_blit_ns;
        f.frag_invocations = keep.frag_invocations;
        if (frames_.size() > 200000) frames_.erase(frames_.begin(), frames_.begin() + 100000);   // a bounded run
    }
    frame_census_base_ = c;
    frame_ordinal_++;
}

// ---------------------------------------------------------------- gpu_timing

namespace {
constexpr uint32_t kSamplesPerBuffer = 2048;   // 512 render passes per command buffer
}

bool MetalDevice::TakeSampleBuffers() {
    CbTiming& t = cb_timing_;
    if (t.sample_buf) return true;
    {
        std::lock_guard<std::mutex> lk(stats_mu_);
        if (!free_sample_bufs_.empty()) {
            t.sample_buf = free_sample_bufs_.back();
            free_sample_bufs_.pop_back();
        }
        if (statistic_counter_set_ && !free_stat_bufs_.empty()) {
            t.stat_buf = free_stat_bufs_.back();
            free_stat_bufs_.pop_back();
        }
    }
    @autoreleasepool {
        auto make = [&](void* set) -> void* {
            MTLCounterSampleBufferDescriptor* d = [[MTLCounterSampleBufferDescriptor alloc] init];
            d.counterSet = (id<MTLCounterSet>)set;
            d.storageMode = MTLStorageModeShared;
            d.sampleCount = kSamplesPerBuffer;
            NSError* err = nil;
            id<MTLCounterSampleBuffer> b = [(id<MTLDevice>)device_ newCounterSampleBufferWithDescriptor:d error:&err];
            [d release];
            return (void*)b;   // +1 (null on failure)
        };
        if (!t.sample_buf) t.sample_buf = make(timestamp_counter_set_);
        if (statistic_counter_set_ && !t.stat_buf) {
            t.stat_buf = make(statistic_counter_set_);
            if (!t.stat_buf) {
                // The device refuses statistics sample buffers: stop asking.
                [(id)statistic_counter_set_ release];
                statistic_counter_set_ = nullptr;
            }
        }
    }
    t.used = 0;
    return t.sample_buf != nullptr;
}

void MetalDevice::AttachRenderTiming(void* render_pass_desc, uint32_t surface, uint32_t w, uint32_t h, uint8_t kind) {
    if (!timing_on_) return;
    CbTiming& t = cb_timing_;
    TakeSampleBuffers();
    if (!t.sample_buf || t.used + 4 > kSamplesPerBuffer) {
        passes_unsampled_++;
        t.open_pass = -1;
        return;
    }
    MTLRenderPassDescriptor* rp = (MTLRenderPassDescriptor*)render_pass_desc;
    MTLRenderPassSampleBufferAttachmentDescriptor* sb = rp.sampleBufferAttachments[0];
    sb.sampleBuffer = (id<MTLCounterSampleBuffer>)t.sample_buf;
    sb.startOfVertexSampleIndex = t.used;
    sb.endOfVertexSampleIndex = t.used + 1;
    sb.startOfFragmentSampleIndex = t.used + 2;
    sb.endOfFragmentSampleIndex = t.used + 3;
    if (t.stat_buf) {
        MTLRenderPassSampleBufferAttachmentDescriptor* st = rp.sampleBufferAttachments[1];
        st.sampleBuffer = (id<MTLCounterSampleBuffer>)t.stat_buf;
        st.startOfVertexSampleIndex = t.used;
        st.endOfVertexSampleIndex = t.used + 1;
        st.startOfFragmentSampleIndex = t.used + 2;
        st.endOfFragmentSampleIndex = t.used + 3;
    }
    PassMeta m;
    m.surface = surface;
    m.width = w;
    m.height = h;
    m.kind = kind;
    m.sample_index = t.used;
    t.used += 4;
    t.passes.push_back(m);
    t.open_pass = static_cast<int32_t>(t.passes.size()) - 1;
}

void* MetalDevice::NewBlitEncoder() {
    @autoreleasepool {
        id<MTLCommandBuffer> cb = (id<MTLCommandBuffer>)cb_;
        if (timing_on_) {
            CbTiming& t = cb_timing_;
            TakeSampleBuffers();
            if (t.sample_buf && t.used + 2 <= kSamplesPerBuffer) {
                MTLBlitPassDescriptor* bp = [MTLBlitPassDescriptor blitPassDescriptor];
                MTLBlitPassSampleBufferAttachmentDescriptor* sb = bp.sampleBufferAttachments[0];
                sb.sampleBuffer = (id<MTLCounterSampleBuffer>)t.sample_buf;
                sb.startOfEncoderSampleIndex = t.used;
                sb.endOfEncoderSampleIndex = t.used + 1;
                PassMeta m;
                m.kind = 3;
                m.sample_index = t.used;
                t.used += 2;
                t.passes.push_back(m);
                t.open_pass = -1;
                id<MTLBlitCommandEncoder> b = [cb blitCommandEncoderWithDescriptor:bp];
                return (void*)[b retain];
            }
            passes_unsampled_++;
        }
        id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
        return (void*)[b retain];
    }
}

void MetalDevice::NoteDraw() {
    census_.draw_calls++;
    if (cb_timing_.open_pass >= 0) cb_timing_.passes[static_cast<size_t>(cb_timing_.open_pass)].draws++;
}

void MetalDevice::Shutdown() {
    if (!initialized_ && !device_) return;
    EndEncoder();
    Commit(/*wait=*/true);
    // The completion handlers of the committed command buffers write the statistics: all of them
    // must have run before the device goes.
    while (handlers_pending_.load(std::memory_order_acquire) != 0) std::this_thread::yield();
    if (cb_timing_.sample_buf) [(id)cb_timing_.sample_buf release];
    if (cb_timing_.stat_buf) [(id)cb_timing_.stat_buf release];
    cb_timing_ = CbTiming{};
    for (void* b : free_sample_bufs_) [(id)b release];
    free_sample_bufs_.clear();
    for (void* b : free_stat_bufs_) [(id)b release];
    free_stat_bufs_.clear();
    if (timestamp_counter_set_) { [(id)timestamp_counter_set_ release]; timestamp_counter_set_ = nullptr; }
    if (statistic_counter_set_) { [(id)statistic_counter_set_ release]; statistic_counter_set_ = nullptr; }
    timing_on_ = false;
    executor_.reset();
    for (auto& kv : surfaces_) ReleaseSurface(kv.second);
    surfaces_.clear();
    for (auto& kv : readbacks_) if (kv.second.buf) [(id)kv.second.buf release];   // the GPU is idle (Commit(wait) above)
    readbacks_.clear();
    for (auto& fb : free_readback_bufs_) [(id)fb.first release];
    free_readback_bufs_.clear();
    for (auto& kv : scratch_depth_) [Tex(kv.second) release];
    scratch_depth_.clear();
    for (PickTexture& p : pick_pool_) if (p.tex) [Tex(p.tex) release];
    pick_pool_.clear();
    for (CaptureSlot& c : capture_) {
        if (c.tex) [Tex(c.tex) release];
        if (c.buf) [(id)c.buf release];
        c.buf = nullptr;
        c = CaptureSlot{};
    }
    if (dummy_tex_) { [Tex(dummy_tex_) release]; dummy_tex_ = nullptr; }
    if (dummy_depth_) { [Tex(dummy_depth_) release]; dummy_depth_ = nullptr; }
    if (vertex_ring_) { [(id)vertex_ring_ release]; vertex_ring_ = nullptr; }
    if (staging_) { [(id)staging_ release]; staging_ = nullptr; }
    if (readback_buf_) { [(id)readback_buf_ release]; readback_buf_ = nullptr; readback_buf_bytes_ = 0; }
    if (layer_) { [(id)layer_ release]; layer_ = nullptr; }
    pipelines_.Destroy();   // owns the samplers
    present_sampler_nearest_ = present_sampler_linear_ = nullptr;
    if (queue_) { [(id)queue_ release]; queue_ = nullptr; }
    if (device_) { [(id)device_ release]; device_ = nullptr; }
    ring_offset_ = staging_offset_ = 0;
    initialized_ = false;
}

Caps MetalDevice::QueryCaps() const {
    Caps c;
    c.max_texture_size = max_texture_size_;
    c.staging_readback = true;    // BeginReadback blits into a pooled shared buffer; Poll waits only if the GPU is behind
    c.reverse_subtract = true;
    c.depth_sample = true;
    c.texture_barrier = true;   // the framebuffer fetch: the destination read needs no copy
    c.compute = false;
    c.copy_pack5551 = true;
    c.alpha_lane_write = true;
    return c;
}

// ---------------------------------------------------------------- command buffers / encoders

void* MetalDevice::CommandBuffer() {
    if (cb_) return cb_;
    @autoreleasepool {
        id<MTLCommandBuffer> cb = [(id<MTLCommandQueue>)queue_ commandBuffer];
        cb_ = (void*)[cb retain];
    }
    return cb_;
}

void MetalDevice::EndEncoder() {
    if (encoder_) {
        [(id<MTLRenderCommandEncoder>)encoder_ endEncoding];
        [(id)encoder_ release];
        encoder_ = nullptr;
        enc_color_ = enc_depth_ = nullptr;
        cb_timing_.open_pass = -1;
        if (executor_) executor_->OnEncoderEnded();
    }
    if (blit_) {
        [(id<MTLBlitCommandEncoder>)blit_ endEncoding];
        [(id)blit_ release];
        blit_ = nullptr;
    }
}

void MetalDevice::Commit(bool wait) {
    EndEncoder();
    if (!cb_) {
        if (wait) WaitSerial(serial_ - 1);
        return;
    }
    id<MTLCommandBuffer> cb = (id<MTLCommandBuffer>)cb_;
    const uint64_t serial = serial_;
    std::atomic<uint64_t>* completed = &completed_serial_;
    std::atomic<uint64_t>* gpu_ns = &gpu_ns_;
    std::atomic<uint32_t>* pending = &handlers_pending_;
    const uint32_t ordinal = frame_ordinal_;
    // The pass samples of this command buffer (moved into the handler; the sample buffer
    // returns to the pool once resolved).
    void* sample_buf = cb_timing_.sample_buf;
    void* stat_buf = cb_timing_.stat_buf;
    const uint32_t used = cb_timing_.used;
    std::vector<PassMeta> passes = std::move(cb_timing_.passes);
    cb_timing_ = CbTiming{};
    const double ticks_to_ns = gpu_ticks_to_ns_;
    MetalDevice* self = this;
    pending->fetch_add(1, std::memory_order_relaxed);
    [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
        const double start_s = done.GPUStartTime, end_s = done.GPUEndTime;
        const double busy_s = end_s - start_s;
        const uint64_t busy_ns = busy_s > 0 ? static_cast<uint64_t>(busy_s * 1e9) : 0;
        if (busy_ns) gpu_ns->fetch_add(busy_ns, std::memory_order_relaxed);
        uint64_t frag = 0, vtx = 0, blit = 0, invocations = 0;
        if (sample_buf && used) {
            @autoreleasepool {
                NSData* data = [(id<MTLCounterSampleBuffer>)sample_buf resolveCounterRange:NSMakeRange(0, used)];
                const MTLCounterResultTimestamp* ts = data ? (const MTLCounterResultTimestamp*)data.bytes : nullptr;
                const size_t n = data ? data.length / sizeof(MTLCounterResultTimestamp) : 0;
                NSData* sdata = stat_buf ? [(id<MTLCounterSampleBuffer>)stat_buf resolveCounterRange:NSMakeRange(0, used)] : nil;
                const MTLCounterResultStatistic* st = sdata ? (const MTLCounterResultStatistic*)sdata.bytes : nullptr;
                const size_t sn = sdata ? sdata.length / sizeof(MTLCounterResultStatistic) : 0;
                auto stat = [&](uint32_t a, uint32_t b, uint64_t MTLCounterResultStatistic::*field) -> uint64_t {
                    if (!st || b >= sn) return 0;
                    const uint64_t s0 = st[a].*field, s1 = st[b].*field;
                    if (s0 == MTLCounterErrorValue || s1 == MTLCounterErrorValue || s1 < s0) return 0;
                    return s1 - s0;
                };
                auto span = [&](uint32_t a, uint32_t b) -> uint64_t {
                    if (!ts || b >= n) return 0;
                    const uint64_t t0 = ts[a].timestamp, t1 = ts[b].timestamp;
                    if (t0 == MTLCounterErrorValue || t1 == MTLCounterErrorValue || t1 <= t0) return 0;
                    return static_cast<uint64_t>(double(t1 - t0) * ticks_to_ns);
                };
                std::lock_guard<std::mutex> lk(self->stats_mu_);
                for (const PassMeta& m : passes) {
                    const uint64_t key = (static_cast<uint64_t>(m.surface) << 8) | m.kind;
                    MetalPassStats& ps = self->pass_stats_[key];
                    ps.surface = m.surface;
                    ps.width = m.width;
                    ps.height = m.height;
                    ps.kind = m.kind;
                    ps.passes++;
                    ps.draws += m.draws;
                    if (m.kind == 3) {
                        const uint64_t s = span(m.sample_index, m.sample_index + 1);
                        ps.frag_ns += s;
                        blit += s;
                    } else {
                        const uint64_t v = span(m.sample_index, m.sample_index + 1);
                        const uint64_t f = span(m.sample_index + 2, m.sample_index + 3);
                        ps.vtx_ns += v;
                        ps.frag_ns += f;
                        vtx += v;
                        frag += f;
                        // The statistics at the fragment stage's end minus its start = this pass's fragments.
                        const uint64_t inv = stat(m.sample_index + 2, m.sample_index + 3, &MTLCounterResultStatistic::fragmentInvocations);
                        ps.frag_invocations += inv;
                        ps.frags_passed += stat(m.sample_index + 2, m.sample_index + 3, &MTLCounterResultStatistic::fragmentsPassed);
                        invocations += inv;
                    }
                }
                self->free_sample_bufs_.push_back(sample_buf);
                if (stat_buf) self->free_stat_bufs_.push_back(stat_buf);
            }
        } else if (sample_buf) {
            std::lock_guard<std::mutex> lk(self->stats_mu_);
            self->free_sample_bufs_.push_back(sample_buf);
            if (stat_buf) self->free_stat_bufs_.push_back(stat_buf);
        }
        self->AddFrameGpu(ordinal, busy_ns, start_s, end_s, frag, vtx, blit, invocations);
        // Serials complete in queue order (one queue, in-order execution).
        uint64_t cur = completed->load(std::memory_order_relaxed);
        while (cur < serial && !completed->compare_exchange_weak(cur, serial, std::memory_order_release)) {
        }
        pending->fetch_sub(1, std::memory_order_release);
    }];
    [cb commit];
    census_.submits++;
    draws_at_commit_ = census_.draw_calls;
    if (wait) {
        const auto t0 = std::chrono::steady_clock::now();
        [cb waitUntilCompleted];
        census_.gpu_waits++;
        census_.gpu_wait_ns += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    }
    [cb release];
    cb_ = nullptr;
    serial_++;
}

void MetalDevice::MaybeCommit() {
    if (cb_ && !encoder_ && !blit_ && settings_.commit_draws && census_.draw_calls - draws_at_commit_ >= settings_.commit_draws)
        Commit(/*wait=*/false);
}

void MetalDevice::SplitPassIfDue() {
    if (cb_ && settings_.commit_draws && census_.draw_calls - draws_at_commit_ >= settings_.commit_draws) {
        EndEncoder();
        Commit(/*wait=*/false);
    }
}

void MetalDevice::WaitSerial(uint64_t serial) {
    if (serial >= serial_) Commit(/*wait=*/true);
    if (GpuIdle(serial)) return;
    const auto t0 = std::chrono::steady_clock::now();
    while (!GpuIdle(serial)) {
        // The completion handler advances the watermark; spin briefly (the queue is in order).
        std::this_thread::yield();
    }
    census_.gpu_waits++;
    census_.gpu_wait_ns += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
}

void MetalDevice::Touch(MetalSurface& s) {
    CommandBuffer();
    s.last_gpu_use = serial_;
}

void* MetalDevice::ScratchDepth(uint32_t w, uint32_t h) {
    const uint64_t key = (static_cast<uint64_t>(w) << 32) | h;
    auto it = scratch_depth_.find(key);
    if (it != scratch_depth_.end()) return it->second;
    void* result = nullptr;
    @autoreleasepool {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                     width:w height:h mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget;
        d.storageMode = MTLStorageModePrivate;
        result = (void*)[(id<MTLDevice>)device_ newTextureWithDescriptor:d];
    }
    scratch_depth_[key] = result;
    return result;
}

void* MetalDevice::BeginTargetPass(SurfaceId color, SurfaceId depth, bool clear_color, bool clear_depth,
                                   uint32_t clear_rgba, float clear_z) {
    MetalSurface* c = SurfaceMut(color);
    if (!c || !c->tex || !c->desc.renderable) return nullptr;
    MetalSurface* d = SurfaceMut(depth);
    void* dtex = (d && d->tex && d->desc.format == SurfaceFormat::Depth32f) ? d->tex
                                                                               : ScratchDepth(c->desc.width, c->desc.height);
    if (encoder_ && enc_color_ == c->tex && enc_depth_ == dtex && !clear_color && !clear_depth) return encoder_;
    EndEncoder();
    MaybeCommit();
    CommandBuffer();
    @autoreleasepool {
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = Tex(c->tex);
        rp.colorAttachments[0].loadAction = clear_color ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        if (clear_color)
            rp.colorAttachments[0].clearColor =
                MTLClearColorMake(((clear_rgba >> 0) & 0xFF) / 255.0, ((clear_rgba >> 8) & 0xFF) / 255.0,
                                  ((clear_rgba >> 16) & 0xFF) / 255.0, ((clear_rgba >> 24) & 0xFF) / 255.0);
        rp.depthAttachment.texture = Tex(dtex);
        rp.depthAttachment.loadAction = clear_depth ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        rp.depthAttachment.clearDepth = clear_z;
        AttachRenderTiming((void*)rp, color.id, c->desc.width, c->desc.height, 0);
        id<MTLRenderCommandEncoder> enc = [(id<MTLCommandBuffer>)cb_ renderCommandEncoderWithDescriptor:rp];
        encoder_ = (void*)[enc retain];
        enc_color_ = c->tex;
        enc_depth_ = dtex;
    }
    if (clear_color || clear_depth) census_.clears++;
    census_.framebuffer_binds++;
    c->last_gpu_use = serial_;
    if (d) d->last_gpu_use = serial_;
    return encoder_;
}

void* MetalDevice::BlitEncoder() {
    if (blit_) return blit_;
    if (encoder_) EndEncoder();
    MaybeCommit();
    CommandBuffer();
    blit_ = NewBlitEncoder();
    return blit_;
}

int MetalDevice::UploadVertices(const void* verts, size_t n) {
    const size_t bytes = n * kVertexBytes;
    if (bytes > kVertexRingBytes) return -1;
    if (ring_offset_ + bytes > kVertexRingBytes) {
        // Wrap: the generation that last wrapped must have left the GPU (the GL arm's fence).
        // The open command buffer reads the ring too: commit it, then wait for the previous
        // wrap's generation.
        const uint64_t cur = cb_ ? serial_ : serial_ - 1;
        Commit(/*wait=*/false);
        if (ring_wrap_serial_) WaitSerial(ring_wrap_serial_);
        ring_wrap_serial_ = cur;
        ring_offset_ = 0;
        if (executor_) executor_->OnEncoderEnded();
    }
    uint8_t* base = static_cast<uint8_t*>([(id<MTLBuffer>)vertex_ring_ contents]);
    std::memcpy(base + ring_offset_, verts, bytes);
    const int first = static_cast<int>(ring_offset_ / kVertexBytes);
    ring_offset_ += bytes;
    census_.buffer_maps++;
    return first;
}

// ---------------------------------------------------------------- surfaces

const MetalSurface* MetalDevice::Surface(SurfaceId id) const {
    auto it = surfaces_.find(id.id);
    return it == surfaces_.end() ? nullptr : &it->second;
}

MetalSurface* MetalDevice::SurfaceMut(SurfaceId id) {
    auto it = surfaces_.find(id.id);
    return it == surfaces_.end() ? nullptr : &it->second;
}

SurfaceDesc MetalDevice::DescribeSurface(SurfaceId id) const {
    const MetalSurface* s = Surface(id);
    return s ? s->desc : SurfaceDesc{};
}

SurfaceId MetalDevice::CreateSurface(const SurfaceDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || !device_) return SurfaceId{};
    MetalSurface s;
    s.desc = desc;
    if (s.desc.scale == 0) s.desc.scale = settings_.scale;
    @autoreleasepool {
        // Plain textures get the full mip storage (UploadLevel fills levels > 0 later; the
        // sampler's max LOD follows FinishMipChain). Attachments are level 0 only.
        const bool plain = !desc.renderable && desc.format == SurfaceFormat::Rgba8;
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:Format(desc.format)
                                                                                     width:desc.width height:desc.height
                                                                                 mipmapped:plain ? YES : NO];
        // Attachments live in Private storage (framebuffer compression; no CPU path needed: Upload stages,
        // Readback blits); plain textures and the stencil format stay Shared (replaceRegion / getBytes).
        s.priv = settings_.private_attachments && (desc.renderable || desc.format == SurfaceFormat::Depth32f);
        d.storageMode = s.priv ? MTLStorageModePrivate : MTLStorageModeShared;
        MTLTextureUsage usage = MTLTextureUsageShaderRead;
        if (desc.renderable || desc.format == SurfaceFormat::Depth32f || desc.format == SurfaceFormat::DepthStencil24_8)
            usage |= MTLTextureUsageRenderTarget;
        d.usage = usage;
        id<MTLTexture> t = [(id<MTLDevice>)device_ newTextureWithDescriptor:d];
        if (!t) return SurfaceId{};
        s.tex = (void*)t;
        s.mip_levels_alloc = plain ? static_cast<uint8_t>(std::min<NSUInteger>(d.mipmapLevelCount, 15)) : 0;
    }
    const uint32_t id = next_surface_++;
    surfaces_[id] = s;
    if (desc.renderable) {
        // A renderable colour surface starts transparent black (the GL arm clears it at creation).
        BeginTargetPass(SurfaceId{id}, SurfaceId{}, true, true, 0u, 0.f);
    }
    return SurfaceId{id};
}

void MetalDevice::ReleaseSurface(MetalSurface& s) {
    if (s.view) { [Tex(s.view) release]; s.view = nullptr; }
    if (s.tex) {
        if (enc_color_ == s.tex || enc_depth_ == s.tex) EndEncoder();
        [Tex(s.tex) release];   // in-flight command buffers hold their own references
        s.tex = nullptr;
    }
}

void MetalDevice::DestroySurface(SurfaceId id) {
    auto it = surfaces_.find(id.id);
    if (it == surfaces_.end()) return;
    ReleaseSurface(it->second);
    surfaces_.erase(it);
}

void MetalDevice::Clear(SurfaceId color, SurfaceId depth, uint32_t rgba, float z, uint32_t flags) {
    const MetalSurface* c = Surface(color);
    if (!c || !c->tex || !c->desc.renderable) return;
    const MetalSurface* d = Surface(depth);
    const bool cc = (flags & kClearColor) != 0;
    const bool cd = (flags & kClearDepth) != 0 && d && d->tex;
    if (!cc && !cd) return;
    BeginTargetPass(color, depth, cc, cd, rgba, z);
}

void MetalDevice::StagingUpload(MetalSurface& s, int level, const Rect& rect, const void* bytes, size_t src_stride,
                                size_t bpp) {
    const size_t row = static_cast<size_t>(rect.w) * bpp;
    const size_t dst_row = (row + 255) & ~size_t(255);
    const size_t total = dst_row * static_cast<size_t>(rect.h);
    if (total > kStagingRingBytes) {
        // Larger than the ring: row bands (each at most half the ring).
        const int band = std::max<int>(1, static_cast<int>((kStagingRingBytes / 2) / dst_row));
        for (int y = 0; y < rect.h; y += band) {
            const Rect part{rect.x, rect.y + y, rect.w, std::min(band, rect.h - y)};
            StagingUpload(s, level, part, static_cast<const uint8_t*>(bytes) + src_stride * static_cast<size_t>(y), src_stride, bpp);
        }
        return;
    }
    if (staging_offset_ + total > kStagingRingBytes) {
        const uint64_t cur = cb_ ? serial_ : serial_ - 1;
        Commit(/*wait=*/false);
        if (staging_wrap_serial_) WaitSerial(staging_wrap_serial_);
        staging_wrap_serial_ = cur;
        staging_offset_ = 0;
    }
    uint8_t* base = static_cast<uint8_t*>([(id<MTLBuffer>)staging_ contents]) + staging_offset_;
    const uint8_t* src = static_cast<const uint8_t*>(bytes);
    for (int y = 0; y < rect.h; ++y) std::memcpy(base + dst_row * y, src + src_stride * y, row);
    id<MTLBlitCommandEncoder> blit = (id<MTLBlitCommandEncoder>)BlitEncoder();
    [blit copyFromBuffer:(id<MTLBuffer>)staging_
            sourceOffset:staging_offset_
       sourceBytesPerRow:dst_row
     sourceBytesPerImage:total
              sourceSize:MTLSizeMake(rect.w, rect.h, 1)
               toTexture:Tex(s.tex)
        destinationSlice:0
        destinationLevel:level
       destinationOrigin:MTLOriginMake(rect.x, rect.y, 0)];
    staging_offset_ += total;
    s.last_gpu_use = serial_;
}

void MetalDevice::Upload(SurfaceId id, const Rect& rect, const void* bytes, size_t stride) {
    MetalSurface* s = SurfaceMut(id);
    if (!s || !s->tex || !bytes || rect.w <= 0 || rect.h <= 0) return;
    const bool depth = s->desc.format == SurfaceFormat::Depth32f;
    if (rect.x < 0 || rect.y < 0 || uint64_t(rect.x) + uint32_t(rect.w) > s->desc.width ||
        uint64_t(rect.y) + uint32_t(rect.h) > s->desc.height)
        return;
    if (depth && stride && (stride % 4 || stride / 4 < uint32_t(rect.w))) return;
    const size_t bpp = Bpp(s->desc.format);
    const size_t src_stride = stride ? stride : static_cast<size_t>(rect.w) * bpp;
    census_.uploads++;
    census_.upload_bytes += static_cast<uint64_t>(rect.w) * static_cast<uint64_t>(rect.h) * bpp;
    if (!s->priv && GpuIdle(s->last_gpu_use)) {
        [Tex(s->tex) replaceRegion:MTLRegionMake2D(rect.x, rect.y, rect.w, rect.h)
                       mipmapLevel:0
                         withBytes:bytes
                       bytesPerRow:src_stride];
        return;
    }
    StagingUpload(*s, 0, rect, bytes, src_stride, bpp);
}

void MetalDevice::UploadLevel(SurfaceId id, int level, int w, int h, const void* rgba) {
    MetalSurface* s = SurfaceMut(id);
    if (!s || !s->tex || level <= 0 || w <= 0 || h <= 0 || !rgba) return;
    if (level >= s->mip_levels_alloc) return;
    census_.uploads++;
    census_.upload_bytes += static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * 4u;
    const Rect r{0, 0, w, h};
    if (GpuIdle(s->last_gpu_use)) {
        [Tex(s->tex) replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:level withBytes:rgba bytesPerRow:static_cast<NSUInteger>(w) * 4];
        return;
    }
    StagingUpload(*s, level, r, rgba, static_cast<size_t>(w) * 4, 4);
}

void MetalDevice::FinishMipChain(SurfaceId id, int authored_levels, bool synthesize_tail) {
    MetalSurface* s = SurfaceMut(id);
    if (!s || !s->tex || authored_levels <= 0) return;
    int syn = 0;
    if (synthesize_tail && authored_levels < s->mip_levels_alloc) {
        auto t = Tex(s->tex);   // (the parameter `id` shadows the ObjC type name)
        int cw = std::max<int>(1, static_cast<int>(s->desc.width) >> authored_levels);
        int ch = std::max<int>(1, static_cast<int>(s->desc.height) >> authored_levels);
        WaitSerial(s->last_gpu_use);
        std::vector<uint8_t> cur(static_cast<size_t>(cw) * ch * 4);
        [t getBytes:cur.data() bytesPerRow:static_cast<NSUInteger>(cw) * 4 fromRegion:MTLRegionMake2D(0, 0, cw, ch) mipmapLevel:authored_levels];
        census_.readbacks++;
        census_.readback_bytes += cur.size();
        std::vector<uint8_t> nxt;
        while ((cw > 1 || ch > 1) && authored_levels + syn + 1 < s->mip_levels_alloc) {
            int nw = 0, nh = 0;
            BoxHalve(cur, cw, ch, nxt, &nw, &nh);
            [t replaceRegion:MTLRegionMake2D(0, 0, nw, nh) mipmapLevel:authored_levels + syn + 1 withBytes:nxt.data()
               bytesPerRow:static_cast<NSUInteger>(nw) * 4];
            syn++;
            cur.swap(nxt);
            cw = nw;
            ch = nh;
        }
    }
    s->levels = static_cast<uint8_t>(authored_levels);
    s->synth_levels = static_cast<uint8_t>(syn);
    // The sampled chain ends at the last written level (GL_TEXTURE_MAX_LEVEL): a view over
    // [0, authored + syn] so the LOD clamps there instead of reaching unwritten storage.
    if (s->view) { [Tex(s->view) release]; s->view = nullptr; }
    const int top = std::min<int>(authored_levels + syn, std::max<int>(0, s->mip_levels_alloc - 1));
    @autoreleasepool {
        auto v = [Tex(s->tex) newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                            textureType:MTLTextureType2D
                                                                 levels:NSMakeRange(0, static_cast<NSUInteger>(top + 1))
                                                                 slices:NSMakeRange(0, 1)];
        s->view = (void*)v;   // +1
    }
}

// ---------------------------------------------------------------- draws and passes

DrawResult MetalDevice::Draw(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& params) {
    if (!executor_) return DrawResult{};
    @autoreleasepool {
        return executor_->Draw(ir, tex, params);
    }
}

// The stencil-composite primitives are a class renderer's recipe; the generic pipeline
// never issues them. Not implemented on the Metal arm (recorded).
void MetalDevice::ClearStencil(SurfaceId, uint8_t) {}
void MetalDevice::DrawConstQuad(SurfaceId, SurfaceId, const Rect&, uint32_t, bool, const StencilState&) {}

void MetalDevice::CopyTexture(MetalSurface& src, MetalSurface& dst, int x, int y, int w, int h) {
    id<MTLBlitCommandEncoder> blit = (id<MTLBlitCommandEncoder>)BlitEncoder();
    [blit copyFromTexture:Tex(src.tex)
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(x, y, 0)
               sourceSize:MTLSizeMake(w, h, 1)
                toTexture:Tex(dst.tex)
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(x, y, 0)];
    census_.copies++;
    census_.copy_bytes += static_cast<uint64_t>(w) * h * Bpp(dst.desc.format);
    src.last_gpu_use = dst.last_gpu_use = serial_;
}

// A fullscreen-triangle pass of `program` into `color_tex` (load, store), scissored, with the
// program's uniforms on fragment buffer 0 and `tex0` on texture 0.
void MetalDevice::DrawFullscreen(MetalProgram program, void* color_tex, const Rect& scissor, const void* uniforms,
                                 size_t uniform_bytes, void* tex0, uint32_t w, uint32_t h) {
    void* ps = pipelines_.Pipeline(program, true, false);
    if (!ps) return;
    EndEncoder();
    CommandBuffer();
    @autoreleasepool {
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = Tex(color_tex);
        rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        AttachRenderTiming((void*)rp, 0, w, h, 1);
        id<MTLRenderCommandEncoder> enc = [(id<MTLCommandBuffer>)cb_ renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:(id<MTLRenderPipelineState>)ps];
        MTLViewport vp = {0.0, 0.0, static_cast<double>(w), static_cast<double>(h), 0.0, 1.0};
        [enc setViewport:vp];
        const int sx0 = std::max(0, scissor.x), sy0 = std::max(0, scissor.y);
        const int sx1 = std::min<int>(w, scissor.x + scissor.w), sy1 = std::min<int>(h, scissor.y + scissor.h);
        if (sx1 > sx0 && sy1 > sy0) {
            MTLScissorRect sc = {static_cast<NSUInteger>(sx0), static_cast<NSUInteger>(sy0), static_cast<NSUInteger>(sx1 - sx0),
                                 static_cast<NSUInteger>(sy1 - sy0)};
            [enc setScissorRect:sc];
            if (uniforms && uniform_bytes) [enc setFragmentBytes:uniforms length:uniform_bytes atIndex:0];
            if (tex0) [enc setFragmentTexture:Tex(tex0) atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            NoteDraw();
        }
        [enc endEncoding];
        cb_timing_.open_pass = -1;
    }
    census_.framebuffer_binds++;
}

void MetalDevice::CopyRect(SurfaceId src, const Rect& src_rect, SurfaceId dst, const Rect& dst_rect, uint32_t transform) {
    MetalSurface* s = SurfaceMut(src);
    MetalSurface* d = SurfaceMut(dst);
    if (!s || !d || !s->tex || !d->tex || src_rect.w <= 0 || src_rect.h <= 0) return;
    if (transform == kCopyPack5551 && d->desc.renderable && src_rect.x == dst_rect.x && src_rect.y == dst_rect.y) {
        DrawFullscreen(MetalProgram::Pack5551, d->tex, dst_rect, nullptr, 0, s->tex, d->desc.width, d->desc.height);
        s->last_gpu_use = d->last_gpu_use = serial_;
        return;
    }
    const int w = std::min<int>(src_rect.w, std::min<int>(static_cast<int>(s->desc.width) - src_rect.x,
                                                          static_cast<int>(d->desc.width) - dst_rect.x));
    const int h = std::min<int>(src_rect.h, std::min<int>(static_cast<int>(s->desc.height) - src_rect.y,
                                                          static_cast<int>(d->desc.height) - dst_rect.y));
    if (w <= 0 || h <= 0 || src_rect.x < 0 || src_rect.y < 0 || dst_rect.x < 0 || dst_rect.y < 0) return;
    id<MTLBlitCommandEncoder> blit = (id<MTLBlitCommandEncoder>)BlitEncoder();
    [blit copyFromTexture:Tex(s->tex)
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(src_rect.x, src_rect.y, 0)
               sourceSize:MTLSizeMake(w, h, 1)
                toTexture:Tex(d->tex)
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(dst_rect.x, dst_rect.y, 0)];
    census_.copies++;
    census_.copy_bytes += static_cast<uint64_t>(w) * h * 4u;
    s->last_gpu_use = d->last_gpu_use = serial_;
}

void MetalDevice::WriteAlphaLane(SurfaceId dst, const Rect& dst_rect, SurfaceId lane, const Rect& exclude) {
    MetalSurface* d = SurfaceMut(dst);
    MetalSurface* l = SurfaceMut(lane);
    if (!d || !l || !d->tex || !l->tex || !d->desc.renderable || dst_rect.w <= 0 || dst_rect.h <= 0) return;
    MetalAlphaLaneUniforms u;
    u.origin_x = dst_rect.x;
    u.origin_y = dst_rect.y;
    u.scale = std::max<int>(1, d->desc.scale);
    const bool excl = exclude.w > 0 && exclude.h > 0;
    u.ex0 = excl ? exclude.x : 0;
    u.ey0 = excl ? exclude.y : 0;
    u.ex1 = excl ? exclude.x + exclude.w : 0;
    u.ey1 = excl ? exclude.y + exclude.h : 0;
    DrawFullscreen(MetalProgram::AlphaLane, d->tex, dst_rect, &u, sizeof(u), l->tex, d->desc.width, d->desc.height);
    d->last_gpu_use = l->last_gpu_use = serial_;
}

// ---------------------------------------------------------------- readbacks

void* MetalDevice::ReadbackBuffer(size_t bytes) {
    if (readback_buf_ && readback_buf_bytes_ >= bytes) return readback_buf_;
    if (readback_buf_) { [(id)readback_buf_ release]; readback_buf_ = nullptr; }
    size_t cap = std::max<size_t>(bytes, 1u << 20);
    @autoreleasepool {
        readback_buf_ = (void*)[(id<MTLDevice>)device_ newBufferWithLength:cap options:MTLResourceStorageModeShared];
    }
    readback_buf_bytes_ = readback_buf_ ? cap : 0;
    return readback_buf_;
}

void MetalDevice::ReadTexture(MetalSurface& s, const Rect& rect, void* out, size_t stride) {
    const size_t bpp = Bpp(s.desc.format);
    const size_t dst_stride = stride ? stride : static_cast<size_t>(rect.w) * bpp;
    const bool depth = s.desc.format == SurfaceFormat::Depth32f || s.desc.format == SurfaceFormat::DepthStencil24_8;
    const size_t row_bytes = static_cast<size_t>(rect.w) * bpp;
    // Colour: the blit encoder linearises the rows into a shared buffer on the GPU (a texture
    // getBytes de-twiddles on the CPU, ~1 ms per 512x448 frame); the queue is drained either way.
    // A Private depth attachment has no getBytes: its floats go through the blit as well.
    const bool blit_ok = !depth || (s.priv && s.desc.format == SurfaceFormat::Depth32f);
    void* buf = blit_ok ? ReadbackBuffer(row_bytes * static_cast<size_t>(rect.h)) : nullptr;
    if (buf) {
        id<MTLBlitCommandEncoder> b = (id<MTLBlitCommandEncoder>)BlitEncoder();
        [b copyFromTexture:Tex(s.tex) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(rect.x, rect.y, 0)
                sourceSize:MTLSizeMake(rect.w, rect.h, 1) toBuffer:(id<MTLBuffer>)buf destinationOffset:0
                destinationBytesPerRow:row_bytes destinationBytesPerImage:row_bytes * static_cast<size_t>(rect.h)];
        Commit(/*wait=*/true);
        const uint8_t* src = static_cast<const uint8_t*>([(id<MTLBuffer>)buf contents]);
        if (dst_stride == row_bytes) {
            std::memcpy(out, src, row_bytes * static_cast<size_t>(rect.h));
        } else {
            for (int r = 0; r < rect.h; ++r)
                std::memcpy(static_cast<uint8_t*>(out) + dst_stride * r, src + row_bytes * r, std::min(row_bytes, dst_stride));
        }
    } else {
        // Everything queued so far must have landed (the attachment's writes included).
        Commit(/*wait=*/true);
        [Tex(s.tex) getBytes:out bytesPerRow:dst_stride fromRegion:MTLRegionMake2D(rect.x, rect.y, rect.w, rect.h) mipmapLevel:0];
    }
    census_.readbacks++;
    census_.readback_bytes += static_cast<uint64_t>(rect.w) * static_cast<uint64_t>(rect.h) * bpp;
}

void MetalDevice::Readback(SurfaceId id, const Rect& rect, void* out, size_t stride) {
    MetalSurface* s = SurfaceMut(id);
    if (!s || !s->tex || !out || rect.w <= 0 || rect.h <= 0) return;
    if (rect.x < 0 || rect.y < 0 || uint64_t(rect.x) + uint32_t(rect.w) > s->desc.width ||
        uint64_t(rect.y) + uint32_t(rect.h) > s->desc.height)
        return;
    ReadTexture(*s, rect, out, stride);
}

// The deferred form: the blit lands in the OPEN command buffer (GPU-ordered after the
// attachment's writes) into a pooled shared buffer; the command buffer is committed without a
// wait so the GPU linearises the rows while the CPU encodes the next frame. Colour only
// (RGBA8 / R8); a depth or out-of-bounds request takes the synchronous path into the buffer
// so the fence still completes. Caps.staging_readback = true: the HLE layer defers deposits.
FenceId MetalDevice::BeginReadback(SurfaceId sid, const Rect& rect) {   // `sid`: `id` is Objective-C's type below
    MetalSurface* s = SurfaceMut(sid);
    if (!s || !s->tex || rect.w <= 0 || rect.h <= 0) return FenceId{};
    const size_t bpp = Bpp(s->desc.format);
    PendingReadback pr;
    pr.stride = static_cast<size_t>(rect.w) * bpp;
    pr.rows = rect.h;
    const size_t bytes = pr.stride * static_cast<size_t>(rect.h);
    // A pooled buffer of enough capacity, else a new one.
    for (size_t i = 0; i < free_readback_bufs_.size(); ++i) {
        if (free_readback_bufs_[i].second >= bytes) {
            pr.buf = free_readback_bufs_[i].first;
            free_readback_bufs_[i] = free_readback_bufs_.back();
            free_readback_bufs_.pop_back();
            break;
        }
    }
    if (!pr.buf) {
        @autoreleasepool {
            pr.buf = (void*)[(id<MTLDevice>)device_ newBufferWithLength:std::max<size_t>(bytes, 1u << 16)
                                                                 options:MTLResourceStorageModeShared];
        }
        if (!pr.buf) return FenceId{};
    }
    const bool depth = s->desc.format == SurfaceFormat::Depth32f || s->desc.format == SurfaceFormat::DepthStencil24_8;
    const bool in_bounds = rect.x >= 0 && rect.y >= 0 && uint64_t(rect.x) + uint32_t(rect.w) <= s->desc.width &&
                           uint64_t(rect.y) + uint32_t(rect.h) <= s->desc.height;
    if (depth || !in_bounds) {
        // Not a blit's job: the synchronous read into the buffer; the fence is ready at once.
        Readback(sid, rect, [(id<MTLBuffer>)pr.buf contents], pr.stride);
        pr.serial = 0;
    } else {
        id<MTLBlitCommandEncoder> b = (id<MTLBlitCommandEncoder>)BlitEncoder();
        [b copyFromTexture:Tex(s->tex) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(rect.x, rect.y, 0)
                sourceSize:MTLSizeMake(rect.w, rect.h, 1) toBuffer:(id<MTLBuffer>)pr.buf destinationOffset:0
                destinationBytesPerRow:pr.stride destinationBytesPerImage:bytes];
        pr.serial = serial_;
        s->last_gpu_use = serial_;
        Commit(/*wait=*/false);   // the GPU starts on it now; the caller polls a frame later
        census_.readbacks++;
        census_.readback_bytes += bytes;
    }
    const uint64_t f = next_fence_++;
    readbacks_[f] = pr;
    return FenceId{f};
}

MetalDevice::PickTexture* MetalDevice::AcquirePickTexture(uint32_t w, uint32_t h, bool depth_source) {
    for (PickTexture& p : pick_pool_)
        if (p.tex && p.w == w && p.h == h && p.depth_source == depth_source && GpuIdle(p.serial)) return &p;
    if (pick_pool_.size() >= 16) {
        // Bounded: drop the oldest idle entry of another size (the pool follows the live target set).
        for (size_t i = 0; i < pick_pool_.size(); ++i) {
            if (GpuIdle(pick_pool_[i].serial)) {
                [Tex(pick_pool_[i].tex) release];
                pick_pool_.erase(pick_pool_.begin() + static_cast<long>(i));
                break;
            }
        }
        if (pick_pool_.size() >= 16) return nullptr;
    }
    PickTexture p;
    @autoreleasepool {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:depth_source ? MTLPixelFormatR32Float : MTLPixelFormatRGBA8Unorm
                                                                                     width:w height:h mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModePrivate;
        p.tex = (void*)[(id<MTLDevice>)device_ newTextureWithDescriptor:d];
    }
    if (!p.tex) return nullptr;
    p.w = w;
    p.h = h;
    p.depth_source = depth_source;
    pick_pool_.push_back(p);
    return &pick_pool_.back();
}

MetalDevice::PickTexture* MetalDevice::EncodeNearestPick(MetalSurface& src, const Rect& rect, int step, int sub_x, int sub_y) {
    if (step < 2 || rect.w <= 0 || rect.h <= 0 || rect.w % step || rect.h % step) return nullptr;
    if (sub_x < 0 || sub_y < 0 || sub_x >= step || sub_y >= step) return nullptr;
    if (rect.x < 0 || rect.y < 0 || uint64_t(rect.x) + uint32_t(rect.w) > src.desc.width ||
        uint64_t(rect.y) + uint32_t(rect.h) > src.desc.height)
        return nullptr;
    const bool depth = src.desc.format == SurfaceFormat::Depth32f;
    if (!depth && src.desc.format != SurfaceFormat::Rgba8) return nullptr;
    const uint32_t w = static_cast<uint32_t>(rect.w / step), h = static_cast<uint32_t>(rect.h / step);
    PickTexture* pick = AcquirePickTexture(w, h, depth);
    if (!pick) return nullptr;
    void* ps = pipelines_.Pipeline(depth ? MetalProgram::NearestDepth : MetalProgram::NearestColor, true, false);
    if (!ps) return nullptr;
    MetalNearestUniforms u;
    u.origin_x = rect.x + sub_x;
    u.origin_y = rect.y + sub_y;
    u.step = step;
    EndEncoder();
    MaybeCommit();
    CommandBuffer();
    @autoreleasepool {
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = Tex(pick->tex);
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        AttachRenderTiming((void*)rp, 0, w, h, 1);
        id<MTLRenderCommandEncoder> enc = [(id<MTLCommandBuffer>)cb_ renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:(id<MTLRenderPipelineState>)ps];
        MTLViewport vp = {0.0, 0.0, static_cast<double>(w), static_cast<double>(h), 0.0, 1.0};
        [enc setViewport:vp];
        [enc setFragmentBytes:&u length:sizeof(u) atIndex:0];
        [enc setFragmentTexture:Tex(src.tex) atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        NoteDraw();
        [enc endEncoding];
        cb_timing_.open_pass = -1;
    }
    census_.framebuffer_binds++;
    src.last_gpu_use = serial_;
    pick->serial = serial_;
    return pick;
}

bool MetalDevice::ReadbackNearest(SurfaceId sid, const Rect& rect, int step, void* out, size_t stride, int sub_x, int sub_y) {
    MetalSurface* s = SurfaceMut(sid);
    if (!s || !s->tex || !out) return false;
    PickTexture* pick = EncodeNearestPick(*s, rect, step, sub_x, sub_y);
    if (!pick) return false;
    const size_t bpp = 4;   // RGBA8 bytes or one float
    const size_t row_bytes = static_cast<size_t>(pick->w) * bpp;
    const size_t dst_stride = stride ? stride : row_bytes;
    void* buf = ReadbackBuffer(row_bytes * pick->h);
    if (!buf) return false;
    id<MTLBlitCommandEncoder> b = (id<MTLBlitCommandEncoder>)BlitEncoder();
    [b copyFromTexture:Tex(pick->tex) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
            sourceSize:MTLSizeMake(pick->w, pick->h, 1) toBuffer:(id<MTLBuffer>)buf destinationOffset:0
            destinationBytesPerRow:row_bytes destinationBytesPerImage:row_bytes * pick->h];
    pick->serial = serial_;
    Commit(/*wait=*/true);
    const uint8_t* src = static_cast<const uint8_t*>([(id<MTLBuffer>)buf contents]);
    if (dst_stride == row_bytes) {
        std::memcpy(out, src, row_bytes * pick->h);
    } else {
        for (uint32_t r = 0; r < pick->h; ++r)
            std::memcpy(static_cast<uint8_t*>(out) + dst_stride * r, src + row_bytes * r, std::min(row_bytes, dst_stride));
    }
    census_.readbacks++;
    census_.readback_bytes += row_bytes * pick->h;
    return true;
}

FenceId MetalDevice::BeginReadbackNearest(SurfaceId sid, const Rect& rect, int step, int sub_x, int sub_y) {
    MetalSurface* s = SurfaceMut(sid);
    if (!s || !s->tex || s->desc.format != SurfaceFormat::Rgba8) return FenceId{};
    PickTexture* pick = EncodeNearestPick(*s, rect, step, sub_x, sub_y);
    if (!pick) return FenceId{};
    PendingReadback pr;
    pr.stride = static_cast<size_t>(pick->w) * 4;
    pr.rows = static_cast<int>(pick->h);
    const size_t bytes = pr.stride * static_cast<size_t>(pr.rows);
    for (size_t i = 0; i < free_readback_bufs_.size(); ++i) {
        if (free_readback_bufs_[i].second >= bytes) {
            pr.buf = free_readback_bufs_[i].first;
            free_readback_bufs_[i] = free_readback_bufs_.back();
            free_readback_bufs_.pop_back();
            break;
        }
    }
    if (!pr.buf) {
        @autoreleasepool {
            pr.buf = (void*)[(id<MTLDevice>)device_ newBufferWithLength:std::max<size_t>(bytes, 1u << 16)
                                                                 options:MTLResourceStorageModeShared];
        }
        if (!pr.buf) return FenceId{};
    }
    id<MTLBlitCommandEncoder> b = (id<MTLBlitCommandEncoder>)BlitEncoder();
    [b copyFromTexture:Tex(pick->tex) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
            sourceSize:MTLSizeMake(pick->w, pick->h, 1) toBuffer:(id<MTLBuffer>)pr.buf destinationOffset:0
            destinationBytesPerRow:pr.stride destinationBytesPerImage:bytes];
    pr.serial = serial_;
    pick->serial = serial_;
    Commit(/*wait=*/false);
    census_.readbacks++;
    census_.readback_bytes += bytes;
    const uint64_t f = next_fence_++;
    readbacks_[f] = pr;
    return FenceId{f};
}

bool MetalDevice::ReadbackReady(FenceId fence) {
    auto it = readbacks_.find(fence.id);
    if (it == readbacks_.end()) return false;
    return GpuIdle(it->second.serial);
}

bool MetalDevice::PollReadback(FenceId fence, void* out, size_t stride) {
    auto it = readbacks_.find(fence.id);
    if (it == readbacks_.end()) return false;
    const PendingReadback pr = it->second;
    readbacks_.erase(it);
    if (!out && !GpuIdle(pr.serial)) {
        // Release-only (a dropped grab) while the blit is still in flight: no wait; the buffer
        // dies with the command buffer's reference instead of rejoining the (GPU-idle) pool.
        [(id)pr.buf release];
        return true;
    }
    if (!GpuIdle(pr.serial)) {
        census_.readback_waits++;
        WaitSerial(pr.serial);
    }
    if (out) {
        const size_t dst_stride = stride ? stride : pr.stride;
        const uint8_t* src = static_cast<const uint8_t*>([(id<MTLBuffer>)pr.buf contents]);
        if (dst_stride == pr.stride) {
            std::memcpy(out, src, pr.stride * static_cast<size_t>(pr.rows));
        } else {
            for (int r = 0; r < pr.rows; r++)
                std::memcpy(static_cast<uint8_t*>(out) + dst_stride * r, src + pr.stride * r, std::min(pr.stride, dst_stride));
        }
    }
    free_readback_bufs_.emplace_back(pr.buf, static_cast<size_t>([(id<MTLBuffer>)pr.buf length]));
    return true;
}

// ---------------------------------------------------------------- present

bool MetalDevice::EnsureCapture(CaptureSlot& slot, int w, int h) {
    if (slot.tex && slot.w == w && slot.h == h) return true;
    if (slot.tex) { [Tex(slot.tex) release]; slot.tex = nullptr; }
    if (slot.buf) { [(id)slot.buf release]; slot.buf = nullptr; }
    @autoreleasepool {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                     width:w height:h mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModePrivate;
        slot.tex = (void*)[(id<MTLDevice>)device_ newTextureWithDescriptor:d];
        slot.buf = (void*)[(id<MTLDevice>)device_ newBufferWithLength:static_cast<NSUInteger>(w) * h * 4
                                                             options:MTLResourceStorageModeShared];
    }
    if (!slot.tex || !slot.buf) {
        if (slot.tex) { [Tex(slot.tex) release]; slot.tex = nullptr; }
        if (slot.buf) { [(id)slot.buf release]; slot.buf = nullptr; }
    }
    slot.w = slot.tex ? w : 0;
    slot.h = slot.tex ? h : 0;
    slot.pending = false;
    return slot.tex != nullptr;
}

void MetalDevice::CompleteCapture(CaptureSlot& slot, bool wait, int lag) {
    if (!slot.pending || !slot.tex) return;
    if (!GpuIdle(slot.serial)) {
        if (!wait) return;
        present_capture_waits_++;
        census_.capture_waits++;
        WaitSerial(slot.serial);
    }
    slot.pending = false;
    presented_.resize(static_cast<size_t>(slot.w) * slot.h * 4);
    // The present's command buffer blitted the rows into the slot's buffer (GPU-linearised).
    std::memcpy(presented_.data(), [(id<MTLBuffer>)slot.buf contents], presented_.size());
    presented_w_ = slot.w;
    presented_h_ = slot.h;
    presented_lag_ = lag;
    presented_fresh_ = true;
    census_.readbacks++;
    census_.readback_bytes += presented_.size();
}

// The bounded wait of the prompt capture (SetPresentCaptureWaitNs): poll the slot's serial until the
// GPU is done or the budget ran out. True = done (the slot can be completed without a wait).
bool MetalDevice::WaitCaptureBounded(const CaptureSlot& slot) {
    if (present_capture_wait_ns_ == 0 || !slot.pending) return false;
    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline = t0 + std::chrono::nanoseconds(present_capture_wait_ns_);
    bool done = GpuIdle(slot.serial);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        done = GpuIdle(slot.serial);
    }
    census_.prompt_wait_ns += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    if (done) census_.prompt_captures++;
    else census_.prompt_timeouts++;
    return done;
}

void MetalDevice::Present(const ScanoutDesc& desc) {
    if (desc.filter != PresentationFilter::Surface && desc.filter != PresentationFilter::Nearest &&
        desc.filter != PresentationFilter::Linear)
        throw std::invalid_argument("Graphics presentation filter is unsupported");
    MetalSurface* s = SurfaceMut(desc.source);
    Rect source = desc.src;
    MetalScanoutUniforms su;
    if (desc.source.id) {
        if (!s || !s->tex || !s->desc.width || !s->desc.height || s->desc.width > uint32_t(INT32_MAX) ||
            s->desc.height > uint32_t(INT32_MAX))
            throw std::invalid_argument("Graphics presentation source is unavailable");
        if (!source.x && !source.y && !source.w && !source.h)
            source = {0, 0, int32_t(s->desc.width), int32_t(s->desc.height)};
        if (source.x < 0 || source.y < 0 || source.w <= 0 || source.h <= 0 || uint32_t(source.x) >= s->desc.width ||
            uint32_t(source.y) >= s->desc.height || uint32_t(source.w) > s->desc.width - uint32_t(source.x) ||
            uint32_t(source.h) > s->desc.height - uint32_t(source.y))
            throw std::invalid_argument("Graphics presentation crop exceeds its source");
        su.u0 = float(source.x) / float(s->desc.width);
        su.u1 = float(source.x + source.w) / float(s->desc.width);
        su.v0 = float(source.y) / float(s->desc.height);
        su.v1 = float(source.y + source.h) / float(s->desc.height);
        if (!desc.flip_y) std::swap(su.v0, su.v1);
    }
    EndEncoder();
    const int capture_w = window_w_ > 0 ? window_w_ : desc.dst.w;
    const int capture_h = window_h_ > 0 ? window_h_ : desc.dst.h;
    const bool capture = present_capture_ && capture_w > 0 && capture_h > 0;
    if (capture) {
        // The deferred form completes the PREVIOUS present first (its command buffer is long
        // done when this one presents: no drain).
        if (present_capture_deferred_) {
            CompleteCapture(capture_[capture_cur_ ^ 1], /*wait=*/true);
        }
        CaptureSlot& slot = capture_[capture_cur_];
        CompleteCapture(slot, /*wait=*/true);   // a slot still pending from a mode switch
        if (EnsureCapture(slot, capture_w, capture_h)) {
            void* ps = pipelines_.Pipeline(MetalProgram::Scanout, true, false);
            CommandBuffer();
            @autoreleasepool {
                MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = Tex(slot.tex);
                rp.colorAttachments[0].loadAction = MTLLoadActionClear;
                rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
                rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                AttachRenderTiming((void*)rp, 0, static_cast<uint32_t>(slot.w), static_cast<uint32_t>(slot.h), 2);
                id<MTLRenderCommandEncoder> enc = [(id<MTLCommandBuffer>)cb_ renderCommandEncoderWithDescriptor:rp];
                census_.clears++;
                if (s && s->tex && ps) {
                    const int vw = desc.dst.w > 0 ? desc.dst.w : capture_w;
                    const int vh = desc.dst.h > 0 ? desc.dst.h : capture_h;
                    // Rows stored bottom-up (the GL readback order): the viewport's y origin is
                    // the destination's bottom offset as the GL arm's glViewport takes it.
                    su.flip_rows = 1;
                    MTLViewport vp = {static_cast<double>(desc.dst.x), static_cast<double>(desc.dst.y),
                                      static_cast<double>(vw), static_cast<double>(vh), 0.0, 1.0};
                    [enc setRenderPipelineState:(id<MTLRenderPipelineState>)ps];
                    [enc setViewport:vp];
                    [enc setVertexBytes:&su length:sizeof(su) atIndex:0];
                    [enc setFragmentTexture:Tex(s->tex) atIndex:0];
                    void* sampler = desc.filter == PresentationFilter::Linear ? present_sampler_linear_ : present_sampler_nearest_;
                    [enc setFragmentSamplerState:(id<MTLSamplerState>)sampler atIndex:0];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
                    NoteDraw();
                    s->last_gpu_use = serial_;
                }
                [enc endEncoding];
                cb_timing_.open_pass = -1;
                id<MTLBlitCommandEncoder> b = (id<MTLBlitCommandEncoder>)NewBlitEncoder();
                [b copyFromTexture:Tex(slot.tex) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake(slot.w, slot.h, 1) toBuffer:(id<MTLBuffer>)slot.buf destinationOffset:0
                destinationBytesPerRow:static_cast<NSUInteger>(slot.w) * 4
              destinationBytesPerImage:static_cast<NSUInteger>(slot.w) * slot.h * 4];
                [b endEncoding];
                [b release];
            }
            slot.serial = serial_;
            slot.pending = true;
            census_.framebuffer_binds++;
        }
        if (layer_) {
            // The window shows the same image (the CAMetalLayer's drawable, blitted and flipped).
            @autoreleasepool {
                CAMetalLayer* layer = (CAMetalLayer*)layer_;
                id<CAMetalDrawable> drawable = [layer nextDrawable];
                if (drawable) {
                    void* ps = pipelines_.Pipeline(MetalProgram::Scanout, true, false);
                    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
                    rp.colorAttachments[0].texture = drawable.texture;
                    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
                    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
                    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                    id<MTLRenderCommandEncoder> enc = [(id<MTLCommandBuffer>)cb_ renderCommandEncoderWithDescriptor:rp];
                    if (s && s->tex && ps) {
                        MetalScanoutUniforms lu = su;
                        lu.flip_rows = 0;
                        [enc setRenderPipelineState:(id<MTLRenderPipelineState>)ps];
                        [enc setVertexBytes:&lu length:sizeof(lu) atIndex:0];
                        [enc setFragmentTexture:Tex(s->tex) atIndex:0];
                        [enc setFragmentSamplerState:(id<MTLSamplerState>)present_sampler_nearest_ atIndex:0];
                        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
                    }
                    [enc endEncoding];
                    [(id<MTLCommandBuffer>)cb_ presentDrawable:drawable];
                }
            }
        }
        Commit(/*wait=*/false);
        if (!present_capture_deferred_) {
            CompleteCapture(slot, /*wait=*/true, /*lag=*/0);
        } else {
            // The prompt capture (round 5): a bounded wait for THIS present's GPU work, the picture
            // handed over now when it made it (lag 0); otherwise the next present completes it (lag 1).
            // A previous present's picture completed above (lag 1) and not yet handed over goes first
            // (its own post) so this present's prompt picture does not overwrite it unshown.
            const bool prompt_ok = present_capture_wait_ns_ == 0 || !present_capture_gate_ || present_capture_gate_();
            if (!prompt_ok) census_.prompt_skipped++;
            if (prompt_ok && WaitCaptureBounded(slot)) {
                if (presented_fresh_ && swap_) swap_();
                CompleteCapture(slot, /*wait=*/false, /*lag=*/0);
            }
            capture_cur_ ^= 1;
        }
    } else {
        presented_w_ = presented_h_ = 0;
        presented_fresh_ = false;
        Commit(/*wait=*/false);
    }
    CloseFrameStats();
    if (swap_) swap_();
}

void MetalDevice::SetPresentCaptureDeferred(bool deferred) {
    if (present_capture_deferred_ && !deferred) {
        for (CaptureSlot& c : capture_) CompleteCapture(c, /*wait=*/true);
    }
    present_capture_deferred_ = deferred;
}

void MetalDevice::ArmPresentCapture(bool armed) {
    present_capture_ = armed;
    if (!armed) {
        presented_.clear();
        presented_w_ = presented_h_ = 0;
        presented_fresh_ = false;
    }
}

bool MetalDevice::ReadPresented(std::vector<uint8_t>* rgba, int* w, int* h) {
    if (!present_capture_ || presented_w_ <= 0 || presented_h_ <= 0 || !rgba || !w || !h) return false;
    // Under the deferred capture an image is handed over ONCE: a present whose own capture is still
    // pending and whose predecessor was already handed over has nothing new (no duplicate post).
    if (present_capture_deferred_ && !presented_fresh_) return false;
    presented_fresh_ = false;
    *rgba = presented_;
    *w = presented_w_;
    *h = presented_h_;
    return true;
}

void MetalDevice::SetWindowSize(int w, int h) {
    window_w_ = w;
    window_h_ = h;
}

void MetalDevice::SetMetalLayer(void* layer) {
    if (layer_) [(id)layer_ release];
    layer_ = layer ? (void*)[(id)layer retain] : nullptr;
    if (layer_) ((CAMetalLayer*)layer_).device = (id<MTLDevice>)device_;
}

void MetalDevice::FlushCommands() { Commit(/*wait=*/false); }

void MetalDevice::BeginFrame() {}
void MetalDevice::EndFrame() { Commit(/*wait=*/false); }

}  // namespace render
}  // namespace ps2r
