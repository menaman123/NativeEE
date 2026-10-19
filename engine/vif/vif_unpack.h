#pragma once
// UNPACK: expands a VIF payload into VU data-memory quadwords. The scalar loop
// is the reference for every format / sign / mask / mode / CL-WL shape; the
// bulk lanes (VifUnpackBulk) take the plainest VIF1 shapes first, the table
// lane (VifUnpackTable) every other skipping-write shape (CL >= WL); both are
// byte-identical to it. The scalar loop runs the filling write (CL < WL),
// VIF0 and every partial payload.
//
// PS2 rules (EE User's Manual 6.3.5): the format nibble selects S/V2/V3/V4 x
// 32/16/8 bits or V4-5; S replicates to all fields, V2 writes {x,y,x,y}, V3
// takes W from the following word; USN zero-extends, otherwise sign-extend;
// MODE adds ROW (and/or COL of the write cycle); a masked UNPACK consults MASK
// per field and write cycle (rows 0-2 then row 3 repeats); CL >= WL skips
// CL-WL destination quadwords per cycle, CL < WL writes every cycle with the
// source read only CL times; the destination wraps at 1024 quadwords; TOPS is
// added to the address for VIF1 when bit 15 of the immediate is set.
#include <cstddef>
#include <cstdint>

#include "engine/vif/vif_regs.h"

class VuUnit;

// Module-local knob: the bulk lanes are the norm; false forces the scalar loop
// (the equivalence proof drives both). Not a configuration setting.
struct VifUnpackSettings {
    bool bulk_lanes = true;
};

class VifUnpack {
public:
    explicit VifUnpack(VifUnpackSettings settings = VifUnpackSettings{});

    static bool IsUnpackCmd(uint32_t cmd);
    // Payload bytes a command consumes (rounded up to a 32-bit word; 0 for an undefined format).
    static uint32_t GetUnpackSize(uint32_t cmd, uint32_t num, uint32_t cl, uint32_t wl);
    // The 2-bit MASK entry of `field` for write cycle `write_cycle` (row = cycle & 3).
    static MaskAction MaskActionFor(uint32_t mask, int field, uint32_t write_cycle);

    // Run one UNPACK on unit `vif` (0/1) writing into `vu`. `payload` holds the
    // command's data words plus, when the feed appended it, one lookahead word.
    void Execute(int vif, VuUnit& vu, const VifRegs& regs, uint32_t cmd, uint32_t imm, uint32_t num,
                 const uint8_t* payload, size_t payload_size, bool use_mask, uint8_t unpack_phase);

    // Lane census (single writer): commands and quadwords taken by each lane.
    uint64_t bulk_cmds() const { return bulk_cmds_; }
    uint64_t bulk_qw() const { return bulk_qw_; }
    uint64_t skip_cmds() const { return skip_cmds_; }
    uint64_t skip_qw() const { return skip_qw_; }
    uint64_t table_cmds() const { return table_cmds_; }
    uint64_t table_qw() const { return table_qw_; }
    uint64_t scalar_cmds() const { return scalar_cmds_; }
    uint64_t scalar_qw() const { return scalar_qw_; }

    VifUnpackSettings& settings() { return settings_; }
    const VifUnpackSettings& settings() const { return settings_; }

private:
    VifUnpackSettings settings_;
    uint64_t bulk_cmds_ = 0;
    uint64_t bulk_qw_ = 0;
    uint64_t skip_cmds_ = 0;
    uint64_t skip_qw_ = 0;
    uint64_t table_cmds_ = 0;
    uint64_t table_qw_ = 0;
    uint64_t scalar_cmds_ = 0;
    uint64_t scalar_qw_ = 0;
};
