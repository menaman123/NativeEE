#pragma once
// LibgraphBindings: the libgraph.a rows on this title's manifest that are not
// the boundary owner (GsSync binds sceGsSyncV). The rows write the GS
// privileged register file through the guest page (the MMIO route, so the
// page's handler sees every store) AND the GsPrivRegs shadow they are bound
// over; the GsGParam statics at `gparam_addr` are title data passed in.
//
//   sceGsResetGraph(mode, interlace, omode, ffmode) -- the SDK 3.0 libgraph
//                          shape: mode 0 writes CSR.RESET (bit 9), mode 1
//                          CSR.FLUSH (bit 8); GsGParam = four shorts
//                          {interlace, omode, ffmode != 0, GS revision = CSR
//                          bits 48..55}; a registered SyncV callback (+0x08 fn,
//                          +0x0C handler id) is torn down (_DisableIntc(2),
//                          RemoveIntcHandler(2, id), both words zeroed);
//                          GsPutIMR(0xff00) masks every GS interrupt; then the
//                          console's power-on display environment (SMODE2 <- 1,
//                          DISPLAY1/2, DISPFB1/2 = the BIOS's NTSC 512x448
//                          scanout) so a scanout target exists from the first
//                          frame; the tail call SetGsCrt(interlace & 1,
//                          omode & 0xff, ffmode & 1) supplies v0.
//   sceGsSyncVCallback(fn) -> registers `fn` as the VBLANK-start INTC handler
//                          (cause 2, appended, arg 0, the caller's $gp) through
//                          the kernel's handler table, enabling the cause;
//                          fn == 0 removes the current handler and disables
//                          the cause. GsGParam +0x08 holds the function and
//                          +0x0C the handler id; v0 = the previous function.
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;

namespace ps2r {
namespace gs {

class GsPrivRegs;

class LibgraphBindings {
public:
    struct Statics {
        uint32_t gparam_addr = 0;   // the SDK's sceGsGParam block in this ELF
    };
    // Binds the two rows over `shadow`; returns the number registered.
    static int Bind(SdkBindingManifest& m, GsPrivRegs* shadow, const Statics& statics);
    static uint64_t ResetGraphCalls();
    static uint64_t SyncVCallbackCalls();
    static void ResetForTest();

    // Row bodies (the manifest's handlers).
    static void ResetGraph(CpuContext& ctx);
    static void SyncVCallback(CpuContext& ctx);

private:
    static void WritePriv(uint32_t addr, uint64_t value);
    static GsPrivRegs* shadow_;
    static Statics statics_;
    static uint64_t reset_graph_calls_;
    static uint64_t syncv_callback_calls_;
};

}  // namespace gs
}  // namespace ps2r
