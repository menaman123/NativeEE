// The table-driven UNPACK lane: one kernel per (format, masked, mode) cell.
// Every rule here restates the scalar reference (VifUnpack::Execute); the
// equivalence test runs both over every cell and every CL/WL shape.
#include "engine/vif/vif_unpack_table.h"

#include <array>
#include <cstring>

#if PS2R_SIMD
#include <arm_neon.h>
#endif

namespace {

constexpr uint32_t kQwMask = 0x3FFu;
// Source elements per vector and bits per element, indexed by the format nibble.
constexpr uint8_t kElems[16] = {1, 1, 1, 0, 2, 2, 2, 0, 3, 3, 3, 0, 4, 4, 4, 1};
constexpr uint8_t kBits[16] = {32, 16, 8, 0, 32, 16, 8, 0, 32, 16, 8, 0, 32, 16, 8, 16};

// ---- the four-lane vector twin: NEON or plain words --------------------------------

#if PS2R_SIMD
struct Lanes {
    uint32x4_t v;
};
inline Lanes LoadWords(const uint32_t* p) { return {vld1q_u32(p)}; }
inline Lanes LoadMem(const uint8_t* p) { return {vld1q_u32(reinterpret_cast<const uint32_t*>(p))}; }
inline void StoreMem(uint8_t* p, Lanes a) { vst1q_u32(reinterpret_cast<uint32_t*>(p), a.v); }
inline Lanes Add(Lanes a, Lanes b) { return {vaddq_u32(a.v, b.v)}; }
// Per lane: sel ? a : b (sel lanes are all-ones or zero).
inline Lanes Select(Lanes sel, Lanes a, Lanes b) { return {vbslq_u32(sel.v, a.v, b.v)}; }
inline Lanes Splat(uint32_t x) { return {vdupq_n_u32(x)}; }
// V4-5: the 5:5:5:1 halfword expanded to {r<<3, g<<3, b<<3, a<<7}.
inline Lanes ExpandV45(uint32_t raw) {
    const int32x4_t shifts = {0, -5, -10, -15};
    const uint32x4_t keep = {0x1Fu, 0x1Fu, 0x1Fu, 0x1u};
    const int32x4_t up = {3, 3, 3, 7};
    uint32x4_t v = vshlq_u32(vdupq_n_u32(raw), shifts);
    return {vshlq_u32(vandq_u32(v, keep), up)};
}
#else
struct Lanes {
    uint32_t v[4];
};
inline Lanes LoadWords(const uint32_t* p) {
    Lanes a;
    std::memcpy(a.v, p, 16);
    return a;
}
inline Lanes LoadMem(const uint8_t* p) {
    Lanes a;
    std::memcpy(a.v, p, 16);
    return a;
}
inline void StoreMem(uint8_t* p, Lanes a) { std::memcpy(p, a.v, 16); }
inline Lanes Add(Lanes a, Lanes b) {
    for (int i = 0; i < 4; i++) a.v[i] += b.v[i];
    return a;
}
inline Lanes Select(Lanes sel, Lanes a, Lanes b) {
    Lanes r;
    for (int i = 0; i < 4; i++) r.v[i] = (a.v[i] & sel.v[i]) | (b.v[i] & ~sel.v[i]);
    return r;
}
inline Lanes Splat(uint32_t x) { return {{x, x, x, x}}; }
inline Lanes ExpandV45(uint32_t raw) {
    return {{((raw >> 0) & 0x1Fu) << 3, ((raw >> 5) & 0x1Fu) << 3, ((raw >> 10) & 0x1Fu) << 3, (raw & 0x8000u) >> 8}};
}
#endif

// ---- source elements ----------------------------------------------------------------

template <uint32_t Bits>
inline uint32_t ReadElement(const uint8_t* p, bool usn) {
    if constexpr (Bits == 8) {
        const uint8_t v = *p;
        return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(v)));
    } else if constexpr (Bits == 16) {
        uint16_t v;
        std::memcpy(&v, p, 2);
        return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v)));
    } else {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }
}

