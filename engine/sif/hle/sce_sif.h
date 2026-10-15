#pragma once
// SceSif: the libkernl SIF RPC entry points as real bodies, REBOUND over the
// Phase-3 park owner's rows (KernlSifRpc) through SdkBindingManifest::Rebind
// -- the park owner is not edited (D-SEQ-2). Every body keeps the original
// runtime's observable behaviour (sif_hle.cpp InitRpc/BindRpc/CallRpc;
// sce_hle/libkernl.cpp for the rest), routed through the RpcRouter, the
// SifState flag words and an injected IOP-heap allocator:
//
//   sceSifInitRpc      -> SMFLAG |= CMDINIT|BOOTEND (the IOP's handshake reply), 1
//   sceSifInitCmd      -> 0                       sceSifAddCmdHandler -> 0
//   sceSifBindRpc      -> client record filled, router Bind, 0
//   sceSifCallRpc      -> router Call (unbound server = the leg's park
//                         `sif_server_unbound`), client record completed,
//                         end callback queued, completion sema signalled, 0
//   sceSifCheckStatRpc -> 0 (every call completes inside sceSifCallRpc)
//   sceSifRegisterRpc  -> the EE-side server recorded (sid/func/buff), 0
//   sceSifSetRpcQueue  -> 0   sceSifGetNextRequest -> 0   sceSifRpcLoop -> SleepThread
//   sceSifLoadModule / _sceSifLoadModule -> 0 (module id; the IOP modules are
//                         booted by the composition root), _lf_bind -> 1
//   sceSifLoadModuleBuffer(iop_addr, arg_len, args) -> the same loader, told
//                         "buffer@<iop addr>" (+ the args bytes): a module image
//                         the title pushed into IOP RAM itself (an IRX embedded
//                         in the ELF), registered as a new row
//   sceSifInitIopHeap  -> 0   sceSifAllocIopHeap -> allocator(size)
//   sceSifFreeIopHeap / sceSifFreeSysMemory -> allocator free, 0
//   sceSifWriteBackDCache -> 0 (no cache to write back)
//
// Four SIF rows the park owner never held are REGISTERED (new rows, not
// rebinds; libkernl.cpp:428, :496-497, :582): sceSifRebootIop -> 1 (accepted;
// the IOP is never rebooted -- the post-reboot IOPRP300 set is what the module
// table boots), sceSifSyncIop -> 1 (in sync), sceSifInitRebootNotify -> 0,
// sceFsSifCallRpc -> 0.
//
// A client record bound before this router existed (an anchor restored from
// the original runtime, whose handle word is the lossy 0xDEAD0000|sid) has its
// server id recovered by the injected HandleToServerId hook -- the title's
// composition root knows its server ids; the engine does not.
//
// The 8 non-SIF rows KernlSifRpc also owns (SetT2, sbrk, kFindAddress, kCopy,
// InitExecPS2, PatchIsNeeded, iGetTimerSystemTime, SetNextComp) are not SIF
// and stay with their park owner until their own family lands.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;
class RpcRouter;
class SifState;

class SceSif {
public:
    struct IopHeap {
        std::function<uint32_t(uint32_t size)> alloc;  // IOP address, 0 when exhausted
        std::function<void(uint32_t addr)> free;
    };
    struct EeServer {
        uint32_t sid = 0, func = 0, buff = 0;
        bool registered = false;
    };
    // Recovers a server id from the client record's handle word (0 = unknown).
    using HandleToServerId = std::function<uint32_t(uint32_t handle)>;
    // libkernl's sceSifInitRpc statics in the title's ELF (title data; the
    // engine knows no address): the is_initialized word the SDK tests first
    // (non-zero = a repeat call returns 1 without re-seeding), the client-queue
    // block it clears, and the queue/ring words and handler words it seeds.
    struct InitRpcStatics {
        struct Word { uint32_t addr = 0, value = 0; };
        uint32_t init_flag_addr = 0;                      // 0 = no statics bound
        uint32_t queue_clear_addr = 0, queue_clear_size = 0;
        std::vector<Word> words;
    };

