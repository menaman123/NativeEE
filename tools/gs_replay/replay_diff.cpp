// tools/gs_replay/replay_diff.cpp -- the diff / classdiff verdict math and lines.
#include "tools/gs_replay/replay_diff.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "tests/lib/gsdl_reader.h"
#include "tools/gs_replay/census.h"
#include "tools/gs_replay/gsrep_file.h"

namespace ps2r {
namespace tools {

int ReplayDiff::RunDiff(const char* pa, const char* pb, int tol, double pct_req, size_t max_lines) {
    GsrepFile::Loaded a, b;
    if (!GsrepFile::Load(pa, a) || !GsrepFile::Load(pb, b)) return 2;
    if (a.hdr.ds_n != b.hdr.ds_n) {
        std::fprintf(stderr, "[GSDIFF] ds grid mismatch: %u vs %u\n", a.hdr.ds_n, b.hdr.ds_n);
        return 2;
    }
    const size_t n = std::min(a.frames.size(), b.frames.size());
    size_t ident = 0, ok = 0, diff = 0, shown = 0;
    for (size_t i = 0; i < n; i++) {
        const GsrepFrame& fa = a.frames[i];
        const GsrepFrame& fb = b.frames[i];
        const std::vector<uint8_t>& da = a.ds[i];
        const std::vector<uint8_t>& db = b.ds[i];
        const size_t px = da.size() / 4;
        uint64_t sum[4] = {0, 0, 0, 0};
        int maxd = 0;
        size_t within = 0;
        for (size_t p = 0; p < px; p++) {
            int pmax = 0;
            for (int c = 0; c < 4; c++) {
                const int d = std::abs(static_cast<int>(da[p * 4 + c]) - static_cast<int>(db[p * 4 + c]));
                sum[c] += static_cast<uint64_t>(d);
                if (d > pmax) pmax = d;
            }
            if (pmax > maxd) maxd = pmax;
            if (pmax <= tol) within++;
        }
        const double wpct = px ? 100.0 * static_cast<double>(within) / static_cast<double>(px) : 100.0;
        const bool identical = fa.full_hash == fb.full_hash && fa.w == fb.w && fa.h == fb.h;
        const bool pass = identical || (maxd <= tol) || (wpct >= pct_req && pct_req < 100.0);
        const char* verdict = identical ? "IDENT" : (pass ? "OK" : "DIFF");
        if (identical) ident++;
        else if (pass) ok++;
        else diff++;
        if ((!identical || shown < 3) && shown < max_lines) {
            shown++;
            std::printf("[GSDIFF] f=%zu vf=%u/%u ident=%d draws=%u/%u dims=%ux%u/%ux%u mean=%.3f,%.3f,%.3f,%.3f "
                        "max=%d within_tol=%.2f%% verdict=%s\n",
                        i, fa.vsync_frame, fb.vsync_frame, identical ? 1 : 0, fa.draws, fb.draws, fa.w, fa.h, fb.w,
                        fb.h, px ? (double)sum[0] / px : 0.0, px ? (double)sum[1] / px : 0.0,
                        px ? (double)sum[2] / px : 0.0, px ? (double)sum[3] / px : 0.0, maxd, wpct, verdict);
        }
    }
    if (a.frames.size() != b.frames.size())
        std::printf("[GSDIFF] frame count mismatch: %zu vs %zu (compared %zu)\n", a.frames.size(), b.frames.size(), n);
    std::printf("[GSDIFF] SUMMARY frames=%zu ident=%zu ok=%zu diff=%zu tol=%d pct_req=%.2f replay_hash_a=0x%016" PRIx64
                " replay_hash_b=0x%016" PRIx64 " hashes_equal=%d\n",
                n, ident, ok, diff, tol, pct_req, a.hdr.replay_hash, b.hdr.replay_hash,
                a.hdr.replay_hash == b.hdr.replay_hash ? 1 : 0);
    if (a.frames.size() != b.frames.size()) return 3;
    return diff ? 3 : 0;
}

bool ReplayDiff::BuildClassProfile(const gs::ClassClassifier::Rules& rules, const char* path, const FrameRange& fr,
                                   bool want_seq, ClassProfile& out) {
    gs::GsdlReader::Recording rec;
    if (!gs::GsdlReader::Load(path, rec)) return false;
    for (const gs::GsdlReader::Frame& f : rec.frames) {
        if (!fr.Contains(f.vsync_frame)) continue;
        for (const gs::GsdlReader::Draw& d : f.draws) {
            const gs::ClassClassifier::Class c = Census::ClassifyRec(rules, rec, d.d);
            const char* cn = gs::ClassClassifier::ClassName(c);
            out.draws[cn]++;
            out.verts[cn] += d.d.vertex_count;
            out.total_draws++;
            if (want_seq) out.seq.push_back(static_cast<uint8_t>(c));
        }
    }
    return true;
}

int ReplayDiff::RunClassDiff(const gs::ClassClassifier::Rules& rules, const char* pa, const char* pb,
                             const FrameRange& fr, bool want_seq) {
    ClassProfile a, b;
    if (!BuildClassProfile(rules, pa, fr, want_seq, a)) return 2;
    if (!BuildClassProfile(rules, pb, fr, want_seq, b)) return 2;
    std::map<std::string, int> names;
    for (const auto& kv : a.draws) names[kv.first] = 1;
    for (const auto& kv : b.draws) names[kv.first] = 1;
    bool differ = false;
    for (const auto& nk : names) {
        const std::string& n = nk.first;
        const uint64_t ad = a.draws.count(n) ? a.draws[n] : 0;
        const uint64_t bd = b.draws.count(n) ? b.draws[n] : 0;
        const uint64_t av = a.verts.count(n) ? a.verts[n] : 0;
        const uint64_t bv = b.verts.count(n) ? b.verts[n] : 0;
        if (ad != bd || av != bv) differ = true;
        std::printf("[GSCLASSDIFF] class=%s draws_a=%" PRIu64 " draws_b=%" PRIu64 " d_draws=%lld verts_a=%" PRIu64
                    " verts_b=%" PRIu64 " d_verts=%lld\n",
                    n.c_str(), ad, bd, (long long)bd - (long long)ad, av, bv, (long long)bv - (long long)av);
    }
    long long first_div = -1;
    uint64_t mismatches = 0;
    if (want_seq) {
        const size_t nmin = std::min(a.seq.size(), b.seq.size());
        for (size_t i = 0; i < nmin; i++) {
            if (a.seq[i] != b.seq[i]) {
                mismatches++;
                if (first_div < 0) {
                    first_div = static_cast<long long>(i);
                    std::printf("[GSCLASSDIFF] SEQ first_div=%lld a=%s b=%s\n", first_div,
                                gs::ClassClassifier::ClassName(a.seq[i]), gs::ClassClassifier::ClassName(b.seq[i]));
                }
            }
        }
        if (a.seq.size() != b.seq.size()) {
            differ = true;
            if (first_div < 0) first_div = static_cast<long long>(nmin);
        }
        if (mismatches) differ = true;
        std::printf("[GSCLASSDIFF] SEQ len_a=%zu len_b=%zu first_div=%lld mismatches=%" PRIu64 "\n", a.seq.size(),
                    b.seq.size(), first_div, mismatches);
    }
    std::printf("[GSCLASSDIFF] SUMMARY classes=%zu draws_a=%" PRIu64 " draws_b=%" PRIu64 " equal=%d\n", names.size(),
                a.total_draws, b.total_draws, differ ? 0 : 1);
    return differ ? 3 : 0;
}

}  // namespace tools
}  // namespace ps2r
