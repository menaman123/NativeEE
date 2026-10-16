#pragma once
// Generated-code ABI (engine/iop/abi): the two `iop_stubs::` names a TU produced by
// `ps2r_iop_recompiler --namespace <id> --import-call` calls -- every library import is
// `import_call("lib", index, ctx)` (resolved by the bound IopLibraryTable) and every
// computed jump/call target goes through `iop_resolve(addr)` (the bound resolver over
// every hosted module + the registered import stubs). Bodies: iop_abi_surface.cpp.
#include <cstdint>
#include <functional>

#include "../host_app/iop_context.h"

namespace iop_stubs {

void import_call(const char* lib, uint32_t index, IopContext& ctx);
const std::function<void(IopContext&)>* iop_resolve(uint32_t addr);

}  // namespace iop_stubs
