#include "engine/ee/hle/kernl_sif_rpc.h"

#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/sdk_binding_manifest.h"

// park_pc is the CALLER of the SIF row ($ra): the bound row runs in place of
// the generated body, whose pc the generated code only materialises at jumps.
void KernlSifRpc::Park(CpuContext& ctx) { GuestPark::Forever(ParkReason(), ctx.cpuRegs.GPR.r[31].UL[0]); }

void KernlSifRpc::Register(SdkBindingManifest& m) {
    static const char* const kRows[] = {
        "sceSifInitRpc",       "sceSifInitCmd",        "sceSifBindRpc",      "sceSifCallRpc",
        "sceSifCheckStatRpc",  "sceSifRegisterRpc",    "sceSifSetRpcQueue",  "sceSifGetNextRequest",
        "sceSifRpcLoop",       "sceSifAddCmdHandler",  "sceSifLoadModule",   "_sceSifLoadModule",
        "_lf_bind",            "sceSifInitIopHeap",    "sceSifAllocIopHeap", "sceSifFreeIopHeap",
        "sceSifFreeSysMemory", "sceSifWriteBackDCache", "SetT2",             "sbrk",
        "kFindAddress",        "kCopy",                "InitExecPS2",        "PatchIsNeeded",
        "iGetTimerSystemTime", "SetNextComp",
    };
    for (const char* name : kRows) m.Register("libkernl.a", name, &KernlSifRpc::Park);
}
