#pragma once
// Generated-code ABI forwarder (rows 14, 20, 21): SIF RPC bindings (park
// owners), boot-env seams and the global-constructors pass-through.
#include "engine/ee/cpu/cpu_state.h"

void hle_sceSifInitRpc(CpuContext& ctx);
void hle_sceSifBindRpc(CpuContext& ctx);
void hle_sceSifCallRpc(CpuContext& ctx);
void hle_InitTLB(CpuContext& ctx);
void hle_ContextRestore(CpuContext& ctx);
void hle_DoGlobalConstructors(CpuContext& ctx);
