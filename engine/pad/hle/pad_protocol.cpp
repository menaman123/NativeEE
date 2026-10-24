// PadProtocol implementation; the slot layout and staging are in the header.
#include "engine/pad/hle/pad_protocol.h"

#include <utility>

PadProtocol::PadProtocol(Writer writer) : writer_(std::move(writer)) {}

void PadProtocol::RegisterArea(int port, uint32_t area, uint64_t now) {
    if (port < 0 || port >= kPorts) return;
    ports_[port] = Port{area, now, Stage::Disconn, false};
}

void PadProtocol::ClearArea(int port) {
    if (port < 0 || port >= kPorts) return;
    ports_[port] = Port{};
}

void PadProtocol::NoteReadinessEvent(uint64_t now) {
    if (readiness_event_ == 0) readiness_event_ = now == 0 ? 1 : now;
}

void PadProtocol::RestorePort(int port, uint32_t area, uint64_t opened_at, Stage stage, bool ready_stamped) {
    if (port < 0 || port >= kPorts) return;
    ports_[port] = Port{area, opened_at, stage, ready_stamped};
}

PadProtocol::Stage PadProtocol::StageFor(uint64_t elapsed, uint64_t now) const {
    if (readiness_event_ != 0 && elapsed >= kDisconnUntil && now >= readiness_event_ + kReadyDelayAfterEvent)
        return Stage::Ready;
    if (elapsed < kDisconnUntil) return Stage::Disconn;
    if (elapsed < kConfigUntil) return Stage::Config;
    return Stage::Ready;
}

// Every word but the sequence counter: the previous counter stays valid until
// the new image is published.
void PadProtocol::ZeroSlot(uint32_t slot) {
    for (uint32_t i = 0; i < kSlotBytes; i += 4) {
        if (i == kSeqOffset) continue;
        W32(slot + i, 0);
    }
}

void PadProtocol::WritePhaseSlot(uint32_t slot, uint8_t state, uint8_t req_state, uint8_t task, uint32_t seq) {
    ZeroSlot(slot);
    W8(slot + 0x70, state);
    W8(slot + 0x71, req_state);
    W8(slot + 0x72, task);
    W32(slot + kSeqOffset, seq);   // published last
}

void PadProtocol::WriteReadySlot(uint32_t slot, uint16_t wire, const Input& in, uint32_t seq, bool full) {
    if (full) ZeroSlot(slot);
    W8(slot + 0x00, 0x00);
    W8(slot + 0x01, kModeAnalog);
    W16(slot + 0x02, wire);   // one aligned halfword: lo byte SELECT..LEFT, hi byte L2..SQUARE
    W8(slot + 0x04, in.rx);
    W8(slot + 0x05, in.ry);
    W8(slot + 0x06, in.lx);
    W8(slot + 0x07, in.ly);
    // Pressure bytes in padButtonStatus order; a closed contact reports full pressure.
    static const uint8_t kPressureBit[12] = {5, 7, 4, 6, 12, 13, 14, 15, 10, 11, 8, 9};
    for (uint32_t i = 0; i < 12; i++) W8(slot + 0x08 + i, (wire & (1u << kPressureBit[i])) ? 0x00 : 0xFF);
    // The DualShock 2 descriptors libpad's scePadInfoAct / scePadInfoMode read
    // from the slot: the actuator table [actno][term-1] and the mode table.
    for (uint32_t i = 0; i < kActuators * 4; i++) W8(slot + 0x30 + i, kActuatorTable[i / 4][i % 4]);
    for (uint32_t i = 0; i < kModeCount; i++) W16(slot + 0x50 + i * 2, kModeTable[i]);
    W8(slot + 0x60, 0x20);
    W8(slot + 0x64, 0x02);
    W8(slot + 0x65, kModeCurIdAnalog);
    W8(slot + 0x66, 0x02);
    W8(slot + 0x68, kModeCount);
    W8(slot + 0x69, kModeCurOffset);
    W8(slot + 0x6A, kActuators);
    W8(slot + 0x70, kStateStable);
    W8(slot + 0x71, kReqComplete);
    W8(slot + 0x72, 0x01);
    W8(slot + 0x76, 0x10);
    W8(slot + 0x79, static_cast<uint8_t>(kButtonMask & 0xFF));
    W8(slot + 0x7A, static_cast<uint8_t>((kButtonMask >> 8) & 0xFF));
    W8(slot + 0x7B, static_cast<uint8_t>((kButtonMask >> 16) & 0xFF));
    W8(slot + 0x7C, static_cast<uint8_t>((kButtonMask >> 24) & 0xFF));
    W32(slot + kSeqOffset, seq);   // published last
}

void PadProtocol::Stamp(const Input& in, uint64_t now) {
    const uint16_t wire = static_cast<uint16_t>(~in.buttons);   // active low on the wire
    last_wire_ = wire;
    ++seq_;
    for (int port = 0; port < kPorts; port++) {
        Port& p = ports_[port];
        if (p.area == 0) continue;
        const uint64_t elapsed = now >= p.opened_at ? now - p.opened_at : 0;
        // PADMAN never regresses a STABLE controller on the clock alone: once
        // READY the port stays READY until scePadPortClose/PortOpen restart the
        // staging (RegisterArea). This also carries a READY port across a
        // savestate restore whose boundary clock restarts at 0.
        const Stage stage = p.stage == Stage::Ready ? Stage::Ready : StageFor(elapsed, now);
        p.stage = stage;
        const uint32_t slot0 = p.area, slot1 = p.area + kSlotBytes;
        switch (stage) {
            case Stage::Disconn:
                WritePhaseSlot(slot0, kStateDisconn, kReqBusy, 0, seq_);
                WritePhaseSlot(slot1, kStateDisconn, kReqBusy, 0, seq_ + 1);
                p.ready_stamped = false;
                break;
            case Stage::Config:
                WritePhaseSlot(slot0, kStateFindPad, kReqBusy, 0, seq_);
                WritePhaseSlot(slot1, kStateFindPad, kReqBusy, 0, seq_ + 1);
                p.ready_stamped = false;
                break;
            case Stage::Ready: {
                const bool full = !p.ready_stamped;
                if (full && p.ready_at == 0) p.ready_at = now;
                WriteReadySlot(slot0, wire, in, seq_, full);
                WriteReadySlot(slot1, wire, in, seq_ + 1, full);
                p.ready_stamped = true;
                break;
            }
        }
        ++stamps_;
    }
}
