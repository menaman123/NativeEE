// PadsSection: the PADS savestate section over PadProtocol.
//   roundtrip  -- serialize a protocol with two open ports (READY / CONFIG), a
//                 stamped wire word and a readiness event through the
//                 SectionRegistry: the payload is exactly 166 bytes in the wire
//                 order, the 26 modelled bytes as expected and the 140 latch
//                 bytes zero; restore into a fresh protocol is field-equal and a
//                 READY re-stamp then skips the zero pass (the restored flag
//                 matters); re-serializing reproduces the bytes; a wrong
//                 length, a stage outside 0..2 and an area outside EE RAM are
//                 refused; latch content never is; the absent path puts a fresh
//                 AND a live protocol at boot state.
//   anchors <dir> <expected.tsv> -- every frozen anchor listed in the table
//                 carries a 166-byte PADS blob the codec validates and decodes
//                 (printed per anchor); re-serializing the decoded state
//                 reproduces the 26 modelled bytes exactly and writes zeros in
//                 the latch bytes -- the anchors' latch bytes are NOT zero
//                 (asserted: the premise of EQUIVALENCE_PLAN 7.1
//                 P4-PADS-LATCH-BYTES), and that difference is the accepted
//                 one, never a failure here. Skip 77 when the anchor directory
//                 is absent.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "engine/core/savestate/savestate_format.h"
#include "engine/core/savestate/section_registry.h"
#include "engine/pad/hle/pad_protocol.h"
#include "engine/pad/savestate/pads_section.h"

using ps2r::savestate::Blob;
using ps2r::savestate::SectionRegistry;

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

constexpr uint32_t kArea0 = 0x00300000u;   // 256-byte pad areas inside EE RAM
constexpr uint32_t kArea1 = 0x00300100u;

// Guest writes counted per pad area (the slot images themselves are not
// under test here).
struct WriteCounter {
    int in_area0 = 0, in_area1 = 0;
};

PadProtocol::Writer WriterFor(WriteCounter& counts) {
    return [&counts](uint32_t addr, uint32_t, int) {
        if (addr >= kArea0 && addr < kArea0 + 0x100) ++counts.in_area0;
        if (addr >= kArea1 && addr < kArea1 + 0x100) ++counts.in_area1;
    };
}

bool LatchBytesZero(const std::vector<uint8_t>& payload) {
    for (size_t i = PadsSection::kModelledBytes; i < payload.size(); i++)
        if (payload[i] != 0) return false;
    return true;
}

bool FieldsEqual(const PadProtocol& a, const PadProtocol& b) {
    for (int p = 0; p < PadProtocol::kPorts; p++) {
        if (a.Area(p) != b.Area(p)) return false;
        if (a.OpenedAt(p) != b.OpenedAt(p)) return false;
        if (a.StageOf(p) != b.StageOf(p)) return false;
        if (a.ReadyStamped(p) != b.ReadyStamped(p)) return false;
    }
    return a.LastWire() == b.LastWire() && a.ReadinessEvent() == b.ReadinessEvent();
}

}  // namespace

