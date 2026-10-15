#include "engine/vif/vif_mmio_gate.h"

#include "engine/vif/vif.h"
#include "engine/vif/vif_regs.h"

bool VifMmioGate::RoutedRegister(uint32_t addr) {
    uint32_t offset;
    if (addr >= Vif::kVif0Base && addr < Vif::kVif0Base + Vif::kRegsSize) offset = addr - Vif::kVif0Base;
    else if (addr >= Vif::kVif1Base && addr < Vif::kVif1Base + Vif::kRegsSize) offset = addr - Vif::kVif1Base;
    else return false;
    return offset == VifRegOffset::kFbrst || offset == VifRegOffset::kErr;
}

bool VifMmioGate::InVif1Fifo(uint32_t addr) {
    return addr >= Vif::kVif1FifoBase && addr < Vif::kVif1FifoBase + Vif::kFifoSize;
}

uint64_t VifMmioGate::Read(uint32_t addr, unsigned size) {
    if (RoutedRegister(addr)) return vif_.Read(addr, size);
    ++reads_zeroed_;
    return 0;
}

void VifMmioGate::Write(uint32_t addr, unsigned size, uint64_t value) {
    if (RoutedRegister(addr) || InVif1Fifo(addr)) {
        vif_.Write(addr, size, value);
        return;
    }
    ++writes_dropped_;
}
