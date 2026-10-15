// core/diag/bench_report.cpp -- the frozen JSON renderer and its helpers.
#include "engine/core/diag/bench_report.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ps2r {
namespace diag {

namespace {

struct Out {
    char*  buf;
    size_t cap;
    size_t len = 0;
    bool   truncated = false;
};

void Raw(Out& o, const char* s) {
    size_t l = std::strlen(s);
    if (o.len + l >= o.cap) {
        o.truncated = true;
        l = (o.cap > o.len + 1) ? o.cap - o.len - 1 : 0;
    }
    std::memcpy(o.buf + o.len, s, l);
    o.len += l;
    o.buf[o.len] = '\0';
}

void Fmt(Out& o, const char* f, ...) __attribute__((format(printf, 2, 3)));
void Fmt(Out& o, const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = std::vsnprintf(o.buf + o.len, o.cap - o.len, f, ap);
    va_end(ap);
    if (r < 0) return;
    if ((size_t)r >= o.cap - o.len) {
        o.truncated = true;
        o.len = o.cap - 1;
    } else {
        o.len += (size_t)r;
    }
}

void KvU64(Out& o, const char* k, uint64_t v, bool last = false) {
    Fmt(o, "\"%s\":%llu%s", k, (unsigned long long)v, last ? "" : ",");
}
void KvI(Out& o, const char* k, int v, bool last = false) {
    Fmt(o, "\"%s\":%d%s", k, v, last ? "" : ",");
}
void KvF(Out& o, const char* k, double v, bool last = false) {
    Fmt(o, "\"%s\":%.3f%s", k, v, last ? "" : ",");
}
void KvS(Out& o, const char* k, const char* v, bool last = false) {
    Fmt(o, "\"%s\":\"%s\"%s", k, v, last ? "" : ",");
}

using EmitFn = void (*)(Out&, const Census&, bool);

void CensusGroup(Out& o, const char* name, bool with_gauges, const Census& fin,
                 const Census& win, EmitFn emit) {
    Fmt(o, "\"%s\":{\"final\":{", name);
    emit(o, fin, with_gauges);
    Raw(o, "},\"window\":{");
    emit(o, win, false);
    Raw(o, "}}");
}

void EmitTexcache(Out& o, const Census& c, bool) {
    KvU64(o, "probes", c.tc_probes); KvU64(o, "elig", c.tc_elig);
    KvU64(o, "ehit", c.tc_ehit);     KvU64(o, "rehit", c.tc_rehit);
    KvU64(o, "dec", c.tc_dec);       KvU64(o, "hhit", c.tc_hhit);
    KvU64(o, "mut", c.tc_mut);       KvU64(o, "nosp", c.tc_nosp, true);
}
void EmitL2(Out& o, const Census& c, bool gauges) {
    KvU64(o, "hit", c.l2_hit); KvU64(o, "miss", c.l2_miss);
    KvU64(o, "ins", c.l2_ins); KvU64(o, "ev", c.l2_ev, !gauges);
    if (gauges) {
        KvU64(o, "live", c.l2_live);
        KvF(o, "live_mb", (double)c.l2_bytes / (1024.0 * 1024.0), true);
    }
}
void EmitL2x(Out& o, const Census& c, bool) {
    KvU64(o, "dgk", c.l2x_dgk);       KvU64(o, "oldrec", c.l2x_oldrec);
    KvU64(o, "norec", c.l2x_norec);   KvU64(o, "drop_dims", c.l2x_drop_dims);
    KvU64(o, "drop_repl", c.l2x_drop_repl); KvU64(o, "drop_budg", c.l2x_drop_budg);
    KvU64(o, "drop_all", c.l2x_drop_all, true);
}
void EmitZpbo(Out& o, const Census& c, bool) {
    KvU64(o, "iss", c.zp_iss);   KvU64(o, "hit", c.zp_hit);
    KvU64(o, "fall", c.zp_fall); KvU64(o, "nocap", c.zp_nocap);
    KvU64(o, "seq", c.zp_seq);   KvU64(o, "cov", c.zp_cov);
    KvU64(o, "timo", c.zp_timo); KvU64(o, "age0", c.zp_age0);
    KvU64(o, "age1", c.zp_age1); KvU64(o, "age2", c.zp_age2, true);
}
void EmitZsamp(Out& o, const Census& c, bool) {
    KvU64(o, "hit32", c.zs_hit32);
    KvU64(o, "hit16", c.zs_hit16);
    KvU64(o, "hit16v", c.zs_hit16v);
    KvU64(o, "fall_off", c.zs_fall_off);
    KvU64(o, "fall_self", c.zs_fall_self);
    KvU64(o, "fall_tbw", c.zs_fall_tbw);
    KvU64(o, "fall_notex", c.zs_fall_notex);
    KvU64(o, "fall_z24", c.zs_fall_z24);
    KvU64(o, "fall_zpsm", c.zs_fall_zpsm);
    KvU64(o, "fall_range", c.zs_fall_range);
    KvU64(o, "zskip_refr", c.zs_skip_refr);
    KvF(o, "refrz_ms", (double)c.zs_refrz_ns / 1e6);
    KvF(o, "refrdec_ms", (double)c.zs_refrdec_ns / 1e6);
    KvU64(o, "zret_skip", c.zs_zret_skip);
    KvU64(o, "zret_keep", c.zs_zret_keep, true);
}
void EmitIdxs(Out& o, const Census& c, bool gauges) {
    KvU64(o, "hit32s", c.idx_hit32s);
    KvU64(o, "hit32", c.idx_hit32);
    KvU64(o, "hit16", c.idx_hit16);
    KvU64(o, "fall_off", c.idx_fall_off);
    KvU64(o, "fall_stale", c.idx_fall_stale);
    KvU64(o, "fall_tbw", c.idx_fall_tbw);
    KvU64(o, "fall_clut", c.idx_fall_clut);
    KvU64(o, "fall_notex", c.idx_fall_notex);
    KvU64(o, "fall_16pair", c.idx_fall_16pair);
    KvU64(o, "fall_fresh", c.idx_fall_fresh);
    KvU64(o, "fall_lane", c.idx_fall_lane);
    KvU64(o, "fall_self", c.idx_fall_self);
    KvU64(o, "fall_zpsm16", c.idx_fall_zpsm16);
    KvU64(o, "fall_range", c.idx_fall_range);
    KvU64(o, "skip_p8", c.idx_skip_p8);
    KvU64(o, "clut_up", c.idx_clut_up);
    KvU64(o, "clut_hit", c.idx_clut_hit);
    KvU64(o, "cret_skip", c.idx_cret_skip);
    KvU64(o, "cret_keep", c.idx_cret_keep, !gauges);
    if (gauges) KvU64(o, "pend", c.idx_pend_live, true);
}
void EmitPool(Out& o, const Census& c, bool gauges) {
    KvU64(o, "ret", c.pool_ret);
    KvU64(o, "del", c.pool_del, !gauges);
    if (gauges) {
        KvU64(o, "live", c.pool_live);
        KvF(o, "live_mb", (double)c.pool_bytes / (1024.0 * 1024.0), true);
    }
}
void EmitUpl(Out& o, const Census& c, bool) {
    KvU64(o, "img", c.upl_img); KvU64(o, "sub", c.upl_sub);
    KvU64(o, "orph", c.upl_orph);
    KvF(o, "img_ms", (double)c.upl_img_ns / 1e6);
    KvF(o, "sub_ms", (double)c.upl_sub_ns / 1e6);
    KvF(o, "orph_ms", (double)c.upl_orph_ns / 1e6, true);
}
void EmitGlob(Out& o, const Census& c, bool) {
    KvU64(o, "texg", c.glob_texg); KvU64(o, "texd", c.glob_texd);
    KvU64(o, "fbog", c.glob_fbog); KvU64(o, "fbod", c.glob_fbod, true);
}

uint64_t Sub(uint64_t x, uint64_t y) { return x >= y ? x - y : 0; }

uint64_t WindowDelta(const uint64_t* cum, uint32_t w0, uint32_t w1) {
    if (!cum) return 0;
    const uint64_t d0 = w0 >= 2 ? cum[w0 - 1] : 0;
    return Sub(cum[w1], d0);
}

void EmitCumArray(Out& o, const char* key, const uint64_t* cum, bool window_ok, uint32_t w0,
                  uint32_t w1) {
    Fmt(o, "\"%s\":[", key);
    if (window_ok && cum != nullptr) {
        for (uint32_t f = w0; f <= w1; f++)
            Fmt(o, "%s%llu", f == w0 ? "" : ",", (unsigned long long)cum[f]);
    }
    Raw(o, "],\n");
}

}  // namespace

const char* BenchReport::MtvuSiteName(int i) {
    static const char* const n[kMtvuSiteCount] = {"vifR", "vifW", "vumem", "syncv", "path", "rstp",
                                                  "dmas", "gifs", "vbl",   "kick",  "other", "priv"};
    return (i >= 0 && i < kMtvuSiteCount) ? n[i] : "?";
}

Census BenchReport::CensusDelta(const Census& a, const Census& b) {
    Census d;
    d.tc_probes = Sub(b.tc_probes, a.tc_probes); d.tc_elig = Sub(b.tc_elig, a.tc_elig);
    d.tc_ehit = Sub(b.tc_ehit, a.tc_ehit);       d.tc_rehit = Sub(b.tc_rehit, a.tc_rehit);
    d.tc_dec = Sub(b.tc_dec, a.tc_dec);          d.tc_hhit = Sub(b.tc_hhit, a.tc_hhit);
    d.tc_mut = Sub(b.tc_mut, a.tc_mut);          d.tc_nosp = Sub(b.tc_nosp, a.tc_nosp);
    d.l2_hit = Sub(b.l2_hit, a.l2_hit);          d.l2_miss = Sub(b.l2_miss, a.l2_miss);
    d.l2_ins = Sub(b.l2_ins, a.l2_ins);          d.l2_ev = Sub(b.l2_ev, a.l2_ev);
    d.l2x_dgk = Sub(b.l2x_dgk, a.l2x_dgk);       d.l2x_oldrec = Sub(b.l2x_oldrec, a.l2x_oldrec);
    d.l2x_norec = Sub(b.l2x_norec, a.l2x_norec);
    d.l2x_drop_dims = Sub(b.l2x_drop_dims, a.l2x_drop_dims);
    d.l2x_drop_repl = Sub(b.l2x_drop_repl, a.l2x_drop_repl);
    d.l2x_drop_budg = Sub(b.l2x_drop_budg, a.l2x_drop_budg);
    d.l2x_drop_all = Sub(b.l2x_drop_all, a.l2x_drop_all);
    d.zp_iss = Sub(b.zp_iss, a.zp_iss);          d.zp_hit = Sub(b.zp_hit, a.zp_hit);
    d.zp_fall = Sub(b.zp_fall, a.zp_fall);       d.zp_nocap = Sub(b.zp_nocap, a.zp_nocap);
    d.zp_seq = Sub(b.zp_seq, a.zp_seq);          d.zp_cov = Sub(b.zp_cov, a.zp_cov);
    d.zp_timo = Sub(b.zp_timo, a.zp_timo);       d.zp_age0 = Sub(b.zp_age0, a.zp_age0);
    d.zp_age1 = Sub(b.zp_age1, a.zp_age1);       d.zp_age2 = Sub(b.zp_age2, a.zp_age2);
    d.zs_hit32 = Sub(b.zs_hit32, a.zs_hit32);
    d.zs_hit16 = Sub(b.zs_hit16, a.zs_hit16);
    d.zs_hit16v = Sub(b.zs_hit16v, a.zs_hit16v);
    d.zs_fall_off = Sub(b.zs_fall_off, a.zs_fall_off);
    d.zs_fall_self = Sub(b.zs_fall_self, a.zs_fall_self);
    d.zs_fall_tbw = Sub(b.zs_fall_tbw, a.zs_fall_tbw);
    d.zs_fall_notex = Sub(b.zs_fall_notex, a.zs_fall_notex);
    d.zs_fall_z24 = Sub(b.zs_fall_z24, a.zs_fall_z24);
    d.zs_fall_zpsm = Sub(b.zs_fall_zpsm, a.zs_fall_zpsm);
    d.zs_fall_range = Sub(b.zs_fall_range, a.zs_fall_range);
    d.zs_skip_refr = Sub(b.zs_skip_refr, a.zs_skip_refr);
    d.zs_refrz_ns = Sub(b.zs_refrz_ns, a.zs_refrz_ns);
    d.zs_refrdec_ns = Sub(b.zs_refrdec_ns, a.zs_refrdec_ns);
    d.zs_zret_skip = Sub(b.zs_zret_skip, a.zs_zret_skip);
    d.zs_zret_keep = Sub(b.zs_zret_keep, a.zs_zret_keep);
    d.idx_hit32s = Sub(b.idx_hit32s, a.idx_hit32s);
    d.idx_hit32 = Sub(b.idx_hit32, a.idx_hit32);
    d.idx_hit16 = Sub(b.idx_hit16, a.idx_hit16);
    d.idx_fall_off = Sub(b.idx_fall_off, a.idx_fall_off);
    d.idx_fall_stale = Sub(b.idx_fall_stale, a.idx_fall_stale);
    d.idx_fall_tbw = Sub(b.idx_fall_tbw, a.idx_fall_tbw);
    d.idx_fall_clut = Sub(b.idx_fall_clut, a.idx_fall_clut);
    d.idx_fall_notex = Sub(b.idx_fall_notex, a.idx_fall_notex);
    d.idx_fall_16pair = Sub(b.idx_fall_16pair, a.idx_fall_16pair);
    d.idx_fall_fresh = Sub(b.idx_fall_fresh, a.idx_fall_fresh);
    d.idx_fall_lane = Sub(b.idx_fall_lane, a.idx_fall_lane);
    d.idx_fall_self = Sub(b.idx_fall_self, a.idx_fall_self);
    d.idx_fall_zpsm16 = Sub(b.idx_fall_zpsm16, a.idx_fall_zpsm16);
    d.idx_fall_range = Sub(b.idx_fall_range, a.idx_fall_range);
    d.idx_skip_p8 = Sub(b.idx_skip_p8, a.idx_skip_p8);
    d.idx_clut_up = Sub(b.idx_clut_up, a.idx_clut_up);
    d.idx_clut_hit = Sub(b.idx_clut_hit, a.idx_clut_hit);
    d.idx_cret_skip = Sub(b.idx_cret_skip, a.idx_cret_skip);
    d.idx_cret_keep = Sub(b.idx_cret_keep, a.idx_cret_keep);
    d.pool_ret = Sub(b.pool_ret, a.pool_ret);    d.pool_del = Sub(b.pool_del, a.pool_del);
    d.upl_img = Sub(b.upl_img, a.upl_img);       d.upl_sub = Sub(b.upl_sub, a.upl_sub);
    d.upl_orph = Sub(b.upl_orph, a.upl_orph);
    d.upl_img_ns = Sub(b.upl_img_ns, a.upl_img_ns);
    d.upl_sub_ns = Sub(b.upl_sub_ns, a.upl_sub_ns);
    d.upl_orph_ns = Sub(b.upl_orph_ns, a.upl_orph_ns);
    d.glob_texg = Sub(b.glob_texg, a.glob_texg); d.glob_texd = Sub(b.glob_texd, a.glob_texd);
    d.glob_fbog = Sub(b.glob_fbog, a.glob_fbog); d.glob_fbod = Sub(b.glob_fbod, a.glob_fbod);
    return d;
}

MtvuSites BenchReport::MtvuDelta(const MtvuSites& a, const MtvuSites& b) {
    MtvuSites d;
    for (int i = 0; i < kMtvuSiteCount; i++) {
        d.hits[i] = Sub(b.hits[i], a.hits[i]);
        d.blocked[i] = Sub(b.blocked[i], a.blocked[i]);
        d.ms[i] = b.ms[i] >= a.ms[i] ? b.ms[i] - a.ms[i] : 0.0;
        d.skips[i] = Sub(b.skips[i], a.skips[i]);
    }
    d.lines_parsed = Sub(b.lines_parsed, a.lines_parsed);
    return d;
}

double BenchReport::FrameMs(const uint64_t* t_ns, uint32_t f) {
    return (double)(t_ns[f] - t_ns[f - 1]) / 1e6;
}

Derived BenchReport::ComputeDerived(const uint64_t* t_ns, uint32_t w0, uint32_t w1) {
    Derived d;
    if (t_ns == nullptr || w0 < 1 || w1 < w0) return d;
    const uint32_t n = w1 - w0 + 1;
    std::vector<double> ms(n);
    double sum = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        ms[i] = FrameMs(t_ns, w0 + i);
        sum += ms[i];
    }
    if (sum <= 0.0) return d;
    d.fps_mean = 1000.0 * (double)n / sum;

