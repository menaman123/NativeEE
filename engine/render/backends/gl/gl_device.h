// render/backends/gl/gl_device.h -- GlDevice: the OpenGL 4.1 core Backend.
//
// Owns the GL objects of the backend (programs, uniform / state shadows, the vertex
// stream, the texture pool, the 1x1 white dummy texture every unused sampler unit
// binds) and the surface table: a renderable RGBA8 surface is a texture plus a
// framebuffer object, a depth surface a GL_DEPTH_COMPONENT32F texture (sampleable, so
// depth views can be served), a plain RGBA8 surface a texture drawn from the pool.
// Depth clears use 0.0 (the vertex shader maps GS z to window depth z / 2^32, so the
// GS far plane z = 0 is exactly window 0.0 and GEQUAL orders correctly). Readback is
// synchronous glReadPixels (rows bottom-up as the attachment stores them) and the
// staging API completes immediately. A DepthStencil24_8 surface is a
// GL_DEPTH24_STENCIL8 renderbuffer; the stencil-composite passes draw into it through
// two device-owned framebuffers -- a stencil-only one (no colour attachment, draw
// buffer NONE) and a resolve one (a colour texture + the same stencil) -- rebuilt
// when the attachments change (OLD GsS9EnsureStencil). Present draws the scanout quad into the default
// framebuffer and calls the swap callback the platform supplied. The GL context must
// be current on the calling thread for the device's whole life.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/render/backend.h"
#include "engine/render/backends/gl/gl_shader_library.h"
#include "engine/render/backends/gl/gl_state_cache.h"
#include "engine/render/backends/gl/gl_texture_pool.h"
#include "engine/render/backends/gl/gl_uniform_cache.h"
#include "engine/render/backends/gl/gl_vertex_stream.h"
#include "engine/render/texture_filter_selection.h"

namespace ps2r {
namespace render {

class GlDrawExecutor;

struct GlDeviceSettings {
    uint8_t scale = 1;             // default surface scale
};

// EXT/ARB_texture_filter_anisotropic tokens (the core headers do not all carry them).
constexpr uint32_t kGlTextureMaxAnisotropy = 0x84FE;
constexpr uint32_t kGlMaxTextureMaxAnisotropy = 0x84FF;

// One surface's GL objects.
struct GlSurface {
    SurfaceDesc desc;
    uint32_t tex = 0;
    uint32_t rbo = 0;              // DepthStencil24_8 renderbuffer
    uint32_t fbo = 0;              // renderable colour surfaces only
    uint32_t attached_depth = 0;   // depth texture currently attached to fbo
    bool pooled = false;           // texture came from / returns to the pool
    bool respecify = false;        // pooled storage was busy: the first full upload re-specifies
    uint8_t levels = 0;            // authored mip levels
    uint8_t synth_levels = 0;      // synthesized tail levels
};

class GlDevice : public Backend {
public:
    explicit GlDevice(const GlDeviceSettings& settings = GlDeviceSettings{});
    ~GlDevice() override;

    // The context is already current; `swap` presents the default framebuffer.
    bool Init(std::function<void()> swap);
    void Shutdown();
    bool IsInitialized() const { return initialized_; }

