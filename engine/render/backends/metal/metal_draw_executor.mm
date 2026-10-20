// render/backends/metal/metal_draw_executor.mm -- MetalDrawExecutor implementation.
#include "engine/render/backends/metal/metal_draw_executor.h"

#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>

#include "engine/gs/hle/ir/alpha_fail_mask.h"
#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/render/backends/gl/blend_ff_map.h"
#include "engine/render/backends/gl/cd_copy_policy.h"
#include "engine/render/backends/gl/vertex_convert.h"
#include "engine/render/backends/metal/metal_device.h"

namespace ps2r {
namespace render {

namespace {

bool Is16BitFrame(uint8_t psm) { return psm == 0x02 || psm == 0x0A; }

float SnapColumn8(float u) { return std::nearbyint(u / 8.0f) * 8.0f; }
float SnapRow(float v) {
    const float s = std::nearbyint(v / 8.0f) * 8.0f;
    return s + std::nearbyint(v - s);
}

// A 5551 mask whose R/G/B fields are each all-0 or all-1 is expressible as a per-channel
// colour mask; anything else must merge bit-exactly in the shader (the GL arm's rule).
bool Fbmsk16Exact(uint32_t fbmsk16) {
    auto aligned = [](uint32_t v, uint32_t field) {
        const uint32_t f = v & field;
        return f == 0 || f == field;
    };
    const bool channel_aligned = aligned(fbmsk16, 0x001F) && aligned(fbmsk16, 0x03E0) && aligned(fbmsk16, 0x7C00);
    return !channel_aligned && fbmsk16 != 0;
}

bool Fbmsk32Partial(uint32_t fbmsk) {
    for (int i = 0; i < 4; ++i) {
        const uint32_t byte = (fbmsk >> (8 * i)) & 0xFFu;
        if (byte != 0x00u && byte != 0xFFu) return true;
    }
    return false;
}

id<MTLTexture> T(void* p) { return (id<MTLTexture>)p; }

// TEX1 MMAG / MMIN -> the sampler key (the GL arm's SelectFilters: with levels the mip
// forms for MMIN 2..5, without levels the base filter).
void SelectFilters(bool mag_linear, uint8_t mmin, uint8_t levels, MetalSamplerKey* k) {
    k->mag_linear = mag_linear ? 1 : 0;
    k->mip = 0;
    if (levels > 0) {
        switch (mmin) {
            case 0: k->min_linear = 0; break;
            case 1: k->min_linear = 1; break;
            case 2: case 3: k->min_linear = 0; k->mip = 2; break;
            case 4: case 5: k->min_linear = 1; k->mip = 2; break;
            default: k->min_linear = 0; break;
        }
    } else {
        k->min_linear = (mmin == 1 || mmin == 4 || mmin == 5) ? 1 : 0;
    }
}

// The player's enhancement over an owned texture's sampler (the GL arm's EnhanceFilters).
bool EnhanceFilters(TextureFilter filter, int anisotropy, int max_anisotropy, bool title_mips, uint8_t levels,
                    MetalSamplerKey* k, int* applied_anisotropy) {
    const int aniso = std::min(TextureFilterSelection::ClampAnisotropy(anisotropy), std::max(1, max_anisotropy));
    *applied_anisotropy = aniso;
    const bool chain = levels > 0;
    const bool hw_lod = chain && (aniso > 1 || filter == TextureFilter::Trilinear);
    const bool mips = chain && (title_mips || hw_lod);
    switch (filter) {
        case TextureFilter::Title: break;
        case TextureFilter::Point: k->mag_linear = 0; k->min_linear = 0; k->mip = mips ? 1 : 0; break;
        case TextureFilter::Bilinear: k->mag_linear = 1; k->min_linear = 1; k->mip = mips ? 1 : 0; break;
        case TextureFilter::Trilinear: k->mag_linear = 1; k->min_linear = 1; k->mip = chain ? 2 : 0; break;
    }
    return hw_lod;
}

}  // namespace

MetalDrawExecutor::MetalDrawExecutor(MetalDevice& device) : dev_(device) {}

void MetalDrawExecutor::OnEncoderEnded() {
    enc_shadow_ = nullptr;
    bound_pipeline_ = bound_depth_state_ = bound_sampler_ = nullptr;
    for (void*& t : bound_tex_) t = nullptr;
    vertex_bound_ = false;
    vp_w_ = vp_h_ = -1;
    sc_[0] = sc_[1] = sc_[2] = sc_[3] = -1;
}

DrawResult MetalDrawExecutor::Draw(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p) {
    DrawResult none;
    if (p.program != StencilProgram::None) return none;   // the stencil-composite recipe: not on this arm
    Step s;
    const size_t n = ir.verts.size();
    if (n < 3) return none;
    if (p.alt16_route) {
        gs::HalfwordShuffle::Op op;
        if (gs::HalfwordShuffle::Detect(ir, &op)) {
            const DrawResult hw = DrawHalfwordShuffle(ir, tex, p, op);
            if (hw.drew) return hw;
        }
    }
    if (!BindTarget(ir, p, s)) return none;
    s.afail = gs::AlphaFailMask::Compute(ir.raster.test);
    if (s.afail.WritesNothing()) return none;
    BindTextures(tex, p, s);
    if (!ConvertVertices(ir, tex, s)) return none;
    TextureUniforms(ir, tex, p, s);
    RegisterUniforms(ir, p);
    ClassifyRoutes(ir, tex, p, s);
    BlendState(ir, p, s);   // may refresh the copy (a blit: the pass is re-opened by Emit)
    DepthState(ir, p, s);
    s.first = dev_.UploadVertices(scratch_.data(), s.tri_verts);
    if (s.first < 0) return none;
    ColorMaskState(ir, s);
    if (gs::AlphaFailMask::SplitsPopulations(ir.raster.test)) {
        u_.afail_pass = 1;
        Emit(ir, s);
        Step fail = s;
        fail.afail = gs::AlphaFailMask::FailingPixels(ir.raster.test.afail);
        u_.afail_pass = 2;
        DepthState(ir, p, fail);
        ColorMaskState(ir, fail);
        Emit(ir, fail);
    } else {
        Emit(ir, s);
    }
    return MakeResult(ir, p, s);
}

// The half-word copy over the 32-bit image: the MAIN attachment, the whole target copied
// to rt_copy (the destination half-words and, for a self read, the source), the sibling's
// 32-bit image on texture 0 otherwise; one quad over [0, cols) x [0, rows32) under the
// halfword program (framebuffer fetch = the destination), blend / depth / scissor off.
DrawResult MetalDrawExecutor::DrawHalfwordShuffle(const gs::DrawIr& ir, const gs::TextureSource& tex,
                                                  const DrawParams& p, const gs::HalfwordShuffle::Op& op) {
    DrawResult none;
    MetalSurface* copy = dev_.SurfaceMut(p.rt_copy);
    if (!p.rt_copy_available || !copy || !copy->tex) return none;
    MetalSurface* src = nullptr;
    if (op.self) {
        src = copy;
    } else {
        using Kind = gs::TextureSource::OwnerKind;
        MetalSurface* main = dev_.SurfaceMut(tex.owner.alt16_main);
        const bool sibling = tex.enabled && tex.owner.kind == Kind::Surface && tex.owner.fmt_case == 0 &&
                             tex.owner.alt16_serve && main && main->tex;
        if (!sibling) return none;
        src = main;
    }
    if (op.rows32 > p.target_h || op.cols > p.target_w) return none;
    MetalSurface* target = dev_.SurfaceMut(p.color);
    if (!target || !target->tex) return none;
    const int sc = std::max<int>(1, p.scale);
    // The frame clear the target layer consumed for this draw, then the whole-target copy.
    if (p.do_frame_clear) {
        if (!dev_.BeginTargetPass(p.color, p.depth, p.clear_color, true, 0u, 0.f)) return none;
    }
    dev_.CopyTexture(*target, *copy, 0, 0, p.target_w * sc, p.target_h * sc);
    if (p.cd_copy) CdCopyPolicy::Invalidate(*p.cd_copy);
    void* enc = dev_.BeginTargetPass(p.color, p.depth, false, false, 0u, 0.f);
    if (!enc) return none;
    void* ps = dev_.Pipelines().Pipeline(MetalProgram::Halfword, true, true);
    if (!ps) return none;
    const float w = static_cast<float>(op.cols), h = static_cast<float>(op.rows32);
    scratch_.assign(6, GpuVertex{});
    const float xs[6] = {0.f, w, 0.f, w, w, 0.f}, ys[6] = {0.f, 0.f, h, 0.f, h, h};
    for (int i = 0; i < 6; ++i) { scratch_[i].x = xs[i]; scratch_[i].y = ys[i]; scratch_[i].q = 1.f; }
    const int first = dev_.UploadVertices(scratch_.data(), 6);
    if (first < 0) return none;
    enc = dev_.BeginTargetPass(p.color, p.depth, false, false, 0u, 0.f);   // the ring wrap may have ended it
    if (!enc) return none;
    OnEncoderEnded();   // the halfword pass leaves the register program's shadows stale
    id<MTLRenderCommandEncoder> e = (id<MTLRenderCommandEncoder>)enc;
    [e setRenderPipelineState:(id<MTLRenderPipelineState>)ps];
    MetalDepthKey dk;
    dk.compare = 1;
    dk.write = 0;
    [e setDepthStencilState:(id<MTLDepthStencilState>)dev_.Pipelines().DepthStencil(dk)];
    MTLViewport vp = {0.0, 0.0, static_cast<double>(p.target_w * sc), static_cast<double>(p.target_h * sc), 0.0, 1.0};
    [e setViewport:vp];
    MTLScissorRect scr = {0, 0, static_cast<NSUInteger>(p.target_w * sc), static_cast<NSUInteger>(p.target_h * sc)};
    [e setScissorRect:scr];
    MetalVertUniforms vu;
    vu.screen_size_x = static_cast<float>(p.target_w);
    vu.screen_size_y = static_cast<float>(p.target_h);
    [e setVertexBuffer:(id<MTLBuffer>)dev_.VertexRing() offset:0 atIndex:0];
    [e setVertexBytes:&vu length:sizeof(vu) atIndex:1];
    MetalHalfwordUniforms hu;
    hu.dest_hi = op.dest_hi ? 1 : 0;
    hu.mask16 = static_cast<int32_t>(op.mask16);
    hu.ta0 = op.ta0;
    hu.ta1 = op.ta1;
    hu.aem = op.aem ? 1 : 0;
    [e setFragmentBytes:&hu length:sizeof(hu) atIndex:0];
    [e setFragmentTexture:T(src->tex) atIndex:0];
    [e setFragmentTexture:T(copy->tex) atIndex:1];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:static_cast<NSUInteger>(first) vertexCount:6];
    dev_.CensusMut().draw_calls++;
    dev_.Touch(*src);
    dev_.Touch(*copy);
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

// The pass over the main or companion attachment; the per-game-frame clear (depth to 0.0,
// colour only on the scanout target) is the pass's load action.
bool MetalDrawExecutor::BindTarget(const gs::DrawIr& ir, const DrawParams& p, Step& s) {
    (void)ir;
    s.render_w = p.render_w;
    s.render_h = p.render_h;
    s.scale = std::max<int>(1, p.scale);
    s.color_id = p.alt16_route && p.alt16.id ? p.alt16 : p.color;
    s.depth_id = p.depth;
    s.encoder = dev_.BeginTargetPass(s.color_id, s.depth_id, p.do_frame_clear && p.clear_color, p.do_frame_clear, 0u, 0.f);
    return s.encoder != nullptr;
}

void MetalDrawExecutor::BindTextures(const gs::TextureSource& tex, const DrawParams& p, Step& s) {
    using Kind = gs::TextureSource::OwnerKind;
    MetalSurface* owner = dev_.SurfaceMut(tex.owner.surface);
    s.using_rt = tex.enabled && tex.owner.kind == Kind::Surface && owner && owner->tex;
    s.has_texture = tex.enabled && owner && owner->tex && (tex.owner.kind == Kind::Surface || tex.owner.kind == Kind::Decoded);
    s.sampler = MetalSamplerKey{};
    s.hw_lod = false;
    s.depth_owner = false;
    if (s.has_texture) {
        s.depth_owner = owner->desc.format == SurfaceFormat::Depth32f;
        if (s.depth_owner) s.tex5 = owner->tex;
        else s.tex0 = MetalDevice::SampleTexture(*owner);
        dev_.Touch(*owner);
        // CLAMP: 0 REPEAT, 1 CLAMP, 2 REGION_CLAMP (as CLAMP), 3 REGION_REPEAT (as REPEAT); a
        // render target never tiles.
        auto wrap = [](uint8_t mode) -> uint8_t { return (mode == 0 || mode == 3) ? 1 : 0; };
        if (s.using_rt) {
            s.sampler.wrap_s = s.sampler.wrap_t = 0;
        } else {
            s.sampler.wrap_s = wrap(tex.sampler.wms);
            s.sampler.wrap_t = wrap(tex.sampler.wmt);
        }
        const uint8_t levels = s.using_rt ? 0 : owner->levels;
        SelectFilters(tex.sampler.mag_linear, tex.sampler.mmin, levels, &s.sampler);
        if (tex.owner.owned && !s.using_rt) {
            const TextureFilterSelection* sel = dev_.TextureFilter();
            const TextureFilter mode = sel ? sel->Filter() : TextureFilter::Title;
            const int requested = sel ? sel->Anisotropy() : 1;
            const MetalSamplerKey title = s.sampler;
            int aniso = 1;
            s.hw_lod = EnhanceFilters(mode, requested, dev_.MaxAnisotropy(), tex.transform.mip_levels > 0, levels, &s.sampler,
                                      &aniso);
            s.sampler.anisotropy = static_cast<uint8_t>(aniso);
            MetalDevice::TextureFilterCensus& census = dev_.FilterCensus();
            census.owned_draws++;
            if (PackSamplerKey(s.sampler) != PackSamplerKey(title)) census.enhanced_draws++;
            if (s.hw_lod) census.hw_lod_draws++;
        }
        // depth and indexed serves never filter
        if (s.using_rt && (tex.transform.depth_fmt != 0 || tex.transform.idx_fmt != 0)) {
            s.sampler.mag_linear = s.sampler.min_linear = 0;
            s.sampler.mip = 0;
        }
    }
    const bool idx_active = s.has_texture && s.using_rt && tex.transform.idx_fmt != 0;
    MetalSurface* clut = dev_.SurfaceMut(p.clut);
    MetalSurface* partner = dev_.SurfaceMut(p.idx_partner);
    if (idx_active && clut && clut->tex) { s.tex2 = clut->tex; dev_.Touch(*clut); }
    const bool partner_active = idx_active && tex.transform.idx_fmt == 5 && partner && partner->tex;
    if (partner_active) { s.tex3 = partner->tex; dev_.Touch(*partner); }
}

void MetalDrawExecutor::TextureUniforms(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p,
                                        const Step& s) {
    const auto& t = tex.transform;
    const bool rt = s.has_texture && s.using_rt;
    u_.use_texture = s.has_texture ? 1 : 0;
    u_.depth_fmt = rt ? t.depth_fmt : 0;
    u_.depth_zoff = rt ? t.z_row_offset : 0;
    const int idx_fmt = rt ? t.idx_fmt : 0;
    u_.idx_fmt = idx_fmt;
    if (!(p.inherited_uniforms & kInheritUvNative)) u_.uv_native = (s.has_texture && t.uv_native && idx_fmt == 0) ? 1 : 0;
    if (!(p.inherited_uniforms & kInheritDrawScale)) u_.draw_scale = std::max<int>(1, p.scale);
    int idx_self = idx_fmt != 0 ? static_cast<int>(t.idx_self) : 0;
    if (idx_fmt == 6 && ir.tex.fst) idx_self |= 0x1000;
    u_.idx_self = idx_self;
    const bool clut_raw = idx_fmt == 4 && t.idx_self != 0 && tex.enabled && ir.tex.tfx == 3 && ir.tex.tcc == 1;
    u_.clut_raw = clut_raw ? 1 : 0;
    u_.flip_v = 0;
    if (t.region.applied) {
        u_.tex_size_x = static_cast<float>(t.region.w);
        u_.tex_size_y = static_cast<float>(t.region.h);
    } else {
        u_.tex_size_x = static_cast<float>(t.uv_tex_w);
        u_.tex_size_y = static_cast<float>(t.uv_tex_h);
    }
    u_.tfx = tex.enabled ? static_cast<int>(ir.tex.tfx) : 0;
    const int levels = rt ? 0 : t.mip_levels;
    u_.mip_levels = levels;
    u_.mip_lcm = t.mip_lcm ? 1 : 0;
    u_.tex_lod_hw = s.hw_lod ? 1 : 0;
    u_.mip_k = static_cast<float>(t.mip_k) / 16.0f;
    u_.mip_l = static_cast<float>(1u << (t.mip_l & 3));
    u_.tcc = tex.enabled ? static_cast<int>(ir.tex.tcc) : 0;
}

bool MetalDrawExecutor::ConvertVertices(const gs::DrawIr& ir, const gs::TextureSource& tex, Step& s) {
    const auto& t = tex.transform;
    const size_t n = ir.verts.size();
    scratch_.resize(n);
    const bool textured = tex.enabled && t.uv_tex_w > 0 && t.uv_tex_h > 0;
    const VertexConvert::UvMode mode = !textured ? VertexConvert::UvMode::Untextured
                                                 : (ir.tex.fst ? VertexConvert::UvMode::UvFixed : VertexConvert::UvMode::Stq);
    const VertexConvert::FusedBboxes bb = VertexConvert::Convert(ir.verts.data(), n, scratch_.data(), mode, t.uv_pre_w, t.uv_pre_h);
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

void MetalDrawExecutor::RegisterUniforms(const gs::DrawIr& ir, const DrawParams& p) {
    const auto& r = ir.raster;
    u_.blend_enabled = ir.alpha_blend ? 1 : 0;
    u_.fba = r.fba ? 1 : 0;
    u_.colclamp = r.colclamp ? 1 : 0;
    u_.fb16 = r.frame.psm == 0x0A ? 1 : 0;
    if (!(p.inherited_uniforms & kInheritCt16Quant)) u_.ct16q = Is16BitFrame(r.frame.psm) ? std::max<int>(1, p.scale) : 0;
    if (!(p.inherited_uniforms & kInheritDither)) {
        u_.dthe = r.dthe ? 1 : 0;
        u_.dimx_lo = static_cast<int32_t>(static_cast<uint32_t>(r.dimx & 0xFFFFFFFFull));
        u_.dimx_hi = static_cast<int32_t>(static_cast<uint32_t>(r.dimx >> 32));
    }
    u_.scanmsk = ir.scanmsk;
    u_.ate = r.test.ate ? 1 : 0;
    u_.atst = r.test.atst;
    u_.aref = r.test.aref;
    u_.afail = r.test.afail;
    u_.afail_pass = 0;
    u_.date = r.test.date ? 1 : 0;
    u_.datm = r.test.datm ? 1 : 0;
    u_.fog_enabled = ir.fog ? 1 : 0;
    u_.fogcol_r = r.fogcol_r / 255.0f;
    u_.fogcol_g = r.fogcol_g / 255.0f;
    u_.fogcol_b = r.fogcol_b / 255.0f;
}

void MetalDrawExecutor::ClassifyRoutes(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p, Step& s) {
    const auto& r = ir.raster;
    s.alpha_blend = ir.alpha_blend;
    s.fbmsk16 = 0;
    s.fbmsk16_exact = false;
    s.fbmsk32_exact = false;
    if (Is16BitFrame(r.frame.psm) && r.frame.fbmsk != 0) {
        s.fbmsk16 = gs::HalfwordShuffle::Fbmsk16From32(r.frame.fbmsk);
        s.fbmsk16_exact = Fbmsk16Exact(s.fbmsk16);
    } else if (!Is16BitFrame(r.frame.psm)) {
        s.fbmsk32_exact = Fbmsk32Partial(r.frame.fbmsk);
    }
    s.idx_self_serve = s.has_texture && s.using_rt && tex.transform.idx_fmt == 4 && tex.transform.idx_self != 0;
    // Only the UV-addressed self read needs the copy; every fragment-positioned destination
    // read is the framebuffer fetch.
    s.needs_rt_copy = s.idx_self_serve;
    s.wrap_route = BlendFfMap::RouteWrapBlend(ir.alpha_blend, r.colclamp);
    (void)p;
}

// The whole-target copy for the UV-addressed self read (the GL arm's force_full rule).
void MetalDrawExecutor::CdCopy(const DrawParams& p, Step& s) {
    MetalSurface* copy = dev_.SurfaceMut(p.rt_copy);
    MetalSurface* att = dev_.SurfaceMut(p.alt16_route && p.alt16.id ? p.alt16 : p.color);
    if (!p.rt_copy_available || !copy || !copy->tex || !att || !att->tex) return;
    bool skip = false;
    if (p.cd_copy) {
        const int8_t route_now = p.alt16_route ? 1 : 0;
        if (p.cd_copy->last_route != route_now) {
            CdCopyPolicy::Invalidate(*p.cd_copy);
            p.cd_copy->last_route = route_now;
        }
        int oc[4];
        if (!CdCopyPolicy::Decide(*p.cd_copy, 0, 0, 0, 0, /*force_full=*/true, p.target_w, p.target_h, oc)) skip = true;
    }
    if (!skip) {
        const int sc = s.scale;
        dev_.CopyTexture(*att, *copy, 0, 0, p.target_w * sc, p.target_h * sc);
        s.cd_copied = true;
        s.cd_rect = Rect{0, 0, 0, 0};
    }
    s.tex1 = copy->tex;
    dev_.Touch(*copy);
}

void MetalDrawExecutor::BlendState(const gs::DrawIr& ir, const DrawParams& p, Step& s) {
    const auto& a = ir.raster.alpha;
    // Every destination read is the framebuffer fetch: the shader blend is the ONE blend
    // route (the integer GS equation; the GL fixed-function arm has no counterpart here).
    s.sw_blend_active = true;
    if (s.needs_rt_copy) CdCopy(p, s);
    if (!(p.inherited_uniforms & kInheritBlendAbcd)) {
        u_.blend_a = a.a;
        u_.blend_b = a.b;
        u_.blend_c = a.c;
        u_.blend_d = a.d;
        u_.fix_alpha = a.fix / 128.0f;
    }
    u_.sw_blend = 1;
}

void MetalDrawExecutor::DepthState(const gs::DrawIr& ir, const DrawParams& p, Step& s) {
    const auto& t = ir.raster.test;
    MetalSurface* zsrc = dev_.SurfaceMut(p.zsrc);
    const bool armed = p.zsrc_armed && zsrc && zsrc->tex;
    s.tex4 = armed ? zsrc->tex : nullptr;
    if (armed) dev_.Touch(*zsrc);
    u_.zsrc_en = armed ? t.ztst : 0;
    if (!(p.inherited_uniforms & kInheritZsrcMap)) {
        u_.zsrc_num = armed ? std::max<int>(1, p.zsrc_scale) : 1;
        u_.zsrc_den = armed ? std::max<int>(1, p.scale) : 1;
    }
    const MetalSurface* d = dev_.Surface(p.depth);
    const bool has_depth = d && d->tex && d->desc.format == SurfaceFormat::Depth32f;
    if (armed || !has_depth) {
        s.depth.compare = 1;
        s.depth.write = 0;
    } else if (t.zte) {
        switch (t.ztst) {
            case 0: s.depth.compare = 0; break;
            case 1: s.depth.compare = 1; break;
            case 2: s.depth.compare = 2; break;
            case 3: s.depth.compare = 3; break;
            default: s.depth.compare = 2; break;
        }
        s.depth.write = (!ir.raster.zbuf.zmsk && s.afail.write_depth) ? 1 : 0;
    } else {
        s.depth.compare = 1;
        s.depth.write = 0;
    }
}

void MetalDrawExecutor::ColorMaskState(const gs::DrawIr& ir, const Step& s) {
    const uint32_t mask = ir.raster.frame.fbmsk;
    const uint8_t psm = ir.raster.frame.psm;
    const bool engaged = s.fbmsk16_exact;
    const bool engaged32 = s.fbmsk32_exact;
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
    if (!s.afail.write_rgb) wr = wg = wb = false;
    if (!s.afail.write_alpha) wa = false;
    u_.color_mask = (wr ? 1 : 0) | (wg ? 2 : 0) | (wb ? 4 : 0) | (wa ? 8 : 0);
    u_.fbmsk16_en = engaged ? 1 : 0;
    u_.fbmsk16 = static_cast<int32_t>(s.fbmsk16);
    u_.fbmsk32_en = engaged32 ? 1 : 0;
    u_.fbmsk32 = static_cast<int32_t>(engaged32 ? mask : 0u);
}

// The GS scissor is inclusive and always active; the half-open span scales exactly to
// physical pixels. Returns false for an empty scissor (nothing to draw).
bool MetalDrawExecutor::ApplyScissor(const gs::DrawIr& ir, const Step& s) {
    const auto& sc = ir.raster.scissor;
    const int sx0 = std::max(0, static_cast<int>(sc.x0));
    const int sy0 = std::max(0, static_cast<int>(sc.y0));
    const int sx1 = std::min(s.render_w - 1, static_cast<int>(sc.x1));
    const int sy1 = std::min(s.render_h - 1, static_cast<int>(sc.y1));
    if (sx1 < sx0 || sy1 < sy0) return false;
    const int x = sx0 * s.scale, y = sy0 * s.scale, w = (sx1 - sx0 + 1) * s.scale, h = (sy1 - sy0 + 1) * s.scale;
    if (x == sc_[0] && y == sc_[1] && w == sc_[2] && h == sc_[3]) return true;
    sc_[0] = x; sc_[1] = y; sc_[2] = w; sc_[3] = h;
    MTLScissorRect r = {static_cast<NSUInteger>(x), static_cast<NSUInteger>(y), static_cast<NSUInteger>(w),
                        static_cast<NSUInteger>(h)};
    [(id<MTLRenderCommandEncoder>)s.encoder setScissorRect:r];
    return true;
}

void MetalDrawExecutor::Emit(const gs::DrawIr& ir, Step& s) {
    // The pass may have been ended by a blit (the copy) or a ring wrap: re-open it (load;
    // the clear, if any, landed in the first pass).
    void* enc = dev_.BeginTargetPass(s.color_id, s.depth_id, false, false, 0u, 0.f);
    if (!enc) return;
    if (enc != enc_shadow_) {
        OnEncoderEnded();
        enc_shadow_ = enc;
    }
    s.encoder = enc;
    id<MTLRenderCommandEncoder> e = (id<MTLRenderCommandEncoder>)enc;
    void* ps = dev_.Pipelines().Pipeline(MetalProgram::Ps2, true, true, PackSpecKey(SpecKey()));
    if (!ps) return;
    if (bound_pipeline_ != ps) {
        [e setRenderPipelineState:(id<MTLRenderPipelineState>)ps];
        bound_pipeline_ = ps;
        dev_.CensusMut().program_binds++;
    }
    void* ds = dev_.Pipelines().DepthStencil(s.depth);
    if (bound_depth_state_ != ds) {
        [e setDepthStencilState:(id<MTLDepthStencilState>)ds];
        bound_depth_state_ = ds;
    }
    if (!vertex_bound_) {
        [e setVertexBuffer:(id<MTLBuffer>)dev_.VertexRing() offset:0 atIndex:0];
        vertex_bound_ = true;
    }
    const int vw = s.render_w * s.scale, vh = s.render_h * s.scale;
    if (vw != vp_w_ || vh != vp_h_) {
        MTLViewport vp = {0.0, 0.0, static_cast<double>(vw), static_cast<double>(vh), 0.0, 1.0};
        [e setViewport:vp];
        vp_w_ = vw;
        vp_h_ = vh;
    }
    if (!ApplyScissor(ir, s)) return;
    MetalVertUniforms vu;
    vu.screen_size_x = static_cast<float>(s.render_w);
    vu.screen_size_y = static_cast<float>(s.render_h);
    [e setVertexBytes:&vu length:sizeof(vu) atIndex:1];
    [e setFragmentBytes:&u_ length:sizeof(u_) atIndex:0];
    void* texes[6] = {s.tex0, s.tex1, s.tex2, s.tex3, s.tex4, s.tex5};
    for (int i = 0; i < 6; ++i) {
        void* want = texes[i] ? texes[i] : (i == 5 ? dev_.DummyDepthTexture() : dev_.DummyTexture());
        if (bound_tex_[i] != want) {
            [e setFragmentTexture:T(want) atIndex:static_cast<NSUInteger>(i)];
            bound_tex_[i] = want;
            dev_.CensusMut().texture_binds++;
        }
    }
    void* sampler = dev_.Pipelines().Sampler(s.sampler);
    if (bound_sampler_ != sampler) {
        [e setFragmentSamplerState:(id<MTLSamplerState>)sampler atIndex:0];
        bound_sampler_ = sampler;
    }
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:static_cast<NSUInteger>(s.first)
          vertexCount:static_cast<NSUInteger>(s.tri_verts)];
    dev_.CensusMut().draw_calls++;
    dev_.SplitPassIfDue();   // the GPU starts on this chunk while the CPU encodes the next
}

// The specialisation of the register program for the current uniforms (the shader's
// function constants: each folds the branch the matching uniform drives).
MetalSpecKey MetalDrawExecutor::SpecKey() const {
    MetalSpecKey k;
    k.use_texture = u_.use_texture != 0 ? 1 : 0;
    if (k.use_texture) {
        const bool depth_view = u_.depth_fmt >= 1 && u_.depth_fmt <= 3;
        const bool indexed = u_.idx_fmt >= 4 && u_.idx_fmt <= 7;
        k.tex_path = depth_view ? 2 : indexed ? 3 : (u_.mip_levels > 0 ? 1 : 0);
    }
    k.fog = u_.fog_enabled != 0 ? 1 : 0;
    k.alpha_test = u_.ate != 0 ? 1 : 0;
    k.date = u_.date != 0 ? 1 : 0;
    k.zsrc = u_.zsrc_en != 0 ? 1 : 0;
    k.blend = (u_.blend_enabled != 0 && u_.sw_blend != 0) ? 1 : 0;
    k.fbmsk16 = u_.fbmsk16_en != 0 ? 1 : 0;
    k.fbmsk32 = u_.fbmsk32_en != 0 ? 1 : 0;
    k.ct16 = (u_.fb16 != 0 || u_.ct16q != 0) ? 1 : 0;
    k.fba = u_.fba != 0 ? 1 : 0;
    k.mask_all = u_.color_mask == 0xF ? 1 : 0;
    k.scanmsk = (u_.scanmsk == 2 || u_.scanmsk == 3) ? 1 : 0;
    return k;
}

DrawResult MetalDrawExecutor::MakeResult(const gs::DrawIr& ir, const DrawParams& p, const Step& s) const {
    DrawResult r;
    r.drew = true;
    r.bbox_valid = s.bb_valid;
    for (int i = 0; i < 4; i++) { r.bbox_tri[i] = s.bb_tri[i]; r.bbox_all[i] = s.bb_all[i]; }
    r.alt16_route = p.alt16_route;
    r.sw_blend_active = s.alpha_blend && s.sw_blend_active;
    r.needs_rt_copy = s.needs_rt_copy;
    r.route_rt_blend = false;
    r.wrap_route = s.wrap_route;
    r.fbmsk16_exact = s.fbmsk16_exact;
    r.fbmsk16_engaged = s.fbmsk16_exact;
    r.fbmsk16 = s.fbmsk16;
    r.cd_copy_performed = s.cd_copied;
    r.cd_copy_rect = s.cd_rect;
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
