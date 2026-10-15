#pragma once

#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/ee/timing/emu_domain.h"
#include "engine/ee/timing/tick_source.h"
#include "engine/ee/timing/tick_sync.h"

namespace ps2r::app {

// Existing logical clock persistence. The completed game owner captures;
// the joined supervisor restores before timers and service deadlines.
// Wall deadlines, renderer tickets and device buffers are process resources.
class SessionClock {
public:
    static constexpr uint32_t kTag = 0x4b4c4347; // GCLK
    static constexpr uint32_t kVersion = 2;
    static constexpr size_t kMaxIdentityBytes = 8192;
    struct Snapshot {
        std::string providers;
        uint64_t raw_tick = 0;
        EmuDomain::Snapshot domain;
        uint64_t pending_ticks = 0;
    };
    SessionClock(EmuDomain& domain, TickSource& source, TickSync& ticks, std::string providers);
    savestate::SectionSpec Section();
    Snapshot Capture() const;
    static bool SetFrame(savestate::FileMeta&, uint64_t frame, std::string& error);
    // Fresh interactive restore requires a clock owned by this composition.
    // Historical boot import remains a separate explicitly selected policy.
    bool ValidateImage(const savestate::FileMeta&, const std::vector<savestate::Blob>&,
                       std::string& error) const;
    // Explicit historical boot classification can admit an absent clock.
    // Any present clock still requires exact provider/policy/frame validation.
    bool ValidateBootImage(const savestate::FileMeta&, const std::vector<savestate::Blob>&,
                           bool clockless_import, std::string& error) const;
    static bool Encode(const Snapshot&, std::vector<uint8_t>&, std::string& error);
    static bool Decode(const savestate::Blob&, Snapshot&, std::string& error);
    bool Validate(const Snapshot&, std::string& error) const;
    // All note producers (including the host ticker) and guest consumers must
    // have stopped. Preflight Validate never stops or changes these owners.
    bool Restore(const Snapshot&, std::string& error);
    // Host lifecycle fact, never a saved game value. A clockless boot import
    // may rebase owner deadlines; a restored clock must retain them exactly.
    bool Restored() const { return restored_; }

private:
    EmuDomain& domain_;
    TickSource& source_;
    TickSync& ticks_;
    const std::string providers_;
    bool restored_ = false;
};

} // namespace ps2r::app