    std::vector<double> sorted = ms;
    std::sort(sorted.begin(), sorted.end());
    // Nearest-rank quantile of frame TIME (fps_pN = 1000 / time_q(1-N)).
    auto time_q = [&](double q) {
        size_t idx = (size_t)(q * (double)(n - 1) + 0.5);
        if (idx >= n) idx = n - 1;
        return sorted[idx];
    };
    const double t50 = time_q(0.50);
    const double t95 = time_q(0.95);
    const double tmax = sorted[n - 1];
    d.fps_p50 = t50 > 0.0 ? 1000.0 / t50 : 0.0;
    d.fps_p5 = t95 > 0.0 ? 1000.0 / t95 : 0.0;
    d.fps_min = tmax > 0.0 ? 1000.0 / tmax : 0.0;

    std::vector<uint32_t> order(n);
    for (uint32_t i = 0; i < n; i++) order[i] = i;
    const uint32_t k = n < 10 ? n : 10;
    std::partial_sort(order.begin(), order.begin() + k, order.end(), [&](uint32_t a, uint32_t b) {
        if (ms[a] != ms[b]) return ms[a] > ms[b];
        return a < b;   // deterministic tie-break: earlier frame
    });
    d.worst_count = (int)k;
    for (uint32_t i = 0; i < k; i++) {
        d.worst[i].frame = w0 + order[i];
        d.worst[i].ms = ms[order[i]];
    }
    return d;
}

