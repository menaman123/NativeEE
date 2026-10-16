// engine/iop/hle/thmsgbx/thmsgbx_module.h -- ThmsgbxModule: the IOP kernel's
// thmsgbx library routed to IopKernel's message boxes: CreateMbx (idx 4),
// DeleteMbx (5), SendMbx (6), iSendMbx (7), ReceiveMbx (8), PollMbx (9),
// ReferMbxStatus (11). A message is a guest pointer to a MsgPacket whose first
// two words are the kernel's link fields; the kernel keeps the pointers as
// opaque values (no host dereference), so the module is a queue of IOP
// addresses per box. ReceiveMbx parks the running thread (Wait::Mbx) until a
// message arrives; PollMbx answers KE_MBOX_NOMSG when the box is empty.
#pragma once

#include <cstdint>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {
class IopModuleTable;
}

namespace ps2r::iop::hle {

class ThmsgbxModule {
public:
    static constexpr int kNoMessage = -424;   // KE_MBOX_NOMSG
    static constexpr int kUnknownMbxid = -410;

    ThmsgbxModule(IopModuleTable& table, IopMemory& mem) : table_(table), mem_(mem) {}

    void CreateMbx(IopContext& ctx);          // int (MbxParam*) -> mbx id
    void DeleteMbx(IopContext& ctx);          // int (mbx) -> 0
    void SendMbx(IopContext& ctx);            // int (mbx, MsgPacket*) -> 0
    void iSendMbx(IopContext& ctx);           // int (mbx, MsgPacket*) -> 0
    void ReceiveMbx(IopContext& ctx);         // int (MsgPacket** out, mbx) -> 0 (parks until a message)
    void PollMbx(IopContext& ctx);            // int (MsgPacket** out, mbx) -> 0 / KE_MBOX_NOMSG
    void ReferMbxStatus(IopContext& ctx);     // int (mbx, MbxStatus*) -> 0

    int Created() const { return created_; }

private:
    IopModuleTable& table_;
    IopMemory& mem_;
    int created_ = 0;
};

}  // namespace ps2r::iop::hle
