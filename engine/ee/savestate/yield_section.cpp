#include "engine/ee/savestate/yield_section.h"

#include "engine/core/savestate/payload_reader.h"
#include "engine/ee/sched/ee_yield_point.h"

using ps2r::savestate::Blob;
using ps2r::savestate::PayloadReader;
using ps2r::savestate::SectionSpec;

namespace {
void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    const uint8_t b[4] = {static_cast<uint8_t>(x), static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x >> 16),
                          static_cast<uint8_t>(x >> 24)};
    v.insert(v.end(), b, b + 4);
}
}  // namespace

YieldSection::YieldSection(EeYieldPoint& point) : point_(point) {}

void YieldSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec YieldSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_YLDP;
    s.version = kVersion;
    s.required = false;   // a pre-v5 image restores; the process's residual stays (counted budget_absent)
    s.owner = "YieldSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [](const Blob& b, std::string* err) { return Validate(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    s.absent = [this] { point_.NoteBudgetAbsent(); };
    return s;
}

void YieldSection::Serialize(std::vector<uint8_t>& out) const {
    PutU32(out, point_.Budget());
    PutU32(out, point_.BudgetRemaining());
}

bool YieldSection::Decode(const std::vector<uint8_t>& payload, uint32_t* budget, uint32_t* remaining) {
    if (payload.size() != kPayloadBytes) return false;
    PayloadReader r(payload);
    return r.Get(budget) && r.Get(remaining);
}

bool YieldSection::Validate(const std::vector<uint8_t>& payload, std::string* err) {
    uint32_t budget = 0, remaining = 0;
    if (!Decode(payload, &budget, &remaining)) {
        if (err) *err = "YLDP: bad payload length";
        return false;
    }
    if (!budget || !remaining || remaining > budget) {
        if (err) *err = "YLDP: back-edge budget remaining " + std::to_string(remaining) + " outside 1.." + std::to_string(budget);
        return false;
    }
    return true;
}

bool YieldSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(payload, err)) return false;
    uint32_t budget = 0, remaining = 0;
    Decode(payload, &budget, &remaining);
    if (budget != point_.Budget()) {
        if (err) *err = "YLDP: saved back-edge budget " + std::to_string(budget) + " differs from the live " +
                        std::to_string(point_.Budget()) + " (a different field schedule)";
        return false;
    }
    point_.StageRestoredBudget(remaining);
    return true;
}
