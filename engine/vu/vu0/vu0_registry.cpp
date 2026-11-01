// Vu0Registry -- the VU0 macro-program census registry (rules in the header).
#include "engine/vu/vu0/vu0_registry.h"

#include <cstring>

void Vu0Registry::AddEntry(const Descriptor& d) {
    entries_.push_back(d);
    // The rows cache Lookup's answer: a new entry (or a reallocation of entries_) invalidates it.
    for (auto& rows : rows_)
        if (rows)
            for (uint32_t i = 0; i <= kRows; ++i) rows[i].descriptor_cached = false;
}

Vu0Registry::Row& Vu0Registry::RowFor(int vu_id, uint32_t pc) {
    const int v = vu_id == 1 ? 1 : 0;
    if (!rows_[v]) rows_[v].reset(new Row[kRows + 1]);
    return rows_[v][pc < kRows ? pc : kRows];
}

void Vu0Registry::AddSubroutine(const Descriptor& d) { subroutines_.push_back(d); }

const Vu0Registry::Descriptor* Vu0Registry::Lookup(int vu_id, uint32_t entry_pc) const {
    for (const Descriptor& d : entries_)
        if (d.vu_id == vu_id && d.entry_pc == entry_pc) return &d;
    return nullptr;
}

const Vu0Registry::Descriptor* Vu0Registry::LookupSubroutine(int vu_id, uint32_t target_pc) const {
    for (const Descriptor& d : subroutines_)
        if (d.vu_id == vu_id && d.entry_pc == target_pc) return &d;
    return nullptr;
}

const char* Vu0Registry::KindName(Kind k) {
    switch (k) {
        case Kind::Hle: return "HLE";
        case Kind::Interpreted: return "INTERP";
        case Kind::Unimplemented: return "UNIMPL";
    }
    return "?";
}

Vu0Registry::Note Vu0Registry::NoteEntry(int vu_id, uint32_t pc, const uint8_t* micro, size_t micro_size) {
    Note n;
    Row& row = RowFor(vu_id, pc);
    n.call_number = ++row.count;
    if (!row.descriptor_cached) {
        row.descriptor = Lookup(vu_id, pc);
        row.descriptor_cached = true;
    }
    n.descriptor = row.descriptor;
    if (micro != nullptr && static_cast<size_t>(pc) + 16 <= micro_size) {
        std::memcpy(&n.live_lo, micro + pc, 8);
        std::memcpy(&n.live_hi, micro + pc + 8, 8);
    }

    const bool first_call = n.call_number == 1;
    const bool periodic = n.call_number == 1000 || n.call_number == 10000 || n.call_number == 100000 ||
                          n.call_number == 1000000;
    if (n.descriptor == nullptr) {
        n.event = first_call ? Event::FirstUnknown : Event::None;
        return n;
    }

    if (first_call)
        n.event = Event::FirstKnown;
    else if (periodic)
        n.event = Event::Periodic;

    // A declared fingerprint that the live bytes contradict is reported once
    // per (vu, pc), independently of the call marks.
    const Descriptor& d = *n.descriptor;
    if ((d.fingerprint_lo | d.fingerprint_hi) != 0 && (n.live_lo != d.fingerprint_lo || n.live_hi != d.fingerprint_hi)) {
        n.fingerprint_mismatch = !row.fingerprint_warned;
        row.fingerprint_warned = true;
    }
    return n;
}
