// PadProtocol across a savestate restore: a port restored READY (stage READY,
// ready_stamped) keeps its READY image when the boundary clock that drives the
// stamps restarts from 0 -- a controller PADMAN already reports STABLE never
// regresses to DISCONN/FINDPAD on the clock alone (only scePadPortClose /
// PortOpen restarts the staging). The restored slot image (state 0x06, length
// 0x20, mode 0x79) stays, and the first stamp after restore is a READY ->
// READY re-stamp (no zero pass). A port restored mid-CONFIG still stages by
// the clock, and a re-open after restore restarts at DISCONN. The slot
// sequence counter continues from the restored slot words (PadsSection derives
// it; the Restart toggle is the original's restart-at-1).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/pad/hle/pad_protocol.h"
#include "engine/pad/savestate/pads_section.h"

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

// The anchor's restored slot image: a STABLE DualShock 2, nothing pressed.
void SeedRestoredReadySlots() {
    std::memset(g_ram, 0, sizeof(g_ram));
    for (uint32_t slot = 0; slot < 2; slot++) {
        const uint32_t b = slot * 0x80;
        g_ram[b + 0x01] = 0x79;
        g_ram[b + 0x02] = 0xFF;
        g_ram[b + 0x03] = 0xFF;
        for (uint32_t i = 4; i < 8; i++) g_ram[b + i] = 0x80;
        g_ram[b + 0x60] = 0x20;
        g_ram[b + 0x64] = 0x02;
        g_ram[b + 0x65] = 0x73;
        g_ram[b + 0x66] = 0x02;
        g_ram[b + 0x68] = 0x10;
        g_ram[b + 0x6A] = 0x10;
        g_ram[b + 0x70] = 0x06;
        g_ram[b + 0x71] = 0x00;
        g_ram[b + 0x72] = 0x01;
        g_ram[b + 0x76] = 0x10;
        g_ram[b + 0x79] = 0xFF;
        g_ram[b + 0x7A] = 0xFF;
        g_ram[b + 0x7B] = 0x03;
    }
}

bool SlotIsReady(uint32_t b) {
    return Rd8(b + 0x01) == 0x79 && Rd8(b + 0x60) == 0x20 && Rd8(b + 0x70) == 0x06 && Rd8(b + 0x71) == 0x00 &&
           Rd8(b + 0x72) == 0x01;
}
}  // namespace

// The anchors' PADS section: opened_at 0, stage READY, ready_stamped, a
// readiness event at 126; the restored boundary clock starts at 0.
static void TestRestoredReadyStaysReady() {
    SeedRestoredReadySlots();
    g_writes.clear();
    PadProtocol pad(&Writer);
    pad.RestorePort(0, kArea, 0, PadProtocol::Stage::Ready, true);
    pad.SetLastWire(0xFFFF);
    pad.SetReadinessEvent(126);
    PadProtocol::Input idle;
    pad.Stamp(idle, 0);
    CHECK(pad.StageOf(0) == PadProtocol::Stage::Ready, "restored_ready_stage_kept_at_now_0");
    CHECK(SlotIsReady(0x00) && SlotIsReady(0x80), "restored_ready_slots_state_06_len_20_both_slots");
    CHECK(Rd8(0x02) == 0xFF && Rd8(0x03) == 0xFF, "idle_wire_ffff_after_restore");
    bool zero_pass = false;
    for (const Rec& r : g_writes) zero_pass |= (r.size == 4 && r.value == 0 && r.addr != kArea + 0x58 && r.addr != kArea + 0xD8);
    CHECK(!zero_pass, "first_stamp_after_restore_is_a_restamp_no_zero_pass");
    CHECK(pad.ReadyStamped(0), "ready_stamped_kept");
    // Through the window where the clock alone would say DISCONN (< 8) and
    // CONFIG (< 214): still READY.
    for (uint64_t now = 1; now <= 220; now++) pad.Stamp(idle, now);
    CHECK(pad.StageOf(0) == PadProtocol::Stage::Ready && SlotIsReady(0x00), "ready_through_disconn_and_config_windows");
    // Re-open restarts the staging (the only way back to DISCONN).
    pad.RegisterArea(0, kArea, 300);
    pad.Stamp(idle, 300);
    CHECK(Rd8(0x70) == 0x00 && !pad.ReadyStamped(0), "reopen_after_restore_restarts_disconn");
}

// A port restored READY whose first READY stamp had not yet happened (stage
// READY, ready_stamped false): the stage holds; the first stamp zeroes the
// phase bytes and publishes the READY image as the live sequence does.
static void TestRestoredReadyNotStampedHolds() {
    std::memset(g_ram, 0xAA, sizeof(g_ram));
    PadProtocol pad(&Writer);
    pad.RestorePort(0, kArea, 0, PadProtocol::Stage::Ready, false);
    PadProtocol::Input idle;
    pad.Stamp(idle, 3);
    CHECK(pad.StageOf(0) == PadProtocol::Stage::Ready && SlotIsReady(0x00) && pad.ReadyStamped(0),
          "restored_ready_unstamped_publishes_ready");
}

// A port restored mid-CONFIG (its elapsed carried by opened_at in the restored
// clock) stages by the clock as a live port does.
static void TestRestoredConfigStagesByClock() {
    std::memset(g_ram, 0xAA, sizeof(g_ram));
    PadProtocol pad(&Writer);
    pad.RestorePort(0, kArea, 0, PadProtocol::Stage::Config, false);
    PadProtocol::Input idle;
    pad.Stamp(idle, 100);
    CHECK(Rd8(0x70) == 0x01 && pad.StageOf(0) == PadProtocol::Stage::Config, "restored_config_at_100_is_findpad");
    pad.Stamp(idle, 214);
    CHECK(Rd8(0x70) == 0x06 && pad.StageOf(0) == PadProtocol::Stage::Ready, "restored_config_reaches_ready_at_214");
}

