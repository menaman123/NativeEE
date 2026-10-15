#pragma once
// RecompAbi: the binding between the frozen recompiler's output (which calls
// free functions in the `memory::` namespace and reads a few globals) and the
// engine objects that serve them. The composition root binds the FastMem
// instance before `initialize_recompiled_functions`; the forwarding headers
// under recomp_abi/host_app/ route every generated access through Memory().
#include <cstdint>

class FastMem;

class RecompAbi {
public:
    static void BindMemory(FastMem* mem);
    static FastMem& Memory();  // asserts a binding exists
    static bool HasMemory();
};
