// engine/iop/hle/modload/modload_module.h -- ModloadModule: the IOP kernel's
// modload library as a hosted module sees it, answered from the module table:
// ReferModuleStatus (idx 17) fills a ModuleStatus {name[56], version:u16,
// flags:u16, id, entry, gp, text_addr, text_size, data_size, bss_size,
// reserve[2]} for a module id; GetModuleIdListByName (18) lists the ids of the
// modules with that name (ids are the table row + 1). Modules the engine serves
// natively are rows too, so a hosted module probing for a library module finds
// it (with the image facts the composition recorded for the row).
#pragma once

#include <cstdint>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {
class IopModuleTable;
}

namespace ps2r::iop::hle {

class ModloadModule {
public:
    static constexpr uint32_t kStatusNameBytes = 56;
    static constexpr int kUnknownModule = -202;   // KE_UNKNOWN_MODULE

    ModloadModule(IopModuleTable& table, IopMemory& mem) : table_(table), mem_(mem) {}

    void ReferModuleStatus(IopContext& ctx);        // int (id, ModuleStatus*) -> 0 / KE_UNKNOWN_MODULE
    void GetModuleIdListByName(IopContext& ctx);    // int (name, int* ids, int max, int* count) -> count

    uint32_t Queries() const { return queries_; }

private:
    IopModuleTable& table_;
    IopMemory& mem_;
    uint32_t queries_ = 0;
};

}  // namespace ps2r::iop::hle
