// gs/hle/targets/alias_page_set.h -- AliasPageSet: the two 512-page sets the
// GPU -> VRAM -> GPU alias bookkeeping shares.
//
//   * cpu_dirty: pages whose VRAM bytes were rewritten behind the attachments --
//     by an EE upload (the InvalidateTextures job) or by a colour writeback (the
//     mirror now carries what the GPU drew; a later 16-bit frame over the same
//     bytes must re-import them). Cleared over a draw's rows once the draw made
//     the attachment authoritative again (MarkPagesGpuValid), and consulted by
//     the indexed-serve and keep-serve vetoes.
//   * aliased: pages a writeback deposited; a later frame over any of them
//     imports the mirror before drawing (the two views of one page must agree),
//     and the import clears the pages it fully covered. A page a target's OWN
//     view deposited (its CT32 rows at its own FBP / FBW, the bytes its
//     attachment holds) records that view: a frame in the same view need not
//     import them back (AnyAliasedExcept) -- the round trip was a synchronous
//     readback of the frame for an identity copy, and a lossy one above scale 1.
//     Any other mark, and every EE write (DropOwners), makes the page foreign.
// GS User's Manual 8.1: local memory is 512 pages of 8 KiB. The walks here are
// CLAMPED at page 511, never wrapped: a span past the end marks / tests nothing
// beyond the last page (the single-page queries take the page modulo 512).
#pragma once

#include <cstdint>
#include <vector>

namespace ps2r {
namespace gs {

class AliasPageSet {
public:
    AliasPageSet();

    void MarkCpuDirty(uint32_t addr, uint32_t size);
    void ClearCpuDirty(uint32_t addr, uint32_t size);
    bool AnyCpuDirty(uint32_t addr, uint32_t size) const;
    void MarkAliased(uint32_t addr, uint32_t size);
    // Aliased by the deposit of the frame view `view` (ViewKey): the page records the view unless another
    // mark already made it foreign.
    void MarkAliasedBy(uint32_t addr, uint32_t size, uint64_t view);
    void DropOwners(uint32_t addr, uint32_t size);
    void ClearAliasedPage(uint32_t page);
    void ClearAliased(uint32_t addr, uint32_t size);
    bool AnyAliased(uint32_t addr, uint32_t size) const;
    // Any aliased page that `view` did not deposit itself.
    bool AnyAliasedExcept(uint32_t addr, uint32_t size, uint64_t view) const;
    // A frame view: base (bytes), PSM, FBW (pixels); never 0 (0 = foreign).
    static uint64_t ViewKey(uint32_t fbp, uint32_t psm, uint32_t fbw) {
        return 1u + ((uint64_t(fbp) << 24) | (uint64_t(fbw & 0xFFFFu) << 8) | (psm & 0xFFu));
    }
    bool IsAliased(uint32_t page) const;
    bool IsCpuDirty(uint32_t page) const;

private:
    // [first, last] pages of a span clamped to the 512; false = nothing to walk.
    static bool PageWalk(uint32_t addr, uint32_t size, uint32_t* first, uint32_t* last);
    static void Mark(std::vector<uint64_t>& set, uint32_t addr, uint32_t size, bool on);
    static bool Any(const std::vector<uint64_t>& set, uint32_t addr, uint32_t size);

    void SetOwners(uint32_t addr, uint32_t size, uint64_t view);

    std::vector<uint64_t> cpu_dirty_;
    std::vector<uint64_t> aliased_;
    std::vector<uint64_t> owner_;   // per page: the view whose own deposit aliased it; 0 = foreign
};

}  // namespace gs
}  // namespace ps2r