// Decode vector k at `src` (advanced past it); the V3 lookahead and the
// V2-32 / V3-32 W rules as the reference states them.
template <int F>
inline Lanes Decode(const uint8_t*& src, const uint8_t* end, uint32_t k, uint8_t phase, bool usn) {
    constexpr uint32_t bits = kBits[F];
    constexpr uint32_t elems = kElems[F];
    constexpr size_t ebytes = bits / 8;
    if constexpr (F == 0xF) {
        uint16_t raw;
        std::memcpy(&raw, src, 2);
        src += 2;
        return ExpandV45(raw);
    } else if constexpr (F <= 3) {
        const uint32_t val = ReadElement<bits>(src, usn);
        src += ebytes;
        return Splat(val);
    } else if constexpr (elems == 2) {
        uint32_t vec[4];
        vec[0] = ReadElement<bits>(src, usn);
        vec[1] = ReadElement<bits>(src + ebytes, usn);
        src += 2 * ebytes;
        vec[2] = vec[0];
        vec[3] = (bits == 32 && (phase & 1u)) ? 0u : vec[1];
        return LoadWords(vec);
    } else if constexpr (elems == 3) {
        uint32_t vec[4];
        vec[0] = ReadElement<bits>(src, usn);
        vec[1] = ReadElement<bits>(src + ebytes, usn);
        vec[2] = ReadElement<bits>(src + 2 * ebytes, usn);
        src += 3 * ebytes;
        // W = the next element, not consumed; 0 past the payload's end.
        vec[3] = (src + ebytes <= end) ? ReadElement<bits>(src, usn) : 0u;
        if (bits == 32 && ((k & 1u) != static_cast<uint32_t>(phase & 1u))) vec[3] = 0;
        return LoadWords(vec);
    } else {
        uint32_t vec[4];
        vec[0] = ReadElement<bits>(src, usn);
        vec[1] = ReadElement<bits>(src + ebytes, usn);
        vec[2] = ReadElement<bits>(src + 2 * ebytes, usn);
        vec[3] = ReadElement<bits>(src + 3 * ebytes, usn);
        src += 4 * ebytes;
        return LoadWords(vec);
    }
}

// Per MASK row (write cycle & 3): the MODE addend and the mask selection.
struct RowPlan {
    Lanes add;          // mode 1-3: ROW and/or COL[row]
    Lanes write_sel;    // all-ones where the field takes the unpacked value
    Lanes fixed;        // ROW / COL replacement values
    Lanes fixed_sel;    // all-ones where `fixed` applies
    bool keeps = false; // some field keeps the existing quadword (NoWrite)
};

template <bool M, int Mode>
inline void PlanRows(const VifRegs& regs, RowPlan (&rows)[4]) {
    for (uint32_t row = 0; row < 4; row++) {
        uint32_t add[4] = {0, 0, 0, 0};
        uint32_t wsel[4] = {~0u, ~0u, ~0u, ~0u};
        uint32_t fixed[4] = {0, 0, 0, 0};
        uint32_t fsel[4] = {0, 0, 0, 0};
        bool keeps = false;
        for (int f = 0; f < 4; f++) {
            if constexpr (Mode == 1 || Mode == 3) add[f] += regs.row[f];
            if constexpr (Mode == 2 || Mode == 3) add[f] += regs.col[row];
            if constexpr (M) {
                const auto action = static_cast<MaskAction>((regs.mask >> (row * 8 + static_cast<uint32_t>(f) * 2)) & 3u);
                switch (action) {
                    case MaskAction::Write: break;
                    case MaskAction::Row: wsel[f] = 0; fsel[f] = ~0u; fixed[f] = regs.row[f]; break;
                    case MaskAction::Col: wsel[f] = 0; fsel[f] = ~0u; fixed[f] = regs.col[row]; break;
                    case MaskAction::NoWrite: wsel[f] = 0; keeps = true; break;  // the existing quadword stays
                }
            }
        }
        rows[row].add = LoadWords(add);
        rows[row].write_sel = LoadWords(wsel);
        rows[row].fixed = LoadWords(fixed);
        rows[row].fixed_sel = LoadWords(fsel);
        rows[row].keeps = keeps;
    }
}

