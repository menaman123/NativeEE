#pragma once
// RpcCallbackQueue: EE-side queue of SIF RPC end-callbacks awaiting delivery.
//
// A SIF RPC completion (Phase 4 producer: the IOP dispatcher) may not run the
// guest's end-callback on the producer's thread. The client-data address is
// queued here and the callbacks are DRAINED at the event-queue step of the
// boundary by the run-token holder (a HostGate participant or a guest thread),
// which invokes them in FIFO order. Phase 3 has no producer: the queue is
// empty and Drain returns nothing.
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

class RpcCallbackQueue {
public:
    static RpcCallbackQueue& Instance();

    // Producer (any thread): queue a completed RPC's client-data address.
    void Push(uint32_t client_data_addr);
    // Consumer (the run-token holder at its scheduling point): take everything
    // queued so far, FIFO.
    std::vector<uint32_t> Drain();
    bool Empty();
    // Block (with an absolute steady-clock deadline in ns) until non-empty.
    bool WaitUntilNonEmpty(uint64_t deadline_ns);

private:
    RpcCallbackQueue() = default;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<uint32_t> pending_;
};
