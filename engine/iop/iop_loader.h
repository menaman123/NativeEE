// engine/iop/iop_loader.h -- IopLoader: puts an IRX (ELF32 MIPS, REL) module's
// sections into IOP RAM and runs its entry on the cooperative kernel. The
// section walk copies .text/.data/.rodata bytes at their sh_addr and
// zero-fills .bss; the entry runs as a fresh IopContext ($sp/$gp from the
// title's ModuleLayout) through the recompiled function registry; afterwards
// the module table is told the module's RPC-dispatch thread (read from the
// layout's rpc_tid word, 1 when unset). Every address here is layout data --
// the engine holds no title constants.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/iop/iop_context.h"

namespace ps2r::iop {

class IopMemory;
class IopModuleTable;

class IopLoader {
public:
    using GuestFunction = std::function<void(IopContext&)>;
    using FunctionResolver = std::function<GuestFunction(uint32_t addr)>;

    struct ModuleLayout {
        uint32_t gp = 0;             // the module's $gp
        uint32_t entry = 0;          // the module entry (_start)
        uint32_t stack_top = 0;      // the entry's $sp is stack_top - 0x10
        uint32_t stack_stride = 0;   // per-thread stack spacing (thbase)
        uint32_t rpc_tid_addr = 0;   // word holding the RPC-dispatch tid after entry (0 = none)
    };

    struct SectionReport {
        bool ok = false;
        bool found_text = false;
        uint32_t text_bytes = 0;
        uint32_t data_bytes = 0;
        uint32_t rodata_bytes = 0;
        uint32_t bss_bytes = 0;
        std::string error;
    };

    // The IOP reboot handshake (sceSifRebootIop with an IOPRP image): the pre-reboot
    // BIOS module set is skipped; the post-reboot set is what the module table hosts.
    struct RebootReport {
        bool accepted = false;
        std::string image;
    };

    static constexpr uint32_t kLoadFileRpcId = 0x80000006;   // the EE's LOADFILE server

    explicit IopLoader(IopMemory& mem) : mem_(mem) {}

    // ELF32 checks (magic, ELFCLASS32, EM_MIPS) and the section copy/zero-fill.
    SectionReport LoadSections(const uint8_t* irx, size_t size);

    // Runs the entry at layout.entry and reports the module's RPC tid to the table.
    // Returns false when no recompiled function exists at the entry.
    bool RunEntry(const ModuleLayout& layout, const FunctionResolver& resolve, IopModuleTable& table);

    RebootReport RebootHandshake(const std::string& iop_image_path);
    // The LOADFILE RPC face: a module load request by disc path; returns the module id
    // handed back to the EE (1-based per accepted request, 0 = rejected).
    uint32_t LoadFileRequest(const std::string& path);
    const std::vector<std::string>& LoadRequests() const { return load_requests_; }
    int LastRpcTid() const { return last_rpc_tid_; }

private:
    IopMemory& mem_;
    std::vector<std::string> load_requests_;
    int last_rpc_tid_ = 0;
};

}  // namespace ps2r::iop