// The slot sequence counter across a restore: the console's PADMAN continues
// its frame counter, and the only place a saved state keeps it is the pad
// area's +0x58 words. The PADS decode derives the protocol's counter from the
// restored slot 0 word (N), so the next stamp publishes N+1 / N+2 -- not the
// boot value's 2 / 3 (the original's process-static counter restarted at 1
// after every load, the value its baseline images carry).
static uint32_t Rd32(uint32_t addr) {
    const uint32_t off = addr - kArea;
    uint32_t v = 0;
    std::memcpy(&v, g_ram + off, 4);
    return v;
}

static std::vector<uint8_t> PadsPayloadPort0Ready() {
    // The anchors' PADS record: port 0 open at kArea since 0, READY, stamped;
    // idle wire; readiness event 126; latch bytes zero.
    std::vector<uint8_t> p(PadsSection::kPayloadBytes, 0);
    auto put32 = [&p](size_t at, uint32_t v) { std::memcpy(&p[at], &v, 4); };
    put32(0x00, kArea);
    p[0x10] = 2;
    p[0x12] = 0xFF;
    p[0x13] = 0xFF;
    p[0x14] = 1;
    put32(0x16, 126);
    return p;
}

static void TestRestoredSeqContinuesFromImage() {
    SeedRestoredReadySlots();
    const uint32_t kImageSeq = 1552;   // the artdet anchor's slot 0 word at 0xbb05d8
    std::memcpy(g_ram + 0x58, &kImageSeq, 4);
    const uint32_t s1 = kImageSeq + 1;
    std::memcpy(g_ram + 0xD8, &s1, 4);
    PadProtocol pad(&Writer);
    PadsSection section(pad, &Rd32, PadsSection::SeqRestore::FromImage);
    std::string err;
    CHECK(section.Restore(PadsPayloadPort0Ready(), &err), "pads_restore_ok");
    CHECK(section.DerivedSeq() == kImageSeq && pad.Seq() == kImageSeq, "restore_derives_seq_from_slot0_word");
    PadProtocol::Input idle;
    pad.Stamp(idle, 0);
    CHECK(Rd32(kArea + 0x58) == kImageSeq + 1, "first_stamp_after_restore_publishes_image_plus_1");
    CHECK(Rd32(kArea + 0xD8) == kImageSeq + 2, "slot1_publishes_image_plus_2");
    pad.Stamp(idle, 1);
    pad.Stamp(idle, 2);
    CHECK(Rd32(kArea + 0x58) == kImageSeq + 3, "third_stamp_publishes_image_plus_3");
    CHECK(SlotIsReady(0x00) && SlotIsReady(0x80), "ready_image_kept_with_derived_seq");
}

// The toggle back to the original's behaviour: the counter restarts at the
// boot value, the first stamp publishes 2 / 3 whatever the image holds.
static void TestRestoredSeqRestartToggle() {
    SeedRestoredReadySlots();
    const uint32_t kImageSeq = 1552;
    std::memcpy(g_ram + 0x58, &kImageSeq, 4);
    PadProtocol pad(&Writer);
    pad.SetSeq(777);   // a live counter the restore must reset
    PadsSection section(pad, &Rd32, PadsSection::SeqRestore::Restart);
    std::string err;
    CHECK(section.Restore(PadsPayloadPort0Ready(), &err), "pads_restore_restart_ok");
    CHECK(section.DerivedSeq() == 0 && pad.Seq() == PadProtocol::kSeqBoot, "restart_toggle_resets_to_boot");
    PadProtocol::Input idle;
    pad.Stamp(idle, 0);
    CHECK(Rd32(kArea + 0x58) == 2 && Rd32(kArea + 0xD8) == 3, "restart_toggle_first_stamp_publishes_2_3");
}

// No reader (a decoder with no guest RAM) or no open port: the counter is left
// alone; the absent-section path puts it at the boot value.
static void TestRestoredSeqNoReaderOrNoPort() {
    SeedRestoredReadySlots();
    const uint32_t kImageSeq = 1552;
    std::memcpy(g_ram + 0x58, &kImageSeq, 4);
    PadProtocol pad(&Writer);
    pad.SetSeq(40);
    PadsSection no_reader(pad);
    std::string err;
    CHECK(no_reader.Restore(PadsPayloadPort0Ready(), &err) && pad.Seq() == 40 && no_reader.DerivedSeq() == 0,
          "no_reader_leaves_counter");
    PadsSection with_reader(pad, &Rd32);
    std::vector<uint8_t> closed(PadsSection::kPayloadBytes, 0);
    closed[0x12] = 0xFF;
    closed[0x13] = 0xFF;
    CHECK(with_reader.Restore(closed, &err) && pad.Seq() == 40 && with_reader.DerivedSeq() == 0,
          "no_open_port_leaves_counter");
    with_reader.SeedBootDefault();
    CHECK(pad.Seq() == PadProtocol::kSeqBoot, "absent_section_seeds_boot_seq");
}

int main() {
    TestRestoredReadyStaysReady();
    TestRestoredReadyNotStampedHolds();
    TestRestoredConfigStagesByClock();
    TestRestoredSeqContinuesFromImage();
    TestRestoredSeqRestartToggle();
    TestRestoredSeqNoReaderOrNoPort();
    if (g_fail) { std::printf("FAILURES: %d\n", g_fail); return 1; }
    std::printf("ALL PASS\n");
    return 0;
}
