#pragma once
// FrameRendezvous: the lockstep EE <-> present barrier. The EE issues a
// TICKET for every frame it declares complete (the VSync it pushes) and parks
// until the presenter has PROCESSED that ticket; the presenter acks at every
// exit of its VSync handling (coalesce-skip, hold, genuine present), so every
// pushed frame is matched by construction and no pop accounting can leak.
// The barrier is process-local and never part of saved state. WaitFor has NO
// wall valve: it releases only on the ack or on shutdown (a wedge is reported
// every kStallReportSeconds and abandoned after kAbandonSeconds so a defect
// can never freeze the process silently; a correct run reaches neither).
//
// Phase 3 form: no presenter exists, so a ticket is acked the moment it is
// issued (immediate mode, the default). Phase 6 registers its presenter with
// ArmForPresenter(); nothing else in the boundary changes.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

class FrameRendezvous {
public:
    static constexpr unsigned kStallReportSeconds = 5;
    static constexpr unsigned kAbandonSeconds = 60;

    // A presenter will ack tickets (Phase 6); until then tickets self-ack.
    void ArmForPresenter() { immediate_ack_ = false; }
    bool ImmediateAck() const { return immediate_ack_; }

    // EE side: the next ticket (monotonic from 1; 0 = "no ticket").
    uint32_t Issue();
    // Presenter side: the frame carrying `ticket` has been fully processed.
    void Ack(uint32_t ticket);
    // EE side: block until Acked() >= ticket or `running` turns false.
    bool WaitFor(uint32_t ticket, const std::atomic<bool>& running);
    // Shutdown: wake every waiter so it re-evaluates `running` at once.
    void Wake();

    uint32_t Issued() const { return issued_.load(std::memory_order_relaxed); }
    uint32_t Acked() const { return acked_.load(std::memory_order_acquire); }
    uint64_t Waits() const { return waits_.load(std::memory_order_relaxed); }
    uint64_t Stalls() const { return stalls_.load(std::memory_order_relaxed); }
    uint64_t Abandoned() const { return abandoned_.load(std::memory_order_relaxed); }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    bool immediate_ack_ = true;
    std::atomic<uint32_t> issued_{0};
    std::atomic<uint32_t> acked_{0};
    std::atomic<uint64_t> waits_{0};
    std::atomic<uint64_t> stalls_{0};
    std::atomic<uint64_t> abandoned_{0};
};
