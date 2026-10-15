// LibgraphBindings + GsPrivRegs: the two non-boundary libgraph rows over the GS
// privileged page (the MMIO sink until the GS registers the range) and the
// GsPrivRegs shadow (Phase 6's class; its CSR semantics win).
//   * both rows register under "libgraph.a";
//   * sceGsResetGraph(mode 0): CSR <- RESET through the page (the sink counts it)
//     and the shadow (which zeroes its file and CSR); the GsGParam words
//     {interlace, omode, ffmode, GS revision} at the title's address; SetGsCrt
//     run with the same arguments; SMODE2 = 1, DISPLAY1/2 and DISPFB1/2 = the
//     BIOS's NTSC 512x448 scanout in the shadow; v0 = 0;
//   * sceGsResetGraph(mode 1): CSR <- FLUSH, a no-op on the shadow's file;
//   * sceGsSyncVCallback(fn): the kernel's INTC table gains a cause-2 record with
//     fn / the caller's $gp / arg 0 at the tail, INTC_MASK bit 2 set, GsGParam
//     +0x08/+0x0C = fn/id, v0 = the previous fn; a second registration replaces
//     the first (one record); fn = 0 removes it and clears the mask bit;
//   * a reset while a callback is registered tears the callback down;
//   * a NULL sink on the shadow is safe for every register the rows write.
#include <cstdio>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/gs/hle/sce/libgraph_bindings.h"
#include "engine/gs/state/gs_priv_regs.h"

using ps2r::gs::GsPrivRegs;
using ps2r::gs::LibgraphBindings;

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

namespace {
constexpr uint32_t kGparam = 0x00200000u;   // any RAM word block: the test's GsGParam
constexpr uint32_t kGp = 0x00310000u;

uint32_t Call(void (*fn)(CpuContext&), uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    static CpuContext ctx;
    ctx.cpuRegs.GPR.r[4].UL[0] = a0;
    ctx.cpuRegs.GPR.r[5].UL[0] = a1;
    ctx.cpuRegs.GPR.r[6].UL[0] = a2;
    ctx.cpuRegs.GPR.r[7].UL[0] = a3;
    ctx.cpuRegs.GPR.r[28].UL[0] = kGp;
    ctx.cpuRegs.GPR.r[29].UL[0] = 0x001FFF00u;
    ctx.cpuRegs.GPR.r[2].UD[0] = 0xDEADBEEFull;
    fn(ctx);
    return ctx.cpuRegs.GPR.r[2].UL[0];
}

size_t CauseCount(int cause) {
    size_t n = 0;
    for (const auto& h : SyscallTable::Instance().IntcHandlers()) n += h.cause == cause;
    return n;
}
}  // namespace

