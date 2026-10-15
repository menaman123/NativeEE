// core/verify/run_meta.h -- the leg's provenance stamp, <leg>/run.meta
// (EQUIVALENCE_PLAN section 2; asserted by verify/diff_legs.py before any
// hash compare). key=value lines, in this FROZEN order:
//   build_id=     the binary's build id
//   gl_renderer=  renderer string captured at the first present ("none"
//                 while no renderer exists)
//   tree=         "new" (the runner labels OLD legs legacy/v2)
//   profile=      the config profile name
//   env_ps2=      Config::Fingerprint() (sorted non-default key=value;
//                 harness/paths/meta excluded)
//   env_hash=     FNV-1a64 fold of the fingerprint entries, continued over
//                 the root's build-constant entries (avcodec_version) -- the
//                 cross-run equality gate
//   frames=       presents counted so far
//   wall_ms=      wall time since the first present (informational)
//   finalized=    1 once the exit hook ran
// followed by NEW-only informational keys (Set()), which the cross-engine
// gate ignores. Rewritten every 64 presents and at finalize so a killed leg
// still carries a mostly complete stamp.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ps2r::verify {

class RunMeta {
public:
    struct Identity {
        std::string build_id;
        std::string tree = "new";
        std::string profile;
        std::string env_ps2;      // Config::Fingerprint()
        uint64_t    env_hash = 0; // Config::FingerprintHash()
    };

    RunMeta(std::string path, Identity id);

    // First call latches the renderer string and writes the early stamp;
    // every call counts a present. (wall_ms runs from construction.)
    void OnPresent(const char* gl_renderer);
    // Final rewrite (idempotent).
    void Finalize();
    // NEW-only informational key (appended after the frozen keys).
    void Set(const std::string& key, const std::string& value);
    // Same build/configuration provenance, with fresh frame/wall counters.
    std::unique_ptr<RunMeta> CopyForSegment(std::string path) const;
    bool Flush() const;

    // The file text as it would be written now (tests / logs).
    std::string Render() const;
    uint64_t Frames() const { return frames_; }
    bool Finalized() const { return finalized_; }

private:
    bool WriteNow() const;

    std::string path_;
    Identity    id_;
    std::string gl_renderer_;
    uint64_t    frames_ = 0;
    uint64_t    start_ns_ = 0;
    bool        finalized_ = false;
    std::vector<std::pair<std::string, std::string>> extra_;
};

}  // namespace ps2r::verify