size_t BenchReport::Render(const ReportInput& in, char* buf, size_t cap) {
    if (buf == nullptr || cap < 2) return 0;
    Out o{buf, cap};

    const uint32_t w0 = in.window_start;
    const uint32_t w1 = in.frames_run;
    const bool window_ok = (in.t_ns != nullptr) && w0 >= 1 && w1 >= w0;
    const Derived d = window_ok ? ComputeDerived(in.t_ns, w0, w1) : Derived{};

    Raw(o, "{\n");

    Raw(o, "\"r127_bench\":{");
    KvI(o, "version", 1);
    KvS(o, "exit_kind", in.exit_kind);
    KvU64(o, "frames_target", in.frames_target);
    KvU64(o, "frames_run", in.frames_run);
    KvU64(o, "window_start_requested", in.window_start_requested);
    KvU64(o, "window_start", in.window_start);
    KvU64(o, "window_end", window_ok ? w1 : 0);
    KvU64(o, "window_frames", window_ok ? (uint64_t)(w1 - w0 + 1) : 0, true);
    Raw(o, "},\n");

    Raw(o, "\"gates\":{");
    {
        const Gates& g = in.gates;
        KvI(o, "collected", g.collected ? 1 : 0);
        KvI(o, "FAST", g.fast);
        KvI(o, "TEXCACHE", g.texcache);
        KvI(o, "BBOX_COPY", g.bbox_copy);
        KvI(o, "NO_FLUSH", g.no_flush);
        KvI(o, "VBO_RING", g.vbo_ring);
        KvI(o, "SNAPSHOT_FAST", g.snapshot_fast);
        KvI(o, "XFER_FAST", g.xfer_fast);
        KvI(o, "FAST2", g.fast2);
        KvI(o, "EPOCH_KEY", g.epoch_key);
        KvI(o, "UNIF_CACHE", g.unif_cache);
        KvI(o, "BIND_CACHE", g.bind_cache);
        KvI(o, "bindmirror", g.bindmirror);
        KvI(o, "BLOCK_DECODE", g.block_decode);
        KvI(o, "TEXSUB", g.texsub);
        KvI(o, "LAZY_RTSYNC", g.lazy_rtsync);
        KvI(o, "FASTHASH", g.fasthash);
        KvI(o, "PAR_DECODE", g.par_decode);
        KvI(o, "HASH_L2", g.hash_l2);
        KvI(o, "ALT16_GPU", g.alt16_gpu);
        KvI(o, "SYNC_RECT", g.sync_rect);
        KvI(o, "LAZY_ZSYNC", g.lazy_zsync);
        KvI(o, "NOVSYNC", g.novsync);
        KvI(o, "MTVU", g.mtvu);
        KvI(o, "VIF_FAST", g.vif_fast);
        KvI(o, "gif_fast", g.gif_fast);
        KvI(o, "l2_prob", g.l2_prob);
        KvI(o, "l2_su", g.l2_su);
        KvI(o, "texdemand", g.texdemand);
        KvI(o, "ZTEX", g.ztex);
        KvI(o, "IDXTEX", g.idxtex);
        KvS(o, "VU_MODE", g.vu_mode);
        KvI(o, "sharded_build", g.sharded);
        KvI(o, "shard_count", g.shard_count, true);
    }
    Raw(o, "},\n");

    uint64_t window_draws = 0, window_vticks = 0, window_bundles = 0;
    if (window_ok) {
        window_draws = WindowDelta(in.draws_cum, w0, w1);
        window_vticks = WindowDelta(in.vticks, w0, w1);
        window_bundles = WindowDelta(in.bundles_cum, w0, w1);
    }
    Raw(o, "\"derived\":{");
    KvF(o, "fps_mean", d.fps_mean);
    KvF(o, "fps_p50", d.fps_p50);
    KvF(o, "fps_p5", d.fps_p5);
    KvF(o, "fps_min", d.fps_min);
    KvU64(o, "window_draws", window_draws);
    KvU64(o, "window_bundles", window_bundles);
    KvU64(o, "window_vblank_ticks", window_vticks);
    KvU64(o, "injected_presents", in.injected_presents);
    KvU64(o, "dispfb_flips", in.dispfb_flips);
    KvF(o, "presents_per_s_fmv", in.presents_per_s_fmv);
    KvI(o, "present_edge", in.present_edge);
    Raw(o, "\"worst_frames\":[");
    for (int i = 0; i < d.worst_count; i++)
        Fmt(o, "%s{\"frame\":%u,\"ms\":%.3f}", i ? "," : "", d.worst[i].frame, d.worst[i].ms);
    Raw(o, "]},\n");

    Raw(o, "\"per_frame_ms\":[");
    if (window_ok) {
        for (uint32_t f = w0; f <= w1; f++) Fmt(o, "%s%.3f", f == w0 ? "" : ",", FrameMs(in.t_ns, f));
    }
    Raw(o, "],\n");

    EmitCumArray(o, "per_frame_draws_cum", in.draws_cum, window_ok, w0, w1);
    EmitCumArray(o, "per_frame_draws_live_cum", in.draws_live_cum, window_ok, w0, w1);
    EmitCumArray(o, "per_frame_vblank_ticks", in.vticks, window_ok, w0, w1);

    Raw(o, "\"perf_ms_per_frame\":{");
    for (int i = 0; i < kSubsysCount; i++)
        KvF(o, PerfProfiler::Name(i), in.perf_ms[i], i == kSubsysCount - 1);
    Raw(o, "},\n");

    Raw(o, "\"mtvusync\":{");
    KvS(o, "source", "r123_census_lines");
    KvU64(o, "lines_parsed", in.mtvu.lines_parsed);
    Raw(o, "\"sites\":{");
    for (int i = 0; i < kMtvuSiteCount; i++) {
        Fmt(o, "\"%s\":{\"hits\":%llu,\"blocked\":%llu,\"ms\":%.1f,\"skips\":%llu}%s",
            MtvuSiteName(i), (unsigned long long)in.mtvu.hits[i],
            (unsigned long long)in.mtvu.blocked[i], in.mtvu.ms[i],
            (unsigned long long)in.mtvu.skips[i], i == kMtvuSiteCount - 1 ? "" : ",");
    }
    Raw(o, "}},\n");

    Raw(o, "\"census\":{");
    CensusGroup(o, "texcache", false, in.census_final, in.census_window, &EmitTexcache);
    Raw(o, ",");
    CensusGroup(o, "l2", true, in.census_final, in.census_window, &EmitL2);
    Raw(o, ",");
    CensusGroup(o, "l2x", false, in.census_final, in.census_window, &EmitL2x);
    Raw(o, ",");
    CensusGroup(o, "zpbo", false, in.census_final, in.census_window, &EmitZpbo);
    Raw(o, ",");
    CensusGroup(o, "zsamp", false, in.census_final, in.census_window, &EmitZsamp);
    Raw(o, ",");
    CensusGroup(o, "idxs", true, in.census_final, in.census_window, &EmitIdxs);
    Raw(o, ",");
    CensusGroup(o, "pool", true, in.census_final, in.census_window, &EmitPool);
    Raw(o, ",");
    CensusGroup(o, "upl", false, in.census_final, in.census_window, &EmitUpl);
    Raw(o, ",");
    CensusGroup(o, "glob", false, in.census_final, in.census_window, &EmitGlob);
    Raw(o, "},\n");

    Raw(o, "\"rss_mb\":{");
    KvF(o, "start", in.rss_start_mb);
    KvF(o, "end", in.rss_end_mb);
    KvF(o, "peak", in.rss_peak_mb, true);
    Raw(o, "}\n}\n");

    return o.len;
}