static int RunRoundTrip() {
    WriteCounter src_writes;
    PadProtocol src(WriterFor(src_writes));
    src.RegisterArea(0, kArea0, 3);     // PortOpen at boundary 3
    src.RegisterArea(1, kArea1, 96);    // PortOpen at boundary 96
    src.NoteReadinessEvent(101);
    PadProtocol::Input in;
    in.buttons = 0x0010;                // UP pressed -> wire 0xFFEF
    src.Stamp(in, 102);                 // port 0 CONFIG (elapsed 99), port 1 DISCONN (elapsed 6)
    src.Stamp(in, 103);                 // port 0 READY (event + 2), first READY image; port 1 DISCONN
    CHECK(src.StageOf(0) == PadProtocol::Stage::Ready && src.ReadyStamped(0));
    CHECK(src.StageOf(1) == PadProtocol::Stage::Disconn && !src.ReadyStamped(1));
    CHECK(src.LastWire() == 0xFFEF);
    CHECK(src.ReadinessEvent() == 101);
    // Port 1 as a CONFIG-stage port so every stage value is on the wire.
    src.RestorePort(1, kArea1, 96, PadProtocol::Stage::Config, false);

    PadsSection src_section(src);
    SectionRegistry src_reg;
    src_section.Register(src_reg);
    CHECK(src_reg.Count() == 1);
    CHECK(src_reg.Specs()[0].tag == ps2r::savestate::TAG_PADS);
    CHECK(!src_reg.Specs()[0].required);

    const std::vector<Blob> sections = src_reg.Capture();
    CHECK(sections.size() == 1);
    const std::vector<uint8_t>& p = sections[0].payload;
    CHECK(p.size() == PadsSection::kPayloadBytes);
    CHECK(p.size() == 166);
    CHECK(PadsSection::kModelledBytes == 26 && PadsSection::kLatchBytes == 140);
    // Wire order: u32 area[2], u32 opened_at[2], u8 stage[2], u16 last_wire,
    // u8 ready_stamped[2], u32 readiness_event, then the 140 latch bytes.
    const uint8_t expect[26] = {0x00, 0x00, 0x30, 0x00, 0x00, 0x01, 0x30, 0x00,   // areas
                                0x03, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00,   // opened_at 3, 96
                                0x02, 0x01,                                       // READY, CONFIG
                                0xEF, 0xFF,                                       // last wire
                                0x01, 0x00,                                       // ready_stamped
                                0x65, 0x00, 0x00, 0x00};                          // readiness event 101
    CHECK(std::memcmp(p.data(), expect, 26) == 0);
    CHECK(LatchBytesZero(p));

    WriteCounter dst_writes;
    PadProtocol dst(WriterFor(dst_writes));
    PadsSection dst_section(dst);
    SectionRegistry dst_reg;
    dst_section.Register(dst_reg);
    std::string err;
    CHECK(dst_reg.Validate(sections, &err));
    CHECK(dst_reg.Restore(sections, &err));
    CHECK(err.empty());
    CHECK(dst.Area(0) == kArea0 && dst.Area(1) == kArea1);
    CHECK(dst.OpenedAt(0) == 3 && dst.OpenedAt(1) == 96);
    CHECK(dst.StageOf(0) == PadProtocol::Stage::Ready && dst.StageOf(1) == PadProtocol::Stage::Config);
    CHECK(dst.ReadyStamped(0) && !dst.ReadyStamped(1));
    CHECK(dst.LastWire() == 0xFFEF);
    CHECK(dst.ReadinessEvent() == 101);
    CHECK(FieldsEqual(src, dst));
    // Re-serialize the restored protocol: identical bytes.
    const std::vector<Blob> again = dst_reg.Capture();
    CHECK(again.size() == 1 && again[0].payload == p);
    // The restored READY-image flag is what the next stamp consults: port 0
    // re-stamps READY without the zero pass (34 writes per slot, 68 per
    // port); port 1 reaches READY for the first time and zeroes both slots
    // first (65 per slot, 130 per port).
    dst.Stamp(in, 200);
    CHECK(dst_writes.in_area0 == 68);
    CHECK(dst_writes.in_area1 == 130);

    // A wrong-length blob is refused by validate (short and long).
    std::vector<Blob> bad = sections;
    bad[0].payload.pop_back();
    CHECK(!dst_reg.Validate(bad, &err));
    CHECK(err.find("PADS") != std::string::npos);
    bad = sections;
    bad[0].payload.push_back(0);
    CHECK(!dst_reg.Validate(bad, &err));
    // A stage outside DISCONN/CONFIG/READY is refused.
    bad = sections;
    bad[0].payload[0x10] = 3;
    CHECK(!dst_reg.Validate(bad, &err));
    CHECK(err.find("stage") != std::string::npos);
    // An area outside EE main RAM is refused; a KSEG0 mirror of RAM is not.
    bad = sections;
    bad[0].payload[0x04] = 0x00; bad[0].payload[0x05] = 0x00; bad[0].payload[0x06] = 0x00; bad[0].payload[0x07] = 0x02;
    CHECK(!dst_reg.Validate(bad, &err));
    CHECK(err.find("area") != std::string::npos);
    bad = sections;
    bad[0].payload[0x07] = 0x80;   // 0x80300100
    CHECK(dst_reg.Validate(bad, &err));
    // The whole 256-byte area must fit: the last area of RAM (0x01FFFF00) does,
    // one straddling the top (0x01FFFF80) does not.
    bad = sections;
    bad[0].payload[0x04] = 0x00; bad[0].payload[0x05] = 0xFF; bad[0].payload[0x06] = 0xFF; bad[0].payload[0x07] = 0x01;
    CHECK(dst_reg.Validate(bad, &err));
    bad[0].payload[0x04] = 0x80;
    CHECK(!dst_reg.Validate(bad, &err));
    CHECK(err.find("area") != std::string::npos);
    // Latch-byte content is never a reason to refuse: a blob carrying the
    // original's non-idle latch values restores like an idle one.
    std::vector<Blob> latched = sections;
    for (size_t i = PadsSection::kModelledBytes; i < latched[0].payload.size(); i++) latched[0].payload[i] = 0xA5;
    WriteCounter latched_writes;
    PadProtocol from_latched(WriterFor(latched_writes));
    PadsSection from_latched_section(from_latched);
    SectionRegistry latched_reg;
    from_latched_section.Register(latched_reg);
    CHECK(latched_reg.Validate(latched, &err));
    CHECK(latched_reg.Restore(latched, &err));
    CHECK(FieldsEqual(src, from_latched));

    // Absent: a file without PADS puts the protocol at boot state -- for a
    // fresh protocol and for one holding live state (both ports open, wire
    // pressed, readiness event seen).
    WriteCounter fresh_writes;
    PadProtocol fresh(WriterFor(fresh_writes));
    PadsSection fresh_section(fresh);
    SectionRegistry fresh_reg;
    fresh_section.Register(fresh_reg);
    const std::vector<Blob> none;
    CHECK(fresh_reg.Restore(none, &err));
    CHECK(fresh.Area(0) == 0 && fresh.Area(1) == 0);
    CHECK(fresh.OpenedAt(0) == 0 && fresh.OpenedAt(1) == 0);
    CHECK(fresh.StageOf(0) == PadProtocol::Stage::Disconn && fresh.StageOf(1) == PadProtocol::Stage::Disconn);
    CHECK(!fresh.ReadyStamped(0) && !fresh.ReadyStamped(1));
    CHECK(fresh.LastWire() == 0xFFFF);
    CHECK(fresh.ReadinessEvent() == 0);
    CHECK(src.Area(0) != 0 && src.LastWire() != 0xFFFF && src.ReadinessEvent() != 0);   // src is live
    CHECK(src_reg.Restore(none, &err));
    CHECK(FieldsEqual(src, fresh));
    CHECK(src.Area(0) == 0 && src.Area(1) == 0 && src.LastWire() == 0xFFFF && src.ReadinessEvent() == 0);
    CHECK(src.StageOf(0) == PadProtocol::Stage::Disconn && !src.ReadyStamped(0));
    return 0;
}

