#pragma once
// The AOT tier's verification gate: an image is Verified only when, on EVERY
// corpus case of that image, the native run (the AOT tier with the image
// assumed verified) and the interpreter run on fresh units leave identical
// registers (the whole 608-byte file, TPC included), data memory, VU clock,
// bundle count and kicked PATH1 bytes (every packet the arbiter accepted,
// hashed in order). Any divergence refuses the image and names its first
// differing field. No corpus case -> not verified (the caller never asks).
// Pure engine code: the cases arrive in memory; the directory walk and the
// manifest are the composition root's (app/host/vu1_aot_cache.*).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Vu1Case;

class Vu1AotVerifier {
public:
    struct Result {
        bool identical = false;
        size_t cases = 0;            // cases compared
        uint64_t image_hash = 0;     // the image every case must carry (0 when the set is empty / mixed)
        uint64_t corpus_hash = 0;    // FNV-1a over the cases' whole records, in the given order
        uint64_t bundles = 0;        // bundles retired by the native legs (the census)
        std::string divergence;      // "" when identical; else "<case index>: <first differing field>"
    };

    // Compare every case; `cases` must all carry the same image (a mixed set is
    // refused with divergence "mixed images"). An empty set is not identical.
    static Result Verify(const std::vector<const Vu1Case*>& cases);

    // The image hash of a case (VuImageHash::Fast over its micro memory).
    static uint64_t ImageHashOf(const Vu1Case& c);
    // The corpus hash of a case set (what the manifest records).
    static uint64_t CorpusHashOf(const std::vector<const Vu1Case*>& cases);

private:
    struct Outcome;
    static void RunLeg(const Vu1Case& c, bool native, Outcome& out);
    static bool Same(const Outcome& native, const Outcome& interp, std::string& field);
};
