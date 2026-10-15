// tools/gs_replay/census.cpp -- the census counters and their line grammar.
#include "tools/gs_replay/census.h"

#include <cinttypes>
#include <cstdio>

#include "engine/gs/hle/ir/job_rec.h"

namespace ps2r {
namespace tools {

using gs::ClassClassifier;
using gs::DrawRec;
using gs::JobKind;
using gs::JobRec;

namespace {
bool PsmIndexed(uint32_t p) { return p == 19 || p == 20 || p == 27 || p == 36 || p == 44; }
}  // namespace

Census::Census(const ClassClassifier::Rules& rules, const gs::GsdlReader::Recording& rec, bool want_cfg)
    : rules_(rules), rec_(rec), want_cfg_(want_cfg) {}

std::string Census::PsmName(uint32_t p) {
    switch (p) {
        case 0: return "CT32"; case 1: return "CT24"; case 2: return "CT16"; case 10: return "CT16S";
        case 19: return "T8"; case 20: return "T4"; case 27: return "T8H"; case 36: return "T4HL";
        case 44: return "T4HH"; case 48: return "Z32"; case 49: return "Z24"; case 50: return "Z16";
        case 58: return "Z16S";
        default: {
            char b[16];
            std::snprintf(b, sizeof(b), "psm%u", p);
            return b;
        }
    }
}

ClassClassifier::Class Census::ClassifyRec(const ClassClassifier::Rules& rules, const gs::GsdlReader::Recording& r,
                                           const DrawRec& x) {
    const float w = x.bbox[3] - x.bbox[0], h = x.bbox[4] - x.bbox[1];
    return ClassClassifier::ClassifyFields(rules, x.hle_handler, x.hle_handler ? r.Name(x.hle_handler) : nullptr,
                                           x.test_date != 0, x.frame_psm, x.tex_enabled != 0, x.tpsm, x.prim_type,
                                           w, h);
}

const char* Census::ClassOf(const ClassClassifier::Rules& rules, const gs::GsdlReader::Recording& r,
                            const DrawRec& x) {
    return ClassClassifier::ClassName(ClassifyRec(rules, r, x));
}

// Conditioned fields print '-' when their enable bit is off so the key set stays finite.
std::string Census::CfgKey(const DrawRec& x) const {
    char b[352], texs[48], cluts[24], bls[24], ates[24], dates[16];
    if (x.tex_enabled) {
        std::snprintf(texs, sizeof(texs), "%s,tfx%u,tcc%u", PsmName(x.tpsm).c_str(), x.tfx, x.tcc);
        if (PsmIndexed(x.tpsm)) std::snprintf(cluts, sizeof(cluts), "%s,csm%u", PsmName(x.cpsm).c_str(), x.csm);
        else std::snprintf(cluts, sizeof(cluts), "-");
    } else {
        std::snprintf(texs, sizeof(texs), "-");
        std::snprintf(cluts, sizeof(cluts), "-");
    }
    if (x.alpha_blend)
        std::snprintf(bls, sizeof(bls), "%u%u%u%u,fix%s", x.alpha_a, x.alpha_b, x.alpha_c, x.alpha_d,
                      (x.alpha_c == 2) ? (x.fix_alpha ? "+" : "0") : "-");
    else std::snprintf(bls, sizeof(bls), "-");
    if (x.test_ate) std::snprintf(ates, sizeof(ates), "atst%u,afail%u", x.test_atst, x.test_afail);
    else std::snprintf(ates, sizeof(ates), "-");
    if (x.test_date) std::snprintf(dates, sizeof(dates), "datm%u", x.test_datm);
    else std::snprintf(dates, sizeof(dates), "-");
    const char* nm = x.hle_handler ? (rec_.Name(x.hle_handler) ? rec_.Name(x.hle_handler) : "hle_?") : "raw";
    std::snprintf(b, sizeof(b),
                  "class=%s prim=%u iip=%u fst=%u fog=%u abe=%u tex=%s clut=%s bl=%s cc=%u pabe=%u fba=%u ate=%s "
                  "date=%s zte=%u ztst=%u zmsk=%u fpsm=%s zpsm=%s fbmsk=%08x smsk=%u shuf=%u hle=%s",
                  ClassOf(rules_, rec_, x), x.prim_type & 7, x.gouraud, x.use_fst, x.fog_enabled, x.alpha_blend,
                  texs, cluts, bls, x.colclamp, x.pabe, x.fba, ates, dates, x.test_zte, x.test_ztst, x.zbuf_zmsk,
                  PsmName(x.frame_psm).c_str(), PsmName(x.zbuf_psm).c_str(), x.frame_fbmsk, x.scanmsk,
                  x.shuffle_type, nm);
    return b;
}

void Census::AddDraw(const DrawRec& x) {
    draws_++;
    verts_ += x.vertex_count;
    prim_[x.prim_type & 7]++;
    if (x.tex_enabled) textured_++;
    if (x.alpha_blend) blended_++;
    if (x.test_date) date_++;
    if (x.fog_enabled) fogged_++;
    if (x.gouraud) gouraud_++;
    char b[128];
    if (x.hle_handler) {
        const char* nm = rec_.Name(x.hle_handler);
        if (nm) std::snprintf(b, sizeof(b), "%s", nm);
        else std::snprintf(b, sizeof(b), "hle_%08x", x.hle_handler);
    } else {
        std::snprintf(b, sizeof(b), "raw/CPU");
    }
    hle_[b]++;
    const char* cn = ClassOf(rules_, rec_, x);
    cls_draws_[cn]++;
    cls_verts_[cn] += x.vertex_count;
    if (want_cfg_) cfg_[CfgKey(x)]++;
}

void Census::AddJob(const JobRec& j) {
    jobs_++;
    job_by_type_[j.type & 15]++;
    if (j.type == static_cast<uint32_t>(JobKind::UploadVRAMTexture)) {
        if (j.payload_incl) uploads_with_payload_++;
        else uploads_no_payload_++;
        if (want_cfg_) {
            char k[96];
            const uint64_t px = static_cast<uint64_t>(j.arg2) * j.arg3;
            const uint32_t bpp = px ? static_cast<uint32_t>((static_cast<uint64_t>(j.arg4) * 8) / px) : 0;
            std::snprintf(k, sizeof(k), "w=%u h=%u bpp=%u", j.arg2, j.arg3, bpp);
            cfg_up_[k]++;
        }
    }
    if (want_cfg_ && j.type == static_cast<uint32_t>(JobKind::SetFrontBuffer)) {
        char k[96];
        std::snprintf(k, sizeof(k), "dispfb fbp=0x%x w=%u psm=%s", j.arg1, j.arg2, PsmName(j.arg3).c_str());
        cfg_scan_[k]++;
    }
    if (want_cfg_ && j.type == static_cast<uint32_t>(JobKind::SetWindow)) {
        char k[96];
        std::snprintf(k, sizeof(k), "display ckt=%u w=%u h=%u", j.arg1, j.arg2, j.arg3);
        cfg_scan_[k]++;
    }
}

void Census::Print(const char* tag) const {
    std::printf("%s CENSUS frames=%" PRIu64 " draws=%" PRIu64 " verts=%" PRIu64 " jobs=%" PRIu64
                " draws/frame=%.4f verts/frame=%.2f\n",
                tag, frames_, draws_, verts_, jobs_, frames_ ? (double)draws_ / (double)frames_ : 0.0,
                frames_ ? (double)verts_ / (double)frames_ : 0.0);
    std::printf("%s PRIM point=%" PRIu64 " line=%" PRIu64 " linestrip=%" PRIu64 " tri=%" PRIu64 " tristrip=%" PRIu64
                " trifan=%" PRIu64 " sprite=%" PRIu64 "\n",
                tag, prim_[0], prim_[1], prim_[2], prim_[3], prim_[4], prim_[5], prim_[6]);
    std::printf("%s FLAGS tex=%" PRIu64 " abe=%" PRIu64 " date=%" PRIu64 " fog=%" PRIu64 " iip=%" PRIu64 "\n", tag,
                textured_, blended_, date_, fogged_, gouraud_);
    std::printf("%s JOBS", tag);
    static const char* jn[] = {"draw", "scis", "blend", "depth", "dispfb", "window", "clear", "vsync", "inval",
                               "upload", "syncrt"};
    for (int i = 0; i < 11; i++)
        if (job_by_type_[i]) std::printf(" %s=%" PRIu64, jn[i], job_by_type_[i]);
    std::printf(" upload_payload=%" PRIu64 " upload_nopayload=%" PRIu64 "\n", uploads_with_payload_,
                uploads_no_payload_);
    std::printf("%s HLE", tag);
    for (const auto& kv : hle_) std::printf(" %s=%" PRIu64, kv.first.c_str(), kv.second);
    std::printf("\n");
    for (const auto& kv : cls_draws_) {
        const auto vit = cls_verts_.find(kv.first);
        std::printf("%s CLASS %s draws=%" PRIu64 " verts=%" PRIu64 "\n", tag, kv.first.c_str(), kv.second,
                    vit != cls_verts_.end() ? vit->second : 0);
    }
    if (want_cfg_) {
        for (const auto& kv : cfg_) std::printf("%s CFG draw n=%" PRIu64 " %s\n", tag, kv.second, kv.first.c_str());
        for (const auto& kv : cfg_up_) std::printf("%s CFG upload n=%" PRIu64 " %s\n", tag, kv.second, kv.first.c_str());
        for (const auto& kv : cfg_scan_)
            std::printf("%s CFG scanout n=%" PRIu64 " %s\n", tag, kv.second, kv.first.c_str());
        std::printf("%s CFG summary draw_cfgs=%zu upload_cfgs=%zu scanout_cfgs=%zu\n", tag, cfg_.size(),
                    cfg_up_.size(), cfg_scan_.size());
    }
}

}  // namespace tools
}  // namespace ps2r
