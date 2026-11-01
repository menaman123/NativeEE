// tools/gs_replay/stream_census.cpp -- StreamCensus (see the header).
#include "tools/gs_replay/stream_census.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "engine/gs/hle/capture/gs_stream_reader.h"

namespace ps2r {
namespace tools {

namespace {

// Triangles a kick of `n` vertices produces per PRIM type (points/lines count 0; sprites = 2 per pair).
uint32_t TrisOf(uint8_t prim, uint32_t n) {
    switch (prim) {
        case 3: return n / 3;                 // triangle list
        case 4: case 5: return n >= 3 ? n - 2 : 0;   // strip / fan
        case 6: return (n / 2) * 2;           // sprite = 2 triangles
        default: return 0;
    }
}

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, static_cast<size_t>(p * double(v.size() - 1) + 0.5));
    return v[i];
}

}  // namespace

int StreamCensus::Run() {
    gs::GsStreamReader reader;
    if (!reader.Open(opt_.in_path)) {
        std::fprintf(stderr, "[CENSUS] %s\n", reader.Error().c_str());
        return 2;
    }
    gs::GsStreamReader::Record rec;
    StreamCensusRow cur;
    std::set<uint32_t> targets_written;    // FRAME.FBP written this frame (bytes)
    std::set<uint32_t> targets_seen;
    bool have_prev = false;
    uint32_t prev_fbp = 0, prev_zbp = 0;
    bool have_prev_tex = false;
    uint32_t prev_tbp = 0, prev_tpsm = 0, prev_cbp = 0;
    const double fh = double(opt_.frame_height);
    while (reader.Next(&rec)) {
        ++total_records_;
        switch (rec.kind) {
            case gs::GsStreamFormat::kDraw: {
                const gs::DrawIr& d = rec.draw;
                ++cur.draws;
                const uint32_t n = d.vertex_count ? d.vertex_count : static_cast<uint32_t>(d.verts.size());
                cur.verts += n;
                cur.tris += TrisOf(d.prim_type, n);
                if (d.prim_type == 6) ++cur.sprites;
                if (d.alpha_blend) ++cur.blended;
                if (d.tex_enabled) ++cur.textured;
                if (d.raster.test.zte && d.raster.test.ztst >= 2) ++cur.ztested;
                if (!d.raster.zbuf.zmsk) ++cur.zwrite;
                if (d.raster.test.ate) ++cur.atested;
                if (d.gouraud) ++cur.gouraud;
                if (d.fog) ++cur.fogged;
                // extent from the recorded vertices (GS pixel space after the offset)
                double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
                for (const gs::VertexRec& v : d.verts) {
                    x0 = std::min(x0, double(v.x)); x1 = std::max(x1, double(v.x));
                    y0 = std::min(y0, double(v.y)); y1 = std::max(y1, double(v.y));
                }
                const double fw = d.raster.frame.fbw ? double(d.raster.frame.fbw) : 512.0;
                if (!d.verts.empty()) {
                    // clip to the scissor (the GS draws nothing outside it)
                    const double sx0 = d.raster.scissor.x0, sy0 = d.raster.scissor.y0;
                    const double sx1 = d.raster.scissor.x1 + 1, sy1 = d.raster.scissor.y1 + 1;
                    x0 = std::max(x0, sx0); y0 = std::max(y0, sy0); x1 = std::min(x1, sx1); y1 = std::min(y1, sy1);
                    const double area = std::max(0.0, x1 - x0) * std::max(0.0, y1 - y0);
                    const double target_area = std::max(1.0, std::min(fw, sx1 - sx0) * std::min(fh, sy1 - sy0));
                    const double frac = area / target_area;
                    cur.bbox_area_frames += area / (fw * fh);
                    if (frac >= opt_.full_frac) {
                        ++cur.fullscreen;
                        cur.fullscreen_area_frames += area / (fw * fh);
                        if (d.alpha_blend) ++cur.fullscreen_blended;
                    }
                }
                const uint32_t fbp = d.raster.frame.fbp, zbp = d.raster.zbuf.zbp;
                if (have_prev && (fbp != prev_fbp || zbp != prev_zbp)) ++cur.target_switches;
                have_prev = true; prev_fbp = fbp; prev_zbp = zbp;
                targets_seen.insert(fbp);
                if (d.tex_enabled) {
                    switch (d.tex.psm) {
                        case 0x00: case 0x01: ++cur.psm_ct32; break;   // PSMCT32 / PSMCT24
                        case 0x02: case 0x0A: ++cur.psm_ct16; break;   // PSMCT16 / 16S
                        case 0x13: case 0x1B: ++cur.psm_t8; break;     // PSMT8 / 8H
                        case 0x14: case 0x24: case 0x2C: ++cur.psm_t4; break;
                        default: ++cur.psm_other; break;
                    }
                    if (targets_written.count(d.tex.tbp)) ++cur.rtt_reads;
                    if (have_prev_tex && (d.tex.tbp != prev_tbp || d.tex.psm != prev_tpsm || d.tex.cbp != prev_cbp)) ++cur.tex_switches;
                    have_prev_tex = true; prev_tbp = d.tex.tbp; prev_tpsm = d.tex.psm; prev_cbp = d.tex.cbp;
                }
                if (d.raster.frame.fbmsk != 0xFFFFFFFFu) targets_written.insert(fbp);
                break;
            }
            case gs::GsStreamFormat::kJob:
                ++cur.jobs;
                if (rec.job.type == static_cast<uint32_t>(gs::JobKind::UploadVRAMTexture)) {
                    ++cur.uploads;
                    cur.upload_bytes += rec.job.payload_bytes;
                }
                break;
            case gs::GsStreamFormat::kFrame:
                cur.frame = static_cast<uint32_t>(rows_.size());
                cur.targets = static_cast<uint32_t>(targets_seen.size());
                rows_.push_back(cur);
                cur = StreamCensusRow();
                targets_written.clear();
                targets_seen.clear();
                have_prev = false;
                have_prev_tex = false;
                if (opt_.max_frames && rows_.size() >= opt_.max_frames) goto done;
                break;
            default:
                break;
        }
    }
done:
    if (!reader.Error().empty()) std::fprintf(stderr, "[CENSUS] note: %s (tail kept)\n", reader.Error().c_str());
    if (opt_.csv_path) WriteCsv();
    if (!opt_.quiet) PrintSummary();
    return 0;
}

