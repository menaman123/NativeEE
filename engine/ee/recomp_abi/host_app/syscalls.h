#pragma once
// Generated-code ABI forwarder (row 6): the two kernel entry points the
// generated `syscall` / `mtc0` sites call. Provided by the SyscallTable (3C2);
// until then engine/ee/cpu/seams.cpp parks them.
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

void runtime_syscall_dispatcher(uint32_t syscall_num, CpuContext& ctx);
void handle_mtc0_write(CpuContext& ctx, uint8_t rd, uint32_t value);

// The back-edge yield seam (recompiler --yield-backedges): a generated loop's
// back-edge runs `if (--g_ee_backedge_budget == 0) ps2r_ee_yield_point(ctx);`.
// One plain counter is enough -- only the EE run-token holder executes guest
// code, and the token changes hands under a mutex. The engine owns both
// (engine/ee/cpu/seams.cpp -> engine/ee/sched/ee_yield_point.h): unbound, the
// yield only reloads the budget. Generated code built without the flag never
// references either name.
extern uint32_t g_ee_backedge_budget;
void ps2r_ee_yield_point(CpuContext& ctx);
