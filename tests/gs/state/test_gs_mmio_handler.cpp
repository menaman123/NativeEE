// tests/gs/state/test_gs_mmio_handler.cpp -- GsMmioHandler: the EE's view of the
// GS privileged page over a GsState. Usage: test_gs_mmio_handler <case>
//   csr | halves | jobs | widen | all
#include <cstdint>
#include <cstdio>
#include <string>

#include "engine/gs/state/gs_mmio_handler.h"
#include "engine/gs/state/gs_state.h"
#include "tests/lib/capture_sink.h"

using namespace ps2r::gs;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg)                                                       \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(c)) {                                                         \
            ++g_fail;                                                       \
            std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
        }                                                                   \
    } while (0)

namespace {
constexpr uint32_t kBase = 0x12000000u;
constexpr uint32_t kCsr = kBase + GsPrivRegs::kCsr;
constexpr uint32_t kDispfb1 = kBase + GsPrivRegs::kDispfb1;
constexpr uint32_t kDisplay1 = kBase + GsPrivRegs::kDisplay1;
constexpr uint32_t kBgcolor = kBase + GsPrivRegs::kBgcolor;
}  // namespace

// CSR: power-on reads kCsrAfterReset; a 64-bit RESET write zeroes the register
// file AND CSR (the reference's reset path: GS_Reset then CSR = 0); FINISH
// (general register 0x61) sets CSR.FINISH and a 32-bit write-1 clears it; reads count.
static void Csr() {
    GsState gs;
    GsMmioHandler h(gs);
    CHECK(h.Read(kCsr, 8) == GsPrivRegs::kCsrAfterReset, "power-on CSR reads kCsrAfterReset (0x1402) through the handler");
    gs.WritePrivileged(kBase + GsPrivRegs::kPmode, 0x66);
    h.Write(kCsr, 8, GsPrivRegs::kCsrReset);
    CHECK(h.Read(kCsr, 8) == 0, "64-bit RESET -> CSR reads 0");
    CHECK(h.Read(kBase + GsPrivRegs::kPmode, 8) == 0, "RESET zeroed PMODE");
    gs.WriteRegister(0x61, 0);  // FINISH
    CHECK((h.Read(kCsr, 4) & GsPrivRegs::kCsrFinish) != 0, "FINISH visible in the 32-bit lower half");
    h.Write(kCsr, 4, GsPrivRegs::kCsrFinish);
    CHECK((h.Read(kCsr, 4) & GsPrivRegs::kCsrFinish) == 0, "32-bit write-1 clears FINISH");
    CHECK(h.Read(kCsr + 4, 4) == 0, "CSR upper half reads 0");
    CHECK(h.Reads() == 6 && h.Writes() == 2, "access census");
    CHECK(gs.Priv().Writes() == 3, "the shadow counted the three register writes (one direct, two via the handler)");
}

// 32-bit halves merge into the 64-bit register in either order; a 64-bit read
// returns the merged value and each half reads back.
static void Halves() {
    GsState gs;
    GsMmioHandler h(gs);
    const uint32_t smode1 = kBase + GsPrivRegs::kSmode1;
    h.Write(smode1, 4, 0x11223344u);
    CHECK(h.Read(smode1, 8) == 0x0000000011223344ull, "lower half written, upper 0");
    h.Write(smode1 + 4, 4, 0x55667788u);
    CHECK(h.Read(smode1, 8) == 0x5566778811223344ull, "upper half merged");
    CHECK(h.Read(smode1, 4) == 0x11223344u && h.Read(smode1 + 4, 4) == 0x55667788u, "halves read back");
    h.Write(smode1, 4, 0xAABBCCDDu);
    CHECK(h.Read(smode1, 8) == 0x55667788AABBCCDDull, "a lower rewrite keeps the upper half");
    CHECK(gs.ReadPrivileged(smode1) == 0x55667788AABBCCDDull, "the façade sees the same register");
}

// Display registers written through the handler emit the presenter jobs on the
// façade's sink: DISPFB -> SetFrontBuffer, DISPLAY -> SetWindow, BGCOLOR ->
// SetClearColor, each once per changed value, stamped with the live epoch.
static void Jobs() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    GsMmioHandler h(gs);
    auto jobs_of = [&sink](JobKind k) {
        size_t n = 0;
        for (const JobRec& j : sink.jobs) n += (j.type == (uint32_t)k);
        return n;
    };
    const uint64_t dispfb = (uint64_t)0x10 | ((uint64_t)10 << 9);  // FBP 0x10 FBW 10 PSM 0
    h.Write(kDispfb1, 4, (uint32_t)dispfb);
    h.Write(kDispfb1 + 4, 4, 0);
    CHECK(jobs_of(JobKind::SetFrontBuffer) == 1, "DISPFB1 via halves emits ONE SetFrontBuffer (the upper half changed nothing)");
    CHECK(sink.jobs.back().arg1 == 0x10 * 8192 && sink.jobs.back().arg2 == 640, "SetFrontBuffer args (fbp bytes, fbw pixels)");
    const uint64_t display = ((uint64_t)3 << 23) | ((uint64_t)2559 << 32) | ((uint64_t)447 << 44);
    h.Write(kDisplay1, 8, display);
    CHECK(jobs_of(JobKind::SetWindow) == 1 && sink.jobs.back().arg2 == ((640u << 16) | 640u), "DISPLAY1 64-bit emits SetWindow 640 wide");
    h.Write(kDisplay1, 8, display);
    CHECK(jobs_of(JobKind::SetWindow) == 1, "same DISPLAY value: no second job");
    h.Write(kBgcolor, 8, 0x00332211);
    CHECK(jobs_of(JobKind::SetClearColor) == 1 && sink.jobs.back().arg1 == 0x11 && sink.jobs.back().arg3 == 0x33, "BGCOLOR emits SetClearColor r,g,b");
    CHECK(sink.jobs.back().vram_epoch == gs.Vram().UploadEpoch(), "job stamped with the live upload epoch");
    CHECK(h.Read(kDispfb1, 8) == dispfb && h.Read(kBgcolor, 8) == 0x00332211, "reads come back from the shadow");
}

// 16/8-bit accesses widen to the enclosing 32-bit half: byte lanes are merged on
// write and extracted on read.
static void Widen() {
    GsState gs;
    GsMmioHandler h(gs);
    const uint32_t imr = kBase + GsPrivRegs::kImr;
    h.Write(imr, 4, 0x00007F00u);
    h.Write(imr + 1, 1, 0xAB);
    CHECK(h.Read(imr, 4) == 0x0000AB00u, "byte 1 merged into the lower half");
    h.Write(imr + 2, 2, 0xCDEF);
    CHECK(h.Read(imr, 4) == 0xCDEFAB00u, "halfword at +2 merged");
    CHECK(h.Read(imr + 1, 1) == 0xAB && h.Read(imr + 2, 2) == 0xCDEF && h.Read(imr + 3, 1) == 0xCD, "sub-word reads extract their lanes");
    h.Write(imr + 6, 2, 0x1234);
    CHECK(h.Read(imr, 8) == 0x12340000CDEFAB00ull, "a halfword in the upper half lands at bits 48-63");
}

int main(int argc, char** argv) {
    const std::string c = argc > 1 ? argv[1] : "all";
    if (c == "csr" || c == "all") Csr();
    if (c == "halves" || c == "all") Halves();
    if (c == "jobs" || c == "all") Jobs();
    if (c == "widen" || c == "all") Widen();
    std::printf("test_gs_mmio_handler %s: %d checks, %d failures\n", c.c_str(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
