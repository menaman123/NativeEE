// gs/hle/classrdr/sequence_recipe.h -- SequenceRecipe: the stencil-composite
// sequence state machine and its byte arithmetic.
//
// The title's shadow composite accumulates a shadow-volume winding count in a
// 16-bit colour buffer: a CLEAR primes every channel to a base byte, then strips
// blended (Cs - Cd) * FIX + Cd with COLCLAMP off wrap-add a flat +8 (front faces)
// or -8 (back faces) per channel under a DATE test. Rendered as a stencil counter,
// byte_post == (base + 8 * net) mod 256 == (8 * stencil) mod 256 when the stencil
// starts at base >> 3 and counts net with wrapping increments; MATERIALIZE rebuilds
// the bytes with 32 masked constant-colour quads (func EQUAL ref = v mask 0x1F ->
// byte 8 * v). This class holds the STATE of one live sequence (per accumulator
// FBP) and the decisions; the stencil surface, the passes and the quads belong to
// the stencil-composite renderer.
//
//   IDLE   --(native CLEAR whose every vertex carries the base colour)--> PRIMED
//   PRIMED --(an RGB-writing non-strip draw into the fbp)--> IDLE (demote)
//   PRIMED --(first stencil strip)--> ARMED (stencil cleared to the base)
//   ARMED  --(any draw into the fbp / texture read of the fbp / leaving a
//            falling-edge capture / writeback / texture serve of the fbp /
//            non-stencil strip)--> MATERIALIZE, then IDLE
// While ARMED the accumulator's own pending writeback entry is HELD out of the
// stencil ladder's pre-draw flush (HoldsEntry): the deposit lands once, at the
// materialising trigger, over materialised bytes.
// The base colour, the stencil base / counter mask / quant bit, the two strip
// deltas and the invert pass's vertex colour are title data (Constants).
#pragma once

#include <cstdint>
#include <functional>

#include "engine/gs/hle/ir/draw_ir.h"

namespace ps2r {
namespace gs {

class SequenceRecipe {
public:
    enum State : uint8_t { kIdle = 0, kPrimed = 1, kArmed = 2 };
    enum StripDelta : int8_t { kStripDecline = 0, kStripIncr = 1, kStripDecr = -1 };
    enum class Action : uint8_t { kNone = 0, kDemote, kMaterialize };

    struct Constants {
        uint32_t clear_rgba = 0;          // the priming CLEAR colour, every vertex (R | G<<8 | B<<16 | A<<24)
        uint8_t stencil_base = 0;         // == clear byte >> 3
        uint8_t counter_mask = 0;         // net mod (mask + 1); sound when (mask + 1) divides 256
        uint8_t quant_bit = 0;            // counter bit that is byte bit 7 after the quantise pass
        uint8_t incr_rgb[3] = {0, 0, 0};  // flat strip colour meaning +1
        uint8_t decr_rgb[3] = {0, 0, 0};  // flat strip colour meaning -1
        uint8_t invert_rgba[4] = {0, 0, 0, 0};   // the invert pass's pinned vertex colour
        uint32_t rgb_mask = 0x00FFFFFFu;  // FBMSK bits that protect RGB (a draw keeping them all does not demote)
    };

    explicit SequenceRecipe(const Constants& k);
    const Constants& K() const { return k_; }

    // Byte arithmetic.
    uint8_t MaterializeByte(uint8_t stencil_v) const;   // (v & mask) << 3
    uint8_t OracleWrapByte(int net) const;              // (base_byte + 8 * net) & 0xFF
    uint8_t StencilAfter(int net) const;                // (stencil_base + net) & 0xFF
    static uint32_t Csm1Swizzle(uint32_t index);        // CSM1 CLUT grid position of an index
    // The folded invert pass for one pixel: rgb = 255 - Cd, a = 255 - CSM1(idx),
    // idx = the generic index reconstruction of the destination alpha.
    static void InvertFold(uint8_t cd_r, uint8_t cd_g, uint8_t cd_b, uint8_t cd_a, uint8_t out[4]);
    // The generic-chain reference for one invert pixel (CLUT row 1 raw texel at idx,
    // HIGHLIGHT2 with the pinned vertex colour, (Cs - Cd) * 1.0 + 0 clamped).
    static void InvertGenericRef(uint8_t cd_r, uint8_t cd_g, uint8_t cd_b, uint8_t cd_a, uint8_t out[4]);
    // The invert pass's CLUT is the identity-invert table: grid position p (x = p %
    // 16, y = p / 16, CT32 addresses from `addr_of`) holds {FF, FF, FF, 255 -
    // CSM1(p)} for every p. A zero mirror or the pure identity layout rejects.
    static bool ClutIsInvertTable(const std::function<uint8_t(uint32_t)>& byte_at,
                                  const std::function<uint32_t(int, int)>& addr_of);

    // Predicates over a draw.
    StripDelta ClassifyStripColor(uint8_t r, uint8_t g, uint8_t b) const;
    StripDelta ClassifyStripVerts(const DrawIr& ir) const;   // every vertex the same delta colour
    bool ClearPrimes(const DrawIr& ir) const;                // every vertex == clear_rgba
    bool InvertVertexColorOk(const DrawIr& ir) const;        // every vertex == invert_rgba

    // State.
    State GetState() const { return state_; }
    uint32_t Fbp() const { return fbp_; }
    bool Alt16() const { return alt16_; }
    bool Live() const { return state_ != kIdle; }
    bool LiveOn(uint32_t fbp) const { return state_ != kIdle && fbp_ == fbp; }
    void Prime(uint32_t fbp);
    void Arm(bool alt16);      // PRIMED -> ARMED; resets the extent
    void Idle() { state_ = kIdle; }

    // The pre-draw hook decision for ANY draw about to run (native or generic).
    // `leaving_capture`: the draw leaves the armed accumulator while capture
    // gates would snapshot it. A kMaterialize result means the caller must
    // materialise (then the state is IDLE); kDemote means PRIMED died.
    Action OnPreDraw(const DrawIr& ir, bool leaving_capture);
    // A writeback or texture serve of `fbp`.
    Action OnTouch(uint32_t fbp);
    // The pre-draw ledger hold: the armed accumulator's own pending entry.
    bool HoldsEntry(uint32_t entry_fbp) const { return state_ == kArmed && entry_fbp == fbp_; }

    // The union of strip draw rects the materialise must cover (half-open).
    void ExtendExtent(int x0, int y0, int x1, int y1);
    bool ExtentValid() const { return ext_x1_ > ext_x0_ && ext_y1_ > ext_y0_; }
    int ExtX0() const { return ext_x0_; }
    int ExtY0() const { return ext_y0_; }
    int ExtX1() const { return ext_x1_; }
    int ExtY1() const { return ext_y1_; }

    struct Counters {
        uint64_t primes = 0, arms = 0, demotes = 0, materializes = 0;
    };
    const Counters& Count() const { return cnt_; }

private:
    Constants k_;
    State state_ = kIdle;
    uint32_t fbp_ = 0;
    bool alt16_ = false;
    int ext_x0_ = 0, ext_y0_ = 0, ext_x1_ = 0, ext_y1_ = 0;
    Counters cnt_;
};

}  // namespace gs
}  // namespace ps2r
