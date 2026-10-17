#pragma once
// Vu1AotCache -- the AOT tier's persistent side, under the user data dir
// (CHARTER 2 C4: never beside the binary, never under the source tree):
//
//   <user_data>/vu1_aot/corpus/<image>_<entry>_<n>.bin   the title's VU1 corpus: Vu1Case
//                                                        records (the engine's capture format)
//                                                        collected at first run, up to
//                                                        `cases_per_image` per image, spread
//                                                        over the image's entry addresses
//   <user_data>/vu1_aot/<engine build id>/manifest.txt   one row per image the verifier judged (the id's
//                                                        bytes outside [A-Za-z0-9._-] written as '_')
//                                                        under THIS engine build:
//        image=<16 hex> status=verified|refused cases=<n> corpus=<16 hex> engine=<build token>
//        verified_at=<unix s> bundles=<n> [divergence=<first differing field>]
//
// At Open the manifest seeds the tier's status map (a verified image goes
// native at its first kick of this run), then every corpus case on disk -- the
// collected corpus and, when set, engine.vu1_aot_corpus -- is read and grouped
// by image; images without a verdict are queued for the verifier thread. At
// run time the tier's kick observer feeds the collector: a kick of an image
// without a verdict is captured (VuUnit::SaveCase, written to the corpus dir)
// until the image's quota is met, then the image is queued; the verifier
// thread runs Vu1AotVerifier over the image's cases, publishes the status to
// the tier (the next kick goes native or stays on the fallback) and appends
// the manifest row. The frame never waits: the live unit runs the fallback
// tier until the verdict lands. Everything is in-process; no compiler, no
// generated file: the specialised tables are built from the image at the
// first native kick (engine/vu/vu1_aot/vu1_aot_tier.h).
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine/vu/vu1_aot/vu1_aot_tier.h"

struct Vu1Case;
class VuUnit;

namespace ps2r::app {

class Vu1AotCache {
public:
    struct Settings {
        std::string root;            // <user_data>/vu1_aot
        std::string build_id;        // g_build_id
        std::string extra_corpus;    // engine.vu1_aot_corpus ("" = none)
        int cases_per_image = 16;    // the collection quota per image (>= 1)
        bool collect = true;         // capture new images' kicks into the corpus dir
    };
    struct Census {
        uint64_t corpus_cases = 0;      // cases read at Open (collected + extra)
        uint64_t collected = 0;         // cases captured this run
        uint64_t verify_runs = 0;       // images judged this run
        uint64_t verify_ns = 0;         // verifier wall time this run
        uint64_t manifest_rows = 0;     // verdicts seeded from the manifest
        uint64_t pending = 0;           // images queued, verdict not yet published
    };

    Vu1AotCache(Vu1AotTier& tier, Settings settings);
    ~Vu1AotCache();

    // Read the manifest and the corpus directories, install the kick observer,
    // start the verifier thread. False when the root cannot be created
    // (`note` says why); the tier then simply runs its fallback everywhere.
    bool Open(std::string* note);
    // Stop the verifier (the queued images are judged first, bounded by the
    // caller's patience: `drain` = finish the queue; false = drop it) and
    // uninstall the observer.
    void Close(bool drain);

    // run.meta vu1_aot= row: programs:<n>:verified:<n>:refused:<n>:pending:<n>:
    // native_kicks:<n>:fallback_kicks:<n>:ns_per_bundle:<x.xx>:corpus:<n>:collected:<n>:
    // verify_runs:<n>:verify_ms:<n>
    std::string CensusRow() const;
    Census census() const;

    // The manifest row grammar (public for the round-trip test).
    struct ManifestRow {
        uint64_t image = 0;
        Vu1AotTier::Status status = Vu1AotTier::Status::Unverified;
        uint64_t cases = 0;
        uint64_t corpus = 0;
        std::string engine;
        uint64_t verified_at = 0;
        uint64_t bundles = 0;
        std::string divergence;
    };
    static std::string FormatRow(const ManifestRow& r);
    static bool ParseRow(const std::string& line, ManifestRow& r);
    static std::string ManifestPath(const std::string& root, const std::string& build_id);
    // The build id as written (the directory name and the row's engine field).
    static std::string BuildToken(const std::string& build_id);
    static std::string CorpusDir(const std::string& root);

private:
    struct Image {
        std::vector<std::unique_ptr<Vu1Case>> cases;  // owned until the verifier takes them
        std::map<uint32_t, int> per_entry;            // cases collected per start pc
        uint64_t kicks = 0;
        bool queued = false;
    };

    static void OnKick(void* user, VuUnit& unit, uint32_t start_pc, uint64_t image_hash, Vu1AotTier::Status status);
    void Collect(VuUnit& unit, uint32_t start_pc, uint64_t image_hash);
    void LoadCorpusDir(const std::string& dir, std::map<uint64_t, Image>& images);
    void Enqueue(uint64_t image_hash, std::vector<std::unique_ptr<Vu1Case>> cases);
    void VerifierMain();
    void Judge(uint64_t image_hash, const std::vector<std::unique_ptr<Vu1Case>>& cases);
    void AppendManifest(const ManifestRow& r);

    Vu1AotTier& tier_;
    Settings settings_;
    std::map<uint64_t, Image> images_;   // the collector's view (the observer's thread)
    mutable std::mutex mu_;              // the queue, the census, the manifest file
    std::condition_variable cv_;
    struct Job {
        uint64_t image = 0;
        std::vector<std::unique_ptr<Vu1Case>> cases;
    };
    std::vector<Job> queue_;
    std::thread verifier_;
    std::atomic<bool> stop_{false};
    bool drain_on_stop_ = true;
    bool open_ = false;
    Census census_;
};

}  // namespace ps2r::app
