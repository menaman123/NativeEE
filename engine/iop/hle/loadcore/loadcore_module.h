// engine/iop/hle/loadcore/loadcore_module.h -- LoadcoreModule: the IOP kernel's
// loadcore library as a hosted module sees it: RegisterLibraryEntries (idx 6)
// reads the module's 0x41C00000 export table out of IOP RAM ({magic, next,
// version:u16, mode:u16, name[8], entries... until 0}) and records it in the
// IopLibraryTable -- the registry every other module's imports of that
// library resolve against (a cross-module import is a table lookup, never a
// patched stub); ReleaseLibraryEntries (7) drops it; FlushDcache (5) is a
// no-op on a host with no IOP cache. Nothing here names a library.
#pragma once

#include <cstdint>
#include <string>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {
class IopLibraryTable;
}

namespace ps2r::iop::hle {

class LoadcoreModule {
public:
    static constexpr uint32_t kExportMagic = 0x41C00000u;
    static constexpr uint32_t kHeaderBytes = 0x14;
    static constexpr uint32_t kMaxEntries = 512;
    static constexpr int kLibraryFound = -213;   // KE_LIBRARY_FOUND

    LoadcoreModule(IopMemory& mem, IopLibraryTable& libs) : mem_(mem), libs_(libs) {}

    void FlushDcache(IopContext& ctx);               // () -> void
    void RegisterLibraryEntries(IopContext& ctx);    // int (table) -> 0, KE_LIBRARY_FOUND, or -1 (bad table)
    void ReleaseLibraryEntries(IopContext& ctx);     // int (table) -> 0 / -1
    void QueryBootMode(IopContext& ctx);             // u32* (id) -> 0: no boot-mode record of that id (idx 12)

    const std::string& LastRegistered() const { return last_registered_; }
    uint32_t Registered() const { return registered_; }

private:
    IopMemory& mem_;
    IopLibraryTable& libs_;
    std::string last_registered_;
    uint32_t registered_ = 0;
};

}  // namespace ps2r::iop::hle
