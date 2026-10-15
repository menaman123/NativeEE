#pragma once
// PadProtocol: the scePad wire image PADMAN's DMA lands in the title's pad area
// once per vsync, produced here at the boundary (the maintenance slot's pad
// stamp duty) from an input sample.
//
// A pad area is two 128-byte slots (double-buffered). libpad's reader picks
// the slot with the larger +0x58 sequence counter, so a stamp writes the
// payload FIRST and the counter LAST (a zero pass never touches +0x58: the
// previous counter stays valid until the new image is published). Slot 1 is
// stamped with seq + 1 so it is the reader's pick; both carry the same image.
//
// Slot layout (libpad 3.0.1 pad_data_new; every byte a reader consults):
//   +0x00 stat70 (0 ok)          +0x01 modeCurId 0x79 (DualShock 2 analog)
//   +0x02 buttons lo (active low: SELECT L3 R3 START UP RIGHT DOWN LEFT)
//   +0x03 buttons hi (active low: L2 R2 L1 R1 TRIANGLE CIRCLE CROSS SQUARE)
//   +0x04 rx +0x05 ry +0x06 lx +0x07 ly
//   +0x08..0x13 pressure bytes (right left up down tri circ cross square
//         l1 r1 l2 r2): 0xFF for a closed contact, 0 otherwise -- the title's
//         per-control reader consumes face/d-pad state from THESE
//   +0x58 u32 seq                +0x60 length 0x20 (scePadRead byte count)
//   +0x64 modeConfig 2  +0x65 modeCurId 0x73  +0x66 model 2
//   +0x68 nrOfModes 0x10  +0x6A nrOfActuators 0x10
//   +0x70 state  +0x71 reqState  +0x72 currentTask  +0x76 0x10
//   +0x79..0x7C button mask bytes FF FF 03 00 (scePadGetButtonMask = 0x3FFFF)
//
// Staging after scePadPortOpen, in boundaries (the vsync clock PADMAN itself
// runs on): elapsed < 8 DISCONN (state 0, reqState 2 BUSY, task 0);
// elapsed < 214 CONFIG (state 1 FINDPAD, BUSY, 0); then READY (state 6
// STABLE, reqState 0 COMPLETE, task 1) -- the title's pad state machine
// needs the pad NOT live through its boot UI's case-2 entry. A readiness
// event (the title's case-2 entry, reported by a title hook) makes READY
// come 2 boundaries after it once DISCONN has passed. READY is sticky: PADMAN
// never regresses a STABLE controller on the clock alone, so a port stays
// READY until PortClose/PortOpen restart the staging (this is what carries a
// READY port across a savestate restore whose boundary clock restarts at 0).
// The first READY stamp
// of a port zeroes the slot (it held phase bytes); READY -> READY re-stamps
// rewrite every consulted byte and skip the zero pass, so a reader on the
// other thread never sees an all-zero (all-pressed / DISCONN) window.
#include <cstdint>
#include <functional>

class PadProtocol {
public:
    // (address, value, size in bytes 1/2/4) -- one guest write, in program order.
    using Writer = std::function<void(uint32_t addr, uint32_t value, int size)>;

    static constexpr int kPorts = 2;
    static constexpr uint32_t kSlotBytes = 0x80;
    static constexpr uint32_t kSeqOffset = 0x58;
    // The counter's boot value; Stamp pre-increments, so the first stamp
    // publishes 2 / 3. After a savestate restore the PADS section sets the
    // counter from the restored slot words (the console's PADMAN never
    // restarts its frame counter; see pads_section.h).
    static constexpr uint32_t kSeqBoot = 1;
    static constexpr uint64_t kDisconnUntil = 8;     // boundaries since PortOpen
    static constexpr uint64_t kConfigUntil = 214;
    static constexpr uint64_t kReadyDelayAfterEvent = 2;
    static constexpr uint8_t kStateDisconn = 0x00, kStateFindPad = 0x01, kStateStable = 0x06;
    static constexpr uint8_t kReqBusy = 0x02, kReqComplete = 0x00;
    static constexpr uint8_t kModeAnalog = 0x79, kModeCurIdAnalog = 0x73;
    static constexpr uint32_t kButtonMask = 0x3FFFF;

    enum class Stage : uint8_t { Disconn = 0, Config = 1, Ready = 2 };

    // One input sample: buttons ACTIVE-HIGH (bit = pressed, libpad bit order),
    // sticks 0x80 neutral.
    struct Input {
        uint16_t buttons = 0;
        uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;
    };

    explicit PadProtocol(Writer writer);

    // scePadPortOpen: the port's 256-byte pad area; staging restarts.
    void RegisterArea(int port, uint32_t area, uint64_t now);
    // scePadPortClose.
    void ClearArea(int port);
    // The title's readiness event (boot UI case-2 entry) at boundary `now`.
    void NoteReadinessEvent(uint64_t now);
    // The boundary duty: stamp both slots of every open port for `now`.
    void Stamp(const Input& in, uint64_t now);

    Stage StageFor(uint64_t elapsed, uint64_t now) const;
    uint32_t Area(int port) const { return port >= 0 && port < kPorts ? ports_[port].area : 0; }
    Stage StageOf(int port) const { return port >= 0 && port < kPorts ? ports_[port].stage : Stage::Disconn; }
    uint32_t Seq() const { return seq_; }
    uint16_t LastWire() const { return last_wire_; }   // active-low word of the last stamp
    uint64_t Stamps() const { return stamps_; }

    // Savestate.
    void SetSeq(uint32_t seq) { seq_ = seq; }
    void RestorePort(int port, uint32_t area, uint64_t opened_at, Stage stage, bool ready_stamped);
    uint64_t OpenedAt(int port) const { return port >= 0 && port < kPorts ? ports_[port].opened_at : 0; }
    // The boundary of the port's first READY stamp since RegisterArea (0 =
    // none yet): the control arm's availability key, recorded in run.meta next
    // to the owned service's `stable=` so a frame-lane difference between the
    // two arms is attributable to when each told the title its pad was live.
    uint64_t ReadyAt(int port) const { return port >= 0 && port < kPorts ? ports_[port].ready_at : 0; }
    bool ReadyStamped(int port) const { return port >= 0 && port < kPorts && ports_[port].ready_stamped; }
    // The active-low word of the last stamp (what a READY re-stamp repeats).
    void SetLastWire(uint16_t wire) { last_wire_ = wire; }
    // The readiness event's boundary; 0 = none seen (NoteReadinessEvent keeps the first).
    void SetReadinessEvent(uint64_t at) { readiness_event_ = at; }
    uint64_t ReadinessEvent() const { return readiness_event_; }

private:
    struct Port {
        uint32_t area = 0;
        uint64_t opened_at = 0;
        Stage stage = Stage::Disconn;
        bool ready_stamped = false;
        uint64_t ready_at = 0;
    };
    void W8(uint32_t a, uint8_t v) { writer_(a, v, 1); }
    void W16(uint32_t a, uint16_t v) { writer_(a, v, 2); }
    void W32(uint32_t a, uint32_t v) { writer_(a, v, 4); }
    void ZeroSlot(uint32_t slot);
    void WritePhaseSlot(uint32_t slot, uint8_t state, uint8_t req_state, uint8_t task, uint32_t seq);
    void WriteReadySlot(uint32_t slot, uint16_t wire, const Input& in, uint32_t seq, bool full);

    Writer writer_;
    Port ports_[kPorts];
    uint32_t seq_ = kSeqBoot;
    uint64_t readiness_event_ = 0;   // 0 = none
    uint16_t last_wire_ = 0xFFFF;
    uint64_t stamps_ = 0;
};
