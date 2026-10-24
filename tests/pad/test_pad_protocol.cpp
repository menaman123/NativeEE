// PadProtocol: the port of the original host_app/input/test_pad_buffer.cpp
// protocol half over a host byte array standing in for the pad area --
// the staging timeline (DISCONN < 8, CONFIG < 214, READY), the READY slot
// byte contract (active-low inversion, +0x02/+0x03 order, mode ids, pressure
// bytes, the FF FF 03 00 mask bytes, +0x58 written LAST), the zero pass that
// skips +0x58, the READY -> READY re-stamp that skips the zero pass, the idle
// wire (0xFFFF, sticks 0x80, pressure 0), and the readiness event.
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/pad/hle/pad_protocol.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

namespace {
constexpr uint32_t kArea = 0x00400000;
uint8_t g_ram[0x200];
struct Rec { uint32_t addr; uint32_t value; int size; };
std::vector<Rec> g_writes;

void Writer(uint32_t addr, uint32_t value, int size) {
    g_writes.push_back(Rec{addr, value, size});
    const uint32_t off = addr - kArea;
    if (off + static_cast<uint32_t>(size) > sizeof(g_ram)) return;
    for (int i = 0; i < size; i++) g_ram[off + i] = static_cast<uint8_t>(value >> (8 * i));
}
uint8_t Rd8(uint32_t off) { return g_ram[off]; }
uint32_t Rd32(uint32_t off) { return g_ram[off] | (g_ram[off + 1] << 8) | (g_ram[off + 2] << 16) | (static_cast<uint32_t>(g_ram[off + 3]) << 24); }

// libpad bit order (active high in Input).
constexpr uint16_t kStart = 0x0008, kCross = 0x4000, kUp = 0x0010;
}  // namespace

static void TestStagingTimeline() {
    std::memset(g_ram, 0xAA, sizeof(g_ram));
    PadProtocol pad(&Writer);
    pad.RegisterArea(0, kArea, 100);
    PadProtocol::Input idle;
    pad.Stamp(idle, 100);
    CHECK(pad.ReadyAt(0) == 0, "ready_at_none_before_ready");
    CHECK(Rd8(0x70) == 0x00 && Rd8(0x71) == 0x02 && Rd8(0x72) == 0x00, "disconn_state_busy_task0");
    CHECK(Rd8(0x02) == 0x00 && Rd8(0x03) == 0x00, "disconn_carries_no_button_word");
    CHECK(pad.StageOf(0) == PadProtocol::Stage::Disconn, "stage_disconn");
    pad.Stamp(idle, 107);
    CHECK(Rd8(0x70) == 0x00, "elapsed_7_still_disconn");
    pad.Stamp(idle, 108);
    CHECK(Rd8(0x70) == 0x01 && Rd8(0x71) == 0x02 && Rd8(0x72) == 0x00, "elapsed_8_config_findpad_busy");
    pad.Stamp(idle, 313);
    CHECK(Rd8(0x70) == 0x01, "elapsed_213_still_config");
    pad.Stamp(idle, 314);
    CHECK(Rd8(0x70) == 0x06 && Rd8(0x71) == 0x00 && Rd8(0x72) == 0x01, "elapsed_214_ready_stable_complete_task1");
    CHECK(pad.StageOf(0) == PadProtocol::Stage::Ready && pad.ReadyStamped(0), "stage_ready");
    // The first READY stamp's boundary is the control arm's availability key
    // (run.meta pad_ready); later stamps keep it, PortOpen restarts it.
    CHECK(pad.ReadyAt(0) == 314, "ready_at_first_ready_stamp");
    pad.Stamp(idle, 315);
    CHECK(pad.ReadyAt(0) == 314, "ready_at_kept_by_later_stamps");
    pad.RegisterArea(0, kArea, 400);
    CHECK(pad.ReadyAt(0) == 0 && pad.ReadyAt(1) == 0, "ready_at_restarts_with_port_open");
    // The second slot mirrors the first (its own seq).
    CHECK(Rd8(0x80 + 0x70) == 0x06 && Rd8(0x80 + 0x71) == 0x00, "slot1_ready_too");
    // Port 1 unopened: nothing written there.
    CHECK(pad.Area(1) == 0, "port1_closed");
}

