// tests/core/test_bench_report.cpp -- the frozen bench report format (diag
// builds only): derived statistics, JSON key set/order, census-line parsers.
// Ported from the frozen original's test_bench_report cases.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/diag/bench_report.h"
#include "engine/core/diag/perf_profiler.h"

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

static bool Near(double a, double b) { return std::fabs(a - b) < 1e-3 * std::fmax(1.0, std::fabs(b)); }

static double JsonNum(const std::string& j, const char* key, size_t from = 0) {
    const std::string k = std::string("\"") + key + "\":";
    const size_t p = j.find(k, from);
    if (p == std::string::npos) return -1e300;
    return std::atof(j.c_str() + p + k.size());
}

int main() {
    using namespace ps2r::diag;
    // 20 frames: frame f takes f*5 ms (f=1..20) -> window [11,20].
    std::vector<uint64_t> t(21, 0);
    for (uint32_t f = 1; f <= 20; f++) t[f] = t[f - 1] + (uint64_t)f * 5 * 1000000ull;
    Derived d = BenchReport::ComputeDerived(t.data(), 11, 20);
    // sum(55..100 step 5) = 775 ms over 10 frames
    CHECK(Near(d.fps_mean, 1000.0 * 10.0 / 775.0));
    CHECK(Near(d.fps_min, 1000.0 / 100.0));
    CHECK(d.worst_count == 10);
    CHECK(d.worst[0].frame == 20 && Near(d.worst[0].ms, 100.0));
    CHECK(d.worst[1].frame == 19 && Near(d.worst[1].ms, 95.0));
    CHECK(d.worst[9].frame == 11 && Near(d.worst[9].ms, 55.0));

    std::vector<uint64_t> draws(21, 0), vt(21, 0);
    for (uint32_t f = 0; f <= 20; f++) { draws[f] = 100 * f; vt[f] = 3 * f; }
    ReportInput in;
    in.frames_target = 20;
    in.frames_run = 20;
    in.window_start_requested = 1200;
    in.window_start = 11;
    in.exit_kind = "frames_reached";
    in.gates.mtvu = 1;
    in.gates.vif_fast = 1;
    in.gates.novsync = 1;
    in.gates.sharded = 1;
    in.gates.shard_count = 32;
    in.gates.collected = true;
    std::snprintf(in.gates.vu_mode, sizeof(in.gates.vu_mode), "cached");
    in.t_ns = t.data();
    in.draws_cum = draws.data();
    in.vticks = vt.data();
    in.injected_presents = 123;
    in.dispfb_flips = 456;
    in.presents_per_s_fmv = 30.5;
    in.present_edge = 1;
    std::vector<char> buf(BenchReport::SuggestBufferSize(10));
    const size_t len = BenchReport::Render(in, buf.data(), buf.size());
    CHECK(len > 0 && len < buf.size());
    const std::string j(buf.data(), len);
    CHECK(j.size() >= 2 && j.back() == '\n' && j[j.size() - 2] == '}');
    int brace = 0, brack = 0;
    bool ascii = true;
    for (unsigned char ch : j) {
        if (ch > 0x7e) ascii = false;
        brace += (ch == '{') - (ch == '}');
        brack += (ch == '[') - (ch == ']');
    }
    CHECK(ascii && brace == 0 && brack == 0);
    // Top-level key order (frozen).
    const char* order[] = {"\"r127_bench\":", "\"gates\":", "\"derived\":", "\"per_frame_ms\":",
                           "\"per_frame_draws_cum\":", "\"per_frame_draws_live_cum\":",
                           "\"per_frame_vblank_ticks\":", "\"perf_ms_per_frame\":", "\"mtvusync\":",
                           "\"census\":", "\"rss_mb\":"};
    size_t pos = 0;
    for (const char* k : order) {
        const size_t p = j.find(k, pos);
        CHECK(p != std::string::npos);
        if (p != std::string::npos) pos = p;
    }
    CHECK(Near(JsonNum(j, "frames_target"), 20.0));
    CHECK(Near(JsonNum(j, "window_start"), 11.0));
    CHECK(Near(JsonNum(j, "window_end"), 20.0));
    CHECK(Near(JsonNum(j, "window_frames"), 10.0));
    CHECK(j.find("\"exit_kind\":\"frames_reached\"") != std::string::npos);
    CHECK(j.find("\"VU_MODE\":\"cached\"") != std::string::npos);
    CHECK(j.find("\"MTVU\":1") != std::string::npos);
    CHECK(j.find("\"shard_count\":32") != std::string::npos);
    CHECK(j.find("\"BLOCK_DECODE\":0") != std::string::npos);
    const size_t dpos = j.find("\"derived\":{");
    CHECK(Near(JsonNum(j, "fps_mean", dpos), d.fps_mean));
    CHECK(Near(JsonNum(j, "window_draws", dpos), 1000.0));
    CHECK(Near(JsonNum(j, "window_vblank_ticks", dpos), 30.0));
    CHECK(Near(JsonNum(j, "injected_presents", dpos), 123.0));
    CHECK(j.find("\"worst_frames\":[{\"frame\":20,\"ms\":100.000") != std::string::npos);
    CHECK(j.find("\"per_frame_draws_cum\":[1100,") != std::string::npos);
    CHECK(j.find("\"per_frame_vblank_ticks\":[33,") != std::string::npos);
    // Every perf bucket name appears in the perf block, in enum order.
    size_t pp = j.find("\"perf_ms_per_frame\":{");
    for (int i = 0; i < kSubsysCount; i++) {
        const std::string k = std::string("\"") + PerfProfiler::Name(i) + "\":";
        const size_t p = j.find(k, pp);
        CHECK(p != std::string::npos);
        if (p != std::string::npos) pp = p;
    }
    CHECK(std::string(PerfProfiler::Name(0)) == "ee" && std::string(PerfProfiler::Name(kSubsysCount - 1)) == "p2copy");
    CHECK(kSubsysCount == 31);

    // Census-line parsers.
    MtvuSites acc;
    CHECK(!BenchReport::AccumulateMtvuLine("[PERF] f=1 fps=2", acc));
    CHECK(BenchReport::AccumulateMtvuLine(
        "[R123-MTVUSYNC] sites h/b/ms: vifR=5/2/12.5 syncv=100/40/500.0 vbl=10/1/1.0(skip=4) path=2/0/33.3", acc));
    CHECK(acc.lines_parsed == 1 && acc.hits[0] == 5 && acc.blocked[0] == 2 && Near(acc.ms[0], 12.5));
    CHECK(acc.hits[3] == 100 && acc.skips[8] == 4 && acc.hits[4] == 2 && Near(acc.ms[4], 33.3));
    uint64_t dr = 0, vv = 0;
    CHECK(BenchReport::ParseGfxresDrawsLine("[R147-GFXRES] ms{vconv=1} n{draws=154122 verts=987654} cpbo{}", &dr, &vv));
    CHECK(dr == 154122ull && vv == 987654ull);
    CHECK(!BenchReport::ParseGfxresDrawsLine("[R147-GFXRES] ms{vconv=1}", &dr, &vv));

    char line[512];
    CHECK(BenchReport::SummaryLine(in, line, sizeof(line)) > 0);
    CHECK(std::strstr(line, "exit=frames_reached frames=20 window=[11,20] (10 frames)") != nullptr);

    if (g_fail) {
        std::printf("test_bench_report: %d FAILURE(S)\n", g_fail);
        return 1;
    }
    std::printf("test_bench_report: all checks passed\n");
    return 0;
}
