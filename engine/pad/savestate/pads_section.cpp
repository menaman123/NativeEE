#include "engine/pad/savestate/pads_section.h"

#include <cstdio>
#include <utility>

#include "engine/core/savestate/payload_reader.h"
#include "engine/ee/mem/main_memory.h"

using ps2r::savestate::Blob;
using ps2r::savestate::PayloadReader;
using ps2r::savestate::PutPod;
using ps2r::savestate::SectionSpec;

namespace {
// The original's press-latch record per port: one u32 read count, one u16
// latched-bit set, and one u32 read stamp per button bit (16 bits).
constexpr int kLatchBitsPerPort = 16;
// A pad area is two 128-byte slots.
constexpr uint32_t kAreaBytes = 2 * PadProtocol::kSlotBytes;
}  // namespace

static_assert(PadsSection::kLatchBytes ==
                  static_cast<size_t>(PadProtocol::kPorts) * (4 + 2 + kLatchBitsPerPort * 4),
              "the latch half is two read counts, two bit sets, 2 x 16 read stamps");

PadsSection::PadsSection(PadProtocol& protocol, Reader read32, SeqRestore seq_restore)
    : protocol_(protocol), read32_(std::move(read32)), seq_restore_(seq_restore) {}

void PadsSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec PadsSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_PADS;
    s.required = false;
    s.owner = "PadsSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [](const Blob& b, std::string* err) { return Validate(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    s.absent = [this] { SeedBootDefault(); };
    return s;
}

void PadsSection::SeedBootDefault() {
    for (int p = 0; p < PadProtocol::kPorts; p++) protocol_.RestorePort(p, 0, 0, PadProtocol::Stage::Disconn, false);
    protocol_.SetLastWire(0xFFFF);   // active low: nothing pressed
    protocol_.SetReadinessEvent(0);
    protocol_.SetSeq(PadProtocol::kSeqBoot);
    derived_seq_ = 0;
}

void PadsSection::Serialize(std::vector<uint8_t>& out) const {
    out.reserve(out.size() + kPayloadBytes);
    for (int p = 0; p < PadProtocol::kPorts; p++) PutPod<uint32_t>(out, protocol_.Area(p));
    for (int p = 0; p < PadProtocol::kPorts; p++) PutPod<uint32_t>(out, static_cast<uint32_t>(protocol_.OpenedAt(p)));
    for (int p = 0; p < PadProtocol::kPorts; p++) PutPod<uint8_t>(out, static_cast<uint8_t>(protocol_.StageOf(p)));
    PutPod<uint16_t>(out, protocol_.LastWire());
    for (int p = 0; p < PadProtocol::kPorts; p++) PutPod<uint8_t>(out, protocol_.ReadyStamped(p) ? 1 : 0);
    PutPod<uint32_t>(out, static_cast<uint32_t>(protocol_.ReadinessEvent()));
    // The press latch at its boot values: no reads counted, no bit latched,
    // no read stamps (see the header).
    for (int p = 0; p < PadProtocol::kPorts; p++) PutPod<uint32_t>(out, 0);
    for (int p = 0; p < PadProtocol::kPorts; p++) PutPod<uint16_t>(out, 0);
    for (int p = 0; p < PadProtocol::kPorts; p++)
        for (int b = 0; b < kLatchBitsPerPort; b++) PutPod<uint32_t>(out, 0);
}

bool PadsSection::Decode(const std::vector<uint8_t>& payload, Fields* f) {
    PayloadReader r(payload);
    for (int p = 0; p < PadProtocol::kPorts; p++)
        if (!r.Get(&f->area[p])) return false;
    for (int p = 0; p < PadProtocol::kPorts; p++)
        if (!r.Get(&f->opened_at[p])) return false;
    for (int p = 0; p < PadProtocol::kPorts; p++)
        if (!r.Get(&f->stage[p])) return false;
    if (!r.Get(&f->last_wire)) return false;
    for (int p = 0; p < PadProtocol::kPorts; p++)
        if (!r.Get(&f->ready_stamped[p])) return false;
    if (!r.Get(&f->readiness_event)) return false;
    return r.Offset() == kModelledBytes;
}

bool PadsSection::AreaPlausible(uint32_t area) {
    return area == 0 || MainMemory::Phys(area) <= MainMemory::kSize - kAreaBytes;
}

bool PadsSection::Validate(const std::vector<uint8_t>& payload, std::string* err) {
    if (payload.size() != kPayloadBytes) {
        if (err) *err = "PADS: payload is " + std::to_string(payload.size()) + " bytes, expected " + std::to_string(kPayloadBytes);
        return false;
    }
    Fields f;
    if (!Decode(payload, &f)) {
        if (err) *err = "PADS: short payload";
        return false;
    }
    for (int p = 0; p < PadProtocol::kPorts; p++) {
        if (f.stage[p] > static_cast<uint8_t>(PadProtocol::Stage::Ready)) {
            if (err) *err = "PADS: port " + std::to_string(p) + " stage " + std::to_string(f.stage[p]) + " is not DISCONN/CONFIG/READY";
            return false;
        }
        if (!AreaPlausible(f.area[p])) {
            if (err) {
                char hex[16];
                std::snprintf(hex, sizeof hex, "0x%08x", f.area[p]);
                *err = "PADS: port " + std::to_string(p) + " area " + hex + " is outside EE main RAM";
            }
            return false;
        }
    }
    // The 140 latch bytes are present by the length check and not inspected.
    return true;
}

bool PadsSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(payload, err)) return false;
    Fields f;
    if (!Decode(payload, &f)) return false;
    for (int p = 0; p < PadProtocol::kPorts; p++)
        protocol_.RestorePort(p, f.area[p], f.opened_at[p], static_cast<PadProtocol::Stage>(f.stage[p]),
                              f.ready_stamped[p] != 0);
    protocol_.SetLastWire(f.last_wire);
    protocol_.SetReadinessEvent(f.readiness_event);
    // The sequence counter: PADMAN's frame counter lives in the pad area the
    // memory sections restored ahead of this one (slot 0 = counter, slot 1 =
    // counter + 1). The protocol continues from the image's slot 0 word so the
    // next stamp publishes image + 1; the original's counter restarted at 1.
    derived_seq_ = 0;
    if (seq_restore_ == SeqRestore::Restart) {
        protocol_.SetSeq(PadProtocol::kSeqBoot);
    } else if (read32_) {
        for (int p = 0; p < PadProtocol::kPorts; p++) {
            if (f.area[p] == 0) continue;
            derived_seq_ = read32_(f.area[p] + PadProtocol::kSeqOffset);
            protocol_.SetSeq(derived_seq_);
            break;
        }
    }
    return true;
}
