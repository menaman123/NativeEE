// render/backends/gl/gl_device.cpp -- GlDevice implementation.
#include "engine/render/backends/gl/gl_device.h"

#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "engine/render/backends/gl/gl_draw_executor.h"

namespace ps2r {
namespace render {

namespace {
bool HasExtension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (e && std::strcmp(e, name) == 0) return true;
    }
    return false;
}
}  // namespace

GlDevice::GlDevice(const GlDeviceSettings& settings) : settings_(settings) {}

GlDevice::~GlDevice() { Shutdown(); }

const CallCensus* GlDevice::Census() const {
    census_.buffer_maps = vertices_.Maps();   // the stream keeps its own count
    return &census_;
}

bool GlDevice::Init(std::function<void()> swap) {
    Shutdown();
    swap_ = std::move(swap);
    state_.InvalidateAll();
    state_.SetCensus(&census_);
    uniforms_.SetCensus(&census_);
    // Dual-source blending (GL 3.3 core): the PS2 program's second fragment output
    // carries the blend factors. Required by the fixed-function blend arm as is;
    // the unified blend state (one state for every FUNC_ADD tuple) is gated on it too.
    {
        GLint dual = 0;
        glGetIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, &dual);
        dual_source_ = dual >= 1;
    }
    if (!shaders_.Build(uniforms_)) return false;
    if (!vertices_.Init(state_)) return false;

    // Scanout quad: NDC positions + texcoords with V flipped (PS2 top at GL bottom).
    static const float kQuad[] = {
        -1.f, -1.f, 0.f, 1.f,  1.f, -1.f, 1.f, 1.f,  -1.f, 1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 1.f,  1.f,  1.f, 1.f, 0.f,  -1.f, 1.f, 0.f, 0.f,
    };
    glGenVertexArrays(1, &scanout_vao_);
    glGenBuffers(1, &scanout_vbo_);
    state_.BindVertexArray(scanout_vao_);
    state_.BindArrayBuffer(scanout_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    state_.BindVertexArray(0);

    // A window filter belongs to presentation, not the shared color image.
    // Sampler objects keep mixed draws and later retained presents independent
    // of the explicit owned filter; no texture parameter repair is needed.
    const auto sampler = [](uint32_t& object,GLint filter) {
        glGenSamplers(1,&object);
        glSamplerParameteri(object,GL_TEXTURE_MIN_FILTER,filter);
        glSamplerParameteri(object,GL_TEXTURE_MAG_FILTER,filter);
        glSamplerParameteri(object,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glSamplerParameteri(object,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    };
    sampler(present_nearest_,GL_NEAREST);
    sampler(present_linear_,GL_LINEAR);

    // NDC fullscreen triangle for the stencil-masked constant quads.
    static const float kTri[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    glGenVertexArrays(1, &const_vao_);
    glGenBuffers(1, &const_vbo_);
    state_.BindVertexArray(const_vao_);
    state_.BindArrayBuffer(const_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kTri), kTri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    state_.BindVertexArray(0);

    // GL state defaults: GEQUAL depth (the GS default), far plane cleared to 0.0.
    state_.Enable(GL_DEPTH_TEST);
    state_.DepthFunc(GL_GEQUAL);
    glClearDepth(0.0);
    state_.Enable(GL_BLEND);
    state_.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 1x1 white dummy for every sampler unit a draw leaves unused.
    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &dummy_tex_);
    state_.ActiveTexture(GL_TEXTURE0);
    state_.BindTex2D(dummy_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    state_.TexParami(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    state_.TexParami(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    state_.BindTex2D(0);
    for (GLenum unit : {GL_TEXTURE1, GL_TEXTURE2, GL_TEXTURE3, GL_TEXTURE4}) {
        state_.ActiveTexture(unit);
        state_.BindTex2D(dummy_tex_);
    }
    state_.ActiveTexture(GL_TEXTURE0);

    GLint mts = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mts);
    max_texture_size_ = mts;
    // Anisotropic filtering (EXT/ARB_texture_filter_anisotropic; a GL 4.1 core
    // context exposes it as an extension): the limit is 1 without it, and the
    // executor never asks the driver for more than the limit.
    max_anisotropy_ = 1;
    GLint extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
    for (GLint i = 0; i < extensions; ++i) {
        const char* name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (!name) continue;
        if (std::strcmp(name, "GL_EXT_texture_filter_anisotropic") == 0 ||
            std::strcmp(name, "GL_ARB_texture_filter_anisotropic") == 0) {
            GLfloat limit = 1.0f;
            glGetFloatv(kGlMaxTextureMaxAnisotropy, &limit);
            max_anisotropy_ = std::max(1, static_cast<int>(limit));
            break;
        }
    }
    executor_ = std::make_unique<GlDrawExecutor>(*this);
    // NV_texture_barrier (exposed by Apple's GL-on-Metal 4.1 and by Mesa): the
    // destination read without the copy. Looked up by name; absent = the copy route.
    texture_barrier_ = nullptr;
    if (HasExtension("GL_NV_texture_barrier"))
        texture_barrier_ = reinterpret_cast<TextureBarrierFn>(dlsym(RTLD_DEFAULT, "glTextureBarrierNV"));
    // OFF by default: Apple's GL-on-Metal barrier is not coherent at the 32-row
    // tile boundaries for a same-pass attachment read (1-px horizontal lines every
    // 32 rows on the Paris capture, 0.15-0.4 % of the pixels, delta up to 36); the
    // copy route is the reference. Opt-in for a driver whose barrier is exact.
    cd_copy_by_barrier_ = false;
    initialized_ = true;
    return true;
}

void GlDevice::Shutdown() {
    if (!initialized_) return;
    executor_.reset();
    for (auto& kv : surfaces_) ReleaseSurface(kv.second);
    surfaces_.clear();
    pool_.DropAll(state_);
    ReleaseReadbacks();
    if (dummy_tex_) { state_.DeleteTextures(1, &dummy_tex_); dummy_tex_ = 0; }
    vertices_.Shutdown(state_);
    if (scanout_vbo_) { state_.DeleteBuffers(1, &scanout_vbo_); scanout_vbo_ = 0; }
    if (scanout_vao_) { state_.DeleteVertexArrays(1, &scanout_vao_); scanout_vao_ = 0; }
    if (present_nearest_) { glDeleteSamplers(1,&present_nearest_); present_nearest_ = 0; }
    if (present_linear_) { glDeleteSamplers(1,&present_linear_); present_linear_ = 0; }
    if (const_vbo_) { state_.DeleteBuffers(1, &const_vbo_); const_vbo_ = 0; }
    if (const_vao_) { state_.DeleteVertexArrays(1, &const_vao_); const_vao_ = 0; }
    if (sten_fbo_) { state_.DeleteFramebuffers(1, &sten_fbo_); sten_fbo_ = 0; }
    if (res_fbo_) { state_.DeleteFramebuffers(1, &res_fbo_); res_fbo_ = 0; }
    ReleaseDeferredCapture();
    if (capture_fbo_) { state_.DeleteFramebuffers(1, &capture_fbo_); capture_fbo_ = 0; }
    if (capture_tex_) { state_.DeleteTextures(1, &capture_tex_); capture_tex_ = 0; }
    capture_w_ = capture_h_ = 0;
    sten_fbo_rbo_ = res_fbo_rbo_ = res_fbo_tex_ = 0;
    shaders_.Destroy();
    state_.InvalidateAll();
    initialized_ = false;
}

Caps GlDevice::QueryCaps() const {
    Caps c;
    c.max_texture_size = max_texture_size_;
    c.staging_readback = true;   // BeginReadback: a pack buffer behind a fence
    c.reverse_subtract = true;
    c.depth_sample = true;
    c.texture_barrier = texture_barrier_ != nullptr;
    c.compute = false;
    c.copy_pack5551 = shaders_.Pack5551Program() != 0;
    c.alpha_lane_write = shaders_.AlphaLaneProgram() != 0;
    c.dual_source_blend = dual_source_;
    return c;
}

const GlSurface* GlDevice::Surface(SurfaceId id) const {
    auto it = surfaces_.find(id.id);
    return it == surfaces_.end() ? nullptr : &it->second;
}

SurfaceDesc GlDevice::DescribeSurface(SurfaceId id) const {
    const GlSurface* s = Surface(id);
    return s ? s->desc : SurfaceDesc{};
}

// Colour attachment RGBA8 + framebuffer; NEAREST, cleared to transparent black and
// depth 0.0 (the GS far plane).
bool GlDevice::BuildRenderable(GlSurface& s) {
    glGenFramebuffers(1, &s.fbo);
    state_.BindFb(GL_FRAMEBUFFER, s.fbo);
    glGenTextures(1, &s.tex);
    state_.ActiveTexture(GL_TEXTURE0);
    state_.BindTex2D(s.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(s.desc.width),
                 static_cast<GLsizei>(s.desc.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    state_.TexParami(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    state_.TexParami(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.tex, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[gl] framebuffer incomplete: 0x%x\n", status);
        state_.BindFb(GL_FRAMEBUFFER, 0);
        return false;
    }
    state_.Disable(GL_SCISSOR_TEST);
    state_.ColorMask(true, true, true, true);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    state_.BindTex2D(0);
    state_.BindFb(GL_FRAMEBUFFER, 0);
    return true;
}

SurfaceId GlDevice::CreateSurface(const SurfaceDesc& desc) {
    if (desc.width == 0 || desc.height == 0) return SurfaceId{};
    GlSurface s;
    s.desc = desc;
    if (s.desc.scale == 0) s.desc.scale = settings_.scale;
    state_.ActiveTexture(GL_TEXTURE0);
    if (desc.format == SurfaceFormat::DepthStencil24_8) {
        glGenRenderbuffers(1, &s.rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, s.rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, static_cast<GLsizei>(desc.width),
                              static_cast<GLsizei>(desc.height));
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    } else if (desc.format == SurfaceFormat::Depth32f) {
        glGenTextures(1, &s.tex);
        state_.BindTex2D(s.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, static_cast<GLsizei>(desc.width),
                     static_cast<GLsizei>(desc.height), 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        state_.TexParami(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        state_.TexParami(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        state_.TexParami(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        state_.TexParami(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        state_.BindTex2D(0);
    } else if (desc.renderable) {
        if (!BuildRenderable(s)) { ReleaseSurface(s); return SurfaceId{}; }
    } else {
        // The pool holds level-0 RGBA8 storage only (OLD PoolAcquireTexture is
        // RGBA8-only); an R8 surface is created and destroyed directly so pooled
        // storage never changes internal format.
        const bool poolable = desc.format == SurfaceFormat::Rgba8;
        bool idle = false;
        if (poolable)
            s.tex = pool_.Acquire(static_cast<int>(desc.width), static_cast<int>(desc.height),
                                  &idle);
        s.pooled = s.tex != 0;
        s.respecify = s.pooled && !idle;
        const GLenum fmt = desc.format == SurfaceFormat::R8 ? GL_R8 : GL_RGBA8;
        const GLenum ext = desc.format == SurfaceFormat::R8 ? GL_RED : GL_RGBA;
        if (!s.tex) {
            glGenTextures(1, &s.tex);
            state_.BindTex2D(s.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, static_cast<GLsizei>(desc.width),
                         static_cast<GLsizei>(desc.height), 0, ext, GL_UNSIGNED_BYTE, nullptr);
        } else {
            state_.BindTex2D(s.tex);
        }
        state_.TexParami(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        state_.TexParami(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        state_.TexParami(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        state_.TexParami(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        state_.BindTex2D(0);
    }
    const uint32_t id = next_surface_++;
    surfaces_[id] = s;
    return SurfaceId{id};
}

void GlDevice::ReleaseSurface(GlSurface& s) {
    if (s.fbo) { state_.DeleteFramebuffers(1, &s.fbo); s.fbo = 0; }
    if (s.rbo) {
        if (sten_fbo_rbo_ == s.rbo) sten_fbo_rbo_ = 0;
        if (res_fbo_rbo_ == s.rbo) res_fbo_rbo_ = 0;
        glDeleteRenderbuffers(1, &s.rbo);
        s.rbo = 0;
    }
    if (s.tex && res_fbo_tex_ == s.tex) res_fbo_tex_ = 0;
    if (s.tex) {
        // Only RGBA8 storage is ever retired to the pool (see CreateSurface).
        const bool rgba8 = s.desc.format == SurfaceFormat::Rgba8;
        if (rgba8 && (s.pooled || (!s.desc.renderable && s.levels == 0))) {
            pool_.Retire(s.tex, static_cast<int>(s.desc.width), static_cast<int>(s.desc.height),
                         state_);
        } else {
            state_.DeleteTextures(1, &s.tex);
        }
        s.tex = 0;
    }
}

void GlDevice::DestroySurface(SurfaceId id) {
    auto it = surfaces_.find(id.id);
    if (it == surfaces_.end()) return;
    ReleaseSurface(it->second);
    surfaces_.erase(it);
}

void GlDevice::Clear(SurfaceId color, SurfaceId depth, uint32_t rgba, float z, uint32_t flags) {
    const GlSurface* c = Surface(color);
    const GlSurface* d = Surface(depth);
    if (!c || !c->fbo) return;
    BindTarget(color, depth);
    state_.Disable(GL_SCISSOR_TEST);
    GLbitfield bits = 0;
    if (flags & kClearColor) {
        state_.ColorMask(true, true, true, true);
        glClearColor(((rgba >> 0) & 0xFF) / 255.0f, ((rgba >> 8) & 0xFF) / 255.0f,
                     ((rgba >> 16) & 0xFF) / 255.0f, ((rgba >> 24) & 0xFF) / 255.0f);
        bits |= GL_COLOR_BUFFER_BIT;
    }
    if ((flags & kClearDepth) && d) {
        state_.DepthMask(true);
        glClearDepth(static_cast<double>(z));
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    if (bits) { census_.clears++; glClear(bits); }
}

void GlDevice::Upload(SurfaceId id, const Rect& rect, const void* bytes, size_t stride) {
    auto it = surfaces_.find(id.id);
    if (it == surfaces_.end() || !bytes || rect.w <= 0 || rect.h <= 0) return;
    GlSurface& s = it->second;
    const bool depth = s.desc.format == SurfaceFormat::Depth32f;
    if (depth && (rect.x < 0 || rect.y < 0 ||
                  uint64_t(rect.x) + uint32_t(rect.w) > s.desc.width ||
                  uint64_t(rect.y) + uint32_t(rect.h) > s.desc.height ||
                  (stride && (stride % 4 || stride / 4 < uint32_t(rect.w) ||
                              stride / 4 > size_t(std::numeric_limits<GLint>::max())))))
        return;
    const GLenum ext = depth ? GL_DEPTH_COMPONENT :
        (s.desc.format == SurfaceFormat::R8 ? GL_RED : GL_RGBA);
    const GLenum type = depth ? GL_FLOAT : GL_UNSIGNED_BYTE;
    const size_t bpp = s.desc.format == SurfaceFormat::R8 ? 1 : 4;
    state_.ActiveTexture(GL_TEXTURE0);
    state_.BindTex2D(s.tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride ? static_cast<GLint>(stride / bpp) : 0);
    const bool full = rect.x == 0 && rect.y == 0 && rect.w == static_cast<int32_t>(s.desc.width) &&
                      rect.h == static_cast<int32_t>(s.desc.height);
    if (s.respecify && full && s.desc.format == SurfaceFormat::Rgba8) {
        // busy pooled storage (always RGBA8): re-specifying with data orphans the old
        // allocation
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rect.w, rect.h, 0, ext, GL_UNSIGNED_BYTE, bytes);
        s.respecify = false;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x, rect.y, rect.w, rect.h, ext, type,
                        bytes);
    }
    census_.uploads++;
    census_.upload_bytes += static_cast<uint64_t>(rect.w) * static_cast<uint64_t>(rect.h) * bpp;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    state_.BindTex2D(0);
}

void GlDevice::UploadLevel(SurfaceId id, int level, int w, int h, const void* rgba) {
    auto it = surfaces_.find(id.id);
    if (it == surfaces_.end() || level <= 0 || w <= 0 || h <= 0) return;
    state_.ActiveTexture(GL_TEXTURE0);
    state_.BindTex2D(it->second.tex);
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    census_.uploads++;
    census_.upload_bytes += static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * 4u;
    state_.BindTex2D(0);
}

// Sets GL_TEXTURE_MAX_LEVEL to the authored count, optionally extending the chain
// by box-averaging the last authored level down to 1x1 so the shader's LOD floor
// always finds a level (the retail LOD term is clamped at the authored count).
void GlDevice::FinishMipChain(SurfaceId id, int authored_levels, bool synthesize_tail) {
    auto it = surfaces_.find(id.id);
    if (it == surfaces_.end() || authored_levels <= 0) return;
    GlSurface& s = it->second;
    state_.ActiveTexture(GL_TEXTURE0);
    state_.BindTex2D(s.tex);
    int syn = 0;
    if (synthesize_tail) {
        GLint cw = 0, ch = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, authored_levels, GL_TEXTURE_WIDTH, &cw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, authored_levels, GL_TEXTURE_HEIGHT, &ch);
        if (cw > 0 && ch > 0) {
            std::vector<uint8_t> cur(static_cast<size_t>(cw) * ch * 4);
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            state_.BindFb(GL_READ_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.tex,
                                   authored_levels);
            census_.readbacks++;
            census_.readback_bytes += cur.size();
            glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, cur.data());
            state_.BindFb(GL_READ_FRAMEBUFFER, 0);
            state_.DeleteFramebuffers(1, &fbo);
            std::vector<uint8_t> nxt;
            while (cw > 1 || ch > 1) {
                int nw = 0, nh = 0;
                GlTexturePool::BoxHalve(cur, cw, ch, nxt, &nw, &nh);
                glTexImage2D(GL_TEXTURE_2D, authored_levels + syn + 1, GL_RGBA8, nw, nh, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, nxt.data());
                syn++;
                cur.swap(nxt);
                cw = nw;
                ch = nh;
            }
        }
    }
    state_.TexParami(GL_TEXTURE_MAX_LEVEL, authored_levels + syn);
    state_.BindTex2D(0);
    s.levels = static_cast<uint8_t>(authored_levels);
    s.synth_levels = static_cast<uint8_t>(syn);
}

uint32_t GlDevice::BindTarget(SurfaceId color, SurfaceId depth) {
    auto it = surfaces_.find(color.id);
    if (it == surfaces_.end() || !it->second.fbo) return 0;
    GlSurface& c = it->second;
    const GlSurface* d = Surface(depth);
    const uint32_t dtex = d ? d->tex : 0;
    state_.BindFb(GL_FRAMEBUFFER, c.fbo);
    if (c.attached_depth != dtex) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, dtex, 0);
        c.attached_depth = dtex;
    }
    return c.fbo;
}

uint32_t GlDevice::BindStencilTarget(SurfaceId color, SurfaceId stencil) {
    const GlSurface* st = Surface(stencil);
    if (!st || !st->rbo) return 0;
    const GlSurface* c = Surface(color);
    if (color.id != 0 && (!c || !c->tex)) return 0;
    if (color.id == 0) {
        if (!sten_fbo_) glGenFramebuffers(1, &sten_fbo_);
        state_.BindFb(GL_FRAMEBUFFER, sten_fbo_);
        if (sten_fbo_rbo_ != st->rbo) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, st->rbo);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
            sten_fbo_rbo_ = st->rbo;
        }
        return sten_fbo_;
    }
    if (!res_fbo_) glGenFramebuffers(1, &res_fbo_);
    state_.BindFb(GL_FRAMEBUFFER, res_fbo_);
    if (res_fbo_tex_ != c->tex || res_fbo_rbo_ != st->rbo) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->tex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, st->rbo);
        const GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, bufs);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 0;
        res_fbo_tex_ = c->tex;
        res_fbo_rbo_ = st->rbo;
    }
    return res_fbo_;
}

void GlDevice::ApplyStencil(const StencilState& st) {
    if (!st.enabled) { glDisable(GL_STENCIL_TEST); return; }
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(st.func == StencilState::kEqual ? GL_EQUAL : GL_ALWAYS, st.ref, st.cmp_mask);
    glStencilMask(st.write_mask);
    GLenum op = GL_KEEP;
    switch (st.pass_op) {
        case StencilState::kInvert: op = GL_INVERT; break;
        case StencilState::kIncrWrap: op = GL_INCR_WRAP; break;
        case StencilState::kDecrWrap: op = GL_DECR_WRAP; break;
        default: break;
    }
    glStencilOp(GL_KEEP, GL_KEEP, op);
}

void GlDevice::ResetStencil() {
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
}

void GlDevice::ClearStencil(SurfaceId stencil, uint8_t value) {
    if (!BindStencilTarget(SurfaceId{}, stencil)) return;
    state_.Disable(GL_SCISSOR_TEST);
    glStencilMask(0xFF);
    glClearStencil(value);
    census_.clears++;
    glClear(GL_STENCIL_BUFFER_BIT);
}

// Constant colour over `scissor` under the stencil test: viewport = the stencil
// surface, blend off, depth ALWAYS without writes, colour writes only with a colour
// attachment (alpha per `write_alpha`); the const program over the NDC triangle.
void GlDevice::DrawConstQuad(SurfaceId color, SurfaceId stencil, const Rect& scissor, uint32_t rgba,
                             bool write_alpha, const StencilState& st) {
    const GlSurface* ss = Surface(stencil);
    if (!ss || scissor.w <= 0 || scissor.h <= 0) return;
    if (!BindStencilTarget(color, stencil)) return;
    EndDrawRun();
    state_.Viewport(0, 0, static_cast<int>(ss->desc.width), static_cast<int>(ss->desc.height));
    state_.Enable(GL_SCISSOR_TEST);
    state_.Scissor(scissor.x, scissor.y, scissor.w, scissor.h);
    state_.Disable(GL_BLEND);
    state_.Enable(GL_DEPTH_TEST);
    state_.DepthFunc(GL_ALWAYS);
    state_.DepthMask(false);
    const bool cw = color.id != 0;
    state_.ColorMask(cw, cw, cw, cw && write_alpha);
    state_.UseProgram(shaders_.ConstProgram());
    glUniform4f(shaders_.StencilLocations().const_color, ((rgba >> 0) & 0xFF) / 255.0f,
                ((rgba >> 8) & 0xFF) / 255.0f, ((rgba >> 16) & 0xFF) / 255.0f,
                ((rgba >> 24) & 0xFF) / 255.0f);
    state_.BindVertexArray(const_vao_);
    ApplyStencil(st);
    census_.draw_calls++;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ResetStencil();
    state_.BindVertexArray(0);
    state_.UseProgram(0);
    state_.ColorMask(true, true, true, true);
    state_.DepthMask(true);
    state_.Disable(GL_SCISSOR_TEST);
}

DrawResult GlDevice::Draw(const gs::DrawIr& ir, const gs::TextureSource& tex,
                          const DrawParams& params) {
    if (!executor_) return DrawResult{};
    return executor_->Draw(ir, tex, params);
}

// kCopyPlain: glCopyTexSubImage2D from the source attachment into the destination
// texture. kCopyPack5551: the pack program over the NDC triangle into the
// destination's framebuffer, scissored to dst_rect, sampling the source at the
// same physical texel (the rects coincide for the companion refresh; a shifted
// rect falls back to the plain copy -- the pass has no offset).
void GlDevice::CopyRect(SurfaceId src, const Rect& src_rect, SurfaceId dst, const Rect& dst_rect,
                        uint32_t transform) {
    const GlSurface* s = Surface(src);
    const GlSurface* d = Surface(dst);
    if (!s || !d || !s->fbo || src_rect.w <= 0 || src_rect.h <= 0) return;
    if (transform == kCopyPack5551 && d->fbo && s->tex && shaders_.Pack5551Program() &&
        src_rect.x == dst_rect.x && src_rect.y == dst_rect.y) {
        EndDrawRun();
        state_.BindFb(GL_FRAMEBUFFER, d->fbo);
        state_.Viewport(0, 0, static_cast<int>(d->desc.width), static_cast<int>(d->desc.height));
        state_.Enable(GL_SCISSOR_TEST);
        state_.Scissor(dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h);
        state_.Disable(GL_BLEND);
        state_.Disable(GL_DEPTH_TEST);
        state_.DepthMask(false);
        state_.ColorMask(true, true, true, true);
        state_.ActiveTexture(GL_TEXTURE0);
        state_.BindTex2D(s->tex);
        state_.UseProgram(shaders_.Pack5551Program());
        state_.BindVertexArray(const_vao_);
        census_.draw_calls++;
        glDrawArrays(GL_TRIANGLES, 0, 3);
        state_.BindVertexArray(0);
        state_.UseProgram(0);
        state_.BindTex2D(0);
        state_.DepthMask(true);
        state_.Enable(GL_DEPTH_TEST);
        state_.Disable(GL_SCISSOR_TEST);
        return;
    }
    state_.BindFb(GL_READ_FRAMEBUFFER, s->fbo);
    state_.ActiveTexture(GL_TEXTURE0);
    state_.BindTex2D(d->tex);
    census_.copies++;
    census_.copy_bytes += static_cast<uint64_t>(src_rect.w) * static_cast<uint64_t>(src_rect.h) * 4u;
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, dst_rect.x, dst_rect.y, src_rect.x, src_rect.y, src_rect.w,
                        src_rect.h);
    state_.BindTex2D(0);
}

// The alpha-lane program over the NDC triangle into the destination's framebuffer,
// scissored to dst_rect, colour mask A only; the lane on unit 0.
void GlDevice::WriteAlphaLane(SurfaceId dst, const Rect& dst_rect, SurfaceId lane, const Rect& exclude) {
    const GlSurface* d = Surface(dst);
    const GlSurface* l = Surface(lane);
    if (!d || !l || !d->fbo || !l->tex || dst_rect.w <= 0 || dst_rect.h <= 0 || !shaders_.AlphaLaneProgram()) return;
    EndDrawRun();
    const GlStencilLocations& L = shaders_.StencilLocations();
    state_.BindFb(GL_FRAMEBUFFER, d->fbo);
    state_.Viewport(0, 0, static_cast<int>(d->desc.width), static_cast<int>(d->desc.height));
    state_.Enable(GL_SCISSOR_TEST);
    state_.Scissor(dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h);
    state_.Disable(GL_BLEND);
    state_.Disable(GL_DEPTH_TEST);
    state_.DepthMask(false);
    state_.ColorMask(false, false, false, true);
    state_.ActiveTexture(GL_TEXTURE0);
    state_.BindTex2D(l->tex);
    state_.UseProgram(shaders_.AlphaLaneProgram());
    if (L.lane_origin >= 0) glUniform2i(L.lane_origin, dst_rect.x, dst_rect.y);
    if (L.lane_scale >= 0) glUniform1i(L.lane_scale, std::max<int>(1, d->desc.scale));
    const bool excl = exclude.w > 0 && exclude.h > 0;
    if (L.lane_exclude >= 0)
        glUniform4i(L.lane_exclude, excl ? exclude.x : 0, excl ? exclude.y : 0, excl ? exclude.x + exclude.w : 0,
                    excl ? exclude.y + exclude.h : 0);
    state_.BindVertexArray(const_vao_);
    census_.draw_calls++;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    state_.BindVertexArray(0);
    state_.UseProgram(0);
    state_.BindTex2D(0);
    state_.ColorMask(true, true, true, true);
    state_.DepthMask(true);
    state_.Enable(GL_DEPTH_TEST);
    state_.Disable(GL_SCISSOR_TEST);
}

// Rows bottom-up (attachment order); colour RGBA8, depth float.
void GlDevice::Readback(SurfaceId id, const Rect& rect, void* out, size_t stride) {
    const GlSurface* s = Surface(id);
    if (!s || !out || rect.w <= 0 || rect.h <= 0) return;
    GLuint fbo = s->fbo;
    GLuint tmp = 0;
    const bool depth = s->desc.format == SurfaceFormat::Depth32f;
    if (!fbo) {
        glGenFramebuffers(1, &tmp);
        fbo = tmp;
        state_.BindFb(GL_READ_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, s->tex, 0);
    } else {
        state_.BindFb(GL_READ_FRAMEBUFFER, fbo);
    }
    const size_t bpp = depth ? 4 : 4;
    glPixelStorei(GL_PACK_ROW_LENGTH, stride ? static_cast<GLint>(stride / bpp) : 0);
    census_.readbacks++;
    census_.readback_bytes += static_cast<uint64_t>(rect.w) * static_cast<uint64_t>(rect.h) * bpp;
    if (depth)
        glReadPixels(rect.x, rect.y, rect.w, rect.h, GL_DEPTH_COMPONENT, GL_FLOAT, out);
    else
        glReadPixels(rect.x, rect.y, rect.w, rect.h, GL_RGBA, GL_UNSIGNED_BYTE, out);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    state_.BindFb(GL_READ_FRAMEBUFFER, 0);
    if (tmp) state_.DeleteFramebuffers(1, &tmp);
}

// The asynchronous colour readback: glReadPixels into a pixel-pack buffer (the
// GPU writes it when it gets there; nothing drains here), a fence after it.
// Depth surfaces take the synchronous path into a pack buffer the same way.
FenceId GlDevice::BeginReadback(SurfaceId id, const Rect& rect) {
    const GlSurface* s = Surface(id);
    if (!s || rect.w <= 0 || rect.h <= 0) return FenceId{};
    const bool depth = s->desc.format == SurfaceFormat::Depth32f;
    GLuint fbo = s->fbo;
    GLuint tmp = 0;
    if (!fbo) {
        glGenFramebuffers(1, &tmp);
        fbo = tmp;
        state_.BindFb(GL_READ_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, s->tex, 0);
    } else {
        state_.BindFb(GL_READ_FRAMEBUFFER, fbo);
    }
    PendingReadback pr;
    pr.stride = static_cast<size_t>(rect.w) * 4;
    pr.rows = rect.h;
    if (!free_pbos_.empty()) {
        pr.pbo = free_pbos_.back();
        free_pbos_.pop_back();
    } else {
        GLuint b = 0;
        glGenBuffers(1, &b);
        pr.pbo = b;
    }
    const size_t bytes = pr.stride * static_cast<size_t>(rect.h);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pr.pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_READ);   // orphan: no stall on reuse
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    census_.readbacks++;
    census_.readback_bytes += bytes;
    if (depth)
        glReadPixels(rect.x, rect.y, rect.w, rect.h, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    else
        glReadPixels(rect.x, rect.y, rect.w, rect.h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    pr.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    state_.BindFb(GL_READ_FRAMEBUFFER, 0);
    if (tmp) state_.DeleteFramebuffers(1, &tmp);
    const uint64_t f = next_fence_++;
    readbacks_[f] = pr;
    return FenceId{f};
}

bool GlDevice::ReadbackReady(FenceId fence) {
    auto it = readbacks_.find(fence.id);
    if (it == readbacks_.end()) return false;
    return !it->second.sync || ReadbackReadySync(it->second.sync);
}

bool GlDevice::PollReadback(FenceId fence, void* out, size_t stride) {
    auto it = readbacks_.find(fence.id);
    if (it == readbacks_.end()) return false;
    PendingReadback pr = it->second;
    readbacks_.erase(it);
    if (pr.sync) {
        GLsync sync = static_cast<GLsync>(pr.sync);
        if (!ReadbackReadySync(sync)) {
            readback_waits_++;
            census_.readback_waits++;
            // The commands may still sit in the client queue: flush them with the wait.
            glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 5000000000ull);
        }
        glDeleteSync(sync);
    }
    if (out) {
        const size_t dst_stride = stride ? stride : pr.stride;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pr.pbo);
        const size_t bytes = pr.stride * static_cast<size_t>(pr.rows);
        if (const void* src = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT)) {
            for (int r = 0; r < pr.rows; r++)
                std::memcpy(static_cast<uint8_t*>(out) + dst_stride * r,
                            static_cast<const uint8_t*>(src) + pr.stride * r, std::min(pr.stride, dst_stride));
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    free_pbos_.push_back(pr.pbo);
    return true;
}

bool GlDevice::ReadbackReadySync(void* sync) {
    GLint status = GL_UNSIGNALED;
    GLsizei n = 0;
    glGetSynciv(static_cast<GLsync>(sync), GL_SYNC_STATUS, 1, &n, &status);
    return status == GL_SIGNALED;
}

void GlDevice::ReleaseReadbacks() {
    for (auto& kv : readbacks_) {
        if (kv.second.sync) glDeleteSync(static_cast<GLsync>(kv.second.sync));
        if (kv.second.pbo) { GLuint b = kv.second.pbo; glDeleteBuffers(1, &b); }
    }
    readbacks_.clear();
    for (uint32_t b : free_pbos_) { GLuint g = b; glDeleteBuffers(1, &g); }
    free_pbos_.clear();
}

// Textured quad of the source into the default framebuffer, then swap.
void GlDevice::Present(const ScanoutDesc& desc) {
    if (desc.filter != PresentationFilter::Surface && desc.filter != PresentationFilter::Nearest &&
        desc.filter != PresentationFilter::Linear)
        throw std::invalid_argument("Graphics presentation filter is unsupported");
    const GlSurface* s = Surface(desc.source);
    Rect source = desc.src;
    float u0 = 0.f, u1 = 1.f, v0 = 0.f, v1 = 1.f;
    if (desc.source.id) {
        if (!s || !s->tex || !s->desc.width || !s->desc.height ||
            s->desc.width > uint32_t(INT32_MAX) || s->desc.height > uint32_t(INT32_MAX))
            throw std::invalid_argument("Graphics presentation source is unavailable");
        // An entirely unspecified source retains the existing full-image API.
        // Every explicit rectangle must fit before ending work or clearing the
        // window. Subtraction avoids overflow in malformed origin + extent.
        if (!source.x && !source.y && !source.w && !source.h)
            source = {0,0,int32_t(s->desc.width),int32_t(s->desc.height)};
        if (source.x < 0 || source.y < 0 || source.w <= 0 || source.h <= 0 ||
            uint32_t(source.x) >= s->desc.width || uint32_t(source.y) >= s->desc.height ||
            uint32_t(source.w) > s->desc.width-uint32_t(source.x) ||
            uint32_t(source.h) > s->desc.height-uint32_t(source.y))
            throw std::invalid_argument("Graphics presentation crop exceeds its source");
        u0 = float(source.x)/float(s->desc.width);
        u1 = float(source.x+source.w)/float(s->desc.width);
        v0 = float(source.y)/float(s->desc.height);
        v1 = float(source.y+source.h)/float(s->desc.height);
        if (!desc.flip_y) std::swap(v0,v1);
    }
    EndDrawRun();
    // An armed capture draws the window image into an owned framebuffer of the
    // window's size (the default framebuffer of a hidden window is not the
    // window's size, and its pixels are not readable), reads it back, and
    // mirrors it onto the window. Unarmed, the window is drawn directly.
    const int capture_w = window_w_ > 0 ? window_w_ : desc.dst.w;
    const int capture_h = window_h_ > 0 ? window_h_ : desc.dst.h;
    const bool capture = present_capture_ && capture_w > 0 && capture_h > 0 && CaptureTarget(capture_w, capture_h);
    state_.BindFb(GL_FRAMEBUFFER, capture ? capture_fbo_ : 0);
    state_.Disable(GL_SCISSOR_TEST);
    state_.ColorMask(true, true, true, true);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    census_.clears++;
    glClear(GL_COLOR_BUFFER_BIT);
    if (s && s->tex) {
        const int vw = desc.dst.w > 0 ? desc.dst.w : window_w_;
        const int vh = desc.dst.h > 0 ? desc.dst.h : window_h_;
        state_.Viewport(desc.dst.x, desc.dst.y, vw, vh);
        state_.UseProgram(shaders_.ScanoutProgram());
        state_.ActiveTexture(GL_TEXTURE0);
        state_.BindTex2D(s->tex);
        const uint32_t sampler = desc.filter == PresentationFilter::Linear ? present_linear_ :
            desc.filter == PresentationFilter::Nearest ? present_nearest_ : 0;
        glBindSampler(0,sampler);
        glUniform1i(shaders_.ScanoutTextureLocation(), 0);
        state_.Disable(GL_BLEND);
        state_.Disable(GL_DEPTH_TEST);
        const float quad[]{
            -1.f,-1.f,u0,v1, 1.f,-1.f,u1,v1, -1.f,1.f,u0,v0,
             1.f,-1.f,u1,v1, 1.f, 1.f,u1,v0, -1.f,1.f,u0,v0,
        };
        state_.BindArrayBuffer(scanout_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(quad),quad);
        state_.BindVertexArray(scanout_vao_);
        census_.draw_calls++;
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindSampler(0,0); // the next draw observes only its own texture parameters
        state_.BindVertexArray(0);
        state_.UseProgram(0);
        state_.Enable(GL_DEPTH_TEST);
    }
    if (capture && present_capture_deferred_) {
        // The previous present's pixels first (its fence is long signalled: no
        // drain), then this present's read into the pack buffer behind a new fence.
        CompleteDeferredCapture(/*wait=*/true);
        DeferredCapture& dc = deferred_capture_;
        if (!dc.pbo) glGenBuffers(1, &dc.pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, dc.pbo);
        const size_t bytes = static_cast<size_t>(capture_w) * capture_h * 4;
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_READ);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        census_.readbacks++;
        census_.readback_bytes += bytes;
        glReadPixels(0, 0, capture_w, capture_h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        dc.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        dc.w = capture_w;
        dc.h = capture_h;
        dc.pending = true;
        state_.BindFb(GL_READ_FRAMEBUFFER, capture_fbo_);
        state_.BindFb(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, capture_w, capture_h, 0, 0, capture_w, capture_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        state_.BindFb(GL_FRAMEBUFFER, 0);
    } else if (capture) {
        presented_w_ = presented_h_ = 0;
        // The frame dump's presented image: the whole window as drawn above
        // (the display quad at its offset, black around it), bottom-up.
        presented_.assign(static_cast<size_t>(capture_w) * capture_h * 4, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        census_.readbacks++;
        census_.readback_bytes += presented_.size();
        glReadPixels(0, 0, capture_w, capture_h, GL_RGBA, GL_UNSIGNED_BYTE, presented_.data());
        presented_w_ = capture_w;
        presented_h_ = capture_h;
        state_.BindFb(GL_READ_FRAMEBUFFER, capture_fbo_);
        state_.BindFb(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, capture_w, capture_h, 0, 0, capture_w, capture_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        state_.BindFb(GL_FRAMEBUFFER, 0);
    } else {
        presented_w_ = presented_h_ = 0;
    }
    if (swap_) swap_();
}

void GlDevice::SetPresentCaptureDeferred(bool deferred) {
    if (present_capture_deferred_ && !deferred) CompleteDeferredCapture(/*wait=*/true);
    present_capture_deferred_ = deferred;
}

// The pending read's pixels into presented_ (the image ReadPresented hands over).
// `wait`: block on the fence when it has not signalled (counted); otherwise leave
// an unsignalled read pending.
void GlDevice::CompleteDeferredCapture(bool wait) {
    DeferredCapture& dc = deferred_capture_;
    if (!dc.pending) return;
    if (dc.sync) {
        GLenum r = glClientWaitSync(static_cast<GLsync>(dc.sync), 0, 0);
        if (r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED) {
            if (!wait) return;
            present_capture_waits_++;
            r = glClientWaitSync(static_cast<GLsync>(dc.sync), GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        }
        glDeleteSync(static_cast<GLsync>(dc.sync));
        dc.sync = nullptr;
    }
    dc.pending = false;
    const size_t bytes = static_cast<size_t>(dc.w) * dc.h * 4;
    presented_.resize(bytes);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, dc.pbo);
    if (const void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT)) {
        std::memcpy(presented_.data(), p, bytes);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        presented_w_ = dc.w;
        presented_h_ = dc.h;
    } else {
        presented_w_ = presented_h_ = 0;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void GlDevice::ReleaseDeferredCapture() {
    DeferredCapture& dc = deferred_capture_;
    if (dc.sync) { glDeleteSync(static_cast<GLsync>(dc.sync)); dc.sync = nullptr; }
    if (dc.pbo) { glDeleteBuffers(1, &dc.pbo); dc.pbo = 0; }
    dc.pending = false;
}

bool GlDevice::CaptureTarget(int w, int h) {
    if (capture_fbo_ && capture_w_ == w && capture_h_ == h) return true;
    if (!capture_tex_) glGenTextures(1, &capture_tex_);
    state_.ActiveTexture(GL_TEXTURE0);
    state_.BindTex2D(capture_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    if (!capture_fbo_) glGenFramebuffers(1, &capture_fbo_);
    state_.BindFb(GL_FRAMEBUFFER, capture_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, capture_tex_, 0);
    const GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, bufs);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    state_.BindFb(GL_FRAMEBUFFER, 0);
    capture_w_ = complete ? w : 0;
    capture_h_ = complete ? h : 0;
    return complete;
}

void GlDevice::ArmPresentCapture(bool armed) {
    present_capture_ = armed;
    if (!armed) {
        presented_.clear();
        presented_w_ = presented_h_ = 0;
    }
}

bool GlDevice::ReadPresented(std::vector<uint8_t>* rgba, int* w, int* h) {
    if (!present_capture_ || presented_w_ <= 0 || presented_h_ <= 0 || !rgba || !w || !h) return false;
    *rgba = presented_;
    *w = presented_w_;
    *h = presented_h_;
    return true;
}

void GlDevice::SetWindowSize(int w, int h) {
    window_w_ = w;
    window_h_ = h;
}

void GlDevice::FlushCommands() { glFlush(); }

void GlDevice::BeginFrame() { BeginDrawRun(); }
void GlDevice::EndFrame() { EndDrawRun(); }
void GlDevice::BeginDrawRun() { vertices_.BeginDrawRun(); }
void GlDevice::EndDrawRun() { vertices_.EndDrawRun(state_); }

}  // namespace render
}  // namespace ps2r
