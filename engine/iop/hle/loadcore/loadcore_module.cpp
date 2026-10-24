// engine/iop/hle/loadcore/loadcore_module.cpp -- LoadcoreModule bodies.
#include "engine/iop/hle/loadcore/loadcore_module.h"

#include <vector>

#include "engine/iop/iop_library_table.h"

namespace ps2r::iop::hle {

namespace {
std::string TableName(const IopMemory& mem, uint32_t table) {
    std::string name;
    for (uint32_t i = 0; i < 8; ++i) {
        const char c = static_cast<char>(mem.Read<uint8_t>(table + 0x0C + i));
        if (!c) break;
        name.push_back(c);
    }
    return name;
}
}  // namespace

void LoadcoreModule::FlushDcache(IopContext& ctx) { ctx.gpr[2] = 0; }

void LoadcoreModule::RegisterLibraryEntries(IopContext& ctx) {
    const uint32_t table = ctx.gpr[4];
    if (!table || mem_.Read<uint32_t>(table) != kExportMagic) {
        ctx.gpr[2] = static_cast<uint32_t>(-1);
        return;
    }
    const uint32_t version = mem_.Read<uint16_t>(table + 0x08);
    const std::string name = TableName(mem_, table);
    std::vector<uint32_t> entries;
    for (uint32_t i = 0; i < kMaxEntries; ++i) {
        const uint32_t fn = mem_.Read<uint32_t>(table + kHeaderBytes + i * 4);
        if (fn == 0) break;
        entries.push_back(fn);
    }
    if (!libs_.RegisterGuestTable(name, version, entries, name)) {
        ctx.gpr[2] = static_cast<uint32_t>(kLibraryFound);
        return;
    }
    last_registered_ = name;
    registered_++;
    ctx.gpr[2] = 0;
}

void LoadcoreModule::ReleaseLibraryEntries(IopContext& ctx) {
    const uint32_t table = ctx.gpr[4];
    if (!table || mem_.Read<uint32_t>(table) != kExportMagic) {
        ctx.gpr[2] = static_cast<uint32_t>(-1);
        return;
    }
    ctx.gpr[2] = libs_.Release(TableName(mem_, table)) ? 0 : static_cast<uint32_t>(-1);
}

void LoadcoreModule::QueryBootMode(IopContext& ctx) { ctx.gpr[2] = 0; }

}  // namespace ps2r::iop::hle
