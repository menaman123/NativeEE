// engine/iop/hle/modload/modload_module.cpp -- ModloadModule over the module table.
#include "engine/iop/hle/modload/modload_module.h"

#include <string>

#include "engine/iop/iop_module_table.h"

namespace ps2r::iop::hle {

void ModloadModule::ReferModuleStatus(IopContext& ctx) {
    queries_++;
    const int id = static_cast<int32_t>(ctx.gpr[4]);
    const uint32_t out = ctx.gpr[5];
    const auto& rows = table_.Modules();
    if (id <= 0 || static_cast<size_t>(id) > rows.size() || !out) {
        ctx.gpr[2] = static_cast<uint32_t>(kUnknownModule);
        return;
    }
    const IopModuleTable::ModuleEntry& m = rows[static_cast<size_t>(id) - 1];
    for (uint32_t i = 0; i < kStatusNameBytes; ++i)
        mem_.Write<uint8_t>(out + i, i < m.name.size() ? static_cast<uint8_t>(m.name[i]) : 0);
    mem_.Write<uint16_t>(out + 0x38, static_cast<uint16_t>(m.version));
    mem_.Write<uint16_t>(out + 0x3A, 0);   // flags
    mem_.Write<uint32_t>(out + 0x3C, static_cast<uint32_t>(id));
    mem_.Write<uint32_t>(out + 0x40, m.entry);
    mem_.Write<uint32_t>(out + 0x44, m.gp);
    mem_.Write<uint32_t>(out + 0x48, m.text_addr);
    mem_.Write<uint32_t>(out + 0x4C, m.text_size);
    mem_.Write<uint32_t>(out + 0x50, m.data_size);
    mem_.Write<uint32_t>(out + 0x54, m.bss_size);
    mem_.Write<uint32_t>(out + 0x58, 0);
    mem_.Write<uint32_t>(out + 0x5C, 0);
    ctx.gpr[2] = 0;
}

void ModloadModule::GetModuleIdListByName(IopContext& ctx) {
    queries_++;
    const uint32_t name_addr = ctx.gpr[4];
    const uint32_t ids = ctx.gpr[5];
    const int max = static_cast<int32_t>(ctx.gpr[6]);
    const uint32_t count_out = ctx.gpr[7];
    std::string name;
    for (uint32_t i = 0; i < kStatusNameBytes; ++i) {
        const char c = static_cast<char>(mem_.Read<uint8_t>(name_addr + i));
        if (!c) break;
        name.push_back(c);
    }
    int total = 0, written = 0;
    const auto& rows = table_.Modules();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].name != name) continue;
        ++total;
        if (ids && written < max) mem_.Write<uint32_t>(ids + static_cast<uint32_t>(written++) * 4, static_cast<uint32_t>(i + 1));
    }
    if (count_out) mem_.Write<uint32_t>(count_out, static_cast<uint32_t>(total));
    ctx.gpr[2] = static_cast<uint32_t>(written);
}

}  // namespace ps2r::iop::hle