    Caps QueryCaps() const override;
    SurfaceId CreateSurface(const SurfaceDesc& desc) override;
    void DestroySurface(SurfaceId id) override;
    SurfaceDesc DescribeSurface(SurfaceId id) const override;
    void Clear(SurfaceId color, SurfaceId depth, uint32_t rgba, float z, uint32_t flags) override;
    void Upload(SurfaceId id, const Rect& rect, const void* bytes, size_t stride) override;
    void UploadLevel(SurfaceId id, int level, int w, int h, const void* rgba) override;
    void FinishMipChain(SurfaceId id, int authored_levels, bool synthesize_tail) override;
    DrawResult Draw(const gs::DrawIr& ir, const gs::TextureSource& tex,
                    const DrawParams& params) override;
    void CopyRect(SurfaceId src, const Rect& src_rect, SurfaceId dst, const Rect& dst_rect,
                  uint32_t transform) override;
    void WriteAlphaLane(SurfaceId dst, const Rect& dst_rect, SurfaceId lane, const Rect& exclude) override;
    void Readback(SurfaceId id, const Rect& rect, void* out, size_t stride) override;
    FenceId BeginReadback(SurfaceId id, const Rect& rect) override;
    bool PollReadback(FenceId fence, void* out, size_t stride) override;
    void Present(const ScanoutDesc& desc) override;
    void ArmPresentCapture(bool armed) override;
    bool ReadPresented(std::vector<uint8_t>* rgba, int* w, int* h) override;
    // The armed capture's read: synchronous (the default -- ReadPresented hands over
    // THIS present's image; the frame dump's contract) or DEFERRED: Present reads
    // its window image into a pixel-pack buffer behind a fence and ReadPresented
    // hands over the PREVIOUS present's image (the fence of frame N-1 is long
    // signalled when frame N presents: no GPU pipeline drain per present; the
    // output lane is one present late). The play composition's choice; a reader
    // that needs this present's image keeps the synchronous form.
    void SetPresentCaptureDeferred(bool deferred) override;
    bool PresentCaptureDeferred() const { return present_capture_deferred_; }
    uint64_t PresentCaptureWaits() const { return present_capture_waits_; }   // fences found unsignalled at completion
    void ClearStencil(SurfaceId stencil, uint8_t value) override;
    void DrawConstQuad(SurfaceId color, SurfaceId stencil, const Rect& scissor, uint32_t rgba,
                       bool write_alpha, const StencilState& st) override;
    void BeginFrame() override;
    void EndFrame() override;

    // Binds GL_FRAMEBUFFER to the colour surface's fbo with `depth` attached
    // (attachment changed only when it differs). Returns the fbo (0 if none).
    uint32_t BindTarget(SurfaceId color, SurfaceId depth);
    // Binds GL_FRAMEBUFFER to the stencil-only framebuffer (color none) or the
    // resolve framebuffer (colour texture + stencil renderbuffer). Returns the fbo
    // (0 when the stencil surface is missing or the framebuffer is incomplete).
    uint32_t BindStencilTarget(SurfaceId color, SurfaceId stencil);
    // Raw stencil state (not cached: the passes set it per draw and reset it after).
    static void ApplyStencil(const StencilState& st);
    static void ResetStencil();
    uint32_t ConstVao() const { return const_vao_; }

    // Run brackets (a run = consecutive draws; any other GL work ends it).
    void BeginDrawRun();
    void EndDrawRun();

    // Component access for the executor and tests.
    GlStateCache& State() { return state_; }
    GlUniformCache& Uniforms() { return uniforms_; }
    const GlShaderLibrary& Shaders() const { return shaders_; }
    GlVertexStream& Vertices() { return vertices_; }
    // A 1x1 white RGBA8 texture kept bound on every sampler unit a draw does not use.
    uint32_t DummyTexture() const { return dummy_tex_; }
    // The player's filtering enhancement over the owned textures (Charter 2 A2):
    // read per draw by the executor; null = the title's choice everywhere. Any
    // thread may change the selection's fields; this pointer is set once by the
    // context owner. MaxAnisotropy is the driver's limit (1 = no extension).
    void SetTextureFilter(std::shared_ptr<const TextureFilterSelection> selection) override {
        texture_filter_ = std::move(selection);
    }
    const TextureFilterSelection* TextureFilter() const { return texture_filter_.get(); }
    int MaxAnisotropy() const { return max_anisotropy_; }
    // Census of the enhancement: owned-texture draws seen and the draws whose
    // sampler the selection changed (the title's choice would have differed).
    struct TextureFilterCensus {
        uint64_t owned_draws = 0;
        uint64_t enhanced_draws = 0;
        uint64_t hw_lod_draws = 0;   // draws sampled with the hardware LOD (aniso / trilinear over a chain)
    };
    const TextureFilterCensus& FilterCensus() const { return filter_census_; }
    TextureFilterCensus& FilterCensus() { return filter_census_; }
    const GlSurface* Surface(SurfaceId id) const;
    const CallCensus* Census() const override;
    CallCensus& CensusMut() { return census_; }
    // Default-framebuffer size Present falls back to when ScanoutDesc.dst is empty.
    void SetWindowSize(int w, int h) override;
    // The destination-read route of a shader-blended draw: the lazy Cd COPY of the
    // attachment (glCopyTexSubImage2D before the draw -- ends the render pass each
    // time) or, when the driver exposes NV_texture_barrier, the attachment itself
    // sampled behind glTextureBarrierNV (Caps::texture_barrier). Resolved at Init
    // (dlsym); the mode defaults to the COPY (the Apple barrier is not tile-coherent:
    // gl_device.cpp Init); SetCdCopyByBarrier(true) opts in where the barrier is exact.
    using TextureBarrierFn = void (*)();
    TextureBarrierFn TextureBarrier() const { return texture_barrier_; }
    void SetCdCopyByBarrier(bool on) { cd_copy_by_barrier_ = on && texture_barrier_ != nullptr; }
    bool CdCopyByBarrier() const { return cd_copy_by_barrier_; }
    // Every blended draw through the shader-side blend (the exact integer GS equation over
    // the destination copy) instead of the fixed-function arm where one exists: the
    // reference arm another backend's identity judge compares against. OFF by default.
    void SetForceShaderBlend(bool on) { force_shader_blend_ = on; }
    bool ForceShaderBlend() const { return force_shader_blend_; }
    // glFlush: the host's swap callback for a window that is never shown (the
    // hidden GL window of a captured present) -- the command stream reaches the
    // GPU without the swap's drawable flush.
    void FlushCommands() override;

private:
    bool BuildRenderable(GlSurface& s);
    void ReleaseSurface(GlSurface& s);

