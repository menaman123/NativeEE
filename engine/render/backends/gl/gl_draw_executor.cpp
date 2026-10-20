// render/backends/gl/gl_draw_executor.cpp -- GlDrawExecutor implementation.
#include "engine/render/backends/gl/gl_draw_executor.h"

#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

#include <algorithm>
#include <cmath>

#include "engine/gs/hle/ir/alpha_fail_mask.h"
#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/render/backends/gl/blend_ff_map.h"
#include "engine/render/backends/gl/cd_copy_policy.h"
#include "engine/render/backends/gl/gl_device.h"
#include "engine/render/backends/gl/vertex_convert.h"

namespace ps2r {
namespace render {

namespace {

bool Is16BitFrame(uint8_t psm) { return psm == 0x02 || psm == 0x0A; }

float SnapColumn8(float u) { return std::nearbyint(u / 8.0f) * 8.0f; }
float SnapRow(float v) {
    const float s = std::nearbyint(v / 8.0f) * 8.0f;
    return s + std::nearbyint(v - s);
}

}  // namespace

GlDrawExecutor::GlDrawExecutor(GlDevice& device) : dev_(device) {}

// FRAME.FBMSK is an RGBA8-layout mask for every frame format; 16-bit frames use the
// top 5 bits of each channel and bit 31 for A, folded to R[0:4] G[5:9] B[10:14] A[15]
// (one definition, in the ir library).
uint32_t GlDrawExecutor::Fbmsk16From32(uint32_t m) {
    return gs::HalfwordShuffle::Fbmsk16From32(m);
}

// A 5551 mask whose R/G/B fields are each all-0 or all-1 is expressible as a
// per-channel colour mask; anything else must merge bit-exactly in the shader.
bool GlDrawExecutor::Fbmsk16Exact(uint32_t fbmsk16) {
    auto aligned = [](uint32_t v, uint32_t field) {
        const uint32_t f = v & field;
        return f == 0 || f == field;
    };
    const bool channel_aligned =
        aligned(fbmsk16, 0x001F) && aligned(fbmsk16, 0x03E0) && aligned(fbmsk16, 0x7C00);
    return !channel_aligned && fbmsk16 != 0;
}

// Blended reads of another target whose ALPHA maps EXACTLY to fixed-function blending
// (the eight combos below) skip the destination copy; every other Cd-reading form,
// C = Ad, PABE, COLCLAMP=0, exact 5551 masks, DATE and FBA stay on the shader blend.
bool GlDrawExecutor::Fbmsk32Partial(uint32_t fbmsk) {
    for (int i = 0; i < 4; ++i) {
        const uint32_t byte = (fbmsk >> (8 * i)) & 0xFFu;
        if (byte != 0x00u && byte != 0xFFu) return true;
    }
    return false;
}

bool GlDrawExecutor::RouteRtBlend(const gs::DrawIr& ir, bool using_rt, bool fbmsk16_exact) {
    const auto& r = ir.raster;
    if (!(ir.alpha_blend && using_rt && !r.fba && !fbmsk16_exact && !r.test.date && !r.pabe &&
          r.colclamp))
        return false;
    const uint8_t a = r.alpha.a, b = r.alpha.b, c = r.alpha.c, d = r.alpha.d;
    auto is = [&](uint8_t A, uint8_t B, uint8_t C, uint8_t D) {
        return a == A && b == B && c == C && d == D;
    };
    return is(0, 1, 0, 1) || is(0, 2, 0, 1) || is(0, 2, 0, 2) || is(0, 1, 2, 1) ||
           is(0, 2, 2, 1) || is(1, 0, 0, 0) || is(0, 2, 2, 2) || is(2, 1, 0, 1);
}

// TEX1 MMAG/MMIN -> GL filters. With mip levels uploaded the trilinear forms are
// used for MMIN 2/4 as well (the shader selects the LOD); without levels the mip
// modes fall back to their base filter.
void GlDrawExecutor::SelectFilters(bool mag_linear, uint8_t mmin, uint8_t levels, uint32_t* mag,
                                   uint32_t* min) {
    *mag = mag_linear ? GL_LINEAR : GL_NEAREST;
    if (levels > 0) {
        switch (mmin) {
            case 0: *min = GL_NEAREST; break;
            case 1: *min = GL_LINEAR; break;
            case 2: *min = GL_NEAREST_MIPMAP_LINEAR; break;
            case 3: *min = GL_NEAREST_MIPMAP_LINEAR; break;
            case 4: *min = GL_LINEAR_MIPMAP_LINEAR; break;
            case 5: *min = GL_LINEAR_MIPMAP_LINEAR; break;
            default: *min = GL_NEAREST; break;
        }
    } else {
        switch (mmin) {
            case 1: case 4: case 5: *min = GL_LINEAR; break;
            default: *min = GL_NEAREST; break;
        }
    }
}

bool GlDrawExecutor::EnhanceFilters(TextureFilter filter, int anisotropy, int max_anisotropy,
                                    bool title_mips, uint8_t levels, uint32_t* mag, uint32_t* min,
                                    int* applied_anisotropy) {
    const int aniso = std::min(TextureFilterSelection::ClampAnisotropy(anisotropy), std::max(1, max_anisotropy));
    *applied_anisotropy = aniso;
    const bool chain = levels > 0;
    const bool hw_lod = chain && (aniso > 1 || filter == TextureFilter::Trilinear);
    const bool mips = chain && (title_mips || hw_lod);
    switch (filter) {
        case TextureFilter::Title:
            break;
        case TextureFilter::Point:
            *mag = GL_NEAREST;
            *min = mips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
            break;
        case TextureFilter::Bilinear:
            *mag = GL_LINEAR;
            *min = mips ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR;
            break;
        case TextureFilter::Trilinear:
            *mag = GL_LINEAR;
            *min = chain ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
            break;
    }
    return hw_lod;
}

DrawResult GlDrawExecutor::Draw(const gs::DrawIr& ir, const gs::TextureSource& tex,
                                const DrawParams& p) {
    if (p.program == StencilProgram::StripCount) return DrawStripCount(ir, tex, p);
    if (p.program == StencilProgram::Invert) return DrawInvert(ir, tex, p);
    Step s;
    DrawResult none;
    const size_t n = ir.verts.size();
    if (n < 3) return none;
    // A 16-bit FRAME over 32-bit content (the companion route) that is the texture
    // shuffle's column pass runs as an exact half-word copy over the 32-bit image.
    if (p.alt16_route) {
        gs::HalfwordShuffle::Op op;
        if (gs::HalfwordShuffle::Detect(ir, &op)) {
            const DrawResult hw = DrawHalfwordShuffle(ir, tex, p, op);
            if (hw.drew) return hw;
        }
    }
    if (!BindTarget(ir, p, s)) return none;
    FrameClear(p);
    // The alpha test's static arm (ATST NEVER / ALWAYS, ATE off): a draw that updates
    // neither buffer (all pixels fail, AFAIL KEEP) is no draw -- after the per-frame
    // clear the target layer already consumed for it; the others compose their AFAIL
    // write mask with FBMSK / ZMSK below (DepthState, ColorMaskState).
    s.afail = gs::AlphaFailMask::Compute(ir.raster.test);
    if (s.afail.WritesNothing()) return none;
    BindTextures(tex, p, s);
    if (!ConvertVertices(ir, tex, s)) return none;
    GlStateCache& st = dev_.State();
    GlUniformCache& u = dev_.Uniforms();
    const GlUniformLocations& L = dev_.Shaders().Locations();
    st.UseProgram(dev_.Shaders().Ps2Program());
    u.Set2f(L.screen_size, static_cast<float>(s.render_w), static_cast<float>(s.render_h));
    TextureUniforms(ir, tex, p, s);
    RegisterUniforms(ir, p);
    ClassifyRoutes(ir, tex, p, s);
    BlendState(ir, p, s);
    DepthState(ir, p, s);
    ScissorState(ir, s);
    dev_.Vertices().BindForDraw(st);
    s.first = dev_.Vertices().Upload(scratch_.data(), s.tri_verts);
    s.wrote_alpha = ColorMaskState(ir, s);
    // A per-pixel alpha test whose AFAIL still updates something (GS User's Manual,
    // TEST register): the passing pixels write per FBMSK / ZMSK, the failing ones per
    // AFAIL -- GL has no per-fragment depth mask, so the two populations are two
    // passes of the same geometry, the shader keeping one population each
    // (u_afail_pass) and the pass's write masks carrying its arm. The passing pass
    // runs first (it writes Z; the failing pass under FB_ONLY / RGB_ONLY does not).
    if (gs::AlphaFailMask::SplitsPopulations(ir.raster.test)) {
        u.Set1i(L.afail_pass, 1);
        dev_.CensusMut().draw_calls++;
        glDrawArrays(GL_TRIANGLES, s.first, static_cast<GLsizei>(s.tri_verts));
        Step fail = s;
        fail.afail = gs::AlphaFailMask::FailingPixels(ir.raster.test.afail);
        u.Set1i(L.afail_pass, 2);
        DepthState(ir, p, fail);
        s.wrote_alpha = ColorMaskState(ir, fail) || s.wrote_alpha;
        dev_.CensusMut().draw_calls++;
        glDrawArrays(GL_TRIANGLES, s.first, static_cast<GLsizei>(s.tri_verts));
    } else {
        dev_.CensusMut().draw_calls++;
        glDrawArrays(GL_TRIANGLES, s.first, static_cast<GLsizei>(s.tri_verts));
    }
    // Masks return to their open defaults after every draw, so any raw GL pass
    // that follows (clear, refresh, present) inherits writable colour and depth.
    st.ColorMask(true, true, true, true);
    st.DepthMask(true);
    return MakeResult(ir, p, s);
}

// The strip-count pass: geometry into the stencil-only framebuffer; the shader
// discards on DATE (unit 1 = the ROUTED accumulator image, not attached here) and
// on the colour-authored Z compare (unit 4); the caller's stencil state counts the
// survivors; no colour output. Viewport = the logical render size (the accumulator
// is native-scale by the recipe's prime rule). The per-frame clear the target
// layer consumed for this draw still lands on the COLOUR target (bound first, as
// the reference strip ladder does) before the stencil-only framebuffer is bound;
// the scissor is issued at scale 1 (the prime rule pins the accumulator native).
DrawResult GlDrawExecutor::DrawStripCount(const gs::DrawIr& ir, const gs::TextureSource& tex,
                                          const DrawParams& p) {
    Step s;
    DrawResult none;
    if (ir.verts.size() < 3) return none;
    GlStateCache& st = dev_.State();
    if (p.do_frame_clear) {
        if (!BindTarget(ir, p, s)) return none;
        FrameClear(p);
    }
    st.Disable(GL_SCISSOR_TEST);
    s.render_w = p.render_w;
    s.render_h = p.render_h;
    s.scale = 1;
    s.fbo = dev_.BindStencilTarget(SurfaceId{}, p.stencil);
    if (!s.fbo) return none;
    st.Viewport(0, 0, s.render_w, s.render_h);
    st.UseProgram(dev_.Shaders().StencilStripProgram());
    const GlStencilLocations& SL = dev_.Shaders().StencilLocations();
    glUniform2f(SL.strip_screen, static_cast<float>(s.render_w), static_cast<float>(s.render_h));
    glUniform1i(SL.strip_zsrc_en, static_cast<int>(ir.raster.test.ztst));
    glUniform1i(SL.strip_zsrc_num, std::max<int>(1, p.zsrc_scale));
    glUniform1i(SL.strip_zsrc_den, std::max<int>(1, p.scale));
    glUniform1i(SL.strip_datm, ir.raster.test.datm ? 1 : 0);
    const GlSurface* routed = dev_.Surface(p.alt16_route && p.alt16.id ? p.alt16 : p.color);
    const GlSurface* zsrc = dev_.Surface(p.zsrc);
    st.Disable(GL_BLEND);
    st.ActiveTexture(GL_TEXTURE1);
    st.BindTex2D(routed && routed->tex ? routed->tex : dev_.DummyTexture());
    st.ActiveTexture(GL_TEXTURE4);
    st.BindTex2D(zsrc && zsrc->tex ? zsrc->tex : dev_.DummyTexture());
    st.ActiveTexture(GL_TEXTURE0);
    st.Enable(GL_DEPTH_TEST);
    st.DepthFunc(GL_ALWAYS);
    st.DepthMask(false);
    ScissorState(ir, s);
    if (!ConvertVertices(ir, tex, s)) return none;
    // The strip's FBMSK still classifies the draw for the coherence tail (a masked
    // 16-bit write keeps the uploaded assets under it and records its keep mask)
    // even though no colour is written here.
    if (Is16BitFrame(ir.raster.frame.psm) && ir.raster.frame.fbmsk != 0) {
        s.fbmsk16 = Fbmsk16From32(ir.raster.frame.fbmsk);
        s.fbmsk16_exact = Fbmsk16Exact(s.fbmsk16);
    }
    dev_.Vertices().BindForDraw(st);
    s.first = dev_.Vertices().Upload(scratch_.data(), s.tri_verts);
    st.ColorMask(false, false, false, false);
    GlDevice::ApplyStencil(p.stencil_state);
    dev_.CensusMut().draw_calls++;
        glDrawArrays(GL_TRIANGLES, s.first, static_cast<GLsizei>(s.tri_verts));
    GlDevice::ResetStencil();
    st.ColorMask(true, true, true, true);
    st.DepthMask(true);
    return MakeResult(ir, p, s);
}

// The folded invert: the target's Cd copy is refreshed over the WHOLE target (the
// self-read rule), bound on unit 1, and the program writes rgb = ~Cd, a = 255 -
// CSM1(idx) with FIX/128; blend off, depth ALWAYS without writes, all channels.
DrawResult GlDrawExecutor::DrawInvert(const gs::DrawIr& ir, const gs::TextureSource& tex,
                                      const DrawParams& p) {
    Step s;
    DrawResult none;
    if (ir.verts.size() < 3) return none;
    if (!BindTarget(ir, p, s)) return none;
    FrameClear(p);
    GlStateCache& st = dev_.State();
    st.UseProgram(dev_.Shaders().StencilInvertProgram());
    const GlStencilLocations& SL = dev_.Shaders().StencilLocations();
    glUniform2f(SL.invert_screen, static_cast<float>(s.render_w), static_cast<float>(s.render_h));
    glUniform1f(SL.invert_fix, ir.raster.alpha.fix / 128.0f);
    if (!ConvertVertices(ir, tex, s)) return none;
    s.idx_self_serve = true;
    s.copy_x = s.copy_y = s.copy_w = s.copy_h = 0;
    s.needs_rt_copy = true;
    CdCopy(p, s);
    if (!s.sw_blend_active) return none;   // no Cd copy on the target: the caller's Accept declines this
    st.Disable(GL_BLEND);
    st.ActiveTexture(GL_TEXTURE4);
    st.BindTex2D(dev_.DummyTexture());
    st.ActiveTexture(GL_TEXTURE0);
    st.Enable(GL_DEPTH_TEST);
    st.DepthFunc(GL_ALWAYS);
    st.DepthMask(false);
    ScissorState(ir, s);
    dev_.Vertices().BindForDraw(st);
    s.first = dev_.Vertices().Upload(scratch_.data(), s.tri_verts);
    st.ColorMask(true, true, true, true);
    dev_.CensusMut().draw_calls++;
        glDrawArrays(GL_TRIANGLES, s.first, static_cast<GLsizei>(s.tri_verts));
    st.DepthMask(true);
    return MakeResult(ir, p, s);
}

// The half-word copy: the MAIN attachment bound (never the companion), the whole
// target copied to rt_copy (unit 1: the destination half-words and, for a self read,
// the source), the sibling target's colour image on unit 0 otherwise; one quad over
// [0, cols) x [0, rows32) under the halfword program with blend, depth and scissor
// off and every channel writable (FBMSK is merged in the program).
DrawResult GlDrawExecutor::DrawHalfwordShuffle(const gs::DrawIr& ir, const gs::TextureSource& tex,
                                               const DrawParams& p, const gs::HalfwordShuffle::Op& op) {
    DrawResult none;
    const GlSurface* copy = dev_.Surface(p.rt_copy);
    if (!p.rt_copy_available || !copy || !copy->tex) return none;
    const GlSurface* src = nullptr;
    if (op.self) {
        src = copy;
    } else {
        // The sibling: a 16-bit read the resolver served from a 32-bit-content
        // target's companion; the copy reads that target's 32-bit image itself.
        using Kind = gs::TextureSource::OwnerKind;
        const GlSurface* main = dev_.Surface(tex.owner.alt16_main);
        const bool sibling = tex.enabled && tex.owner.kind == Kind::Surface && tex.owner.fmt_case == 0 &&
                             tex.owner.alt16_serve && main && main->tex;
        if (!sibling) return none;
        src = main;
    }
    if (op.rows32 > p.target_h || op.cols > p.target_w) return none;
    GlStateCache& st = dev_.State();
    st.Disable(GL_SCISSOR_TEST);
    const int sc = std::max<int>(1, p.scale);
    const uint32_t fbo = dev_.BindTarget(p.color, p.depth);
    if (!fbo) return none;
    FrameClear(p);
    st.Viewport(0, 0, p.target_w * sc, p.target_h * sc);
    // The destination copy: the whole target, from the main attachment.
    st.BindFb(GL_READ_FRAMEBUFFER, fbo);
    st.ActiveTexture(GL_TEXTURE1);
    st.BindTex2D(copy->tex);
    dev_.CensusMut().copies++;
    dev_.CensusMut().copy_bytes += static_cast<uint64_t>(p.target_w) * p.target_h * sc * sc * 4u;
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, p.target_w * sc, p.target_h * sc);
    if (p.cd_copy) CdCopyPolicy::Invalidate(*p.cd_copy);
    st.ActiveTexture(GL_TEXTURE0);
    st.BindTex2D(src->tex);
    st.UseProgram(dev_.Shaders().HalfwordShuffleProgram());
    const GlStencilLocations& SL = dev_.Shaders().StencilLocations();
    glUniform2f(SL.hw_screen, static_cast<float>(p.target_w), static_cast<float>(p.target_h));
    glUniform1i(SL.hw_dest_hi, op.dest_hi ? 1 : 0);
    glUniform1i(SL.hw_mask16, static_cast<int>(op.mask16));
    glUniform1i(SL.hw_ta0, op.ta0);
    glUniform1i(SL.hw_ta1, op.ta1);
    glUniform1i(SL.hw_aem, op.aem ? 1 : 0);
    st.Disable(GL_BLEND);
    st.Disable(GL_DEPTH_TEST);
    st.DepthMask(false);
    st.ColorMask(true, true, true, true);
    const float w = static_cast<float>(op.cols), h = static_cast<float>(op.rows32);
    scratch_.assign(6, GpuVertex{});
    const float xs[6] = {0.f, w, 0.f, w, w, 0.f}, ys[6] = {0.f, 0.f, h, 0.f, h, h};
    for (int i = 0; i < 6; ++i) { scratch_[i].x = xs[i]; scratch_[i].y = ys[i]; scratch_[i].q = 1.f; }
    dev_.Vertices().BindForDraw(st);
    const int first = dev_.Vertices().Upload(scratch_.data(), 6);
    dev_.CensusMut().draw_calls++;
    glDrawArrays(GL_TRIANGLES, first, 6);
    st.DepthMask(true);
    st.Enable(GL_DEPTH_TEST);
    DrawResult r;
    r.drew = true;
    r.bbox_valid = true;
    r.bbox_tri[0] = r.bbox_all[0] = 0.f; r.bbox_tri[1] = r.bbox_all[1] = 0.f;
    r.bbox_tri[2] = r.bbox_all[2] = w; r.bbox_tri[3] = r.bbox_all[3] = h;
    r.write_rect = Rect{0, 0, op.cols, op.rows32};
    r.cd_copy_performed = true;
    r.cd_copy_rect = Rect{0, 0, 0, 0};
    r.halfword_shuffle = true;
    (void)ir;
    return r;
}

// Scissor off before any framebuffer operation; bind the main or companion
// attachment; viewport = physical size; u_screen_size stays logical so PS2 px -> NDC
// is unchanged and rasterisation scales through the viewport alone.
bool GlDrawExecutor::BindTarget(const gs::DrawIr& ir, const DrawParams& p, Step& s) {
    (void)ir;
    GlStateCache& st = dev_.State();
    st.Disable(GL_SCISSOR_TEST);
    s.render_w = p.render_w;
    s.render_h = p.render_h;
    s.scale = std::max<int>(1, p.scale);
    const SurfaceId color = p.alt16_route && p.alt16.id ? p.alt16 : p.color;
    s.fbo = dev_.BindTarget(color, p.depth);
    if (!s.fbo) return false;
    st.Viewport(0, 0, s.render_w * s.scale, s.render_h * s.scale);
    return true;
}

// Per-game-frame clear: depth always (to 0.0 = GS far plane), colour only on the
// scanout target (transparent black).
void GlDrawExecutor::FrameClear(const DrawParams& p) {
    if (!p.do_frame_clear) return;
    GlStateCache& st = dev_.State();
    st.DepthMask(true);
    glClearDepth(0.0);
    GLbitfield bits = GL_DEPTH_BUFFER_BIT;
    if (p.clear_color) {
        glClearColor(0.f, 0.f, 0.f, 0.f);
        st.ColorMask(true, true, true, true);
        bits |= GL_COLOR_BUFFER_BIT;
    }
    dev_.CensusMut().clears++;
    glClear(bits);
}

void GlDrawExecutor::BindTextures(const gs::TextureSource& tex, const DrawParams& p, Step& s) {
    using Kind = gs::TextureSource::OwnerKind;
    GlStateCache& st = dev_.State();
    const GlSurface* owner = dev_.Surface(tex.owner.surface);
    s.using_rt = tex.enabled && tex.owner.kind == Kind::Surface && owner && owner->tex;
    s.has_texture = tex.enabled && owner && owner->tex &&
                    (tex.owner.kind == Kind::Surface || tex.owner.kind == Kind::Decoded);
    st.ActiveTexture(GL_TEXTURE0);
    if (s.has_texture) {
        st.BindTex2D(owner->tex);
        if (s.using_rt && tex.transform.is_shuffle) {
            st.TexParami(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            st.TexParami(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        // CLAMP register: 0 REPEAT, 1 CLAMP, 2 REGION_CLAMP (as CLAMP), 3 REGION_REPEAT (as REPEAT);
        // render-target content never tiles, every other texture wraps per wms/wmt.
        auto wrap = [](uint8_t mode) -> GLint {
            return (mode == 0 || mode == 3) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        };
        if (s.using_rt) {
            st.TexParami(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            st.TexParami(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            st.TexParami(GL_TEXTURE_WRAP_S, wrap(tex.sampler.wms));
            st.TexParami(GL_TEXTURE_WRAP_T, wrap(tex.sampler.wmt));
        }
        uint32_t mag = GL_NEAREST, min = GL_NEAREST;
        const uint8_t levels = s.using_rt ? 0 : owner->levels;
        SelectFilters(tex.sampler.mag_linear, tex.sampler.mmin, levels, &mag, &min);
        // The player's enhancement (video.texture_filter / video.anisotropy) over
        // the OWNED textures only: the title's TEX1 choice above is the baseline
        // (and the whole story under `title` / anisotropy 1); render-target reads
        // and the VRAM-decoded model textures keep the title's sampler.
        s.hw_lod = false;
        if (tex.owner.owned && !s.using_rt) {
            const TextureFilterSelection* sel = dev_.TextureFilter();
            const TextureFilter mode = sel ? sel->Filter() : TextureFilter::Title;
            const int requested = sel ? sel->Anisotropy() : 1;
            const uint32_t title_mag = mag, title_min = min;
            int aniso = 1;
            s.hw_lod = EnhanceFilters(mode, requested, dev_.MaxAnisotropy(), tex.transform.mip_levels > 0,
                                      levels, &mag, &min, &aniso);
            // The anisotropy parameter is per texture object: an owned texture
            // keeps the value of its LAST draw, so it is restated (state-cached)
            // on every owned draw -- 1.0 under a selection without anisotropy.
            if (dev_.MaxAnisotropy() > 1) st.TexParamf(kGlTextureMaxAnisotropy, static_cast<float>(aniso));
            GlDevice::TextureFilterCensus& census = dev_.FilterCensus();
            census.owned_draws++;
            if (mag != title_mag || min != title_min || aniso > 1) census.enhanced_draws++;
            if (s.hw_lod) census.hw_lod_draws++;
        }
        st.TexParami(GL_TEXTURE_MAG_FILTER, static_cast<GLint>(mag));
        st.TexParami(GL_TEXTURE_MIN_FILTER, static_cast<GLint>(min));
        // depth and indexed serves never filter
        if (s.using_rt && (tex.transform.depth_fmt != 0 || tex.transform.idx_fmt != 0)) {
            st.TexParami(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            st.TexParami(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        }
    }
    // Units 2 (u_clut) and 3 (u_idx_tex2) hold valid textures on every draw.
    const bool idx_active = s.has_texture && s.using_rt && tex.transform.idx_fmt != 0;
    const GlSurface* clut = dev_.Surface(p.clut);
    const GlSurface* partner = dev_.Surface(p.idx_partner);
    const uint32_t dummy = dev_.DummyTexture();
    st.ActiveTexture(GL_TEXTURE2);
    st.BindTex2D(idx_active && clut && clut->tex ? clut->tex : dummy);
    st.ActiveTexture(GL_TEXTURE3);
    const bool partner_active = idx_active && tex.transform.idx_fmt == 5 && partner && partner->tex;
    st.BindTex2D(partner_active ? partner->tex : dummy);
    if (partner_active) {
        st.TexParami(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        st.TexParami(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        st.TexParami(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        st.TexParami(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    st.ActiveTexture(GL_TEXTURE0);
}

void GlDrawExecutor::TextureUniforms(const gs::DrawIr& ir, const gs::TextureSource& tex,
                                     const DrawParams& p, const Step& s) {
    GlUniformCache& u = dev_.Uniforms();
    const GlUniformLocations& L = dev_.Shaders().Locations();
    const auto& t = tex.transform;
    const bool rt = s.has_texture && s.using_rt;
    u.Set1i(L.use_texture, s.has_texture ? 1 : 0);
    u.Set1i(L.tex_sampler, 0);
    u.Set1i(L.depth_fmt, rt ? t.depth_fmt : 0);
    u.Set1i(L.depth_zoff, rt ? t.z_row_offset : 0);
    const int idx_fmt = rt ? t.idx_fmt : 0;
    u.Set1i(L.idx_fmt, idx_fmt);
    if (!(p.inherited_uniforms & kInheritUvNative))
        u.Set1i(L.uv_native, (s.has_texture && t.uv_native && idx_fmt == 0) ? 1 : 0);
    if (!(p.inherited_uniforms & kInheritDrawScale)) u.Set1i(L.draw_scale, std::max<int>(1, p.scale));
    int idx_self = idx_fmt != 0 ? static_cast<int>(t.idx_self) : 0;
    if (idx_fmt == 6 && ir.tex.fst) idx_self |= 0x1000;   // fmt 6 rescales STQ to claimed texels
    u.Set1i(L.idx_self, idx_self);
    // CLUT row 1 (raw PS2 alpha) only for the PSMT8H self-read HIGHLIGHT2/TCC=1 class.
    const bool clut_raw = idx_fmt == 4 && t.idx_self != 0 && tex.enabled && ir.tex.tfx == 3 &&
                          ir.tex.tcc == 1;
    u.Set1i(L.clut_raw, clut_raw ? 1 : 0);
    // Render-target composites are never V-flipped; the single flip is at scanout.
    u.Set1i(L.flip_v, 0);
    if (t.region.applied)
        u.Set2f(L.tex_size, static_cast<float>(t.region.w), static_cast<float>(t.region.h));
    else
        u.Set2f(L.tex_size, static_cast<float>(t.uv_tex_w), static_cast<float>(t.uv_tex_h));
    u.Set1i(L.tfx, tex.enabled ? static_cast<int>(ir.tex.tfx) : 0);
    const int levels = rt ? 0 : t.mip_levels;
    u.Set2i(L.mip_ctl, levels, t.mip_lcm ? 1 : 0);
    u.Set1i(L.tex_lod_hw, s.hw_lod ? 1 : 0);
    u.Set2f(L.mip_lodp, static_cast<float>(t.mip_k) / 16.0f,
            static_cast<float>(1u << (t.mip_l & 3)));
    u.Set1i(L.tcc, tex.enabled ? static_cast<int>(ir.tex.tcc) : 0);
}

// Mode by FST / enable; STQ prescale by the resolver's operands; region rebase
// ((u - rx*q)/q = u/q - rx per pixel); shuffle U scale for STQ; optional column snap.
bool GlDrawExecutor::ConvertVertices(const gs::DrawIr& ir, const gs::TextureSource& tex, Step& s) {
    const auto& t = tex.transform;
    const size_t n = ir.verts.size();
    scratch_.resize(n);
    // Untextured iff no enabled texture with a positive u_tex_size (uv_tex_w/h);
    // the STQ prescale operands (uv_pre_w/h) are a separate input.
    const bool textured = tex.enabled && t.uv_tex_w > 0 && t.uv_tex_h > 0;
    const VertexConvert::UvMode mode =
        !textured ? VertexConvert::UvMode::Untextured
                  : (ir.tex.fst ? VertexConvert::UvMode::UvFixed : VertexConvert::UvMode::Stq);
    const VertexConvert::FusedBboxes bb =
        VertexConvert::Convert(ir.verts.data(), n, scratch_.data(), mode, t.uv_pre_w, t.uv_pre_h);
    s.bb_valid = bb.valid;
    s.bb_tri[0] = bb.tri.x0; s.bb_tri[1] = bb.tri.y0; s.bb_tri[2] = bb.tri.x1; s.bb_tri[3] = bb.tri.y1;
    s.bb_all[0] = bb.all.x0; s.bb_all[1] = bb.all.y0; s.bb_all[2] = bb.all.x1; s.bb_all[3] = bb.all.y1;
    if (t.region.applied) {
        const float rx = static_cast<float>(t.region.u0), ry = static_cast<float>(t.region.v0);
        for (GpuVertex& gv : scratch_) {
            gv.u -= rx * gv.q;
            gv.v -= ry * gv.q;
        }
    }
    if (t.shuffle_uv_scale_u != 1.0f && !ir.tex.fst)
        for (GpuVertex& gv : scratch_) gv.u *= t.shuffle_uv_scale_u;
    if (t.shuffle_snap)
        for (GpuVertex& gv : scratch_) {
            gv.u = SnapColumn8(gv.u);
            gv.v = SnapRow(gv.v);
        }
    s.tri_verts = (n / 3) * 3;
    return s.tri_verts >= 3;
}

void GlDrawExecutor::RegisterUniforms(const gs::DrawIr& ir, const DrawParams& p) {
    GlUniformCache& u = dev_.Uniforms();
    const GlUniformLocations& L = dev_.Shaders().Locations();
    const auto& r = ir.raster;
    u.Set1i(L.blend_enabled, ir.alpha_blend ? 1 : 0);
    u.Set1i(L.fba, r.fba ? 1 : 0);
    u.Set1i(L.colclamp, r.colclamp ? 1 : 0);
    // 16-bit colour FRAME conventions: the A-bit alpha (PSMCT16S only) and the 5-bit
    // channel quantisation with DTHE/DIMX dither (both CT16 formats; value = scale).
    u.Set1i(L.fb16, r.frame.psm == 0x0A ? 1 : 0);
    // A class-native generic arm leaves these as the last full-set draw wrote them
    // (InheritedUniform, backend.h).
    if (!(p.inherited_uniforms & kInheritCt16Quant))
        u.Set1i(L.ct16q, Is16BitFrame(r.frame.psm) ? std::max<int>(1, p.scale) : 0);
    if (!(p.inherited_uniforms & kInheritDither)) {
        u.Set1i(L.dthe, r.dthe ? 1 : 0);
        u.Set1i(L.dimx_lo, static_cast<int>(static_cast<uint32_t>(r.dimx & 0xFFFFFFFFull)));
        u.Set1i(L.dimx_hi, static_cast<int>(static_cast<uint32_t>(r.dimx >> 32)));
    }
    u.Set1i(L.scanmsk, ir.scanmsk);
    u.Set1i(L.ate, r.test.ate ? 1 : 0);
    u.Set1i(L.atst, r.test.atst);
    u.Set1i(L.aref, r.test.aref);
    u.Set1i(L.afail, r.test.afail);
    u.Set1i(L.afail_pass, 0);   // every fragment; Draw's two-pass split re-sets it per pass
    u.Set1i(L.date, r.test.date ? 1 : 0);
    u.Set1i(L.datm, r.test.datm ? 1 : 0);
    u.Set1i(L.fog_enabled, ir.fog ? 1 : 0);
    u.Set3f(L.fogcol, r.fogcol_r / 255.0f, r.fogcol_g / 255.0f, r.fogcol_b / 255.0f);
}

// fbmsk16 analysis, the exact-GL route, the Cd read rect (bbox ∩ scissor ∩ target),
// and the reasons a destination copy is needed.
void GlDrawExecutor::ClassifyRoutes(const gs::DrawIr& ir, const gs::TextureSource& tex,
                                    const DrawParams& p, Step& s) {
    const auto& r = ir.raster;
    s.alpha_blend = ir.alpha_blend;
    s.fbmsk16 = 0;
    s.fbmsk16_exact = false;
    s.fbmsk32_exact = false;
    if (Is16BitFrame(r.frame.psm) && r.frame.fbmsk != 0) {
        s.fbmsk16 = Fbmsk16From32(r.frame.fbmsk);
        s.fbmsk16_exact = Fbmsk16Exact(s.fbmsk16);
    } else if (!Is16BitFrame(r.frame.psm)) {
        s.fbmsk32_exact = Fbmsk32Partial(r.frame.fbmsk);
    }
    s.route_rt_blend = RouteRtBlend(ir, s.using_rt, s.fbmsk16_exact || s.fbmsk32_exact);
    if (dev_.ForceShaderBlend()) s.route_rt_blend = false;
    s.idx_self_serve = s.has_texture && s.using_rt && tex.transform.idx_fmt == 4 &&
                       tex.transform.idx_self != 0;
    if (s.idx_self_serve) s.route_rt_blend = false;
    // read rect: bbox_all clamped to ±1e9 (a NaN box collapses to the ±1e9 seeds) ∩ scissor ∩ target
    s.copy_x = s.copy_y = s.copy_w = s.copy_h = 0;
    if (!ir.verts.empty()) {
        const float cbx0 = std::min(s.bb_all[0], 1e9f), cby0 = std::min(s.bb_all[1], 1e9f);
        const float cbx1 = std::max(s.bb_all[2], -1e9f), cby1 = std::max(s.bb_all[3], -1e9f);
        int rx0 = std::max(static_cast<int>(std::floor(cbx0)), static_cast<int>(r.scissor.x0));
        int ry0 = std::max(static_cast<int>(std::floor(cby0)), static_cast<int>(r.scissor.y0));
        int rx1 = std::min(static_cast<int>(std::ceil(cbx1)), static_cast<int>(r.scissor.x1) + 1);
        int ry1 = std::min(static_cast<int>(std::ceil(cby1)), static_cast<int>(r.scissor.y1) + 1);
        rx0 = std::max(0, std::min(rx0, p.target_w));
        ry0 = std::max(0, std::min(ry0, p.target_h));
        rx1 = std::max(0, std::min(rx1, p.target_w));
        ry1 = std::max(0, std::min(ry1, p.target_h));
        if (rx1 > rx0 && ry1 > ry0) {
            s.copy_x = rx0; s.copy_y = ry0; s.copy_w = rx1 - rx0; s.copy_h = ry1 - ry0;
        }
    }
    if (s.idx_self_serve) { s.copy_w = 0; s.copy_h = 0; }   // UV-addressed read: whole copy
    s.needs_rt_copy = r.test.date || s.fbmsk16_exact || s.fbmsk32_exact || s.idx_self_serve;
    // What the copy feeds: the exact-mask merges and the indexed self-read consume RGB;
    // DATE alone reads the destination ALPHA only (BlendState sets this true again when
    // the shader blends, whose Cd read consumes everything).
    s.reads_cd_rgb = s.fbmsk16_exact || s.fbmsk32_exact || s.idx_self_serve;
    s.wrap_route = BlendFfMap::RouteWrapBlend(ir.alpha_blend, r.colclamp);
}

// Lazy destination copy into rt_copy (unit 1) from the bound attachment -- or, by
// texture barrier, the attachment itself on unit 1 behind glTextureBarrierNV: the
// writes of the draws before it are visible to this draw's destination reads
// without a copy (and without ending the render pass for the copy's sake).
void GlDrawExecutor::CdCopy(const DrawParams& p, Step& s) {
    const GlSurface* copy = dev_.Surface(p.rt_copy);
    if (!p.rt_copy_available || !copy || !copy->tex) return;
    GlStateCache& st = dev_.State();
    if (dev_.CdCopyByBarrier()) {
        const GlSurface* att = dev_.Surface(p.alt16_route ? p.alt16 : p.color);
        if (att && att->tex) {
            st.ActiveTexture(GL_TEXTURE1);
            st.BindTex2D(att->tex);
            st.ActiveTexture(GL_TEXTURE0);
            dev_.TextureBarrier()();
            dev_.CensusMut().barriers++;
            s.sw_blend_active = true;
            return;
        }
    }
    bool skip = false;
    int cx = s.copy_x, cy = s.copy_y, cw = s.copy_w, ch = s.copy_h;
    if (p.cd_copy) {
        const int8_t route_now = p.alt16_route ? 1 : 0;
        if (p.cd_copy->last_route != route_now) {
            CdCopyPolicy::Invalidate(*p.cd_copy);
            p.cd_copy->last_route = route_now;
        }
        int oc[4];
        if (!CdCopyPolicy::Decide(*p.cd_copy, s.copy_x, s.copy_y, s.copy_w, s.copy_h,
                                  s.idx_self_serve, p.target_w, p.target_h, oc, s.reads_cd_rgb)) {
            skip = true;
        } else {
            cx = oc[0]; cy = oc[1]; cw = oc[2]; ch = oc[3];
        }
    }
    st.BindFb(GL_READ_FRAMEBUFFER, s.fbo);
    st.ActiveTexture(GL_TEXTURE1);
    st.BindTex2D(copy->tex);
    if (!skip) {
        const int sc = s.scale;
        const int pw = (cw > 0 && ch > 0) ? cw : p.target_w, ph = (cw > 0 && ch > 0) ? ch : p.target_h;
        dev_.CensusMut().copies++;
        dev_.CensusMut().copy_bytes += static_cast<uint64_t>(pw) * ph * sc * sc * 4u;
        if (cw > 0 && ch > 0)
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, cx * sc, cy * sc, cx * sc, cy * sc, cw * sc, ch * sc);
        else
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, p.target_w * sc, p.target_h * sc);
        s.cd_copied = true;
        s.cd_rect = Rect{cx, cy, cw, ch};
    }
    st.ActiveTexture(GL_TEXTURE0);
    s.sw_blend_active = true;
}

// The blend-equation uniforms of a blended draw (both the shader and the
// fixed-function arm write them); a class-native generic arm inherits them.
void GlDrawExecutor::BlendAbcdUniforms(const gs::DrawIr& ir, const DrawParams& p) {
    if (p.inherited_uniforms & kInheritBlendAbcd) return;
    GlUniformCache& u = dev_.Uniforms();
    const GlUniformLocations& L = dev_.Shaders().Locations();
    const auto& a = ir.raster.alpha;
    u.Set1i(L.blend_a, a.a);
    u.Set1i(L.blend_b, a.b);
    u.Set1i(L.blend_c, a.c);
    u.Set1i(L.blend_d, a.d);
    u.Set1f(L.fix_alpha, a.fix / 128.0f);
}

void GlDrawExecutor::BlendState(const gs::DrawIr& ir, const DrawParams& p, Step& s) {
    GlStateCache& st = dev_.State();
    GlUniformCache& u = dev_.Uniforms();
    const GlUniformLocations& L = dev_.Shaders().Locations();
    const auto& a = ir.raster.alpha;
    s.sw_blend_active = false;
    // FBA (FBA register: bit 7 OR-ed into the alpha WRITTEN, GS User's Manual
    // 3.6.3) is applied by the fragment program to its stored-alpha output on
    // every route, and the fixed-function arm's alpha factor is the SECOND output
    // (frag_factor, the pre-FBA As), so an FBA draw whose ALPHA tuple has an exact
    // fixed-function form needs no destination copy: it blends like any other
    // matched draw. A tuple without an exact form keeps the shader blend (the
    // OLD route for every FBA draw).
    const FfBlendMap ffm = BlendFfMap::Map(a.a, a.b, a.c, a.d);
    const bool fba_sw = ir.raster.fba && !ffm.matched;
    auto fixed_function = [&]() {
        if (ffm.uses_fix) st.BlendColor(0.f, 0.f, 0.f, a.fix / 128.0f);
        st.Enable(GL_BLEND);
        st.BlendEquationSeparate(ffm.eq_rgb, GL_FUNC_ADD);
        st.BlendFuncSeparate(ffm.src_rgb, ffm.dst_rgb, GL_ONE, GL_ZERO);   // alpha: source replace
        BlendAbcdUniforms(ir, p);
    };
    if (ir.alpha_blend &&
        (s.using_rt || fba_sw || s.fbmsk16_exact || s.fbmsk32_exact || s.wrap_route || dev_.ForceShaderBlend()) &&
        !s.route_rt_blend) {
        s.needs_rt_copy = true;
        s.reads_cd_rgb = true;
        CdCopy(p, s);
        if (s.sw_blend_active) u.Set1i(L.rt_sampler, 1);
        BlendAbcdUniforms(ir, p);
        st.Disable(GL_BLEND);
    } else if (s.needs_rt_copy) {
        // DATE / exact mask / idx self-read: the destination copy feeds the shader's
        // test / merge / serve. The BLEND of such a draw is decided here as it would be
        // without the copy: a tuple with an exact fixed-function form whose only copy
        // reason is DATE blends on the GPU (the shadow-volume counter strips -- their
        // sub/add faces self-overlap, which one pre-draw Cd copy cannot accumulate),
        // the shader's DATE block reading u_rt_texture independently of u_sw_blend;
        // every other blended form takes the shader blend with ITS OWN tuple (never
        // the previous draw's uniforms, never GL_BLEND left on from a fixed-function
        // draw); an unblended draw must not inherit blend state.
        const bool date_only = ir.raster.test.date && !s.reads_cd_rgb;
        if (ir.alpha_blend && date_only && ffm.matched && !fba_sw && ir.raster.colclamp &&
            !dev_.ForceShaderBlend()) {
            CdCopy(p, s);
            if (s.sw_blend_active) u.Set1i(L.rt_sampler, 1);
            s.ff_with_copy = s.sw_blend_active;
            fixed_function();
        } else {
            if (ir.alpha_blend) s.reads_cd_rgb = true;
            CdCopy(p, s);
            if (s.sw_blend_active) u.Set1i(L.rt_sampler, 1);
            if (ir.alpha_blend) BlendAbcdUniforms(ir, p);
            st.Disable(GL_BLEND);
        }
    } else if (ir.alpha_blend) {
        fixed_function();
    } else {
        st.Disable(GL_BLEND);
    }
    u.Set1i(L.sw_blend, (s.sw_blend_active && !s.ff_with_copy) ? 1 : 0);
}

// The colour-authored Z source (unit 4) moves the compare into the shader with the
// GL depth function forced ALWAYS; otherwise TEST.ZTE/ZTST and ZBUF.ZMSK map directly.
void GlDrawExecutor::DepthState(const gs::DrawIr& ir, const DrawParams& p, const Step& s) {
    GlStateCache& st = dev_.State();
    GlUniformCache& u = dev_.Uniforms();
    const GlUniformLocations& L = dev_.Shaders().Locations();
    const auto& t = ir.raster.test;
    const GlSurface* zsrc = dev_.Surface(p.zsrc);
    const bool armed = p.zsrc_armed && zsrc && zsrc->tex;
    st.ActiveTexture(GL_TEXTURE4);
    st.BindTex2D(armed ? zsrc->tex : dev_.DummyTexture());
    st.ActiveTexture(GL_TEXTURE0);
    u.Set1i(L.zsrc_en, armed ? t.ztst : 0);
    if (!(p.inherited_uniforms & kInheritZsrcMap)) {
        u.Set1i(L.zsrc_num, armed ? std::max<int>(1, p.zsrc_scale) : 1);
        u.Set1i(L.zsrc_den, armed ? std::max<int>(1, p.scale) : 1);
    }
    if (armed) {
        st.Enable(GL_DEPTH_TEST);
        st.DepthFunc(GL_ALWAYS);
        st.DepthMask(false);
    } else if (t.zte) {
        st.Enable(GL_DEPTH_TEST);
        switch (t.ztst) {
            case 0: st.DepthFunc(GL_NEVER); break;
            case 1: st.DepthFunc(GL_ALWAYS); break;
            case 2: st.DepthFunc(GL_GEQUAL); break;
            case 3: st.DepthFunc(GL_GREATER); break;
            default: st.DepthFunc(GL_GEQUAL); break;
        }
        // ZMSK and the alpha test's static arm (AFAIL FB_ONLY / RGB_ONLY under ATST
        // NEVER: the frame buffer alone is updated) both veto the depth write.
        st.DepthMask(!ir.raster.zbuf.zmsk && s.afail.write_depth);
    } else {
        st.Enable(GL_DEPTH_TEST);
        st.DepthFunc(GL_ALWAYS);
        st.DepthMask(false);
    }
}

// The GS scissor is inclusive and always active; the targets keep PS2 top at GL
// bottom so y is not flipped. The half-open span scales exactly to physical pixels.
void GlDrawExecutor::ScissorState(const gs::DrawIr& ir, const Step& s) {
    GlStateCache& st = dev_.State();
    const auto& sc = ir.raster.scissor;
    const int sx0 = std::max(0, static_cast<int>(sc.x0));
    const int sy0 = std::max(0, static_cast<int>(sc.y0));
    const int sx1 = std::min(s.render_w - 1, static_cast<int>(sc.x1));
    const int sy1 = std::min(s.render_h - 1, static_cast<int>(sc.y1));
    if (sx1 >= sx0 && sy1 >= sy0 &&
        (sx0 > 0 || sy0 > 0 || sx1 < s.render_w - 1 || sy1 < s.render_h - 1)) {
        st.Enable(GL_SCISSOR_TEST);
        st.Scissor(sx0 * s.scale, sy0 * s.scale, (sx1 - sx0 + 1) * s.scale,
                   (sy1 - sy0 + 1) * s.scale);
    } else {
        st.Disable(GL_SCISSOR_TEST);
    }
}

// FBMSK bit = 1 protects. Exact 5551 merge engaged => the shader merges and GL writes
// all channels; 16-bit frames mask per 5551 field; 32-bit per byte; PSMCT24 never
// writes alpha.
bool GlDrawExecutor::ColorMaskState(const gs::DrawIr& ir, const Step& s) {
    GlStateCache& st = dev_.State();
    GlUniformCache& u = dev_.Uniforms();
    const GlUniformLocations& L = dev_.Shaders().Locations();
    const uint32_t mask = ir.raster.frame.fbmsk;
    const uint8_t psm = ir.raster.frame.psm;
    const bool engaged = s.fbmsk16_exact && s.sw_blend_active;
    const bool engaged32 = s.fbmsk32_exact && s.sw_blend_active;
    bool wr, wg, wb, wa;
    if (engaged || engaged32) {
        wr = wg = wb = wa = true;
    } else if (Is16BitFrame(psm)) {
        wr = (s.fbmsk16 & 0x001F) != 0x001F;
        wg = (s.fbmsk16 & 0x03E0) != 0x03E0;
        wb = (s.fbmsk16 & 0x7C00) != 0x7C00;
        wa = (s.fbmsk16 & 0x8000) != 0x8000;
    } else {
        wr = (mask & 0x000000FF) != 0x000000FF;
        wg = (mask & 0x0000FF00) != 0x0000FF00;
        wb = (mask & 0x00FF0000) != 0x00FF0000;
        wa = (mask & 0xFF000000) != 0xFF000000;
    }
    if (psm == 0x01) wa = false;
    // The alpha test's static arm: ATST NEVER with AFAIL ZB_ONLY updates no colour
    // (the GS Z-clear idiom), RGB_ONLY no alpha -- composed with FBMSK, never instead.
    if (!s.afail.write_rgb) wr = wg = wb = false;
    if (!s.afail.write_alpha) wa = false;
    st.ColorMask(wr, wg, wb, wa);
    u.Set1i(L.fbmsk16_en, engaged ? 1 : 0);
    u.Set1i(L.fbmsk16, static_cast<int>(s.fbmsk16));
    u.Set1i(L.fbmsk32_en, engaged32 ? 1 : 0);
    u.Set1i(L.fbmsk32, static_cast<int>(engaged32 ? mask : 0u));
    return wa;
}

DrawResult GlDrawExecutor::MakeResult(const gs::DrawIr& ir, const DrawParams& p,
                                      const Step& s) const {
    DrawResult r;
    r.drew = true;
    r.bbox_valid = s.bb_valid;
    for (int i = 0; i < 4; i++) { r.bbox_tri[i] = s.bb_tri[i]; r.bbox_all[i] = s.bb_all[i]; }
    r.alt16_route = p.alt16_route;
    // The shader BLEND engaged (as the Metal executor reports it): not the copy bound for
    // DATE alone, whether the draw is unblended or blends on the fixed function.
    r.sw_blend_active = s.alpha_blend && s.sw_blend_active && !s.ff_with_copy;
    r.needs_rt_copy = s.needs_rt_copy;
    r.wrote_alpha = s.wrote_alpha;
    r.route_rt_blend = s.route_rt_blend;
    r.wrap_route = s.wrap_route;
    r.fbmsk16_exact = s.fbmsk16_exact;
    r.fbmsk16_engaged = s.fbmsk16_exact && s.sw_blend_active;
    r.fbmsk16 = s.fbmsk16;
    r.cd_copy_performed = s.cd_copied;
    r.cd_copy_rect = s.cd_rect;
    // write rect: bbox_all ∩ scissor ∩ target (logical, half-open)
    const auto& sc = ir.raster.scissor;
    int x0 = std::max(static_cast<int>(std::floor(s.bb_all[0])), static_cast<int>(sc.x0));
    int y0 = std::max(static_cast<int>(std::floor(s.bb_all[1])), static_cast<int>(sc.y0));
    int x1 = std::min(static_cast<int>(std::ceil(s.bb_all[2])), static_cast<int>(sc.x1) + 1);
    int y1 = std::min(static_cast<int>(std::ceil(s.bb_all[3])), static_cast<int>(sc.y1) + 1);
    x0 = std::max(0, std::min(x0, p.target_w));
    y0 = std::max(0, std::min(y0, p.target_h));
    x1 = std::max(0, std::min(x1, p.target_w));
    y1 = std::max(0, std::min(y1, p.target_h));
    if (x1 > x0 && y1 > y0) r.write_rect = Rect{x0, y0, x1 - x0, y1 - y0};
    return r;
}

}  // namespace render
}  // namespace ps2r
