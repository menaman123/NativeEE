// PadmanModule implementation; the face is described in the header.
#include "engine/pad/hle/padman_module.h"

#include "engine/pad/hle/pad_protocol.h"

void PadmanModule::Call(const RpcRouter::CallRequest& req) {
    ++calls_;
    const uint32_t op = req.send && read_ ? read_(req.send) : 0;
    last_op_ = op;
    int32_t result = 1;
    switch (op) {
        case kOpInit:
            ++init_calls_;
            break;
        case kOpOpen: {
            // send: +0x04 port, +0x08 slot, +0x0C the 256-byte pad area.
            ++open_calls_;
            if (req.send && read_) {
                const uint32_t port = read_(req.send + 4);
                const uint32_t area = read_(req.send + 12);
                if (port < static_cast<uint32_t>(PadProtocol::kPorts) && area != 0 && (area & 0x1FFFFFFFu) < 0x02000000u)
                    protocol_.RegisterArea(static_cast<int>(port), area, now_ ? now_() : 0);
            }
            break;
        }
        case kOpGetState:
            result = kStateStable;
            break;
        case kOpInfoMode:
        case kOpInfoAct:
            result = 0;
            break;
        case kOpRead:
            // The idle report: status ok, digital mode, no button, sticks centred.
            if (req.recv && write8_) {
                static constexpr uint8_t kIdle[8] = {0x00, 0x70, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80};
                for (uint32_t i = 0; i < 8; i++) write8_(req.recv + i, kIdle[i]);
            }
            return;
        case kOpClose:
        case kOpSetMainMode:
        case kOpSetActDirect:
        case kOpSetActAlign:
        case kOpGetButtonMask:
        case kOpSetButtonInfo:
            break;
        default:
            ++unknown_calls_;
            break;
    }
    if (req.recv == 0 || !write32_) return;
    for (uint32_t off : kReplyResultOffsets)
        if (off + 4 <= req.recv_size || req.recv_size == 0) write32_(req.recv + off, static_cast<uint32_t>(result));
}
