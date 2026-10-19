#pragma once
// The VU result pipelines that make flags and the Q/P registers visible LATER
// than the instruction that produced them, all keyed on the ONE VU clock this
// class owns.
//
// PS2 rules: an FMAC's MAC/STATUS flags and a CLIP judgement become
// architecturally visible 4 cycles after issue (8 results may be in flight);
// Q after DIV/SQRT retires 7 cycles after issue, after RSQRT 13; P retires
// after the EFU latency of its instruction (ESADD 11 ... EATAN 54); a second
// FDIV-pipe op waits for the pending Q, a second EFU op for the pending P
// (released one cycle early); WAITQ / program end drain everything.
//
// The MAC/STATUS ring has two bit-identical representations: EAGER packs the
// 16-bit MAC word at push time and commits each retired entry to the
// architectural registers; LAZY keeps the raw result lanes and folds the
// retired entries into sticky accumulators, materialising the registers only
// when a reader asks. Both are always compiled (the algebra property test
// drives them side by side); `kDefaultLazy` picks the SIMD build's ring.
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class FlagPipeline {
public:
    static constexpr int kRingDepth = 8;
    static constexpr int kClipDelay = 4;
    static constexpr int kMacDelay = 4;
    static constexpr int kDivDelay = 7;
    static constexpr int kSqrtDelay = 7;
    static constexpr int kRsqrtDelay = 13;

    // EFU latencies (cycles from issue to P retire).
    static constexpr int kEfuEsadd = 11, kEfuErsadd = 18, kEfuEleng = 18, kEfuErleng = 24;
    static constexpr int kEfuEatanxy = 54, kEfuEatanxz = 54, kEfuEsum = 12, kEfuErcpr = 12;
    static constexpr int kEfuEsqrt = 12, kEfuErsqrt = 18, kEfuEsin = 29, kEfuEatan = 54, kEfuEexp = 44;

#if defined(PS2R_SIMD) && PS2R_SIMD
    static constexpr bool kDefaultLazy = true;
#else
    static constexpr bool kDefaultLazy = false;
#endif

    struct PendingClip {
        uint64_t retire;
        uint32_t value;
    };
    struct PendingMac {
        uint64_t retire;
        uint32_t bits[4];  // raw result lanes x,y,z,w (lazy)
        uint16_t mac;      // packed MAC word (eager)
        uint8_t mask;      // dest lanes XYZW = 8/4/2/1 (lazy)
    };

    explicit FlagPipeline(bool lazy = kDefaultLazy);

    bool lazy() const { return lazy_; }

    // The VU clock: free-running across runs, advanced by the bundle loop and
    // by every stall.
    uint64_t cycle() const { return vu_cycle_; }
    uint64_t& cycle() { return vu_cycle_; }
    void SetCycle(uint64_t c) { vu_cycle_ = c; }

    // ---- CLIP ring (linear, oldest first) ----
    // Retire every due judgement in issue order and return the visible flag.
    uint32_t VisibleClip(VUregs& regs);
    // Queue a 24-bit judgement; a full ring retires its oldest entry now.
    void PushClip(VUregs& regs, uint32_t value);
    // Program end: the newest queued judgement becomes visible.
    void FlushClipPipe(VUregs& regs);
    // FCSET: the immediate replaces the flag and drops every in-flight judgement.
    void SetClipImmediate(VUregs& regs, uint32_t imm24);
    // The most recently ISSUED judgement (the CLIP instruction shifts from it).
    uint32_t clip_latest() const { return clip_latest_; }
    int clip_depth() const { return clip_pending_n_; }

    // ---- MAC / STATUS ring (circular, oldest first) ----
    // Queue the flags of one FMAC result (raw lanes + dest mask) in the
    // active representation.
    void UpdateMACFlags(VUregs& regs, const VR_reg& result, uint8_t mask);
    // Eager push of a packed MAC word; a full ring commits its oldest entry now.
    void PushMac(VUregs& regs, uint16_t mac);
    // Lazy push of the raw lanes; a full ring folds its oldest entry.
    void PushMacRaw(const uint32_t bits[4], uint8_t mask);
    // The same pushes for a result ISSUED at `issue_cycle` (<= the clock): the
    // generated blocks deliver their results late, in issue order, and a
    // result that has retired by then is folded directly (FoldLazyRetired /
    // CommitEagerRetired) -- what RetireDue / the ring overflow would do with
    // it, in the same order.
    void PushMacRawAt(const uint32_t bits[4], uint8_t mask, uint64_t issue_cycle);
    void PushMacAt(VUregs& regs, uint16_t mac, uint64_t issue_cycle);
    void FoldLazyRetired(const uint32_t bits[4], uint8_t mask);
    void CommitEagerRetired(VUregs& regs, uint16_t mac) { ApplyMacToStatus(regs, mac); }
    // Retire every due entry (commit or fold).
    void RetireDue(VUregs& regs);
    // Fold the lazy accumulators into the architectural registers.
    void MaterializeMacStatus(VUregs& regs);
    // THE reader entry (FS* / FM* instructions): retire, then materialise.
    void RetireMacStatus(VUregs& regs);
    // Program end: every queued entry retires in order.
    void FlushMacPipe(VUregs& regs);

    int mac_depth() const { return mac_pending_n_; }
    // Logical index i (0 = oldest): the packed MAC word / retire cycle.
    uint16_t PackPendingMac(int i) const;
    uint64_t mac_retire(int i) const;

    // ---- Q latch ----
    // Retire the pending quotient if due; returns the clamped visible Q.
    float VisibleQ(VUregs& regs);
    // Issue an FDIV-pipe result (value clamped here); a still-pending Q is
    // first waited for and committed.
    void PushQ(VUregs& regs, float value, int delay, uint16_t status_bits);
    // Wait for and commit the pending Q (WAITQ, a second FDIV op, program end).
    void FlushQPipe(VUregs& regs);
    bool q_pending() const { return q_pending_valid_; }
    uint64_t q_pending_retire() const { return q_pending_retire_; }
    float q_pending_value() const { return q_pending_value_; }
    uint16_t q_pending_status() const { return q_pending_status_; }
    void SetQPending(bool valid, uint64_t retire, float value, uint16_t status);

    // ---- P latch ----
    // Retire the pending P if due; returns the visible P (no clamp, no stall).
    float VisibleP(VUregs& regs);
    // Issue an EFU result (not clamped here); a still-pending P is released
    // one cycle early first.
    void PushP(VUregs& regs, float value, int cycles);
    // A second EFU op: wait to retire-1 and commit the pending P.
    void ReleasePEarly(VUregs& regs);
    // Program end: wait the full latency and commit the pending P.
    void FlushPPipe(VUregs& regs);
    bool p_pending() const { return p_pending_valid_; }
    uint64_t p_pending_retire() const { return p_pending_retire_; }
    float p_pending_value() const { return p_pending_value_; }
    void SetPPending(bool valid, uint64_t retire, float value);

private:
    void ApplyMacToStatus(VUregs& regs, uint16_t mac);
    void AccumulateLazyEntry(const PendingMac& e);
    void LazyRingOverflow();

    // Hot state first, phased to the host cache line (StallModel::kLineBytes):
    // the clock and the ring counters share one line, the MAC ring (8 x 32 B)
    // starts a line so no entry straddles one. The unit's placement used to
    // decide the phase (bench7 native 9.5 .. 15.0 ns/bundle by phase, E181).
    alignas(128) uint64_t vu_cycle_ = 0;
    int mac_pending_n_ = 0;
    int mac_head_ = 0;
    int clip_pending_n_ = 0;
    uint32_t clip_latest_ = 0;
    bool lazy_;
    alignas(128) PendingMac mac_pending_[kRingDepth] = {};
    alignas(128) PendingClip clip_pending_[kRingDepth] = {};
    alignas(16) uint32_t lazy_acc_[4][4] = {};  // [Z,S,U,O][lane] all-ones where seen (SIMD fold)
    uint16_t lazy_or_mac_ = 0;                  // OR of packed words (scalar fold)
    uint32_t lazy_last_bits_[4] = {};
    uint8_t lazy_last_mask_ = 0;
    bool lazy_have_last_ = false;

    bool q_pending_valid_ = false;
    uint64_t q_pending_retire_ = 0;
    float q_pending_value_ = 0.0f;
    uint16_t q_pending_status_ = 0;

    bool p_pending_valid_ = false;
    uint64_t p_pending_retire_ = 0;
    float p_pending_value_ = 0.0f;
};

#include "engine/vu/flag_pipeline.inl"