static void TestReadyByteContract() {
    std::memset(g_ram, 0xAA, sizeof(g_ram));
    g_writes.clear();
    PadProtocol pad(&Writer);
    pad.RegisterArea(0, kArea, 0);
    PadProtocol::Input in;
    in.buttons = kStart | kCross;   // START (lo bit 3) + CROSS (hi bit 6)
    in.lx = 0x11; in.ly = 0x22; in.rx = 0x33; in.ry = 0x44;
    pad.Stamp(in, 214);
    bool ok = true;
    for (uint32_t slot = 0; slot < 2; slot++) {
        const uint32_t b = slot * 0x80;
        ok &= Rd8(b + 0x00) == 0x00 && Rd8(b + 0x01) == 0x79;
        ok &= Rd8(b + 0x02) == 0xF7;   // ~0x08: START pressed in the low byte
        ok &= Rd8(b + 0x03) == 0xBF;   // ~0x40: CROSS pressed in the high byte
        ok &= Rd8(b + 0x04) == 0x33 && Rd8(b + 0x05) == 0x44 && Rd8(b + 0x06) == 0x11 && Rd8(b + 0x07) == 0x22;
        for (uint32_t off = 0x08; off <= 0x13; off++) {
            const uint8_t want = (off == 0x0E) ? 0xFF : 0x00;   // only cross has pressure
            ok &= Rd8(b + off) == want;
        }
        ok &= Rd8(b + 0x60) == 0x20 && Rd8(b + 0x64) == 0x02 && Rd8(b + 0x65) == 0x73 && Rd8(b + 0x66) == 0x02;
        ok &= Rd8(b + 0x68) == 0x02 && Rd8(b + 0x69) == 0x01 && Rd8(b + 0x6A) == 0x02;   // DualShock 2: 2 modes, 2 actuators
        ok &= Rd8(b + 0x30) == 0x01 && Rd8(b + 0x31) == 0x02 && Rd8(b + 0x32) == 0x00 && Rd8(b + 0x33) == 0x0A;
        ok &= Rd8(b + 0x34) == 0x01 && Rd8(b + 0x35) == 0x01 && Rd8(b + 0x36) == 0x01 && Rd8(b + 0x37) == 0x14;
        ok &= Rd8(b + 0x50) == 0x04 && Rd8(b + 0x51) == 0x00 && Rd8(b + 0x52) == 0x07 && Rd8(b + 0x53) == 0x00;
        ok &= Rd8(b + 0x70) == 0x06 && Rd8(b + 0x71) == 0x00 && Rd8(b + 0x72) == 0x01 && Rd8(b + 0x76) == 0x10;
        const uint32_t mask = (Rd8(b + 0x7C) << 24) | (Rd8(b + 0x7B) << 16) | (Rd8(b + 0x7A) << 8) | Rd8(b + 0x79);
        ok &= mask == 0x3FFFF;
    }
    CHECK(ok, "ready_slot_bytes_both_slots");
    CHECK(pad.LastWire() == static_cast<uint16_t>(~(kStart | kCross)), "wire_is_active_low");
    const uint32_t s0 = Rd32(0x58), s1 = Rd32(0x80 + 0x58);
    CHECK(s1 == s0 + 1, "slot1_seq_is_slot0_seq_plus_one");
    // Write order: within a slot the +0x58 counter is the LAST write, and the
    // zero pass never touches +0x58.
    bool seq_last = true, zero_skips_seq = true;
    size_t seq_writes = 0;
    for (size_t i = 0; i < g_writes.size(); i++) {
        const Rec& r = g_writes[i];
        if (r.addr == kArea + 0x58 || r.addr == kArea + 0xD8) {
            ++seq_writes;
            if (r.value == 0) zero_skips_seq = false;
            const bool next_is_other_slot_or_end = (i + 1 == g_writes.size()) || (g_writes[i + 1].addr >= kArea + 0x80);
            if (r.addr == kArea + 0x58 && !next_is_other_slot_or_end) seq_last = false;
            if (r.addr == kArea + 0xD8 && i + 1 != g_writes.size()) seq_last = false;
        }
    }
    CHECK(seq_writes == 2 && seq_last && zero_skips_seq, "payload_first_seq_last_zero_pass_skips_seq");
    // READY -> READY re-stamp: no zero pass (no 4-byte zero write to a payload
    // word), the counter advances, every consulted byte re-stamped.
    g_writes.clear();
    PadProtocol::Input idle;
    pad.Stamp(idle, 215);
    bool zero_pass = false;
    for (const Rec& r : g_writes) zero_pass |= (r.size == 4 && r.value == 0 && r.addr != kArea + 0x58 && r.addr != kArea + 0xD8);
    CHECK(!zero_pass, "ready_restamp_skips_zero_pass");
    CHECK(Rd32(0x58) > s0, "seq_monotonic_per_stamp");
    CHECK(Rd8(0x02) == 0xFF && Rd8(0x03) == 0xFF, "idle_wire_ffff");
    CHECK(Rd8(0x04) == 0x80 && Rd8(0x05) == 0x80 && Rd8(0x06) == 0x80 && Rd8(0x07) == 0x80, "idle_sticks_neutral");
    bool pressure_zero = true;
    for (uint32_t off = 0x08; off <= 0x13; off++) pressure_zero &= Rd8(off) == 0;
    CHECK(pressure_zero, "idle_pressure_zero");
    // Re-open: staging restarts and the first READY stamp zeroes again.
    pad.RegisterArea(0, kArea, 1000);
    pad.Stamp(idle, 1000);
    CHECK(Rd8(0x70) == 0x00 && !pad.ReadyStamped(0), "reopen_restarts_disconn");
}

