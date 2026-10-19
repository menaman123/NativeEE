#pragma once
// The VU1 AOT tier: the generic microprogram recompiler, generated at RUN
// time. A program image (keyed by VuImageHash::Fast over the unit's micro
// memory, the key every other table uses) runs NATIVE once it is VERIFIED --
// its block table re-resolved to the (kind, dest)-specialised bodies
// (VuCachedTier::UpperFastFnD / LowerFastFnD: the cached tier's instruction
// bodies monomorphised on the decoded slot's kind AND dest mask, so the lane
// loops, the lane-select table load and the MAC mask are constants of the
// code; the registers, immediates and hazard arms stay the block tier's
// pre-resolved facts) -- and until then on the FALLBACK tier, the block tier
// (proven == the interpreter by VuCachedVsInterpCorpus) or the interpreter.
//
// Verification is the user's rule: an image is Verified only when its native
// run equals the interpreter's run on EVERY case of the title's corpus that
// exercises that image (Vu1AotVerifier: registers, data memory, clock, bundle
// count, kicked PATH1 bytes); an image with no corpus case is NOT verified.
// The corpus is the engine's capture format (Vu1Case), collected at first run
// by the composition root through the kick observer below and persisted under
// the user data dir with the manifest (app/host/vu1_aot_cache.*: image hash,
// engine build id, corpus hash, status). Nothing is generated under the source
// tree: the first native form of an image is its specialised block table,
// built in memory from the image at first kick; the cache holds the
// verification verdicts (so a verified image goes native at its first kick of
// the next run) and, under the user data dir only, the generated libraries of
// the second form below.
//
// Above the specialised table sits the OPTIONAL generated code: once an image
// is Verified, Vu1AotBuilder (the composition root) emits one C++ function per
// block (Vu1AotEmitter: the block tier's RunOps with every slot a compile-time
// constant through these same bodies), compiles it with a host toolchain found
// at run time, loads the library, re-verifies the loaded functions on the
// image's corpus with the same gate, and hands the exports table to this tier
// (SetNativeExports). The VU thread installs the functions on the image's
// blocks at its next kick (InstallNative: never from another thread); the loop
// calls a block's function instead of RunOps. No compiler on the host = this
// step never happens and the specialised table is the native tier.
//
// Tier order: HLE (the title's library, if any; the dispatch's hook) -> AOT
// (verified) -> the fallback. The status map is written by the verifier
// (another thread) and read per kick: a seqlock-free memo keyed by (hash,
// status epoch) keeps the hot path to one compare.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/vu1_aot/vu1_aot_exports.h"

class VuUnit;
class VuInterpreterTier;

class Vu1AotTier {
public:
    enum class Status : uint8_t { Unverified = 0, Verified, Refused };
    enum class Fallback : uint8_t { Block = 0, Interp };

    struct Stats {
        uint64_t native_kicks = 0;     // programs run on the specialised table
        uint64_t fallback_kicks = 0;   // programs run on the fallback tier (status != Verified)
        uint64_t native_bundles = 0;   // bundles retired on the native path
        uint64_t native_ns = 0;        // wall time of the native runs (steady clock around Execute)
        uint64_t specialized = 0;      // programs whose table was re-resolved (once per image per table life)
        uint64_t native_installs = 0;  // exports tables installed on a program's blocks
        uint64_t native_blocks = 0;    // block functions installed (sum over installs)
    };

    // The kick observer: every kick of an image that is not Verified (the
    // corpus collector lives in the composition root).
    using KickObserver = void (*)(void* user, VuUnit& unit, uint32_t start_pc, uint64_t image_hash, Status status);

    Vu1AotTier(VuBlockTier& block, VuInterpreterTier& generic);

    // Run the unit's microprogram from `start_pc`: native when the image is
    // Verified (or every image is assumed so), else on the fallback.
    void Execute(VuUnit& unit, uint32_t start_pc);

    // The status map (thread-safe: the verifier publishes from another thread).
    void SetStatus(uint64_t image_hash, Status status);
    // The generated code of an image (thread-safe: the builder publishes from
    // another thread; the table and its library must outlive this tier's use of
    // them, or be withdrawn with null first). Installed on the VU thread at the
    // image's next Verified kick; a table whose blocks disagree with the live
    // block structure is refused there (nothing installed, `native_refused`).
    void SetNativeExports(uint64_t image_hash, const Vu1AotImageExports* exports);
    const Vu1AotImageExports* native_exports(uint64_t image_hash) const;
    size_t native_images() const;
    uint64_t native_refused() const { return native_refused_; }
    // Install `ex` on the program's blocks (building the ones it names that do
    // not exist yet). False = a block of the table does not match the program
    // (pc past the image, op count differs): nothing is installed. Called on the
    // thread that runs the unit; public for the verifier leg and the tests.
    bool InstallNative(VuBlockProgram& prog, const Vu1AotImageExports& ex, bool flags_lazy);
    Status status(uint64_t image_hash) const;
    size_t images() const;
    size_t verified() const;
    size_t refused() const;

    // Every image is treated as Verified: the corpus harness / the bench run
    // the native path on images no manifest has judged. Never the play default.
    void set_assume_verified(bool on) { assume_verified_ = on; }
    bool assume_verified() const { return assume_verified_; }
    void set_fallback(Fallback f) { fallback_ = f; }
    Fallback fallback() const { return fallback_; }
    void SetKickObserver(KickObserver fn, void* user);

    const Stats& stats() const { return stats_; }
    // Mean ns per bundle on the native path (0 when nothing ran there).
    double native_ns_per_bundle() const;

    // The resolver installed on a verified program: the block tier's facts
    // with the two bodies swapped for their (kind, dest) specialisations.
    static void ResolveSpecialized(VuBlockOp& op, const VuDecodedSlot& s);

private:
    Status Lookup(uint64_t image_hash);

    VuBlockTier& block_;
    VuInterpreterTier& generic_;
    mutable std::mutex mu_;
    std::unordered_map<uint64_t, Status> status_;
    std::unordered_map<uint64_t, const Vu1AotImageExports*> native_;
    std::atomic<uint32_t> epoch_{0};   // bumped by SetStatus: the memo below re-reads the map
    uint64_t memo_hash_ = 0;
    uint32_t memo_epoch_ = ~0u;
    Status memo_status_ = Status::Unverified;
    const Vu1AotImageExports* memo_native_ = nullptr;
    uint64_t native_refused_ = 0;
    bool assume_verified_ = false;
    Fallback fallback_ = Fallback::Block;
    KickObserver observer_ = nullptr;
    void* observer_user_ = nullptr;
    Stats stats_;
};
