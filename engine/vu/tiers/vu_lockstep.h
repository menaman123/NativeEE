#pragma once
// VuLockstep -- the tier A/B in the running game: every VU1 kick runs on the
// configured tier (the real unit, the real GIF path) AND on the interpreter
// reference (a shadow unit cloned from the real one at the kick), and the
// two end states are diffed -- registers, data memory, the clock, the Q/P
// latches, the bundles retired, the XGKICK packets byte for byte. The first
// divergences are reported in detail (kick ordinal, image, start pc, the
// first differing field, and the bundle index the states first part at: the
// kick is re-run from its entry state under growing cycle caps on both
// tiers, the real unit's kicks muted); every divergence is counted.
//
// What this sees that a Vu1Case corpus cannot: the game's state carried
// ACROSS kicks (a data-memory byte a program stored, the clock, a Q/P latch
// left pending) -- a corpus case replays one kick from its entry, which both
// tiers reproduce identically once the entry has already diverged. The
// knob is Config `diag.vu1_lockstep` (an observer: the GS sees only the
// configured tier's packets; the shadow's go to a null arbiter) and costs an
// interpreter run per kick plus a 33 KB clone.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/null_gs_sink.h"

class VuTierDispatch;
class VuUnit;

class VuLockstep {
public:
    struct Stats {
        uint64_t kicks = 0;
        uint64_t divergent = 0;
        uint64_t first_divergent_kick = 0;  // 1-based ordinal; 0 = none yet
        uint64_t diverged_regs = 0;
        uint64_t diverged_data = 0;
        uint64_t diverged_cycle = 0;
        uint64_t diverged_latches = 0;  // Q / P pending
        uint64_t diverged_bundles = 0;
        uint64_t diverged_packets = 0;  // the kicked bytes
    };

    VuLockstep();
    ~VuLockstep();

    // The kick at `start_pc`: the configured tier on `unit` through
    // `dispatch.RunTier`, the interpreter on the shadow, then the diff.
    void Run(VuTierDispatch& dispatch, VuUnit& unit, uint32_t start_pc);

    const Stats& stats() const { return stats_; }
    // Divergences reported in detail (default 4; the rest are counted).
    void SetReportLimit(int n) { report_limit_ = n; }
    void SetOutput(std::FILE* out) { out_ = out; }
    // One line for a census: kicks, divergent count, first ordinal, the categories.
    std::string Summary() const;
    // Write the entry state of every reported divergence as a Vu1Case
    // (<dir>/kick_<ordinal>_<pc>.bin) for the replay tools ("" = none).
    void SetDumpDir(const std::string& dir) { dump_dir_ = dir; }

private:
    // The categories a diff can land in (bit set).
    enum : uint32_t { kRegs = 1, kData = 2, kCycle = 4, kLatches = 8, kBundles = 16, kPackets = 32 };

    void EnsureShadow(const VuUnit& unit);
    void RunReference(uint32_t start_pc, uint64_t cycle_limit);
    // The end-state diff of the real unit against the shadow (TPC included
    // when `with_tpc`); `detail` names the first differing field.
    uint32_t Compare(const VuUnit& real, bool with_tpc, uint64_t bundles_real, uint64_t bundles_ref, std::string& detail) const;
    // Re-run the kick from `entry_` under growing caps on both tiers until
    // the states part; prints the bundle index and pc. Restores `unit`.
    void Bisect(VuTierDispatch& dispatch, VuUnit& unit, uint32_t start_pc, uint64_t bundles);
    static void TapReal(void* user, const uint8_t* data, size_t size);
    static void TapShadow(void* user, const uint8_t* data, size_t size);
    static void Append(std::vector<uint8_t>& to, const uint8_t* data, size_t size);

    NullGsSink sink_;
    GifRegs gif_regs_;
    GifParser parser_;
    GifArbiter arbiter_;  // the shadow's kicks land here and are cleared after each run
    std::unique_ptr<VuUnit> shadow_;  // the reference run
    std::unique_ptr<VuUnit> entry_;   // the state at the kick (the bisect's seed)
    std::unique_ptr<VuUnit> keep_;    // the real unit's end state across the bisect
    std::vector<uint8_t> kicks_real_;
    std::vector<uint8_t> kicks_shadow_;
    uint32_t kick_packets_real_ = 0;
    uint32_t kick_packets_shadow_ = 0;
    Stats stats_;
    int report_limit_ = 4;
    int reported_ = 0;
    std::FILE* out_ = stderr;
    std::string dump_dir_;
};
