// gs/hle/ir/draw_coalescer.h -- DrawCoalescer: folds a run of DrawIr records that
// provably draw the same pixels as ONE record into that one record, ahead of the
// pipeline (both backends see fewer, longer draws; the per-draw floor of a host
// driver call -- ~2.7 us on the GL arm -- is the cost it removes).
//
// Rule 1, the ink lane pair (Sly 2's cel outline: one GS LINE per silhouette edge,
// drawn as a pair of 2-vertex LINE draws over the SAME two vertices -- first ATST
// NEVER / AFAIL RGB_ONLY under ZTST (the colour, no Z write), then ATST NEVER /
// AFAIL ZB_ONLY (the Z, no colour write); 474-516 pairs a frame, every pair in a
// block under identical registers). A run A1 B1 A2 B2 ... An Bn becomes TWO draws,
// A1..An then B1..Bn, which is pixel-identical because:
//   - no draw of the run writes alpha, reads a texture or fogs, and none BLENDS in
//     effect: ABE is off, or ABE is on with ALPHA 0/1/0/1 and As = 0x80, which the
//     GS evaluates as ((Cs - Cd) * 0x80 >> 7) + Cd = Cs exactly (the game's colour
//     pass is PRIM 0xc1 = LINE ABE AA1 with that tuple; its Z pass has ABE off);
//   - every A writes one constant colour (all vertices of all A's carry the same
//     RGBA): colour writes are idempotent, so what matters per pixel is only
//     WHETHER some A wrote it;
//   - a B writes Z under the same compare as its A and over the same geometry, so
//     Z after the run is the same max / last-wins fold in both orders (the A's
//     never write Z, moving them changes no Z write);
//   - under ZTST GREATER / GEQUAL / ALWAYS the depth seen by A_j in stream order is
//     max(z0, z_1..z_{j-1}) (each B_i wrote at most z_i); A_j writes pixel p iff
//     z_j(p) > that. The smallest j with z_j(p) > z0(p) has every earlier
//     z_i(p) <= z0(p), so it writes in stream order too: "some A wrote p" is
//     the same predicate (exists j: z_j(p) > z0(p)) in both orders. It needs
//     every B to be the partner of the A right before it (same positions): a B
//     without its A would raise Z where no colour was written, and a later A in
//     the reordered run would then colour a pixel the stream did not.
// Trailing / unpaired A's are harmless (they write no Z); an orphan B ends the run
// and passes through on its own.
//
// Boundaries the fold never crosses: any field of the register state other than
// AFAIL and ABE (identity blend, see above), the target (FRAME / ZBUF), the vertex colour, src_frame / vram_epoch, a
// non-candidate draw, a job (the caller flushes) and a frame record (the caller
// flushes). Nothing here is host- or backend-specific.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"

namespace ps2r {
namespace gs {

class DrawCoalescer {
public:
    struct Counters {
        uint64_t in = 0;              // draws offered
        uint64_t passed = 0;          // draws the caller delivered unchanged (Push returned false)
        uint64_t out = 0;             // merged draws flushed
        uint64_t absorbed = 0;        // draws folded away (in - passed - out)
        uint64_t runs = 0;            // runs flushed (one or two draws each)
        uint64_t pairs = 0;           // A/B pairs folded
        uint64_t reject_orphan_z = 0; // a ZB_ONLY line with no partner A before it
        uint64_t reject_colour = 0;   // an A whose colour differs from the run's
        uint64_t reject_state = 0;    // a candidate whose registers differ from the run's
    };

    // Offer a draw. True: the coalescer took it (it will come out of a later Flush,
    // possibly folded). False: the draw is NOT a candidate (or an orphan) -- `out`
    // then already holds whatever the run before it produced, and the caller must
    // deliver the draws in `out` FIRST, then `d` itself, in that order.
    bool Push(DrawIr&& d, std::vector<DrawIr>* out);
    // Appends the pending run's draws (0, 1 or 2) to `out` and resets.
    void Flush(std::vector<DrawIr>* out);
    bool Pending() const { return have_run_; }
    const Counters& Count() const { return n_; }

    // Rule 1's candidate: a 2-vertex LINE, untextured, unfogged, no DATE, ATST NEVER
    // with AFAIL RGB_ONLY or ZB_ONLY, both vertices one colour, and no blend in
    // effect (IdentityBlend).
    static bool InkCandidate(const DrawIr& d);
    // ABE off, or ABE on with ALPHA 0/1/0/1 and As = 0x80 (Cv = Cs exactly).
    static bool IdentityBlend(const DrawIr& d);
    // Everything but AFAIL, ABE (and the vertices) equal.
    static bool SameRegisters(const DrawIr& a, const DrawIr& b);

private:
    void Start(DrawIr&& a);
    static void Finalize(DrawIr& d);

    bool have_run_ = false;
    DrawIr rgb_;                   // the A lane (the first A's record, vertices appended)
    DrawIr z_;                     // the B lane (the first B's record, vertices appended)
    bool have_z_ = false;
    bool expect_z_ = false;        // the last accepted draw was an A whose partner may follow
    float last_a_pos_[6] = {0, 0, 0, 0, 0, 0};   // that A's (x, y, z) x 2
    uint8_t colour_[4] = {0, 0, 0, 0};
    Counters n_;
};

}  // namespace gs
}  // namespace ps2r
