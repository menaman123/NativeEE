// core/diag/bench_report.h -- the bench report: data structs, derived
// statistics and the JSON renderer (diagnostics builds only). Headless and
// allocation-free in the renderer so tests exercise it byte-for-byte.
//
// Renderer contract (FROZEN -- the optimisation-loop tooling diffs these files):
//   * snprintf-only into a caller-provided buffer;
//   * STABLE KEY ORDER, every key always present (zeros when a source is
//     unavailable); the top-level object keys and the census/gates vocabulary
//     are exactly the frozen original's;
//   * ASCII only.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/diag/perf_profiler.h"

namespace ps2r {
namespace diag {

constexpr int kMtvuSiteCount = 12;

struct MtvuSites {
    uint64_t hits[kMtvuSiteCount]    = {};
    uint64_t blocked[kMtvuSiteCount] = {};
    double   ms[kMtvuSiteCount]      = {};
    uint64_t skips[kMtvuSiteCount]   = {};
    uint64_t lines_parsed            = 0;
};

// Renderer-side census counters (monotonic; "window" = field-wise delta of two
// snapshots; the gauge fields are point-in-time and only meaningful in "final").
struct Census {
    uint64_t tc_probes = 0, tc_elig = 0, tc_ehit = 0, tc_rehit = 0;
    uint64_t tc_dec = 0, tc_hhit = 0, tc_mut = 0, tc_nosp = 0;
    uint64_t l2_hit = 0, l2_miss = 0, l2_ins = 0, l2_ev = 0;
    uint64_t l2x_dgk = 0, l2x_oldrec = 0, l2x_norec = 0;
    uint64_t l2x_drop_dims = 0, l2x_drop_repl = 0, l2x_drop_budg = 0, l2x_drop_all = 0;
    uint64_t zp_iss = 0, zp_hit = 0, zp_fall = 0;
    uint64_t zp_nocap = 0, zp_seq = 0, zp_cov = 0, zp_timo = 0;
    uint64_t zp_age0 = 0, zp_age1 = 0, zp_age2 = 0;
    uint64_t zs_hit32 = 0, zs_hit16 = 0, zs_hit16v = 0;
    uint64_t zs_fall_off = 0, zs_fall_self = 0, zs_fall_tbw = 0;
    uint64_t zs_fall_notex = 0, zs_fall_z24 = 0, zs_fall_zpsm = 0;
    uint64_t zs_fall_range = 0;
    uint64_t zs_skip_refr = 0;
    uint64_t zs_refrz_ns = 0, zs_refrdec_ns = 0;
    uint64_t zs_zret_skip = 0, zs_zret_keep = 0;
    uint64_t idx_hit32s = 0, idx_hit32 = 0, idx_hit16 = 0;
    uint64_t idx_fall_off = 0, idx_fall_stale = 0, idx_fall_tbw = 0;
    uint64_t idx_fall_clut = 0, idx_fall_notex = 0, idx_fall_16pair = 0;
    uint64_t idx_fall_fresh = 0;
    uint64_t idx_fall_lane = 0;
    uint64_t idx_fall_self = 0, idx_fall_zpsm16 = 0, idx_fall_range = 0;
    uint64_t idx_skip_p8 = 0;
    uint64_t idx_clut_up = 0, idx_clut_hit = 0;
    uint64_t idx_cret_skip = 0, idx_cret_keep = 0;
    uint64_t idx_pend_live = 0;   // gauge
    uint64_t pool_ret = 0, pool_del = 0;
    uint64_t upl_img = 0, upl_sub = 0, upl_orph = 0;
    uint64_t upl_img_ns = 0, upl_sub_ns = 0, upl_orph_ns = 0;
    uint64_t glob_texg = 0, glob_texd = 0, glob_fbog = 0, glob_fbod = 0;
    uint64_t l2_live = 0, l2_bytes = 0, pool_live = 0, pool_bytes = 0;   // gauges
};

// Resolved gate values the run actually used (the frozen key vocabulary).
struct Gates {
    int fast = 0, texcache = 0, bbox_copy = 0, no_flush = 0, vbo_ring = 0;
    int snapshot_fast = 0, xfer_fast = 0;
    int fast2 = 0, epoch_key = 0, unif_cache = 0, bind_cache = 0, bindmirror = 0;
    int block_decode = 0, texsub = 0, lazy_rtsync = 0, fasthash = 0, par_decode = 0;
    int hash_l2 = 0, alt16_gpu = 0, sync_rect = 0, lazy_zsync = 0;
    int novsync = 0;
    int mtvu = 0, vif_fast = 0;
    int gif_fast = 0, l2_prob = 0, texdemand = 0;
    int l2_su = 0;
    int ztex = 0;
    int idxtex = 0;
    char vu_mode[16] = "interp";
    int sharded = 0, shard_count = 0;
    bool collected = false;
};

struct WorstFrame {
    uint32_t frame = 0;
    double   ms = 0.0;
};

struct Derived {
    double fps_mean = 0.0;
    double fps_p50 = 0.0;
    double fps_p5 = 0.0;
    double fps_min = 0.0;
    int    worst_count = 0;
    WorstFrame worst[10];
};

struct ReportInput {
    uint32_t    frames_target = 0;
    uint32_t    frames_run = 0;
    uint32_t    window_start_requested = 0;
    uint32_t    window_start = 0;
    const char* exit_kind = "unknown";   // frames_reached | early_exit | signal
    Gates       gates;
    const uint64_t* t_ns = nullptr;            // t_ns[0..frames_run] timestamps
    const uint64_t* bundles_cum = nullptr;
    const uint64_t* draws_cum = nullptr;
    const uint64_t* draws_live_cum = nullptr;
    const uint64_t* vticks = nullptr;
    uint64_t    injected_presents = 0;
    uint64_t    dispfb_flips = 0;
    double      presents_per_s_fmv = 0.0;
    int         present_edge = -1;
    double      perf_ms[kSubsysCount] = {};
    MtvuSites   mtvu;
    Census      census_final;
    Census      census_window;
    double      rss_start_mb = 0.0, rss_end_mb = 0.0, rss_peak_mb = 0.0;
};

class BenchReport {
public:
    static const char* MtvuSiteName(int i);
    static Census CensusDelta(const Census& a, const Census& b);
    static MtvuSites MtvuDelta(const MtvuSites& a, const MtvuSites& b);

    // Per-frame duration of frame f (t[f] - t[f-1], valid for 1 <= f <= last).
    static double FrameMs(const uint64_t* t_ns, uint32_t f);
    // Derived stats over window [w0, w1] (inclusive frame numbers, both >= 1).
    static Derived ComputeDerived(const uint64_t* t_ns, uint32_t w0, uint32_t w1);

    // Renders the full report. Returns bytes written (excluding NUL); the
    // buffer is always NUL-terminated and truncated JSON lacks the trailing
    // newline. Size buffers with SuggestBufferSize().
    static size_t Render(const ReportInput& in, char* buf, size_t cap);
    static size_t SuggestBufferSize(uint32_t window_frames);
    // One-line human summary.
    static size_t SummaryLine(const ReportInput& in, char* buf, size_t cap);

    // Census-line parsers (pure string parsing).
    static bool AccumulateMtvuLine(const char* line, MtvuSites& acc);
    static bool ParseGfxresDrawsLine(const char* line, uint64_t* draws_out, uint64_t* verts_out);
};

}  // namespace diag
}  // namespace ps2r
