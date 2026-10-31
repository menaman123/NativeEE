#pragma once
// Generated-code ABI (engine/iop/abi): the per-function SEAM a TU produced by
// `ps2r_iop_recompiler --namespace <id> --import-call --seam` goes through at EVERY function
// entry. The tool emits each guest function as a body (`iop_body_<addr>`) plus a wrapper
// (`iop_func_<addr>`) that calls `iop_seam::Call(slot, body, ctx)`: when a native host body is
// installed in the function's slot it runs INSTEAD of the recompiled body (same IopContext, same
// IOP RAM, same stack: the module's own state), otherwise the recompiled body runs. Every way a
// guest function is reached -- a JAL site, a JR/JALR through iop_resolve, a thread entry, an RPC
// handler, a hard-timer or DMA callback -- goes through the wrapper, so one slot per function is
// the whole hot-swap surface: a module is ported one function at a time with the recompiled
// body as the fallback and the oracle.
//
// The slot table is a plain array in address order (`iop_<id>::iop_fn_slots[kFunctionCount]`);
// the two counters are the census (`native_iop=<module>:<native fns>/<total>:<native calls>/
// <recompiled calls>`). `trace`, when bound, sees every entry and exit (the NATIVEEE_IOP_TRACE
// call trace: args a0-a3 at entry, v0/v1 at exit). Engine-pure: no module name or address here.
#include <cstdint>

#include "../host_app/iop_context.h"

namespace iop_seam {

// A native body: the IopContext the recompiled body would have run on (a0-a3 in gpr[4..7],
// $sp/$gp/$ra as the caller left them; the result goes to gpr[2]/gpr[3]) and the installer's
// own pointer (the native module object).
using NativeFn = void (*)(IopContext& ctx, void* self);
// A native body without an object: the plain `void(IopContext&)` form (installed through InstallPlain).
using PlainFn = void (*)(IopContext& ctx);

struct IopFnSlot {
    uint32_t addr;                  // the function's entry address (the module's IOP address)
    uint32_t end;                   // one past its last instruction
    NativeFn native;                // installed native body, or nullptr = the recompiled body runs
    void* self;                     // handed to `native`
    uint64_t native_calls;          // calls answered by `native`
    uint64_t recompiled_calls;      // calls answered by the recompiled body
    PlainFn plain;                  // the plain body InstallPlain set (native = PlainThunk, self = the slot)
};

inline void PlainThunk(IopContext& ctx, void* self) { static_cast<IopFnSlot*>(self)->plain(ctx); }

// Installs / clears a native body in a slot (either form).
inline void Install(IopFnSlot& s, NativeFn fn, void* self) {
    s.native = fn;
    s.self = self;
    s.plain = nullptr;
}
inline void InstallPlain(IopFnSlot& s, PlainFn fn) {
    s.plain = fn;
    s.native = fn ? &PlainThunk : nullptr;
    s.self = fn ? &s : nullptr;
}
inline void Uninstall(IopFnSlot& s) {
    s.native = nullptr;
    s.self = nullptr;
    s.plain = nullptr;
}

// The call trace hook: (slot, native?, entry?, ctx). Bound by the composition; nullptr = off.
using TraceFn = void (*)(const IopFnSlot& slot, bool native, bool entry, IopContext& ctx);
extern TraceFn trace;
// The import trace hook (every `import_call("lib", index, ctx)`: entry with a0-a3, exit with v0), the
// same binding; nullptr = off. The two hooks together are the call stream a port is compared on.
using ImportTraceFn = void (*)(const char* lib, uint32_t index, bool entry, IopContext& ctx);
extern ImportTraceFn import_trace;

inline void Call(IopFnSlot& s, void (*body)(IopContext&), IopContext& ctx) {
    if (s.native) {
        ++s.native_calls;
        if (trace) {
            trace(s, true, true, ctx);
            s.native(ctx, s.self);
            trace(s, true, false, ctx);
        } else {
            s.native(ctx, s.self);
        }
        return;
    }
    ++s.recompiled_calls;
    if (trace) {
        trace(s, false, true, ctx);
        body(ctx);
        trace(s, false, false, ctx);
    } else {
        body(ctx);
    }
}

// The slot of the function at `addr` in a table sorted by address (binary search), or nullptr.
inline IopFnSlot* Find(IopFnSlot* slots, uint32_t count, uint32_t addr) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (slots[mid].addr == addr) return &slots[mid];
        if (slots[mid].addr < addr) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

}  // namespace iop_seam
