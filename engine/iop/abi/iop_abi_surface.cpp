// engine/iop/abi/iop_abi_surface.cpp -- IopAbiSurface + the abi/host_app forwarders.
#include "engine/iop/abi/iop_abi_surface.h"

#include <cstdio>

#include "engine/iop/abi/host_app/iop_stubs.h"
#include "engine/iop/iop_library_table.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {

IopMemory* IopAbiSurface::memory_ = nullptr;
IopLibraryTable* IopAbiSurface::libs_ = nullptr;
IopAbiSurface::Resolver IopAbiSurface::resolver_;
IopAbiSurface::UnresolvedImport IopAbiSurface::unresolved_;
std::map<uint32_t, IopAbiSurface::GuestFunction> IopAbiSurface::stub_thunks_;
uint64_t IopAbiSurface::import_calls_ = 0;
uint64_t IopAbiSurface::resolve_misses_ = 0;

void IopAbiSurface::RegisterImportStub(uint32_t addr, std::string_view lib, uint32_t index) {
    const std::string name(lib);
    stub_thunks_[addr] = [name, index](IopContext& ctx) { ImportCall(name.c_str(), index, ctx); };
}

void IopAbiSurface::Unbind() {
    memory_ = nullptr;
    libs_ = nullptr;
    resolver_ = nullptr;
    unresolved_ = nullptr;
    stub_thunks_.clear();
}

void IopAbiSurface::ImportCall(const char* lib, uint32_t index, IopContext& ctx) {
    import_calls_++;
    const IopLibraryTable::Entry* e = libs_ ? libs_->FindEntry(lib, index) : nullptr;
    if (e) {
        if (e->hle) {
            e->hle(ctx);
            ctx.FixZero();
            return;
        }
        if (const GuestFunction* fn = Resolve(e->guest)) {
            // The export runs as the stub's callee: same registers, its own frame.
            const uint32_t saved_pc = ctx.pc;
            ctx.pc = e->guest;
            (*fn)(ctx);
            ctx.pc = saved_pc;
            ctx.FixZero();
            return;
        }
    }
    if (libs_) libs_->NoteUnresolved(lib, index);
    if (unresolved_) {
        unresolved_(lib, index, ctx);
    } else {
        std::fprintf(stderr, "iop: unresolved import %s:%u (pc=0x%08x ra=0x%08x)\n", lib, index, ctx.pc, ctx.gpr[31]);
        ctx.gpr[2] = 0;
    }
    ctx.FixZero();
}

const IopAbiSurface::GuestFunction* IopAbiSurface::Resolve(uint32_t addr) {
    if (resolver_) {
        if (const GuestFunction* fn = resolver_(addr)) return fn;
    }
    auto it = stub_thunks_.find(addr);
    if (it != stub_thunks_.end()) return &it->second;
    resolve_misses_++;
    return nullptr;
}

}  // namespace ps2r::iop

namespace iop_stubs {

void import_call(const char* lib, uint32_t index, IopContext& ctx) {
    ps2r::iop::IopAbiSurface::ImportCall(lib, index, ctx);
}

const std::function<void(IopContext&)>* iop_resolve(uint32_t addr) {
    return ps2r::iop::IopAbiSurface::Resolve(addr);
}

}  // namespace iop_stubs
