#pragma once
// KernlSifRpc: THE PARK OWNER of Phase 3. Every libkernl SIF RPC entry
// (sceSifInitRpc / InitCmd / BindRpc / CallRpc / CheckStatRpc / RegisterRpc /
// SetRpcQueue / GetNextRequest / RpcLoop / AddCmdHandler / LoadModule / IopHeap)
// records `park=sif_rpc_absent` and never returns: there is no IOP before
// Phase 4, which rebinds these rows through sif/hle registration. Both the
// generated seams (row 14, hle_sceSif*) and the manifest rows land here.
#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;

class KernlSifRpc {
public:
    static void Register(SdkBindingManifest& m);
    [[noreturn]] static void Park(CpuContext& ctx);
    static const char* ParkReason() { return "sif_rpc_absent"; }
};
