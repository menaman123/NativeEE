#pragma once
// The clock the MTVU backpressure and runahead valve read: a steady clock in
// nanoseconds behind one virtual so tests substitute fake time. The default
// instance is the process steady clock (the deadline domain of ps2r::Park).
#include <cstdint>

class MtvuClock {
public:
    virtual ~MtvuClock() = default;
    virtual uint64_t NowNs() const;

    // The shared steady-clock instance.
    static MtvuClock& Steady();
};
