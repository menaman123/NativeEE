#pragma once
// KernlFileIo: the libkernl file-IO family (sceOpen / sceClose / write /
// get_iob / sceFs*). Every body that needs the IOP file server PARKS with
// `park=iop_dependent` in Phase 3 (the CDVD/IOP HLE of Phase 4 rebinds these
// rows through its own registration, not by editing this pair). The sceFs
// init/sema helpers that the SDK runs before any IOP round trip return 0.
#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;

class KernlFileIo {
public:
    static void Register(SdkBindingManifest& m);
    [[noreturn]] static void ParkIopDependent(CpuContext& ctx);
    // The four parking rows, each naming itself in the park record.
    [[noreturn]] static void ParkOpen(CpuContext& ctx);
    [[noreturn]] static void ParkClose(CpuContext& ctx);
    [[noreturn]] static void ParkWrite(CpuContext& ctx);
    [[noreturn]] static void ParkGetIob(CpuContext& ctx);
};
