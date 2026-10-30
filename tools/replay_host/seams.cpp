// The engine seams the generated code and the candidate reference, defined for the replay host. Everything the
// game would reach through these is OUTSIDE the function under test: the kernel (syscalls), the VU0
// microprograms, the scheduler's yield point, the diagnostics ring. The replay host records what was reached
// and keeps the body running where that is harmless, or aborts the record where a result would be invented.
//
// Sources these mirror (engine side): engine/ee/cpu/seams.cpp (the production seams), engine/ee/cpu/fn_hooks.cpp
// (recomp_probe_enter/exit/unwind), engine/ee/cpu/access_tracer.cpp (AccessTracer::OnRead/OnWrite),
// engine/vu/macro/vu_macro_bridge.cpp:122-135 (VuMacroCommitFlags, copied verbatim), engine/ee/cpu/dispatch_table.cpp
// (the three DispatchTable wrappers the common TU calls -- here identity wrappers, no proof bookkeeping).
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>

#include "replay_state.h"

#include "engine/ee/cpu/access_tracer.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/recomp_abi/host_app/ps2_threading.h"
#include "engine/ee/recomp_abi/host_app/syscalls.h"
#include "engine/ee/recomp_abi/host_app/vu.h"
#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"

using replay::S;

// ---- the probe bracket (recompiled.h's RECOMP-PROBE marker; every id is armed by the host) -------------------------
extern "C" void recomp_probe_enter(uint32_t /*id*/, uint32_t entry, CpuContext& /*ctx*/, uint32_t /*entry_ra*/) {
    auto& s = S();
    if (s.depth == 1) s.callee_seq.push_back(entry);
    ++s.depth;
}
extern "C" void recomp_probe_exit(uint32_t /*id*/, uint32_t /*entry*/, CpuContext& /*ctx*/) {
    auto& s = S();
    if (s.depth > 0) --s.depth;
}
extern "C" void recomp_probe_unwind(uint32_t /*id*/, uint32_t /*entry*/) {
    auto& s = S();
    if (s.depth > 0) --s.depth;
}

// ---- the access tracer (the candidate is compiled with PS2R_TRACE_ACCESS; so are the shards of a tracing build)
bool AccessTracer::Available() { return true; }
bool AccessTracer::Active() { return true; }
bool AccessTracer::Start(const std::string&, const std::vector<uint32_t>&,
                         const std::vector<std::pair<std::string, std::string>>&, uint64_t, std::string* error) {
    if (error) *error = "the replay host does not record corpora";
    return false;
}
bool AccessTracer::Stop(std::string* error) {
    if (error) *error = "the replay host does not record corpora";
    return false;
}
AccessTracer::Stats AccessTracer::CurrentStats() { return {}; }
void AccessTracer::OnRead(uint32_t addr, const void*, uint32_t n) { S().OnRead(addr, n); }
void AccessTracer::OnWrite(uint32_t addr, const void*, uint32_t n) { S().OnWrite(addr, n); }

// ---- the yield point (recompiler --yield-backedges): a loop that trips it too often is a runaway -------------
uint32_t g_ee_backedge_budget = 1024;
void ps2r_ee_yield_point(CpuContext&) {
    auto& s = S();
    g_ee_backedge_budget = 1024;
    if (++s.trips > s.max_trips) throw replay::Abort{"runaway loop: back-edge trips exceeded the budget"};
}

// ---- the kernel: a syscall's effect is not replayable; it is recorded and the registers are left as they are -
void runtime_syscall_dispatcher(uint32_t syscall_num, CpuContext&) { S().syscalls.push_back(syscall_num); }
void handle_mtc0_write(CpuContext&, uint8_t, uint32_t) {}

namespace ps2_threading {
void exitThread() { throw replay::Abort{"exitThread reached"}; }
std::shared_ptr<ThreadInfo> lookupThreadInfo(int) { return nullptr; }
}  // namespace ps2_threading

// ---- VU0 macro mode: the flag commit is pure arithmetic (copied from vu_macro_bridge.cpp:122-135); VCALLMS is not
void VuMacroCommitFlags(VUregs& regs, const VR_reg& result, uint8_t mask) {
    const uint16_t mac = VuFloat::PackMacBits(result.I, mask);
    regs.mac_flag = mac;
    uint16_t status = static_cast<uint16_t>(regs.status_flag & 0xFF0u);
    if (mac & 0x000Fu) status |= vu_status::Z;
    if (mac & 0x00F0u) status |= vu_status::S;
    if (mac & 0x0F00u) status |= vu_status::U;
    if (mac & 0xF000u) status |= vu_status::O;
    if (status & vu_status::Z) status |= vu_status::ZS;
    if (status & vu_status::S) status |= vu_status::SS;
    if (status & vu_status::U) status |= vu_status::US;
    if (status & vu_status::O) status |= vu_status::OS;
    regs.status_flag = status;
}
void Vu0Macro::Execute(CpuContext&, uint32_t) { ++S().vu0_calls; }
void Vu0Macro::ResumeIfWaiting(CpuContext&) {}
void Vu1Window::Execute(CpuContext&, uint32_t) { ++S().vu0_calls; }

// ---- the dispatch surface wrappers (dispatch_table.cpp keeps proof metadata; the replay host needs none) -----
DispatchTable::Body DispatchTable::GeneratedBody(uint32_t, Body body, Continuations) { return body; }
DispatchTable::Body DispatchTable::TraceBody(uint32_t, const Body&, Body traced) { return traced; }
DispatchTable::Body DispatchTable::ObserveInstall(uint32_t, Body body) { return body; }

// ---- the shadow stack's two notifications (the common TU owns the stack itself) ------------------------------
void ShadowCallStack::RejectedPush() {}
void ShadowCallStack::StackEmptied() {}

// ---- diagnostics gates (seams.cpp rows 26 / E1) ------------------------------------------------------------------
extern "C" { bool crashring_enabled = false; }
extern "C" void crashring_record_indirect(uint32_t, uint32_t) {}
extern "C" [[noreturn]] void crashring_fatal_no_jump(uint32_t pc) {
    throw replay::Abort{"indirect jump to an address outside the generated code: 0x" + [pc] {
        char b[16];
        std::snprintf(b, sizeof b, "%08x", pc);
        return std::string(b);
    }()};
}
extern "C" { bool recomp_fast_dispatch_enabled = true; }

// [RECOMP-P3a] every tier of find_containing_function missed: no body exists for the target.
const std::function<void(CpuContext&, uint32_t)>* recomp_fcf_miss(uint32_t pc) {
    static std::function<void(CpuContext&, uint32_t)> miss = [](CpuContext&, uint32_t target) {
        crashring_fatal_no_jump(target);
    };
    (void)pc;
    return &miss;
}

// sceGsSyncV's frame counter (engine/gs/hle/sce/gs_sync.cpp); the generated diag arms read it.
std::atomic<uint32_t> g_vsync_frame{0};
