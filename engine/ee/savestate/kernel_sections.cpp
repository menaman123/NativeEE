#include "engine/ee/savestate/kernel_sections.h"

#include <memory>

#include "engine/core/savestate/payload_reader.h"
#include "engine/ee/hle/event_flags.h"
#include "engine/ee/hle/semaphores.h"

using ps2r::savestate::Blob;
using ps2r::savestate::PayloadReader;
using ps2r::savestate::SectionSpec;

namespace {

void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    const uint8_t b[4] = {static_cast<uint8_t>(x), static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x >> 16),
                          static_cast<uint8_t>(x >> 24)};
    v.insert(v.end(), b, b + 4);
}
void PutI32(std::vector<uint8_t>& v, int32_t x) { PutU32(v, static_cast<uint32_t>(x)); }

// Restored ids first (file order, still present), then the rest ascending.
template <typename Table>
std::vector<typename Table::value_type> Ordered(const Table& table, const std::vector<int>& order) {
    std::vector<typename Table::value_type> out;
    for (int id : order)
        for (const auto& e : table)
            if (e.first == id) out.push_back(e);
    for (const auto& e : table) {
        bool seen = false;
        for (int id : order) seen = seen || id == e.first;
        if (!seen) out.push_back(e);
    }
    return out;
}

bool CountOf(PayloadReader& r, uint32_t max, uint32_t* count, size_t record_bytes, std::string* err,
             const char* what) {
    if (!r.Get(count) || *count > max || r.Remaining() != static_cast<size_t>(*count) * record_bytes) {
        if (err) *err = std::string(what) + ": bad count / payload length";
        return false;
    }
    return true;
}

}  // namespace

KernelSections::KernelSections(Semaphores& semas, EventFlags& flags) : semas_(semas), flags_(flags) {}

void KernelSections::Register(ps2r::savestate::SectionRegistry& registry) {
    registry.Register(SemaSpec());
    registry.Register(EvflSpec());
}

SectionSpec KernelSections::SemaSpec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_SEMA;
    // required: STRICTER than the original, which Find()'d SEMA/EVFL/INTC and
    // dereferenced the result unchecked (only THRD had a presence test); every
    // original writer emitted all three, so a file without them is malformed.
    s.owner = "KernelSections/SEMA";
    s.serialize = [this](std::vector<uint8_t>& out) { SerializeSema(out); };
    s.validate = [](const Blob& b, std::string* err) { return ValidateSema(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return RestoreSema(b.payload, err); };
    return s;
}

SectionSpec KernelSections::EvflSpec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_EVFL;
    s.owner = "KernelSections/EVFL";
    s.serialize = [this](std::vector<uint8_t>& out) { SerializeEvfl(out); };
    s.validate = [](const Blob& b, std::string* err) { return ValidateEvfl(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return RestoreEvfl(b.payload, err); };
    return s;
}

void KernelSections::SerializeSema(std::vector<uint8_t>& out) const {
    const auto table = Ordered(semas_.SnapshotTable(), sema_order_);
    PutU32(out, static_cast<uint32_t>(table.size()));
    for (const auto& e : table) {
        const SemaInfo& s = *e.second;
        PutI32(out, e.first);
        PutI32(out, s.count); PutI32(out, s.max_count); PutI32(out, s.init_count);
        PutU32(out, s.attr); PutU32(out, s.option);
    }
}

void KernelSections::SerializeEvfl(std::vector<uint8_t>& out) const {
    const auto table = Ordered(flags_.SnapshotTable(), evfl_order_);
    PutU32(out, static_cast<uint32_t>(table.size()));
    for (const auto& e : table) {
        const EventFlagInfo& f = *e.second;
        PutI32(out, e.first);
        PutU32(out, f.attr); PutU32(out, f.option); PutU32(out, f.init_bits); PutU32(out, f.bits);
    }
}

bool KernelSections::ValidateSema(const std::vector<uint8_t>& payload, std::string* err) {
    PayloadReader r(payload);
    uint32_t count = 0;
    return CountOf(r, kMaxObjects, &count, 24, err, "SEMA");
}

bool KernelSections::ValidateEvfl(const std::vector<uint8_t>& payload, std::string* err) {
    PayloadReader r(payload);
    uint32_t count = 0;
    return CountOf(r, kMaxObjects, &count, 20, err, "EVFL");
}

bool KernelSections::RestoreSema(const std::vector<uint8_t>& payload, std::string* err) {
    if (!ValidateSema(payload, err)) return false;
    PayloadReader r(payload);
    uint32_t count = 0;
    r.Get(&count);
    std::vector<Semaphores::TableEntry> table;
    sema_order_.clear();
    for (uint32_t i = 0; i < count; i++) {
        int32_t sid = 0;
        auto s = std::make_shared<SemaInfo>();
        r.Get(&sid);
        r.Get(&s->count); r.Get(&s->max_count); r.Get(&s->init_count);
        r.Get(&s->attr); r.Get(&s->option);
        table.emplace_back(sid, std::move(s));
        sema_order_.push_back(sid);
    }
    semas_.InstallTable(table);
    return true;
}

bool KernelSections::RestoreEvfl(const std::vector<uint8_t>& payload, std::string* err) {
    if (!ValidateEvfl(payload, err)) return false;
    PayloadReader r(payload);
    uint32_t count = 0;
    r.Get(&count);
    std::vector<EventFlags::TableEntry> table;
    evfl_order_.clear();
    for (uint32_t i = 0; i < count; i++) {
        int32_t eid = 0;
        auto f = std::make_shared<EventFlagInfo>();
        r.Get(&eid);
        r.Get(&f->attr); r.Get(&f->option); r.Get(&f->init_bits); r.Get(&f->bits);
        table.emplace_back(eid, std::move(f));
        evfl_order_.push_back(eid);
    }
    flags_.InstallTable(table);
    return true;
}
