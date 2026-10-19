// UNPACK scalar reference + bulk-lane dispatch.
#include "engine/vif/vif_unpack.h"

#include <algorithm>
#include <cstring>

#include "engine/vif/vif_unpack_bulk.h"
#include "engine/vif/vif_unpack_table.h"
#include "engine/vu/vu_unit.h"

namespace {

// Source elements per vector and bits per element, indexed by the format nibble.
constexpr uint8_t kElems[16] = {1, 1, 1, 0, 2, 2, 2, 0, 3, 3, 3, 0, 4, 4, 4, 1};
constexpr uint8_t kBits[16] = {32, 16, 8, 0, 32, 16, 8, 0, 32, 16, 8, 0, 32, 16, 8, 16};

constexpr uint32_t kQwMask = 0x3FFu;
constexpr uint32_t kVu1ByteMask = 0x3FFFu;

// One source element; `ptr` advances by the element size.
uint32_t ReadElement(const uint8_t*& ptr, uint32_t bits, bool usn) {
    switch (bits) {
        case 8: {
            uint8_t v = *ptr++;
            return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(v)));
        }
        case 16: {
            uint16_t v;
            std::memcpy(&v, ptr, 2);
            ptr += 2;
            return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v)));
        }
        case 32: {
            uint32_t v;
            std::memcpy(&v, ptr, 4);
            ptr += 4;
            return v;
        }
        default:
            return 0;
    }
}

}  // namespace

VifUnpack::VifUnpack(VifUnpackSettings settings) : settings_(settings) {}

bool VifUnpack::IsUnpackCmd(uint32_t cmd) { return cmd >= 0x60 && cmd <= 0x7F; }

uint32_t VifUnpack::GetUnpackSize(uint32_t cmd, uint32_t num, uint32_t cl, uint32_t wl) {
    const uint32_t format = cmd & 0xF;
    const uint32_t bits = kBits[format];
    if (bits == 0) return 0;
    const uint32_t real_num = (num == 0) ? 256 : num;
    uint32_t source_vectors;
    if (cl >= wl || cl == 0 || wl == 0) {
        source_vectors = real_num;  // skipping write: every vector comes from the source
    } else {
        const uint32_t full = real_num / wl;  // filling write: CL source vectors per WL writes
        const uint32_t rem = real_num % wl;
        source_vectors = full * cl + std::min(rem, cl);
    }
    const uint32_t elems = (format == 0xF) ? 1 : (format <= 3) ? 1 : kElems[format];
    const uint32_t total_bytes = (source_vectors * elems * bits + 7) / 8;
    return (total_bytes + 3) & ~3u;
}

MaskAction VifUnpack::MaskActionFor(uint32_t mask, int field, uint32_t write_cycle) {
    const uint32_t row = write_cycle & 3u;
    const uint32_t bit = row * 8 + static_cast<uint32_t>(field) * 2;
    return static_cast<MaskAction>((mask >> bit) & 3u);
}

