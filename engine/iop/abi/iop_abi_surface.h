// engine/iop/abi/iop_abi_surface.h -- IopAbiSurface: the process-global binding
// behind the generic ABI a recompiled IOP module's TU calls (ps2r_iop_recompiler
// --namespace --import-call; headers under abi/host_app/):
//   iop_memory::read/write<T>   -> the bound IopMemory
//   iop_stubs::import_call      -> the bound IopLibraryTable (a stock module's
//                                  HLE body, another hosted module's export, or
//                                  the unresolved-import hook)
//   iop_stubs::iop_resolve      -> the bound function resolver (every hosted
//                                  module's address -> body map) plus the import
//                                  stub addresses registered from each module's
//                                  kImportStubs (a stub address taken as a
//                                  function pointer resolves to its import)
// One binding per process (the generated code is free functions); the
// composition root binds at construction and Unbind()s at teardown. The engine
// knows no module name, base or server id here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "engine/iop/iop_context.h"

namespace ps2r::iop {

class IopMemory;
class IopLibraryTable;

class IopAbiSurface {
public:
    using GuestFunction = std::function<void(IopContext&)>;
    // Answers a guest code address with its body (nullptr = unknown).
    using Resolver = std::function<const GuestFunction*(uint32_t addr)>;
    // The composition's answer to an import no library serves (default: a
    // stderr line, $v0 = 0). A park is the honest answer for a boot leg.
    using UnresolvedImport = std::function<void(std::string_view lib, uint32_t index, IopContext& ctx)>;

    static void BindMemory(IopMemory* mem) { memory_ = mem; }
    static void BindLibraries(IopLibraryTable* libs) { libs_ = libs; }
    static void BindResolver(Resolver r) { resolver_ = std::move(r); }
    static void SetUnresolvedImport(UnresolvedImport fn) { unresolved_ = std::move(fn); }
    // A module's import stub: the address of its `jr ra / addiu zero,zero,idx` pair.
    static void RegisterImportStub(uint32_t addr, std::string_view lib, uint32_t index);
    static void Unbind();

    static IopMemory& Memory() { return *memory_; }
    static bool Bound() { return memory_ != nullptr; }

    // The ABI bodies (abi/host_app/iop_stubs.h forwards here).
    static void ImportCall(const char* lib, uint32_t index, IopContext& ctx);
    static const GuestFunction* Resolve(uint32_t addr);

    static uint64_t ImportCalls() { return import_calls_; }
    static uint64_t ResolveMisses() { return resolve_misses_; }

private:
    static IopMemory* memory_;
    static IopLibraryTable* libs_;
    static Resolver resolver_;
    static UnresolvedImport unresolved_;
    static std::map<uint32_t, GuestFunction> stub_thunks_;
    static uint64_t import_calls_;
    static uint64_t resolve_misses_;
};

}  // namespace ps2r::iop
