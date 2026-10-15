#pragma once
// EeRpcServer: the EE side of an IOP -> EE SIF RPC -- boundary slot 6
// (BoundarySequencer::Step::EeStreamRpc). An IOP worker's sceSifCallRpc to the
// server the EE registered with sceSifRegisterRpc (SceSif::RegisteredEeServer:
// sid/func/buff) becomes a memcpy pair plus a wakeup, never CPU emulation:
//
//   IOP worker (kernel coroutine): RequestAndWait -- the request bytes are
//     copied IOP -> EE into the server's buffer (what the SIF DMA moves ahead of
//     the server dispatch), the request is posted, and the worker parks in
//     Wait::Rpc (SifRpcBridge::WaitEeReplyUs) for kReplyDeadlineUs. A deadline
//     that expires withdraws the request: the caller synthesises its reply and
//     a late reply is dropped, never banked.
//   EE (slot 6, or the bridge's service poll while the EE itself is inside a
//     RunOnRpcThread): Service -- the server function runs as a guest call on
//     the boundary thread's context, `void* func(u32 fno, void* buff, int size)`
//     (a0/a1/a2, ra = the return sentinel), with EVERY GPR and the pc saved and
//     restored around it (the original's borrowed-context form); v0 = the
//     reply pointer, whose recv_size bytes are copied EE -> IOP into the
//     worker's recv buffer (the end-of-RPC SIF DMA); then NotifyEeReply wakes
//     exactly the Wait::Rpc parkee.
//
// The server id an IOP module calls is the module's own knowledge (a title
// constant lives with the module, not here): RequestAndWait matches it against
// the registered record and reports `false` when no such EE server exists yet.
#include <cstdint>
#include <functional>
#include <mutex>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/sif/hle/sce_sif.h"

namespace ps2r::iop {
class IopMemory;
class SifRpcBridge;
}

class EeRpcServer {
public:
    using GuestCall = std::function<void(uint32_t addr, CpuContext& ctx)>;
    using EeRead = std::function<uint8_t(uint32_t addr)>;
    using EeWrite = std::function<void(uint32_t addr, uint8_t value)>;

    static constexpr uint64_t kReplyDeadlineUs = 120000;     // the worker's park deadline
    static constexpr uint32_t kReturnSentinel = 0x80001000;  // ra of the guest call
    static constexpr uint32_t kMaxTransfer = 0x100;          // request/reply bytes per call (server buffer size)
    static constexpr uint32_t kEeRamMask = 0x01FFFFFF;       // SIF DMA addresses physical EE RAM

    // `record` is the EE server registration the sceSifRegisterRpc row keeps
    // (SceSif::RegisteredEeServer()); `bridge` is the IOP kernel's RPC bridge.
    EeRpcServer(const SceSif::EeServer& record, ps2r::iop::IopMemory& iop_mem, ps2r::iop::SifRpcBridge& bridge,
                EeRead ee_read, EeWrite ee_write, GuestCall guest_call);
    EeRpcServer(const EeRpcServer&) = delete;
    EeRpcServer& operator=(const EeRpcServer&) = delete;

    // IOP worker side (on a kernel coroutine). True when the reply is in place
    // at recv_iop; false when no EE server `sid` is registered or the deadline
    // expired (the request withdrawn) -- the caller synthesises.
    bool RequestAndWait(uint32_t sid, uint32_t fno, uint32_t send_iop, uint32_t send_size, uint32_t recv_iop,
                        uint32_t recv_size);

    // EE side: services the posted request, if any. True when one was served.
    bool Service(CpuContext& ctx);

    bool Pending() const;
    uint64_t Served() const { return served_; }
    uint64_t Timeouts() const { return timeouts_; }
    uint64_t Rejected() const { return rejected_; }   // no EE server registered for the sid

private:
    struct Request {
        uint32_t fno = 0, send_size = 0, recv_iop = 0, recv_size = 0;
    };

    // REALTIME hazard (open for 4F'): `record_` aliases SceSif's registration,
    // which the EE thread's sceSifRegisterRpc writes field by field with no
    // lock; a RequestAndWait on the IOP host thread racing that registration
    // can read a torn {sid, func, buff, registered}. Under FRAME both run on
    // one thread. 4F' publishes the record atomically (copy under a lock or a
    // seqlock) before REALTIME becomes a gate lane.
    const SceSif::EeServer& record_;
    ps2r::iop::IopMemory& iop_mem_;
    ps2r::iop::SifRpcBridge& bridge_;
    EeRead ee_read_;
    EeWrite ee_write_;
    GuestCall guest_call_;
    mutable std::mutex m_;   // the request is posted by the worker's thread (REALTIME: the IOP host thread)
    bool pending_ = false;
    bool done_ = false;
    Request req_;
    uint64_t served_ = 0;
    uint64_t timeouts_ = 0;
    uint64_t rejected_ = 0;
};
