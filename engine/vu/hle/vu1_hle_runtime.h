#pragma once
// The VU1 HLE tier: offered every VU1 program start before the microcode
// tiers. TryExecute hashes the micro image (memoised on the unit's micro
// generation plus the external-store epoch), analyses it once per distinct
// image, looks the (image, variant, entry) up in the registry and runs the
// native program. On success the program's data-memory writes are applied
// FIRST (its packets were snapshotted from the working copy), then every
// kick goes to the arbiter through KickLowering in program order, then the
// register file is installed wholesale or TPC is parked at the family exit.
// A MISS or a DECLINE returns false with the unit untouched.
//
// Identity observer: the one site that knows {handler, image, entry pc} AND
// the kick ordinal is the kick loop, so before each kick is submitted the
// observer receives the KickIdentity the DrawIR identity stamp is built from
// (handler name -> GsdlFormat::HandlerId by the binder, image hash, MSCAL
// entry pc, XGKICK ordinal within the run); after the run's last kick it
// receives null (nothing staged). Observation only: the kicks themselves are
// unchanged.
//
// Cross-thread contract: one runtime per VU1 unit, driven from the thread
// that executes that unit (the EE or the MTVU worker); nothing here is shared.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/vu/hle/kick_lowering.h"
#include "engine/vu/hle/vu1_hle_contract.h"
#include "engine/vu/hle/vu1_image_analyzer.h"
#include "engine/vu/hle/vu1_library_entry.h"
#include "engine/vu/hle/vu1_registry.h"

class GifArbiter;
class VuUnit;

class Vu1HleRuntime {
public:
    struct Census {
        uint64_t runs = 0;      // programs that ran
        uint64_t misses = 0;    // no entry for the image/variant/entry
        uint64_t declines = 0;  // an entry declined the layout
        // The missed images, by (variant hash, entry pc) with their counts --
        // the attribution of `misses` (which program family the corpus lacks,
        // or which residency the image carries that the corpus never saw).
        // Bounded: past kMissKinds distinct kinds the rest count in `misses` only.
        struct MissKind { uint64_t variant_hash; uint32_t entry_pc; uint64_t count; };
        static constexpr size_t kMissKinds = 64;
        std::vector<MissKind> miss_kinds;
    };
    // The declines by program: which library entry declined which image at
    // which entry pc, how often (the first kDeclineRows distinct tuples; the
    // rest fold into `declines` alone). Names the programs the interpreter
    // still runs so an owner can lower them with the replay oracle.
    struct Decline {
        const char* name = nullptr;   // the entry's name (may be null for a nameless row)
        uint64_t image_hash = 0;
        uint64_t variant_hash = 0;
        uint32_t entry_pc = 0;
        uint64_t count = 0;
    };
    static constexpr size_t kDeclineRows = 32;
    const std::vector<Decline>& declines() const { return declines_; }
    // The identity of one HLE'd kick, reported before its submit.
    struct KickIdentity {
        const char* handler_name;  // the library entry's name (HandlerId source)
        uint64_t image_hash;       // whole-image hash of the running program
        uint16_t kick_pc;          // MSCAL entry pc (& 0x3FFF)
        uint16_t kick_ord;         // XGKICK ordinal within this run, from 0
    };
    // `id` is null after the run's last kick (clear).
    using IdentityObserver = void (*)(void* user, const KickIdentity* id);

    // Raw lowering into `arbiter`.
    Vu1HleRuntime(const Vu1ProgramLibrary& library, GifArbiter& arbiter);
    // A caller-owned lowering (the renderer's upper tiers).
    Vu1HleRuntime(const Vu1ProgramLibrary& library, KickLowering& lowering);

    // The VuTierDispatch::HleHook shape; `user` is the runtime.
    static bool Hook(void* user, VuUnit& unit, uint32_t start_pc, uint64_t micro_generation);
    bool TryExecute(VuUnit& unit, uint32_t start_pc, uint64_t micro_generation);

    // VIF1 TOPS / ITOPS (the unit carries only TOP / ITOP); the VIF sets them.
    void SetVifTops(uint16_t tops, uint16_t itops);
    // The EE-window micro-store epoch: invalidates the image-hash memo.
    void BumpExternalGeneration() { ext_gen_++; }
    void SetIdentityObserver(IdentityObserver fn, void* user);
    // Miss capture (a harness knob): the first `per_kind` kicks of every missed
    // (variant hash, entry pc) kind are written as Vu1Case records to
    // <dir>/<tag>_<variant>_<pc>_<n>.bin -- the whole unit at the kick, so the
    // replay oracle (vu1_replay --hle) can judge a program added for them.
    // Empty dir = off (the default). `tag` distinguishes the runtimes of one
    // process (the inline unit and the MTVU worker's) in the file names.
    void SetMissCapture(const std::string& dir, const char* tag, unsigned per_kind);
    uint64_t miss_captures() const { return miss_captures_; }

    Vu1Registry& registry() { return registry_; }
    const Vu1Registry& registry() const { return registry_; }
    Vu1ImageAnalyzer& analyzer() { return analyzer_; }
    const Census& census() const { return census_; }

    static constexpr size_t kMemoLimit = 512;  // analysed images kept before the memo is dropped

private:
    struct MemoEntry {
        uint64_t image;
        Vu1ImageInfo info;
    };
    const Vu1ImageInfo& AnalyzeMemo(uint64_t image, const uint8_t* micro, const uint8_t* data);

    Vu1Registry registry_;
    Vu1ImageAnalyzer analyzer_;
    uint32_t family_exit_tpc_;
    std::unique_ptr<KickLowering> owned_lowering_;
    KickLowering* lowering_;
    std::vector<MemoEntry> memo_;
    bool gen_valid_ = false;
    uint64_t gen_key_ = 0;
    uint64_t ext_key_ = 0;
    uint64_t gen_img_ = 0;
    uint64_t ext_gen_ = 0;
    uint16_t tops_ = 0;
    uint16_t itops_ = 0;
    IdentityObserver identity_observer_ = nullptr;
    void* identity_user_ = nullptr;
    Census census_;
    std::vector<Decline> declines_;
    std::string capture_dir_;
    const char* capture_tag_ = "vu1";
    unsigned capture_per_kind_ = 0;
    uint64_t miss_captures_ = 0;
    void CaptureMiss(const VuUnit& unit, uint32_t start_pc, uint64_t variant_hash, uint32_t entry_pc, uint64_t count);
};
