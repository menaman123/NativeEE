// Runtime seams of the generated code (PHASE3_PLAN 3.4): every roster symbol
// the generated TUs call and NEW must provide, with its stage-3B provider.
// Kinds: REAL = forwards to the owning engine class; PARK = GuestPark::Forever with
// a reason (never returns); INTERIM PARK = the same body until the named later
// stage installs the real provider (3D2 gs_sync); INERT = a definition whose observable effect is nothing. Title
// pass-throughs are declared here through the ABI headers but DEFINED in
// games/<t>/hle/glue/.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>

#include "engine/core/log/ring_log.h"
#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/cpu/park_site_dump.h"
#include "engine/ee/hle/kernl_boot.h"
#include "engine/ee/hle/kernl_sif_rpc.h"
#include "engine/ee/hle/kernl_timer.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/recomp_abi/host_app/cdvd_stream.h"
#include "engine/ee/recomp_abi/host_app/gs_hle.h"
#include "engine/ee/recomp_abi/host_app/ps2_threading.h"
#include "engine/ee/recomp_abi/host_app/sif_hle.h"
#include "engine/ee/recomp_abi/host_app/syscalls.h"
#include "engine/ee/recomp_abi/host_app/timer_hle.h"
#include "engine/ee/recomp_abi/host_app/vu.h"

// ---- row 26: crash ring (REAL: forwards to core's RingLog; fatal = report + PARK)
// The ONLY definitions of this extern "C" trio in the tree (ring_log owns the
// behaviour, this seam owns the frozen C names). `crashring_enabled` is the plain
// bool the 2,619 indirect-call guards test; DispatchTable::Initialize binds it to
// RingLog, which keeps it equal to "EE channel in the verbose tier".
extern "C" { bool crashring_enabled = false; }   // the brace form: a definition, not an initialised `extern` (GCC -Wall)
extern "C" void crashring_record_indirect(uint32_t target, uint32_t ra) {
    ps2r::ringlog::RingLog::EmitV(ps2r::ringlog::CH_EE, ps2r::ringlog::TAG_INDIRECT_JUMP, target, ra);
}
extern "C" [[noreturn]] void crashring_fatal_no_jump(uint32_t pc) {
    ps2r::ringlog::RingLog::FatalNoJumpReport(pc);  // ring_crash.rlog + crash_report.txt
    // The park record names the site: the shadow chain (innermost = the function
    // doing the jr/jalr) and the parking guest thread's GPRs, so the register
    // the zero target came from -- and the object it was loaded from -- are
    // readable from run.meta's park_detail without a second leg.
    const CpuContext* ctx = nullptr;
    if (auto info = ThreadManager::Instance().Current()) ctx = info->ctx;
    GuestPark::Forever("fatal_no_jump", pc, ParkSiteDump::FatalNoJump(pc, ctx));
}

// ---- row 7: the one real seam of the dispatch surface (PARK) -----------------
// find_containing_function's all-tiers-missed tail: an indirect-jump target no
// tier resolves is terminal, exactly as on OLD (recomp_seams.cpp routes it
// through crashring_fatal_no_jump: report, then park).
const std::function<void(CpuContext&, uint32_t)>* recomp_fcf_miss(uint32_t pc) {
    crashring_fatal_no_jump(pc);
}

// ---- row 14: SIF RPC bindings (PARK: KernlSifRpc is the Phase-3 park owner) -
void hle_sceSifInitRpc(CpuContext& ctx) { KernlSifRpc::Park(ctx); }
void hle_sceSifBindRpc(CpuContext& ctx) { KernlSifRpc::Park(ctx); }
void hle_sceSifCallRpc(CpuContext& ctx) { KernlSifRpc::Park(ctx); }

// ---- row 23: libcdvd stream (PARK: iop_dependent) ----------------------------
void hle_sceCdStInit(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdStInit"); }
void hle_sceCdStStart(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdStStart"); }
void hle_sceCdStSeek(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdStSeek"); }
void hle_sceCdStStop(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdStStop"); }
void hle_sceCdStRead(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdStRead"); }
void hle_sceCdStStat(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdStStat"); }
void hle_sceCdStPause(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdStPause"); }
void hle_sceCdStResume(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdStResume"); }
void hle_sceCdDiskReady(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdDiskReady"); }
void hle_sceCdDiskReady2(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libcdvd.a sceCdDiskReady2"); }

// ---- row 27: the vector units (engine/vu/macro/vu_macro_bridge.cpp, ps2r_vu)
// provides Vu0Macro::Execute / Vu0Macro::ResumeIfWaiting / Vu1Window::Execute
// (the COP2 <-> VuUnit hand-over; unbound they park as vu_micro_absent).

// ---- rows 6, 18, 20 (REAL): SyscallTable, KernlTimer, KernlBoot -------------
void runtime_syscall_dispatcher(uint32_t v1, CpuContext& ctx) { SyscallTable::Instance().Dispatch(v1, ctx); }
void handle_mtc0_write(CpuContext& ctx, uint8_t rd, uint32_t value) { SyscallTable::Instance().HandleMtc0(ctx, rd, value); }
void hle_TimerWaitMicroseconds(CpuContext& ctx) { KernlTimer::TimerWaitMicroseconds(ctx); }
void hle_InitTLB(CpuContext& ctx) { KernlBoot::InitTLB(ctx); }
void hle_ContextRestore(CpuContext& ctx) { KernlBoot::ContextRestore(ctx); }

// ---- row 15: the boundary owner (engine/gs/hle/sce/gs_sync.cpp, ps2r_gs_sync)
// provides hle_sceGsSyncV / hle_sceGsSyncPath / g_sceGsSyncV_count / g_vsync_frame.

// ---- row 13: thread exit + main-thread lookup (REAL: ThreadManager) ---------
// exitThread unwinds the calling guest host thread through ThreadExitUnwind;
// lookupThreadInfo(1)->gp is the main thread record adopted at boot.
namespace ps2_threading {
void exitThread() { ThreadManager::Instance().ExitThread(); }
std::shared_ptr<ThreadInfo> lookupThreadInfo(int tid) { return ThreadManager::Instance().Lookup(tid); }
}  // namespace ps2_threading

// ---- P3-SEAM-2: the loader's five file-IO hooks (HLE_FUN_002ad8f0 open,
// 002adb18 size, 002aac80 windowed read, 002adb40 close, 002ad1a0 wait) are
// title pass-throughs DEFINED in games/<t>/hle/glue/loader_glue.cpp over
// LoaderFileIo (ReadEngine + LoaderModel).
