#include "engine/ee/mem/fast_mem.h"

#include <cstring>

#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"

FastMem::FastMem(MainMemory& ram, AddressMap& map, MmioRouter& mmio)
    : ram_(ram), map_(map), mmio_(mmio), ram_base_(ram.Data()), spad_base_(map.Spad().Data()) {}

FastMem::WriteObserver FastMem::observer_ = nullptr;
void* FastMem::observer_user_ = nullptr;
uint32_t FastMem::observe_lo_ = 0;
uint32_t FastMem::observe_hi_ = 0;

void FastMem::SetWriteObserver(WriteObserver fn, void* user, uint32_t lo, uint32_t hi) {
    observer_ = fn;
    observer_user_ = user;
    observe_lo_ = lo;
    observe_hi_ = hi;
}

void FastMem::ObserveWrite(uint32_t phys, uint32_t addr, const void* value, unsigned width) {
    uint64_t old_bits = 0, new_bits = 0;
    std::memcpy(&old_bits, ram_base_ + phys, width);
    std::memcpy(&new_bits, value, width);
    observer_(observer_user_, addr, width, old_bits, new_bits);
}

void FastMem::ReadQuadSlow(uint32_t addr, QuadWord& out) {
    const uint32_t aligned = addr & ~0xFu;
    if (MmioRouter::IsIoRegister(aligned)) {
        out.u64[0] = mmio_.Read(aligned, 8);
        out.u64[1] = mmio_.Read(aligned + 8, 8);
        return;
    }
    std::memset(&out, 0, sizeof(out));
    if (const uint8_t* p = map_.Translate(aligned, sizeof(QuadWord))) std::memcpy(&out, p, sizeof(QuadWord));
}

void FastMem::WriteQuadSlow(uint32_t addr, const QuadWord& value) {
    const uint32_t aligned = addr & ~0xFu;
    if (MmioRouter::IsIoRegister(aligned)) {
        mmio_.Write(aligned, 8, value.u64[0]);
        mmio_.Write(aligned + 8, 8, value.u64[1]);
        return;
    }
    if (uint8_t* p = map_.Translate(aligned, sizeof(QuadWord))) std::memcpy(p, &value, sizeof(QuadWord));
}
