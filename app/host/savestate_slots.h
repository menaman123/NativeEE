#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/savestate/savestate_format.h"
#include "engine/core/savestate/section_registry.h"

namespace ps2r {
class Config;
namespace app {

// The product's savestate slots: where F5 / --set harness.save_at_frame write
// and F8 / --load-state read. One root (`paths.save_dir`, else the harness out
// dir, else `paths.log_dir`), `<root>/savestates/slot<N>.ps2st` with the slot
// from `save.slot`, and a `.meta` sidecar (frame, build id, composition, the
// section tags the image carries). Pure path/text helpers plus the whole-image
// preflight F8 runs BEFORE it closes the live session: every section the
// current composition's registry requires must be present in the file, the
// sidecar's composition= must equal the live Composition(), and the running
// image must resolve; a refusal names the missing section / differing field.
class SavestateSlots {
public:
    struct Meta {
        uint64_t frame = 0;
        uint64_t wall_ms = 0;
        std::string build_id;
        std::string composition;
        std::vector<std::string> sections;   // "TAGx:v<version>" per section, file order
    };

    static std::string Root(const Config& config);
    static std::string Directory(const Config& config);      // Root()/savestates
    static std::string SlotPath(const std::string& directory, int slot);
    static std::string SlotPath(const Config& config);       // Directory()/slot<save.slot>.ps2st
    static std::string MetaPath(const std::string& slot_path);
    // Every hle.* key with its value plus the profile: the composition the
    // image was captured under (the fingerprint lists only non-default keys).
    static std::string Composition(const Config& config);
    static std::string RenderMeta(const Meta& meta);
    // Reads the written image's section envelope (tags/versions) into meta.sections
    // and writes the sidecar atomically. False + err names the failure.
    static bool WriteMeta(const std::string& slot_path, Meta meta, std::string* err);
    // Parses a sidecar written by RenderMeta (key=value lines; unknown keys
    // ignored). False + err names a missing/unreadable file or a sidecar
    // without a composition= line.
    static bool ReadMeta(const std::string& meta_path, Meta* meta, std::string* err);
    // Pure: empty when the two Composition() strings agree; otherwise the
    // differing fields by name, e.g. "profile(saved=deterministic,live=default)"
    // -- a key present on one side only reads "(saved=<absent>,…)".
    static std::string CompositionDifference(const std::string& saved, const std::string& live);
    // The F8 preflight of the slot's SIDECAR against the live composition,
    // BEFORE anything of the live session unwinds: the .meta must exist and its
    // composition= must equal `live_composition` (refused by name:
    // "composition differs: profile(saved=deterministic,live=default)"). A
    // build_id= other than `live_build_id` is not a refusal: it is returned in
    // *warning for the caller to report.
    static bool PreflightComposition(const std::string& slot_path, const std::string& live_composition,
                                     const std::string& live_build_id, std::string* err, std::string* warning);
    // The running executable image, resolved from the kernel
    // (_NSGetExecutablePath on macOS, /proc/self/exe on Linux), so the F8
    // relaunch re-executes THIS binary whatever argv[0] was (a bare name found
    // through PATH, a relative path after a chdir). Empty + err when it cannot
    // be resolved -- the preflight refuses then, with the session still open.
    static std::string RunningImage(std::string* err);
    // The newest slot<N>.ps2st under the directory by modification time; empty +
    // err when there is none.
    static std::string Latest(const std::string& directory, std::string* err);
    // Pure preflight of a file against the live registry: envelope valid, and
    // every registered owner that is required (or capture-required) present.
    // The first missing owner is named ("required section missing: GCLK (SessionClock)").
    static bool Preflight(const std::string& path, const savestate::SectionRegistry& registry,
                          std::string* err, savestate::FileMeta* meta = nullptr);
    // Pure: the run.meta `savestate=` value of the harness trigger's terminal
    // state ("written" | "refused" | "failed" | other = pending).
    static std::string TriggerMetaLine(const std::string& state, const std::string& path, uint64_t frame,
                                       uint64_t boundary, const std::string& detail);
    // The loud line: stderr + the SAVE log channel, never silent.
    static void Report(const char* action, const char* phase, const std::string& detail);
    // Pure: the F8 relaunch command line from the current one. Drops
    // --load-state / --inputs / --warp-to and the one-shot `--set
    // harness.save_at_frame= / harness.restore_at_frame=`, nests --harness-out under
    // <out>/restores/<stamp>, renames an explicit --record-input file to
    // <stem>-r<stamp>.padlog, pins the savestate root with
    // --set paths.save_dir=<root> (so the slots stay where they are) and
    // appends --load-state <state_path>.
    static std::vector<std::string> RelaunchArguments(const std::vector<std::string>& argv,
                                                      const std::string& state_path, const std::string& save_root,
                                                      uint64_t stamp);
};

}  // namespace app
}  // namespace ps2r
