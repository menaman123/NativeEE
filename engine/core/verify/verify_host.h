// core/verify/verify_host.h -- the in-binary harness: owns the leg's four
// emitters (frames.fhash / audio.ahash / mem.mhash / run.meta) plus the
// optional input.padlog recorder, armed from Config.harness (CLI only --
// never the environment). Disarmed, every entry point is one branch and no
// file is opened.
//
// Entry points the subsystems call:
//   OnPresent(rgba, pw, ph, scale, renderer)  graphics: one line per present
//   OnBoundary(mix, mix_frames, spans)        EE frame edge: audio pump +
//                                             memory checkpoint cadence
//   OnPadServe(wire_buttons, lx, ly, rx, ry)  pad: served state recorder
//   Finalize()                                exit path: tails + run.meta
// Line formats are FROZEN (diff_legs.py): "<key>,0x%016llx".
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine/core/triage/input_log.h"
#include "engine/core/verify/audio_hasher.h"
#include "engine/core/verify/memory_checkpointer.h"
#include "engine/core/verify/run_meta.h"

namespace ps2r {
class Config;
}

namespace ps2r::verify {

class VerifyHost {
public:
    VerifyHost() = default;
    ~VerifyHost();
    VerifyHost(const VerifyHost&) = delete;
    VerifyHost& operator=(const VerifyHost&) = delete;

    // One build-constant provenance entry the root folds into the leg's
    // identity (CONSTRUCTION_PLAN section 4.7: the decoder dependency is part
    // of the fingerprint gate). Folded into env_hash as `key=value;` AFTER the
    // Config entries and stamped as a run.meta key; env_ps2 stays
    // Config::Fingerprint() (the frozen preset string battery.py asserts).
    struct FingerprintEntry {
        std::string key;
        std::string value;
    };

    // Arms when cfg.harness.out_dir is set: creates the leg dir, opens the
    // streams, writes the early run.meta identity. `title` labels the padlog
    // header. False + *err when the directory cannot be created.
    bool Arm(const Config& cfg, const std::string& build_id, const std::string& title, std::string* err,
             const std::vector<FingerprintEntry>& extra = {});
    bool Armed() const { return armed_; }
    const std::string& LegDir() const { return leg_dir_; }
    std::string LegPath(const char* name) const;

    // Root's present key for a process boundary returned by OnBoundary.
    // Keeps the historical CLI boot convention; an interactive restore
    // segment starts at saved frame + 1. Overflow/old-epoch input refuses.
    uint32_t PresentKey(uint32_t boot_restore_frame, uint64_t boundary) const;
    // One line per present; hash 0 = no readable scanout (null rgba), still
    // one line so leg frame streams stay index-aligned.
    void OnPresent(uint32_t vf, const uint8_t* rgba_bottomup, int pw, int ph, int scale, const char* renderer);
    // The same line from a hash computed elsewhere (the GS consumer hashes its
    // grab with FrameHasher::HashNativeScale at the present scale): OnPresent is
    // this with the hash taken over its arguments.
    void OnPresentHashed(uint32_t vf, uint64_t hash, const char* renderer);
    // Frame edge n (0-based, internal counter): pushes `mix_frames` stereo
    // frames into the audio lane and hashes `spans` every 60 boundaries.
    // Returns the boundary index it labelled.
    uint64_t OnBoundary(const float* mix, int mix_frames, const std::vector<MemorySpan>& spans);
    // The mem lane (the 34 MB FNV-1a every 60 boundaries, ~25 ms on the game
    // thread): the deterministic profile's equivalence instrument. Off, the
    // boundary skips it and run.meta reads mem_lane=off -- play mode's choice
    // (the clock contract: that hash was one two-field frame per second).
    void SetMemLane(bool on);
    bool MemLane() const { return mem_lane_; }
    // Served pad state (ACTIVE-LOW wire word as the game reads it) at game
    // frame `vf` -- the frames-lane key of the boundary being served (the
    // recording's clock, named in its header: `clock=frame`; run.meta
    // pad_record_clock=). Never the logical tick: wall-paced in play.
    static constexpr triage::InputLog::Clock kRecordClock = triage::InputLog::Clock::Frame;
    void OnPadServe(uint32_t vf, uint16_t wire_buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry);
    // Save the recording through its last served frame; safe during play.
    void FlushInput();
    // Game owner or joined supervisor only. A rewind starts a distinct file;
    // no old range or completed input file is truncated/reused.
    bool BeginInputSegment(uint64_t restored_tick, std::string* err);
    // Joined supervisor only: reserve a distinct output directory, finish
    // preceding hash/input tails and start fresh lanes with explicit restored
    // frame/tick provenance. Lifetime boundary count (and frame cap) survives.
    // A failure after tail completion requires session close, not old play.
    bool BeginOutputSegment(uint64_t restored_frame, uint64_t restored_tick, std::string* err);
    uint64_t OutputSegment() const { return output_segment_; }
    uint64_t SegmentStartBoundary() const { return segment_boundary_; }
    bool OutputFailed() const { return !output_error_.empty(); }
    const std::string& OutputError() const { return output_error_; }
    bool RecordingInput() const { return armed_ && record_input_ && !finalized_ && !OutputFailed(); }
    std::string InputPath() const;
    // Tails: partial audio window, open pad ranges, final run.meta. Idempotent.
    void Finalize();

    RunMeta* Meta() { return meta_.get(); }
    uint64_t Boundaries() const { return n_boundaries_; }

private:
    std::FILE* OpenLeg(const char* name) const;
    // The current recording segment's file: an explicit `--record-input <file>`
    // destination (it names a directory component) or a name inside the leg.
    std::string PadPath() const;
    void WriteInputLines(const std::vector<std::string>& lines);

    bool        armed_ = false;
    bool        finalized_ = false;
    bool        record_input_ = false;
    bool        mem_lane_ = true;
    std::string leg_dir_;
    std::string output_root_;
    std::string output_error_;
    std::string title_;
    std::FILE*  frames_f_ = nullptr;
    std::FILE*  audio_f_ = nullptr;
    std::FILE*  mem_f_ = nullptr;
    std::FILE*  pad_f_ = nullptr;
    std::unique_ptr<RunMeta>     meta_;
    std::unique_ptr<AudioHasher> audio_;
    MemoryCheckpointer           mem_;
    triage::InputLog             pad_;
    std::mutex                   pad_mu_;
    uint32_t                     pad_last_vf_ = 0;
    uint32_t                     pad_flushed_vf_ = 0;
    uint64_t                     pad_lines_ = 0;
    uint64_t                     pad_segment_ = 0;
    std::string                  pad_name_ = "input.padlog";
    bool                         pad_seen_ = false;
    uint64_t                     n_boundaries_ = 0;
    uint64_t                     output_segment_ = 0, segment_boundary_ = 0;
    uint32_t                     segment_restore_frame_ = 0;
};

}  // namespace ps2r::verify
