#pragma once
// The whole-span VIF parser: decodes a complete DMA span (whole quadwords) in
// place, executing every command whose payload lies inside the span and
// handing the tail of a payload that crosses the span end to the word latch.
// It declines (returns false, nothing consumed) whenever the unit is mid-
// quadword, has buffered words, or is inside a TTE-quadword feed -- those
// shapes need the word-at-a-time state machine (Vif::ProcessFifo), which is
// the parser's slow path and produces the identical (payload, phase, lookahead)
// triple for every command.
#include <cstddef>
#include <cstdint>

class Vif;

class VifFastSpan {
public:
    // True = the span was consumed (possibly with a tail in the latch); false =
    // the caller feeds the whole span through the word latch.
    static bool Process(Vif& vif, int unit, const uint8_t* data, size_t size);
};
