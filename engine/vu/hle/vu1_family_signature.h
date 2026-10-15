#pragma once
// The constants that identify one title's VU1 program family: the first byte
// of the per-material variable region, the dispatcher's first instruction
// word, the dispatch chain's E-bit terminator stubs and the TPC a program
// exits at when the runtime installs no registers. The engine reads them
// through this record only; the values are title data (games/).
#include <cstdint>

struct Vu1FamilySignature {
    uint32_t variant_base;     // first byte of the per-material variable region
    uint32_t dispatcher_op0;   // the family dispatcher's first lower op word
    uint16_t exit_stubs[2];    // the dispatch chain's E-bit terminators
    uint32_t family_exit_tpc;  // TPC after a run whose program reproduces no registers
};
