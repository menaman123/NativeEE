#pragma once
// Generated-code ABI (engine/iop/abi): the per-function seam a TU produced by
// `ps2r_iop_recompiler --override-table` carries -- one IopFnSlot per recompiled function
// (address order). A native body (IopNativeFn) runs on the same IopContext the recompiled
// body would (args in $a0..$a3 + the stack, result in $v0) over the same IOP RAM, so the two
// are interchangeable at any call; the recompiled body stays linked as the fallback and the
// oracle. The counters are the census (`native_iop=<module>:<native>/<recompiled>`).
#include <cstdint>

#include "../host_app/iop_context.h"

using IopNativeFn = void (*)(IopContext&);

struct IopFnSlot {
    uint32_t addr;              // the function's (relocated) address
    uint32_t size;              // its byte length
    IopNativeFn native;         // the installed host body, or nullptr = the recompiled body
    uint64_t native_calls;
    uint64_t recompiled_calls;
};
