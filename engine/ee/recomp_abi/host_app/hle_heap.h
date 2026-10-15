#pragma once
// Generated-code ABI forwarder (row 22): the 12 libc/allocator hook bodies
// the common TU installs. Title pass-throughs (games/<t>/hle/glue/).
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

void HLE_002c5080(CpuContext& ctx);
void HLE_002c5820(CpuContext& ctx);
void HLE_002c5940(CpuContext& ctx);
void HLE_002c6ce0(CpuContext& ctx);
void HLE_002c6d8c(CpuContext& ctx);
void HLE_002c6e88(CpuContext& ctx);
void HLE_002c7070(CpuContext& ctx);
void HLE_002c71bc(CpuContext& ctx);
void HLE_002c7418(CpuContext& ctx);
void HLE_002c75e8(CpuContext& ctx);
void HLE_002ccba0(CpuContext& ctx);
void HLE_002cf930(CpuContext& ctx);

// 3B census finding P3-SEAM-2/3 (PHASE3_PLAN 7.1): five loader file-IO hooks
// spelled `HLE_FUN_` (windowed ISO read 0x2aac80, WaitForReadComplete 0x2ad1a0,
// 0x2ad8f0, 0x2adb18, Close Handle 0x2adb40; OLD hle_heap.h:283-287) and the
// PTC-stub record lookup (OLD hle_heap.h:495). Park (iop_dependent) / inert.
#include <string>
#include <vector>

void HLE_FUN_002aac80(CpuContext& ctx, uint32_t addr);
void HLE_FUN_002ad1a0(CpuContext& ctx, uint32_t addr);
void HLE_FUN_002ad8f0(CpuContext& ctx, uint32_t addr);
void HLE_FUN_002adb18(CpuContext& ctx, uint32_t addr);
void HLE_FUN_002adb40(CpuContext& ctx, uint32_t addr);
const std::vector<uint8_t>* GetPtcRecord(const std::string& normalized_path);
