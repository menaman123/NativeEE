// tests/pad/test_pad_round_trip.cpp -- the InputLog round trip through the pad
// stamp duty (CONSTRUCTION_PLAN gate item, PHASE4_PLAN section 2.8):
//   a scripted run stamps slot images and RECORDS the served samples;
//   the recorder's lines are parsed back and REPLAYED through a second duty;
//   the two runs' slot images are identical at every boundary.
// Also pins: the neutral sample when no script is loaded, the wire polarity
// (script button held -> active-low bit cleared -> pressure byte 0xFF).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/triage/input_log.h"
#include "engine/pad/hle/pad_protocol.h"
#include "engine/pad/hle/pad_stamp_duty.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { \
        if (cond) std::printf("PASS %s\n", name); \
        else { std::printf("FAIL %s\n", name); g_fail++; } \
    } while (0)

namespace {
constexpr uint32_t kArea0 = 0x1000, kArea1 = 0x2000, kRamBytes = 0x3000;

struct Ram { uint8_t bytes[kRamBytes] = {}; };

PadProtocol::Writer WriterFor(Ram& ram) {
    return [&ram](uint32_t a, uint32_t v, int size) {
        if (a + static_cast<uint32_t>(size) > kRamBytes) return;
        for (int i = 0; i < size; i++) ram.bytes[a + i] = static_cast<uint8_t>(v >> (8 * i));
    };
}
std::vector<uint8_t> ImageOf(const Ram& ram) { return std::vector<uint8_t>(ram.bytes, ram.bytes + kRamBytes); }

// Runs `boundaries` stamps from an open at boundary 1 on both ports; returns the
// image after every boundary.
std::vector<std::vector<uint8_t>> Run(PadStampDuty& duty, PadProtocol& pad, Ram& ram, uint64_t boundaries) {
    pad.RegisterArea(0, kArea0, 1);
    pad.RegisterArea(1, kArea1, 1);
    std::vector<std::vector<uint8_t>> images;
    for (uint64_t now = 1; now <= boundaries; now++) {
        duty.Run(now);
        images.push_back(ImageOf(ram));
    }
    return images;
}
}  // namespace

static void TestNeutralWhenNoScript() {
    Ram ram;
    PadProtocol pad(WriterFor(ram));
    PadStampDuty duty(pad, nullptr, nullptr);
    const PadProtocol::Input in = duty.SampleFor(300);
    CHECK(in.buttons == 0 && in.lx == 0x80 && in.ly == 0x80 && in.rx == 0x80 && in.ry == 0x80, "no_script_sample_is_neutral");
    Run(duty, pad, ram, 220);
    CHECK(ram.bytes[kArea0 + 0x70] == 0x06 && ram.bytes[kArea0 + 0x02] == 0xFF && ram.bytes[kArea0 + 0x03] == 0xFF,
          "neutral_ready_wire_is_ffff");
    CHECK(ram.bytes[kArea0 + 0x06] == 0x80 && ram.bytes[kArea0 + 0x07] == 0x80, "neutral_sticks_centred");
    CHECK(!duty.Replaying() && !duty.Recording(), "no_script_no_recorder_flags");
}