void VifUnpack::Execute(int vif, VuUnit& vu, const VifRegs& regs, uint32_t cmd, uint32_t imm,
                        uint32_t num, const uint8_t* payload, size_t payload_size, bool use_mask,
                        uint8_t unpack_phase) {
    const bool use_tops = (imm & 0x8000) != 0;
    const bool usn = (imm & 0x4000) != 0;
    uint32_t addr_qw = imm & kQwMask;
    if (use_tops && vif == 1) addr_qw = (addr_qw + regs.tops) & kQwMask;

    const uint32_t format = cmd & 0xF;
    const uint32_t bits = kBits[format];
    const uint32_t src_elems = kElems[format];
    if (bits == 0) return;

    const bool is_s_type = format <= 3;
    const bool is_v4_5 = format == 0xF;
    const uint32_t n = (num == 0) ? 256 : num;
    uint32_t cl = VifRegsCl(regs);
    uint32_t wl = VifRegsWl(regs);
    if (cl == 0) cl = 1;
    if (wl == 0) wl = 1;
    const uint32_t mode = regs.mode;

    if (settings_.bulk_lanes && vif == 1 && !use_mask && mode == 0) {
        uint8_t* dmem = vu.state().data;
        if (cl == wl && (format == 0x8 || format == 0xC || format == 0xD || format == 0xE)) {
            if (VifUnpackBulk::Unpack(format, usn, addr_qw, n, payload, payload_size, unpack_phase,
                                      dmem)) {
                bulk_cmds_++;
                bulk_qw_ += n;
                return;
            }
        }
        if (wl == 1 && cl > 1 && (format == 0x8 || format == 0xC || format == 0xE)) {
            if (VifUnpackBulk::UnpackSkip(format, usn, addr_qw, n, payload, payload_size,
                                          unpack_phase, cl, dmem)) {
                skip_cmds_++;
                skip_qw_ += n;
                return;
            }
        }
    }
    if (settings_.bulk_lanes && vif == 1 && cl >= wl) {
        // The table lane: every other skipping-write shape (masked, MODE 1-3,
        // S / V2 / V3-16 / V3-8 / V4-5, WL > 1); declines partial payloads.
        VifUnpackTable::Command c;
        c.format = format;
        c.usn = usn;
        c.use_mask = use_mask;
        c.addr_qw = addr_qw;
        c.n = n;
        c.cl = cl;
        c.wl = wl;
        c.regs = &regs;
        c.payload = payload;
        c.payload_size = payload_size;
        c.phase = unpack_phase;
        c.dmem = vu.state().data;
        if (VifUnpackTable::Run(c)) {
            table_cmds_++;
            table_qw_ += n;
            return;
        }
    }
    scalar_cmds_++;
    scalar_qw_ += n;

    const uint8_t* src_ptr = payload;
    const uint8_t* src_end = payload + payload_size;
    uint32_t write_cycle = 0;
    uint32_t written = 0;
    const uint32_t max_iter = n * cl + 16;
    for (uint32_t i = 0; written < n && i < max_iter; i++) {
        const uint32_t cycle_pos = i % cl;
        if (cycle_pos >= wl) {
            addr_qw = (addr_qw + 1) & kQwMask;  // skip cycle: advance, consume nothing
            continue;
        }

        uint32_t vec[4] = {0, 0, 0, 0};
        if (is_v4_5) {
            if (src_ptr + 2 <= src_end) {
                uint16_t raw;
                std::memcpy(&raw, src_ptr, 2);
                src_ptr += 2;
                vec[0] = ((raw >> 0) & 0x1F) << 3;
                vec[1] = ((raw >> 5) & 0x1F) << 3;
                vec[2] = ((raw >> 10) & 0x1F) << 3;
                vec[3] = (raw & 0x8000) >> 8;
            }
        } else if (is_s_type) {
            if (src_ptr < src_end) {
                const uint32_t val = ReadElement(src_ptr, bits, usn);
                vec[0] = vec[1] = vec[2] = vec[3] = val;
            }
        } else {
            for (uint32_t e = 0; e < src_elems && src_ptr < src_end; e++)
                vec[e] = ReadElement(src_ptr, bits, usn);
            if (src_elems == 2) {
                vec[2] = vec[0];
                vec[3] = vec[1];
                if (bits == 32 && (unpack_phase & 1)) vec[3] = 0;
            } else if (src_elems == 3) {
                const uint8_t* peek = src_ptr;  // W = the next word, not consumed
                if (peek < src_end) vec[3] = ReadElement(peek, bits, usn);
                if (bits == 32 && ((written & 1) != (unpack_phase & 1u))) vec[3] = 0;
            }
        }

        if (mode != 0) {
            const uint32_t col_idx = write_cycle & 3u;
            for (int f = 0; f < 4; f++) {
                uint32_t add = 0;
                if (mode == 1 || mode == 3) add += regs.row[f];
                if (mode == 2 || mode == 3) add += regs.col[col_idx];
                vec[f] += add;
            }
        }

        const uint32_t dest_addr = (addr_qw * 16) & kVu1ByteMask;
        if (use_mask) {
            uint32_t existing[4];
            vu.ReadDataMem(dest_addr, reinterpret_cast<uint8_t*>(existing), 16);
            const uint32_t mask_row = write_cycle & 3u;
            uint32_t final_vec[4] = {0, 0, 0, 0};
            for (int f = 0; f < 4; f++) {
                switch (MaskActionFor(regs.mask, f, mask_row)) {
                    case MaskAction::Write: final_vec[f] = vec[f]; break;
                    case MaskAction::NoWrite: final_vec[f] = existing[f]; break;
                    case MaskAction::Row: final_vec[f] = regs.row[f]; break;
                    case MaskAction::Col: final_vec[f] = regs.col[mask_row]; break;
                }
            }
            vu.WriteDataMem(dest_addr, reinterpret_cast<const uint8_t*>(final_vec), 16);
        } else {
            vu.WriteDataMem(dest_addr, reinterpret_cast<const uint8_t*>(vec), 16);
        }

        written++;
        write_cycle++;
        if (write_cycle >= wl) write_cycle = 0;
        addr_qw = (addr_qw + 1) & kQwMask;
    }
}
