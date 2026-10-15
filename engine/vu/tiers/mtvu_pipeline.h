#pragma once
// What the backpressure needs from the MTVU pipeline (the worker + the
// arbiter ledger), behind one interface so the park loop is proven with a
// fake pipeline and fake time. The worker implements it; every method is
// called on the producer (EE) thread.
#include <cstdint>

class MtvuPipeline {
public:
    virtual ~MtvuPipeline() = default;
    // The worker thread is up.
    virtual bool Running() const = 0;
    // The caller may park: not the worker thread and not inside an eager drain.
    virtual bool MayPark() const = 0;
    // Bytes queued for the worker plus payload bytes the arbiter still holds.
    virtual uint64_t PipelineBytes() const = 0;
    // No segment in flight (acquire: the worker's writes are visible).
    virtual bool WorkerIdle() const = 0;
    // Bumped after every marker seal -- the wake hint the wait re-checks.
    virtual uint64_t SealCount() const = 0;
    // Run one guarded arbiter drain on this thread (a no-op when another
    // drain is in progress). Under deferred boundaries the parked producer is
    // the only boundary deliverer, so the park loop drains every iteration.
    virtual void EagerDrain() = 0;
    // Park until the worker is idle, a seal newer than `seen_seal` lands, or
    // `timeout_ns` passes.
    virtual void WaitIdleOrSeal(uint64_t timeout_ns, uint64_t seen_seal) = 0;
    // Park for `timeout_ns`; frame progress comes from the present thread,
    // which this pipeline is not woken by.
    virtual void WaitFrameProgress(uint64_t timeout_ns) = 0;
    // The producer is parked (the worker takes the locked wake path only then).
    virtual void EnterSyncWait() = 0;
    virtual void LeaveSyncWait() = 0;
};
