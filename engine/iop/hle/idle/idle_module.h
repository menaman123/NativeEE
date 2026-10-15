// engine/iop/hle/idle/idle_module.h -- IdleModule: an IOP library the title
// loads but whose entries the recompiled module never imports (STREAM.IRX's
// 74-name census names none of sio2man/padman/mcman/mcserv/sdrdrv/cdvdstm).
// Its whole observable behaviour is the RegisterLibraryEntries row in the module
// table: name + version, no RPC thread. The EE-side RPC servers those libraries
// host (pad, memory card, SDR, CD streaming) are the Phase 4C/4D/4E bridges.
#pragma once

#include <cstdint>
#include <string>

#include "engine/iop/iop_module_table.h"

namespace ps2r::iop::hle {

class IdleModule {
public:
    IdleModule(std::string name, uint32_t version) : name_(std::move(name)), version_(version) {}

    // Emits the module-table row; returns its index.
    int Register(IopModuleTable& table) const { return table.RegisterModule(name_, version_, 0); }

    const std::string& Name() const { return name_; }
    uint32_t Version() const { return version_; }

private:
    std::string name_;
    uint32_t version_;
};

}  // namespace ps2r::iop::hle
