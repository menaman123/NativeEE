#include "engine/ipu/mpeg_coeff_vlc.h"

#include <array>
#include <cstddef>

namespace ps2r::ipu {
namespace {

// One table row: the code bits as written in the spec (MSB first, without the
// trailing sign bit), its length, and the (run, level) it stands for.
struct Code {
    const char* bits;
    uint8_t run;
    uint8_t level;
};

// Table B-14 (table zero). "1s" first-coefficient and "11s" forms are handled in
// code; EOB "10" and ESCAPE "000001" too. Every entry below is followed by a sign bit.
constexpr Code kTableZero[] = {
    {"011", 1, 1},   {"0100", 0, 2},  {"0101", 2, 1},  {"00101", 0, 3},  {"00111", 3, 1},  {"00110", 4, 1},
    {"000110", 1, 2}, {"000111", 5, 1}, {"000101", 6, 1}, {"000100", 7, 1},
    {"0000110", 0, 4}, {"0000100", 2, 2}, {"0000111", 8, 1}, {"0000101", 9, 1},
    {"00100110", 0, 5}, {"00100001", 0, 6}, {"00100101", 1, 3}, {"00100100", 3, 2}, {"00100111", 10, 1},
    {"00100011", 11, 1}, {"00100010", 12, 1}, {"00100000", 13, 1},
    {"0000001010", 0, 7}, {"0000001100", 1, 4}, {"0000001011", 2, 3}, {"0000001111", 4, 2}, {"0000001001", 5, 2},
    {"0000001110", 14, 1}, {"0000001101", 15, 1}, {"0000001000", 16, 1},
    {"000000011101", 0, 8}, {"000000011000", 0, 9}, {"000000010011", 0, 10}, {"000000010000", 0, 11},
    {"000000011011", 1, 5}, {"000000010100", 2, 4}, {"000000011100", 3, 3}, {"000000010010", 4, 3},
    {"000000011110", 6, 2}, {"000000010101", 7, 2}, {"000000010001", 8, 2}, {"000000011111", 17, 1},
    {"000000011010", 18, 1}, {"000000011001", 19, 1}, {"000000010111", 20, 1}, {"000000010110", 21, 1},
    {"0000000011010", 0, 12}, {"0000000011001", 0, 13}, {"0000000011000", 0, 14}, {"0000000010111", 0, 15},
    {"0000000010110", 1, 6}, {"0000000010101", 1, 7}, {"0000000010100", 2, 5}, {"0000000010011", 3, 4},
    {"0000000010010", 5, 3}, {"0000000010001", 9, 2}, {"0000000010000", 10, 2}, {"0000000011111", 22, 1},
    {"0000000011110", 23, 1}, {"0000000011101", 24, 1}, {"0000000011100", 25, 1}, {"0000000011011", 26, 1},
    {"00000000011111", 0, 16}, {"00000000011110", 0, 17}, {"00000000011101", 0, 18}, {"00000000011100", 0, 19},
    {"00000000011011", 0, 20}, {"00000000011010", 0, 21}, {"00000000011001", 0, 22}, {"00000000011000", 0, 23},
    {"00000000010111", 0, 24}, {"00000000010110", 0, 25}, {"00000000010101", 0, 26}, {"00000000010100", 0, 27},
    {"00000000010011", 0, 28}, {"00000000010010", 0, 29}, {"00000000010001", 0, 30}, {"00000000010000", 0, 31},
    {"000000000011000", 0, 32}, {"000000000010111", 0, 33}, {"000000000010110", 0, 34}, {"000000000010101", 0, 35},
    {"000000000010100", 0, 36}, {"000000000010011", 0, 37}, {"000000000010010", 0, 38}, {"000000000010001", 0, 39},
    {"000000000010000", 0, 40}, {"000000000011111", 1, 8}, {"000000000011110", 1, 9}, {"000000000011101", 1, 10},
    {"000000000011100", 1, 11}, {"000000000011011", 1, 12}, {"000000000011010", 1, 13}, {"000000000011001", 1, 14},
    {"0000000000010011", 1, 15}, {"0000000000010010", 1, 16}, {"0000000000010001", 1, 17}, {"0000000000010000", 1, 18},
    {"0000000000010100", 6, 3}, {"0000000000011010", 11, 2}, {"0000000000011001", 12, 2}, {"0000000000011000", 13, 2},
    {"0000000000010111", 14, 2}, {"0000000000010110", 15, 2}, {"0000000000010101", 16, 2}, {"0000000000011111", 27, 1},
    {"0000000000011110", 28, 1}, {"0000000000011101", 29, 1}, {"0000000000011100", 30, 1}, {"0000000000011011", 31, 1},
};

// Table B-15 (table one): the short codes differ, the 13..16-bit codes are table zero's.
constexpr Code kTableOne[] = {
    {"10", 0, 1},    {"010", 1, 1},   {"110", 0, 2},   {"00101", 2, 1},  {"0111", 0, 3},   {"00111", 3, 1},
    {"000110", 4, 1}, {"00110", 1, 2}, {"000111", 5, 1}, {"0000110", 6, 1}, {"0000100", 7, 1}, {"11100", 0, 4},
    {"0000111", 2, 2}, {"0000101", 8, 1}, {"1111000", 9, 1},
    {"11101", 0, 5}, {"000101", 0, 6}, {"1111001", 1, 3}, {"00100110", 3, 2}, {"1111010", 10, 1},
    {"00100001", 11, 1}, {"00100101", 12, 1}, {"00100100", 13, 1},
    {"000100", 0, 7}, {"00100111", 1, 4}, {"11111100", 2, 3}, {"11111101", 4, 2}, {"000000100", 5, 2},
    {"000000101", 14, 1}, {"000000111", 15, 1}, {"0000001101", 16, 1},
    {"1111011", 0, 8}, {"1111100", 0, 9}, {"00100011", 0, 10}, {"00100010", 0, 11}, {"00100000", 1, 5},
    {"0000001100", 2, 4}, {"000000011100", 3, 3}, {"000000010010", 4, 3}, {"000000011110", 6, 2},
    {"000000010101", 7, 2}, {"000000010001", 8, 2}, {"000000011111", 17, 1}, {"000000011010", 18, 1},
    {"000000011001", 19, 1}, {"000000010111", 20, 1}, {"000000010110", 21, 1},
    {"11111010", 0, 12}, {"11111011", 0, 13}, {"11111110", 0, 14}, {"11111111", 0, 15},
    {"0000000010110", 1, 6}, {"0000000010101", 1, 7}, {"0000000010100", 2, 5}, {"0000000010011", 3, 4},
    {"0000000010010", 5, 3}, {"0000000010001", 9, 2}, {"0000000010000", 10, 2}, {"0000000011111", 22, 1},
    {"0000000011110", 23, 1}, {"0000000011101", 24, 1}, {"0000000011100", 25, 1}, {"0000000011011", 26, 1},
    {"00000000011111", 0, 16}, {"00000000011110", 0, 17}, {"00000000011101", 0, 18}, {"00000000011100", 0, 19},
    {"00000000011011", 0, 20}, {"00000000011010", 0, 21}, {"00000000011001", 0, 22}, {"00000000011000", 0, 23},
    {"00000000010111", 0, 24}, {"00000000010110", 0, 25}, {"00000000010101", 0, 26}, {"00000000010100", 0, 27},
    {"00000000010011", 0, 28}, {"00000000010010", 0, 29}, {"00000000010001", 0, 30}, {"00000000010000", 0, 31},
    {"000000000011000", 0, 32}, {"000000000010111", 0, 33}, {"000000000010110", 0, 34}, {"000000000010101", 0, 35},
    {"000000000010100", 0, 36}, {"000000000010011", 0, 37}, {"000000000010010", 0, 38}, {"000000000010001", 0, 39},
    {"000000000010000", 0, 40}, {"000000000011111", 1, 8}, {"000000000011110", 1, 9}, {"000000000011101", 1, 10},
    {"000000000011100", 1, 11}, {"000000000011011", 1, 12}, {"000000000011010", 1, 13}, {"000000000011001", 1, 14},
    {"0000000000010011", 1, 15}, {"0000000000010010", 1, 16}, {"0000000000010001", 1, 17}, {"0000000000010000", 1, 18},
    {"0000000000010100", 6, 3}, {"0000000000011010", 11, 2}, {"0000000000011001", 12, 2}, {"0000000000011000", 13, 2},
    {"0000000000010111", 14, 2}, {"0000000000010110", 15, 2}, {"0000000000010101", 16, 2}, {"0000000000011111", 27, 1},
    {"0000000000011110", 28, 1}, {"0000000000011101", 29, 1}, {"0000000000011100", 30, 1}, {"0000000000011011", 31, 1},
};

struct Packed {
    uint16_t code;
    uint8_t len;
    uint8_t run;
    uint8_t level;
};

// Pack one spec table into (code, len, run, level) rows; a plain array, no type
// of its own, so this file declares no primary type besides MpegCoeffVlc.
template <size_t N>
std::array<Packed, N> Pack(const Code (&rows)[N]) {
    std::array<Packed, N> out{};
    for (size_t i = 0; i < N; ++i) {
        uint16_t code = 0;
        uint8_t len = 0;
        for (const char* p = rows[i].bits; *p; ++p) {
            code = static_cast<uint16_t>((code << 1) | (*p == '1'));
            ++len;
        }
        out[i] = Packed{code, len, rows[i].run, rows[i].level};
    }
    return out;
}

const std::array<Packed, sizeof(kTableZero) / sizeof(kTableZero[0])>& TableZero() {
    static const auto t = Pack(kTableZero);
    return t;
}
const std::array<Packed, sizeof(kTableOne) / sizeof(kTableOne[0])>& TableOne() {
    static const auto t = Pack(kTableOne);
    return t;
}

template <size_t N>
bool Match(const BitCursor& c, const std::array<Packed, N>& table, int& len_out, int& run, int& level) {
    for (size_t i = 0; i < N; ++i) {
        const Packed& e = table[i];
        if (!MpegDecode::HasBits(c, e.len + 1)) continue;
        if (MpegDecode::PeekBits(c, e.len) == e.code) {
            len_out = e.len;
            run = e.run;
            level = e.level;
            return true;
        }
    }
    return false;
}

}  // namespace

bool MpegCoeffVlc::Decode(BitCursor& c, bool table_one, bool first_nonintra, bool mpeg1, Symbol& out) {
    out = Symbol{};
    // EOB.
    if (table_one) {
        if (MpegDecode::HasBits(c, 4) && MpegDecode::PeekBits(c, 4) == 0b0110) {
            MpegDecode::SkipBits(c, 4);
            out.eob = true;
            return true;
        }
    } else if (!first_nonintra) {
        if (MpegDecode::HasBits(c, 2) && MpegDecode::PeekBits(c, 2) == 0b10) {
            MpegDecode::SkipBits(c, 2);
            out.eob = true;
            return true;
        }
        // "11s": run 0, level 1.
        if (MpegDecode::HasBits(c, 3) && MpegDecode::PeekBits(c, 2) == 0b11) {
            MpegDecode::SkipBits(c, 2);
            const bool s = MpegDecode::ReadBits(c, 1) != 0;
            out.run = 0;
            out.level = s ? -1 : 1;
            return true;
        }
    } else {
        // First coefficient of a non-intra block, table zero: "1s" is run 0 / level 1.
        if (MpegDecode::HasBits(c, 2) && MpegDecode::PeekBits(c, 1) == 1) {
            MpegDecode::SkipBits(c, 1);
            const bool s = MpegDecode::ReadBits(c, 1) != 0;
            out.run = 0;
            out.level = s ? -1 : 1;
            return true;
        }
    }
    // ESCAPE.
    if (MpegDecode::HasBits(c, 6) && MpegDecode::PeekBits(c, 6) == 0b000001) {
        if (mpeg1) {
            if (!MpegDecode::HasBits(c, 6 + 6 + 8)) return false;
            MpegDecode::SkipBits(c, 6);
            out.run = static_cast<int>(MpegDecode::ReadBits(c, 6));
            int level = static_cast<int>(MpegDecode::ReadBits(c, 8));
            if (level == 0) {
                if (!MpegDecode::HasBits(c, 8)) return false;
                level = static_cast<int>(MpegDecode::ReadBits(c, 8));
            } else if (level == 128) {
                if (!MpegDecode::HasBits(c, 8)) return false;
                level = static_cast<int>(MpegDecode::ReadBits(c, 8)) - 256;
            } else if (level > 128) {
                level -= 256;
            }
            out.level = level;
            return true;
        }
        if (!MpegDecode::HasBits(c, 6 + 6 + 12)) return false;
        MpegDecode::SkipBits(c, 6);
        out.run = static_cast<int>(MpegDecode::ReadBits(c, 6));
        int level = static_cast<int>(MpegDecode::ReadBits(c, 12));
        if (level >= 2048) level -= 4096;
        out.level = level;
        return true;
    }
    int len = 0, run = 0, level = 0;
    const bool found = table_one ? Match(c, TableOne(), len, run, level) : Match(c, TableZero(), len, run, level);
    if (!found) return false;
    MpegDecode::SkipBits(c, len);
    const bool s = MpegDecode::ReadBits(c, 1) != 0;
    out.run = run;
    out.level = s ? -level : level;
    return true;
}

}  // namespace ps2r::ipu