void StreamCensus::WriteCsv() const {
    std::FILE* f = std::fopen(opt_.csv_path, "w");
    if (!f) { std::fprintf(stderr, "[CENSUS] cannot write %s\n", opt_.csv_path); return; }
    std::fprintf(f, "frame,draws,verts,tris,sprites,blended,textured,ztested,zwrite,atested,gouraud,fogged,fullscreen,fullscreen_blended,"
                    "rtt_reads,targets,target_switches,tex_switches,uploads,upload_bytes,jobs,bbox_area_frames,fullscreen_area_frames,"
                    "psm_ct32,psm_ct16,psm_t8,psm_t4,psm_other\n");
    for (const StreamCensusRow& r : rows_) {
        std::fprintf(f, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%u,%.3f,%.3f,%u,%u,%u,%u,%u\n",
                     r.frame, r.draws, r.verts, r.tris, r.sprites, r.blended, r.textured, r.ztested, r.zwrite, r.atested, r.gouraud,
                     r.fogged, r.fullscreen, r.fullscreen_blended, r.rtt_reads, r.targets, r.target_switches, r.tex_switches, r.uploads,
                     static_cast<unsigned long long>(r.upload_bytes), r.jobs, r.bbox_area_frames, r.fullscreen_area_frames,
                     r.psm_ct32, r.psm_ct16, r.psm_t8, r.psm_t4, r.psm_other);
    }
    std::fclose(f);
}

