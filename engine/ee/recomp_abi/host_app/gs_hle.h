#pragma once
// Generated-code ABI forwarder (rows 15, 16, 17, 19): GS/GIF/DMA sync seams.
#include <atomic>
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

extern std::atomic<uint32_t> g_sceGsSyncV_count;
extern std::atomic<uint32_t> g_vsync_frame;

void hle_sceGsSyncV(CpuContext& ctx);
void hle_sceGsSyncPath(CpuContext& ctx);
void hle_sceGsResetGraph(CpuContext& ctx);
void hle_sceGsResetPath(CpuContext& ctx);
void hle_sceDmaSync(CpuContext& ctx);
void hle_sceGifSyncDma(CpuContext& ctx);
void hle_sceGsSendImage(CpuContext& ctx);
void hle_SetupDisplayRegisters(CpuContext& ctx);
