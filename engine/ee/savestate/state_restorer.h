#pragma once
// StateRestorer: the savestate LOAD / SAVE orchestrator over the section
// registry, in the original restore order (OLD savestate.cpp DoLoad):
//   1 parse + validate the whole file, including every owned section's
//     validate hook (a bad file disturbs nothing: no step below runs);
//   2 EmuDomain re-anchor (the domain's ReAnchor step, bound by the root);
//   3 memory (RAM0 / SPAD / META);
//   4 threads (THRD);           5 kernel objects (SEMA / EVFL);
//   6 INTC (STAT / MASK / handler records);
//   7 EE timers (TIMR, last: after the counters so baselines are coherent).
// Each phase binds its step as a SectionRegistry spec; the order above is the
// REGISTRATION order the composition root uses (Phase-3 root: memory first).
// Partial-owner inspection tools may retain the historical phase reader.
// The application requires an owner for every REQUIRED section, except the
// explicit frozen legacy tags it still imports without an owner.
//
// restore.mhash: RestoredMemoryHash() is the MemoryCheckpointer hash of the
// memory spans right after Restore(), before the first guest instruction.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

class MemorySections;

class StateRestorer {
public:
    using Step = std::function<bool(std::string* err)>;
    using ImagePreflight = std::function<bool(const std::vector<ps2r::savestate::Blob>&,
                                              std::string* err)>;

    explicit StateRestorer(MemorySections& memory);
    ps2r::savestate::SectionRegistry& Registry() { return registry_; }

    // Runs before any section restore (EmuDomain::ReAnchor and the guest
    // thread teardown belong here); false aborts the load untouched.
    void SetPreRestore(Step step);
    // Cross-owner validation sees the same immutable parsed image that will
    // be restored. Runs after section validation, before teardown or RAM writes.
    // A validator may stage an unpublished owner; it must not mutate live state.
    // Optional paired discard releases only unpublished resources, after
    // cancellation, failed preparation/apply, or successful publication.
    // Discard must not throw; callbacks run in reverse registration order.
    void AddImagePreflight(ImagePreflight check, std::function<void()> discard = {});
    using ImageValidation = std::function<bool(const ps2r::savestate::FileMeta&,
        const std::vector<ps2r::savestate::Blob>&, std::string*)>;
    // Pure application policy over the ORIGINAL image, before ownership
    // filtering, teardown or RAM writes. Optional foreign owner records must
    // remain visible so selection mismatches cannot silently lose their state.
    void SetImageValidation(ImageValidation validate);

    // Enable this in the application before loading any image. Unknown
    // REQUIRED sections reject before teardown or memory writes. The exception
    // list is an explicit legacy migration debt, not a wildcard for new service
    // state. Registered owners still validate even when their tag is listed.
    void RequireSectionOwners(std::vector<uint32_t> legacy_unowned = {}) {
        require_owners_ = true;
        legacy_unowned_ = std::move(legacy_unowned);
        ++policy_revision_;
    }

    // Complete validation and resource staging before live-owner unwind.
    // Holds the exact image and parsed owners; no RAM writes or pre-restore
    // steps run here. A second prepare requires explicit discard.
    bool PrepareImage(const uint8_t* data, size_t len, std::string* err);
    // Supervisor only, after all producers/consumers have joined. Bytes and
    // owner registrations must still match. Consumed even on apply failure.
    bool RestorePrepared(const uint8_t* data, size_t len, std::string* err);
    void DiscardPrepared();
    bool HasPrepared() const { return !prepared_bytes_.empty(); }

    // Parse + validate + restore from an in-memory image / a file path.
    bool RestoreImage(const uint8_t* data, size_t len, std::string* err);
    bool RestoreFile(const std::string& path, std::string* err);
    // The parsed sections of the last successful RestoreImage (file order).
    const std::vector<ps2r::savestate::Blob>& LastSections() const { return last_sections_; }
    const ps2r::savestate::FileMeta& LastMeta() const { return last_meta_; }

    // Capture every registered section (registration order) and serialize
    // the file image; `meta` is stamped with the lowest covering version.
    std::vector<uint8_t> SaveImage(ps2r::savestate::FileMeta meta) const;

    uint64_t RestoredMemoryHash() const;

private:
    MemorySections& memory_;
    ps2r::savestate::SectionRegistry registry_;
    Step pre_restore_;
    std::vector<std::pair<ImagePreflight, std::function<void()>>> image_preflights_;
    ImageValidation image_validation_;
    bool require_owners_ = false;
    std::vector<uint32_t> legacy_unowned_;
    std::vector<ps2r::savestate::Blob> last_sections_;
    ps2r::savestate::FileMeta last_meta_;
    std::vector<uint8_t> prepared_bytes_;
    std::vector<ps2r::savestate::Blob> prepared_sections_, prepared_owned_;
    ps2r::savestate::FileMeta prepared_meta_;
    uint64_t policy_revision_ = 0, prepared_policy_ = 0, prepared_registry_ = 0;
};
