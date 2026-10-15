// Vu1Registry -- registration-order table + first-match lookup (rules in the header).
#include "engine/vu/hle/vu1_registry.h"

#include <algorithm>

Vu1Registry::Vu1Registry(const Vu1ProgramLibrary& library)
    : entries_(library.entries, library.entries + library.entry_count) {}

void Vu1Registry::Append(const Vu1LibraryEntry& entry) { entries_.push_back(entry); }

const Vu1LibraryEntry* Vu1Registry::Lookup(uint64_t image_hash, uint64_t variant_hash, uint32_t start_pc) const {
    return Lookup(image_hash, variant_hash, 0, start_pc);
}

const Vu1LibraryEntry* Vu1Registry::Lookup(const Vu1ImageInfo& info, uint32_t start_pc) const {
    return Lookup(info.image_hash, info.variant_hash, info.layout_hash, start_pc);
}

const Vu1LibraryEntry* Vu1Registry::Lookup(uint64_t image_hash, uint64_t variant_hash, uint64_t layout_hash,
                                           uint32_t start_pc) const {
    const Vu1LibraryEntry* by_variant = nullptr;
    const Vu1LibraryEntry* by_layout = nullptr;
    const Vu1LibraryEntry* wildcard = nullptr;
    for (const Vu1LibraryEntry& e : entries_) {
        if (e.start_pc != kVu1AnyEntry && e.start_pc != start_pc) continue;
        if (e.image_hash != 0 && e.image_hash == image_hash) return &e;
        if (e.image_hash != 0) continue;
        if (e.variant_hash != 0) {
            if (e.variant_hash == variant_hash && !by_variant) by_variant = &e;
            continue;
        }
        if (e.layout_hash != 0) {
            if (layout_hash != 0 && e.layout_hash == layout_hash && !by_layout) by_layout = &e;
            continue;
        }
        if (!wildcard) wildcard = &e;
    }
    if (by_variant) return by_variant;
    if (by_layout) return by_layout;
    return wildcard;
}

size_t Vu1Registry::LayoutEntries() const {
    size_t n = 0;
    for (const Vu1LibraryEntry& e : entries_) n += (e.image_hash == 0 && e.variant_hash == 0 && e.layout_hash != 0);
    return n;
}

size_t Vu1Registry::DistinctVariantHashes() const {
    std::vector<uint64_t> hashes;
    hashes.reserve(entries_.size());
    for (const Vu1LibraryEntry& e : entries_)
        if (e.variant_hash != 0) hashes.push_back(e.variant_hash);  // the layout / wildcard rows carry none
    std::sort(hashes.begin(), hashes.end());
    return (size_t)(std::unique(hashes.begin(), hashes.end()) - hashes.begin());
}