size_t BenchReport::SuggestBufferSize(uint32_t window_frames) {
    return 65536 + (size_t)window_frames * 120;
}

size_t BenchReport::SummaryLine(const ReportInput& in, char* buf, size_t cap) {
    const uint32_t w0 = in.window_start;
    const uint32_t w1 = in.frames_run;
    const bool window_ok = (in.t_ns != nullptr) && w0 >= 1 && w1 >= w0;
    const Derived d = window_ok ? ComputeDerived(in.t_ns, w0, w1) : Derived{};
    const uint64_t wdraws = window_ok ? WindowDelta(in.draws_cum, w0, w1) : 0;
    int n = std::snprintf(buf, cap,
        "[R127-BENCH] exit=%s frames=%u window=[%u,%u] (%u frames) "
        "fps{mean=%.2f p50=%.2f p5=%.2f min=%.2f} worst_ms=%.2f@f%u "
        "rss_mb{start=%.0f end=%.0f peak=%.0f} "
        "anchor{wdraws=%llu inj=%llu flips=%llu pps_fmv=%.1f edge=%d}",
        in.exit_kind, in.frames_run, w0, window_ok ? w1 : 0, window_ok ? (w1 - w0 + 1) : 0,
        d.fps_mean, d.fps_p50, d.fps_p5, d.fps_min, d.worst_count ? d.worst[0].ms : 0.0,
        d.worst_count ? d.worst[0].frame : 0, in.rss_start_mb, in.rss_end_mb, in.rss_peak_mb,
        (unsigned long long)wdraws, (unsigned long long)in.injected_presents,
        (unsigned long long)in.dispfb_flips, in.presents_per_s_fmv, in.present_edge);
    return n > 0 ? (size_t)n : 0;
}

