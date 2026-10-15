#pragma once


#include "cpu_state.h"
#include <vector>
#include <iostream>
#include <atomic>
#include <cstring>
#include <cstdlib>


// Declare a global variable for the PS2's main memory.
extern std::vector<uint8_t> main_memory;
// [META-ARENA] handle-allocator descriptor arena, EE [0x02000000, +2MB).
// Exported so the [SNAP] snapshot system can dump it (pool structs live here).
extern std::vector<uint8_t> meta_arena_memory;
extern std::vector<uint8_t> vu0_code_memory;
extern std::vector<uint8_t> vu0_data_memory;
extern std::vector<uint8_t> vu1_code_memory;
extern std::vector<uint8_t> vu1_data_memory;
// [S102-PERF] External-writer generation for VU1 micro memory: bumped by
// every vu1_code_memory writer that does NOT route through
// VU1::WriteMicroMem (which bumps micro_generation itself) — the EE-window
// raw-pointer stores in memory::translate_address and the savestate VU1C
// restore. Generation-keyed image-hash memos (vu1_hle_runtime TryExecute)
// key on (micro_generation, THIS) so no writer class can leave a stale
// hash live. Owned by memory.cpp so every test binary that links the
// translate path also links the definition. Relaxed ops throughout: a bump
// racing a reader is equally visible to the old hash-every-entry path.
extern std::atomic<uint64_t> g_vu1_micro_extgen;
extern std::vector<uint8_t> gs_vram;


// Define a 128-bit data type for quadword operations.
struct alignas(16) QuadWord {
   union {
       uint8_t  u8[16];
       uint16_t u16[8];
       uint32_t u32[4];
       uint64_t u64[2];
       int8_t   s8[16];
       int16_t  s16[8];
       int32_t  s32[4];
       int64_t  s64[2];
       float    f32[4];
       uint8_t  m2[16];  // Keep for backward compatibility
   };
};


namespace memory {
   uint8_t* translate_address(uint32_t address, size_t size);
   void initialize();

   // =========================================================================
   // [R130-FASTMEM] Inline plain-main-RAM fast path.
   //
   // Profile evidence (iter3): memory::write<u32>/u64/u8 + translate_address
   // are the LARGEST host self-time block (~half of non-wait samples), paid by
   // every recompiled store. The slow bodies (renamed *_slow below, verbatim
   // pre-R130 code in memory.cpp / the test shims) run a ~3300-line diagnostic
   // probe gauntlet plus translate_address per access. For accesses that land
   // in plain main RAM we short-circuit to a direct memcpy on main_memory.
   //
   // GUARD DERIVATION (must mirror the slow path EXACTLY):
   //   The slow path's main-RAM branch is reached only after
   //     - is_io_register(address): raw [0x10000000,0x1000FFFF] and
   //       [0x12000000,0x12001FFF] (memory.cpp is_io_register) -- both have
   //       (addr & 0x1FFFFFFF) >= 0x10000000,
   //     - scratchpad (address & 0x70000000) == 0x70000000
   //       (memory.cpp translate_address "1. Handle Scratchpad") -- bit 28 set
   //       => (addr & 0x1FFFFFFF) >= 0x10000000,
   //     - HW window raw [0x10000000,0x20000000) incl. the [R121-MTVU] VU1-mem
   //       Sync hooks (memory.cpp "2. Handle Hardware Registers") -- phys ==
   //       addr >= 0x10000000,
   //     - meta-arena phys in [0x02000000,0x02200000) and BIOS mirrors phys >=
   //       0x1FC00000 (memory.cpp "3b."/"4.")
   //   all fail, and then computes
   //     physical_addr = address & 0x1FFFFFFF   ("3. Handle Main RAM and
   //     Mirrors (KUSEG, KSEG0, KSEG1)", memory.cpp)
   //   and requires physical_addr + size <= main_memory.size() (else OOB log +
   //   dropped/zero access). Every one of the excluded regions has
   //   (addr & 0x1FFFFFFF) >= 0x02000000 == main_memory.size(), so the single
   //   condition
   //     (address & 0x1FFFFFFF) + sizeof(T) <= main_memory.size()
   //   selects EXACTLY the slow path's in-bounds main-RAM accesses -- including
   //   KSEG0/KSEG1 (0x80xxxxxx/0xA0xxxxxx) and 0x2xxxxxxx mirrors, and
   //   excluding end-of-RAM straddles (those fall through to the slow path's
   //   OOB handling, byte-identical).
   //
   //   ALIGNMENT: the slow path performs a plain std::memcpy on the host
   //   pointer for every scalar width (no alignment enforcement), so the fast
   //   path's memcpy matches unaligned accesses exactly. read_quad/write_quad
   //   apply the R5900 `address &= ~0xF` mask BEFORE translating in the slow
   //   path (memory.cpp read_quad/write_quad), so the fast path applies the
   //   SAME mask before its range check.
   //
   // WHAT STAYS OUTSIDE THE FAST PATH (and why):
   //   - All MMIO (0x1000xxxx hw regs incl. T0/GIF/DMAC/INTC/SIF, VIF regs with
   //     their R121/R123 MTVU Sync drains), VU0/VU1 memory windows (0x11xxxxxx,
   //     incl. the R122 MTVU EE-access Sync hooks in translate_address), GS
   //     privileged (0x12xxxxxx), IOP RAM, BIOS, scratchpad, meta-arena, and
   //     unmapped addresses: the guard cannot select them (phys >= 32MB).
   //   - The [KEY-REDIRECT] scavenger (the ONE state-mutating probe in the
   //     write gauntlet, cadence-based every-256-writes): preserved via the
   //     every-256th-fast-write fallthrough to write_slow<T> below.
   //   - The read-only logging probes/watch blocks: they execute for every
   //     access that takes the slow path; accesses taken by the fast path skip
   //     them. PS2_FASTMEM=0 restores full probe coverage verbatim (that is
   //     the documented diagnostic mode). Raising g_fastmem_force_slow also
   //     routes EVERY access down the verbatim legacy path at runtime.
   //
   // Gate: PS2_FASTMEM (default ON; "0" => legacy verbatim). Latched once
   // into g_fastmem_mode; per-access cost is one relaxed load + compare.
   // =========================================================================

