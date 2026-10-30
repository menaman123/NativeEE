#pragma once
// The replay host's process state, shared between the driver (replay_host.cpp) and the engine seams the
// generated code calls (seams.cpp: the probe bracket, the access tracer, the yield point, the syscall stub).
//
// Address spaces, in the tracer's NORMALISED form (racorp.h): main RAM by physical offset (< 32 MB), the
// scratchpad as 0x7000xxxx, the I/O registers as 0x1xxxxxxx. Every byte of RAM and scratchpad carries a state
// byte: bit 0 = seeded from the record's read set (or the stack arguments), bit 1 = written during this replay
// (by the candidate or a callee), bit 3 = poisoned: a byte the record says was WRITTEN but not read is pre-filled
// with the complement of its expected final value, so a missing store is always visible (a zero image would hide a
// missing store of zero). A read by the candidate of a byte that is neither seeded nor written is an UNCOVERED
// read: the recording did not capture that byte, so the value the candidate saw is not the game's.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_handler.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/scratchpad.h"

namespace replay {

constexpr uint8_t kSeeded = 1;
constexpr uint8_t kWritten = 2;
constexpr uint8_t kReadByCandidate = 4;   // counted once per byte for the coverage figure
constexpr uint8_t kPoison = 8;            // a recorded-write byte pre-filled with the complement of its expected value

struct Abort {   // thrown out of a body to end the record (runaway loop, exitThread, unknown jump)
    std::string reason;
};

inline uint32_t Normalise(uint32_t addr) {
    return (addr & 0xF0000000u) == 0x70000000u ? addr : MainMemory::Phys(addr);
}
inline bool IsSpad(uint32_t norm) { return (norm & 0xF0000000u) == 0x70000000u; }
inline bool IsRam(uint32_t norm) { return norm < MainMemory::kSize; }

// The I/O register side table: reads are served from the record's read set (seeded), writes land here and
// are compared like RAM writes. Anything else answers 0 (and counts as uncovered).
class ReplayMmio : public MmioHandler {
public:
    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;
    std::map<uint32_t, uint8_t> bytes;   // normalised address -> value
    std::map<uint32_t, uint8_t> state;   // normalised address -> kSeeded / kWritten
};

struct State {
    // the scratch image
    MainMemory ram;
    Scratchpad spad;
    AddressMap map{ram, spad};
    ReplayMmio mmio_handler;
    MmioRouter mmio{mmio_handler};
    FastMem mem{ram, map, mmio};
    std::vector<uint8_t> ram_state;    // 32 MB
    std::vector<uint8_t> spad_state;   // 16 KB
    std::vector<std::pair<uint32_t, uint32_t>> undo;   // (normalised addr, len) touched since the last reset

    // per-record bookkeeping
    uint32_t target_fn = 0;
    int depth = 0;                        // probe depth: 1 = inside the candidate, 2+ = inside a callee
    std::vector<uint32_t> callee_seq;     // the entries called directly by the candidate, in order
    std::vector<uint32_t> syscalls;       // syscall numbers reached at any depth
    std::vector<uint32_t> callees_unmatched;   // callees run as their original body because the corpus has no record of them
    uint64_t own_read_bytes = 0;          // bytes read at depth 1
    uint64_t own_read_seeded = 0;         // ... of which seeded (covered by the recording)
    uint64_t uncovered_reads = 0;
    std::vector<uint32_t> uncovered_first;   // first few uncovered addresses
    std::vector<std::pair<uint32_t, uint32_t>> own_writes;   // (normalised addr, len) at depth 1
    uint64_t trips = 0;                   // back-edge yield trips in this record
    uint64_t max_trips = 200000;          // abort threshold (runaway loop)
    uint32_t vu0_calls = 0;               // VCALLMS sites reached (unsupported here)
    std::string abort_reason;

    void Reset();   // undo every touched byte, clear the per-record bookkeeping

    // state helpers over the three spaces
    uint8_t* StatePtr(uint32_t norm);           // RAM / scratchpad; nullptr for other spaces
    void MarkRange(uint32_t norm, uint32_t len, uint8_t bits);
    // Write bytes into the image at a normalised address (seeding, or applying a recorded callee's writes).
    void Poke(uint32_t norm, const uint8_t* bytes, uint32_t len, uint8_t state_bits);
    // Read the image at a normalised address (for comparison). False when the space is unknown.
    bool Peek(uint32_t norm, uint8_t* out, uint32_t len);
    bool Known(uint32_t norm);   // state != 0

    // accessor seams (depth-aware)
    void OnRead(uint32_t addr, uint32_t n);
    void OnWrite(uint32_t addr, uint32_t n);
};

State& S();

}  // namespace replay