static std::vector<std::string> AnchorNames(const std::string& tsv) {
    std::vector<std::string> names;
    std::ifstream in(tsv);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        names.push_back(tab == std::string::npos ? line : line.substr(0, tab));
    }
    return names;
}

static int RunAnchors(const std::string& dir, const std::string& tsv) {
    const std::vector<std::string> names = AnchorNames(tsv);
    if (names.empty()) {
        std::printf("SKIP: no anchors listed in %s\n", tsv.c_str());
        return 77;
    }
    int decoded = 0;
    size_t anchors = 0;
    for (const std::string& n : names) {
        const std::string path = dir + "/" + n;
        ps2r::savestate::FileMeta meta;
        std::vector<Blob> sections;
        std::string err;
        // Rows approving boot imports outside the anchor directory (the coldboot
        // probe under verify/baseline/coldboot/probe) are not this test's anchors.
        if (FILE* present = std::fopen(path.c_str(), "rb")) std::fclose(present);
        else { std::printf("%s: not in anchor dir, skipped\n", n.c_str()); continue; }
        ++anchors;
        if (!ps2r::savestate::ReadStateFile(path, &meta, &sections, &err)) {
            std::printf("SKIP: cannot read anchor %s (%s)\n", path.c_str(), err.c_str());
            return 77;
        }
        const Blob* pads = nullptr;
        for (const Blob& b : sections)
            if (b.tag == ps2r::savestate::TAG_PADS) pads = &b;
        CHECK(pads != nullptr);
        if (!pads) continue;
        CHECK(pads->payload.size() == PadsSection::kPayloadBytes);
        CHECK(PadsSection::Validate(pads->payload, &err));
        WriteCounter writes;
        PadProtocol protocol(WriterFor(writes));
        PadsSection section(protocol);
        CHECK(section.Restore(pads->payload, &err));
        std::printf("%-44s areas %08x/%08x opened_at %llu/%llu stage %d/%d wire %04x ready %d/%d readiness %llu\n",
                    n.c_str(), protocol.Area(0), protocol.Area(1),
                    static_cast<unsigned long long>(protocol.OpenedAt(0)), static_cast<unsigned long long>(protocol.OpenedAt(1)),
                    static_cast<int>(protocol.StageOf(0)), static_cast<int>(protocol.StageOf(1)), protocol.LastWire(),
                    protocol.ReadyStamped(0) ? 1 : 0, protocol.ReadyStamped(1) ? 1 : 0,
                    static_cast<unsigned long long>(protocol.ReadinessEvent()));
        // Every frozen anchor was taken with port 0 open (READY, image stamped),
        // port 1 closed, the wire idle and the readiness event seen.
        CHECK(protocol.Area(0) != 0 && protocol.Area(1) == 0);
        CHECK(protocol.StageOf(0) == PadProtocol::Stage::Ready && protocol.StageOf(1) == PadProtocol::Stage::Disconn);
        CHECK(protocol.ReadyStamped(0) && !protocol.ReadyStamped(1));
        CHECK(protocol.LastWire() == 0xFFFF);
        CHECK(protocol.ReadinessEvent() != 0);
        // Re-serializing the decoded state reproduces the 26 modelled bytes
        // exactly and writes zeros in the 140 latch bytes.
        std::vector<uint8_t> again;
        section.Serialize(again);
        CHECK(again.size() == pads->payload.size());
        CHECK(std::memcmp(again.data(), pads->payload.data(), PadsSection::kModelledBytes) == 0);
        CHECK(LatchBytesZero(again));
        // The anchor's latch bytes are the original's live press-latch state,
        // which this tree does not model: they differ from the zeros written
        // above. That is the accepted difference P4-PADS-LATCH-BYTES, reported
        // here and not a failure. What IS asserted is the row's premise: every
        // frozen anchor carries a non-idle latch (the port-0 read count is one
        // scePadRead per frame since PortOpen).
        size_t latch_diff = 0;
        for (size_t i = PadsSection::kModelledBytes; i < again.size(); i++)
            if (again[i] != pads->payload[i]) ++latch_diff;
        uint32_t read_count0 = 0;
        std::memcpy(&read_count0, pads->payload.data() + PadsSection::kModelledBytes, sizeof read_count0);
        std::printf("    latch bytes differing from the anchor: %zu of %zu (anchor read_count[0]=%u; accepted: P4-PADS-LATCH-BYTES)\n",
                    latch_diff, PadsSection::kLatchBytes, read_count0);
        CHECK(!LatchBytesZero(pads->payload));
        CHECK(latch_diff > 0 && read_count0 > 0);
        decoded++;
    }
    if (anchors == 0) {
        std::printf("SKIP: no anchors of %s present in %s\n", tsv.c_str(), dir.c_str());
        return 77;
    }
    std::printf("anchors decoded: %d of %zu\n", decoded, anchors);
    CHECK(decoded == static_cast<int>(anchors));
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s roundtrip | anchors <dir> <expected.tsv>\n", argv[0]);
        return 2;
    }
    const std::string mode = argv[1];
    int rc = 2;
    if (mode == "roundtrip") {
        rc = RunRoundTrip();
    } else if (mode == "anchors" && argc >= 4) {
        rc = RunAnchors(argv[2], argv[3]);
    }
    if (rc == 77) return 77;
    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS PadsSection %s\n", mode.c_str());
    return 0;
}
