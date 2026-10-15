// engine/ipu/hle/libipu_bindings.h -- LibipuBindings: the libipu.a SDK rows
// (sceIpuInit, sceIpuSync, sceIpuStopDMA, sceIpuRestartDMA) as manifest thunks
// over ONE LibipuApi instance and the GuestMemory the save buffer lives in. The
// manifest handler is a plain function pointer, so Bind installs file-static
// thunks over the bound instance (the libdma / libcdvd rows' shape).
//
// SDK rules (register sequences are LibipuApi's; this file owns the ABI):
//   sceIpuInit()            -> void (RST, BCLR, RST, BCLR)
//   sceIpuSync(mode)        -> v0 = 0 after the CTRL.BUSY poll (mode 1: the bit)
//   sceIpuStopDMA(save)     -> void; `save` (a0) receives the nine snapshot
//                              words in the SDK order (ch4 MADR/TADR/QWC/CHCR,
//                              ch3 MADR/QWC/CHCR, IPU_BP, IPU_CTRL); a null
//                              pointer returns without touching anything
//   sceIpuRestartDMA(save)  -> void; the nine words are read back from a0 and
//                              replayed; null returns
//   setD3_CHCR              -> stays a trap row (never reached on this title)
#pragma once

#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;

namespace ps2r::ipu {

class GuestMemory;
class LibipuApi;

class LibipuBindings {
public:
    static constexpr const char* kLibrary = "libipu.a";

    LibipuBindings(LibipuApi& api, GuestMemory& memory);

    // Registers the four rows as thunks over `self`; returns the number registered.
    static int Bind(SdkBindingManifest& m, LibipuBindings* self);

    void SceIpuInit(CpuContext& ctx);
    void SceIpuSync(CpuContext& ctx);
    void SceIpuStopDma(CpuContext& ctx);
    void SceIpuRestartDma(CpuContext& ctx);

    // Evidence counters for run.meta.
    uint64_t InitCalls() const { return init_calls_; }
    uint64_t SyncCalls() const { return sync_calls_; }
    uint64_t StopCalls() const { return stop_calls_; }
    uint64_t RestartCalls() const { return restart_calls_; }

private:
    LibipuApi& api_;
    GuestMemory& memory_;
    uint64_t init_calls_ = 0;
    uint64_t sync_calls_ = 0;
    uint64_t stop_calls_ = 0;
    uint64_t restart_calls_ = 0;
};

}  // namespace ps2r::ipu
