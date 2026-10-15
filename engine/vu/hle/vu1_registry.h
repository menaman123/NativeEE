#pragma once
// The VU1 HLE registry: the title library's entries in registration order plus
// the lookup rule. Lookup(image, variant, layout, pc): entries whose `start_pc`
// pins a different entry are skipped; the FIRST exact image match wins
// immediately; otherwise the first variant match; otherwise the first layout
// match (the loaded programs alone, stale tail excluded); otherwise the first
// wildcard (every key 0 -- the harness reference program). `Append` exists for
// that reference, which a tool registers after the library.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/vu/hle/vu1_library_entry.h"

class Vu1Registry {
public:
    explicit Vu1Registry(const Vu1ProgramLibrary& library);

    void Append(const Vu1LibraryEntry& entry);
    const Vu1LibraryEntry* Lookup(uint64_t image_hash, uint64_t variant_hash, uint32_t start_pc) const;
    const Vu1LibraryEntry* Lookup(uint64_t image_hash, uint64_t variant_hash, uint64_t layout_hash,
                                  uint32_t start_pc) const;
    const Vu1LibraryEntry* Lookup(const Vu1ImageInfo& info, uint32_t start_pc) const;

    const std::vector<Vu1LibraryEntry>& Entries() const { return entries_; }
    size_t DistinctVariantHashes() const;
    size_t LayoutEntries() const;

private:
    std::vector<Vu1LibraryEntry> entries_;
};
