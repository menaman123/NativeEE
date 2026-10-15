#pragma once
// Image analysis of one VU1 micro memory against a family signature: the
// whole-image hash, the extent (last non-zero bundle), the prefix / variant
// hashes split at `variant_base`, the family-prefix judgement and the
// dispatch chain.
//
// Rules: the family prefix hash is LEARNED from the first image whose word 0
// equals the signature's dispatcher op (per analyzer instance; one instance
// per process reproduces the catalog's `prefix=` column). The chain walk
// follows the family dispatcher: vi14 = data[3].x; each node QW holds the
// callee pc in 8-byte units (.x) and the next node (.z); it terminates when
// a callee is one of the exit stubs, after 64 steps, or on a pc past micro
// memory. With a loaded-program catalog the layout walk identifies, from
// variant_base, each program whose code hash matches at the current position
// and advances by its size until none matches; layout_hash covers exactly
// those bytes (the stale tail past them is excluded).
#include <cstdint>
#include <string>

#include "engine/vu/hle/vu1_family_signature.h"
#include "engine/vu/hle/vu1_hle_contract.h"
#include "engine/vu/hle/vu1_library_entry.h"

class Vu1ImageAnalyzer {
public:
    explicit Vu1ImageAnalyzer(const Vu1FamilySignature& signature);
    // With the title's loaded-program catalog (layout identification on).
    explicit Vu1ImageAnalyzer(const Vu1ProgramLibrary& library);

    // `data` may be null (no chain walk).
    Vu1ImageInfo Analyze(const uint8_t* micro, const uint8_t* data);

    // The catalog's short form: the upper 32 bits as 8 hex digits.
    static std::string ShortHash(uint64_t h);

    bool LooksLikeFamilyDispatcher(const uint8_t* micro) const;

private:
    Vu1FamilySignature sig_;
    const Vu1LoadedProgram* programs_ = nullptr;
    size_t program_count_ = 0;
    uint64_t learned_prefix_ = 0;
};
