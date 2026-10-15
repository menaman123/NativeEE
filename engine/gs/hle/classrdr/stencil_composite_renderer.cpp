// gs/hle/classrdr/stencil_composite_renderer.cpp -- Accept, the three native passes,
// the strip ladder, materialise, the invert predicate and the counters.
#include "engine/gs/hle/classrdr/stencil_composite_renderer.h"

#include <algorithm>
#include <cmath>

#include "engine/gs/format/psm_layout.h"
#include "engine/gs/hle/classrdr/class_classifier.h"

namespace ps2r {
namespace gs {

namespace {

// The CLUT span the invert predicate must see committed: one CT32 256-entry table.
constexpr uint32_t kClutBytes = 0x2000;

render::StencilState Stencil(uint8_t ref, uint8_t cmp, uint8_t write, render::StencilState::Op op) {
    render::StencilState s;
    s.enabled = true;
    s.func = render::StencilState::kEqual;
    s.ref = ref;
    s.cmp_mask = cmp;
    s.write_mask = write;
    s.pass_op = op;
    return s;
}

}  // namespace

StencilCompositeRenderer::StencilCompositeRenderer(const Shapes& shapes, SequenceRecipe& seq,
                                                   const ClassRegistry* registry)
    : shapes_(shapes), seq_(seq), registry_(registry) {}

// The stencil attachment is freed only through a backend the caller vouches for;
// the cached pipeline is forgotten so nothing dangles across a backend
// replacement (Draw / WarmUp re-cache it).
void StencilCompositeRenderer::Release(render::Backend& be) {
    if (stencil_.id) be.DestroySurface(stencil_);
    stencil_ = render::SurfaceId{};
    res_color_ = render::SurfaceId{};
    sten_w_ = sten_h_ = 0;
    pipe_ = nullptr;
}

bool StencilCompositeRenderer::Active() const {
    return registry_ && registry_->Enabled(ClassClassifier::kStencilComposite) &&
           registry_->Slot(ClassClassifier::kStencilComposite) == this;
}

// CLEAR shape -> yes; a raw strip / fan joins the stencil pipeline only when the
// stamped family can too (a split world class would count half the winding); the
// INVERT shape + the self-read pins + the pinned vertex colour + the backend predicate.
bool StencilCompositeRenderer::Accept(const DrawIr& ir) const {
    if (shapes_.clear && shapes_.clear->Matches(ir)) return true;
    if ((shapes_.strip && shapes_.strip->Matches(ir)) || (shapes_.fan && shapes_.fan->Matches(ir))) {
        return registry_ && registry_->Enabled(ClassClassifier::kWorldTristrip) &&
               registry_->Slot(ClassClassifier::kWorldTristrip) != nullptr;
    }
    if (shapes_.invert && shapes_.invert->Matches(ir) && ir.tex.tbp == ir.raster.frame.fbp &&
        ir.tex.tbw == ir.raster.frame.fbw && ir.tex.wms <= 1 && ir.tex.wmt <= 1 &&
        seq_.InvertVertexColorOk(ir))
        return InvertBackendOk(ir);
    return false;
}

// A live 32-bit-class target (CT32 or the colour-authored PSMZ32 copy) with a
// destination copy, content, drawn this frame, no deferred writeback under the
// CLUT, and the CLUT bytes the identity-invert table.
bool StencilCompositeRenderer::InvertBackendOk(const DrawIr& ir) const {
    if (!pipe_) { cnt_.inv_no_target++; return false; }
    const RenderTarget* t = pipe_->Cache().Find(ir.raster.frame.fbp);
    if (!t) { cnt_.inv_no_target++; return false; }
    if (!t->views.HasColor() || !t->views.rt_copy.id) { cnt_.inv_no_tex++; return false; }
    if (!t->coherence.has_content) { cnt_.inv_no_content++; return false; }
    if (t->coherence.last_draw_frame != pipe_->Present().FrameCount()) { cnt_.inv_stale++; return false; }
    if (t->identity.render_psm != 0x00 && t->identity.render_psm != 0x30) { cnt_.inv_psm++; return false; }
    const uint32_t cbp = ir.tex.cbp;
    const uint32_t c0 = cbp % VramImage::kBytes;
    for (const PendingRtSyncLedger::Entry& e : pipe_->Cache().Ledger().Entries())
        if (PendingRtSyncLedger::Overlaps(e, c0, kClutBytes)) { cnt_.inv_pending++; return false; }
    const uint8_t* vram = pipe_->Vram().Bytes();
    const bool ok = SequenceRecipe::ClutIsInvertTable(
        [vram](uint32_t addr) { return vram[addr % VramImage::kBytes]; },
        [cbp](int x, int y) {
            return PsmLayout::PixelByteAddress(Psm::CT32, uint32_t(x), uint32_t(y), cbp, 64);
        });
    if (!ok) cnt_.inv_clut++;
    return ok;
}

int StencilCompositeRenderer::WarmUp(DrawPipeline& pipe) {
    pipe_ = &pipe;
    pipe.SetMaterializer(this);
    return 0;   // the programs are the backend's (built with its shader library)
}

void StencilCompositeRenderer::Draw(DrawPipeline& pipe, const DrawIr& ir) {
    pipe_ = &pipe;
    pipe.SetMaterializer(this);
    if (shapes_.clear && shapes_.clear->Matches(ir)) DrawClear(pipe, ir);
    else if ((shapes_.strip && shapes_.strip->Matches(ir)) || (shapes_.fan && shapes_.fan->Matches(ir)))
        DrawStripOrWorld(pipe, ir);
    else DrawInvert(pipe, ir);
}

// Pass A: the generic lowering (the CLEAR's native program IS the register
// program's live arms), then the FSM: every vertex the base colour on a
// native-scale target primes; a scaled target declines; anything else idles. An
// alias refresh demanded under the CLEAR (unreachable: its FBMSK is 0) idles the
// sequence and leaves the whole draw to the generic path, uncounted.
void StencilCompositeRenderer::DrawClear(DrawPipeline& pipe, const DrawIr& ir) {
    DrawPipeline::Prepared pr;
    DrawPipeline::PrepareOptions opt;
    opt.abort_on_alias = true;
    if (!pipe.Prepare(ir, &pr, opt)) {
        if (pr.alias_aborted) {
            seq_.Idle();
            pipe.DrawGeneric(ir);
        }
        return;
    }
    const render::DrawResult res = pipe.Backend().Draw(ir, pr.src, pr.params);
    if (!res.drew) return;
    pipe.Finish(ir, pr, res);
    cnt_.clears++;
    const bool primes = seq_.ClearPrimes(ir);
    if (primes && pr.target->identity.scale != 1) {
        cnt_.scale_declines++;
        seq_.Idle();
    } else if (primes) {
        seq_.Prime(ir.raster.frame.fbp);
        cnt_.primes++;
    } else {
        seq_.Idle();
    }
}

// Pass D: the folded byte-NOT over the whole-target destination copy; the
// resolver is bypassed (the shader reads the copy on unit 1, never a texture
// source). Without a copy the generic path draws it (the reference's DrawBatch arm).
void StencilCompositeRenderer::DrawInvert(DrawPipeline& pipe, const DrawIr& ir) {
    DrawPipeline::Prepared pr;
    DrawPipeline::PrepareOptions opt;
    opt.resolve_texture = false;
    if (!pipe.Prepare(ir, &pr, opt)) return;
    pr.src.enabled = true;
    pr.src.transform.uv_tex_w = pr.src.transform.uv_tex_h = 1;
    pr.src.transform.idx_fmt = 4;
    pr.src.transform.idx_self = 1;
    pr.params.program = render::StencilProgram::Invert;
    const render::DrawResult res = pipe.Backend().Draw(ir, pr.src, pr.params);
    if (!res.drew) {
        pipe.DrawGeneric(ir);
        return;
    }
    pipe.Finish(ir, pr, res);
    cnt_.inverts++;
}

// The steer: a conforming strip (flat +1 / -1 colour, a live colour target at its
// ZBUF, the sequence live on its buffer) counts in the stencil; every other strip
// takes the colour path -- materialising an armed sequence on the same buffer or
// demoting a primed one first. An inactive slot IS the colour path (no hook).
void StencilCompositeRenderer::DrawStripOrWorld(DrawPipeline& pipe, const DrawIr& ir) {
    pipe_ = &pipe;
    pipe.SetMaterializer(this);
    if (!Active()) {
        DrawPipeline::PrepareOptions opt;
        opt.run_hook = false;
        pipe.DrawGeneric(ir, opt);
        return;
    }
    const SequenceRecipe::StripDelta delta = seq_.ClassifyStripVerts(ir);
    bool stencil_ok = delta != SequenceRecipe::kStripDecline;
    if (stencil_ok) {
        stencil_ok = pipe.ZsrcRouteAvailable(ir);
    }
    if (stencil_ok) stencil_ok = seq_.LiveOn(ir.raster.frame.fbp);
    if (!stencil_ok) {
        if (seq_.Fbp() == ir.raster.frame.fbp) {
            if (seq_.GetState() == SequenceRecipe::kArmed) {
                seq_.Idle();
                Materialize(pipe);
            } else if (seq_.GetState() == SequenceRecipe::kPrimed) {
                seq_.Idle();
                cnt_.demotes++;
            }
        }
        DrawStripColor(pipe, ir);
        return;
    }
    DrawStripStencil(pipe, ir, delta);
}

void StencilCompositeRenderer::DrawStripColor(DrawPipeline& pipe, const DrawIr& ir) {
    cnt_.strips_color++;
    DrawPipeline::PrepareOptions opt;
    opt.run_hook = false;   // the reference's colour strip path has no pre-draw hook
    pipe.DrawGeneric(ir, opt);
}

void StencilCompositeRenderer::MaterializeIfArmedOn(DrawPipeline& pipe, uint32_t fbp) {
    if (seq_.GetState() == SequenceRecipe::kArmed && seq_.Fbp() == fbp) {
        seq_.Idle();
        Materialize(pipe);
    }
}

bool StencilCompositeRenderer::EnsureStencil(render::Backend& be, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (stencil_.id && sten_w_ == w && sten_h_ == h) return true;
    if (stencil_.id) be.DestroySurface(stencil_);
    render::SurfaceDesc d;
    d.width = uint32_t(w);
    d.height = uint32_t(h);
    d.format = render::SurfaceFormat::DepthStencil24_8;
    d.renderable = false;
    stencil_ = be.CreateSurface(d);
    sten_w_ = w;
    sten_h_ = h;
    return stencil_.id != 0;
}

// The stencil ladder for one strip: the generic pre-draw sequence without the hook
// (the armed accumulator's own deferred writeback held out of the pre-draw flush),
// aborting to the colour path when the alias refresh fired (an out-of-band colour
// rewrite: an armed sequence materialises, a primed one dies), the sequence is
// not live on this buffer, or the route changed; ARM on the first strip; then the
// three passes, the extent union, and the generic coherence tail.
void StencilCompositeRenderer::DrawStripStencil(DrawPipeline& pipe, const DrawIr& ir,
                                                SequenceRecipe::StripDelta delta) {
    if (ir.verts.size() < 3 || ir.raster.frame.fbw == 0) return;
    const uint32_t fbp = ir.raster.frame.fbp;
    render::Backend& be = pipe.Backend();
    DrawPipeline::Prepared pr;
    DrawPipeline::PrepareOptions opt;
    opt.run_hook = false;
    opt.abort_on_alias = true;
    opt.hold_fbp = seq_.HoldsEntry(fbp) ? fbp : DrawPipeline::kNoFbp;
    const bool ladder_clean = pipe.Prepare(ir, &pr, opt);
    const bool seq_ok = ladder_clean && seq_.LiveOn(fbp) &&
                        (seq_.GetState() == SequenceRecipe::kPrimed || seq_.Alt16() == pr.params.alt16_route);
    const render::SurfaceId routed =
        pr.target ? (pr.params.alt16_route ? pr.target->views.alt16 : pr.target->views.color)
                  : render::SurfaceId{};
    if (!seq_ok || routed.id == 0) {
        if (seq_.GetState() == SequenceRecipe::kArmed && seq_.Fbp() == fbp) {
            seq_.Idle();
            Materialize(pipe);
        } else if (!ladder_clean) {
            seq_.Idle();
        }
        DrawStripColor(pipe, ir);   // its own ladder re-runs (and imports) benignly
        return;
    }
    RenderTarget& target = *pr.target;
    if (seq_.GetState() == SequenceRecipe::kPrimed) {
        if (!EnsureStencil(be, target.identity.width, target.identity.height)) {
            seq_.Idle();
            DrawStripColor(pipe, ir);
            return;
        }
        be.ClearStencil(stencil_, seq_.K().stencil_base);
        res_color_ = routed;
        seq_.Arm(pr.params.alt16_route);
        cnt_.arms++;
    } else if (sten_w_ != target.identity.width || sten_h_ != target.identity.height ||
               res_color_.id != routed.id) {
        seq_.Idle();
        Materialize(pipe);
        DrawStripColor(pipe, ir);
        return;
    }
    // Pass 1: the geometry into the stencil alone, toggling bit 7 where it lands.
    pr.params.program = render::StencilProgram::StripCount;
    pr.params.stencil = stencil_;
    pr.params.stencil_state = Stencil(0x00, 0x80, 0x80, render::StencilState::kInvert);
    const render::DrawResult res = be.Draw(ir, pr.src, pr.params);
    if (!res.drew) return;
    // The strip's rect: the triangle bbox within the scissor, clamped to the stencil.
    const float* bb = res.bbox_valid ? res.bbox_tri : res.bbox_all;
    const DrawIr::Scissor& sc = ir.raster.scissor;
    int x0 = std::max(int(std::floor(bb[0])), int(sc.x0));
    int y0 = std::max(int(std::floor(bb[1])), int(sc.y0));
    int x1 = std::min(int(std::ceil(bb[2])), int(sc.x1) + 1);
    int y1 = std::min(int(std::ceil(bb[3])), int(sc.y1) + 1);
    x0 = std::max(0, std::min(x0, sten_w_));
    y0 = std::max(0, std::min(y0, sten_h_));
    x1 = std::max(0, std::min(x1, sten_w_));
    y1 = std::max(0, std::min(y1, sten_h_));
    if (x1 > x0 && y1 > y0) {
        // Pass 2: count once per touched pixel; pass 3: clear the touch bit.
        const render::Rect rect{x0, y0, x1 - x0, y1 - y0};
        be.DrawConstQuad(render::SurfaceId{}, stencil_, rect, 0, false,
                         Stencil(0x80, 0x80, seq_.K().counter_mask,
                                 delta > 0 ? render::StencilState::kIncrWrap : render::StencilState::kDecrWrap));
        be.DrawConstQuad(render::SurfaceId{}, stencil_, rect, 0, false,
                         Stencil(0x80, 0x80, 0x80, render::StencilState::kInvert));
        cnt_.dedupe_draws += 2;
        seq_.ExtendExtent(x0, y0, x1, y1);
    }
    pipe.Finish(ir, pr, res);   // the per-strip CT16S ledger record -- load-bearing
    cnt_.strips_stencil++;
}

// The 32 stencil-masked constant quads over the strip extent into the routed view,
// then the stamps one covering draw would have left. The FSM is already idle;
// nothing here touches the ledger (every strip recorded its own entry).
void StencilCompositeRenderer::Materialize(DrawPipeline& pipe) {
    const uint32_t fbp = seq_.Fbp();
    const bool alt16 = seq_.Alt16();
    RenderTarget* target = pipe.Cache().Find(fbp);
    if (!target || !stencil_.id) return;
    const int ex0 = std::max(0, seq_.ExtX0()), ey0 = std::max(0, seq_.ExtY0());
    const int ex1 = std::min(sten_w_, seq_.ExtX1()), ey1 = std::min(sten_h_, seq_.ExtY1());
    if (ex1 <= ex0 || ey1 <= ey0) return;   // no strip landed: bytes untouched
    const render::SurfaceId routed = alt16 ? target->views.alt16 : target->views.color;
    if (routed.id == 0 || routed.id != res_color_.id) return;   // surface changed: leave the bytes
    cnt_.materializes++;
    const render::Rect rect{ex0, ey0, ex1 - ex0, ey1 - ey0};
    render::Backend& be = pipe.Backend();
    for (int v = 0; v < 32; v++) {
        const uint32_t b = seq_.MaterializeByte(uint8_t(v));
        be.DrawConstQuad(routed, stencil_, rect, 0xFF000000u | (b << 16) | (b << 8) | b, false,
                         Stencil(uint8_t(v), seq_.K().counter_mask, 0x00, render::StencilState::kKeep));
    }
    cnt_.mat_draws += 32;
    target->MarkMaterialized(ex0, ey0, ex1, ey1, alt16, pipe.Present().FrameCount(), pipe.GfxEpoch());
}

void StencilCompositeRenderer::Emit(std::FILE* f) const {
    if (!f) return;
    const Counters& c = cnt_;
    const uint64_t total = c.clears + c.strips_stencil + c.strips_color + c.inverts;
    if (!total && !c.materializes) return;
    std::fprintf(f,
                 "[GS-S9RDR] on=1 clears=%llu primes=%llu arms=%llu strips_stencil=%llu strips_color=%llu"
                 " inverts=%llu materializes=%llu mat_draws=%llu demotes=%llu scale_declines=%llu"
                 " dedupe_draws=%llu\n",
                 (unsigned long long)c.clears, (unsigned long long)c.primes, (unsigned long long)c.arms,
                 (unsigned long long)c.strips_stencil, (unsigned long long)c.strips_color,
                 (unsigned long long)c.inverts, (unsigned long long)c.materializes,
                 (unsigned long long)c.mat_draws, (unsigned long long)Demotes(),
                 (unsigned long long)c.scale_declines, (unsigned long long)c.dedupe_draws);
    const uint64_t declines = c.inv_no_target + c.inv_no_tex + c.inv_no_content + c.inv_stale + c.inv_psm +
                              c.inv_pending + c.inv_clut;
    if (declines)
        std::fprintf(f,
                     "[GS-S9RDR] invert_declines no_target=%llu no_tex=%llu no_content=%llu stale=%llu"
                     " psm=%llu pending=%llu clut=%llu\n",
                     (unsigned long long)c.inv_no_target, (unsigned long long)c.inv_no_tex,
                     (unsigned long long)c.inv_no_content, (unsigned long long)c.inv_stale,
                     (unsigned long long)c.inv_psm, (unsigned long long)c.inv_pending,
                     (unsigned long long)c.inv_clut);
}

}  // namespace gs
}  // namespace ps2r
