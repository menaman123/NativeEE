#include "engine/ee/savestate/memory_sections.h"

#include <cstring>

#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"

using ps2r::savestate::Blob;
using ps2r::savestate::SectionSpec;

MemorySections::MemorySections(MainMemory& ram, Scratchpad& spad, uint8_t* meta, size_t meta_size)
    : ram_(ram), spad_(spad), meta_(meta), meta_size_(meta_size) {}

MemorySections::Span MemorySections::SpanFor(uint32_t tag) const {
    if (tag == ps2r::savestate::TAG_RAM) return {ram_.Data(), ram_.Size()};
    if (tag == ps2r::savestate::TAG_SPAD) return {spad_.Data(), spad_.Size()};
    return {meta_, meta_size_};
}

SectionSpec MemorySections::SpecFor(uint32_t tag, const char* owner) const {
    SectionSpec s;
    s.tag = tag;
    s.version = 1;
    s.required = true;
    s.owner = owner;
    s.serialize = [this, tag](std::vector<uint8_t>& out) {
        const Span sp = SpanFor(tag);
        out.insert(out.end(), sp.data, sp.data + sp.size);
    };
    s.validate = [this, tag, owner](const Blob& b, std::string* err) {
        const Span sp = SpanFor(tag);
        if (b.payload.size() == sp.size) return true;
        if (err) *err = std::string(owner) + ": payload size " + std::to_string(b.payload.size()) +
                        " != memory size " + std::to_string(sp.size);
        return false;
    };
    s.restore = [this, tag](const Blob& b, std::string*) {
        const Span sp = SpanFor(tag);
        if (b.payload.size() != sp.size) return false;
        std::memcpy(sp.data, b.payload.data(), sp.size);
        return true;
    };
    return s;
}

void MemorySections::Register(ps2r::savestate::SectionRegistry& registry) {
    registry.Register(SpecFor(ps2r::savestate::TAG_RAM, "RAM0"));
    registry.Register(SpecFor(ps2r::savestate::TAG_SPAD, "SPAD"));
    if (meta_ && meta_size_) registry.Register(SpecFor(ps2r::savestate::TAG_META, "META"));
}

std::vector<ps2r::verify::MemorySpan> MemorySections::Spans() const {
    std::vector<ps2r::verify::MemorySpan> v;
    v.push_back({ram_.Data(), ram_.Size()});
    v.push_back({spad_.Data(), spad_.Size()});
    v.push_back({meta_, meta_size_});
    return v;
}
