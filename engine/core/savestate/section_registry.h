// core/savestate/section_registry.h -- who owns which .ps2st section.
//
// Each subsystem registers a SectionSpec for the tag it saves (Phases 3-8);
// the registry captures every registered section in registration order,
// derives the FILE version from the optional sections present (TIMR -> 3,
// SNDC -> 4, else 2), and on restore hands each blob to its owner:
//   * unknown section, not REQUIRED  -> skipped;
//   * unknown section, REQUIRED       -> load error (file from a newer build);
//   * registered REQUIRED tag missing -> load error;
//   * a spec's validate/restore returning false -> load error, named.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/savestate/savestate_format.h"

namespace ps2r::savestate {

struct SectionSpec {
    uint32_t    tag = 0;
    uint32_t    version = 1;
    bool        required = true;
    const char* owner = "";   // module name for messages
    // Append the section payload for the current state.
    std::function<void(std::vector<uint8_t>& out)> serialize;
    // Optional pre-check (versions, sizes) before any restore runs.
    std::function<bool(const Blob&, std::string* err)> validate;
    // Apply the payload to live state; false = load error.
    std::function<bool(const Blob&, std::string* err)> restore;
    // Optional-section owners only: runs during Restore when the file carries
    // no section with this tag (e.g. TIMR absent -> the timers' boot seed).
    std::function<void()> absent;
    // New captures require this owner even when explicitly approved older
    // files may omit it. Whole-image preflight owns that compatibility policy.
    bool capture_required = false;
};

class SectionRegistry {
public:
    // False (and no change) when the tag is already registered.
    bool Register(const SectionSpec& spec);
    using ImageValidation = std::function<bool(const std::vector<Blob>&, std::string*)>;
    // Pure cross-owner checks (selection, loans, generations, ordering).
    // All run after individual validators and before the first restore.
    bool RegisterValidation(std::string owner, ImageValidation validate);
    uint64_t Revision() const { return revision_; }
    bool Has(uint32_t tag) const;
    size_t Count() const { return specs_.size(); }
    const std::vector<SectionSpec>& Specs() const { return specs_; }
    std::vector<uint32_t> KnownTags() const;

    // Serialize every registered section (registration order).
    std::vector<Blob> Capture() const;
    // Lowest file version covering the given sections.
    static uint32_t FileVersionFor(const std::vector<Blob>& sections);

    // Presence / ownership / per-spec validate checks only -- touches no
    // state, so an orchestrator can run it before any teardown step.
    bool Validate(const std::vector<Blob>& sections, std::string* err) const;
    // Validate (again) then restore all in registration order; see the header comment.
    bool Restore(const std::vector<Blob>& sections, std::string* err) const;

private:
    std::vector<SectionSpec> specs_;
    std::vector<std::pair<std::string, ImageValidation>> validations_;
    uint64_t revision_ = 0;
};

}  // namespace ps2r::savestate
