#pragma once
// Generated-code ABI forwarder (row 23): libcdvd stream seams (park: IOP-dependent).
#include "engine/ee/cpu/cpu_state.h"

void hle_sceCdStInit(CpuContext& ctx);
void hle_sceCdStStart(CpuContext& ctx);
void hle_sceCdStSeek(CpuContext& ctx);
void hle_sceCdStStop(CpuContext& ctx);
void hle_sceCdStRead(CpuContext& ctx);
void hle_sceCdStStat(CpuContext& ctx);
void hle_sceCdStPause(CpuContext& ctx);
void hle_sceCdStResume(CpuContext& ctx);
void hle_sceCdDiskReady(CpuContext& ctx);
void hle_sceCdDiskReady2(CpuContext& ctx);
