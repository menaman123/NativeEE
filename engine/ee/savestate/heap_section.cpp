#include "engine/ee/savestate/heap_section.h"

#include <utility>

#include "engine/core/savestate/payload_reader.h"

using ps2r::savestate::Blob;
using ps2r::savestate::PayloadReader;
using ps2r::savestate::SectionSpec;

namespace {

void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    const uint8_t b[4] = {static_cast<uint8_t>(x), static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x >> 16),
                          static_cast<uint8_t>(x >> 24)};
    v.insert(v.end(), b, b + 4);
}

// The original grammar's sanity cap on either list (its reader rejects more).
constexpr uint32_t kMaxListEntries = 1000000;

}  // namespace

HeapSection::HeapSection(TailCodec tail) : tail_(std::move(tail)) {}

void HeapSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec HeapSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_HEAP;
    s.version = 1;
    s.required = false;
    s.owner = "HeapSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [this](const Blob& b, std::string* err) { return Validate(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    return s;
}

bool HeapSection::PrefixLength(const std::vector<uint8_t>& payload, size_t* len, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    PayloadReader r(payload);
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        if (!r.Get(&v)) return fail("HEAP: truncated heap bounds");
    for (int list = 0; list < 2; list++) {
        uint32_t n = 0;
        if (!r.Get(&n) || n > kMaxListEntries) return fail("HEAP: bad block-list count");
        if (!r.Skip(static_cast<size_t>(n) * 8)) return fail("HEAP: truncated block list");
    }
    if (len) *len = r.Offset();
    return true;
}

void HeapSection::EmitEmptyPrefix(std::vector<uint8_t>& out) {
    for (int i = 0; i < 4; i++) PutU32(out, 0);  // heap_start, heap_end, current_top, current_bottom
    PutU32(out, 0);                               // n_free
    PutU32(out, 0);                               // n_alloc
}

void HeapSection::Serialize(std::vector<uint8_t>& out) const {
    EmitEmptyPrefix(out);
    if (tail_.serialize) tail_.serialize(out);
}

bool HeapSection::Validate(const std::vector<uint8_t>& payload, std::string* err) const {
    size_t prefix = 0;
    if (!PrefixLength(payload, &prefix, err)) return false;
    size_t consumed = 0;
    if (tail_.validate && !tail_.validate(payload.data() + prefix, payload.size() - prefix, &consumed, err)) return false;
    if (prefix + consumed != payload.size()) {
        if (err) *err = "HEAP: trailing bytes after the arena bookkeeping";
        return false;
    }
    return true;
}

bool HeapSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(payload, err)) return false;
    size_t prefix = 0;
    PrefixLength(payload, &prefix, nullptr);
    size_t consumed = 0;
    if (tail_.restore && !tail_.restore(payload.data() + prefix, payload.size() - prefix, &consumed)) {
        if (err) *err = "HEAP: arena bookkeeping restore failed";
        return false;
    }
    restored_++;
    return true;
}
