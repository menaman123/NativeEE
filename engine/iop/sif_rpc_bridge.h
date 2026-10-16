// engine/iop/sif_rpc_bridge.h -- SifRpcBridge: EE<->IOP RPC as a memcpy plus a
// wakeup, never CPU emulation (PHASE4_PLAN 3.1 lines 5-6).
//
//   EE -> IOP: the EE-side face enqueues the dispatch body (send-buffer copy
//   into the module's server buffer -> handler -> result copy-back) and wakes
//   the module's OWN RPC-dispatch kernel thread -- the coroutine parked in
//   RpcServiceLoop (sceSifRpcLoop). Under REALTIME the EE then waits on a
//   condvar for the completion ticket while the IopHostThread runs the body;
//   under FRAME the EE thread itself drives IopKernel::Dispatch() (with forced
//   virtual-clock advances) until the ticket completes. One queue PER
//   RPC-dispatch thread (sceSifSetRpcQueue binds a queue to a thread; a module
//   may serve several server ids from several threads): RunOnRpcThread(body,
//   tid) targets that thread's queue, RpcServiceLoop drains the running
//   thread's. The tid-less form uses the table's default RPC tid.
//
//   IOP -> EE: a guest worker marshalling to the EE's RPC server parks in
//   Wait::Rpc with a deadline (WaitEeReplyUs); the EE services the request at
//   its boundary slot (Step::EeStreamRpc) -- reply bytes copied back, then
//   NotifyEeReply -- which wakes exactly a Wait::Rpc parkee. A reply that
//   arrives after the deadline expired is DROPPED, never banked.
//
//   The round trip's cost on the IOP's clock (FRAME only): a SIF RPC is not
//   free on the console -- the send DMA, the module's handler and the reply
//   DMA take the IOP's time (a title's module round trip, measured on the
//   reference console, is the composition root's data). SetServiceUs(S)
//   makes every completed round trip advance the IOP's virtual clock by S
//   after the body ran (the reply's instant); S = 0 (the default) charges
//   nothing. The value is the title's data, bound by the composition root;
//   the engine knows no rate.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>

namespace ps2r::iop {

class IopKernel;

class SifRpcBridge {
public:
    explicit SifRpcBridge(IopKernel& kernel) : kernel_(kernel) {}
    SifRpcBridge(const SifRpcBridge&) = delete;
    SifRpcBridge& operator=(const SifRpcBridge&) = delete;

    // The module's RPC-dispatch thread (the tid parked in RpcServiceLoop).
    void SetRpcTid(int tid) { rpc_tid_.store(tid, std::memory_order_release); }
    int  RpcTid() const { return rpc_tid_.load(std::memory_order_acquire); }

    // Poked when the EE enqueues work (REALTIME: the IopHostThread's Notify).
    void SetHostWake(std::function<void()> wake) { host_wake_ = std::move(wake); }
    // FRAME only: the EE thread drives Dispatch() inside RunOnRpcThread.
    void SetFrameDrive(bool on) { frame_drive_ = on; }
    bool FrameDrive() const { return frame_drive_; }
    // EE-side marshal service, run by the waiting EE thread between completion
    // checks: services a worker parked in Wait::Rpc while the EE itself is
    // inside RunOnRpcThread (it cannot reach its boundary slot from there).
    void SetEeServicePoll(std::function<void()> poll) { ee_service_poll_ = std::move(poll); }
    // FRAME only: the IOP time one EE->IOP round trip costs (0 = none).
    void SetServiceUs(uint64_t us) { service_us_ = us; }
    uint64_t ServiceUs() const { return service_us_; }
    // Total IOP time charged to round trips so far (the census).
    uint64_t ChargedUs() const { return charged_us_.load(std::memory_order_acquire); }

    // EE side: enqueue `body` for the RPC thread, wake it, wait for completion.
    // False on a stall (bounded; the EE proceeds regardless) or shutdown.
    bool RunOnRpcThread(std::function<void()> body);
    // The same for the RPC thread `tid` (its own queue).
    bool RunOnRpcThread(std::function<void()> body, int tid);

    // Kernel side: the sceSifRpcLoop body of the running RPC thread (drains that thread's queue).
    void RpcServiceLoop();

    // Kernel side: park the calling worker until the EE services its marshal
    // or the timeout expires. True when woken by the EE.
    bool WaitEeReplyUs(uint64_t timeout_us);
    // EE side: complete the marshal -- wakes only a Wait::Rpc parkee.
    void NotifyEeReply();
    // The tid currently parked in WaitEeReplyUs (0 = none).
    int  EeWaitTid() const { return ee_wait_tid_.load(std::memory_order_acquire); }

    uint64_t Submitted() const;
    uint64_t Completed() const;
    uint64_t Stalls() const { return stalls_.load(std::memory_order_acquire); }

private:
    bool DriveUntil(const std::function<bool()>& pred);

    struct Queue {
        std::deque<std::function<void()>> bodies;
        uint64_t submitted = 0;
        uint64_t completed = 0;
    };
    Queue& QueueFor(int tid);   // under m_

    IopKernel& kernel_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::map<int, Queue> queues_;   // keyed by RPC-dispatch tid
    uint64_t submitted_ = 0;
    uint64_t completed_ = 0;
    std::atomic<int> rpc_tid_{0};
    std::atomic<int> ee_wait_tid_{0};
    std::atomic<uint64_t> stalls_{0};
    std::function<void()> host_wake_;
    std::function<void()> ee_service_poll_;
    bool frame_drive_ = false;
    uint64_t service_us_ = 0;
    std::atomic<uint64_t> charged_us_{0};
};

}  // namespace ps2r::iop
