#pragma once
// Generated-code ABI forwarder (row 6): the two kernel entry points the
// generated `syscall` / `mtc0` sites call. Provided by the SyscallTable (3C2);
// until then engine/ee/cpu/seams.cpp parks them.
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

void runtime_syscall_dispatcher(uint32_t syscall_num, CpuContext& ctx);
void handle_mtc0_write(CpuContext& ctx, uint8_t rd, uint32_t value);
