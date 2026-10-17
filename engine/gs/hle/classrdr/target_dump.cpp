// gs/hle/classrdr/target_dump.cpp -- TargetDump implementation.
#include "engine/gs/hle/classrdr/target_dump.h"

#include <algorithm>
#include <cstdio>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/targets/render_target_cache.h"

namespace ps2r {
namespace gs {

void TargetDump::Arm(const Request& r) {
    req_ = r;
    written_ = 0;
    pending_post_ = false;
    std::sort(req_.ordinals.begin(), req_.ordinals.end());
    armed_.store(true, std::memory_order_release);
}

void TargetDump::Disarm() {
    armed_.store(false, std::memory_order_release);
    pending_post_ = false;
}

bool TargetDump::QualifiesByClass(const DrawIr& ir, uint32_t fbp, bool fbmsk_class) {
    if (ir.tex_enabled && ir.tex.tbp == fbp && (IsHighByteIndexed(ir.tex.psm) || Is16BitColor(ir.tex.psm))) return true;
    if (ir.raster.frame.fbp != fbp) return false;
    if (Is16BitColor(ir.raster.frame.psm)) return true;
    const uint32_t m = ir.raster.frame.fbmsk;
    return fbmsk_class && m != 0 && m != 0xFF000000u;
}

bool TargetDump::Qualifies(const DrawIr& ir) const {
    if (std::binary_search(req_.ordinals.begin(), req_.ordinals.end(), ordinal_)) return true;
    return req_.classes && QualifiesByClass(ir, req_.fbp, req_.fbmsk_class);
}

void TargetDump::OnPreDraw(const DrawIr& ir, RenderTargetCache& cache, render::Backend& be, uint64_t seq) {
    pending_post_ = false;
    if (!Armed() || written_ >= req_.cap) return;
    if (!Qualifies(ir)) return;
    Write(ir, cache, be, seq, /*post=*/false);
    pending_post_ = req_.post;
}

void TargetDump::OnPostDraw(const DrawIr& ir, RenderTargetCache& cache, render::Backend& be, uint64_t seq) {
    const bool post = pending_post_;
    pending_post_ = false;
    if (post && Armed() && written_ < req_.cap) Write(ir, cache, be, seq, /*post=*/true);
    ++ordinal_;
}

// One PAM per image; the index line names the draw. Readback is physical texels,
// rows as the attachment stores them (PS2 row 0 first); a scaled target is
// written at its physical size (the header carries both).
void TargetDump::Write(const DrawIr& ir, RenderTargetCache& cache, render::Backend& be, uint64_t seq,
                       bool post) {
    const RenderTarget* t = cache.Find(req_.fbp);
    if (!t || !t->views.HasColor()) return;
    const render::SurfaceDesc d = be.DescribeSurface(t->views.color);
    if (d.width == 0 || d.height == 0) return;
    const size_t stride = static_cast<size_t>(d.width) * 4u;
    scratch_.resize(stride * d.height);
    be.Readback(t->views.color, render::Rect{0, 0, static_cast<int32_t>(d.width), static_cast<int32_t>(d.height)},
                scratch_.data(), stride);
    char name[96];
    std::snprintf(name, sizeof name, "/rt_%06x_%04u_%s.pam", req_.fbp, ordinal_, post ? "post" : "pre");
    const std::string path = req_.dir + name;
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", d.width, d.height);
        std::fwrite(scratch_.data(), 1, scratch_.size(), f);
        std::fclose(f);
        ++written_;
    }
    if (std::FILE* ix = std::fopen((req_.dir + "/rt_dump_index.txt").c_str(), "a")) {
        std::fprintf(ix,
                     "ordinal=%u %s seq=%llu file=%s w=%u h=%u scale=%u FRAME=%06x psm=%02x fbmsk=%08x TEX=%d tbp=%06x "
                     "tpsm=%02x cbp=%06x tfx=%u tcc=%u abe=%d ALPHA=%u%u%u%u/%02x fba=%d date=%d texa=%u/%u/%d verts=%zu\n",
                     ordinal_, post ? "post" : "pre", static_cast<unsigned long long>(seq), name + 1, d.width, d.height,
                     d.scale, ir.raster.frame.fbp, ir.raster.frame.psm, ir.raster.frame.fbmsk, ir.tex_enabled ? 1 : 0,
                     ir.tex.tbp, ir.tex.psm, ir.tex.cbp, ir.tex.tfx, ir.tex.tcc, ir.alpha_blend ? 1 : 0, ir.raster.alpha.a,
                     ir.raster.alpha.b, ir.raster.alpha.c, ir.raster.alpha.d, ir.raster.alpha.fix, ir.raster.fba ? 1 : 0,
                     ir.raster.test.date ? 1 : 0, ir.tex.texa_ta0, ir.tex.texa_ta1, ir.tex.texa_aem ? 1 : 0,
                     ir.verts.size());
        std::fclose(ix);
    }
}

}  // namespace gs
}  // namespace ps2r
