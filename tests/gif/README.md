# `tests/gif/`

**Phase:** 5  
**Purpose:** ctest suites for `engine/gif` (`ps2r_gif`). NEW-only unit and property tests; the
observable is the recording `RegisterOpStream` (record vector + hash) or the per-method
`CountingGsSink`.

## Belongs here

- `counting_gs_sink.h/.cpp` — `CountingGsSink : GsSink`, call counts per method (test-only)
- `test_gif_regs.cpp` → ctest `GifRegsStat` — register file contract, GifTag parse + classification,
  layout pins, RegisterOpStream encoding + hash chain
- `test_gif_parser_equivalence.cpp` → ctest `GifParserEquivalence` — bulk lanes on vs off produce the
  identical op stream + hash and parser state on the synthetic packets transcribed from OLD
  `tests/test_gif_fast_equivalence.cpp` (cases 1–10c); pinned rules: TEXCLUT / reserved / A+D / NOP /
  undefined dropped, REGLIST XYZ2/XYZF2 ADC ignored, PRE=1 PRIM + 11-bit mask, PACKED decoders,
  NLOOP=0 EOP=0 (generic and all-known), FLG=3, PATH3 stale reset, PATH2 continuation, M3R hold /
  release, FinishPath3 mid-IMAGE vs abandon, hash determinism. Under `-DPS2R_SIMD=ON` the strip3 lane
  is its NEON twin and the same case lines (ops + hash) must print.
- `test_gif_arbiter_order.cpp` → ctest `GifArbiterOrder` — PATH2, PATH3, PATH1 submissions drain in
  submission order; `FinishPath3` once per pass that delivered PATH3, never mid-IMAGE; sub-tag payloads
  dropped; pending-bytes ledger; `clear()`; pool recycle bounds + `kPoolMax`
- `test_gif_drain_thread.cpp` → ctest `GifDrainThread` — the drain thread owns the drain while it runs:
  a producer thread's plain submits and worker-style markers (through a fake `GifMarkerOwner`, so fills
  take the real worker-side submit + ledger) deliver in order, all on the drain thread; a boundary
  slot once in stream position; `DrainFence` drained-to-mark; `DrainIdleGeneration`; foreign
  `drain()` = kick; `Stop` with an unsealed marker joins without delivering behind it; restart.
  Order / count / thread-identity checks only (no wall-clock assertion).
- `test_gif_defer_boundary_order.cpp` → ctest `GifDeferBoundaryOrder` — the directed interleaving
  matrix of OLD `tests/test_defer_boundary_order.cpp` (a–g) + `clear()` detach/ledger + inline vs
  deferred identical `RegisterOpStream` (minus the per-pass `Finish`) and hook order + the 10k
  randomized soak with a real worker thread.
- `check_gsops_seed.py` + `fixtures/gsops_seed.txt` → ctest `Vu1GsopsSeed` — the corpus half of the
  parser equivalence: `tools/vu1_replay --gsops <case>` over the 126 frozen `vutrace_cases2` files
  (interpreter leg → `GifArbiter` → `GifParser` → `RegisterOpStream`) must reproduce the recorded
  `<case> <op count> <hash>` lines (SKIP 77 without the corpus). Generated once at stage 5C; a
  difference is a parser / tier divergence to root-cause, never a regeneration.

## Does not belong here

- harness legs (no lane is claimed in Phase 5); OLD binaries (the GIF has no OLD tool oracle);
  the TSAN twin of `GifDrainThread` (OLD `GifDrainThreadTsan` instrumented the header-inline arbiter
  inside the test TU; here the arbiter body lives in `ps2r_gif` — a whole-tree `-fsanitize=thread`
  configure is the honest twin and is not a ctest row)

## OLD sources (read-only)

- `tests/test_gif_fast_equivalence.cpp` (packet builders + cases)
- `tests/test_gif_drain_thread.cpp` (cases a–d), `tests/test_defer_boundary_order.cpp` (cases a–g + soak)
