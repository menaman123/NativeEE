// gs/hle/ir/drawlist_ring.cpp -- DrawlistRing (see the header).
#include "engine/gs/hle/ir/drawlist_ring.h"

#include <algorithm>
#include <cstdio>

namespace ps2r {
namespace gs {

namespace {
std::string Hex(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}
}  // namespace

DrawlistRing::DrawlistRing(DrawIrSink* next, NameResolver names)
    : next_(next), names_(std::move(names)), draws_(kDraws), frames_(kFrames) {}

void DrawlistRing::OnDraw(const DrawIr& draw) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        DrawRecord& r = draws_[static_cast<size_t>(draw_total_ % kDraws)];
        r.seq = draw.seq;
        r.src_frame = draw.src_frame;
        r.frame_inst = draw.identity.frame_inst;
        r.handler = draw.identity.hle_handler;
        r.kick_pc = draw.identity.kick_pc;
        r.kick_ord = draw.identity.kick_ord;
        r.prim_type = draw.prim_type;
        r.ctx = draw.ctx;
        r.frame_psm = draw.raster.frame.psm;
        r.tex_psm = static_cast<uint8_t>(draw.tex.psm);
        r.tex_enabled = draw.tex_enabled;
        r.alpha_blend = draw.alpha_blend;
        r.vertex_count = draw.vertex_count;
        r.fbp = draw.raster.frame.fbp;
        r.fbw = draw.raster.frame.fbw;
        r.zbp = draw.raster.zbuf.zbp;
        r.tbp0 = draw.tex.tbp;
        for (int i = 0; i < 6; ++i) r.bbox[i] = draw.bbox[i];
        if (open_.draws == 0) {
            open_.src_frame = draw.src_frame;
            open_first_draw_ = static_cast<uint32_t>(draw_total_);
        }
        ++open_.draws;
        if (open_.draws > kDraws) ++open_.dropped;
        ++draw_total_;
    }
    if (next_) next_->OnDraw(draw);
}

void DrawlistRing::OnJob(const JobRec& job) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        ++open_.jobs;
        if (open_.draws == 0 && open_.jobs == 1) open_.src_frame = job.src_frame;
    }
    if (next_) next_->OnJob(job);
}

void DrawlistRing::OnFrame(const JobRec& vsync) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        open_.vsync_seq = vsync.seq;
        if (open_.draws == 0 && open_.jobs == 0) open_.src_frame = vsync.src_frame;
        frames_[static_cast<size_t>(frame_total_ % kFrames)] = open_;
        ++frame_total_;
        open_ = FrameSummary{};
    }
    if (next_) next_->OnFrame(vsync);
}

uint64_t DrawlistRing::Draws() const {
    std::lock_guard<std::mutex> lock(mu_);
    return draw_total_;
}

uint64_t DrawlistRing::Frames() const {
    std::lock_guard<std::mutex> lock(mu_);
    return frame_total_;
}

bool DrawlistRing::Last(DrawRecord* out) const {
    std::lock_guard<std::mutex> lock(mu_);
    if (draw_total_ == 0) return false;
    *out = draws_[static_cast<size_t>((draw_total_ - 1) % kDraws)];
    return true;
}

std::string DrawlistRing::Describe(const DrawRecord& d) const {
    char buf[320];
    const char* name = (d.handler && names_) ? names_(d.handler) : nullptr;
    std::snprintf(buf, sizeof buf,
                  "draw seq=%u frame=%u inst=%u handler=%s%s%s%s kick_pc=0x%x ord=%u prim=%u ctx=%u verts=%u "
                  "fb=0x%x/%u psm=%u z=0x%x tex=%s0x%x/%u abe=%u bbox=[%g,%g,%g,%g,%g,%g]",
                  d.seq, d.src_frame, d.frame_inst, Hex(d.handler).c_str(), name ? "(" : "", name ? name : "",
                  name ? ")" : "", d.kick_pc, d.kick_ord, d.prim_type, d.ctx, d.vertex_count, d.fbp, d.fbw,
                  d.frame_psm, d.zbp, d.tex_enabled ? "" : "off:", d.tbp0, d.tex_psm, d.alpha_blend ? 1u : 0u,
                  d.bbox[0], d.bbox[1], d.bbox[2], d.bbox[3], d.bbox[4], d.bbox[5]);
    return buf;
}

std::string DrawlistRing::Dump() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::string out;
    const uint64_t closed = std::min<uint64_t>(frame_total_, kFrames);
    const uint64_t retained_draws = std::min<uint64_t>(draw_total_, kDraws);
    out += "drawlist ring: frames_closed=" + std::to_string(frame_total_) + " frames_retained=" + std::to_string(closed) +
           (open_.draws || open_.jobs ? "+open" : "") + " draws_total=" + std::to_string(draw_total_) +
           " draws_retained=" + std::to_string(retained_draws) + " capacity=" + std::to_string(kFrames) + "/" +
           std::to_string(kDraws) + "\n";
    // The frame summaries, oldest retained first (the open frame = the one the
    // fault interrupted), then the retained draws in push order with a header
    // line wherever the stamped frame changes (one pass; the draws of a frame
    // are the newest kDraws draws stamped with its src_frame).
    for (uint64_t k = frame_total_ - closed; k < frame_total_; ++k) {
        const FrameSummary& f = frames_[static_cast<size_t>(k % kFrames)];
        out += "frame src_frame=" + std::to_string(f.src_frame) + " draws=" + std::to_string(f.draws) +
               " jobs=" + std::to_string(f.jobs) + " dropped=" + std::to_string(f.dropped) +
               " vsync_seq=" + std::to_string(f.vsync_seq) + "\n";
    }
    if (open_.draws || open_.jobs)
        out += "frame src_frame=" + std::to_string(open_.src_frame) + " draws=" + std::to_string(open_.draws) +
               " jobs=" + std::to_string(open_.jobs) + " dropped=" + std::to_string(open_.dropped) +
               " (open: the frame the fault interrupted)\n";
    // Draws older than the oldest retained frame are not part of the last
    // kFrames frames even when their ring slots survive.
    const uint32_t oldest_frame = closed ? frames_[static_cast<size_t>((frame_total_ - closed) % kFrames)].src_frame
                                         : open_.src_frame;
    const uint64_t first_draw = draw_total_ - retained_draws;
    bool have_frame = false;
    uint32_t current = 0;
    for (uint64_t i = first_draw; i < draw_total_; ++i) {
        const DrawRecord& d = draws_[static_cast<size_t>(i % kDraws)];
        if (d.src_frame < oldest_frame) continue;
        if (!have_frame || d.src_frame != current) {
            have_frame = true;
            current = d.src_frame;
            out += "draws of frame src_frame=" + std::to_string(current) + "\n";
        }
        out += "  " + Describe(d) + "\n";
    }
    return out;
}

}  // namespace gs
}  // namespace ps2r
