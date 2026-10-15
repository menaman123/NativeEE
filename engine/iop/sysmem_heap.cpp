// engine/iop/sysmem_heap.cpp -- SysmemHeap: bump tail + first-fit free list.
#include "engine/iop/sysmem_heap.h"

#include <iterator>

namespace ps2r::iop {

void SysmemHeap::ResetTransient() {
    alloc_sizes_.clear();
    free_blocks_.clear();
}

void SysmemHeap::RestoreHeapPtr(uint32_t heap_ptr) {
    ResetTransient();
    heap_ptr_ = heap_ptr;
}

uint32_t SysmemHeap::Alloc(uint32_t size) {
    size = (size + 15) & ~15u;
    if (size == 0) size = 16;

    // First fit from the free list.
    for (auto it = free_blocks_.begin(); it != free_blocks_.end(); ++it) {
        if (it->second >= size) {
            const uint32_t addr = it->first;
            const uint32_t rem_addr = it->first + size;
            const uint32_t rem_size = it->second - size;
            free_blocks_.erase(it);
            if (rem_size != 0) free_blocks_[rem_addr] = rem_size;
            alloc_sizes_[addr] = size;
            return addr;
        }
    }
    // Bump path: never hand out bytes overlapping the stack band; the sub-band
    // remainder becomes an ordinary free block.
    if (heap_ptr_ < kStackBandHi && heap_ptr_ + size > kStackBandLo) {
        if (heap_ptr_ < kStackBandLo) free_blocks_[heap_ptr_] = kStackBandLo - heap_ptr_;
        heap_ptr_ = kStackBandHi;
    }
    // Arena exhaustion: fail instead of growing into the scratch stacks.
    if (heap_ptr_ + size > kCeiling || heap_ptr_ + size < size) return 0;
    const uint32_t addr = heap_ptr_;
    heap_ptr_ += size;
    alloc_sizes_[addr] = size;
    return addr;
}

uint32_t SysmemHeap::Free(uint32_t addr) {
    auto it = alloc_sizes_.find(addr);
    if (it == alloc_sizes_.end()) return 0;
    const uint32_t size = it->second;
    alloc_sizes_.erase(it);

    // Insert and coalesce with byte-adjacent neighbours.
    auto ins = free_blocks_.emplace(addr, size).first;
    if (ins != free_blocks_.begin()) {
        auto prev = std::prev(ins);
        if (prev->first + prev->second == ins->first) {
            prev->second += ins->second;
            free_blocks_.erase(ins);
            ins = prev;
        }
    }
    auto next = std::next(ins);
    if (next != free_blocks_.end() && ins->first + ins->second == next->first) {
        ins->second += next->second;
        free_blocks_.erase(next);
    }
    // Tail reclaim: a free block ending at the bump pointer shrinks the heap.
    if (ins->first + ins->second == heap_ptr_) {
        heap_ptr_ = ins->first;
        free_blocks_.erase(ins);
    }
    return size;
}

uint32_t SysmemHeap::QueryMaxFree() const {
    uint32_t free_size;
    if (heap_ptr_ <= kStackBandLo) {
        free_size = kStackBandLo - heap_ptr_;
        const uint32_t above = kCeiling - kStackBandHi;
        if (above > free_size) free_size = above;
    } else if (heap_ptr_ < kStackBandHi) {
        free_size = kCeiling - kStackBandHi;   // the tail is never placed inside the band
    } else {
        free_size = (heap_ptr_ <= kCeiling) ? (kCeiling - heap_ptr_) : 0;
    }
    for (const auto& kv : free_blocks_)
        if (kv.second > free_size) free_size = kv.second;
    return free_size;
}

}  // namespace ps2r::iop
