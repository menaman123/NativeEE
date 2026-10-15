#include "engine/ee/hle/kernl_file_io.h"

#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/sdk_binding_manifest.h"

namespace {
void Ret0(CpuContext& ctx) { ctx.cpuRegs.GPR.r[2].SD[0] = 0; }
}  // namespace

void KernlFileIo::ParkIopDependent(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc); }
void KernlFileIo::ParkOpen(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libkernl.a sceOpen"); }
void KernlFileIo::ParkClose(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libkernl.a sceClose"); }
void KernlFileIo::ParkWrite(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libkernl.a write"); }
void KernlFileIo::ParkGetIob(CpuContext& ctx) { GuestPark::Forever("iop_dependent", ctx.cpuRegs.pc, "libkernl.a get_iob"); }

void KernlFileIo::Register(SdkBindingManifest& m) {
    m.Register("libkernl.a", "sceOpen", &KernlFileIo::ParkOpen);
    m.Register("libkernl.a", "sceClose", &KernlFileIo::ParkClose);
    m.Register("libkernl.a", "write", &KernlFileIo::ParkWrite);
    m.Register("libkernl.a", "get_iob", &KernlFileIo::ParkGetIob);
    m.Register("libkernl.a", "sceFsInit", &Ret0);
    m.Register("libkernl.a", "sceFsReset", &Ret0);
    m.Register("libkernl.a", "_sceFsWaitS", &Ret0);
    m.Register("libkernl.a", "_sceFsSemInit", &Ret0);
    m.Register("libkernl.a", "_sceFsIobSemaMK", &Ret0);
    m.Register("libkernl.a", "sceFsSifCallRpc", &Ret0);
}
