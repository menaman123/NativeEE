#pragma once
// VifMmioGate: the MmioHandler the composition root registers for the two VIF
// register files and the VIF1 FIFO window, in front of `Vif`.
//
// The reference routed only a subset of the VIF register map to its VIF unit:
// reads of FBRST (+0x10) and ERR (+0x20) on either unit, writes of FBRST/ERR
// on either unit plus the VIF1 FIFO window (0x10005000..0x1000500C). Every
// other VIF register read returned 0 (the STAT poll therefore always saw an
// idle unit) and every other VIF register write was dropped. The frozen
// baselines were recorded under that surface (P5-VIF-MMIO-OBSERVABLE), so
// the router must present the same one even though `Vif` itself implements
// the full register file. The MTVU VIF1_STAT busy view is the MTVU stage's.
//
// Gated accesses are counted (`ReadsZeroed`, `WritesDropped`) so a leg's
// census can show how often the game touched the unrouted registers.
#include <cstdint>

#include "engine/ee/mem/mmio_handler.h"

class Vif;

class VifMmioGate final : public MmioHandler {
public:
    explicit VifMmioGate(Vif& vif) : vif_(vif) {}

    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    uint64_t ReadsZeroed() const { return reads_zeroed_; }
    uint64_t WritesDropped() const { return writes_dropped_; }

private:
    // True when `addr` is FBRST or ERR of either unit's register file.
    static bool RoutedRegister(uint32_t addr);
    static bool InVif1Fifo(uint32_t addr);

    Vif& vif_;
    uint64_t reads_zeroed_ = 0;
    uint64_t writes_dropped_ = 0;
};