   // Slow paths: the verbatim pre-R130 read<T>/write<T>/read_quad/write_quad
   // bodies, renamed. Defined out-of-line in host_app/memory.cpp (and, for the
   // isolated test binaries, in tests/recompiler_verify/sut_link_shims.cpp,
   // tests/recomp_semantics/mem_shim.cpp, tests/vif_replay.cpp).
   template <typename T>
   T read_slow(uint32_t address);

   template <typename T>
   void write_slow(uint32_t address, T value);

   void read_quad_slow(uint32_t address, QuadWord& value);
   void write_quad_slow(uint32_t address, const QuadWord& value);

   // [R130-FASTMEM] master gate. -1 = unlatched, 0 = off (legacy verbatim),
   // 1 = on. C++17 inline variables: one instance program-wide, no extra
   // definitions needed in the shim-backed test binaries.
   inline std::atomic<int> g_fastmem_mode{-1};

   // [R130-FASTMEM] watchpoint-active counter. Any nonzero value forces every
   // access down the verbatim legacy (slow) path. There is no dynamic
   // watchpoint add/remove API in the runtime today (the probe blocks are
   // compile-time and the g_cam_rot_watch/g_ring_watch_addr/g_bootobj_watch
   // diagnostics are capped log emitters covered by PS2_FASTMEM=0); this flag
   // is wired so tests and any future hard watchpoint can raise/lower it.
   inline std::atomic<uint32_t> g_fastmem_force_slow{0};

   inline bool fastmem_on() {
       int m = g_fastmem_mode.load(std::memory_order_relaxed);
       if (m < 0) {
           const char* e = std::getenv("PS2_FASTMEM");
           m = (e && e[0] == '0') ? 0 : 1;   // default ON; PS2_FASTMEM=0 => legacy
           g_fastmem_mode.store(m, std::memory_order_relaxed);
       }
       return m != 0;
   }

   // [W10-MEMWATCH] Diagnostic write-watch gauntlet gate. -1 = unlatched,
   // 0 = off (default: every pure-logging watch/trace probe block in the
   // slow paths -- write_slow<T>, write_quad_slow, read_slow's [WATCH]
   // family, and write_io_register's [IO-TRACE] -- is skipped), 1 = on
   // (PS2_MEMWATCH=1: every probe restored VERBATIM, including the
   // backtrace()+backtrace_symbols() symbolization sites, for debugging).
   // The [KEY-REDIRECT] mutator in write_slow<T> is NOT behind this gate:
   // it repairs a game-progression-critical pointer and stays
   // unconditionally live regardless of this latch.
   // C++17 inline variable: one instance program-wide, no extra
   // definitions needed in the shim-backed test binaries.
   inline std::atomic<int> g_memwatch_mode{-1};

   inline bool memwatch_on() {
       int m = g_memwatch_mode.load(std::memory_order_relaxed);
       if (m < 0) {
           const char* e = std::getenv("PS2_MEMWATCH");
           m = (e && e[0] == '1') ? 1 : 0;   // default OFF; PS2_MEMWATCH=1 => verbatim probes
           g_memwatch_mode.store(m, std::memory_order_relaxed);
       }
       return m != 0;
   }

   // Test/diagnostic controls.
   inline void fastmem_set_for_test(bool on) {
       g_fastmem_mode.store(on ? 1 : 0, std::memory_order_relaxed);
   }
   inline void memwatch_set_for_test(bool on) {
       g_memwatch_mode.store(on ? 1 : 0, std::memory_order_relaxed);
   }
   inline void fastmem_watch_raise() {
       g_fastmem_force_slow.fetch_add(1, std::memory_order_relaxed);
   }
   inline void fastmem_watch_lower() {
       g_fastmem_force_slow.fetch_sub(1, std::memory_order_relaxed);
   }

