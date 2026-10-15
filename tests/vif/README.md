# `tests/vif/`

**Phase:** 5  
**Purpose:** ctest suites for `engine/vif` (`ps2r_vif`). NEW-only unit tests in part 1; the ported
`vif_replay --unpack-bulk [--vif-fast]` equivalence arms arrive with `tools/vif_replay` in part 2.

| File | ctest | Covers |
|---|---|---|
| `test_vif_unpack_scalar.cpp` | `VifUnpackScalar` | `GetUnpackSize` (num 0 → 256, CL<WL count, byte rounding, undefined formats), the MASK table, the scalar UNPACK loop on hand-computed vectors (every format × USN, S / V2 / V3 / V4-5 rules incl. the V2-32 / V3-32 phase rules and the lookahead word, MODE 1/2/3 with write-cycle wrap at WL, every mask action, CL>WL skip, CL<WL fill, 1024-QW wrap, TOPS on VIF1 only, VU0 masked read, partial payloads), and the bulk lanes byte-identical to the scalar loop + census on 27 shapes (+ short-payload decline) |
| `test_vif_commands.cpp` | `VifCommands` | register effects of every non-payload command, MSCAL / MSCALF / MSCNT (TOP/ITOPS latch, DBF/TOPS swap, `TopsObserver`, a real micro through `VuUnit` reading XTOP/XITOP, `BindVu1`), VIF0 kick rules, MPG (+ wrap, VU0, num 0), DIRECT / DIRECTHL into `GifArbiter → GifParser → RegisterOpStream` (in-span and streaming, i-bit / MII), MSKPATH3 → `GifRegs`, the MMIO file (widths, read-only registers, VIF0 views, STAT w1c, FBRST, the FIFO window), the TTE pad skip, `ResetLatch` / `IsIdle`, and span-parser vs word-latch identity on a mixed stream at every quadword split |
| (`tools/vif_replay`) | `VifUnpackBulkEquivalence`, `VifUnpackBulkEquivalenceFast` | the ported OLD arms: `vif_replay --unpack-bulk [--vif-fast]` — 30 synthetic streams, bulk lanes off vs on → byte-identical VU1 data memory + lane census, on the word latch and on the whole-span parser |
| (`tools/vif_replay/vif_arms_vs_old.py`) | `VifArmsVsOld` | the two arms above on the NEW tool vs the frozen OLD tool (re-run bounded, cwd pinned under the build tree): every line + exit code identical; SKIP 77 when the OLD tool is absent |

Belongs: NEW-side proofs of the VIF contract. Does not belong: OLD-oracle arms (those are
`tools/vif_replay` + its driver). Provenance: the coverage list of `/tmp/p5/5E/FACTS.md` §5;
OLD had no `test_vif_*.cpp` — only `tests/vif_replay.cpp`.
