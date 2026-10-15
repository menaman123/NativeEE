// gs/hle/targets/alias_page_set.cpp -- 512-bit page sets over clamped byte spans.
#include "engine/gs/hle/targets/alias_page_set.h"

#include "engine/gs/vram/vram_image.h"

namespace ps2r {
namespace gs {

AliasPageSet::AliasPageSet() : cpu_dirty_(8, 0), aliased_(8, 0) {}

// The page walk of [addr, addr+size) CLAMPED to the 512 pages: a span starting
// past the end marks / tests nothing, a span running past the end stops at page
// 511. No 4 MiB wrap -- the alias bookkeeping never touches a low page for a
// high frame region (the writeback that fed it is clamped the same way).
bool AliasPageSet::PageWalk(uint32_t addr, uint32_t size, uint32_t* first, uint32_t* last) {
    if (size == 0) return false;
    const uint64_t f = uint64_t(addr) / VramImage::kPageBytes;
    uint64_t l = (uint64_t(addr) + size - 1) / VramImage::kPageBytes;
    if (f >= VramImage::kPageCount) return false;
    if (l >= VramImage::kPageCount) l = VramImage::kPageCount - 1;
    *first = static_cast<uint32_t>(f);
    *last = static_cast<uint32_t>(l);
    return true;
}

void AliasPageSet::Mark(std::vector<uint64_t>& set, uint32_t addr, uint32_t size, bool on) {
    uint32_t first, last;
    if (!PageWalk(addr, size, &first, &last)) return;
    for (uint32_t p = first; p <= last; ++p) {
        if (on) set[p / 64] |= uint64_t(1) << (p % 64);
        else set[p / 64] &= ~(uint64_t(1) << (p % 64));
    }
}

bool AliasPageSet::Any(const std::vector<uint64_t>& set, uint32_t addr, uint32_t size) {
    uint32_t first, last;
    if (!PageWalk(addr, size, &first, &last)) return false;
    for (uint32_t p = first; p <= last; ++p)
        if (set[p / 64] & (uint64_t(1) << (p % 64))) return true;
    return false;
}

void AliasPageSet::MarkCpuDirty(uint32_t addr, uint32_t size) { Mark(cpu_dirty_, addr, size, true); }
void AliasPageSet::ClearCpuDirty(uint32_t addr, uint32_t size) { Mark(cpu_dirty_, addr, size, false); }
bool AliasPageSet::AnyCpuDirty(uint32_t addr, uint32_t size) const { return Any(cpu_dirty_, addr, size); }
void AliasPageSet::MarkAliased(uint32_t addr, uint32_t size) { Mark(aliased_, addr, size, true); }
void AliasPageSet::ClearAliased(uint32_t addr, uint32_t size) { Mark(aliased_, addr, size, false); }
bool AliasPageSet::AnyAliased(uint32_t addr, uint32_t size) const { return Any(aliased_, addr, size); }

void AliasPageSet::ClearAliasedPage(uint32_t page) {
    const uint32_t p = page % VramImage::kPageCount;
    aliased_[p / 64] &= ~(uint64_t(1) << (p % 64));
}

bool AliasPageSet::IsAliased(uint32_t page) const {
    const uint32_t p = page % VramImage::kPageCount;
    return (aliased_[p / 64] >> (p % 64)) & 1u;
}

bool AliasPageSet::IsCpuDirty(uint32_t page) const {
    const uint32_t p = page % VramImage::kPageCount;
    return (cpu_dirty_[p / 64] >> (p % 64)) & 1u;
}

}  // namespace gs
}  // namespace ps2r
