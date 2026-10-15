#pragma once
// Static classification of a decoded VU program for the cached tier's
// superblock pass: straight-line run lengths, the static stall classes, the
// fold classes over read/write chains and the remaining-span counts. Pure
// functions over the decoded slot array; no execution state.
//
// The classifier assumes the FMAC write-back latency and the IALU load
// latency both equal 4 cycles (the stall model's constants); the fold pass
// therefore looks back 3 chain slots for a read-after-write dependency.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/vu/vu_decoder.h"

class SpanClassifier {
public:
    static constexpr int kFmacWbLatencyShared = 4;
    static constexpr int kIaluLwLatencyShared = 4;

    // Backward pass: touch_hw and run_len (1 at a terminal bundle: E-bit,
    // NOP pair, generic/branch lower, generic upper; else next.run_len + 1,
    // capped at 255).
    static void ComputeRunLenTouchHw(VuDecodedSlot* slots, size_t n);
    // Static stall class 1..14 from (lower present, IALU write, VF reads,
    // VF write count); 0 = dynamic (branch reads, FDIV/EFU ops, 2 writes
    // without a lower, IALU write with 2 writes).
    static void ComputeStallClasses(VuDecodedSlot* slots, size_t n);
    // Forward walk over run chains: fold_class 15 (no VF write) / 16 (one or
    // two VF writes) for a run bundle with a static class, no pipe or IALU
    // side effect and no read of a VF written by the 3 preceding chain slots.
    static void ComputeFoldClasses(VuDecodedSlot* slots, size_t n);
    // span_rem[i] = number of consecutive foldable slots starting at i (cap 255).
    static void ComputeSpanRem(const VuDecodedSlot* slots, size_t n, std::vector<uint8_t>& span_rem);
};