    // Binds the collaborators (one composition per process), rebinds the
    // KernlSifRpc rows and registers the four title rows; returns the number
    // of rows rebound (the run.meta `sif_rows=`).
    static int Bind(SdkBindingManifest& m, RpcRouter& router, SifState& sif, IopHeap heap,
                    HandleToServerId handle_to_sid = {});
    static void SetHandleToServerId(HandleToServerId fn);
    static void SetInitRpcStatics(InitRpcStatics statics);
    // The end function of a BLOCKING sceSifCallRpc (mode bit 0 clear) runs
    // before the call returns: libkernl 3.0.1 stores end_func/end_param in the
    // client record in both modes and its RPC-end handler runs the end function
    // before signalling the blocked caller's semaphore. The composition root
    // binds the runner (the queued end callbacks delivered on the caller's
    // context); unbound = the callback waits for the boundary duty.
    using EndCallbackRunner = std::function<void(CpuContext&)>;
    static void SetBlockingEndCallbackRunner(EndCallbackRunner fn);
    static const char* const* RowNames(size_t* count);
    static const EeServer& RegisteredEeServer();
    static uint64_t InitRpcCalls();
    // Service-only startup (M11): the composition root answers each
    // sceSifLoadModule by the native owner of that module's intent -- the
    // path string the title passes (a0), the returned module id (>= 0; a
    // negative result is the title's error ABI, counted by the root). Unbound
    // (the retained arm): 0, the IOP modules were booted by the root.
    using ModuleLoader = std::function<int32_t(const std::string& path)>;
    static void SetModuleLoader(ModuleLoader fn);
    // The heap census: AllocIopHeap / FreeIopHeap calls and the allocations the
    // bound allocator refused (returned 0).
    struct HeapCensus { uint64_t allocs = 0, frees = 0, refused = 0, bytes = 0; };
    static HeapCensus Heap();

    // The row bodies (public so tests can call them with a prepared context).
    static void InitRpc(CpuContext& ctx);
    static void InitCmd(CpuContext& ctx);
    static void BindRpc(CpuContext& ctx);
    static void CallRpc(CpuContext& ctx);
    static void CheckStatRpc(CpuContext& ctx);
    static void RegisterRpc(CpuContext& ctx);
    static void SetRpcQueue(CpuContext& ctx);
    static void GetNextRequest(CpuContext& ctx);
    static void RpcLoop(CpuContext& ctx);
    static void AddCmdHandler(CpuContext& ctx);
    static void LoadModule(CpuContext& ctx);
    static void LoadModuleBuffer(CpuContext& ctx);
    static void LfBind(CpuContext& ctx);
    static void InitIopHeap(CpuContext& ctx);
    static void AllocIopHeap(CpuContext& ctx);
    static void FreeIopHeap(CpuContext& ctx);
    static void WriteBackDCache(CpuContext& ctx);
    static void RebootIop(CpuContext& ctx);
    static void SyncIop(CpuContext& ctx);
    static void InitRebootNotify(CpuContext& ctx);
    static void FsSifCallRpc(CpuContext& ctx);

    // sceSifClientData layout (libkernl 3.0.1; sif_hle.cpp:664-675).
    static constexpr uint32_t kClientPacket = 0x00, kClientNext = 0x04, kClientSema = 0x08, kClientUnused = 0x0C,
                              kClientCommand = 0x10, kClientBuff = 0x14, kClientCbuff = 0x18, kClientEndFunc = 0x1C,
                              kClientEndParam = 0x20, kClientServer = 0x24;
    // The bound-server word the game polls: non-zero = bound (the original's
    // encoding of the server id, sif_hle.cpp:704).
    static uint32_t ServerHandle(uint32_t server_id) { return 0xDEAD0000u | (server_id & 0xFFFFu); }
    static constexpr uint32_t kPacketHandle = 0x00100000u;  // the record's packet word (sif_hle.cpp:708)
};
