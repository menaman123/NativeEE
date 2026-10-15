#pragma once
#include <cstdint>

// Lifetime diagnostics, not guest state. Qwords include tags copied with TTE.
// A rejected start is either unsupported or faulted; neither is completed.
struct DmacTransferCount {
    uint64_t starts = 0;
    uint64_t completed = 0;
    uint64_t quadwords = 0;
    uint64_t rejected = 0;
    uint64_t unsupported = 0;
    uint64_t faults = 0;
};

struct DmacTransferStats {
    // Indexed by raw CHCR.MOD: normal, chain, interleave, reserved.
    DmacTransferCount spr_from[4];
    DmacTransferCount spr_to[4];
    // D_CTRL remains write-dropped: count guest requests for SPR MFIFO/stall
    // separately so a later ordinary copy cannot hide an unsupported setup.
    uint64_t unsupported_spr_ctrl_writes = 0;
};