   // Templated read and write functions (fast-path wrappers).
   template <typename T>
   inline T read(uint32_t address) {
       // Same mask as the slow path's main-RAM branch (memory.cpp
       // translate_address: physical_addr = address & 0x1FFFFFFF).
       const uint32_t phys = address & 0x1FFFFFFFu;
       if (static_cast<uint64_t>(phys) + sizeof(T) <= main_memory.size() &&
           fastmem_on() &&
           g_fastmem_force_slow.load(std::memory_order_relaxed) == 0u) {
           T value;
           std::memcpy(&value, main_memory.data() + phys, sizeof(T));
           return value;
       }
       return read_slow<T>(address);
   }

   template <typename T>
   inline void write(uint32_t address, T value) {
       const uint32_t phys = address & 0x1FFFFFFFu;
       if (static_cast<uint64_t>(phys) + sizeof(T) <= main_memory.size() &&
           fastmem_on() &&
           g_fastmem_force_slow.load(std::memory_order_relaxed) == 0u) {
           // Every 256th eligible write falls through to the verbatim slow
           // path so cadence-based scavengers there keep ticking (the
           // [KEY-REDIRECT] pointer-restore at memory.cpp write<T> is the one
           // probe that MUTATES state; it samples every 256 slow-path writes,
           // so it keeps firing at ~1/65536 of total write volume plus every
           // IO/non-RAM write -- multiple checks per frame at observed rates).
           // The tick is per-thread and per-T; the combined slow-entry rate
           // across widths is therefore >= 1/256 of all eligible writes.
           static thread_local uint32_t s_fm_tick = 0;
           if (((++s_fm_tick) & 0xFFu) != 0u) {
               std::memcpy(main_memory.data() + phys, &value, sizeof(T));
               return;
           }
       }
       write_slow<T>(address, value);
   }

   // Functions for reading and writing 128-bit quadwords.
   inline void read_quad(uint32_t address, QuadWord& value) {
       // R5900 lq/lqc2 ignore the low 4 bits; the slow path applies this SAME
       // mask before translating (memory.cpp read_quad_slow: address &= ~0xF),
       // so the fast range check must see the aligned address. The slow path
       // is still handed the ORIGINAL address (it re-masks itself, and its
       // capped [LQ-ALIGN] log is a diagnostic we preserve there).
       const uint32_t phys = (address & ~0xFu) & 0x1FFFFFFFu;
       if (static_cast<uint64_t>(phys) + sizeof(QuadWord) <= main_memory.size() &&
           fastmem_on() &&
           g_fastmem_force_slow.load(std::memory_order_relaxed) == 0u) {
           std::memcpy(&value, main_memory.data() + phys, sizeof(QuadWord));
           return;
       }
       read_quad_slow(address, value);
   }

   inline void write_quad(uint32_t address, const QuadWord& value) {
       // No fallthrough tick here: the quad slow path's probe blocks are all
       // read-only logging (no [KEY-REDIRECT]-class mutator exists in
       // write_quad_slow -- audited in /tmp/loop_iter3_fixer.md section 1).
       const uint32_t phys = (address & ~0xFu) & 0x1FFFFFFFu;
       if (static_cast<uint64_t>(phys) + sizeof(QuadWord) <= main_memory.size() &&
           fastmem_on() &&
           g_fastmem_force_slow.load(std::memory_order_relaxed) == 0u) {
           std::memcpy(main_memory.data() + phys, &value, sizeof(QuadWord));
           return;
       }
       write_quad_slow(address, value);
   }


   // Function to get a direct pointer to memory
   void* get_pointer(uint32_t address);

   // [KEY-OVERLAP] Diagnostic: catch any non-write<T> HLE bulk write (memcpy/
   // fread/memset directly into main_memory) whose destination span overlaps the
   // case-6 level-key char buffer (default 0xbf2c00, auto-refined at runtime by
   // the KEYASSIGN hook in main.cpp). The key buffer's "Levels\Earth\Hub\Beach"
   // bytes get clobbered between frame ~1121 and ~1708 with zero write<T> hits,
   // so the corruptor is one of these host-side bulk writers. Call this right
   // before each EE write site; when it fires it names the exact writer + dest.
   // Read-only. ra=caller MIPS return address where available, else 0.
   extern std::atomic<uint32_t> g_key_buf_addr;
   void key_overlap_check(uint32_t ee_addr, uint32_t len, const char* site, uint32_t ra);
}


// [FUNC-TRACE] Watched function addresses for diagnostic tracing
#include <unordered_set>
extern std::unordered_set<uint32_t> g_watched_functions;


// [IO-TRACE] Lightweight PC tracking for I/O write attribution
extern uint32_t g_current_pc;