template <int F, bool M, int Mode>
void Kernel(const VifUnpackTable::Command& c) {
    const uint8_t* src = c.payload;
    const uint8_t* const end = c.payload + c.payload_size;
    RowPlan rows[4];
    if constexpr (M || Mode != 0) PlanRows<M, Mode>(*c.regs, rows);
    uint32_t addr = c.addr_qw & kQwMask;
    uint32_t cycle = 0;
    const uint32_t cl = c.cl, wl = c.wl;
    for (uint32_t k = 0; k < c.n; k++) {
        Lanes v = Decode<F>(src, end, k, c.phase, c.usn);
        uint8_t* dst = c.dmem + static_cast<size_t>(addr & kQwMask) * 16;
        if constexpr (M || Mode != 0) {
            const RowPlan& plan = rows[cycle & 3u];
            if constexpr (Mode != 0) v = Add(v, plan.add);
            if constexpr (M) {
                const Lanes rest = plan.keeps ? Select(plan.fixed_sel, plan.fixed, LoadMem(dst)) : plan.fixed;
                v = Select(plan.write_sel, v, rest);
            }
        }
        StoreMem(dst, v);
        addr++;
        if (++cycle == wl) {
            cycle = 0;
            addr += cl - wl;  // the skip cycles of this block
        }
    }
}

using Fn = void (*)(const VifUnpackTable::Command&);
// The cell table: [format][masked][mode]; the undefined formats 0x3 / 0x7 / 0xB stay null.
using CellTable = std::array<std::array<std::array<Fn, 4>, 2>, 16>;

template <int F>
constexpr void FillFormat(CellTable& t) {
    t[F][0][0] = &Kernel<F, false, 0>;
    t[F][0][1] = &Kernel<F, false, 1>;
    t[F][0][2] = &Kernel<F, false, 2>;
    t[F][0][3] = &Kernel<F, false, 3>;
    t[F][1][0] = &Kernel<F, true, 0>;
    t[F][1][1] = &Kernel<F, true, 1>;
    t[F][1][2] = &Kernel<F, true, 2>;
    t[F][1][3] = &Kernel<F, true, 3>;
}

constexpr CellTable BuildCellTable() {
    CellTable t{};
    FillFormat<0x0>(t);
    FillFormat<0x1>(t);
    FillFormat<0x2>(t);
    FillFormat<0x4>(t);
    FillFormat<0x5>(t);
    FillFormat<0x6>(t);
    FillFormat<0x8>(t);
    FillFormat<0x9>(t);
    FillFormat<0xA>(t);
    FillFormat<0xC>(t);
    FillFormat<0xD>(t);
    FillFormat<0xE>(t);
    FillFormat<0xF>(t);
    return t;
}
constexpr CellTable kTable = BuildCellTable();

}  // namespace

size_t VifUnpackTable::SourceBytes(uint32_t format, uint32_t n) {
    format &= 0xF;
    if (kBits[format] == 0) return 0;
    const size_t vbytes = (format == 0xF) ? 2 : static_cast<size_t>(kElems[format]) * (kBits[format] / 8);
    return vbytes * n;
}

bool VifUnpackTable::Run(const Command& c) {
    const uint32_t format = c.format & 0xF;
    if (kBits[format] == 0 || c.regs == nullptr || c.dmem == nullptr || c.payload == nullptr) return false;
    if (c.cl == 0 || c.wl == 0 || c.cl < c.wl) return false;  // the filling write stays scalar
    if (c.n == 0) return true;
    if (c.payload_size < SourceBytes(format, c.n)) return false;  // partial payload: scalar semantics
    const uint32_t mode = c.regs->mode > 3u ? 0u : c.regs->mode;  // the reference adds nothing for mode > 3
    const Fn fn = kTable[format][c.use_mask ? 1 : 0][mode];
    if (fn == nullptr) return false;
    fn(c);
    return true;
}
