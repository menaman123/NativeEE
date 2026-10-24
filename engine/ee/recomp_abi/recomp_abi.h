#pragma once
// RecompAbi: the binding between the frozen recompiler's output (which calls
// free functions in the `memory::` namespace and reads a few globals) and the
// engine objects that serve them. The composition root binds the FastMem
// instance before `initialize_recompiled_functions`; the forwarding headers
// under recomp_abi/host_app/ route every generated access through Memory().
// Memory() is inline over the bound pointer (eecode round 4, E5): it sits on
// every generated load and store, and as an out-of-line call it was 4 % of the
// EE logic thread's CPU in Paris. The pointer is the binding's single owner;
// the Debug assert keeps "bind before generated code" a construction finding.
#include <cassert>
#include <cstdint>

class FastMem;

namespace ps2r_recomp_abi {
extern FastMem* g_bound_memory;   // written by RecompAbi::BindMemory only
}

class RecompAbi {
public:
    static void BindMemory(FastMem* mem);
    // Asserts a binding exists (Debug; the comma keeps it one statement for
    // the one-class lint, and the generated code's one load in Release).
    static FastMem& Memory() {
        return *(assert(ps2r_recomp_abi::g_bound_memory && "RecompAbi::BindMemory must run before generated code"), ps2r_recomp_abi::g_bound_memory);
    }
    static bool HasMemory() { return ps2r_recomp_abi::g_bound_memory != nullptr; }
};
