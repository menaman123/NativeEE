#pragma once
// LibsdrBindings: the libsdr SDK rows (library "libsdr.a"; the title's
// sdk_bindings.tsv names two: sceSdRemoteInit, sceSdRemote) as bodies over
// SdrRemote. libsdr's real bodies bind the SDRDRV server (0x80000701) over SIF
// and forward every command as an RPC; here the rows call the ONE SpuCore
// directly, so no SIF bind is issued by the EE for the sound driver (the RPC
// face stays registered for a title that binds it itself).
//   sceSdRemoteInit()                       -> 0
//   sceSdRemote(mode, cmd, a2, a3, t0, t1)  -> SdrRemote::Dispatch(cmd, a2, a3, t0, t1)
//       (mode = a0 is the remote's blocking mode; the arguments ride a2/a3 and
//        the EE's t0/t1 argument registers -- libsdr's six-argument shape)
#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;

namespace ps2r::sound::hle {

class SdrRemote;

class LibsdrBindings {
public:
    static constexpr size_t kRowCount = 2;

    // Registers the rows on the manifest; returns the row count.
    static int Bind(SdkBindingManifest& m, SdrRemote* remote);
    static const char* const* RowNames(size_t* count);

    static uint64_t InitCalls();
    static uint64_t RemoteCalls();
    // "init=<n>,remote=<n>,cmds=<cmd>:<n>,..." for run.meta.
    static std::string CallsCensus();

    // The row bodies (manifest handlers).
    static void RemoteInit(CpuContext& ctx);
    static void Remote(CpuContext& ctx);
};

}  // namespace ps2r::sound::hle
