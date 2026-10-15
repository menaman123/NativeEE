// gs/hle/ir/draw_ir_verifier.h -- DrawIrVerifier: the DrawIR-equals-register-ops
// proof (invariant 27).
//
// Both forms of a packet are interpreted abstractly over an attribute state with
// known-bits (a field is unknown until the packet writes it, so equality holds
// for EVERY pre-existing GS state) and canonicalised to the sequence of
// GS-observable events: register writes, PRIM writes, flush positions, vertex
// kicks with the attribute state visible at each kick, and the final attribute
// state (trailing writes). Two packets forms are equivalent iff the sequences
// are equal element for element.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/gs/hle/ir/direct_ops.h"
#include "engine/gs/hle/ir/draw_ir_lift.h"

namespace ps2r {
namespace gs {

class DrawIrVerifier {
public:
    struct AttrState {
        bool k_rgba = false;
        uint8_t r = 0, g = 0, b = 0, a = 0;
        bool k_st = false;
        float s = 0, t = 0;
        bool k_q = false;
        float q = 0;
        bool k_uv = false;
        uint16_t u = 0, v = 0;
        bool k_uvw = false;
        bool uvw = false;
        bool k_fogf = false;
        bool fogf = false;

        bool operator==(const AttrState& o) const;
    };

    struct Event {
        enum T { Kick, State, Prim, Flush, SrcId, SrcName, Final } t = Final;
        uint32_t xb = 0, yb = 0, zb = 0;  // Kick: float bits of the kick arguments
        uint8_t f = 0;
        bool draw = false;
        AttrState at;                     // Kick snapshot / Final state
        uint16_t reg = 0;                 // State
        uint64_t value = 0;               // State
        uint64_t prim = 0;                // Prim (11 bits)
        uint64_t id_lo = 0, id_hi = 0;    // SrcId / SrcName

        bool operator==(const Event& o) const;
    };

    static uint32_t FloatBits(float f);
    static std::vector<Event> RunOps(const std::vector<DirectOps::Op>& ops);
    static std::vector<Event> RunIR(const DrawIrLift::Stream& s);
    // 1 = equivalent, 0 = both forms declined the packet, -1 = mismatch.
    static int VerifyKickEquivalence(const uint8_t* p, size_t n);
};

}  // namespace gs
}  // namespace ps2r