void StreamCensus::PrintSummary() const {
    std::printf("[CENSUS] %s frames=%zu records=%llu\n", opt_.in_path, rows_.size(), static_cast<unsigned long long>(total_records_));
    if (rows_.empty()) return;
    struct Col { const char* name; std::function<double(const StreamCensusRow&)> get; };
    const Col cols[] = {
        {"draws", [](const StreamCensusRow& r) { return double(r.draws); }},
        {"verts", [](const StreamCensusRow& r) { return double(r.verts); }},
        {"tris", [](const StreamCensusRow& r) { return double(r.tris); }},
        {"blended", [](const StreamCensusRow& r) { return double(r.blended); }},
        {"textured", [](const StreamCensusRow& r) { return double(r.textured); }},
        {"ztested", [](const StreamCensusRow& r) { return double(r.ztested); }},
        {"fullscreen", [](const StreamCensusRow& r) { return double(r.fullscreen); }},
        {"fullscreen_blended", [](const StreamCensusRow& r) { return double(r.fullscreen_blended); }},
        {"rtt_reads", [](const StreamCensusRow& r) { return double(r.rtt_reads); }},
        {"targets", [](const StreamCensusRow& r) { return double(r.targets); }},
        {"target_switches", [](const StreamCensusRow& r) { return double(r.target_switches); }},
        {"tex_switches", [](const StreamCensusRow& r) { return double(r.tex_switches); }},
        {"uploads", [](const StreamCensusRow& r) { return double(r.uploads); }},
        {"upload_kb", [](const StreamCensusRow& r) { return double(r.upload_bytes) / 1024.0; }},
        {"bbox_area_frames", [](const StreamCensusRow& r) { return r.bbox_area_frames; }},
        {"fullscreen_area_frames", [](const StreamCensusRow& r) { return r.fullscreen_area_frames; }},
        {"psm_t8", [](const StreamCensusRow& r) { return double(r.psm_t8); }},
        {"psm_t4", [](const StreamCensusRow& r) { return double(r.psm_t4); }},
        {"psm_ct32", [](const StreamCensusRow& r) { return double(r.psm_ct32); }},
        {"psm_ct16", [](const StreamCensusRow& r) { return double(r.psm_ct16); }},
    };
    std::printf("%-24s %12s %12s %12s %12s\n", "column", "median", "p99", "max", "max@frame");
    for (const Col& c : cols) {
        std::vector<double> v;
        v.reserve(rows_.size());
        double mx = -1; uint32_t at = 0;
        for (const StreamCensusRow& r : rows_) { const double x = c.get(r); v.push_back(x); if (x > mx) { mx = x; at = r.frame; } }
        std::printf("%-24s %12.1f %12.1f %12.1f %12u\n", c.name, Percentile(v, 0.5), Percentile(v, 0.99), mx, at);
    }
    auto top = [&](const char* name, std::function<double(const StreamCensusRow&)> get) {
        std::vector<const StreamCensusRow*> p;
        for (const StreamCensusRow& r : rows_) p.push_back(&r);
        std::stable_sort(p.begin(), p.end(), [&](const StreamCensusRow* a, const StreamCensusRow* b) { return get(*a) > get(*b); });
        std::printf("top %s:", name);
        for (uint32_t i = 0; i < opt_.top && i < p.size(); ++i) std::printf(" f%u=%.1f", p[i]->frame, get(*p[i]));
        std::printf("\n");
    };
    top("draws", cols[0].get);
    top("tris", cols[2].get);
    top("fullscreen", cols[6].get);
    top("upload_kb", cols[13].get);
    top("bbox_area_frames", cols[14].get);
    // the composite: draws / median + fullscreen / median + area / median -- the frame the stress lane loops
    std::vector<double> md, mf, ma;
    for (const StreamCensusRow& r : rows_) { md.push_back(r.draws); mf.push_back(r.fullscreen); ma.push_back(r.bbox_area_frames); }
    const double d50 = std::max(1.0, Percentile(md, 0.5)), f50 = std::max(1.0, Percentile(mf, 0.5)), a50 = std::max(1e-6, Percentile(ma, 0.5));
    top("composite(draws/med+fullscreen/med+area/med)", [=](const StreamCensusRow& r) { return r.draws / d50 + r.fullscreen / f50 + r.bbox_area_frames / a50; });
}

}  // namespace tools
}  // namespace ps2r
