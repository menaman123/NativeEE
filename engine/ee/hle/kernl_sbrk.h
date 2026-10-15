#pragma once
// KernlSbrk: libkernl's `sbrk(incr)` -- the libc break-pointer heap grower.
// PS2 rule (libkernl 3.0.1): the break word (seeded by InitHeap; the title's
// BootParams::sbrk_break_word) advances by `incr` rounded up to 16 bytes; the
// call returns the previous break, or -1 when the new break would pass the
// end of main RAM (32 MB) or wrap. The original ran the same body
// (sce_hle/libkernl.cpp:249-280). Rebound over KernlSifRpc's park row: the
// coldboot leg's `park=sif_rpc_absent` at 0x2cdadc was THIS row, not a SIF one.
#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;

class KernlSbrk {
public:
    static constexpr uint32_t kRamEnd = 0x02000000u;
    static void Register(SdkBindingManifest& m);
    static void Sbrk(CpuContext& ctx);
    static uint64_t Calls();
};