    GlDeviceSettings settings_;
    bool initialized_ = false;
    std::function<void()> swap_;
    GlStateCache state_;
    GlUniformCache uniforms_;
    GlShaderLibrary shaders_;
    GlVertexStream vertices_;
    GlTexturePool pool_;
    std::unique_ptr<GlDrawExecutor> executor_;
    uint32_t dummy_tex_ = 0;
    uint32_t scanout_vao_ = 0;
    uint32_t scanout_vbo_ = 0;
    uint32_t present_nearest_ = 0; // window-only sampler objects; never alter shared textures
    uint32_t present_linear_ = 0;
    uint32_t const_vao_ = 0;       // NDC fullscreen triangle for the stencil-masked quads
    uint32_t const_vbo_ = 0;
    uint32_t sten_fbo_ = 0;        // stencil-only framebuffer
    uint32_t res_fbo_ = 0;         // colour + stencil resolve framebuffer
    uint32_t sten_fbo_rbo_ = 0;    // attachments the two framebuffers currently hold
    uint32_t res_fbo_rbo_ = 0;
    uint32_t res_fbo_tex_ = 0;
    int window_w_ = 0;
    int window_h_ = 0;
    bool present_capture_ = false;      // ArmPresentCapture: Present reads its window image back
    std::vector<uint8_t> presented_;    // the last captured window image (RGBA8, bottom-up)
    int presented_w_ = 0, presented_h_ = 0;
    uint32_t capture_fbo_ = 0;          // the armed present's window-sized target (then blitted to the window)
    uint32_t capture_tex_ = 0;
    int capture_w_ = 0, capture_h_ = 0;
    bool CaptureTarget(int w, int h);
    // The deferred capture: the present's pixels are read into `pbo` behind `sync`;
    // completed (mapped into presented_) at the NEXT present or at Shutdown.
    struct DeferredCapture {
        uint32_t pbo = 0;
        void* sync = nullptr;   // GLsync
        int w = 0, h = 0;
        bool pending = false;
    };
    TextureBarrierFn texture_barrier_ = nullptr;
    bool cd_copy_by_barrier_ = false;
    bool force_shader_blend_ = false;
    bool present_capture_deferred_ = false;
    DeferredCapture deferred_capture_;
    uint64_t present_capture_waits_ = 0;
    void CompleteDeferredCapture(bool wait);
    void ReleaseDeferredCapture();
    std::unordered_map<uint32_t, GlSurface> surfaces_;
    uint32_t next_surface_ = 1;
    struct PendingReadback {
        std::vector<uint8_t> bytes;
        size_t stride = 0;
        int rows = 0;
    };
    std::unordered_map<uint64_t, PendingReadback> readbacks_;
    uint64_t next_fence_ = 1;
    int max_texture_size_ = 0;
    int max_anisotropy_ = 1;
    std::shared_ptr<const TextureFilterSelection> texture_filter_;
    TextureFilterCensus filter_census_;
    mutable CallCensus census_;
};

}  // namespace render
}  // namespace ps2r