bool BenchReport::AccumulateMtvuLine(const char* line, MtvuSites& acc) {
    static const char kPrefix[] = "[R123-MTVUSYNC]";
    if (std::strncmp(line, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
    acc.lines_parsed++;
    const char* p = line + sizeof(kPrefix) - 1;
    while (*p) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        const char* end = p;
        while (*end && *end != ' ') end++;
        const size_t tlen = (size_t)(end - p);
        const char* eq = (const char*)std::memchr(p, '=', tlen);
        if (eq != nullptr) {
            char name[16];
            const size_t nl = (size_t)(eq - p);
            if (nl > 0 && nl < sizeof(name)) {
                std::memcpy(name, p, nl);
                name[nl] = '\0';
                for (int i = 0; i < kMtvuSiteCount; i++) {
                    if (std::strcmp(name, MtvuSiteName(i)) != 0) continue;
                    unsigned long long h = 0, b = 0, sk = 0;
                    double msv = 0.0;
                    int consumed = 0;
                    if (std::sscanf(eq + 1, "%llu/%llu/%lf%n", &h, &b, &msv, &consumed) == 3) {
                        acc.hits[i] += h;
                        acc.blocked[i] += b;
                        acc.ms[i] += msv;
                        if (std::sscanf(eq + 1 + consumed, "(skip=%llu)", &sk) == 1) acc.skips[i] += sk;
                    }
                    break;
                }
            }
        }
        p = end;
    }
    return true;
}

bool BenchReport::ParseGfxresDrawsLine(const char* line, uint64_t* draws_out, uint64_t* verts_out) {
    static const char kPrefix[] = "[R147-GFXRES]";
    if (std::strncmp(line, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
    const char* p = std::strstr(line, "n{draws=");
    if (p == nullptr) return false;
    unsigned long long draws = 0, verts = 0;
    if (std::sscanf(p, "n{draws=%llu verts=%llu}", &draws, &verts) != 2) return false;
    if (draws_out) *draws_out = (uint64_t)draws;
    if (verts_out) *verts_out = (uint64_t)verts;
    return true;
}

}  // namespace diag
}  // namespace ps2r
