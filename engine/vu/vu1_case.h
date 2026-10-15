#pragma once
// One recorded VU1 program run ("VUC1" file, version 1): the complete unit
// state at the moment a VU1 program was kicked -- registers, both memories,
// the VIF1 TOP/ITOP sources, the in-flight Q/P latches and the VU clock.
// FROZEN binary layout (33,456 bytes; every corpus file is exactly this
// record written raw): the replay tool, the cached-tier compare and the
// program catalog all read these files, so field order, sizes and padding
// never change. Zero primary types (POD + two free functions).
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "engine/ee/cpu/cpu_state.h"

struct Vu1Case {
    static constexpr uint32_t kMagic = 0x31435556u;  // "VUC1" little-endian
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kMicroSize = 16384;
    static constexpr uint32_t kDataSize = 16384;

    uint32_t magic;
    uint32_t version;
    uint32_t start_pc;  // byte address the program was started at
    uint32_t reserved0;
    VUregs regs;        // the frozen EE register block, raw
    uint8_t micro_mem[kMicroSize];
    uint8_t data_mem[kDataSize];
    uint32_t vif1_top;
    uint32_t vif1_tops;
    uint32_t vif1_itop;
    uint32_t vif1_itops;
    uint8_t q_pending_valid;
    uint8_t p_pending_valid;
    uint8_t reserved1[6];
    uint64_t q_pending_retire;
    uint32_t q_pending_value_bits;
    uint32_t q_pending_status;
    uint64_t p_pending_retire;
    uint32_t p_pending_value_bits;
    uint32_t reserved2;
    uint64_t vu_cycle;
};

static_assert(std::is_trivially_copyable<Vu1Case>::value, "Vu1Case is written raw");
static_assert(sizeof(VUregs) == 608, "VUregs is the frozen EE register block");
static_assert(offsetof(Vu1Case, regs) == 16, "Vu1Case.regs offset");
static_assert(offsetof(Vu1Case, micro_mem) == 624, "Vu1Case.micro_mem offset");
static_assert(offsetof(Vu1Case, data_mem) == 17008, "Vu1Case.data_mem offset");
static_assert(offsetof(Vu1Case, vif1_top) == 33392, "Vu1Case.vif1_top offset");
static_assert(offsetof(Vu1Case, q_pending_valid) == 33408, "Vu1Case.q_pending_valid offset");
static_assert(offsetof(Vu1Case, q_pending_retire) == 33416, "Vu1Case.q_pending_retire offset");
static_assert(offsetof(Vu1Case, p_pending_retire) == 33432, "Vu1Case.p_pending_retire offset");
static_assert(offsetof(Vu1Case, vu_cycle) == 33448, "Vu1Case.vu_cycle offset");
static_assert(sizeof(Vu1Case) == 33456, "Vu1Case is the frozen 33,456-byte record");

namespace vu1_case {

// Whole-record raw read; true only if the file held a full record with the
// expected magic and version.
bool ReadCase(const char* path, Vu1Case& c);
// Whole-record raw write.
bool WriteCase(const char* path, const Vu1Case& c);

}  // namespace vu1_case
