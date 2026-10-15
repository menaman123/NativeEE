#include "engine/ee/savestate/timer_section.h"

#include "engine/ee/intc/ee_timers.h"
#include "engine/ee/intc/timer_codec.h"

using ps2r::savestate::Blob;
using ps2r::savestate::SectionSpec;

TimerSection::TimerSection(EeTimers& timers, uint16_t boot_t0_mode) : timers_(timers), boot_t0_mode_(boot_t0_mode) {}

void TimerSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec TimerSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_TIMR;
    s.required = false;
    s.owner = "TimerSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [](const Blob& b, std::string* err) { return Validate(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    s.absent = [this] { SeedBootDefault(); };
    return s;
}

void TimerSection::SeedBootDefault() { timers_.SeedBootDefault(boot_t0_mode_); }

void TimerSection::Serialize(std::vector<uint8_t>& out) const {
    TimerCodec::Regs snap[4];
    timers_.Snapshot(snap);
    TimerCodec::Encode(snap, out);
}

bool TimerSection::Validate(const std::vector<uint8_t>& payload, std::string* err) {
    TimerCodec::Regs t[4];
    if (payload.size() != TimerCodec::kTimrPayloadLen || !TimerCodec::Decode(payload.data(), payload.size(), t)) {
        if (err) *err = "TIMR: bad payload length / encoding";
        return false;
    }
    return true;
}

bool TimerSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(payload, err)) return false;
    TimerCodec::Regs t[4];
    TimerCodec::Decode(payload.data(), payload.size(), t);
    timers_.Restore(t);
    return true;
}
