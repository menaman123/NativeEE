#pragma once
// MemorySections: the RAM0 / SPAD / META section providers of the .ps2st
// schema over the Phase-3 memory owners -- MainMemory (32 MB), Scratchpad
// (16 KB) and the one extra region the title registers on the AddressMap
// (the META span; a title without it registers a zero-length span and the
// META section is then absent from the file). Payloads are the raw bytes;
// validate = exact size match (restore copies blindly, as OLD did).
//
// Spans() returns the same three spans in MemoryCheckpointer order, so
// mem.mhash after a restore hashes exactly the bytes the file carried
// (PHASE3_PLAN section 4.3, restore.mhash).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/core/verify/memory_checkpointer.h"

class MainMemory;
class Scratchpad;

class MemorySections {
public:
    MemorySections(MainMemory& ram, Scratchpad& spad, uint8_t* meta, size_t meta_size);

    // RAM0, SPAD, then META (registration order = file order).
    void Register(ps2r::savestate::SectionRegistry& registry);
    std::vector<ps2r::verify::MemorySpan> Spans() const;
    uint64_t Hash() const { return ps2r::verify::MemoryCheckpointer::Hash(Spans()); }

private:
    struct Span { uint8_t* data; size_t size; };
    Span SpanFor(uint32_t tag) const;
    ps2r::savestate::SectionSpec SpecFor(uint32_t tag, const char* owner) const;

    MainMemory& ram_;
    Scratchpad& spad_;
    uint8_t* meta_;
    size_t meta_size_;
};
