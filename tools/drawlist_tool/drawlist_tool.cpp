// tools/drawlist_tool/drawlist_tool.cpp -- reader for `.gsdl` recordings.
//
//   drawlist_tool stats <rec>                 per-recording GS feature census
//   drawlist_tool frames <rec>                one line per frame
//   drawlist_tool dump <rec> [--frame N] [--verts] [--max N]
//   drawlist_tool diff <a> <b> [--frame N] [--offset K] [--tol-xy F]
//                     [--tol-z F] [--tol-rgba N] [--tol-st F] [--ignore-tex-hash] [--max N]
//
// diff aligns frame i of A with frame i+K of B and compares the ordered draw
// lists: GS state exactly, vertex counts exactly, vertex attributes within
// tolerance. Exit 0 iff no frame mismatched; 2 on usage / load errors.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "tests/lib/gsdl_reader.h"

using ps2r::gs::DrawRec;
using ps2r::gs::GsdlReader;
using ps2r::gs::JobRec;
using ps2r::gs::VertexRec;
using Draw = GsdlReader::Draw;
using Frame = GsdlReader::Frame;
using Job = GsdlReader::Job;
using Recording = GsdlReader::Recording;

namespace {

const char* PrimName(uint8_t p) {
    static const char* n[] = {"point", "line", "linestrip", "tri", "tristrip", "trifan", "sprite", "?"};
    return n[p & 7];
}
const char* JobName(uint32_t t) {
    static const char* n[] = {"DrawBatch", "SetScissor", "SetBlendMode", "SetDepthTest", "SetFrontBuffer",
                              "SetWindow", "SetClearColor", "VSync", "InvalidateTextures",
                              "UploadVRAMTexture", "SyncAllRTsToVRAM"};
    return t < 11 ? n[t] : "?";
}
const char* PsmName(uint32_t p) {
    switch (p) {
        case 0x00: return "CT32"; case 0x01: return "CT24"; case 0x02: return "CT16"; case 0x0A: return "CT16S";
        case 0x13: return "T8"; case 0x14: return "T4"; case 0x1B: return "T8H"; case 0x24: return "T4HL";
        case 0x2C: return "T4HH"; case 0x30: return "Z32"; case 0x31: return "Z24"; case 0x32: return "Z16";
        case 0x3A: return "Z16S"; default: return "?";
    }
}

void PrintTop(const char* title, const std::map<std::string, uint64_t>& m, uint64_t total,
              size_t max = 24) {
    std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::printf("%s (%zu distinct)\n", title, v.size());
    for (size_t i = 0; i < v.size() && i < max; i++)
        std::printf("  %-48s %10llu  %5.1f%%\n", v[i].first.c_str(), (unsigned long long)v[i].second,
                    total ? 100.0 * v[i].second / total : 0.0);
}

int Stats(const Recording& r) {
    uint64_t draws = 0, verts = 0;
    std::map<std::string, uint64_t> prim, target, tex, blend, test, fbmsk, clamp, jobs, hle;
    for (const Frame& f : r.frames) {
        for (const Job& j : f.jobs) jobs[JobName(j.j.type)]++;
        for (const Draw& d : f.draws) {
            const DrawRec& x = d.d;
            draws++; verts += x.vertex_count;
            char b[200];
            if (x.hle_handler) {
                const char* nm = r.Name(x.hle_handler);
                if (nm) std::snprintf(b, sizeof(b), "%s", nm);
                else std::snprintf(b, sizeof(b), "hle_%08x", x.hle_handler);
            } else {
                std::snprintf(b, sizeof(b), "raw/CPU");
            }
            hle[b]++;
            std::snprintf(b, sizeof(b), "%s%s%s%s%s", PrimName(x.prim_type), x.tex_enabled ? "+tex" : "",
                          x.alpha_blend ? "+abe" : "", x.fog_enabled ? "+fog" : "", x.gouraud ? "+iip" : "");
            prim[b]++;
            std::snprintf(b, sizeof(b), "fbp=0x%06x %s fbw=%u z=0x%06x/%s%s", x.frame_fbp, PsmName(x.frame_psm),
                          x.frame_fbw, x.zbuf_zbp, PsmName(x.zbuf_psm), x.zbuf_zmsk ? " zmsk" : "");
            target[b]++;
            if (x.tex_enabled) {
                const bool indexed = x.tpsm == 0x13 || x.tpsm == 0x14 || x.tpsm == 0x1B;
                std::snprintf(b, sizeof(b), "%s %ux%u tfx=%u tcc=%u%s%s%s", PsmName(x.tpsm), 1u << x.tw, 1u << x.th,
                              x.tfx, x.tcc, indexed ? " clut=" : "", indexed ? PsmName(x.cpsm) : "",
                              x.shuffle_type ? " SHUFFLE" : "");
                tex[b]++;
                std::snprintf(b, sizeof(b), "wms=%u wmt=%u mmag=%u mmin=%u", x.wms, x.wmt, x.mmag, x.mmin);
                clamp[b]++;
            }
            if (x.alpha_blend) {
                std::snprintf(b, sizeof(b), "A=%u B=%u C=%u D=%u fix=%u%s%s", x.alpha_a, x.alpha_b, x.alpha_c, x.alpha_d,
                              x.fix_alpha, x.colclamp ? "" : " COLCLAMP=0", x.pabe ? " PABE" : "");
                blend[b]++;
            }
            std::snprintf(b, sizeof(b), "ate=%u atst=%u aref=%u afail=%u date=%u datm=%u zte=%u ztst=%u", x.test_ate,
                          x.test_atst, x.test_aref, x.test_afail, x.test_date, x.test_datm, x.test_zte, x.test_ztst);
            test[b]++;
            std::snprintf(b, sizeof(b), "0x%08x", x.frame_fbmsk);
            fbmsk[b]++;
        }
    }
    std::printf("frames=%zu draws=%llu vertices=%llu (%.1f draws/frame, %.1f verts/frame)%s\n",
                r.frames.size(), (unsigned long long)draws, (unsigned long long)verts,
                r.frames.empty() ? 0.0 : (double)draws / r.frames.size(),
                r.frames.empty() ? 0.0 : (double)verts / r.frames.size(),
                r.truncated ? "  [TRUNCATED]" : "");
    PrintTop("PRIM", prim, draws);
    PrintTop("HLE source class (v2 identity; v1 files read all-raw)", hle, draws);
    PrintTop("FRAME/ZBUF targets", target, draws);
    PrintTop("FBMSK", fbmsk, draws);
    uint64_t textured = 0; for (auto& kv : tex) textured += kv.second;
    PrintTop("TEX0", tex, textured);
    PrintTop("CLAMP/TEX1", clamp, textured);
    uint64_t blended = 0; for (auto& kv : blend) blended += kv.second;
    PrintTop("ALPHA (ABE draws)", blend, blended);
    PrintTop("TEST", test, draws);
    uint64_t nj = 0; for (auto& kv : jobs) nj += kv.second;
    PrintTop("non-draw jobs", jobs, nj);
    return 0;
}

int Frames(const Recording& r) {
    for (size_t i = 0; i < r.frames.size(); i++) {
        const Frame& f = r.frames[i];
        uint64_t v = 0; for (const Draw& d : f.draws) v += d.d.vertex_count;
        std::map<uint32_t, uint32_t> tg; for (const Draw& d : f.draws) tg[d.d.frame_fbp]++;
        std::printf("frame[%zu] vsync=%u draws=%zu verts=%llu jobs=%zu targets=", i, f.vsync_frame, f.draws.size(),
                    (unsigned long long)v, f.jobs.size());
        for (auto& kv : tg) std::printf("0x%x:%u ", kv.first, kv.second);
        std::printf("\n");
    }
    return 0;
}

void PrintDraw(const Draw& d, bool verts, size_t idx) {
    const DrawRec& x = d.d;
    std::printf("  draw[%zu] seq=%u %s%s%s%s n=%u fbp=0x%06x/%s fbw=%u fbmsk=0x%08x z=0x%06x/%s%s scis=(%u,%u)-(%u,%u)\n",
                idx, x.seq, PrimName(x.prim_type), x.tex_enabled ? "+tex" : "", x.alpha_blend ? "+abe" : "",
                x.fog_enabled ? "+fog" : "", x.vertex_count, x.frame_fbp, PsmName(x.frame_psm), x.frame_fbw,
                x.frame_fbmsk, x.zbuf_zbp, PsmName(x.zbuf_psm), x.zbuf_zmsk ? "(zmsk)" : "", x.scissor_x0,
                x.scissor_y0, x.scissor_x1, x.scissor_y1);
    if (x.tex_enabled)
        std::printf("           TEX0 tbp=0x%06x tbw=%u %s %ux%u tfx=%u tcc=%u cbp=0x%06x cpsm=%s csa=%u fst=%u "
                    "wms=%u wmt=%u mmag=%u mmin=%u snap=%016llx clut=%016llx%s\n",
                    x.tbp, x.tbw, PsmName(x.tpsm), 1u << x.tw, 1u << x.th, x.tfx, x.tcc, x.cbp, PsmName(x.cpsm),
                    x.csa, x.tex_fst, x.wms, x.wmt, x.mmag, x.mmin, (unsigned long long)x.vram_snapshot_hash,
                    (unsigned long long)x.clut_snapshot_hash, x.shuffle_type ? " SHUFFLE" : "");
    std::printf("           ALPHA %u%u%u%u fix=%u colclamp=%u pabe=%u fba=%u | TEST ate=%u atst=%u aref=%u afail=%u "
                "date=%u datm=%u zte=%u ztst=%u | bbox (%.1f,%.1f,%.0f)-(%.1f,%.1f,%.0f) vh=%016llx\n",
                x.alpha_a, x.alpha_b, x.alpha_c, x.alpha_d, x.fix_alpha, x.colclamp, x.pabe, x.fba, x.test_ate,
                x.test_atst, x.test_aref, x.test_afail, x.test_date, x.test_datm, x.test_zte, x.test_ztst,
                x.bbox[0], x.bbox[1], x.bbox[2], x.bbox[3], x.bbox[4], x.bbox[5],
                (unsigned long long)x.vertex_hash);
    if (x.hle_handler)
        std::printf("           HLE id=%08x img=%016llx pc=0x%x ord=%u inst=%u\n",
                    x.hle_handler, (unsigned long long)x.hle_image,
                    x.hle_kick_pc, x.hle_kick_ord, x.hle_frame_inst);
    else
        std::printf("           HLE raw\n");
    if (verts)
        for (size_t i = 0; i < d.verts.size(); i++) {
            const VertexRec& v = d.verts[i];
            std::printf("             v%-4zu xyz=(%9.3f,%9.3f,%10.0f) stq=(%g,%g,%g) uv=(%u,%u) rgba=(%u,%u,%u,%u) f=%u%s\n",
                        i, v.x, v.y, v.z, v.s, v.t, v.q, v.uv_u, v.uv_v, v.r, v.g, v.b, v.a, v.fog,
                        v.draw_kick ? "" : " nokick");
        }
}

int Dump(const Recording& r, long frame, bool verts, size_t max) {
    size_t shown = 0;
    for (size_t i = 0; i < r.frames.size(); i++) {
        if (frame >= 0 && (long)i != frame) continue;
        const Frame& f = r.frames[i];
        std::printf("== frame[%zu] vsync=%u draws=%zu jobs=%zu\n", i, f.vsync_frame, f.draws.size(), f.jobs.size());
        size_t di = 0, ji = 0;
        for (uint32_t o : f.order) {
            if (o == 1) { PrintDraw(f.draws[di], verts, di); di++; }
            else {
                const JobRec& j = f.jobs[ji++].j;
                std::printf("  job  seq=%u %s args=(0x%x,0x%x,0x%x,0x%x) payload=%u hash=%016llx\n", j.seq,
                            JobName(j.type), j.arg1, j.arg2, j.arg3, j.arg4, j.payload_bytes,
                            (unsigned long long)j.payload_hash);
            }
            if (++shown >= max) { std::printf("  ... (--max %zu reached)\n", max); return 0; }
        }
    }
    return 0;
}

struct DiffOpts {
    long frame = -1; long offset = 0;
    float tol_xy = 0.0625f, tol_z = 1.0f, tol_st = 1e-5f;
    int tol_rgba = 1;
    bool ignore_tex_hash = false;
    size_t max = 40;
};

bool StateEqual(const DrawRec& a, const DrawRec& b, const DiffOpts& o, std::string& why) {
#define F(fld) if (a.fld != b.fld) { why = #fld; return false; }
    F(prim_type) F(gouraud) F(fog_enabled) F(use_fst) F(alpha_blend) F(scanmsk)
    F(tex_enabled)
    if (a.tex_enabled) {
        F(tex_fst) F(tfx) F(tcc) F(cpsm) F(csm) F(csa) F(tbp) F(tbw) F(tpsm) F(tw) F(th) F(cbp)
        F(mmag) F(mmin) F(wms) F(wmt) F(minu) F(maxu) F(minv) F(maxv) F(texa_ta0) F(texa_ta1) F(texa_aem)
        if (!o.ignore_tex_hash) { F(vram_snapshot_hash) F(clut_snapshot_hash) }
    }
    F(colclamp) F(pabe) F(fba)
    if (a.alpha_blend) { F(alpha_a) F(alpha_b) F(alpha_c) F(alpha_d) F(fix_alpha) }
    F(fogcol_r) F(fogcol_g) F(fogcol_b)
    F(frame_fbp) F(frame_fbw) F(frame_fbmsk) F(frame_psm) F(zbuf_psm) F(zbuf_zmsk) F(zbuf_zbp)
    F(scissor_x0) F(scissor_y0) F(scissor_x1) F(scissor_y1)
    F(test_ate) F(test_atst) F(test_aref) F(test_afail) F(test_date) F(test_datm) F(test_zte) F(test_ztst)
    F(vertex_count)
#undef F
    return true;
}

bool VertsClose(const Draw& a, const Draw& b, const DiffOpts& o, std::string& why) {
    if (a.verts.empty() || b.verts.empty()) {
        if (a.d.vertex_hash != b.d.vertex_hash) { why = "vertex_hash (no vertex data recorded)"; return false; }
        return true;
    }
    for (size_t i = 0; i < a.verts.size() && i < b.verts.size(); i++) {
        const VertexRec& p = a.verts[i]; const VertexRec& q = b.verts[i];
        char m[200];
        if (std::fabs(p.x - q.x) > o.tol_xy || std::fabs(p.y - q.y) > o.tol_xy || std::fabs(p.z - q.z) > o.tol_z) {
            std::snprintf(m, sizeof(m), "v%zu xyz (%.3f,%.3f,%.0f) vs (%.3f,%.3f,%.0f)", i, p.x, p.y, p.z, q.x, q.y, q.z);
            why = m; return false;
        }
        if (std::abs((int)p.r - q.r) > o.tol_rgba || std::abs((int)p.g - q.g) > o.tol_rgba ||
            std::abs((int)p.b - q.b) > o.tol_rgba || std::abs((int)p.a - q.a) > o.tol_rgba) {
            std::snprintf(m, sizeof(m), "v%zu rgba (%u,%u,%u,%u) vs (%u,%u,%u,%u)", i, p.r, p.g, p.b, p.a, q.r, q.g, q.b, q.a);
            why = m; return false;
        }
        auto fc = [&](float x, float y) { return std::fabs(x - y) <= o.tol_st * std::fmax(1.f, std::fmax(std::fabs(x), std::fabs(y))); };
        if (a.d.use_fst) {
            if (p.uv_u != q.uv_u || p.uv_v != q.uv_v) { std::snprintf(m, sizeof(m), "v%zu uv", i); why = m; return false; }
        } else if (!fc(p.s, q.s) || !fc(p.t, q.t) || !fc(p.q, q.q)) {
            std::snprintf(m, sizeof(m), "v%zu stq (%g,%g,%g) vs (%g,%g,%g)", i, p.s, p.t, p.q, q.s, q.t, q.q);
            why = m; return false;
        }
        if (p.draw_kick != q.draw_kick) { std::snprintf(m, sizeof(m), "v%zu draw_kick", i); why = m; return false; }
    }
    return true;
}

int Diff(const Recording& a, const Recording& b, const DiffOpts& o) {
    size_t bad_frames = 0, shown = 0, compared = 0;
    for (size_t i = 0; i < a.frames.size(); i++) {
        if (o.frame >= 0 && (long)i != o.frame) continue;
        const long jb = (long)i + o.offset;
        if (jb < 0 || (size_t)jb >= b.frames.size()) break;
        const Frame& fa = a.frames[i]; const Frame& fb = b.frames[(size_t)jb];
        compared++;
        size_t frame_bad = 0;
        const size_t n = std::min(fa.draws.size(), fb.draws.size());
        for (size_t k = 0; k < n; k++) {
            std::string why;
            bool ok = StateEqual(fa.draws[k].d, fb.draws[k].d, o, why) && VertsClose(fa.draws[k], fb.draws[k], o, why);
            if (!ok) {
                frame_bad++;
                if (shown < o.max) {
                    shown++;
                    std::printf("frame A[%zu]/B[%ld] draw[%zu]: %s\n", i, jb, k, why.c_str());
                    PrintDraw(fa.draws[k], false, k);
                    PrintDraw(fb.draws[k], false, k);
                }
            }
        }
        if (fa.draws.size() != fb.draws.size()) {
            frame_bad++;
            if (shown < o.max) { shown++; std::printf("frame A[%zu]/B[%ld]: draw count %zu vs %zu\n", i, jb, fa.draws.size(), fb.draws.size()); }
        }
        if (frame_bad) bad_frames++;
    }
    std::printf("DIFF %s: frames compared=%zu mismatching=%zu (offset=%ld tol xy=%g z=%g rgba=%d st=%g)\n",
                bad_frames ? "FAIL" : "PASS", compared, bad_frames, o.offset, o.tol_xy, o.tol_z, o.tol_rgba, o.tol_st);
    return bad_frames ? 1 : 0;
}

int Usage() {
    std::fprintf(stderr,
        "usage: drawlist_tool stats <rec>\n"
        "       drawlist_tool frames <rec>\n"
        "       drawlist_tool dump <rec> [--frame N] [--verts] [--max N]\n"
        "       drawlist_tool diff <a> <b> [--frame N] [--offset K] [--tol-xy F] [--tol-z F]\n"
        "                          [--tol-rgba N] [--tol-st F] [--ignore-tex-hash] [--max N]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return Usage();
    const std::string cmd = argv[1];
    Recording a;
    if (!GsdlReader::Load(argv[2], a)) return 2;
    if (cmd == "stats") return Stats(a);
    if (cmd == "frames") return Frames(a);
    if (cmd == "dump") {
        long frame = -1; bool verts = false; size_t max = 100000;
        for (int i = 3; i < argc; i++) {
            if (!std::strcmp(argv[i], "--frame") && i + 1 < argc) frame = std::strtol(argv[++i], nullptr, 0);
            else if (!std::strcmp(argv[i], "--verts")) verts = true;
            else if (!std::strcmp(argv[i], "--max") && i + 1 < argc) max = std::strtoul(argv[++i], nullptr, 0);
        }
        return Dump(a, frame, verts, max);
    }
    if (cmd == "diff") {
        if (argc < 4) return Usage();
        Recording b;
        if (!GsdlReader::Load(argv[3], b)) return 2;
        DiffOpts o;
        for (int i = 4; i < argc; i++) {
            if (!std::strcmp(argv[i], "--frame") && i + 1 < argc) o.frame = std::strtol(argv[++i], nullptr, 0);
            else if (!std::strcmp(argv[i], "--offset") && i + 1 < argc) o.offset = std::strtol(argv[++i], nullptr, 0);
            else if (!std::strcmp(argv[i], "--tol-xy") && i + 1 < argc) o.tol_xy = std::strtof(argv[++i], nullptr);
            else if (!std::strcmp(argv[i], "--tol-z") && i + 1 < argc) o.tol_z = std::strtof(argv[++i], nullptr);
            else if (!std::strcmp(argv[i], "--tol-st") && i + 1 < argc) o.tol_st = std::strtof(argv[++i], nullptr);
            else if (!std::strcmp(argv[i], "--tol-rgba") && i + 1 < argc) o.tol_rgba = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--ignore-tex-hash")) o.ignore_tex_hash = true;
            else if (!std::strcmp(argv[i], "--max") && i + 1 < argc) o.max = std::strtoul(argv[++i], nullptr, 0);
        }
        return Diff(a, b, o);
    }
    return Usage();
}