static void TestReadinessEvent() {
    std::memset(g_ram, 0, sizeof(g_ram));
    PadProtocol pad(&Writer);
    pad.RegisterArea(0, kArea, 0);
    PadProtocol::Input in;
    in.buttons = kUp;
    pad.Stamp(in, 5);
    CHECK(Rd8(0x70) == 0x00, "before_event_disconn");
    pad.NoteReadinessEvent(20);   // the title's case-2 entry at boundary 20
    pad.Stamp(in, 21);
    CHECK(Rd8(0x70) == 0x01, "event_plus_1_still_config");
    pad.Stamp(in, 22);
    CHECK(Rd8(0x70) == 0x06 && Rd8(0x0A) == 0xFF && Rd8(0x02) == 0xEF, "event_plus_2_ready_with_up_pressed");
    CHECK(pad.ReadyAt(0) == 22 && pad.ReadinessEvent() == 20, "ready_at_event_plus_2");
    // An event inside the DISCONN window does not shorten it.
    PadProtocol pad2(&Writer);
    pad2.RegisterArea(1, kArea, 0);
    pad2.NoteReadinessEvent(1);
    pad2.Stamp(in, 7);
    CHECK(Rd8(0x70) == 0x00, "event_never_shortens_disconn");
    pad2.Stamp(in, 8);
    CHECK(Rd8(0x70) == 0x06, "event_ready_once_disconn_passed");
}

int main() {
    TestStagingTimeline();
    TestReadyByteContract();
    TestReadinessEvent();
    if (g_fail) { std::printf("FAILURES: %d\n", g_fail); return 1; }
    std::printf("ALL PASS\n");
    return 0;
}