int main() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);
    SyscallTable::Instance().ResetForTest();

    GsPrivRegs shadow;   // no sink, no job context: the rows must be safe against both
    CHECK(shadow.Writes() == 0 && shadow.Read(GsPrivRegs::kCsr) == GsPrivRegs::kCsrAfterReset, "shadow_power_on_csr_0x1402");
    shadow.Write(GsPrivRegs::kPmode, 0x66);
    CHECK(shadow.Read(GsPrivRegs::kPmode) == 0x66 && shadow.Writes() == 1, "shadow_records_a_slot_and_counts");
    shadow.Write(0x12000004u, 1);   // off-slot: no register, still a counted write
    CHECK(shadow.Read(0x12000004u) == 0 && shadow.Writes() == 2, "shadow_off_slot_reads_0");
    CHECK(GsPrivRegs::kCsrReset == (1ull << 9) && GsPrivRegs::kCsrFlush == (1ull << 8), "csr_reset_bit9_flush_bit8");

    SdkBindingManifest& m = SdkBindingManifest::Instance();
    m.ResetForTest();
    LibgraphBindings::ResetForTest();
    LibgraphBindings::Statics statics;
    statics.gparam_addr = kGparam;
    CHECK(LibgraphBindings::Bind(m, &shadow, statics) == 2, "two_rows_registered");
    CHECK(m.Find("libgraph.a", "sceGsResetGraph") != nullptr && m.Find("libgraph.a", "sceGsSyncVCallback") != nullptr,
          "rows_resolve_under_libgraph");

    // --- sceGsResetGraph(mode=0, interlace=1, omode=2 (NTSC), ffmode=0) ---
    const uint64_t sink_writes0 = sink.WriteCount();
    const uint64_t shadow_writes0 = shadow.Writes();
    CHECK(Call(&LibgraphBindings::ResetGraph, 0, 1, 2, 0) == 0, "resetgraph_returns_0");
    CHECK(shadow.Read(GsPrivRegs::kPmode) == 0, "resetgraph_reset_zeroed_the_file");
    // Phase 6's CSR.RESET leaves CSR = 0 (the RESET request is not readable back).
    CHECK(shadow.Read(GsPrivRegs::kCsr) == 0, "resetgraph_csr_0_after_reset");
    CHECK(memory::read<uint16_t>(kGparam) == 1 && memory::read<uint16_t>(kGparam + 2) == 2 && memory::read<uint16_t>(kGparam + 4) == 0 &&
              memory::read<uint16_t>(kGparam + 6) == 0 && memory::read<uint32_t>(kGparam + 8) == 0,
          "resetgraph_gparam_shorts_interlace_omode_ffmode_rev");
    CHECK(shadow.Read(GsPrivRegs::kSmode2) == 1 && shadow.Interlaced(), "resetgraph_smode2_interlaced_field");
    const uint64_t dispfb = 0x38ull | (8ull << 9);
    CHECK(shadow.Read(GsPrivRegs::kDispfb1) == dispfb && shadow.Read(GsPrivRegs::kDispfb2) == dispfb, "resetgraph_dispfb_fbp38_fbw8");
    const uint64_t d1 = shadow.Read(GsPrivRegs::kDisplay1), d2 = shadow.Read(GsPrivRegs::kDisplay2);
    CHECK((d1 & 0xFFF) == 0x27C && ((d1 >> 12) & 0x7FF) == 0x32 && ((d2 >> 12) & 0x7FF) == 0x33 && ((d1 >> 23) & 0xF) == 4 &&
              ((d1 >> 32) & 0xFFF) == 2559 && ((d1 >> 44) & 0x7FF) == 447,
          "resetgraph_display_ntsc_512x448");
    CHECK(sink.WriteCount() == sink_writes0 + 6, "resetgraph_six_page_writes_reach_the_sink");
    CHECK(shadow.Writes() == shadow_writes0 + 6, "resetgraph_six_shadow_writes");
    CHECK(LibgraphBindings::ResetGraphCalls() == 1, "resetgraph_counted");
    CHECK((SyscallTable::Instance().GsImr() & 0xff00u) == 0xff00u, "resetgraph_putimr_ff00");

    // --- sceGsResetGraph(mode=1): FLUSH, the file survives ---
    shadow.Write(GsPrivRegs::kPmode, 0x66);
    CHECK(Call(&LibgraphBindings::ResetGraph, 1, 1, 2, 0) == 0, "resetgraph_mode1_returns_0");
    CHECK(shadow.Read(GsPrivRegs::kPmode) == 0x66 && shadow.Read(GsPrivRegs::kCsr) == 0, "resetgraph_mode1_flush_keeps_the_file");
    CHECK(LibgraphBindings::ResetGraphCalls() == 2, "resetgraph_mode1_counted");

    // --- sceGsSyncVCallback ---
    SyscallTable& k = SyscallTable::Instance();
    CHECK(CauseCount(2) == 0 && (k.IntcMask() & 0x4u) == 0, "no_vblank_handler_before");
    CHECK(Call(&LibgraphBindings::SyncVCallback, 0x00123450u) == 0, "syncvcb_first_returns_0");
    CHECK(CauseCount(2) == 1, "syncvcb_one_record");
    const auto& h = k.IntcHandlers().back();
    CHECK(h.cause == 2 && h.pc == 0x00123450u && h.gp == kGp && h.arg == 0 && h.active, "syncvcb_record_fields");
    CHECK((k.IntcMask() & 0x4u) != 0, "syncvcb_enables_cause_2");
    CHECK(memory::read<uint32_t>(kGparam + 8) == 0x00123450u && memory::read<uint32_t>(kGparam + 12) == static_cast<uint32_t>(h.id),
          "syncvcb_gparam_fn_and_id");
    CHECK(Call(&LibgraphBindings::SyncVCallback, 0x00123460u) == 0x00123450u, "syncvcb_replace_returns_previous");
    CHECK(CauseCount(2) == 1 && k.IntcHandlers().back().pc == 0x00123460u && (k.IntcMask() & 0x4u) != 0, "syncvcb_replace_one_record");
    CHECK(Call(&LibgraphBindings::SyncVCallback, 0) == 0x00123460u, "syncvcb_unregister_returns_previous");
    CHECK(CauseCount(2) == 0 && (k.IntcMask() & 0x4u) == 0, "syncvcb_unregister_clears");
    CHECK(memory::read<uint32_t>(kGparam + 8) == 0 && memory::read<uint32_t>(kGparam + 12) == 0, "syncvcb_gparam_cleared");
    CHECK(LibgraphBindings::SyncVCallbackCalls() == 3, "syncvcb_counted");

    // --- a reset with a callback registered tears it down ---
    CHECK(Call(&LibgraphBindings::SyncVCallback, 0x00123470u) == 0 && CauseCount(2) == 1, "syncvcb_registered_again");
    CHECK(Call(&LibgraphBindings::ResetGraph, 0, 1, 2, 0) == 0, "resetgraph_with_callback_returns_0");
    CHECK(CauseCount(2) == 0 && (k.IntcMask() & 0x4u) == 0, "resetgraph_removes_the_callback_and_disables_cause_2");
    CHECK(memory::read<uint32_t>(kGparam + 8) == 0 && memory::read<uint32_t>(kGparam + 12) == 0, "resetgraph_clears_gparam_fn_id");

    std::printf(g_fail ? "test_libgraph_bindings: %d FAILED\n" : "test_libgraph_bindings: all vectors passed\n", g_fail);
    return g_fail ? 1 : 0;
}
