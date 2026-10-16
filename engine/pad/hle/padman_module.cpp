// PadmanModule implementation; the two dialects are described in the header.
#include "engine/pad/hle/padman_module.h"

#include "engine/ee/mem/main_memory.h"
#include "engine/pad/hle/pad_protocol.h"

namespace {
bool ValidArea(uint32_t area) { return area != 0 && MainMemory::Phys(area) < MainMemory::kSize; }
}  // namespace

uint32_t PadmanModule::Sdk3PadId(const ModuleFacts& f, uint32_t port, uint32_t slot) {
    const uint32_t index = port * kSdk3SlotsPerPort + slot;
    const uint32_t offset = index * kSdk3ActBytesPerSlot;
    if (f.act_sink == 0 || offset + kSdk3ActBytesPerSlot > f.act_sink_size) return 1;
    return f.act_sink + offset;
}

void PadmanModule::Reply(const RpcRouter::CallRequest& req, uint32_t offset, uint32_t value) {
    if (req.recv == 0 || !write32_) return;
    if (req.recv_size != 0 && offset + 4 > req.recv_size) return;
    write32_(req.recv + offset, value);
}

void PadmanModule::Call(const RpcRouter::CallRequest& req) {
    ++calls_;
    const uint32_t op = req.send && read_ ? read_(req.send) : 0;
    last_op_ = op;
    const ModuleFacts facts = facts_ ? facts_() : ModuleFacts{};
    last_sdk3_ = IsSdk3(facts.version);
    if (last_sdk3_) CallSdk3(req, op, facts);
    else CallLegacy(req, op);
}

void PadmanModule::CallSdk3(const RpcRouter::CallRequest& req, uint32_t op, const ModuleFacts& facts) {
    const auto arg = [&](uint32_t off) { return req.send && read_ ? read_(req.send + off) : 0u; };
    switch (op) {
        case kSdk3Init:
            ++init_calls_;
            Reply(req, 0x0C, 1);
            return;
        case kSdk3GetModVersion:
            Reply(req, 0x0C, facts.version);
            return;
        case kSdk3Open: {
            ++open_calls_;
            const uint32_t port = arg(4), slot = arg(8), area = arg(0x10);
            const bool ok = port < static_cast<uint32_t>(PadProtocol::kPorts) && slot < kSdk3SlotsPerPort && ValidArea(area);
            if (ok && slot == 0) protocol_.RegisterArea(static_cast<int>(port), area, now_ ? now_() : 0);
            // The pad id first: libpad reads it only after a result of 1.
            Reply(req, 0x14, ok ? Sdk3PadId(facts, port, slot) : 0);
            Reply(req, 0x0C, ok ? 1 : 0);
            return;
        }
        case kSdk3Close: {
            const uint32_t port = arg(4), slot = arg(8);
            if (port < static_cast<uint32_t>(PadProtocol::kPorts) && slot == 0) protocol_.ClearArea(static_cast<int>(port));
            Reply(req, 0x0C, 1);
            return;
        }
        case kSdk3SetMainMode:
        case kSdk3SetActDirect:
        case kSdk3SetActAlign:
            // Accepted: libpad marks the slot BUSY (scePadSetReqState 2) and the
            // next READY stamp completes it (PadProtocol writes reqState 0).
            Reply(req, 0x14, 1);
            return;
        case kSdk3GetButtonMask:
            Reply(req, 0x0C, PadProtocol::kButtonMask);
            return;
        case kSdk3SetButtonInfo:
            Reply(req, 0x10, 1);
            return;
        case kSdk3SetVref:
            Reply(req, 0x1C, 1);
            return;
        case kSdk3GetPortMax:
            Reply(req, 0x0C, kSdk3PortMax);
            return;
        case kSdk3GetSlotMax:
            Reply(req, 0x0C, kSdk3SlotMax);
            return;
        case kSdk3End:
            Reply(req, 0x0C, 1);
            return;
        case kSdk3SetWordA:
        case kSdk3SetWordB:
            Reply(req, 0x08, 1);
            return;
        default:
            // The module prints "unknown" and returns the buffer untouched.
            ++unknown_calls_;
            return;
    }
}

void PadmanModule::CallLegacy(const RpcRouter::CallRequest& req, uint32_t op) {
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
                if (port < static_cast<uint32_t>(PadProtocol::kPorts) && ValidArea(area))
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
    for (uint32_t off : kReplyResultOffsets) Reply(req, off, static_cast<uint32_t>(result));
}
