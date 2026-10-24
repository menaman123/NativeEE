// engine/iop/hle/thevent/thevent_module.h -- TheventModule: the IOP kernel's
// thevent library routed to IopKernel's event flags: CreateEventFlag (idx 4),
// DeleteEventFlag (5), SetEventFlag (6), iSetEventFlag (7), ClearEventFlag
// (8), iClearEventFlag (9), WaitEventFlag (10), PollEventFlag (11),
// ReferEventFlagStatus (12). EventFlagParam {attr, option, init}: attr bit 1
// (EA_MULTI) allows several waiters. WaitEventFlag(ef, bits, mode, u32* res)
// parks the running thread until the pattern matches (mode: WEF_OR 1 / WEF_AND 0,
// WEF_CLEAR 0x10) and stores the pre-clear value in *res; PollEventFlag is the
// non-parking form (KE_EVF_COND when the pattern does not match).
#pragma once

#include <cstdint>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {
class IopModuleTable;
}

namespace ps2r::iop::hle {

class TheventModule {
public:
    static constexpr uint32_t kAttrMulti = 0x2;    // EA_MULTI
    static constexpr uint32_t kModeOr = 0x1;       // WEF_OR
    static constexpr uint32_t kModeClear = 0x10;   // WEF_CLEAR

    TheventModule(IopModuleTable& table, IopMemory& mem) : table_(table), mem_(mem) {}

    void CreateEventFlag(IopContext& ctx);        // int (EventFlagParam*) -> ef id
    void DeleteEventFlag(IopContext& ctx);        // int (ef) -> 0
    void SetEventFlag(IopContext& ctx);           // int (ef, bits) -> 0
    void iSetEventFlag(IopContext& ctx);          // int (ef, bits) -> 0
    void ClearEventFlag(IopContext& ctx);         // int (ef, keep) -> 0
    void iClearEventFlag(IopContext& ctx);        // int (ef, keep) -> 0
    void WaitEventFlag(IopContext& ctx);          // int (ef, bits, mode, u32* res) -> 0 / error
    void PollEventFlag(IopContext& ctx);          // int (ef, bits, mode, u32* res) -> 0 / KE_EVF_COND
    void ReferEventFlagStatus(IopContext& ctx);   // int (ef, EventFlagStatus*) -> 0

    int Created() const { return created_; }
    void SetCreated(int n) { created_ = n < 0 ? 0 : n; }   // savestate restore

private:
    IopModuleTable& table_;
    IopMemory& mem_;
    int created_ = 0;
};

}  // namespace ps2r::iop::hle
