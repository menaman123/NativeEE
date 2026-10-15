#pragma once
// The VU0 macro-program census registry: a title-injected table of known
// microprogram entry points (per unit and pc) with a disposition kind and an
// optional 16-byte fingerprint (the first two instruction bundles at the
// entry), plus the per-entry call counter the census reports from.
//
// Contract: NoteEntry(vu, pc, micro) is called once per MSCAL/MSCNT/VCALLMS
// entry; it returns what the caller should report -- the first call of a
// known or unknown entry, the sparse periodic call marks (1k / 10k / 100k /
// 1M), and a fingerprint mismatch (the live bytes differ from the table's --
// the title uploaded a different program to the pc), warned ONCE per (vu,
// pc). Nothing here dispatches: an HLE `kind` is a label; substitution is
// the fingerprint match of the substituting class (LibVu0SinCos). Zero
// title data lives here -- the table is handed in.
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Vu0Registry {
public:
    enum class Kind : uint8_t {
        Hle = 0,           // a host-side substitution exists (label only)
        Interpreted = 1,   // recognised; runs on the tiers
        Unimplemented = 2  // seen, not yet validated against ground truth
    };

    struct Descriptor {
        int vu_id;               // 0 or 1
        uint32_t entry_pc;       // byte address in micro memory
        const char* name;
        Kind kind;
        uint64_t fingerprint_lo;  // bytes [pc, pc+8) little-endian; 0/0 = unchecked
        uint64_t fingerprint_hi;  // bytes [pc+8, pc+16)
    };

    enum class Event : uint8_t {
        None = 0,      // nothing to report (a repeat call)
        FirstKnown,    // first call of a registered entry
        FirstUnknown,  // first call of an unregistered (vu, pc)
        Periodic,      // call number 1000 / 10000 / 100000 / 1000000
    };

    struct Note {
        Event event = Event::None;
        const Descriptor* descriptor = nullptr;  // null for an unknown entry
        uint64_t call_number = 0;
        uint64_t live_lo = 0, live_hi = 0;  // the 16 live bytes at pc
        // The registered fingerprint contradicts the live bytes (set on the
        // first contradicting call of this (vu, pc) only).
        bool fingerprint_mismatch = false;
    };

    void AddEntry(const Descriptor& d);
    void AddSubroutine(const Descriptor& d);

    const Descriptor* Lookup(int vu_id, uint32_t entry_pc) const;
    const Descriptor* LookupSubroutine(int vu_id, uint32_t target_pc) const;
    const std::vector<Descriptor>& Entries() const { return entries_; }
    const std::vector<Descriptor>& Subroutines() const { return subroutines_; }

    // Count one entry at `pc`; `micro` / `micro_size` = the unit's micro
    // memory (the fingerprint is read when pc + 16 <= micro_size).
    Note NoteEntry(int vu_id, uint32_t pc, const uint8_t* micro, size_t micro_size);

    static const char* KindName(Kind k);

private:
    static uint64_t Key(int vu_id, uint32_t pc) { return (static_cast<uint64_t>(vu_id) << 32) | pc; }

    std::vector<Descriptor> entries_;
    std::vector<Descriptor> subroutines_;
    std::unordered_map<uint64_t, uint64_t> call_counts_;
    std::unordered_set<uint64_t> fingerprint_warned_;
};