static void TestScriptedSampleAndRecord() {
    // START held 230..240, CROSS 235..250, LY pushed up (0x10) 236..238.
    ps2r::triage::InputLog script;
    const size_t accepted = script.ParseSpec("230-240:START,235-250:CROSS,236-238:LY=16");
    CHECK(accepted == 3, "script_three_tokens_accepted");
    Ram ram;
    PadProtocol pad(WriterFor(ram));
    ps2r::triage::InputLog recorder;
    PadStampDuty duty(pad, &script, &recorder);
    CHECK(duty.Replaying() && duty.Recording(), "flags_replaying_and_recording");
    const PadProtocol::Input at236 = duty.SampleFor(236);
    CHECK(at236.buttons == (ps2r::triage::InputLog::ButtonMask("START") | ps2r::triage::InputLog::ButtonMask("CROSS")) &&
              at236.ly == 16 && at236.lx == 0x80,
          "sample_236_start_cross_ly16");
    CHECK(duty.SampleFor(229).buttons == 0 && duty.SampleFor(251).buttons == 0, "sample_outside_ranges_neutral");
    auto images = Run(duty, pad, ram, 260);
    // Boundary 236 (READY since 214+1): the active-low word has START (bit 3)
    // and CROSS (bit 14) cleared; the pressure byte for CROSS (+0x0E) is 0xFF.
    const std::vector<uint8_t>& i236 = images[235];
    const uint16_t wire = static_cast<uint16_t>(i236[kArea0 + 2] | (i236[kArea0 + 3] << 8));
    CHECK(wire == static_cast<uint16_t>(~(ps2r::triage::InputLog::ButtonMask("START") | ps2r::triage::InputLog::ButtonMask("CROSS"))),
          "wire_236_start_cross_active_low");
    CHECK(i236[kArea0 + 0x0E] == 0xFF && i236[kArea0 + 0x08] == 0x00, "pressure_236_cross_ff_right_0");
    CHECK(i236[kArea0 + 0x07] == 16 && i236[kArea1 + 0x07] == 16, "ly_236_on_both_ports");
    duty.Flush(260);
    // The recorder's lines carry the three ranges (the recorder is per frame:
    // START 230-240, CROSS 235-250, LY=16 236-238).
    std::string joined;
    for (const std::string& l : duty.Lines()) joined += (joined.empty() ? "" : ",") + l;
    CHECK(joined.find("230-240:START") != std::string::npos, "recorded_start_range");
    CHECK(joined.find("235-250:CROSS") != std::string::npos, "recorded_cross_range");
    CHECK(joined.find("236-238:LY=16") != std::string::npos, "recorded_ly_segment");
    std::printf("recorded: %s\n", joined.c_str());

    // Replay the recording through a fresh duty: identical images at every boundary.
    ps2r::triage::InputLog replay;
    replay.ParseSpec(joined);
    Ram ram2;
    PadProtocol pad2(WriterFor(ram2));
    PadStampDuty duty2(pad2, &replay, nullptr);
    auto images2 = Run(duty2, pad2, ram2, 260);
    bool identical = images.size() == images2.size();
    size_t first_diff = 0;
    for (size_t i = 0; identical && i < images.size(); i++)
        if (images[i] != images2[i]) { identical = false; first_diff = i + 1; }
    CHECK(identical, "record_then_replay_identical_slot_images_every_boundary");
    if (!identical) std::printf("first differing boundary: %zu\n", first_diff);
    CHECK(pad.Seq() == pad2.Seq() && pad.Stamps() == pad2.Stamps(), "record_then_replay_same_seq_and_stamp_count");

    // The script key is the duty's second argument, the stamp's tick its first: a
    // frame-clock script replayed at a wall-paced tick (900) indexes its frame (236).
    Ram ram3;
    PadProtocol pad3(WriterFor(ram3));
    ps2r::triage::InputLog recorder3;
    PadStampDuty duty3(pad3, &replay, &recorder3);
    const PadProtocol::Input at_frame = duty3.Run(900, 236);
    CHECK(at_frame.ly == 16, "script_indexed_by_key_not_tick");
    const PadProtocol::Input at_tick = duty3.Run(901, 900);
    CHECK(at_tick.ly == 0x80, "tick_outside_script_is_neutral");
    duty3.Flush(900);
    bool keyed_by_frame = false;
    for (const std::string& line : duty3.Lines()) if (line.rfind("236-", 0) == 0) keyed_by_frame = true;
    CHECK(keyed_by_frame, "recorder_fed_the_script_key");
}

int main() {
    TestNeutralWhenNoScript();
    TestScriptedSampleAndRecord();
    std::printf(g_fail ? "FAILED %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
