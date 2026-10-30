#pragma once
// RACORP1 reader (the per-call corpus of recomp-agent; protocol/corpus-format.md).
//
//   "RACORP1\0"  u32 header_len  header (UTF-8 JSON)  zlib( record* )
//   record := u32 fn | u32 count | u32 frame | u8 nargs | u64 args[nargs] | u64 v0 | u64 v1 | u32 sp | u32 ra
//             | u32 caller (0xffffffff = none)
//             | u32 nreads  (u32 addr, u16 len, u8 bytes[len])*
//             | u32 nwrites (u32 addr, u16 len, u8 bytes[len])*
//             | u16 ncallees u32 callees[ncallees]
//
// Addresses are the tracer's normalised form: a scratchpad address is kept as 0x7000xxxx, everything else is
// MainMemory::Phys(addr) (so main RAM is its physical offset and the I/O registers are 0x1xxxxxxx).
#include <cstdint>
#include <string>
#include <vector>

namespace racorp {

struct Run {
    uint32_t addr;
    std::vector<uint8_t> bytes;
};

struct Record {
    uint32_t fn = 0;
    uint32_t count = 1;
    uint32_t frame = 0;
    std::vector<uint64_t> args;   // a0..a3, t0..t3, [sp+0x10..0x1c] when nargs == 12
    uint64_t v0 = 0, v1 = 0;
    uint32_t sp = 0, ra = 0;
    uint32_t caller = 0xFFFFFFFFu;   // 0xFFFFFFFF = none
    std::vector<Run> reads;
    std::vector<Run> writes;
    std::vector<uint32_t> callees;
    size_t index = 0;   // position in the file

    size_t read_bytes() const;
    size_t write_bytes() const;
};

struct Corpus {
    std::string header_json;   // verbatim
    std::vector<Record> records;
};

// Reads the whole file. Throws std::runtime_error with the reason.
Corpus Read(const std::string& path);

}  // namespace racorp
