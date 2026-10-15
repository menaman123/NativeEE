#include "engine/mc/savestate/mchl_section.h"

#include "engine/core/savestate/payload_reader.h"
#include "engine/mc/hle/mc_command_model.h"

using ps2r::savestate::Blob;
using ps2r::savestate::PayloadReader;
using ps2r::savestate::PutPod;
using ps2r::savestate::SectionSpec;

MchlSection::MchlSection(McCommandModel& model) : model_(model) {}

void MchlSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec MchlSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_MCHL;
    s.required = false;
    s.owner = "MchlSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [](const Blob& b, std::string* err) { return Validate(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    // Absent: the model keeps its boot state (nothing pending, no card seen).
    s.absent = [] {};
    return s;
}

void MchlSection::Serialize(std::vector<uint8_t>& out) const {
    out.reserve(out.size() + kPayloadBytes);
    PutPod<int32_t>(out, model_.PendingCmd());
    PutPod<int32_t>(out, model_.PendingResult());
    for (uint32_t port = 0; port < static_cast<uint32_t>(McCommandModel::kPorts); port++)
        for (uint32_t slot = 0; slot < static_cast<uint32_t>(McCommandModel::kSlots); slot++)
            PutPod<uint8_t>(out, model_.Seen(port, slot) ? 1 : 0);
}

bool MchlSection::Validate(const std::vector<uint8_t>& payload, std::string* err) {
    if (payload.size() != kPayloadBytes) {
        if (err) *err = "MCHL: payload is " + std::to_string(payload.size()) + " bytes, expected " + std::to_string(kPayloadBytes);
        return false;
    }
    return true;
}

bool MchlSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(payload, err)) return false;
    PayloadReader r(payload);
    int32_t cmd = 0, result = 0;
    uint8_t seen[McCommandModel::kPorts][McCommandModel::kSlots] = {};
    if (!r.Get(&cmd) || !r.Get(&result)) return false;
    for (int port = 0; port < McCommandModel::kPorts; port++)
        for (int slot = 0; slot < McCommandModel::kSlots; slot++)
            if (!r.Get(&seen[port][slot])) return false;
    model_.SetPending(cmd, result);
    for (uint32_t port = 0; port < static_cast<uint32_t>(McCommandModel::kPorts); port++)
        for (uint32_t slot = 0; slot < static_cast<uint32_t>(McCommandModel::kSlots); slot++)
            model_.SetSeen(port, slot, seen[port][slot] != 0);
    return true;
}
