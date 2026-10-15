#pragma once
// The frame-boundary consumer: called by the arbiter's drain owner exactly once
// per boundary slot, after every packet of frame N and before any packet of
// frame N+1. The composition root implements it (the VSync render job push);
// null in the arbiter means boundaries are counted but nothing is notified.
//
// Cross-thread contract: runs on whichever thread owns the drain (the EE, or
// the drain thread while it runs); `enqueue_ns` is the caller's opaque
// timestamp from openBoundary.
#include <cstdint>

class GifBoundaryHook {
public:
    virtual ~GifBoundaryHook() = default;
    virtual void OnBoundary(uint32_t seq, uint64_t enqueue_ns) = 0;
};
