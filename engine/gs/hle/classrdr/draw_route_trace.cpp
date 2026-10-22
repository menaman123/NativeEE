// gs/hle/classrdr/draw_route_trace.cpp -- DrawRouteTrace implementation.
#include "engine/gs/hle/classrdr/draw_route_trace.h"

namespace ps2r {
namespace gs {

DrawRouteTrace::Cost DrawRouteTrace::Cost::Delta(const render::CallCensus* a, const render::CallCensus* b,
                                                 uint64_t ns) {
    Cost c;
    c.ns = ns;
    if (!a || !b) return c;
    auto d32 = [](uint64_t x, uint64_t y) { return static_cast<uint32_t>(y - x); };
    c.readbacks = d32(a->readbacks, b->readbacks);
    c.readback_bytes = d32(a->readback_bytes, b->readback_bytes);
    c.copies = d32(a->copies, b->copies);
    c.copy_bytes = d32(a->copy_bytes, b->copy_bytes);
    c.uploads = d32(a->uploads, b->uploads);
    c.upload_bytes = d32(a->upload_bytes, b->upload_bytes);
    c.draw_calls = d32(a->draw_calls, b->draw_calls);
    c.texture_binds = d32(a->texture_binds, b->texture_binds);
    c.program_binds = d32(a->program_binds, b->program_binds);
    c.framebuffer_binds = d32(a->framebuffer_binds, b->framebuffer_binds);
    c.state_sets = d32(a->state_sets, b->state_sets);
    c.uniform_sets = d32(a->uniform_sets, b->uniform_sets);
    return c;
}

void DrawRouteTrace::Cost::Add(const Cost& o) {
    ns += o.ns;
    readbacks += o.readbacks; readback_bytes += o.readback_bytes;
    copies += o.copies; copy_bytes += o.copy_bytes;
    uploads += o.uploads; upload_bytes += o.upload_bytes;
    draw_calls += o.draw_calls; texture_binds += o.texture_binds;
    program_binds += o.program_binds; framebuffer_binds += o.framebuffer_binds;
    state_sets += o.state_sets; uniform_sets += o.uniform_sets;
}

void DrawRouteTrace::Note(const DrawIr& ir, const TextureSource& src, const render::DrawResult& res,
                          uint32_t seq, const Cost& cost) {
    if (!armed_) return;
    Line l{};
    l.cost = cost;
    l.seq = seq;
    l.src_frame = ir.src_frame;
    l.prim = ir.prim_type;
    l.abe = ir.alpha_blend ? 1 : 0;
    l.tex = ir.tex_enabled ? 1 : 0;
    const auto& r = ir.raster;
    l.a = r.alpha.a; l.b = r.alpha.b; l.c = r.alpha.c; l.d = r.alpha.d; l.fix = r.alpha.fix;
    l.tfx = static_cast<uint8_t>(ir.tex.tfx);
    l.tcc = static_cast<uint8_t>(ir.tex.tcc);
    l.owner_kind = static_cast<uint8_t>(src.owner.kind);
    l.fmt_case = src.owner.fmt_case;
    l.ate = r.test.ate; l.atst = r.test.atst; l.aref = r.test.aref; l.afail = r.test.afail;
    l.zte = r.test.zte; l.ztst = r.test.ztst; l.zmsk = r.zbuf.zmsk; l.date = r.test.date;
    l.colclamp = r.colclamp; l.pabe = r.pabe; l.fba = r.fba;
    l.fbp = r.frame.fbp; l.fbmsk = r.frame.fbmsk;
    l.tbp = ir.tex.tbp; l.tpsm = ir.tex.psm; l.cbp = ir.tex.cbp;
    l.verts = static_cast<uint32_t>(ir.verts.size());
    if (!ir.verts.empty()) {
        const VertexRec& v = ir.verts[0];
        l.rgba0 = (static_cast<uint32_t>(v.r) << 24) | (static_cast<uint32_t>(v.g) << 16) |
                  (static_cast<uint32_t>(v.b) << 8) | v.a;
    }
    l.drew = res.drew;
    l.sw_blend = res.sw_blend_active;
    l.route_rt_blend = res.route_rt_blend;
    l.needs_rt_copy = res.needs_rt_copy;
    l.cd_copied = res.cd_copy_performed;
    l.wrap_route = res.wrap_route;
    l.fbmsk16_exact = res.fbmsk16_exact;
    cur_.push_back(l);
}

void DrawRouteTrace::NotePresent(const ScanoutComposer::DrawResult& r, const PendingRtSyncLedger& ledger) {
    present_ = r;
    for (size_t i = 0; i < static_cast<size_t>(PendingRtSyncLedger::Reason::kCount); ++i)
        ledger_flushes_[i] = ledger.Flushes(static_cast<PendingRtSyncLedger::Reason>(i));
    ledger_entries_ = ledger.Count();
}

void DrawRouteTrace::OnFrameEnd(const Cost& frame_end, const Cost& frame_total) {
    if (cur_.empty()) return;   // a boundary without draws keeps the last drawn frame
    last_.swap(cur_);
    cur_.clear();
    last_frame_end_ = frame_end;
    last_frame_total_ = frame_total;
}

void DrawRouteTrace::Write(std::FILE* f) const {
    if (!f) return;
    static const char* const kOwner[] = {"none", "surface", "decoded"};
    // The frame summary first: the draws' total cost and the frame end's.
    Cost sum;
    for (const Line& l : last_) sum.Add(l.cost);
    auto row = [f](const char* tag, const Cost& c, size_t n) {
        std::fprintf(f,
                     "%s n=%zu us=%llu readbacks=%u readback_bytes=%u copies=%u copy_bytes=%u uploads=%u "
                     "upload_bytes=%u draw_calls=%u tex_binds=%u prog_binds=%u fbo_binds=%u state_sets=%u uniform_sets=%u\n",
                     tag, n, (unsigned long long)(c.ns / 1000), c.readbacks, c.readback_bytes, c.copies, c.copy_bytes,
                     c.uploads, c.upload_bytes, c.draw_calls, c.texture_binds, c.program_binds, c.framebuffer_binds,
                     c.state_sets, c.uniform_sets);
    };
    row("cost_draws", sum, last_.size());
    row("cost_frame_end", last_frame_end_, 1);
    row("cost_frame_total", last_frame_total_, 1);
    std::fprintf(f, "present source=%d fbp=0x%x fallback=%u stale=%u native=%u\n", static_cast<int>(present_.source),
                 present_.fbp, present_.used_fallback ? 1 : 0, present_.stale_display ? 1 : 0, present_.native ? 1 : 0);
    std::fprintf(f, "ledger entries=%zu flushes(decode,ftt,refresh,predraw,scanout,destroy,present,save)=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                 ledger_entries_, (unsigned long long)ledger_flushes_[0], (unsigned long long)ledger_flushes_[1],
                 (unsigned long long)ledger_flushes_[2], (unsigned long long)ledger_flushes_[3],
                 (unsigned long long)ledger_flushes_[4], (unsigned long long)ledger_flushes_[5],
                 (unsigned long long)ledger_flushes_[6], (unsigned long long)ledger_flushes_[7]);
    for (const Line& l : last_) {
        const char* owner = l.owner_kind < 3 ? kOwner[l.owner_kind] : "?";
        const char* blend = !l.abe ? "off" : (l.sw_blend ? "sw" : (l.route_rt_blend ? "ff-rt" : "ff"));
        std::fprintf(f,
                     "seq=%u vf=%u prim=%u abe=%u tex=%u ALPHA=%u%u%u%u fix=%02x tfx=%u tcc=%u "
                     "TEST(ate=%u atst=%u aref=%02x afail=%u date=%u zte=%u ztst=%u) zmsk=%u cc=%u pabe=%u fba=%u "
                     "FRAME=%06x fbmsk=%08x TEX=%06x psm=%u cbp=%06x owner=%s fmt=%u verts=%u rgba0=%08x "
                     "drew=%u blend=%s rtcopy=%u copied=%u wrap=%u m16=%u ns=%llu rb=%u cp=%u up=%u upb=%u dc=%u ss=%u us=%u\n",
                     l.seq, l.src_frame, l.prim, l.abe, l.tex, l.a, l.b, l.c, l.d, l.fix, l.tfx, l.tcc, l.ate,
                     l.atst, l.aref, l.afail, l.date, l.zte, l.ztst, l.zmsk, l.colclamp, l.pabe, l.fba, l.fbp,
                     l.fbmsk, l.tbp, l.tpsm, l.cbp, owner, l.fmt_case, l.verts, l.rgba0, l.drew ? 1 : 0, blend,
                     l.needs_rt_copy ? 1 : 0, l.cd_copied ? 1 : 0, l.wrap_route ? 1 : 0,
                     l.fbmsk16_exact ? 1 : 0, (unsigned long long)l.cost.ns, l.cost.readbacks, l.cost.copies,
                     l.cost.uploads, l.cost.upload_bytes, l.cost.draw_calls, l.cost.state_sets, l.cost.uniform_sets);
    }
}

}  // namespace gs
}  // namespace ps2r
