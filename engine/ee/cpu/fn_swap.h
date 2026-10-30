#pragma once
// FnSwap: hot swap of one generated function body in the LIVE process (the recomp-agent program's
// verification tier 2). A candidate body -- a function `void (CpuContext&, uint32_t entry)` found by
// dlsym in a shared library, or a plain function pointer (tests; the generated body itself as the
// identity case) -- replaces the dispatched body of a generated function through the generated install
// primitive (update_function_impl, via DispatchTable::Install); Remove restores the body that was
// dispatched before the first swap of that address, and the id's raw-fast-path flag with it.
//
// What the installed callable does, so the swap is invisible to everything but the body's own effects:
//   * the init-time dispatch wrapper's duties (recompiler.cpp "Wrap all registered functions with
//     dispatch-level tracing"): read ra BEFORE the body runs, push/pop the shadow-stack frame
//     (ShadowCallStack = EeCallFrameGuard's saturation rule) -- so cpu.stack, the savestate chain
//     harvest and the FnHooks caller attribution see the same frames. (The legacy FTRACE log line is
//     not reproduced: a diagnostic, not game behaviour.)
//   * the DispatchTable proof is INHERITED from the previous body (DispatchTable::TraceBody): the
//     candidate is declared to implement the generated entry switch (its interior continuation entries
//     and its restartable wait, continuations.h). True for a copy of the generated body and for a tier-1
//     candidate emitted in the generated shape; a candidate that only implements entry 0 must be
//     installed with inherit_proof=false (then it is an opaque body: a savestate cannot be captured
//     while a call of it is live, and a relaunch cannot resume inside it).
//   * FnHooks probes keep working: a swapped id is "hooked" for the dispatch, which takes the classic
//     chain (recomp_hooked_dispatch) where the probe bracket lives.
//
// Rules the caller (the control plane) enforces: install/remove only while the guest is quiescent (a
// frame boundary) and never for a function on the calling thread's shadow stack (replacing the callable
// of a call in flight destroys the callable that call is executing). Other guest threads parked inside
// the swapped function are not detectable from here: swap leaf-ish functions, or functions you know are
// not on any parked chain at the boundary. Libraries are NEVER dlclosed (a parked thread may return
// into the body): a re-swap from the same path gets the already-loaded image -- use a fresh file name
// per candidate build.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"

class FnSwap {
public:
    using Body = void (*)(CpuContext&, uint32_t);
    static constexpr const char* kDefaultSymbol = "recomp_candidate";
    // The candidate format's second export: `extern "C" const uint32_t recomp_candidate_addr`, the function
    // the library was built (and tier-1 verified) for. Install refuses a library whose value is another address.
    static constexpr const char* kAddrSymbol = "recomp_candidate_addr";

    struct Info {
        uint32_t addr = 0;
        uint32_t id = 0;
        std::string library;   // empty: a plain function pointer (tests / identity)
        std::string symbol;
        bool inherit_proof = true;
        uint64_t calls = 0;    // dispatches of the installed callable since the swap
    };

    // dlopen(library) + dlsym(symbol), then InstallBody. `previous` (optional) receives "generated"
    // (the address was not swapped) or "swapped" (a swap was replaced). False + *err: nothing changed.
    static bool Install(uint32_t addr, const std::string& library, const std::string& symbol, bool inherit_proof,
                        std::string* previous, std::string* err);
    // Install a body by pointer (no library). Same contract.
    static bool InstallBody(uint32_t addr, Body body, bool inherit_proof, std::string* previous, std::string* err,
                            const std::string& library = std::string(), const std::string& symbol = std::string());
    // Restore the body dispatched before the first swap of `addr` (and its raw-fast-path flag).
    static bool Remove(uint32_t addr, std::string* err);
    // Every active swap.
    static std::vector<Info> List();
    static bool IsSwapped(uint32_t addr);
    // The generated body of `addr` (g_recomp_fn_raw[id]); nullptr for a non-entry or an HLE-skipped body.
    static Body GeneratedBody(uint32_t addr);
    // True when `addr` is a frame of the calling thread's shadow stack (a call of it is in flight here).
    static bool OnCallingStack(uint32_t addr);
};
