#pragma once
// One registered native program (keyed by image or variant hash + entry pc)
// and the per-title library the engine dispatcher is handed: the family
// signature plus the entry table in REGISTRATION ORDER (the lookup's
// first-match rule makes the order observable). Zero primary types.
#include <cstddef>
#include <cstdint>

#include "engine/vu/hle/vu1_family_signature.h"
#include "engine/vu/hle/vu1_hle_contract.h"

// `start_pc` value meaning "any entry".
constexpr uint32_t kVu1AnyEntry = UINT32_MAX;

struct Vu1LibraryEntry {
    uint64_t image_hash;     // exact whole-image key, or 0 = variant-keyed
    uint64_t variant_hash;   // hash of micro[variant_base .. extent), or 0 = any
    const char* name;        // program name (the catalog's `hle` column)
    uint32_t start_pc;       // required entry pc, or kVu1AnyEntry
    Vu1HleFn fn;             // native program; may DECLINE
    const uint32_t* oracle_case_ids;  // corpus cases proving this entry (may be null)
    size_t oracle_case_count;
    // Layout key (Vu1ImageInfo::layout_hash): the hash of the loaded programs
    // alone, consulted after the image and variant keys. 0 = not layout-keyed.
    // Trailing so the positional rows above need no change.
    uint64_t layout_hash = 0;
};

// One loadable microprogram of the title (a program the title uploads into
// the region past variant_base when a material needs it): the hash of its
// code bytes and its size. The analyzer identifies the programs an image
// carries by these; the engine holds no code bytes.
struct Vu1LoadedProgram {
    uint64_t code_hash;   // VuImageHash::Fast over the program's size_bytes
    uint32_t size_bytes;  // the upload size (bundles * 8)
    const char* name;
};

struct Vu1ProgramLibrary {
    Vu1FamilySignature family;
    const Vu1LibraryEntry* entries;
    size_t entry_count;
    // The loaded-program catalog (may be null / 0: no layout identification).
    const Vu1LoadedProgram* programs = nullptr;
    size_t program_count = 0;
};
