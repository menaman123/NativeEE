# PHASE5_IMPL — stage records; plan = PHASE5_PLAN.md; module gate only

Every stage of Phase 5 appends one section in the PHASE5_PLAN §8.2 / CONTRACT §6 shape (Landed /
Flags / Tests / Gate / Findings / HANDOFF). Outputs are quoted from bounded runs in THIS tree
(`verify/run_bounded.sh`, logs under `/tmp/p5/<stage>/`). No harness lane is claimed in this phase.

Pre-stage doc fixes (REVIEW.md items 1–5, applied to `docs/phases/PHASE5_PLAN.md` before any code):
§7.1 `P5-NEON-DEFAULT` rewritten to the real `option(PS2R_SIMD … OFF)` / `macos-dev` OFF / `macos-diag`
ON facts + the bounded `-DPS2R_SIMD=ON` run named (and one sentence added to §6 G7); §2.1 `gif_tag`
`gif.h:89-112`, `gif_regs` `gif.h:120-202`, `gif_arbiter` `+ :611`, `gif_parser` anchors
`:3183-3200, :3211-3264, :3266-3350` + `:552, :647-658`; §7 5A row + HANDOFF `gif.h:15-118, :120-202`;
§2.7 `dmac.h:16-56`; §2.2 `vu.cpp:718`; §4.1 "at 5A" → "at 5C"; §6 G4 "66 lines after the
`[VU-MODE]` skip … (`old_catalog.log` is 67 with the announce line)".

---

## 5A — engine/gif core + arbiter synchronous half (2026-09-27; part 1 ≈20 min, part 2 ≈22 min)

### 5A part 1 — gif_tag / gs_sink / register_op_stream / gif_regs + GifRegsStat

Pre-flight (tree at `78b5655`, before any code): `verify/run_bounded.sh 900 /tmp/p5/5A/build0.log --
cmake --preset macos-dev` → exit 0, `-- nativeee: build type Debug, PS2R_DIAGNOSTICS=OFF,
PS2R_SIMD=OFF, title <title>`; `verify/run_bounded.sh 1800 /tmp/p5/5A/build1.log --
cmake --build --preset macos-dev -j4` → exit 0, `grep -ci 'error\|warning'` = 0 on both logs.

### Landed (files)

| File | Class / types | OLD source transplanted |
|---|---|---|
| `engine/gif/gif_tag.h/.cpp` | `GifTag` (`Parse(lo,hi)`; 24-byte layout pinned by `static_assert`), `enum class GifPath {Path1=1,Path2=2,Path3=3}`, `enum class GifFormat`, `enum class GsReg : uint8_t` (every value of the OLD table incl. `Reserved0B`, `Texclut`), `GifTagClass` POD + `ClassifyGifTag()` (has_ad / all_known / strip3 / rowable), `GifTransferState` POD (tag, loop, reg, in_transfer) | `gif.h:15-19` (GIFPath), `:21-26` (GIFFormat), `:29-87` (GSInternalReg), `:89-112` (GIFTag + Parse); `gif.cpp:3646-3673` (eligibility predicates, FACTS §3.2) |
| `engine/gif/gs_sink.h` | `GsSink` — abstract, virtual dtor + exactly the 10 pure virtuals of PHASE5_PLAN §3.1 (`BeginTag / WriteRegister / PackedRgbaq / PackedSt / PackedUv / PackedFog / KickVertex / ImageData / EndOfPacket / FinishPath3`); no change needed (FACTS §8 confirms all 14 OLD mutation sites map onto the 10) | the `g_gs_state` surface of `gif.cpp:3641, :3926-3928, :3933, :4317, :3211-3264, :4541-5224, :4419-4422` |
| `engine/gif/register_op_stream.h/.cpp` | `RegisterOpStream : GsSink`; `RegisterOp` 24-byte POD (`static_assert(sizeof==24)` + trivially copyable), `RegisterOpKind`; every call recorded with FACTS §8.1 encoding (path carried from `BeginTag`, pad 0, floats as raw bits); `Hash()` = `ps2r::hash::Fnv1a64(&op, 24, seed)` chained from `kFnv1a64Offset`, O(1) (running); `Count()`, `Clear()`, `Ops()` | new (plan §3.2 / FACTS §8.1) |
| `engine/gif/gif_regs.h/.cpp` | `GifRegs : MmioHandler` (NOT registered): `Reset()`, `Read/Write` for CTRL/MODE/STAT/TAG0-3/CNT/P3CNT/P3TAG with the exact STAT composition (M3R bit 0, M3P bit 1, IMT bit 2, PSE bit 3, OPH bit 9, APATH=3<<10, FQC bits 24-28 = 0), TAG1 with the NREG-16 shift-out, CNT `(nloop-loop) | reg<<16`; CTRL = RST-then-PSE, only bit 3 stored; MODE = `& 7`; read-only regs / unmapped offsets drop writes and read 0; `std::atomic<bool> path3_masked_` (`SetPath3MaskedByVif` for the VIF, 5E); owns `GifTransferState` (`Transfer()`) the parser advances (FACTS §12.1 option A) | `gif.h:120-202` (members); `gif.cpp:185-206` (Reset), `:215-300` (Read), `:302-341` (Write) |
| `engine/gif/CMakeLists.txt` | `ps2r_gif` STATIC (`gif_tag.cpp gif_regs.cpp register_op_stream.cpp`), `-Wall -Wextra -Werror`, `PUBLIC ps2r_core ps2r_ee ps2r_gs_format`, `PS2R_SIMD` define pass-through | pattern `engine/gs/CMakeLists.txt` |
| `engine/gif/README.md` | Files table (file / class / OLD source), register-file notes (the Q3/Q4/Q5 quirks), `PacketTemplate` DROPPED + `ProcessPacket` not-written rows, the intended `MmioRouter` range | — |
| `tests/gif/CMakeLists.txt` | `ps2r_gif_test(name ctest_name)` (links `ps2r_gif` + `counting_gs_sink.cpp`, `-Wall -Wextra -Werror -O2`, TIMEOUT 120) | pattern `tests/gs/CMakeLists.txt` |
| `tests/gif/counting_gs_sink.h/.cpp` | `CountingGsSink : GsSink` — per-method counters, `kick_draw`, `last_reg/last_value`, `Total()`, `Reset()` | new (test-only) |
| `tests/gif/test_gif_regs.cpp` | ctest `GifRegsStat` | recipe FACTS §4.5 |
| `tests/gif/README.md` | new-directory README | — |
| root `CMakeLists.txt` | `+ add_subdirectory(engine/gif)      # Phase 5: GIFtag parse + GsSink + PATH arbiter (ps2r_gif)` after `engine/iop` | append-only |
| `tests/CMakeLists.txt` | `+ add_subdirectory(gif)     # Phase 5: GIF suites (…)` at the end | append-only |

`git diff --stat` (tracked files): `CMakeLists.txt | 1 +`, `engine/gif/README.md | 44 ++++-----`, `tests/CMakeLists.txt | 1 +`.

### Flags applied

None consumed by part 1 (the register file and the sink carry no flag sites). `Ps2R168NoteGifStatRead()`
census hook at STAT read (`gif.cpp:222`) → DIAG → not written. `PS2_GIF_FAST` / `PS2_GSK_SIMD` / `PS2_EEFEED`
(B2) are parser rows → part 2. `PS2_MID_TEMPLATE` / `PS2_MID_TEMPLATE_FORCEMISS` → `PacketTemplate` DROPPED
(README row). `PS2_GIF_DRAIN_THREAD` → class deferred to 5G, rule noted in README.

### Tests added (ctest names)

`GifRegsStat` — fresh-state zeros (STAT/CTRL/MODE/TAG0-3/CNT/P3CNT/P3TAG/unmapped), `MODE 0xFF → 7`, STAT
`0x5`, PSE latch (`0xD`), junk CTRL bits not stored, `CTRL 9` = RST then PSE (MODE 0, STAT 0x8, VIF mask
cleared), RST alone clears PSE, M3P only via STAT bit 1, read-only writes dropped, access width ignored;
transfer readback (STAT `0xE00 | masks`, TAG0 `nloop|eop<<15`, TAG1 `pre<<14|prim<<15|flg<<26|nregs<<28`,
TAG2 `0xCA0EF512`, TAG3 `0xB987643D`, CNT, FLG=IMAGE bits 26-27, NREG 16 → TAG1 bits 28-31 == 0, FQC == 0,
completed tag keeps TAG0 but drops OPH/APATH, RST zeroes the tag); `GifTag::Parse` (reserved bits ignored,
NREG 0→16, 11-bit PRIM, 15-bit NLOOP, FLG 3); `ClassifyGifTag` (strip3 XYZ2/XYZF2, rowable {ST,RGBAQ,XYZ3},
A+D, NREG 16 with NOP, TEX0 not all_known, descriptors beyond NREG ignored); layout pins
`sizeof(GifTag)==24`, `sizeof(RegisterOp)==24`; `RegisterOpStream` encoding per FACTS §8.1 for all 10 kinds,
path carried, pad 0, hash == manual FNV-1a chain, reproducible, draw-bit sensitive, `Clear()`; `CountingGsSink`
self-check.

```
89/89 Test #89: GifRegsStat .........................   Passed    0.01 sec
100% tests passed, 0 tests failed out of 89
```

### Oracle / gate status

No OLD oracle for the register file (OLD has no GIF tool binary); the equivalence oracle is part 2.

```
$ verify/run_bounded.sh 900 /tmp/p5/5A/build2.log -- cmake --preset macos-dev          -> exit 0
$ verify/run_bounded.sh 1800 /tmp/p5/5A/build4.log -- cmake --build --preset macos-dev -j4   -> exit 0, grep -c 'warning\|error' = 0
$ verify/run_bounded.sh 1200 /tmp/p5/5A/ctest2.log -- ctest --preset macos-dev
100% tests passed, 0 tests failed out of 89
$ python3 tests/lint/lint_one_class.py --root .
LintOneClass: 324 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root .
LintEnginePurity: 281 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py --root .
getenv sites: 1 -> PASS
$ ctest --preset macos-dev -R CoreSingleGetenvSite
1/1 Test #19: CoreSingleGetenvSite .............   Passed    0.89 sec
```

Gate status: GREEN (build 0 warnings, ctest 89/89, both lints PASS, getenv gate 1).

### Findings (→ plan §7.1)

| Id | Where | Finding | Disposition |
|---|---|---|---|
| P5-GIF-TAG1-NREG16 (Q3) | `gif_regs.cpp` TAG1 | OLD composes TAG1 from the POST-Parse nregs; 16<<28 leaves the 32-bit word and reads 0 — accidentally the hardware encoding. After RST the tag reads nregs 0 (not 16). | Carried verbatim; pinned in `GifRegsStat`. |
| P5-GIF-APATH3 (Q5) | `gif_regs.cpp` STAT | APATH reads 3 whenever a tag is in transfer regardless of the real path; P1Q/P2Q/P3Q never set; FQC always 0 (nothing fills the FIFO). | Carried verbatim; pinned. |
| P5-GIF-MASK-ALLPATHS (Q4) | `GifRegs::Path3Masked()` | MODE.M3R blocks parsing of every path in OLD, not just PATH3. The register side is written; the parser (part 2) must consult `Path3Masked()` at the zero-copy / ProcessBuffer decision for all paths. | Register side done; parser rule → part 2. |
| P5-GIF-RST-TAIL | `GifRegs::Reset()` | OLD `Reset()` does not clear `dma_buffer`; NEW `GifRegs::Reset()` therefore does not reach the parser's buffered tail (the parser owns it). A CTRL.RST mid-tail leaves the tail for the next ReceiveData, as OLD. | Carried; part 2 must NOT clear the tail on RST. |
| P5-GIF-TRANSFER-OWNER | `gif_regs.h` | Design choice (FACTS §12.1 option A): `GifRegs` owns `GifTransferState`; the parser advances it through `Transfer()`. No back-pointer, no init-order hazard; eligibility bits stay parser-private so `sizeof(GifTag)==24` holds. | Recorded; part 2 codes against it. |
| P5-GIF-PACKED-FOG-FGE | `gs_sink.h` `PackedFog` comment | OLD PACKED FOG stores `f != 0` into the PRIM fog-enable bool (FACTS §6). Documented on the method; Phase 6 `GsState` must reproduce it for identity. | Accepted-difference candidate for Phase 6/8; recorded. |
| P5-GIF-P3-STALE-RESET | (parser, part 2) | `gif.cpp:647-658` is a behaviour rule inside the "probes not written" range: a PATH3 packet arriving with `in_transfer` set resets the transfer and clears the tail. | Must be written in part 2 (FACTS §5.1 R2). |

### HANDOFF (5A part 1 → 5A part 2)

**Done:** `gif_tag`, `gs_sink`, `register_op_stream`, `gif_regs`, `engine/gif/CMakeLists.txt` (`ps2r_gif`),
both append-only CMake lines, `tests/gif/{CMakeLists.txt, counting_gs_sink, test_gif_regs}` (`GifRegsStat`),
`engine/gif/README.md` Files table, `tests/gif/README.md`, the 5 plan doc fixes. Tree GREEN (89/89).

**Not done (moved to part 2):**
1. `engine/gif/gif_parser.h/.cpp` — `GifParser(GsSink&, GifRegs&, lanes knob)`: `ReceiveData(GifPath, data, size)`
   (FACTS §5.1 R1–R5 incl. the PATH3 stale reset R2 and the `Path3Masked()` FIFO-hold for every path),
   `ProcessBuffer`, `ParseSpan` (T1–T12), the strip3 / rowable bulk lanes + Tier A remainder (§5.3), PACKED
   decoders (§6, `StageTripleScalar` + NEON twin under `PS2R_SIMD`), `ProcessPacked` (§6.3), the ONE 64-bit
   dispatcher with the drop table (§7.1: TEXCLUT/0x0B/0x0E/0x0F/unknown dropped, REGLIST kicks ignore ADC,
   PRIM masked to 11 bits on all routes), `ProcessImage` per QW (§7.2), REGLIST odd-pad rule (§7.3),
   `FinishPath3()` (§5.5: sink call iff the mid-IMAGE early return did not fire), counters
   `Strip3Triples()/RowableRows()`, `BufferedBytes()`. Advances `regs.Transfer()` (tag/loop/reg/in_transfer);
   sets Q via `sink.BeginTag`. Quirks Q1/Q2 carried verbatim. Add `gif_parser.cpp` to `ps2r_gif`.
2. `engine/gif/gif_arbiter.h/.cpp` — the SYNCHRONOUS half (FACTS §9): `GifArbiterPacket` aggregate,
   `submit/submitOwned` (`size < 16` dropped, `is_directhl/is_path3_image` computed-unused), pool
   `acquireBuffer/recycleBuffer` (`kPoolMax=4`, 4 KiB..64 KiB capacity keep), `drain()` in submission order →
   `parser.ReceiveData` per packet, `parser.FinishPath3()` iff any PATH3 packet, recycle + `pending_bytes_`
   ledger + `DrainedPackets()`, `hasPending()/clear()`. Ctor-injected `GifParser&`. Add to `ps2r_gif`.
3. `tests/gif/test_gif_parser_equivalence.cpp` → ctest `GifParserEquivalence` (FACTS §10): packet builders
   verbatim, cases 1–10 + TEXCLUT dropped + REGLIST XYZ2 bit-47 still draws + NLOOP=0 EOP=0 count; A/B =
   lanes ON vs lanes OFF (constructor knob) → identical `Ops()` + `Hash()`; optional in-test reference walker.
   Register with `ps2r_gif_test(test_gif_parser_equivalence GifParserEquivalence)`.
4. README rows for the parser quirks Q1/Q2 and the arbiter (submission-order contract).

**Gate status:** GREEN — `100% tests passed, 0 tests failed out of 89`; `LintOneClass … PASS`;
`LintEnginePurity … PASS`; `getenv sites: 1 -> PASS`; build 0 warnings.

**Next command:** `cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`
(expect 89/89), then write `engine/gif/gif_parser.h/.cpp` against `engine/gif/{gs_sink.h, gif_regs.h, gif_tag.h}`
from `/tmp/p5/5A/FACTS.md` §5–§7, add it to `engine/gif/CMakeLists.txt`, then `gif_arbiter`, then the
equivalence test.


### 5A part 2 — gif_parser + gif_arbiter synchronous half + GifParserEquivalence / GifArbiterOrder (2026-09-27, ≈22 min)

No `/tmp/p5/5A/REVIEW.md` existed at the start of part 2 (nothing to apply). Pre-flight: `pwd` =
`<repo>-p5`, branch `phase5`, tree GREEN from part 1 (89/89).

#### Landed (files)

| File | Class / types | OLD source transplanted |
|---|---|---|
| `engine/gif/gif_parser.h/.cpp` (71 + 470 lines; longest function `ParseSpan` ≈40 lines) | `GifParser(GsSink&, GifRegs&, GifParserSettings)`; `GifParserSettings{bool bulk_lanes = true}` (POD, the equivalence-test knob — mirror of OLD `GifFastOverrideForTest`, no env, no latch, not a Config field). `ReceiveData(path, data, size)`: size-0 drop, PATH3 stale reset (transfer + tail cleared, PATH1/2 continue), `active_path_` → `BeginTag`, zero-copy `ParseSpan` with the unconsumed tail assigned, append + `ProcessBuffer` otherwise (also the M3R-held arm for every path). `ProcessBuffer`: returns while `Path3Masked()`. `AcceptTag` + `ParseSpan` (T1–T12): whole-tag wait, `BeginTag`, `ClassifyGifTag`, PRE → `WriteRegister(Prim)`, NLOOP=0 (EOP → `EndOfPacket`, in_transfer cleared only with EOP), payload stall, IMAGE bulk (`ConsumeImage`, `ImageData` per QW), PACKED bulk branch for all-known/no-A+D tags (`ConsumePackedBulk`: strip3 lane `StageTriple` when `reg==0 && left>=3`, rowable lane when `reg==0 && left>=nregs`, per-descriptor remainder), PACKED generic per-QW (`ProcessPacked`: decoded set / A+D `hi&0xFF` + `lo` / other descriptors carry the 64-bit value in `lo`), REGLIST two per QW with the `loop < nloop` guard (odd pad skipped), FLG=3 consumes 16 bytes with no advance, completion → `EndOfPacket` iff EOP. PACKED decoders bit-exact (unsigned 12.4 X/Y, 24-bit Z + fog at hi 36 + ADC 47 for XYZF, 32-bit Z for XYZ, ST raw s/t/q, RGBAQ bytes at 0/32/64/96, UV 14-bit at 0/32, FOG hi 36). `DispatchRegister` = the ONE 64-bit dispatcher (PRIM masked 11 bits, XYZ2/XYZ3/XYZF2/XYZF3 decoded with ADC ignored, `RegisterIsForwarded` table = the OLD "has an effect" set; 0x0B/0x0E/0x0F/0x1C/undefined dropped). `FinishPath3`: mid-IMAGE early return (no sink call, tail kept) else abandon + `sink.FinishPath3()` + tail clear. Counters `Strip3Triples/RowableRows/BulkQuads`, `BufferedBytes`, `Transfer()`. `StageTriple` NEON twin under `#if defined(PS2R_SIMD) && defined(__aarch64__) && defined(__ARM_NEON)` (same three sink calls in the same order). | `gif.cpp:552` (size 0), `:588` (active path), `:647-658` (PATH3 stale reset), `:733-745` (zero-copy / append), `:3183-3200` (ProcessBuffer), `:3211-3264` (decoders), `:3309-3359` (SIMD triple), `:3628-3644, :3926-3946, :4057-4110, :4147-4150, :4178-4213, :4216-4321` (ParseSpan rule lines), `:4374-4425` (FinishDMA), `:4541-4699` (ProcessPacked), `:4700-5156` (ProcessReglist), `:5157-5182` (ProcessImage) |
| `engine/gif/gif_arbiter.h/.cpp` (68 + 94 lines) | `GifArbiterPacket` aggregate `{path, is_directhl, is_path3_image, data, boundary_seq, bnd_enq_ns}`; `GifArbPayloadBytes` free inline; `GifArbiter(GifParser&)` (ctor-injected, as FACTS §9.7 / part-1 HANDOFF; `drain()` takes no argument), `kPoolMax = 4`, `kPoolMinCapacity/kPoolMaxCapacity` = 4 KiB / 64 KiB, `submit` (copy; `!data || size < 16` dropped; `is_directhl = Path2 && flag`, `is_path3_image = Path3 && IsImagePacket`), `submitOwned` (move; `size < 16` dropped), `acquireBuffer` (empty vector when the pool is empty), `recycleBuffer` (capacity bounds, clear, `size < kPoolMax`), `drain()` (submission order, `ReceiveData` per non-empty packet, `FinishPath3()` iff a PATH3 packet was delivered, recycle + `pending_bytes_` ledger + `drained_packets_`), `hasPending`, `clear`, `PendingBytes/DrainedPackets/PoolSize`. Markers / inbox / boundary slots / drain thread: one header comment names the 5G contract; no stubs. | `gif_arbiter.h:85-112` (packet), `:118-121` (payload bytes), `:229-247` (pool), `:283-330` (hasPending/clear synchronous forms), `:592, :609-611, :628`; `gif_arbiter.cpp:20-26` (isImagePacket; `pathPriority` not written), `:579-598, :657, :716` (submit), `:723-776` (submitOwned), `:952, :1257, :1761-1768, :1786-1807` (drain rule lines) |
| `engine/gif/CMakeLists.txt` | `+ gif_parser.cpp gif_arbiter.cpp`; `PS2R_SIMD` comment now names `StageTriple` | — |
| `tests/gif/test_gif_parser_equivalence.cpp` (638 lines) | ctest `GifParserEquivalence`; rig `TestGifParser {GifRegs, RegisterOpStream, GifParser}` (name chosen for the one-class file-name rule, precedent `TestBoundary`); `ab_case` feeds lanes-on and lanes-off rigs and requires identical op vectors (memcmp per record, first-divergence index reported), identical `Hash()`, identical `(in_transfer, loop, reg, BufferedBytes)`, and a zero lane census on the off leg | packet builders + cases 1, 1b, 2, 2b, 3, 4, 5, 6, 7, 8, 9/9b, 10c from `tests/test_gif_fast_equivalence.cpp:116-160, :307-536, :623` |
| `tests/gif/test_gif_arbiter_order.cpp` (211 lines) | ctest `GifArbiterOrder`; rig `TestGifArbiter`; `submission_order` (RegisterOpStream), `finish_counting` (CountingGsSink), `pool_recycle` | new |
| `tests/gif/CMakeLists.txt` | `+ ps2r_gif_test(test_gif_parser_equivalence GifParserEquivalence)`, `+ ps2r_gif_test(test_gif_arbiter_order GifArbiterOrder)` | — |
| `engine/gif/README.md` | Files rows for `gif_parser` / `gif_arbiter` completed; new "Parser / arbiter notes" (Q1/Q2, PATH3 stale reset, REGLIST ADC, drop table, PACKED FOG, lane entry rule, FinishPath3, submission order, drain-thread rule) | — |
| `tests/gif/README.md` | rows for the two suites | — |

#### Flags applied

- `PS2_GIF_FAST` (`GifFastOn()`, `gif.cpp:733, :3647, :4059, :4077`) → NORM: the zero-copy `ParseSpan` entry, the bulk IMAGE arm and the bulk PACKED branch are the only code paths; the `fast=0` per-QW legacy arms are not written (the generic per-QW body survives only as the non-eligible-tag path and as the test knob's route).
- `PS2_GSK_SIMD` (`GskSimdOn()`, `gif.cpp:4106`) → NORM winning arm = the NEON `StageTriple` when compiled with `PS2R_SIMD` (root option, OFF under `macos-dev`); no runtime gate, no census atomics (`g_w10_gsksimd_runs/verts` → DIAG, omitted).
- `PS2_EEFEED` B2 (`w10eefeed::On()`, `gif.cpp:4147`) → NORM: the rowable row loop is unconditional.
- `PS2_GIF_DRAIN_THREAD` → class deferred to 5G; rule ("started only under `engine.mtvu`") in README.
- DIAG rows omitted (not written): `R119ProbesOn`, `GifDiagOn` / `[ADTRACE]` (`gif.cpp:4219-4235`), `r158::` census counters (`ZcEntries/TailAppends/ImgQw/BulkQw/SlowQw`, `LatchCensusAtExit`), `midc::GifTag`, `PERF_SCOPE(GS_KICK)`, `ObjStripReportHook` + `g_objstrip_*`, `KickSrcTally`, `PS2_MIPCEN` census, `PS2_SHADOW8_KTAP` / `[S8-KTAP]` (`ProcessPacket`, no caller), `[GIF-SYNC-FIX]` log line at the stale reset, `[PATH2-STALE-DIAG]`, `[DIAG-EOP]`, `[PATH1-FLUSH-DIAG]`, `[PATHVOL]`, `[RXSEQ]`, `[GEOPROBE]`, `[OBJSTRIP]`, `bullseye_trace`, arbiter `[CUTPIPE]` / `[ARB-SUBMIT]` / `[S8-PKTDUMP]` traces.
- DELETE rows not written: `PS2_S26_DECAL`, `PS2_MID_TEMPLATE` / `_FORCEMISS` (`TplTryServe`), `PS2_R198_PATH3DEFER` / `_BASE` (`packetHasDrawKick`, hold/release), `PS2_SHADOW_TAILFIX` (`s8tailfix`), `PS2_SHADOW_CTXLATCH` / `PS2_SHADOW_ORDERFIX` (`s8ctxlatch`), `PS2_S8_PKTDUMP` / `_MINVF`, the `if (false && g_cutscene_active …)` block, `pathPriority` + the removed stable sort. NEW adds no getenv.

#### Tests added (ctest names)

`GifParserEquivalence` — 23 A/B cases + 2 single-leg pins; every case line quoted from `/tmp/p5/5A/eq2.log`
(the `macos-dev` scalar build) and byte-identical in `/tmp/p5/5A/eq_simd.log` (the `-DPS2R_SIMD=ON` build):

```
case 1_strip3_xyz2                ops=23 hash=9d3e85f86a5e040b triples=7 rows=0 bulkqw=21 PASS
case 1b_strip3_xyzf2              ops=20 hash=191576197a982012 triples=6 rows=0 bulkqw=18 PASS
case 2_split_non16                ops=23 hash=9d3e85f86a5e040b triples=0 rows=0 bulkqw=21 PASS
case 2b_split_midvertex           ops=23 hash=9d3e85f86a5e040b triples=1 rows=0 bulkqw=21 PASS
case 3_ad_fallback                ops=9 hash=3729e5e6c58fbb9a triples=0 rows=0 bulkqw=0 PASS
case 4_reglist                    ops=11 hash=b5409cb5c5bfeb94 triples=0 rows=0 bulkqw=0 PASS
case 5_image_split                ops=15 hash=6500ca743d35aab0 triples=0 rows=0 bulkqw=0 PASS
case 6_nloop0_eop                 ops=16 hash=f30154eceb874bd2 triples=4 rows=0 bulkqw=12 PASS
case 7_xyz3_adc                   ops=28 hash=275bec15d7e2e3d7 triples=4 rows=4 bulkqw=24 PASS
case 8_nregs16                    ops=27 hash=c0c9136123e5c7dc triples=0 rows=2 bulkqw=32 PASS
case 9_row4_whole                 ops=27 hash=9693f85fd7b01994 triples=0 rows=6 bulkqw=24 PASS
case 9b_row4_midrow_split         ops=27 hash=9693f85fd7b01994 triples=0 rows=1 bulkqw=24 PASS
case 10c_strip3_n256              ops=770 hash=3ec6e14af31dbfaf triples=256 rows=0 bulkqw=768 PASS
case texclut_dropped              ops=3 hash=23459ed0e0185292 triples=0 rows=0 bulkqw=0 PASS
case reglist_xyz_adc_ignored      ops=6 hash=c7a23e1fa680d565 triples=0 rows=0 bulkqw=0 PASS
case pre_prim                     ops=6 hash=3e6b36d252d39d7f triples=0 rows=1 bulkqw=1 PASS
case packed_decoders              ops=7 hash=38d294e540362d91 triples=0 rows=0 bulkqw=0 PASS
case nloop0_noeop_generic         ops=11 hash=be2212553432c464 triples=2 rows=0 bulkqw=6 PASS
case nloop0_noeop_allknown        ops on=10 off=8 (bulk arm is the norm)
case flg3_disabled                ops=1 hash=a274580dfcc05878 triples=0 rows=0 bulkqw=0 PASS
case path3_stale_reset            ops=15 hash=8327c522e90fe63d triples=3 rows=0 bulkqw=10 PASS
case path2_continuation           ops=12 hash=3b844f2322454e6c triples=0 rows=0 bulkqw=9 PASS
case m3r_hold                     ops=0 hash=cbf29ce484222325 triples=0 rows=0 bulkqw=0 PASS
case m3r_release                  ops=18 hash=eca9c56e208b9709 triples=4 rows=0 bulkqw=12 PASS
GIF PARSER EQUIVALENCE PASS
```

Pinned inside the cases: op counts (23 / 11 / 15 / 16 …), decoded values (x 1792.0, y 1824.0, z bits, fog 0x40, 700.125 from REGLIST,
4095.9375 / 0.0625 extremes), `0xDEADBEEF` pad never dispatched, PRE PRIM `0x00C` / 11-bit mask on all three routes, TEXCLUT / 0x0B /
0x0E / 0x0F / 0x10 dropped and TEXFLUSH forwarded, REGLIST XYZ2/XYZF2 with bit 47 set still draw + XYZ3/XYZF3 never, PACKED XYZF2
with ADC does not draw, ST passes q = 0 raw, UV 14-bit, FOG byte, unknown PACKED descriptor forwards its 64-bit value, lane census
(`2b`: 1 triple then Tier A; `9b`: 1 row then Tier A; `10c`: 256), FinishPath3 mid-IMAGE no-op vs abandon, register-file readback
of the parser-advanced transfer (STAT `0xE00`, CNT `3 | 1<<16`), hash reproducible and ADC-sensitive.

`GifArbiterOrder`:

```
order: PATH2, PATH3, PATH1 drained in submission order, finish once: ok
finish: once per PATH3 pass, never mid-IMAGE; sub-tag payloads dropped; clear ok
pool: recycle bounds 4 KiB..64 KiB, kPoolMax=4, acquire clears: ok
GIF ARBITER ORDER PASS
```

```
89/91 Test #89: GifRegsStat .........................   Passed    0.01 sec
90/91 Test #90: GifParserEquivalence ................   Passed    0.01 sec
91/91 Test #91: GifArbiterOrder .....................   Passed    0.01 sec
100% tests passed, 0 tests failed out of 91
```

#### Oracle / gate status

```
$ verify/run_bounded.sh 900  /tmp/p5/5A/build5.log -- cmake --preset macos-dev                 -> exit 0
$ verify/run_bounded.sh 1800 /tmp/p5/5A/build7.log -- cmake --build --preset macos-dev -j4     -> exit 0, grep -ci 'warning\|error' = 0
$ verify/run_bounded.sh 1200 /tmp/p5/5A/ctest3.log -- ctest --preset macos-dev
100% tests passed, 0 tests failed out of 91
$ python3 tests/lint/lint_one_class.py --root .
LintOneClass: 330 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root .
LintEnginePurity: 285 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py --root .
getenv sites: 1 -> PASS
# NEON twin (PHASE5_PLAN §7.1 P5-NEON-DEFAULT run):
$ verify/run_bounded.sh 900  /tmp/p5/5A/simd_cfg.log   -- cmake -S . -B /tmp/p5/5A/build-simd -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPS2R_SIMD=ON -DPS2R_DIAGNOSTICS=OFF
-- nativeee: build type Debug, PS2R_DIAGNOSTICS=OFF, PS2R_SIMD=ON, title <title>
$ verify/run_bounded.sh 1800 /tmp/p5/5A/simd_build.log -- cmake --build /tmp/p5/5A/build-simd -j4 --target test_gif_parser_equivalence test_gif_arbiter_order test_gif_regs   -> exit 0, warn/err 0
$ objdump -d … gif_parser.cpp.o | (StageTriple body) | grep -cE '\.4s|ushll|ldr\s+q'   -> 42 (SIMD build) / 0 (macos-dev build)
$ verify/run_bounded.sh 60 /tmp/p5/5A/eq_simd.log -- /tmp/p5/5A/build-simd/tests/gif/test_gif_parser_equivalence   -> exit 0
$ diff <(grep '^case' /tmp/p5/5A/eq2.log) <(grep '^case' /tmp/p5/5A/eq_simd.log)   -> SIMD vs scalar case lines (ops+hash) IDENTICAL
```

Gate status vs PLAN §7 5A green criterion: **GREEN** — `GifRegsStat` (part 1), `GifParserEquivalence` (generic per-QW bodies vs
strip3/rowable lanes → identical `RegisterOpStream::Hash()`; REGLIST odd pad; NLOOP=0 EOP; IMAGE halves; TEXCLUT dropped; REGLIST
XYZ2 ADC ignored) green; `GifArbiterOrder` green (arbiter synchronous half); `sizeof(RegisterOp)==24` / `sizeof(GifTag)==24` pinned
(part 1); lints 0; `CoreSingleGetenvSite` = 1; build 0 warnings; NEON twin proven identical under the bounded `-DPS2R_SIMD=ON` build.
The 5A′ split was NOT needed (part 2 finished inside the budget). No OLD binary oracle exists for the GIF (no tool); the OLD
equivalence suite's cases are the transplanted oracle.

#### Findings (→ plan §7.1)

| Id | Where | Finding | Disposition |
|---|---|---|---|
| P5-5A-NLOOP0-LANES | `gif_parser.cpp` bulk branch vs generic | Q1 (NLOOP=0, EOP=0) is lane-dependent in OLD too: an all-known descriptor set reaches the bulk branch with `remaining == 0` → run 0 → the tag completes without eating a quadword; a non-bulk set eats the next quadword as one register write. The lanes-off leg therefore cannot be A/B-compared on the all-known Q1 case (`ops on=10 off=8`). | Bulk arm is the NORM; pinned single-leg (`nloop0_noeop_allknown`), the generic form A/B'd (`nloop0_noeop_generic`). README row. |
| P5-5A-LANE-ENTRY-ALIGNMENT | `ConsumePackedBulk` | The lanes enter only when `reg == 0` at the START of the run; a mid-vertex / mid-row split rides the per-descriptor remainder for the entire run of that call even after it re-aligns (`2_split_non16` triples=0, `9b` rows=1). Sink sequence identical; only the census differs. Carried verbatim (a lane re-entry inside a run would be a perf-only change). | Recorded; pinned by the census assertions. |
| P5-5A-DROP-TABLE | `RegisterIsForwarded` | The "register has an OLD effect" set is parser knowledge (FACTS §7.1 recommended form): Phase 6 `GsState::WriteRegister` never receives 0x0B/0x0E/0x0F/0x1C/undefined numbers or the four vertex registers. If Phase 6 prefers a GS-side no-op default, the `texclut_dropped` pin must move to the GS test. | Decision recorded; README row. |
| P5-5A-ARBITER-CTOR-INJECT | `gif_arbiter.h` | `GifArbiter(GifParser&)` with a no-argument `drain()` (FACTS §9.7 / part-1 HANDOFF) rather than `drain(GifParser&)`: the 5F/5G/Phase-6 callers (`dmac`, `gs_hle`, `sif_hle`, `vu1_async` in OLD) call `drain()` without a parser argument. | Recorded; 5G extends the same class. |
| P5-5A-TEST-RIG-NAMES | `tests/gif/test_*.cpp` | `lint_one_class` applies the file-name rule to test rigs with member functions; the rigs are `TestGifParser` / `TestGifArbiter` (snake_case prefix of the file name; precedent `TestBoundary` in `tests/ee/test_boundary.cpp`). | Pattern for 5B–5H test files. |
| P5-5A-SIMD-PROOF-SHAPE | `GifParserEquivalence` | The NEON proof is "same binary: NEON lane vs scalar generic path → identical ops/hash" plus "same case lines across the scalar and SIMD builds" (diff of the two logs). `macos-dev` never compiles the NEON body (objdump: 0 vector instructions); the bounded `-DPS2R_SIMD=ON` configure is the only place it runs. | Quoted above; 5B/5E reuse the same run for their NEON twins (plan §7.1 P5-NEON-DEFAULT). |
| P5-GIF-P3-STALE-RESET (closure) | `GifParser::ReceiveData` | Written (part-1 finding); pinned by `path3_stale_reset` + `path2_continuation` and by `finish_counting` (the PATH1 continuation of a PATH3 mid-IMAGE transfer). | Closed in 5A; accepted-difference candidate for Phase 6/8 stays open (behaviour identical to OLD, the rule itself is questionable). |
| P5-GIF-MASK-ALLPATHS (closure) | `GifParser::ReceiveData/ProcessBuffer` | Parser side written: `Path3Masked()` holds every path (`m3r_hold` on PATH1). | Closed in 5A (carried verbatim). |
| P5-GIF-RST-TAIL (closure) | — | `GifRegs::Reset()` does not reach `tail_`; the parser never clears the tail on RST. | Closed (as OLD). |

#### HANDOFF (5A → 5B)

**Done:** all of 5A — `engine/gif/{gif_tag, gif_regs, gs_sink.h, register_op_stream, gif_parser, gif_arbiter (synchronous half)}`,
`ps2r_gif` with both append-only CMake lines, `tests/gif/{counting_gs_sink, test_gif_regs → GifRegsStat, test_gif_parser_equivalence →
GifParserEquivalence, test_gif_arbiter_order → GifArbiterOrder}`, READMEs, the 5 plan doc fixes, the NEON `StageTriple` twin proven under
the bounded SIMD build. Tree GREEN (91/91). No commits (the user commits).

**Not done (moved):** to **5G** — `GifArbiter` cross-thread half (markers, inbox, boundary slots / `boundary_seq` use, `PendingSlots`,
back-pressure statics, `GifDrainThread`, `GifDeferBoundaryOrder` / `GifDrainThread` tests). To **5C** — `GifParserEquivalence`'s corpus
half (plan §4.4: feed every PATH1 kick of the 126 `vutrace_cases2` cases through `GifParser` → `RegisterOpStream` and write
`tests/gif/fixtures/gsops_seed.txt`; needs the VU1 interpreter). To **Phase 6** — `GsState : GsSink` (must reproduce: Q=1.0 at
`BeginTag`, PACKED ST `q != 0.0f` rule + uv-since-st latch, PACKED FOG as the FGE bool, RGBAQ 64-bit Q rule, HWREG re-activation only via
`ImageData`, SIGNAL/FINISH/LABEL CSR/INTC side effects, `FinishPath3` draw-buffer emptiness check); the `register_op_stream` move to
`engine/gs/hle/ir`. Optional, not started: an in-test independent reference walker (FACTS §10.1 leg iii) — the OLD-transcribed cases plus
the pinned values are the oracle today.

**Integration list (for the 5H checklist):** `GifRegs` → `MmioRouter::Register(0x10003000, 0x100, regs, "GIF")` displacing the
`MmioSink` idle row; `GifParserSettings::bulk_lanes` is NOT a Config key (test knob only); no Config keys from 5A; `GifArbiter` is the
seam `XgKick` (5B, `submitOwned(Path1, …)` after `acquireBuffer()`), `Vif` DIRECT/DIRECTHL (5E, `submit(Path2, …, directhl)`) and
`ChainWalker` (5F, `submitOwned(Path3, …)`) code against; the DMAC/GS-side callers call `hasPending()` then `drain()`.

**Gate status:** GREEN — `100% tests passed, 0 tests failed out of 91`; `LintOneClass: 330 files scanned, 0 findings -> PASS`;
`LintEnginePurity: 285 files scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; build 0 warnings (both builds).

**Next command (stage 5B):** `cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect
91/91), then per PHASE5_PLAN §7 row 5B: `engine/vu/` core (`vu1_state`, VU1 interpreter, `XgKick` submitting to `GifArbiter&` from
`engine/gif/gif_arbiter.h`) against `/tmp/p5/5B/FACTS.md`; `GifArbiter::submitOwned(GifPath::Path1, std::move(buf))` with
`buf = arbiter.acquireBuffer(); buf.reserve(0x4000)` is the XGKICK contract.

## 5B — engine/vu core + interp tier (2026-09-27; three parts, ≈40 + 35 + 42 min)

### 5B part 1 — vu1_state / vu_decoder / span_classifier / vu_float / vu_chop_round_guard / vu_image_hash / vu1_case

Written against `/tmp/p5/5B/FACTS.md` (§0, §1, §2, §3, §6, §7, §9, §10, §13, §15, §16) and PHASE5_PLAN §2.2 / §7 row 5B.
OLD was not opened. Tree GREEN at the stop (94/94, both lints 0, getenv 1, build 0 warnings; SIMD twin build 0 warnings).

### Landed (files)

| File | Class | OLD source file:lines transplanted |
|---|---|---|
| `engine/vu/vu1_state.h` | `Vu1State` POD (`VUregs regs` verbatim from `engine/ee/cpu/cpu_state.h`, `alignas(16) micro[16384]`, `data[16384]`, `mem_mask` (0x3FFF / 0x0FFF constants), per-run scratch `suspended, suspended_tpc, xtop_read_count, xtop_exit_requested, xgkick_has_fired, xgkick_count, cur_bundle_pc`, `micro_generation`); `using Vu0State = Vu1State` (FACTS §16.2 recommendation: ONE record, VU0 uses the low 4 KB through `mem_mask`) | `vu.h:464-1167` (members, FACTS §1.5), `memory.h:17-20`, `vu.cpp:717, :761-775` |
| `engine/vu/vu_chop_round_guard.h/.cpp` | `VuChopRoundGuard` RAII `FE_TOWARDZERO` (ctor/dtor bodies in the .cpp — the lint's one-statement allowance) | `vu.cpp:54-69` (FACTS §6) |
| `engine/vu/vu_float.h/.inl/.cpp` | `VuFloat` statics: `Bits2F/F2Bits` (memcpy), `ClampBits1`, `ClampFloat`, `ClampStoreACC`, `WriteMaskClamp`, `kLaneMask[16][4]` (alignas 16), `MacFlags1`, `PackMacBits` (= OLD `PackBitsScalar`), `CommitQStatus`, `FpMax/FpMin`, `ApplyMinMaxRaw`, `VuDiv`, `FtoiSat`, `Ftoi0` (library form, clamps first), `Ftoi4/12/15` (interp form, raw scaled), `Itof0`; NEON `ClampBits4`/`MacFlags4` in the `.inl` under `#if defined(PS2R_SIMD) && PS2R_SIMD && defined(__aarch64__)`. Hot one-liners live in `vu_float.inl` (the sanctioned textual sibling), everything else in the .cpp | `vu_fast_exec.h:38-47, :121-145, :158-222, :357-360, :1807-1833`; `vu.cpp:1814-1829, :1861-1879`; `vu1_hle_programs.cpp:72-101, :1388-1393` (FACTS §7.1-7.6, §4.3) |
| `engine/vu/vu_image_hash.h/.cpp` | `VuImageHash::Fast` — 4-lane FNV-1a, seeds/prime/tail rule verbatim | `vu_hash_fnv.h:22-50` (FACTS §9) |
| `engine/vu/vu1_case.h/.cpp` | `Vu1Case` FROZEN POD (magic `0x31435556` "VUC1", version 1, every offset static_asserted, `sizeof == 33456`) + `vu1_case::ReadCase/WriteCase` | `vu_trace_case.h:24-79` (FACTS §10; `VuTraceForce :85-89` not written) |
| `engine/vu/vu_decoder.h/.cpp` | `VuDecoder` statics `DecodeVuSlot(lower, upper)`, `DecodeStallStatics`, `ClassifyFastKinds`, `VuUpperCanTouchHardwired`, `VuLowerCanTouchHardwired`, `VuSlotCanTouchHardwired`, `IsBranchKind`; `VuDecodedSlot` 60-byte POD (field order of FACTS §1.2, `static_assert(sizeof == 60)` + 4 offset pins); `VUK_U_*` (28 values, the FACTS §1.1 list verbatim — its "29 values" caption is a miscount) / `VUK_L_*` (30); constant namespaces `vu_dest`, `vu_status`, `vu_mac` (FACTS §1.4). No `.inl` (nothing needed `constexpr`) | `vu.h:61-200, :255-321`; `vu_decode_slot.h:21-217, :219-378, :380-470, :537-558, :560-628, :630-634, :643-647` (FACTS §2.1-2.4) |
| `engine/vu/span_classifier.h/.cpp` | `SpanClassifier` statics `ComputeRunLenTouchHw`, `ComputeStallClasses`, `ComputeFoldClasses`, `ComputeSpanRem`; `kFmacWbLatencyShared = kIaluLwLatencyShared = 4` | `vu_span_classify.h:34-195` (FACTS §3.1-3.4) |
| `engine/vu/CMakeLists.txt` | `ps2r_vu` STATIC (6 .cpp), include `${CMAKE_SOURCE_DIR}` PUBLIC, `-Wall -Wextra -Werror -O2 -fno-strict-aliasing -fwrapv` (pinned regardless of preset — FACTS §0.5; no `-ffast-math`, contraction at the clang default), links `ps2r_gif ps2r_ee ps2r_core`, `PS2R_SIMD` passthrough as `engine/gif` | — |
| root `CMakeLists.txt` (+1 line), `tests/CMakeLists.txt` (+1 line) | `add_subdirectory(engine/vu)` after the gif line; `add_subdirectory(vu)` at the end | — |
| `tests/vu/CMakeLists.txt` | `ps2r_vu_test(name ctest_name [args])` (pattern `tests/gif`; `SKIP_RETURN_CODE 77`, TIMEOUT 120) + 3 rows | — |
| `tests/vu/test_vu_decode.cpp` | `VuDecodeTable` | NEW (FACTS §16.6 `VuDecodeStatics`) |
| `tests/vu/test_vu_float.cpp` | `VuFloatRules` | NEW (FACTS §16.6) |
| `tests/vu/test_vu1_case.cpp` | `Vu1CaseRoundTrip` | NEW (FACTS §16.6) |
| `engine/vu/README.md` | Belongs list corrected; Files table (7 rows) + decode/float notes | — |

File names: the lint's snake_case(primary type) rule renamed the plan's `decode` → `vu_decoder`, `chop_round_guard` →
`vu_chop_round_guard`, `span_classify` → `span_classifier` (finding P5-5B-FILENAME-LINT; README states the mapping).

### Flags applied

| Flag | Outcome | How it landed |
|---|---|---|
| `PS2_VU_FASTPACK` | NORM | `VuImageHash::Fast` is the only image hash written (the slow byte-wise twin is not written) |
| `PS2_VU_NOCLAMP` | DELETE | not written — `ClampBits1` has no bypass arm |
| `PS2_VU_CLAMPELIDE` | DELETE | not written — every masked write clamps (`WriteMaskClamp`), ACC re-clamped by `ClampStoreACC` |
| `PS2_VU_SUPERBLOCK`, `PS2_VU_STALLFOLD`, `PS2_VU_SPANFOLD` (classifier side) | NORM | the four `SpanClassifier` passes exist unconditionally; the run bodies are 5C |
| `PS2_VU_SPANFOLD2`, `PS2_VU_RUNFLAT`, `PS2_VU_RUNFLAT_CENSUS` | DELETE | not written — `ComputeSpanRem` reads fold classes 15/16 only (no F1 flat pass, no class 17) |
| `PS2_VU_LAZYFLAGS` (scalar-vs-NEON representation) | NORM (compile-time `PS2R_SIMD`) | part 1 provides both flag packers (`PackMacBits` scalar, `MacFlags4` NEON); the rings are part 2 (`FlagPipeline`) |
| `PS2_VUTRACE` / `VuTraceForce` | DIAG / not written | `Vu1Case` is the record only; no capture path |

P5-FP-CONTRACT (interp tier): resolution recorded now for part 2 — MADD/MSUB/MADDA/MSUBA/OPMSUB lanes are to be written as
`std::fmaf(a, b, acc)` / `std::fmaf(-a, b, acc)` (FACTS §7.7) so `-ffp-contract` cannot move a result; `ps2r_vu` pins `-O2`.

### Tests added (ctest names)

```
92/94 Test #92: VuDecodeTable .......................   Passed    0.27 sec
93/94 Test #93: VuFloatRules ........................   Passed    0.01 sec
94/94 Test #94: Vu1CaseRoundTrip ....................   Passed    0.01 sec
100% tests passed, 0 tests failed out of 94
```
Direct runs:
```
$ ./build/tests/vu/test_vu_decode
VuDecodeTable: PASS (sizeof(VuDecodedSlot)=60, 28 upper kinds, 30 lower kinds)
$ ./build/tests/vu/test_vu_float
VuFloatRules: PASS (scalar)
$ ./build/tests/vu/test_vu1_case $PS2R_OLD_ROOT/vutrace_cases2/vu1_case_0000_1748.bin
case .../vu1_case_0000_1748.bin: start_pc 0x1748 image_hash a11b3516d81359c3 image a11b3516 vu_cycle 3575943 q_pending 0 p_pending 0
Vu1CaseRoundTrip: PASS (sizeof(Vu1Case)=33456)
```
`VuDecodeTable` covers every upper family (bc/q/i/vector/OPMSUB/unused 0x30-0x3B/every special2 op incl. the ABS-0x3C
read-only divergence, CLIP masks, NOP), every lower family (LQ/SQ/ILW/ISW/IADDIU/ISUBIU/flag ops/all 10 branches/the 0x40
group incl. MOVE-NOP, MR32 rotated mask, LQI/SQI/LQD/SQD, DIV/SQRT/RSQRT/WAITQ, MTIR/MFIR/ILWR/ISWR, R*, MFP, every EFU op,
XGKICK/XTOP), I-bit, discard/hide hazards, the two hard-wired predicates on ~50 words, `IsBranchKind`, the 60-byte layout
pin, and the four `SpanClassifier` passes (run_len chain + 255 cap, all 14 stall classes + 4 dynamic corners, fold window of
3, span_rem). `VuFloatRules` covers the clamp table, the MAC nibble rules on raw bits, `kLaneMask`, `WriteMaskClamp`,
`ClampStoreACC`, `CommitQStatus` bit ownership, `FpMax/FpMin` sign cases, `VuDiv` zero/denormal/overflow signs and
truncation (1/3 → 0x3EAAAAAA), FTOI saturation edges (0x4EFFFFFF / 0x4F000000 / 0xCF000000 / NaN by sign), FTOI4/12/15
scaling, ITOF, and the guard's set/restore/nesting; under the SIMD configure it also cross-checks `ClampBits4`/`MacFlags4`
against the scalar functions on 16 masks × 3 lane sets. `Vu1CaseRoundTrip` pins every offset, round-trips a synthetic
record, rejects wrong magic/version/short files, checks the image-hash tail rule, and reads the real corpus case.

### Oracle / gate status

- **Image-hash gate (plan §7 row 5B):** `VuImageHash::Fast(case.micro_mem, 16384)` of `vutrace_cases2/vu1_case_0000_1748.bin`
  = `a11b3516d81359c3`; `/tmp/p5/5P/old_catalog.log:50` reads `a11b3516 a11b3516d81359c3 cefee708f2bdc7cd 0x1748 3 3 42 711
  0.16% 0x1990 1720>90 family vs_sprite_text` — IDENTICAL; pinned in the test as `kCase0000ImageHash`.
- `cmake --preset macos-dev` → `-- nativeee: build type Debug, PS2R_DIAGNOSTICS=OFF, PS2R_SIMD=OFF`; `cmake --build --preset
  macos-dev -j4` → rc 0, `grep -c warning` = 0 (`/tmp/p5/5B/build.log`).
- `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 94` (`/tmp/p5/5B/ctest.log`).
- `python3 tests/lint/lint_one_class.py --root .` → `LintOneClass: 346 files scanned, 0 findings -> PASS`;
  `python3 tests/lint/lint_engine_purity.py --root .` → `LintEnginePurity: 300 files scanned, 0 game tokens -> PASS`;
  `python3 tests/core/check_getenv_sites.py --root .` → `getenv sites: 1 -> PASS`.
- Bounded SIMD twin (P5-NEON-DEFAULT): `cmake -S . -B /tmp/p5/5B/build-simd -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPS2R_SIMD=ON`
  + build of the three suites → 0 warnings; `VuFloatRules: PASS (scalar + NEON ClampBits4/MacFlags4 twins)`;
  `VuDecodeTable: PASS ...` (`/tmp/p5/5B/simd.log`).
- Every command ran through `verify/run_bounded.sh`; no OLD binary was run; OLD read only for the one corpus file.

### Findings (→ plan §7.1)

| Id | Where | Finding | Disposition |
|---|---|---|---|
| P5-5B-FILENAME-LINT | `engine/vu/` | `lint_one_class` names files after the primary type: `decode.h` → `vu_decoder.h`, `chop_round_guard` → `vu_chop_round_guard`, `span_classify` → `span_classifier` (5A precedent P5-5A-TEST-RIG-NAMES). Plan §2.2 rows keep their logical names. | Renamed; README maps them. |
| P5-5B-VU0STATE-ALIAS | `vu1_state.h` | `Vu0State` is `using Vu0State = Vu1State` with `mem_mask = kVu0MemMask` (FACTS §16.2): one record type, one file, `Vu1Case` copies 16 KB straight in; the VU0 instance never reaches the upper 12 KB. | Recorded; `VuUnit(id)` (part 2) sets the mask. |
| P5-5B-KIND-COUNT | `vu_decoder.h` | FACTS §1.1 captions the upper kinds "29 values, 0..28" but lists 28 (`VUK_U_ITOF0 == 27`); the list order is carried verbatim. | Pinned in `VuDecodeTable`. |
| P5-5B-RSQ-NOT-GENERIC | `vu_float.h` | `Rsq` (plan §2.2 row) is library-specific (ITOF0→MULAw→MADDw→FTOI0 over a constant row, FACTS §7.6) — not written in `VuFloat`; its primitives (`Itof0`, `ClampFloat`, `Ftoi0`, fmaf) are. | 5D writes it in `library_helpers`. |
| P5-5B-FTOI-TWO-FORMS | `vu_float.h` | `Ftoi0` clamps the source (library form, FACTS §7.6); the interpreter's FTOI0/4/12/15 use the RAW source (FACTS §11.4). Equal for scale 1; `Ftoi4/12/15` carry the raw form, `FtoiSat(float)` is the shared saturation. | Both pinned in `VuFloatRules`. |
| P5-5B-MICROGEN-PLAIN | `vu1_state.h` | `micro_generation` is a plain `uint64_t` in the POD (single writer under the deterministic profile); if 5G's MTVU worker needs atomicity, `VuUnit` wraps the bump. | Part 2 / 5G decide. |

### HANDOFF (5B part 1 → 5B part 2)

**Done:** `engine/vu/{vu1_state.h, vu_chop_round_guard, vu_float(+.inl), vu_image_hash, vu1_case, vu_decoder, span_classifier}`,
`ps2r_vu` target + both append-only CMake lines, `tests/vu/{CMakeLists.txt, test_vu_decode → VuDecodeTable, test_vu_float →
VuFloatRules, test_vu1_case → Vu1CaseRoundTrip}`, README Files table, image-hash gate identical to the OLD catalog. Tree
GREEN (94/94). No commits (the user commits).

**Not done (part 2 — remaining 5B core, FACTS §16.1 steps 8-11 + tests):**
- `engine/vu/flag_pipeline.h/.cpp` — `FlagPipeline` (FACTS §4): CLIP ring (linear, 8, `kClipDelay 4`, `clip_latest`,
  `SetClipImmediate` for FCSET), MAC/STATUS circular ring 8 (`kMacDelay 4`) with BOTH representations compiled (eager
  `PushMac/ApplyMacToStatus`, lazy `PushMacRaw/AccumulateLazyEntry/MaterializeMacStatus` scalar `lazy_or_mac` twin + NEON
  accumulators under `PS2R_SIMD`), `RetireDue/RetireMacStatus/FlushMacPipe`, `UpdateMACFlags` picking `kLazy = PS2R_SIMD != 0`
  (ctor flag for the algebra test), Q latch (`PushQ/VisibleQ/FlushQPipe`, delays 7/7/13, uses `VuFloat::CommitQStatus`), P
  latch (`PushP` releases one cycle early, `VisibleP` no clamp, `FlushPPipe` full latency, the 13 EFU latencies), the ONE
  `vu_cycle` owner + accessors for `Vu1Case` replay (`q/p_pending_*`).
- `engine/vu/stall_model.h/.cpp` (+ `stall_model.inl` for `ApplySpec<M>`) — `StallModel` (FACTS §5): `FmacWb[8]`/`IaluWb[4]`
  rings, `Apply` (= `ApplyFmacReadStallsFast`, the class-0 body), `ApplyClassed` table + `ApplySpec<M>` (5C consumers),
  `FlushFmacWb/FlushIaluWb`, `Reset`. `ApplyFmacPrunePush` NOT written (5C).
- `engine/vu/xgkick.h/.cpp` — `XgKick(GifArbiter&, bool paced_drain = true)` (FACTS §8): `Arm/Pace/Flush/Reset/PacketSize`,
  quantum 32, 1 QW per 2 cycles, second-kick stall, `0x4000` cancel, `0x40000` runaway, `& 0x3FFF` wrap; the 5A contract
  `buf = arbiter.acquireBuffer(); buf.reserve(0x4000); … arbiter.submitOwned(GifPath::Path1, std::move(buf))`.
- `engine/vu/vu0/libvu0_sincos.h/.cpp` — `LibVu0SinCos` (FACTS §12).
- Tests: `tests/vu/test_lazyflags_algebra.cpp` → `VuLazyFlagsAlgebra` PART 1 (eager ≡ lazy over the FACTS §14.2 generator; both
  representations in one binary via the ctor flag; also run once under the bounded `-DPS2R_SIMD=ON` configure at
  `/tmp/p5/5B/build-simd`), plus the FACTS §16.6 micro tests `VuFlagPipelineTiming`, `VuStallModel`, `VuXgKick` (reuse
  `tests/gif/counting_gs_sink.{h,cpp}` — add it to `ps2r_vu_test`'s sources or link a local sink).
- Part 3 then: `vu_unit.h/.cpp` (`VuUnit`, FACTS §13, `LoadCase/SaveCase` helpers), `tiers/interp_tier.h/.cpp`
  (`VuInterpreterTier`, FACTS §11, `std::fmaf` for the MADD family), the four ported OLD tests (`Vu1TransformGroundTruth`,
  `Vu0ShadowReplay`, `Vu0VisMicro`, `Vu0DecalEval`) with the `.inc` → `fixtures/*.bin` lift (FACTS §14.6), README rows,
  the 5B section close-out + HANDOFF to 5C (cached legs of the VU0 tests / lazyflags PART 2 move to 5C).

**Integration list (for 5H):** none new from part 1 (no Config keys, no MMIO, no `<Module>Settings` yet).

**Gate status:** GREEN — `100% tests passed, 0 tests failed out of 94`; `LintOneClass: 346 files scanned, 0 findings -> PASS`;
`LintEnginePurity: 300 files scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; build 0 warnings (dev + SIMD twin).

**Next command (part 2):** `cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`
(expect 94/94), then write `engine/vu/flag_pipeline.{h,cpp}` against FACTS §4 using `VuFloat::{MacFlags1, PackMacBits,
CommitQStatus, ClampFloat, kLaneMask}` from `engine/vu/vu_float.h`, add it to `engine/vu/CMakeLists.txt`, and land
`VuLazyFlagsAlgebra` PART 1.

### 5B part 2 — flag_pipeline / stall_model / xg_kick / vu_unit (2026-09-27, ≈35 min)

Written against `/tmp/p5/5B/FACTS.md` (§1.5, §4, §5, §8, §13, §16) and PHASE5_PLAN §2.2 rows `flag_pipeline`,
`stall_model`, `xgkick`, `vu_unit` + §7.1 P5-XGKICK-DEFER. OLD was not opened. Tree GREEN at the stop (99/99, both
lints 0, getenv 1, build 0 warnings; SIMD twin 0 warnings, all six VU suites PASS with the NEON fold).

### Landed (files)

| File | Class | OLD source file:lines transplanted |
|---|---|---|
| `engine/vu/flag_pipeline.h/.cpp` | `FlagPipeline` — owner of the ONE `vu_cycle`; CLIP ring (linear 8, `kClipDelay 4`, 24-bit mask, overflow retires oldest, `FlushClipPipe`, `SetClipImmediate` = FCSET, `clip_latest`); MAC/STATUS circular ring 8 (`kMacDelay 4`) — BOTH representations compiled unconditionally: eager `PushMac/ApplyMacToStatus`, lazy `PushMacRaw/AccumulateLazyEntry/LazyRingOverflow/MaterializeMacStatus` (NEON accumulators under `PS2R_SIMD && __aarch64__`, scalar `lazy_or_mac` OR-fold otherwise), `RetireDue/RetireMacStatus/FlushMacPipe`, `UpdateMACFlags(regs, VR_reg, mask)` picking the ctor's representation, `kDefaultLazy = PS2R_SIMD != 0`; Q latch (`PushQ` clamps + waits/commits a pending Q, `VisibleQ` returns the clamped register, `FlushQPipe`, 7/7/13); P latch (`PushP` no clamp + early release, `VisibleP`, `ReleasePEarly` = retire-1, `FlushPPipe` = full latency, 13 EFU latencies); `mac_depth/PackPendingMac/mac_retire`, `q_pending_*/p_pending_*` + `SetQPending/SetPPending` (case replay) | `vu.h:625-633, :654-656, :661-687, :731-737, :873-888`; `vu.cpp:797-829, :872-935, :950-1107, :1109-1161, :1228-1258, :1897-1908`; `vu_fast_exec.h:259-330, :357-360` (`UpdateStatusFlags :1920-1934` DEAD — not written) |
| `engine/vu/stall_model.h/.inl/.cpp` | `StallModel` — `FmacWb[8]` (+ `min_retire`, `regs` mask) and `IaluWb[4]` rings; the ONE body `ApplyBody<M>` in the `.inl` (steps 1-8 of FACTS §5.2 with `if constexpr` pruning) behind `Apply` (= `ApplyFmacReadStallsFast`, M = kDynamic: run-time guards + branch/EFU/FDIV waits), `ApplySpec<M>` (= `ApplyFmacReadStallsSpec<M>`) and `ApplyClassed` (the 14-row class table, default → `Apply`); `FlushFmacWb/FlushIaluWb` advance the clock and empty the rings; `Reset`; `VUSM_*` as `kRd/kWr1/kWr2/kIaluWr/kLower` | `vu_fast_exec.h:337-352, :387-388, :393-639`; `vu.cpp:1198-1214` (runtime twin `:1261-1584`, `ApplyFmacPrunePush :616-663` NOT written — 5C / HACK 269) |
| `engine/vu/xg_kick.h/.cpp` | `XgKick(GifArbiter&, bool paced_drain = true)` — `PacketSize(data, offset, flush)` raw tag walk (PACKED `nregs*nloop*16`, REGLIST odd pad, IMAGE/DISABLED `nloop*16`, NREG 0 → 16, `& 0x3FFF` wrap, `>= 0x4000` → 0), `Arm` (second kick → `Flush(true)` charging `remaining_qw*2`; `cyclecount = 1`; `next_due = cycle + 33`), `Pace` (once per bundle; acts when `cycle >= next_due`, `elapsed = cycle - lastcycle - 1`), `Transfer` (bank cycles, 1 QW / 2 cycles, copy AT DRAIN TIME, wrap-edge split, `0x40000` runaway drop, cancel keeps staged bytes, submit ONE packet via `acquireBuffer` + `submitOwned(Path1)`), `Flush`, `Reset`; `paced_drain=false` = `Transfer(0, flush, no stall)` right after the arm | `vu.h:930-1000`; `vu.cpp:11359-11384, :11566-11732, :11855-11860, :12233-12251` (legacy `xgkick_active/addr`, census counters, snapshot arm `:12262-12389` not written) |
| `engine/vu/vu_unit.h/.cpp` | `VuUnit(id, GifArbiter* = nullptr, xgkick_paced_drain = true)` — heap-owned `Vu1State` (`mem_mask` 0x0FFF / 0x3FFF by id, `VUregs::Reset`), `FlagPipeline`, `StallModel`, `XgKick` (VU1 only); `WriteDataMem/WriteMicroMem` (bumps `micro_generation`) / `ReadDataMem` masked byte loops, `BumpMicroEpoch`, `Reset` (XGKICK disarm, stall rings, P latch, per-run scratch — NOT registers/memories/clock/CLIP-MAC-Q rings), `SetVifTop`, `LoadCase/SaveCase` (regs, both memories, tops, Q/P latches, clock), accessors `state/regs/flags/stalls/xgkick/cycle/micro_generation`. `Execute` intentionally absent (5C `VuTierDispatch`; the interp tier drives the unit through the accessors) | `vu.cpp:714-775, :1598-1602, :1792-1798, :1804-1810, :12416-12418` (`:1603-1791` probes not written) |
| `engine/vu/CMakeLists.txt` | +4 sources | — |
| `tests/vu/CMakeLists.txt` | +5 rows | — |
| `tests/vu/test_vu_flag_pipeline.cpp` | `VuFlagPipeline` | NEW (FACTS §16.6 `VuFlagPipelineTiming`) |
| `tests/vu/test_lazyflags_algebra.cpp` | `VuLazyFlagsAlgebra` (PART 1) | OLD `tests/test_lazyflags_algebra.cpp` PART 1 per FACTS §14.2 (PRNG, pool, action mix, ring hash) |
| `tests/vu/test_vu_stall_model.cpp` | `VuStallModel` | NEW (FACTS §16.6) |
| `tests/vu/test_xg_kick.cpp` | `VuXgKick` (rig type `TestXgKick`) | NEW (FACTS §8.6 cases) |
| `tests/vu/test_vu_unit.cpp` | `VuUnitSurface` | NEW |
| `engine/vu/README.md` | +4 Files rows, pipeline/stall/XGKICK notes | — |

File name: the lint renamed the plan's `xgkick` → `xg_kick` (snake_case of `XgKick`; P5-5B-FILENAME-LINT).

### Flags applied

| Flag | Outcome | How it landed |
|---|---|---|
| `PS2_VU_LAZYFLAGS` | NORM (compile-time) | `FlagPipeline::kDefaultLazy = (PS2R_SIMD != 0)`; no env, no latch. Both representations compiled for the algebra test (ctor bool), the NEON fold only under `PS2R_SIMD && __aarch64__`. The `PS2_VUTRACE`-forces-eager arm not written |
| `PS2_VU_RUNRETIRE` | NORM ON | the interp path never pre-retires per bundle (`RetireMacStatus` is the only reader); the cached-tier consumer (`VuMacPreRetireDue` false) is 5C's — nothing to write here |
| `PS2_VU_STALLSPEC` | NORM ON | `ApplySpec<M>` / `ApplyClassed` exist as thin entry points over the ONE body; the "spec off → always dynamic" arm not written |
| `PS2_VU_XGKPACE_QUANTUM` | NORM 32 | `XgKick::kQuantum = 32` constexpr |
| `PS2_VU_PACE_NEXTDUE` | NORM ON | `Pace` early-outs on `cycle < next_due` (the winning arm); the elapsed-recompute arm not written |
| `PS2_VU_RUNPACE` | NORM ON | slim pace form belongs to the cached run body (5C); `Pace` is the one entry it will call |
| `kEnableMacPipe / kEnableQPipe / kEnableEfuPipe / kEnableReadStalls / kEnableIaluStalls / kEnableXgkickPace / kFixFlagDestIT` | NORM true | only the true arms exist |
| `PS2_R198_XGKPROBE`, `PS2_R198_XGK_WINDOW`, `PS2_W15_KICKCENSUS`, `PS2_PERF` pace census, `PS2_S13_*` / `PS2_S15_*` / `PS2_S8_*` probes | DIAG / DELETE | not written (no counters, no taps) |
| `PS2_VU0_MODE`, `PS2_VU1_MODE` | SETTING (tier choice) | not here — `VuUnit` has no tier yet; 5C's `VuTierDispatch` takes a `VuSettings` field |

### Tests added (ctest names)

```
95/99 Test #95: VuFlagPipeline ......................   Passed    0.01 sec
96/99 Test #96: VuLazyFlagsAlgebra ..................   Passed    0.02 sec
97/99 Test #97: VuStallModel ........................   Passed    0.01 sec
98/99 Test #98: VuXgKick ............................   Passed    0.24 sec
99/99 Test #99: VuUnitSurface .......................   Passed    0.18 sec
100% tests passed, 0 tests failed out of 99
```
Direct runs (macos-dev):
```
VuFlagPipeline: PASS (eager + lazy rings, Q/P latches)
VuLazyFlagsAlgebra: PASS (250 sequences, 11155 actions, eager == lazy [scalar fold])
VuStallModel: PASS
VuXgKick: PASS (paced == immediate through GifArbiter -> GifParser -> RegisterOpStream)
VuUnitSurface: PASS
```
`VuFlagPipeline`: CLIP push/visible at exactly +4 (invisible at +3), later-due-wins, 9th-push overflow, flush,
FCSET drop; MAC (run twice, eager and lazy): -0 → Z+S, denormal → Z+U, Inf → O, clean result clears current
Z/S/U/O but not sticky, dest-lane judging, FDIV bits 4-5/10-11 survive every commit, overflow at push (both
representations show the evicted entry), flush never moves the clock; Q: visible at +7 / +13, status commit
(D/I replace, DS/IS OR), WAITQ jumps the clock, back-to-back FDIV waits+commits, push clamps, `VisibleQ`
returns the clamped register while the register keeps raw bits; P: visible at +11, `ReleasePEarly` = retire-1,
`PushP` over pending = early release then new latency, `FlushPPipe` = full latency, no clamp; the 13-entry EFU
table. `VuLazyFlagsAlgebra` PART 1: FACTS §14.2 generator (xorshift64*, 16-word adversarial pool, 45/25/15/10/5
action mix, 8 Q-status words, FNV ring view) → equal read logs, ring views after every action, final
MAC/STATUS/clock, empty ring; plus 200 `UpdateMACFlags` eager-vs-lazy sequences. `VuStallModel`: RAW stall to
+4, lane-disjoint no stall, VF0 never queued/never stalls, ILW→IADDIU no stall / ILW→IBNE stall, I-bit bundle
skips the IALU prune, DIV-after-DIV waits to +7 and commits Q+status, WAITQ idle no-op, WAITP / second EFU
release at retire-1, 9-in-4-cycles overflow drops the soonest-retiring entry (last entry into its slot, stale
`min_retire`/`regs` bounds), IALU ring of 4, flushes advance the clock, Reset, `ApplyClassed` ≡ `Apply` over a
classified program, `ApplySpec<kWr1>` ≡ `Apply`. `VuXgKick`: `PacketSize` over PACKED/REGLIST(odd pad)/IMAGE/
DISABLED/NREG 0, one-tag vs to-EOP walk, wrap, `0x4000` cancel edge (1023 loops cancels, 1022 = 0x3FF0 valid),
unaligned offset; pacing 1033/1065/late-bank schedule (16 QW per quantum, 9-QW tail completes, clock untouched),
drain-time copy (late store ahead of the pointer lands, drained QW unchanged), second kick stalls 25×2 and
submits the first packet, program-end flush without charge, wrap straddle 0x3FF0→0x0000 as one submission
(both arms), cancel submits nothing (both arms) / cancel after a partial chain submits the staged bytes,
runaway all-zero chain dropped past 0x40000, Reset; three-kick mixed program → identical `RegisterOpStream`
hash + op count + byte total for `paced_drain` true and false. `VuUnitSurface`: masks (VU1 0x3FFC straddle,
0x14000 fold, VU0 0x0FFE straddle, upper 12 KB untouched), micro epoch bumps, Reset scope (regs/clock/CLIP/
MAC/Q kept; P/stalls/XGKICK/scratch dropped), VIF tops, `SaveCase → LoadCase → SaveCase` byte-identical.

### Oracle / gate status

- `cmake --preset macos-dev && cmake --build --preset macos-dev -j4` → rc 0, `grep -c warning` = 0
  (`/tmp/p5/5B/build5.log`).
- `ctest --preset macos-dev` → `100% tests passed, 0 tests failed out of 99` (`/tmp/p5/5B/ctest3.log`).
- `python3 tests/lint/lint_one_class.py --root .` → `LintOneClass: 359 files scanned, 0 findings -> PASS`;
  `python3 tests/lint/lint_engine_purity.py --root .` → `LintEnginePurity: 309 files scanned, 0 game tokens -> PASS`;
  `python3 tests/core/check_getenv_sites.py --root .` → `getenv sites: 1 -> PASS`.
- Bounded SIMD twin (P5-NEON-DEFAULT): `cmake -S . -B /tmp/p5/5B/build-simd -G Ninja -DCMAKE_BUILD_TYPE=Debug
  -DPS2R_SIMD=ON` + build of the six VU suites → 0 warnings; `VuLazyFlagsAlgebra: PASS (400 sequences, 18060
  actions, eager == lazy [NEON fold])`, `VuFlagPipeline/VuStallModel/VuXgKick/VuUnitSurface: PASS`,
  `VuFloatRules: PASS (scalar + NEON ClampBits4/MacFlags4 twins)` (`/tmp/p5/5B/simd2.log`).
- No OLD binary was run; OLD was not read.

### Findings (→ plan §7.1)

| Id | Where | Finding | Disposition |
|---|---|---|---|
| P5-5B-CYCLE-OWNER | `flag_pipeline.h` | `vu_cycle` lives in `FlagPipeline` (FACTS §16.3 option 1); `StallModel` takes `FlagPipeline&`, `XgKick` a `uint64_t&`, `Vu1Case` replay goes through `SetCycle`. | Recorded; the interp tier uses `flags().cycle()`. |
| P5-5B-ONE-STALL-BODY | `stall_model.inl` | Spec/Fast/Classed are ONE template body (`ApplyBody<M>`); `kDynamic` adds the run-time guards and the branch/EFU/FDIV waits. FACTS §5.3's unrolled push order (wr_reg[0] under WR1\|WR2, wr_reg[1] under WR2) is kept literally. | Pinned by `VuStallModel` (classed ≡ dynamic). |
| P5-5B-XGK-CANCEL-KEEPS-STAGED | `xg_kick.cpp` | A tag walk reaching 0x4000 AFTER earlier tags were staged still submits the staged bytes (OLD order: cancel breaks the loop, the submit block runs). | Kept; pinned in `VuXgKick`. |
| P5-5B-FLUSH-RESETS-BOUNDS | `stall_model.cpp` | `FlushFmacWb` zeroes `min_retire`/`regs` (OLD left them stale); unobservable (`n == 0` guards every use) — FACTS §5.5 sanctioned. | Recorded. |
| P5-5B-XGK-TOPS | `vu_unit.cpp` | `SaveCase` writes `vif1_tops = vif1_top`, `vif1_itops = vif1_itop` (the unit holds two words); XITOP reads ITOPS per FACTS §11.6 — the interp must read `vif_itop()` for both until the VIF hands distinct values (5E). | Part 3 / 5E decide. |
| P5-5B-FILENAME-LINT (cont.) | `engine/vu/` | `xgkick` → `xg_kick` (snake_case of `XgKick`); test rig struct named `TestXgKick` to satisfy the lint in `test_xg_kick.cpp`. | Renamed; README maps it. |

### HANDOFF (5B part 2 → 5B part 3)

**Done:** `engine/vu/{flag_pipeline, stall_model(+.inl), xg_kick, vu_unit}`, CMake rows, `tests/vu/{VuFlagPipeline,
VuLazyFlagsAlgebra (PART 1), VuStallModel, VuXgKick, VuUnitSurface}`, README rows + notes. Tree GREEN (99/99, both
builds). No commits (the user commits).

**Not done (part 3 — FACTS §16.1 steps 11-13 + the ported tests):**
- `engine/vu/tiers/interp_tier.h/.cpp` — `VuInterpreterTier` (FACTS §11; `std::fmaf` for the MADD/MSUB/MADDA/
  MSUBA/OPMSUB lanes per P5-FP-CONTRACT). Drive the unit through `VuUnit::{state(), regs(), flags(), stalls(),
  xgkick(), vif_top(), vif_itop()}`: per bundle `VuDecoder::DecodeVuSlot(lower, upper)` → `stalls().Apply(slot,
  regs, flags)` → (VU1) `xgkick()->Pace(state.data, flags.cycle())` → ops; upper flags via
  `flags().UpdateMACFlags(regs, raw_result, dest)`; CLIP via `PushClip((clip_latest() << 6) | nf)`; FS*/FM* via
  `RetireMacStatus`, FC* via `VisibleClip`, FCSET via `SetClipImmediate`, FCGET no MAC retire; Q via
  `VisibleQ/PushQ(…, kDivDelay|kSqrtDelay|kRsqrtDelay, status)`, WAITQ `FlushQPipe`; P via `VisibleP/PushP(…,
  kEfu*)`; program end / XTOP yield: `FlushClipPipe, FlushMacPipe, FlushQPipe, stalls().FlushFmacWb(flags),
  FlushIaluWb, FlushPPipe, xgkick()->Flush(data, cycle, false)`; XGKICK: `xtop_read_count = 0; xgkick_count++;
  xgkick_has_fired = true; xgkick()->Arm(data, VI[is], cycle)`. `cycle_cap` ctor arg (default 1'000'000),
  `bundles()` counter. `VuUnit::Execute` is NOT to be added by part 3 either (5C `VuTierDispatch`); the tests
  call the tier directly.
- `engine/vu/vu0/libvu0_sincos.h/.cpp` — `LibVu0SinCos` (FACTS §12), invoked by the interp for VU0 at pc 0xF0.
- Tests: `Vu1TransformGroundTruth` (FACTS §14.1), `Vu0ShadowReplay`, `Vu0VisMicro`, `Vu0DecalEval` (§14.3-14.5,
  interp legs only; cached legs → 5C) with the `.inc` → `tests/vu/fixtures/*.bin` lift (§14.6, lifter under
  `/tmp/p5/5B/`), `VuLazyFlagsAlgebra` PART 2 (program level, interp only — extend `test_lazyflags_algebra.cpp`).
- 5B close-out section + HANDOFF to 5C (cached legs of the three VU0 tests + lazyflags PART 2 cached leg,
  `ApplyFmacPrunePush`, `VuCachedProgram`, `VuTierDispatch` + `VuUnit::Execute`, `VuMacPreRetireDue`).

**Integration list (for 5H):** `XgKick::paced_drain` ← `TitleProfile.quirks.xgkick_defer` (ctor bool through
`VuUnit`); `VuUnit::SetVifTop` ← VIF1 TOP/ITOP; `VuUnit::BumpMicroEpoch` ← the EE VU memory window store path;
`ResumeIfWaiting` (interp, part 3) ← CTC2. No Config keys, no MMIO, no `<Module>Settings` yet.

**Gate status:** GREEN — `100% tests passed, 0 tests failed out of 99`; `LintOneClass: 359 files scanned, 0
findings -> PASS`; `LintEnginePurity: 309 files scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; build 0
warnings (dev + SIMD twin).

**Next command (part 3):** `cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset
macos-dev` (expect 99/99), then write `engine/vu/tiers/interp_tier.{h,cpp}` against FACTS §11 over the `VuUnit`
surface above, add `tiers/interp_tier.cpp` (and `vu0/libvu0_sincos.cpp`) to `engine/vu/CMakeLists.txt`, and land
`Vu1TransformGroundTruth` first (the fmaf discriminator).

### 5B part 3 — tiers/vu_interpreter_tier / vu0/lib_vu0_sin_cos / ported tests + 5B close-out (2026-09-27, ≈42 min)

Written against `/tmp/p5/5B/FACTS.md` (§11, §12, §14, §16, §18; §2/§4/§5/§7/§8 tables as reference) and PHASE5_PLAN
§2.3 row `interp_tier`, §7 row 5B, §7.1 P5-LIBVU0-ENGINE / P5-FP-CONTRACT / P5-NEON-DEFAULT. No `/tmp/p5/5B/REVIEW.md`
existed at start (nothing to apply). OLD was opened READ-ONLY for the test constant tables only (`tests/test_vu0_shadow_replay.cpp:73-113`,
`test_vu0_vis_micro.cpp:86-147`, `test_vu0_decal_eval.cpp:55-70`) and its five test binaries were run bounded as oracles
(cwd `/tmp/p5/5B/`). Tree GREEN at the stop (105/105, both lints 0, getenv 1, build 0 warnings; SIMD twin 15/15, 0 warnings).

### Landed (files)
| File | Class / content | OLD source (read-only) |
|---|---|---|
| `engine/vu/tiers/vu_interpreter_tier.h` (73) + `.cpp` (197) | `VuInterpreterTier` — `RunOptions{cycle_limit = 1'000'000}`, `Run(VuUnit&, start_pc[, opt])`, `ResumeIfWaiting`, `bundles()`, `cycle_cap()`; the bundle loop: `VuChopRoundGuard` first, `LibVu0SinCos::TryExecute` at VU0 entry, VF0 pin, `pc + 8 > mem_mask + 1` size check, `++cycle`, LE fetch, 4-zero-bundle stop, I/E bits, `VuDecoder::DecodeVuSlot` → `StallModel::Apply` → `XgKick::Pace`, `hide_upper` snapshot of `VF[upper_vf_write]` only, `discard_lower`, `I = lower` after the upper, XTOP yield return, VF0/VI0 re-pin, delay-slot redirect (no TPC), `E` → `TPC = (pc+8) & 0x3FFF` at the delay slot, VU0 self-spin park (`br_op 0x28..0x2F`, VI-pair key, count ≥ 2 → `suspended/suspended_tpc/TPC`), normal advance `TPC = pc`; `DrainAll` = FlushClip/Mac/Q + FlushFmacWb/IaluWb + FlushP + `XgKick::Flush(false)` | `host_app/vu.cpp:5203-5210, :5300-5301, :5365, :5737-5760, :5990-6001, :6774-6797, :6825-6854, :7153-7266, :7492-7500, :7773-7921, :8095-8099` |
| `engine/vu/tiers/vu_interpreter_tier_upper.cpp` (327) | `RunUpper` prologue (clamped `src1/src2` copies, `ClampStoreACC` in place, `protect_fd`) + every func 0x00-0x2F (ADD/SUB/MADD/MSUB/MAX/MINI/MUL bc, q/i forms, ADD/MADD/MUL/MAX/SUB/MSUB/OPMSUB/MINI; 0x30-0x3B no-op); `RunSpecial2Upper` (ADDA/SUBA/MADDA/MSUBA/MULA bc, ITOF0/4/12/15, FTOI0/4/12/15, ABS bc≤1, MULAi, CLIP, MADDAq/ADDAi/MADDAi, MSUBAq/SUBAi/MSUBAi, ADDA/MADDA/MULA/0x0A-3 flag-only tail, SUBA/MSUBA/OPMULA/NOP, legacy 0x18-0x1F MOVE/MR32/DIV/SQRT/RSQRT/WAITQ); `RunClip` (int-compare judgement, `PushClip((clip_latest << 6) \| nf)`); `DoDiv/DoSqrt/DoRsqrt` (status D/DS, I/IS; `PushQ` at 7/7/13). MADD family = `std::fmaf(a, b, acc)` / `fmaf(-a, b, acc)`; flags on the RAW result via `UpdateMACFlags`; `WriteMaskClamp` | `host_app/vu.cpp:8104-8151, :8153-8617, :8618-9475, :8992-9031, :10929-10990` |
| `engine/vu/tiers/vu_interpreter_tier_lower.cpp` (412) | `RunLower` (LQ/SQ RMW, ILW first-lane priority, ISW every lane, IADDIU/ISUBIU + alt 0x23/0x27, FCEQ/FCSET/FCAND/FCOR → VI1, FSEQ/FSSET/FSAND/FSOR/FMEQ/FMAND/FMOR with `RetireMacStatus`, FCGET without, B/BAL/JR/JALR (target before link), IBEQ/IBNE/IBLTZ/IBGTZ/IBLEZ/IBGEZ, 0x22 IADDI primary); `RunLowerSpecial` (IADD/ISUB/IADDI/IAND/IOR; MOVE/MR32, LQI/SQI/LQD/SQD, DIV/SQRT/RSQRT/WAITQ, MTIR/MFIR/ILWR/ISWR, RNEXT/RGET/RINIT/RXOR, MFP, XTOP + yield count, XITOP ← ITOPS, XGKICK `Arm` + scratch); `RunEfu` (ESADD/ERSADD/ELENG/ERLENG with the fused `fmaf(z,z,fmaf(x,x,y*y))` chain, EATANxy/xz, ESUM, ESQRT/ERSQRT/ERCPR, WAITP no-op, ESIN/EATAN/EEXP-clamped; `PushP` with the 13 latencies) | `host_app/vu.cpp:9476-11314`; link mask `vu_fast_exec.h:111-117` |
| `engine/vu/vu0/lib_vu0_sin_cos.h` (32) + `.cpp` (47) | `LibVu0SinCos` — `kEntryPc 0xF0`, `kExitTpc 0x1C0`, `kFingerprint[3][2]` (the three SDK words), `Matches(micro, size, pc)`, `TryExecute(micro, size, regs, pc)` (clamped VF20.x/z → `std::sin/std::cos` → clamped VF31.xyzw, VF0/VI0 pins, TPC; nothing else touched) | `host_app/vu.cpp:5540-5607` (≡ `:2570-2600`) |
| `engine/vu/vu_unit.h/.cpp` (edit) | ctor gains `bool lazy_flags = FlagPipeline::kDefaultLazy` (4th, defaulted) so the algebra test drives both rings through the interpreter; `LoadCase` feeds `vif_itop_` from `vif1_itops` (XITOP reads the ITOPS shadow — FACTS §11.6 / P5-5B-XGK-TOPS resolved) | — |
| `engine/vu/CMakeLists.txt` | +4 sources (`vu0/lib_vu0_sin_cos.cpp`, `tiers/vu_interpreter_tier{,_upper,_lower}.cpp`); header comment | — |
| `engine/vu/README.md` | +3 Files rows (interp tier, sincos, vu_unit note), file-name mapping note, "Interpreter notes" section | — |
| `tests/vu/fixtures/vu0_{shadow,vis,decal}_{code,data}.bin` (6 × 4096 B) | lifted by `/tmp/p5/5B/lift_fixtures.py` from the three OLD `.inc` (two 4096-byte arrays each; shadow/vis hex, decal decimal); shadow code head `ff 0f 00 5a ff 02 00 20 3c 03 00 80 ff 02 00 00` as FACTS §14.6 predicts; the three code images are byte-identical (`sha256 4242fb88…`) | `OLD/tests/vu0_shadow_image.inc`, `vu0_vis_image.inc`, `vu0_decal_eval_image.inc` |
| `tests/vu/CMakeLists.txt` | +6 rows, `PS2R_VU_FIXTURES_DIR` compile definition for the three fixture tests, `VuLazyFlagsAlgebra` args `250 120` | — |

File names follow the lint (`snake_case(primary type)`): the plan's `tiers/interp_tier` and `vu0/libvu0_sincos` are
`tiers/vu_interpreter_tier` and `vu0/lib_vu0_sin_cos` (same precedent as `xg_kick`; README maps them). The interpreter's
upper/lower bodies are two extra `.cpp` files of the SAME class (zero primary types per TU → lint-clean, verified).

### Flags applied
| Flag | Outcome |
|---|---|
| `PS2_VU1_CYCLE_CAP` | TEST → `VuInterpreterTier(cycle_cap)` ctor / `RunOptions.cycle_limit` (default 1,000,000; the 5C tool's `--cycle-cap`); no getenv |
| `PS2_VU1_LINK11` | NORM → `kLinkMask = 0x7FF` in BAL/JALR (the interp's two writers) |
| `PS2_VUTRACE` family, `PS2_VU0_TRACE`, `trace_run`, `vcallms_probe` | DIAG → omitted (no trace sink this phase; `RunOptions` has no sink field — added when a diag consumer exists) |
| `PS2_S13_*`, `PS2_S15_*`, `PS2_S26_*`, `PS2_S8_*`, `PS2_W15_KICKCENSUS`, `[CGATE]/[SKYVTX]/[R70-XFRM]/[VUT]` taps | DELETE → not written (the probe forest `vu.cpp:4334-4999, :5027-5195` and the in-loop taps) |
| `PS2_VU_NOCLAMP`, `PS2_VU_CLAMPELIDE` | DELETE → not written (already absent from `VuFloat`) |
| `xgkick_active/xgkick_addr`, `last_xtop_value`, `branch_pending`, `m/d/t` bits, census counters | not carried (FACTS §16.5) |

### Tests added (ctest names)
```
100/105 Test #100: Vu1TransformGroundTruth .............   Passed    0.23 sec
101/105 Test #101: Vu0ShadowReplay .....................   Passed    0.15 sec
102/105 Test #102: Vu0VisMicro .........................   Passed    0.21 sec
103/105 Test #103: Vu0DecalEval ........................   Passed    0.21 sec
104/105 Test #104: VuInterpSmoke .......................   Passed    0.22 sec
105/105 Test #105: LibVu0SinCosPin .....................   Passed    0.21 sec
100% tests passed, 0 tests failed out of 105
```
- `Vu1TransformGroundTruth` (`test_vu1_transform_gt.cpp`): variants A (VF9 = `48970746 48825220 49cbb259 410105e0`, TPC 0x30)
  and B (Q `3dfdf867`, VF9.xyz `4715d4bb 4701499d 484a14e8`, VF7.xyz `38356 33097 206931`) — the `std::fmaf` + FE_TOWARDZERO
  discriminator; PASS on first run.
- `Vu0ShadowReplay`: entries 0xA08/0xB90 × 5 variants fresh (E-bit termination, VI02 varies: v1 = 0x00a0, others 0), the 12-step
  persistent sequence reproduces the fresh verdicts, captured PASS vector VI02 == 0. GT dump IDENTICAL to the OLD binary
  (`/tmp/p5/5B/old_test_vu0_shadow_replay.log` vs `p3_test_vu0_shadow_replay.log`): `vf26 = 467ef4e1 46e5a408 c5c602b8 c52ca00e …
  Q=3c1f8758 mac=0030 status=00c2 vi02=0000`.
- `Vu0VisMicro`: leg 1 (E injected at 0x210) VI1 0x11, VI2 0x1 (the FTOI0 → MTIR stall re-timing), VF1.w/VF28.w/VF29-31 retail,
  TPC 0x220; leg 2 VF28-31 retail rows, `VF31.w & 0xFFFF == 0xF0`. `Vu0DecalEval`: VF28/VF30/VF31 + Q `3eceb044` retail.
- `VuInterpSmoke` (`test_vu_interp_smoke.cpp`): E-bit + TPC (= E addr + 16), branch delay slot + ignored delay-slot branch, I-bit
  ordering (upper sees the old I) + VF0/VI0 pins, `hide_upper` / `discard_lower`, NOP sled (4 bundles, TPC advanced by 3), cycle cap
  (100 bundles on a self-loop, VU1 never parks), XTOP yield (3073 bundles, TPC = last normal advance), VU0 self-spin park after the
  second issue + `ResumeIfWaiting` completes the program, non-self IBEQ never parks, VU0 4 KB edge (TPC 0x1000), XGKICK from a
  microprogram → `GifArbiter → GifParser → RegisterOpStream` (Tag + 2 A+D + Eop = 4 ops), and corpus `vu1_case_0000_1748.bin`
  end to end: `start_pc=1748 image=a11b3516d81359c3 bundles=237 cycles=253 kicks=1 packets=1 ops=38 ophash=660c3ec569fd9474 TPC=1980`
  (SKIP 77 when the OLD corpus is absent). No post-state in the record → the OLD-vs-NEW compare is the 5C tool's `--compare`.
- `LibVu0SinCosPin` (`test_lib_vu0_sin_cos.cpp`): fingerprint match + substitution (`sincos(0.5, -2.0) = 3ef57743 3f60a940 bf68c7b7 bed51132`,
  TPC 0x1C0, pins, no clock/bundle change), input clamping (denormal → -0, Inf → MAX), no-fingerprint / other-entry / short-image /
  VU1 fall-through.
- `VuLazyFlagsAlgebra` PART 2 (extended `test_lazyflags_algebra.cpp`, args `250 120`): 120 random straight-line programs
  (FACTS §14.2 generator) through the interpreter on an eager and a lazy `VuUnit` → `memcmp(VUregs) == 0`, identical 16 KB data,
  equal clock, empty ring, every consumer ISW landed: `part 2: 120 programs, 4219 bundles, eager == lazy through the interpreter`.

### Oracle / gate status
```
$ cmake --preset macos-dev && cmake --build --preset macos-dev -j4        -> exit 0, warnings 0
$ ctest --preset macos-dev                                                -> 100% tests passed, 0 tests failed out of 105
$ python3 tests/lint/lint_one_class.py --root .                           -> LintOneClass: 371 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root .                       -> LintEnginePurity: 315 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py                                -> getenv sites: 1 -> PASS
$ verify/run_bounded.sh 900 /tmp/p5/5B/p3_simd_runner.log -- /tmp/p5/5B/p3_simd.sh   (cmake -DPS2R_SIMD=ON -B /tmp/p5/5B/build-simd, build, ctest -R 'Vu|Lib')
    -> runner exit 0, warnings 0, "100% tests passed, 0 tests failed out of 15" (all six new suites PASS with the NEON lazy ring)
$ verify/run_bounded.sh 120 /tmp/p5/5B/old_<t>.log -- OLD/build/tests/<t>   for test_vu0_shadow_replay / vis_micro / decal_eval / vu1_transform_gt   -> exit 0 each; shadow GT dump identical (above)
```
PLAN §7 row 5B green criterion: `VuLazyFlagsAlgebra` (lazy ≡ eager, parts 1+2), `Vu1TransformGroundTruth`, `Vu0ShadowReplay`, `Vu0VisMicro`,
`Vu0DecalEval` green ✓; `sizeof(VuDecodedSlot)==60` / `sizeof(Vu1Case)==33456` static_asserts ✓ (part 1); `VuImageHash::Fast` of
`vu1_case_0000_1748.bin` == the OLD catalog row ✓ (part 1, `Vu1CaseRoundTrip`); lints 0 ✓. **5B gate MET.** P5-FP-CONTRACT resolved for the
interp tier: every MADD/MSUB/MADDA/MSUBA/OPMSUB lane and the EFU sum of squares are explicit `std::fmaf`, so `-ffp-contract` cannot move
a result (the transform ground truth proves the fused form is the retail one). P5-NEON-DEFAULT: the retail VU0 ground-truth tests pass
under BOTH rings (dev = eager scalar, twin = lazy NEON) — R-LAZY discharged on real programs, not only on the algebra.

### Findings (→ plan §7.1)
| Id | Where | Finding | Disposition |
|---|---|---|---|
| P5-5B-FILENAME-LINT (cont.) | `engine/vu/tiers`, `engine/vu/vu0` | `interp_tier` → `vu_interpreter_tier`, `libvu0_sincos` → `lib_vu0_sin_cos` (snake_case of the plan-fixed class names); one class across three `.cpp` (upper/lower bodies TU-private) is lint-clean. | README maps the plan names. |
| P5-5B-SINCOS-PAIRING | `lib_vu0_sin_cos.cpp` | `std::sin(a); std::cos(a)` back to back is paired by clang into one `sincosf`; its huge-argument reduction differs in the last bit from a lone `cosf` (seen at MAX_FLOAT: `3f5a5f96` paired vs a different lone value). OLD has the same shape (`vu.cpp:5560-5563`), so the pairing IS the reference behaviour; a constant-folded `std::sin(0.5f)` (round-to-nearest) also differs from the runtime FE_TOWARDZERO call. | Kept as written; the pin test mirrors the call shape and uses volatile sources; recorded as an R-EFU sub-risk (host libm + compiler pairing). |
| P5-5B-XITOP-ITOPS | `vu_unit.cpp` | `LoadCase` fed `vif_itop_` from `vif1_itop`; XITOP reads ITOPS (FACTS §11.6 KEEP-DIVERGENCE) → replay would diverge on cases where the two differ. | Fixed: `LoadCase` reads `vif1_itops`; `SaveCase` still writes both words from the one value (the VIF hands distinct values in 5E). |
| P5-5B-DISCARD-LOWER-SIDE-EFFECTS | `vu_interpreter_tier.cpp` | `discard_lower` skips the WHOLE lower op (LQI's `VI++`, a load's address side effects) — hardware would still bump VI. | Replicated (pinned in `VuInterpSmoke`); BUG-KB candidate. |
| P5-5B-DELAYSLOT-CONTROL | `vu_interpreter_tier.cpp` | A branch or E bit inside a delay slot is ignored; an E bit on a taken-branch bundle wins over the branch; TPC is not set on a redirect. | Replicated (pinned); BUG-KB candidates (with R-VU0-SIZE, NOP sled, XTOP yield). |
| P5-5B-NO-TRACE-SINK | `vu_interpreter_tier.h` | The brief's "trace sink optional" `RunOptions` field is not written: `PS2_VUTRACE` is DIAG and no diag consumer exists this phase. | `RunOptions{cycle_limit}` only; a sink lands with `core/diag` wiring (Phase 9 writer). |
| P5-5B-STALE-TWIN-RACE | `/tmp/p5/5B/build-simd` | A SIMD twin build started while `VuUnit`'s ctor changed failed to link (stale `.a`); re-run on the stable tree: 15/15. | Process note: run the twin AFTER the last edit. |

### HANDOFF (5B → 5C)

**Done (5B complete, parts 1-3):** `engine/vu/{vu1_state.h, vu_chop_round_guard, vu_float(+.inl), vu_image_hash, vu1_case,
vu_decoder, span_classifier, flag_pipeline, stall_model(+.inl), xg_kick, vu_unit, tiers/vu_interpreter_tier(+_upper,+_lower),
vu0/lib_vu0_sin_cos}`, `ps2r_vu` (-O2 -fno-strict-aliasing -fwrapv, `PS2R_SIMD` lane), `tests/vu/` 14 suites (VuDecodeTable,
VuFloatRules, Vu1CaseRoundTrip, VuFlagPipeline, VuLazyFlagsAlgebra parts 1+2, VuStallModel, VuXgKick, VuUnitSurface,
Vu1TransformGroundTruth, Vu0ShadowReplay, Vu0VisMicro, Vu0DecalEval, VuInterpSmoke, LibVu0SinCosPin) + 6 fixture `.bin`,
README. Tree GREEN (105/105; twin 15/15). No commits (the user commits).

**Not done (moved to 5C):**
- `tiers/cached_tier` (`VuCachedProgram`, `ApplyFmacPrunePush` class-16 body, `VuMacPreRetireDue`, superblock consumers of
  `SpanClassifier` + `StallModel::ApplyClassed/ApplySpec<M>`), `tiers/tier_dispatch` (`VuTierDispatch`, `VuTierMode {Cached, Interp}`)
  and `VuUnit::Execute(start_pc)` (PLAN §3.3) — the interp is reached today only through `VuInterpreterTier::Run(unit, pc)`.
- The cached legs of `Vu0ShadowReplay` / `Vu0VisMicro` / `Vu0DecalEval` (interp == cached over VI/VF/Q/mac/status/TPC/cycle) and
  `VuLazyFlagsAlgebra` PART 2's cached + RUNRETIRE-off legs (the `mode` loop is 1 today).
- `tools/vu1_replay` (`--compare` interp vs cached vs OLD on the 126 cases; `--catalog` with `exec_bundles` from `bundles()`,
  `kicks` from `xgkick_count`; `--cycle-cap`), `Vu1ReplaySelfTest`, `Vu1CachedCompare_<case>` rows.
- `RunOptions` trace sink (DIAG; with the diag consumer).

**Integration list (for 5H):** as part 2 (`XgKick::paced_drain` ← `TitleProfile.quirks.xgkick_defer`; `VuUnit::SetVifTop` ← VIF1
TOP/ITOPS — feed ITOPS to the second word; `BumpMicroEpoch` ← the EE VU memory window) + `VuInterpreterTier::ResumeIfWaiting` ←
CTC2-to-VI (VU0), `Run` ← CMSAR0/VCALLMS (VU0) and VIF1 MSCAL/MSCNT/MSCALF (VU1, through `VuTierDispatch` once 5C lands),
`VuUnit(id, arbiter, paced, lazy = kDefaultLazy)`. No Config keys, no MMIO, no `<Module>Settings`.

**Gate status:** GREEN — `100% tests passed, 0 tests failed out of 105`; `LintOneClass: 371 files scanned, 0 findings -> PASS`;
`LintEnginePurity: 315 files scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; build 0 warnings (dev + SIMD twin 15/15).
PLAN §7 5B green criterion met (above).

**Next command (5C):** `cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect
105/105), then write `engine/vu/tiers/vu_cached_tier.{h,cpp}` (+ `vu_cached_program.h` POD) against the 5C FACTS over
`VuDecoder`/`SpanClassifier`/`StallModel::ApplyClassed`/`FlagPipeline`/`XgKick`, then `vu_tier_dispatch` + `VuUnit::Execute`, then
re-enable the cached legs of the three VU0 tests and lazyflags PART 2 (`mode` loop = 2), then `tools/vu1_replay --compare`.

## 5C — cached tier + tier dispatch + vu1_replay (2026-09-28; part 1 ≈40 min)

### 5C part 1 — tiers/vu_cached_tier / tiers/vu_tier_dispatch / VuUnit::Execute + the two differential suites (2026-09-28, ≈40 min)

Written against `/tmp/p5/5C/FACTS.md` (§0, §A, §B, §E, §F) and PHASE5_PLAN §2.3 rows `cached_tier` / `tier_dispatch`, §3.3,
§7 row 5C. OLD was not opened (the FACTS transcription carried every anchor); no OLD binary was run in this part (the tool and
the `compare_vs_old.py` driver are part 2). Tree GREEN at the stop (107/107, both lints 0, getenv 1, build 0 warnings;
SIMD twin: `VuCachedSuperblock` + `VuCachedVsInterpCorpus` 126/126 identical, 0 warnings).

### Landed (files)
| File | Class / content | OLD source (read-only) |
|---|---|---|
| `engine/vu/tiers/vu_cached_tier.h` (80) + `.cpp` (375) | `VuCachedProgram` POD (`hash, generation, exec_count, bundles, slots, span_rem`; AOT/census fields not written) and `VuCachedTier(VuInterpreterTier& generic, cycle_cap)`: `Acquire`/`Lookup` (epoch fast path → `VuImageHash::Fast` over `mem_mask+1` bytes → `current_` then table scan → min-`exec_count` eviction at `kTableCap = 64`, first minimum wins, `current_` cleared if evicted → decode), `DecodeProgram` (`DecodeVuSlot` over `(mem_mask+1)/8` slots, then `ComputeRunLenTouchHw / ComputeStallClasses / ComputeFoldClasses / ComputeSpanRem` in that order; `static_assert` latency pin), `Execute(unit, pc[, cycle_limit])` (round guard, `LibVu0SinCos` at VU0 entry, Acquire at EVERY entry, VF0 pin + the five scratch resets, `prog.bundles` delta), `ResumeIfWaiting`, `Loop`: `pc + 8 > code_size` stop, `++cycle`/`++bundles_` per bundle, predecoded slot or fly-decode of a misaligned pc, the RUN body (`!in_delay_slot && !end_pending && run_len >= 3`; `remaining = run_len - 1`; `fold_pos` warmup 4 → `ApplyClassed`, fold 16 → `ApplyPrunePush`, fold 15 → nothing; span `>= 4` past the warmup executes without the model, `pace()` once per bundle, `touch_hw` re-pin, unmasked `pc += 8`, `MaterializeFmac(slots, pc >> 3, span_executed, cycle)` at the close, the replay close (`ApplyPrunePush` per slot at `saved + 1 - k`) when the cap cuts a span with `< 4` executed, `TPC = pc` + break on every cap cut, `pc &= 0x3FFF; TPC = pc` at the run end), then the per-bundle path (NOP sled, `ApplyClassed`, pace, `ExecBundle`, XTOP yield, pins, delay slot / E bit / VU0 self-spin park / advance) and the shared `DrainAll` | `vu.h:218-252, :503-560`; `vu.cpp:2228-2266, :2268, :2326, :2394-2397, :2462-2563, :2566-2665, :2696-2721, :2801-2818, :2834-2846, :2848-2866, :2905, :3070-3079, :3126-3134, :3346-3495, :3503-3521, :3523-3561, :3578-3600, :3603-3675, :3676-3877` |
| `engine/vu/tiers/vu_cached_tier_exec.cpp` (338) | `ExecBundle` (the interpreter's `hide_upper` snapshot / `discard_lower` / `I = lower` after the upper), `ExecUpperFast` (NOP; ADD/SUB/MUL/MADD/MSUB bc; ADD/SUB/MUL/MADD/MSUB; MULq; MULi/ADDi; MAXbc/MINIbc/MINIi/MAX; MULAbc/MADDAbc/MSUBAbc/MULA/MADDA with the ACC tail; FTOI0/FTOI4; ITOF0; CLIP → `RunClip`; default → `RunUpper`) — every body is the interpreter's expression over the same kernels (`Clamp4`, `ClampStoreACC`, `WriteMaskClamp`, `UpdateMACFlags`, `std::fmaf`, `ApplyMinMaxRaw`, `FtoiSat`); `ExecLowerFast` (NOP, LQ, SQ, ILW, ISW, IADDIU, ISUBIU, IBEQ, IBNE, IADD, ISUB, IAND, IOR, IADDI, MOVE, MR32, MTIR, MFIR, LQI, SQI, RGET, RINIT, B, BAL (link `kLinkMask 0x7FF`), JR, JALR (target before link), DIV → `DoDiv` (`ftf` from the raw word), FCGET, WAITQ; default → `RunLower`) | `vu_fast_exec.h:111-117, :663-1426, :1442-1790` (EL / NEON twins, `kVuClampElide`, the `[OCEAN-ISO]` probe, census counters and `VuMacPreRetireDue :318-334` not written) |
| `engine/vu/tiers/vu_tier_dispatch.h` (70) + `.cpp` (41) | `VuTierMode {Cached, Interp}`, `VuSettings {vu1_mode = Cached, hle_enabled = false}`, `VuTierDispatch(settings, cycle_cap = 1'000'000)`: `Execute` ladder (VU1 only: `hle_enabled && hle_ && hle_(...)` → return; `mtvu_ && mtvu_(...)` → return; then `vu1_mode` → `cached_.Execute` / `interp_.Run`), `ResumeIfWaiting` through the ladder, `SetHleHook/SetMtvuHook` (`bool(*)(void*, VuUnit&, uint32_t[, uint64_t micro_generation])`, null = absent), `set_cycle_cap` for both tiers, `bundles()` = interp + cached | `vu.h:30, :503-515`; `vu.cpp:364-407, :456-466, :4318-4332, :4808-4816, :5000-5008, :8095-8099` |
| `engine/vu/stall_model.h/.cpp` (edit) | `ApplyPrunePush(s, pipes)` (steps 1 + 7: prune when `n && min_retire <= cycle`, push each `wr_reg[w]` at `cycle + 4`) and `MaterializeFmac(slots, close_idx, executed, cycle)` (ring reset; `kmax = min(executed, 3)`; for `k = kmax..1` push the writes of `slots[close_idx - k]` with `retire = cycle + 1 - k + 4`, `regs |=`, `min_retire`; IALU ring untouched) | `vu.cpp:616-663, :2801-2818` |
| `engine/vu/tiers/vu_interpreter_tier.h` (edit) | `friend class VuCachedTier;` (the GENERIC fallbacks `RunUpper/RunLower/RunClip/DoDiv` and `DrainAll` stay private to everyone else) | — |
| `engine/vu/vu_unit.h/.cpp` (edit) | owns `std::unique_ptr<VuTierDispatch> dispatch_` (forward-declared; out-of-line `~VuUnit`); `Execute(start_pc)`, `ResumeIfWaiting()`, `dispatch()`; header comment updated | `vu.cpp:4320-4332` |
| `engine/vu/CMakeLists.txt` | +3 sources (`tiers/vu_cached_tier.cpp`, `tiers/vu_cached_tier_exec.cpp`, `tiers/vu_tier_dispatch.cpp`); header comment | — |
| `tests/vu/test_vu_cached_tier.cpp` (626) | `VuCachedSuperblock` — FACTS §E ports (winner arm): straightline64 (run_len 65/2/1 pins), xtop_mid_run, ebit_boundary, ibit_in_run, stallclass_matrix (+ the 14-class recompute (i)), pace_fire_in_run (quantum-32 adaptation: 48-bundle run after the kick), stallfold_window_k3/k4, stallfold_entry, stallfold_class16, spanfold_basic (+ the no-read variant: `cycle == noread + 2`, the materialised stall LANDS), spanfold_short, spanfold_interrupted, spanfold_backstop (caps 4999/5000/5003 + caps 1..12 — the materialise AND replay closes), spanfold_ialu (af1/af2 merged), spanfold_pace, `CheckSpanRem` recompute; plus vu0_park + `ResumeIfWaiting` through the ladder on both tiers, the table (epoch fast path, re-write → hash hit, 64-cap eviction keeps the 4-count image), the dispatch ladder (gate off / declined / taken / VU0 never enters / per-tier `bundles()`) | `tests/test_vu_superblock.cpp:28-170, :340-470, :483-600, :678-800, :1165-1570` |
| `tests/vu/test_vu_cached_vs_interp.cpp` (202) | `VuCachedVsInterpCorpus` — every `*.bin` under `${PS2R_OG_ROOT}/vutrace_cases2` through cached then interp on fresh units → identical 608-byte `VUregs`, 16 KB data, `cycle`, `bundles`, `xgkick_count`, `RegisterOpStream` count + hash (SKIP 77 when absent); the three VU0 fixture images (`vu0_shadow@0A08/0B90`, `vu0_vis@1C0`, `vu0_decal@1C0`) through both tiers the same way | (NEW-only; replaces the "cached legs" of the three VU0 suites + lazyflags PART 2 named in the 5B HANDOFF) |
| `tests/vu/CMakeLists.txt` | +2 rows (`VuCachedSuperblock`, `VuCachedVsInterpCorpus` with `${PS2R_OG_ROOT}/vutrace_cases2 ${PS2R_VU_FIXTURES_DIR}`, TIMEOUT 600) | — |
| `engine/vu/README.md`, `engine/vu/tiers/README.md` | +2 Files rows, `stall_model` / `vu_unit` rows updated, "Cached tier notes" section; tiers README Belongs/Transplanted rows | — |

### Flags applied
- `PS2_VU_HASHEPOCH` → NORM ON: the epoch fast path in `Lookup` (no hash / no scan when `generation == micro_generation`).
- `PS2_VU_SUPERBLOCK` → NORM ON: the run body written; entry `run_len >= 3`, no gate.
- `PS2_VU_STALLSPEC` → NORM ON: `ComputeStallClasses` in `DecodeProgram`; `ApplyClassed` on every per-bundle slot.
- `PS2_VU_STALLFOLD` → NORM ON: `ComputeFoldClasses`; fold-15/16 dispatch past the warmup; `ApplyPrunePush`.
- `PS2_VU_SPANFOLD` → NORM ON: `ComputeSpanRem`; the `>= 4` span body + `MaterializeFmac` / replay closes.
- `PS2_VU_RUNPACE` → NORM ON: the slim in-run pace (`armed && cycle >= next_due` → `XgKick::Pace`); the per-bundle legacy arm not written.
- `PS2_VU_PACE_NEXTDUE` → NORM ON (5B's `XgKick::next_due()` consumed by the run-pace guard).
- `PS2_VU1_LINK11` → NORM: `kLinkMask 0x7FF` reused from the interpreter in the BAL/JALR fast bodies.
- `PS2_VU_FASTPACK` → NORM: `VuImageHash::Fast` is the only key.
- `PS2_VU_MODE` → SETTING: `VuSettings::vu1_mode = Cached` (Config key `engine.vu1_mode` copied at integration; `Native` and the vutrace veto not written).
- `PS2_VU1_HLE` → SETTING field `VuSettings::hle_enabled = false` this stage (5D installs the hook and flips the default per PLAN §3.3).
- `PS2_VU1_CYCLE_CAP` → TEST-ONLY: `VuTierDispatch::set_cycle_cap` / the cached `Execute(unit, pc, cycle_limit)` overload (the tool's `--cycle-cap`, part 2).
- `PS2_VU_RUNRETIRE` → NORM ON: no pre-retire in the fast bodies (`UpdateMACFlags` exactly as the interpreter — FACTS §A.7).
- `PS2_VU_SPANFOLD2`, `PS2_VU_RUNFLAT`, `PS2_VU_RUNFLAT_CENSUS`, the VU-AOT family, `PS2_VU0_MODE`/`PS2_VU1_MODE`, `PS2_VUTRACE` veto, `[VUCACHE]`/`[VU-MODE]` logs, census counters → not written.
- `PS2_VU_O3` (CMake) → not applied: `ps2r_vu` stays at the 5B `-O2 -fno-strict-aliasing -fwrapv` pin (bit-identity build; the corpus proves it).

### Tests added (ctest names)
```
106/107 Test #106: VuCachedSuperblock ..................   Passed    0.24 sec
107/107 Test #107: VuCachedVsInterpCorpus ..............   Passed    0.35 sec
100% tests passed, 0 tests failed out of 107
```
`VuCachedSuperblock` (every case `identical` = regs + data + cycle + bundles + kicked ops + suspended; excerpt):
```
  straightline64           bundles=66 cycles=66 kicks_ops=0 identical
  xtop_mid_run             bundles=13 cycles=15 kicks_ops=0 identical
  stallclass_matrix        bundles=28 cycles=62 kicks_ops=0 identical
  pace_fire_in_run         bundles=52 cycles=54 kicks_ops=4 identical
  spanfold_basic           bundles=23 cycles=28 kicks_ops=0 identical
  spanfold_basic_noread    bundles=23 cycles=26 kicks_ops=0 identical
  spanfold_backstop_0      bundles=4999 cycles=5003 kicks_ops=0 identical
  backstop_cap_1 .. backstop_cap_12  identical (the replay close at 1-3 executed, the materialise close from 4)
  spanfold_pace            bundles=44 cycles=46 kicks_ops=4 identical
  vu0_park                 bundles=9 cycles=10 kicks_ops=0 identical
VuCachedSuperblock: ALL PASS
```
`VuCachedVsInterpCorpus` (`/tmp/p5/5C/corpus.log`):
```
  VU0 vu0_shadow@0a08    bundles=49 cycles=58 TPC=0b90 identical
  VU0 vu0_shadow@0b90    bundles=36 cycles=54 TPC=0cb0 identical
  VU0 vu0_vis@01c0       bundles=52 cycles=74 TPC=0360 identical
  VU0 vu0_decal@01c0     bundles=52 cycles=74 TPC=0360 identical
COMPARE PASS: pc=0x1748 bundles=237 cycles=253 kicks=1 ops=38 ophash=660c3ec569fd9474 vu1_case_0000_1748.bin (cached vs interp)
COMPARE PASS: pc=0x0000 bundles=1000000 cycles=1722224 kicks=0 ops=0 ophash=cbf29ce484222325 vu1_case_w14bkstp_mat.bin (cached vs interp)
COMPARE PASS: pc=0x0000 bundles=1000000 cycles=1695659 kicks=0 ops=0 ophash=cbf29ce484222325 vu1_case_w14bkstp_rep.bin (cached vs interp)
VuCachedVsInterpCorpus: cases=126 identical=126 failures=0
```
(126 `COMPARE PASS` lines; the two 1,000,000-bundle synthetic images exercise the cap backstop's materialise and replay closes with identical clocks.)

### Oracle / gate status
```
$ cmake --build --preset macos-dev -j4        -> 0 warnings (-Wall -Wextra -Werror), exit 0
$ ctest --preset macos-dev                    -> 100% tests passed, 0 tests failed out of 107   (Total Test time (real) = 173.19 sec)
$ python3 tests/lint/lint_one_class.py --root .      -> LintOneClass: 378 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root .  -> LintEnginePurity: 320 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py           -> getenv sites: 1 -> PASS
$ cmake -S . -B /tmp/p5/5C/build-simd -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPS2R_SIMD=ON -DPS2R_DIAGNOSTICS=OFF && cmake --build ... --target test_vu_cached_tier test_vu_cached_vs_interp
  -> 0 warnings; VuCachedSuperblock: ALL PASS; VuCachedVsInterpCorpus: cases=126 identical=126 failures=0   (the lazy MAC ring)
```
One full-ctest run before the final one showed `EeRunTokenAndEventQueue` (Phase-4, `engine/ee`, not touched) timing out at 120 s with
`sh: /bin/ps: Operation not permitted` in its output; standalone and in the final full run it passes in 0.09 s — environmental, recorded as P5-5C-EE-FLAKE.
OLD-vs-NEW (`--compare` 126 vs `old_compare.log`, per-tier `cmp` of postdata) is part 2 (the tool) — not claimed here.

### Findings (→ plan §7.1)
- `P5-5C-STALLMODEL-SURFACE` (resolved): `StallModel::ApplyPrunePush` / `MaterializeFmac` added (FACTS §0.6 / §A.6.3 gap 1).
- `P5-5C-INTERP-FRIEND` (resolved): the GENERIC fallbacks reached through `friend class VuCachedTier` (gap 2); no public wrapper.
- `P5-5C-MATERIALIZE-K3` (recorded, OLD-faithful): `MaterializeFmac` rebuilds from the last 3 slots (OLD `kmax = min(executed, 3)`); a writer 4 back (retire `cycle + 1`) is not re-created. It is dead for every consumer (pruned at the next bundle) EXCEPT a program-end drain immediately after a cap cut inside a span whose last 3 bundles write nothing — then the drained clock could be 1 lower than the interpreter's. Not observed (both `w14bkstp_*` cap cases and the 15 backstop legs have identical clocks); left as OLD so the cached tier stays OLD-identical; revisit only if `--compare-cycle` ever flags it.
- `P5-5C-TESTS-MERGED`: the 5B HANDOFF's "cached legs of Vu0ShadowReplay / Vu0VisMicro / Vu0DecalEval + lazyflags PART 2 mode loop" are covered by ONE suite (`VuCachedVsInterpCorpus`: the three fixture images from their entries + 126 corpus cases, both MAC rings via the SIMD twin) instead of edits to four suites.
- `P5-5C-HLE-DEFAULT`: `VuSettings::hle_enabled = false` this stage (FACTS §B.1 says `true` = the `PS2_VU1_HLE` winner); 5D flips it when it installs `Vu1HleRuntime::TryExecute` — until then a `true` default would be a no-op (null hook) but would misstate the shipped configuration.
- `P5-5C-EE-FLAKE`: see Oracle / gate status.
- `P5-5C-SPLIT`: part 1 = tiers + dispatch + the two suites; part 2 = `tools/vu1_replay` (+ `XgkCaptureSink`, `Vu1ReplaySelfTest`, `Vu1CachedCompare_*` rows, `compare_vs_old.py`, `gsops_seed.txt`).

### HANDOFF (5C part 1 → 5C part 2)
**Done:** `engine/vu/tiers/{vu_cached_tier(+_exec), vu_tier_dispatch}`, `StallModel::{ApplyPrunePush, MaterializeFmac}`,
`VuUnit::{Execute, ResumeIfWaiting, dispatch()}`, `tests/vu/{test_vu_cached_tier, test_vu_cached_vs_interp}`, README rows. Tree GREEN
(107/107; SIMD twin green). No commits (the user commits).

**Not done (moved to 5C part 2 — the tool):** `tools/vu1_replay/{vu1_replay.cpp, xgk_capture_sink.h/.cpp, README.md}` (grammars per
FACTS §C: `<case>`, `--selftest` (redefined), `--mode interp|cached <case> [postdata_out]` with the `.regs` sidecar, `--compare
[--compare-cycle] [--expect-aot|--expect-noaot] <case|--fabricate>` with the verbatim `COMPARE …` line, `--bench N`, `--bench-cached N`,
`--dump-kicks` partial, `--cycle-cap N`; `--hle`/`--catalog` exit 2), registered from `tests/vu/CMakeLists.txt` (FACTS §F.3; binary at
`build/tools/vu1_replay/vu1_replay`), ctest `Vu1ReplaySelfTest` + `Vu1CachedCompare_<case>` ×126 (SKIP 77 without `PS2R_OG_ROOT`),
`tests/vu/compare_vs_old.py` (+ copy at `/tmp/p5/5C/`; `RB_POLL_S=1`), `tests/gif/fixtures/gsops_seed.txt` (126 lines). Kick byte
capture: decide FACTS §F.2 (A) `RegisterOpStream` through the parser (enough for `kicks=/bytes=` and the seed) vs (B) a `GifArbiter`
submit observer (bytes for 5D's `GsPacketDiff`) — (B) is an OWNED 6-line addition to `engine/gif/gif_arbiter.h/.cpp`.

**Surface for the tool (exact):** `VuUnit vu(1, &arbiter); vu.LoadCase(c); vu.dispatch().settings().vu1_mode = VuTierMode::Cached|Interp;
vu.dispatch().settings().hle_enabled = false; vu.dispatch().set_cycle_cap(N); vu.Execute(c.start_pc); arbiter.drain();` then `vu.regs()`
(608 B incl. padding — `LoadCase` copies `c.regs` whole), `vu.state().data`, `vu.cycle()`, `vu.dispatch().bundles()`,
`vu.state().xgkick_count`. `xgk_staging_inserts` has no NEW twin — `--compare-cycle` compares `vu_cycle` only (record it).

**Integration list (for 5H):** `VuUnit::Execute` ← VIF1 MSCAL/MSCNT/MSCALF and CMSAR0/VCALLMS (VU0); `VuUnit::ResumeIfWaiting` ←
CTC2-to-VI (VU0); `VuTierDispatch::settings()` ← Config `engine.vu1_mode` (Hle → Cached) and the VU1 HLE gate; `SetHleHook` ← 5D
`Vu1HleRuntime`; `SetMtvuHook` ← 5G; `set_cycle_cap` ← none (1,000,000 fixed outside the tool).

**Gate status:** GREEN — `100% tests passed, 0 tests failed out of 107`; `LintOneClass: 378 files scanned, 0 findings -> PASS`;
`LintEnginePurity: 320 files scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; build 0 warnings (dev + SIMD twin).

**Next command (5C part 2):** `cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect
107/107), then write `tools/vu1_replay/vu1_replay.cpp` against FACTS §C over the surface above, register it from `tests/vu/CMakeLists.txt`,
then `RB_POLL_S=1 python3 tests/vu/compare_vs_old.py --old-bin $PS2R_OLD_ROOT/build/tests/vu1_replay
--new-bin build/tools/vu1_replay/vu1_replay --cases $PS2R_OLD_ROOT/vutrace_cases2 --mode compare --out /tmp/p5/5C/cmp`
(expect `CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0`).

### 5C part 2 — tools/vu1_replay + `compare_vs_old.py` + the 126/126 gate vs OLD (2026-09-28, ≈40 min)

Written against `/tmp/p5/5C/FACTS.md` §C / §D / §F.2-F.5 and PHASE5_PLAN §4.1, §6 G2, §7 row 5C, over the part-1 surface
(`VuUnit::Execute`, `dispatch().settings()`, `set_cycle_cap`, `bundles()`). OLD read: `tests/vu1_replay.cpp:130-300, :373-610,
:984-1200` (the printf grammars transcribed verbatim). Every OLD binary run went through `verify/run_bounded.sh` with cwd
`/tmp/p5/5C/cmp/oldcwd` (never OLD's tree). No `getenv`; no `REVIEW.md` existed for this stage.

### Landed (files)
| File | Class / content | OLD source (read-only) |
|---|---|---|
| `tools/vu1_replay/vu1_replay.cpp` (500) | `main` + free functions in an anonymous namespace (zero primary types). `Rig` (`XgkCaptureSink` ← `GifParser` ← `GifArbiter`, tap attached), `RunCaseCapture` (FRESH unit + fresh chain per leg: `LoadCase`, `vu1_mode`, `hle_enabled=false`, `set_cycle_cap`, `Execute`, `drain`), `RunCase` (the `replayed pc=… mode=… -> TPC=… Q=… vi01=… vf01=…` line + the raw 16 KB dump + the 608-byte `.regs` sidecar), `Compare` (interp then cached; the first 8 differing bytes, per-register lines, `--compare-cycle` = `vu_cycle` + kicked op count/hash, the verbatim `COMPARE %s: pc=0x%04x data_diff_bytes=%zu/%u regs_%s aot_dispatches=%llu%s (interp vs cached)` line, exit 0/1), `BenchMode` (one unit per mode, N re-installs, only `Execute` timed; `BENCH …` + `CENSUS entries=<table size>`), `DumpKicks` (partial: entry/tops header, `kicks= bytes= ops= ophash=`, per-packet sizes), `GsOps` (`<case> <count> <hash>`), `FabricateCase` (the OLD synthetic case verbatim), `SelfTest` (redefined: WriteCase/ReadCase round-trip → two interp replays identical → TPC 0x38 / VI01 5 / VF09 ground truth → `Compare` PASS with `--compare-cycle`; `SELFTEST PASS: 2 interp replays identical, interp==cached, bundles=7`), `Usage` (OLD text + the `--cycle-cap N` and `--gsops` lines), `main` (lifts `--cycle-cap N` from anywhere; dispatch order `--selftest`, `--dump-kicks`, `--gsops`, `--hle`/`--catalog` → stderr `not available until stage 5D` exit 2, `--mode`, `--compare [--compare-cycle] [--expect-aot → 77 | --expect-noaot] [--fabricate]` with absent-file → 77 / corrupt → 2, `--bench` / `--bench-cached`, legacy `<case> [trace_out] [postdata_out]` with no trace written) | `tests/vu1_replay.cpp:134-154` (InstallCase → `LoadCase`), `:163-196`, `:203-241`, `:255-296`, `:298-358` (redefined), `:373-473`, `:482-543`, `:588-603` (partial), `:990-1003`, `:1005-1200` |
| `tools/vu1_replay/xgk_capture_sink.h/.cpp` (41 + 33) | `XgkCaptureSink : GsSink` — `Attach(GifArbiter&)` installs the static `OnSubmit` trampoline as the arbiter's submit observer and keeps every PATH1 payload verbatim (`kicks()`, `kick_bytes()`); the ten `GsSink` overrides forward into an owned `RegisterOpStream` (`ops()`: count + hash) | `tests/vu1_test_shims.cpp:37-46` (the PATH1 tap), `tests/vu1_xgk_capture.h` |
| `engine/gif/gif_arbiter.h/.cpp` (edit, OWNED) | `GifArbiter::SetSubmitObserver(SubmitObserver fn, void* user)` — a function-pointer byte tap called in `submit` / `submitOwned` after the sub-tag drop and before queuing; null by default (zero cost). FACTS §F.2 option (B): 5D's `GsPacketDiff` needs the raw packet bytes | — |
| `tools/vu1_replay/compare_vs_old.py` (162) + copy `/tmp/p5/5C/compare_vs_old.py` | the driver: per sorted case, OLD `--compare` (bounded, cwd `<out>/oldcwd`) then NEW `--compare` (bounded); drop `[VU-MODE]` / `[run_bounded]` lines, compare exit codes + every remaining line; `--postdata N|all` additionally runs `--mode interp` and `--mode cached` on both and `cmp`s the data dump + `.regs` sidecar per tier and the `replayed` lines; `CMP <case> SAME|DIFF old_exit= new_exit= [interp=cmp_ok cached=cmp_ok]`, summary `CMP cases=N same=M diff=K old_fail=A new_fail=B`; exit 0 iff `diff=0`, 77 when OLD or the corpus is absent; `RB_POLL_S=1` set in the subprocess environment (our runner's knob); sequential | PLAN §4.1 / FACTS §F.4 |
| `tools/vu1_replay/CMakeLists.txt` + root `CMakeLists.txt` (+1 line `add_subdirectory(tools/vu1_replay)   # Phase 5: VU1 corpus replay tool`) | `add_executable(vu1_replay …)` at `-Wall -Wextra -Werror -O2 -fno-strict-aliasing -fwrapv`, linked to `ps2r_vu` only (its PUBLIC links carry `ps2r_gif ps2r_ee ps2r_core ps2r_gs_format`; naming them again made ld warn "ignoring duplicate libraries" — the 0-warning gate) → `build/tools/vu1_replay/vu1_replay` | `tests/CMakeLists.txt:940-997` |
| `tools/vu1_replay/README.md` | purpose / grammar table / belongs / does-not-belong / OLD provenance (the `--selftest` redefinition stated) | — |
| `tests/vu/CMakeLists.txt` (+2 rows) | `Vu1ReplaySelftest` (`vu1_replay --selftest`, TIMEOUT 120); `Vu1ReplayCompareCorpus` (`compare_vs_old.py --old-bin ${PS2R_OG_ROOT}/build/tests/vu1_replay --new-bin $<TARGET_FILE:vu1_replay> --cases ${PS2R_OG_ROOT}/vutrace_cases2 --out ${CMAKE_BINARY_DIR}/tests/vu/compare_vs_old --postdata 12`, TIMEOUT 1800, SKIP_RETURN_CODE 77) | `tests/CMakeLists.txt:1296-1310` (`Vu1ReplaySelfTest`, `Vu1CachedCompare_*` ×126 — collapsed into the ONE driver row, see P5-5C-COMPARE-ROWS) |
| `tests/gif/fixtures/gsops_seed.txt` (126 lines) + `tests/gif/check_gsops_seed.py` + `tests/gif/CMakeLists.txt` (+1 row `Vu1GsopsSeed`) + `tests/gif/README.md` row | PLAN §4.4 / row 5C: the corpus half of `GifParserEquivalence` — `vu1_replay --gsops <case>` over the 126 corpus files must reproduce the recorded `<case> <op count> <hash>` (generated once via `/tmp/p5/5C/gen_gsops_seed.sh` under `run_bounded.sh`); SKIP 77 without the corpus | — |
| `engine/gif/README.md` (row edit) | `gif_arbiter` row names `SetSubmitObserver` | — |

### Flags applied
- `PS2_VU1_CYCLE_CAP` → TEST-ONLY: the tool option `--cycle-cap N` (any position; `> 0`; default `VuTierDispatch::kDefaultCycleCap`
  = 1,000,000), fed to `set_cycle_cap` for both tiers. No product `getenv`.
- `PS2_VUTRACE` / `PS2_VUTRACE_CAPTURE` (`DisarmTraceEnvs`, `VuTraceForce`) → DIAG, omitted: no trace file is written by any
  arm; the legacy `trace_out` argument is accepted and ignored; `--selftest` redefined (below).
- `PS2_VU_AOT` (build) / `--expect-aot` → not written: `aot_dispatches=0` is printed literally; `--expect-aot` exits 77 with a
  one-line stderr note; `--expect-noaot` is always satisfied.
- `PS2_VU_MODE` → SETTING (part 1): the tool sets `settings().vu1_mode` per leg; the OLD `[VU-MODE] active mode:` announce is
  not printed (the driver strips it from OLD's log).

### Tests added (ctest names)
(quoted from `ctest --preset macos-dev`, `/tmp/p5/5C/ctest3.log`; see Oracle for the full summary)
- `Vu1ReplaySelftest` — `109/110 Test #109: Vu1ReplaySelftest ...................   Passed    0.01 sec`
- `Vu1ReplayCompareCorpus` — `110/110 Test #110: Vu1ReplayCompareCorpus ..............   Passed  326.40 sec` (12 postdata cases; its own log ends `CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0`)
- `Vu1GsopsSeed` — ` 92/110 Test  #92: Vu1GsopsSeed ........................   Passed    1.14 sec` (`GSOPS seed lines=126 diff=0`)

### Oracle / gate status
Driver command (from the NEW root; OLD cwd `/tmp/p5/5C/cmp/oldcwd`, every process under `verify/run_bounded.sh 120`, `RB_POLL_S=1`):
```
python3 tools/vu1_replay/compare_vs_old.py --cases $PS2R_OLD_ROOT/vutrace_cases2 --out /tmp/p5/5C/cmp --postdata 16
```
Result (`/tmp/p5/5C/cmp_gate.log`, 127 lines):
```
CMP vu1_case_0000_1748.bin SAME old_exit=0 new_exit=0 interp=cmp_ok cached=cmp_ok
CMP vu1_case_crys_1ad766a1.bin SAME old_exit=0 new_exit=0 interp=cmp_ok cached=cmp_ok
CMP vu1_case_w14hub_0001.bin SAME old_exit=0 new_exit=0
CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0
```
Every OLD `--compare` log holds exactly `[VU-MODE] active mode: interp` + `COMPARE PASS: pc=0x… data_diff_bytes=0/16384
regs_identical aot_dispatches=0 (interp vs cached)`; every NEW log the same `COMPARE PASS` line (0 `DIFF` lines in the gate log).
Per-tier postdata oracle: the first 16 cases (`vu1_case_0000..0007_1748`, `vu1_case_1400_*`, `vu1_case_crys_*` …) ran `--mode
interp` and `--mode cached` on BOTH binaries — 16 × `interp=cmp_ok cached=cmp_ok`, i.e. 32 data dumps (16 KB) + 32 `.regs`
sidecars (608 B) byte-identical OLD vs NEW and the `replayed …` lines identical (spot check: `cmp
/tmp/p5/5C/cmp/{old,new}/vu1_case_0000_1748.interp.bin` → identical; `…cached.bin.regs` → identical).
Direct tool checks: `--selftest` → `SELFTEST PASS: 2 interp replays identical, interp==cached, bundles=7` (exit 0); `--compare
--fabricate` → `COMPARE PASS: pc=0x0000 …` on both OLD (bounded, `/tmp/p5/5C/old_fab.log`) and NEW; `--compare /nonexistent.bin`
→ stderr `case file absent, skipping` exit 77; `--hle x` → exit 2; `--bench 3 <case>` → `BENCH mode=interp … bundles/run=237 …
aot=0` / `CENSUS entries=0`, `BENCH mode=cached … bundles/run=237 … aot=0` / `CENSUS entries=1`; `--dump-kicks` →
`kicks=1 bytes=576 ops=38 ophash=660c3ec569fd9474` on `vu1_case_0000_1748`.
Gate G2 (PLAN §6): ✔ — `CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0` + per-tier `cmp` identical (16 cases in this run;
the ctest row re-runs 12 on every `ctest`).
Tree: build `cmake --build --preset macos-dev -j4` → 0 warnings; `LintOneClass: 378 files scanned, 0 findings -> PASS`;
`LintEnginePurity: 320 files scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; ctest: `100% tests passed, 0 tests failed out of 110` (`Total Test time (real) = 557.77 sec`).

### Findings (→ plan §7.1)
- `P5-5C-FABRICATE-TPC` (FACTS correction): FACTS §C.4 says the fabricated case ends at `TPC 0x30` like `Vu1TransformGroundTruth`
  variant A; the fabricated program has SEVEN bundles (the `IADDIU vi1,vi0,5` slot precedes NOP[E]) so the E-bit delay bundle is
  at 0x30 and TPC = 0x38. Both OLD and NEW agree (`--compare --fabricate` PASS on both); the selftest pins 0x38.
- `P5-5C-ARBITER-OBSERVER` (surface added, OWNED): `GifArbiter::SetSubmitObserver` (function pointer + user, null default) is
  the byte tap OLD had as a test shim on `submit`; chosen over a parser-side sink because 5D's `--hle` / `--dump-kicks` decode the
  raw PATH1 packet bytes (`GsPacketDiff`). Engine cost when unset: one null check per submit.
- `P5-5C-COMPARE-CYCLE-XGK` (substitution, recorded): OLD's `--compare-cycle` also compared `xgk_staging_inserts` (a PERF counter
  with no NEW twin); NEW compares `vu_cycle` and the kicked `RegisterOpStream` count + hash instead (a stronger GS-visible check).
  The driver never passes `--compare-cycle`, so no OLD line is affected.
- `P5-5C-SELFTEST-REDEFINED`: OLD's selftest asserted a VUT trace file (DIAG, not transplanted); NEW asserts round-trip +
  replay determinism + tier identity + the transform ground truth. Same exit contract; the text is never diffed.
- `P5-5C-COMPARE-ROWS`: PLAN row 5C names `Vu1CachedCompare_<case>` ×126 ctest rows; this tree registers ONE
  `Vu1ReplayCompareCorpus` row (the OLD-vs-NEW driver over all 126, which subsumes 126 NEW-only PASS checks) plus the part-1
  `VuCachedVsInterpCorpus` (in-process 126/126). 126 separate rows would add ~250 process launches per `ctest` for no extra
  evidence; revisit if a per-case failure locator is wanted.
- `P5-5C-DUMPKICKS-PARTIAL`: `--dump-kicks` prints the packet census only (no `image=` header, no GIF decode) until 5D lands
  `Vu1ImageAnalyzer` + `GsPacketDiff`; `--hle` / `--catalog` exit 2 with a stderr note. The driver never runs these arms.
- `P5-5C-BENCH-CENSUS`: OLD's `--bench` printed per-image `CENSUS` PERF counters (DELETE); NEW prints `CENSUS entries=<table
  size>` only. Timing lines are never diffed.
- `P5-5C-LINK-DUPLICATE`: linking the tool to `ps2r_vu ps2r_gif ps2r_core ps2r_ee ps2r_gs_format` (as the row said) produced
  `ld: warning: ignoring duplicate libraries`; the tool links `ps2r_vu` alone (its PUBLIC links carry the rest).
- `P5-5C-DRIVER-COST`: each bounded run costs ≥ 1 s of `run_bounded.sh` polling (`RB_POLL_S` is an integer); 126 × 2 compare
  runs + 16 × 4 postdata runs ≈ 6 min wall. The 5H sweep (3,293 cases) inherits this — batching per process is the lever.

### HANDOFF (5C → 5D)
**Done:** `tools/vu1_replay` (all 5C grammars), `XgkCaptureSink` + `GifArbiter::SetSubmitObserver`, `compare_vs_old.py`
(repo + `/tmp/p5/5C/`), ctest `Vu1ReplaySelftest` / `Vu1ReplayCompareCorpus` / `Vu1GsopsSeed`, `tests/gif/fixtures/gsops_seed.txt`
(126 lines), root CMake +1 line, READMEs. **G2 ✔** (`CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0`, per-tier dumps `cmp`
identical). Tree GREEN. No commits (the user commits).

**Not done (5D):** `--hle [--hle-ref] [--skip-unregistered] [--exact] [--tol-*] [--strict-packets] [--max-lines]` and
`--catalog` bodies (need `Vu1ImageAnalyzer`, `Vu1Registry::Lookup`, `GsPacketDiff::Decode/Print`, `GsDirectOpsCheck`);
`--dump-kicks` full output (`image=` header + `gsp::Print`); `tier_dispatch` step 1 wired (`hle_enabled=true` with a library);
`Vu1Hle_<case>` ×126 / `Vu1HleRef_*` / `Vu1HleCatalog` rows; the title library `games/…/vu1_library` (group A first).

**Surface for 5D (exact):** `XgkCaptureSink::kicks()` = `std::vector<std::vector<uint8_t>>` of raw PATH1 packets in submission
order (the `GsPacketDiff::Decode` input); `RunCaseCapture(c, mode, cap, &kicks)` in `vu1_replay.cpp`; the HLE leg =
`vu.dispatch().SetHleHook(&Vu1HleRuntime::TryExecute, &runtime); settings().hle_enabled = true;` on a fresh unit, then the same
`Capture`. The `--catalog` `bundles` column is the CACHED leg's `dispatch().bundles()` (FACTS §C.8 correction).

**Gate status:** GREEN — build 0 warnings; ctest `100% tests passed, 0 tests failed out of 110` (`Total Test time (real) = 557.77 sec`); `LintOneClass … 0 findings -> PASS`; `LintEnginePurity …
0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; `CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0`.

**Next command (5D):** `cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -E Vu1ReplayCompareCorpus`
(expect 100 %), then write `engine/vu/hle/{vu1_hle_contract.h, vu1_family_signature.h, vu1_registry, vu1_image_analyzer,
vu1_hle_runtime, kick_lowering, gif_packet_builder}` per PLAN §2.4 and `tools/vu1_replay/{gs_packet_diff, gs_direct_ops_check}`;
then `python3 tools/vu1_replay/compare_vs_old.py --out /tmp/p5/5D/cmp` (re-run G2) and the `--hle` driver arm over group A.

## 5D — VU1-HLE mechanism + title library

### 5D part 1 (mechanism, tool, group A) (2026-09-28, ≈75 min)

FACTS: `/tmp/p5/5D/FACTS.md` (§A mechanism, §B `gs_packet.h`, §C library per family, §E tool grammars, §G the
bounded OLD reference `/tmp/p5/5D/old_hle_all.log` = 126 cases, 124 PASS + 2 × exit 77, §H notes). Every OLD
anchor below is a `host_app/hle/*` or `tests/*` line range re-read this session.

### Landed (files)

| New file (lines) | Class / content | OLD source transplanted |
|---|---|---|
| `engine/vu/hle/vu1_hle_contract.h` (59) | PODs `Vu1HleInput {micro, data, start_pc, top, tops, itop, itops, regs, image_hash}`, `Vu1HleOutput {kicks, data_writes (DataWrite), has_regs, regs}`, `Vu1HleFn`, `Vu1ImageInfo`, `kVu1HleMicroSize/DataSize`; zero primary types, zero title constants | `hle/vu1_hle.h:30-31, :37-63, :67, :86-98` (`kVariantBase :35` → games) |
| `engine/vu/hle/vu1_family_signature.h` (14) | `Vu1FamilySignature {variant_base, dispatcher_op0, exit_stubs[2], family_exit_tpc}` — fields only | `vu1_hle.h:35`, `vu1_hle.cpp:46-50, :78`, `vu1_hle_runtime.cpp:407-410` (the values → `Vu1Library::Signature()`) |
| `engine/vu/hle/vu1_library_entry.h` (29) | `Vu1LibraryEntry {image_hash, variant_hash, name, start_pc (uint32, kVu1AnyEntry), fn, oracle_case_ids, oracle_case_count}`, `Vu1ProgramLibrary {family, entries, entry_count}` (C++17: pointer + count, no `std::span`) | `vu1_hle.h:69-75`; ARCH §14.2 |
| `engine/vu/hle/vu1_registry.h/.cpp` (26 + 30) | `Vu1Registry(const Vu1ProgramLibrary&)` — `Lookup` verbatim (pc filter, FIRST exact image returns, first variant, first wildcard), `Append` (the harness reference), `Entries`, `DistinctVariantHashes` | `vu1_hle.cpp:11-31` |
| `engine/vu/hle/vu1_image_analyzer.h/.cpp` (35 + 67) | `Vu1ImageAnalyzer(const Vu1FamilySignature&)` — `Analyze` (extent scan from the top in 8-byte steps, `VuImageHash::Fast` for the three hashes, the family prefix LEARNED per instance from the first dispatcher image, the ≤64-step chain walk to an exit stub), `ShortHash` (`%08llx` of `h >> 32`), `LooksLikeFamilyDispatcher` | `vu1_hle.cpp:33-89` (`0x090E0003` → `sig_.dispatcher_op0`, `0x1720` → `sig_.variant_base`, `0x70/0x90` → `sig_.exit_stubs`) |
| `engine/vu/hle/vu1_hle_runtime.h/.cpp` (77 + 95) | `Vu1HleRuntime(library, GifArbiter&)` / `(library, KickLowering&)` — `Hook` (the `VuTierDispatch::HleHook` shape), `TryExecute`: image-hash memo keyed `(micro_generation, ext_gen)`, per-image analyze memo (`kMemoLimit` 512 → clear), `Lookup(entry_pc & 0x3FFF)`, `Vu1HleInput` from `unit.state()` + `vif_top()/vif_itop()` + the `SetVifTops` seam, run, data writes (`& 0x3FFF`, clipped) BEFORE kicks, one `KickLowering::Submit` per kick, `regs = out.regs` or `TPC = family_exit_tpc`; `census()` runs/misses/declines; `BumpExternalGeneration` (the EE-window epoch seam) | `vu1_hle_runtime.cpp:160-423` (`:181-214` memo, `:216-236` lookup, `:238-247` input, `:249-283` run, `:285-293` writes, `:295-399` kick ladder → raw tier, `:401-410` regs); `:427-437` static installer NOT written |
| `engine/vu/hle/kick_lowering.h/.cpp` (31 + 17) | `KickLowering(GifArbiter&)` — `Submit` = one arbiter slot per kick in program order (raw `submitOwned(Path1)`), `virtual TryLower` = the DrawIR / typed-ops seam (contract in the header comment, not stubbed), `submitted()` | `vu1_hle_runtime.cpp:295-399` (tier 3 written; tiers 1–2 = the seam) |
| `engine/vu/hle/gif_packet_builder.h/.cpp` (35 + 72) | `GifPacketBuilder` — `BeginTag/EndTag/QW/PRIM/RGBAQ/ST/UV/XYZ2/XYZF2/AD/Take/Bytes`, bit-exact | `hle/gs_packet.h:351-403` (`gsp::Builder`) |
| `engine/vu/vu_float.h/.cpp` (+7 / +14) | `VuFloat::ClipNf(x, y, z, w)` — the generic CLIP judgement (the interpreter's `RunClip` body as a pure function; NEW lacked it) | `vu1_hle_programs.cpp:111-123`; `tiers/vu_interpreter_tier_upper.cpp:268-281` |
| `engine/vu/CMakeLists.txt` (+6) | the five `hle/*.cpp` TUs in `ps2r_vu` | — |
| `tools/vu1_replay/gs_packet_diff.h/.cpp` (118 + 481) | `GsPacketDiff` — `REG_*` enum, `Write/TagInfo/Stream/XYZ/RGBAQ/Tolerance/DiffResult`, `RegName`, `IsVertexReg/IsKickReg`, `DecodePacket/Decode` (PACKED via `PackedToCanonical`, REGLIST with the QW pad, IMAGE skip, truncation errors, per-packet `q_latch = 0x3F800000`), `UnpackXYZ/RGBAQ/ST`, `Format` (every printf grammar verbatim), `WriteClose/Normalize/Diff` (summary string verbatim), `Print` | `gs_packet.h:28-345, :406-570` |
| `tools/vu1_replay/gs_direct_ops_check.h/.cpp` (34 + 132) | `GsDirectOpsCheck` — `Op`, `kModeReglist/kOpFlush`, `PacketToOps` verbatim (IMAGE/DISABLE and truncation decline, NLOOP=0 keeps EOP, PACKED-layout registers keep both halves, A+D / default → reglist-mode low 64), `OpsToWrites` (the tool's `ops_to_writes` projection), `Matches` | `hle/gs_direct_ops.h:38-151`; `tests/vu1_replay.cpp:651-704` |
| `tools/vu1_replay/vu1_replay.cpp` (498 → 881) | `--hle [--hle-ref] [--skip-unregistered] [--exact] [--tol-xy N] [--tol-z N] [--tol-rgba N] [--tol-st F] [--strict-packets] [--max-lines N] <case>` (`HleCompare`: header, SKIP/FAIL 77/1, interp leg, DIRECT program call, `HLE FALLBACK` 1, kick diff lines, `direct=` per kick, `data=` with the ≤8 QW detail, `regs=` when claimed, the verdict line), `--catalog <dir|case…>` (`ExpandCasePaths` sorted, `ImageRow` map keyed `(image_hash, start_pc)`, interp leg kicks/verts/prims + cached leg `bundles`, `std::sort` by bundles desc, family-weighted `bundle%`, header/row/footer verbatim), `--dump-kicks` full grammar (header + `kicks= bytes= writes= tags= errors=` + `Print`), `--registry` (`REGISTRY entries=223 distinct_variant_hashes=176`), `ReferenceInterpreter` (the wildcard program under `--hle-ref`), `MakeInput`, `PrintImageHeader`; ONE `Vu1Registry` + ONE `Vu1ImageAnalyzer` per process; the library is linked and the interp legs keep `hle_enabled = false` | `tests/vu1_replay.cpp:554-586, :588-603, :605-815, :817-853, :855-940, :946-975, :1011-1056` |
| `tools/vu1_replay/hle_gate.py` (57) | the ctest driver: `--hle --skip-unregistered` over a corpus dir, every case of the named `--programs` must PASS; `HLE-GATE cases= judged= pass= fail= unregistered= other_programs=`; 77 without the corpus | (NEW) |
| `tools/vu1_replay/CMakeLists.txt` | `ps2r_vu1_replay_lib` STATIC (`gs_packet_diff`, `gs_direct_ops_check`, `xgk_capture_sink`) shared with `tests/vu`; `vu1_replay` links it + `ps2r_title_vu1_library` | — |
| `games/…/vu1_library/vu1_library.h/.cpp` (18 + 256) | `Vu1Library` — `Signature()` = `{0x1720, 0x090E0003, {0x70, 0x90}, 0xA0}`, `Get()` over `kEntries[223]` in REGISTRATION ORDER (generated from `/tmp/p5/5D/registrations.tsv` by `/tmp/p5/5D/gen_table.py`; `static_assert` 223), `DeclineStub` for the 5D′/5D″ rows (each row's trailing comment names the OLD program) | `vu1_hle_programs.cpp:610-635, :2067-2104, :2387-2423, :2914-2946, :3657-3728, :3994-4013, :4208-4273, :4995-5077, :5322-5361, :6083-6098, :6131-6149, :6174-6181, :6198-6204` |
| `games/…/vu1_library/library_common.h/.cpp` (76 + 109) | `namespace vu1lib`: `V4`, the one-line `VuFloat` wrappers `CF/bits2f/f2bits/VuDiv/Ftoi0/ClipNf/Fmin_raw/Fmax_raw` (the transcription vocabulary), `kAdcMark`, `kMeshRingBound = 4096`, `XformEmit`, `XformClip`, `GuardScale`, `CrossZSign`, `CrossXYZ`, `Rsq`, `KickAt`, `RotateQw1`, `WorkingCopy` (`thread_local` 16 KB), `EmitDataWrites` (the ONE copy of the 10 identical diff loops) | `vu1_hle_programs.cpp:67-73, :125-207, :251-258, :516-520, :591-603, :683-700, :1380-1387` |
| `games/…/vu1_library/qw_ptr.h/.cpp` (13 + 19) | `QwPtr {mem; W/SetW/F}` — QW index masked `& 0x3FF` | `:125-134` |
| `games/…/vu1_library/clip_gate.h/.cpp` (50 + 97) | `ClipGate` — `GateRegs` (nested), `SeedGateRegs`, `LatestOfClass`, `UpdateGateRegsAtEnd`, `ClipGateB0` (QW10 spill, per-vertex `XformClip`/`GuardScale`/`ClipNf` triple, hard AND → ADC, guard OR → the near clipper when `QW5.y*8 == NearClipper::kEntryPc` and bit 15 clear needs valid `GateRegs`; `WhyNote` sites → plain `return false`) | `:297-334, :739-744, :752-776, :1091-1136` |
| `games/…/vu1_library/near_clipper.h/.cpp` (39 + 556) | `NearClipper` — `kEntryPc = 0x440`, `Run` (register spill QW14-37 / QW11-13 with the `kLink[3]` window links, triangle init QW39-44, six Sutherland-Hodgman passes in QW39-58/59-78 with the `dp + 2 > dst_base + 20` declines and the empty bail, fan dispatch by `gr.fan_kind`), `EmitClipFan`, `EmitLitClipFan`, `EmitFamClipFan`, `EmitCrysClipFan` (the crystal fan written now; only reachable from 5D″) | `:793-873, :894-1090, :1418-1542, :1543-1639, :1640-1787` |
| `games/…/vu1_library/family_tristrip.h/.cpp` (22 + 244) | `FamilyTristrip` — `Run` (entry 0 only, `VuChopRoundGuard`, working copy, `clip_image_ok` = the one verified image, `SeedGateRegs(fan 0x1ac0, kind 2)`, node walk ≤16: `0x90` break / `0x1720` emitter / else decline), `RunEmitterCall` (mesh loop ≤64 masked `& 0x3FF`, the FDIV-pipeline-ordered pair loop with `CrossZSign` verdicts, `negz_ftoi`, `CrossXYZ` for vf18, the QW1 rotation, the vf16/vf18 re-seed) | `:338-543, :545-605` |
| `games/…/vu1_library/textured_tristrip.h/.cpp` (23 + 444) | `TexturedTristrip` — `Run` (beacon `QW9.x = 1`, 10 `clip_image_ok` hashes, fan 0x1b90 kind 0, `0x1e90` only for image `e265d357…`), `RunTexCall` (GATED path with link 0x2F9 and the `g` loop; NO-GATE path with the previous-quotient windows `Q_arch/Q_pend` and the WAITQ flush), `RunEnvCall` (link 0x3E8, the `c7` invsqrt constants, `Rsq`, sphere-map ST, `gr.vf27/28/29 = litR[1..3]`) | `:1142-1379, :1788-1966, :1968-2058` |
| `games/…/vu1_library/poscolor_strip.h/.cpp` (23 + 196) | `PoscolorStrip` — `Run` (beacon, 11 hashes, fan 0x19b8, the stale-node spin-to-backstop sled rule: `kicks.empty()` ∧ `call_pc > 0x1720` ∧ zero micro tail → break), `RunPosColorCall` (base = QW3.x, rows +78/+82, unconditional gate link 0x2F2, `vi13 = 0x2E4`, own-quotient per vertex, the `adc_of` IOR, the aliasing-exact store order) | `:2151-2274, :2275-2379` |
| `games/…/vu1_library/lit_tristrip.h/.cpp` (32 + 378) | `LitTristrip` — `kGateLink = 0x2F9`, `Run` (beacon, 2 hashes, fan 0x1b98 kind 1, `0x1e40` only for image `8d8dc807…`), `RunLitCall(m, node, top, out, gate_link, gr)` (EXPORTED for the crystal relocated body; mesh walk bounded by `kMeshRingBound` — `PS2_VU1_CRYS_RING` NORM winner; the loop-end vf16/vf18 capture; `nverts < 2 → gr->valid = false`), `RunEnv2Call` (link 0x3DE, `ClipGateB0` without `gr`) | `:2494-2669, :2670-2840, :2841-2906` |
| `games/…/vu1_library/CMakeLists.txt`, `README.md` | `ps2r_title_vu1_library` STATIC → `ps2r_vu` only, `-Wall -Wextra -Werror -O2 -fno-strict-aliasing -fwrapv`; README: file table, the 223-row table by program with part + oracle case ids, provenance | — |
| root `CMakeLists.txt` (+1) | `add_subdirectory(games/${PS2R_TITLE}/vu1_library)   # Phase 5: the VU1 HLE program library (title data)` after the existing games/ lines | — |
| `tests/vu/test_gs_packet.cpp` (187), `tests/vu/test_vu1_hle_mechanism.cpp` (233), `tests/vu/CMakeLists.txt` (+27) | `VuGsPacketCodec`, `Vu1HleMechanism`, `Vu1HleGroupA` (below) | `tests/test_gs_packet.cpp` (163, every CHECK ported); the mechanism test is NEW |
| `engine/vu/hle/README.md`, `engine/vu/README.md` (+6 rows, `ClipNf`), `tools/vu1_replay/README.md` | file tables, grammars, provenance | — |

Placement decisions: `vu1_library.cpp` holds ONE ordered table (not per-family spans) because the OLD order
interleaves families (P5-5D-TABLE-ORDER); the program classes are `FamilyTristrip` / `TexturedTristrip` /
`PoscolorStrip` / `LitTristrip` so the plan's file names satisfy `lint_one_class`'s file-name rule; `QwPtr`
got its own pair (it has member functions); `GifPacketBuilder` stays in `engine/vu/hle` per the plan row although
its only consumers are the codec test and tools (P5-5D-BUILDER-TOOLSIDE).

### Flags applied

| Flag | Outcome | Where |
|---|---|---|
| `PS2_VU1_HLE` | NORM ON → `VuSettings::hle_enabled` (the tool sets `true` only where it installs the runtime; `--hle` calls the program directly as OLD did) | `vu1_hle_runtime`, `vu1_replay.cpp` |
| `PS2_VU1_HLE_HASHEPOCH` | NORM ON → the image-hash memo is unconditional | `Vu1HleRuntime::TryExecute` |
| `PS2_VU1_HLE_DIRECT`, `PS2_GS_DRAWIR` | NORM-as-contract → `KickLowering::TryLower` seam; raw PATH1 tier written (this phase) | `kick_lowering.h` |
| `PS2_GS_DRAWIR_VERIFY` | TEST → nothing in product code; the tool's `direct=` token is the offline half | `vu1_replay.cpp` |
| `PS2_VU1_HLE_DEBUG` | DIAG → omitted (miss log, run/decline stderr censuses; `census()` counters kept as plain data) | — |
| `PS2_VU1_HLE_WHY` | DELETE → not written; every `WhyNote(...); return false;` is `return false;` | `clip_gate`, `near_clipper`, `lit_tristrip` |
| `PS2_VU1_CRYS_RING` | NORM ON → `kMeshRingBound = 4096` (lit walk now; crys walk 5D″) | `library_common.h`, `lit_tristrip.cpp` |
| `PS2_VU1_CRYS_GATE` | NORM ON → the pre-scan decline is not written (5D″ scope) | — |
| `g_cut_xgkick`, the stderr censuses every 500 / 5000 runs | DELETE → not written | — |

### Tests added (ctest names)

```
$ ./build/tests/vu/test_gs_packet
GsPacketCodec PASS
$ ./build/tests/vu/test_vu1_hle_mechanism $PS2R_OLD_ROOT/vutrace_cases2/vu1_case_crys_283276f2.bin
Vu1HleMechanism PASS
$ python3 tools/vu1_replay/hle_gate.py --bin build/tools/vu1_replay/vu1_replay --cases $PS2R_OLD_ROOT/vutrace_cases2 --programs vs_family_tristrip,vs_textured_tristrip,vs_poscolor_strip,vs_lit_tristrip
HLE-GATE cases=126 judged=49 pass=49 fail=0 unregistered=2 other_programs=75
```

- `VuGsPacketCodec` — `GifPacketBuilder` → `GsPacketDiff::Decode` → `Diff` (every OLD `test_gs_packet` CHECK).
- `Vu1HleMechanism` — registry lookup rules on a synthetic library (registration order, exact beats variant, pc
  filter, wildcard, `Append`), analyzer on synthetic images (extent rounding, hashes, learned prefix, chain
  termination / unterminated), `KickLowering` one slot per kick, and `Vu1HleRuntime::Hook` through
  `VuTierDispatch` on `vu1_case_crys_283276f2`: kicks / data / TPC (= `family_exit_tpc`) equal the direct program
  call, `bundles() == 0` (tiers not entered), the memo hit on a second run, a perturbed image = MISS → the tier
  runs, `hle_enabled = false` never consults.
- `Vu1HleGroupA` — `hle_gate.py` over the corpus for the four group-A programs (49 judged, all PASS; SKIP 77
  without the corpus).

### Oracle / gate status

```
$ python3 /tmp/p5/5D/run_new_hle_group.py /tmp/p5/5D/cases_group_A.txt /tmp/p5/5D/new_hle_A.log
HLE cases=49 same=49 diff=0
$ python3 /tmp/p5/5D/run_new_hle_group.py /tmp/p5/5D/cases_group_SKIP.txt /tmp/p5/5D/new_hle_SKIP.log
HLE cases=2 same=2 diff=0
$ ./build/tools/vu1_replay/vu1_replay --hle --skip-unregistered .../vu1_case_crys_283276f2.bin
image=283276f26ddf0d78 (283276f2) variant=e1f9ceeaa5c1ef66 prefix=family extent=0x2c80 entry=0x0000 top=477 tops=632 itop=0 itops=0 dispatch=0x1720->0x1720->0x90
HLE PASS: vs_family_tristrip vs interp -- writes A=1696 B=1696 vertices A=560 B=560 kicks A=256 B=256 packets A=16 B=16 mismatches=0 data=ok regs=n/a direct=ok (tol xy=1 z=1 rgba=1 st=1e-05)
$ ./build/tools/vu1_replay/vu1_replay --registry
REGISTRY entries=223 distinct_variant_hashes=176
$ ./build/tools/vu1_replay/vu1_replay --catalog .../vutrace_cases2 > /tmp/p5/5D/new_catalog.log; diff <(grep -v '^\[VU-MODE\]' /tmp/p5/5P/old_catalog.log) /tmp/p5/5D/new_catalog.log && echo "CATALOG DIFF EMPTY"
CATALOG DIFF EMPTY
CATALOG cases=126 images=63 hle_covered=61/63 bundle_coverage=100.00% (corpus-weighted; live weights are in docs/loop_state/W14_PROFILE.md)
```

The driver (`/tmp/p5/5D/run_new_hle_group.py`, a FILE) runs NEW `--hle --skip-unregistered` per case and requires
every stdout line AND the exit code to equal the OLD block of `/tmp/p5/5D/old_hle_all.log` (recorded bounded in the
investigation): group A (fam 4 / ttx 21 / pcs 11 / lit 13 = 49 cases incl. the 3 crystal-table siblings) →
`same=49 diff=0`; the 2 unregistered images → `same=2` (exit 77 both). Group B (54 cases) prints the identical
`image=` header and `HLE FALLBACK: <name> declined …` exit 1 (DeclineStub — expected until 5D′); group C likewise.
**G4 is already met at 5D** (`--catalog` diff-empty incl. the tie order — `std::sort` reproduced OLD; the `hle`
column names come from the table). G8 grep (`0x1720|0x090E0003|exit_stubs\s*=|0xA0\b|vs_(…)|0x[0-9a-f]{16}` over
`engine/` + `tools/`, format strings excluded): 0 hits under `engine/vu`, `engine/gif`, `tools/` (the only matches are
the Phase-1/3 FNV / libc constants in `engine/core/hash` and `engine/ee/hle/libc.cpp`, unrelated).

Gate: build 0 warnings (`-Wall -Wextra -Werror` on every new target); `LintOneClass: 411 files scanned, 0 findings
-> PASS`; `LintEnginePurity: 333 files scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; ctest `100% tests passed, 0 tests
failed out of 113` (quoted in the HANDOFF).

### Findings (→ plan §7.1)

- `P5-5D-ENTRY-COUNT`: OLD `Entries().size() == 223` (140 `Register(` sites, 176 distinct variant hashes); the
  plan's "entries=140" conflated sites with rows. Pinned 223 / 176 / 140 (`--registry`, `static_assert`, README).
- `P5-5D-TABLE-ORDER`: registration order is the textual static-init order and observable (variant
  `0x59b148e8392fd464`: skn `any` row idx 157 precedes crys `0x12a0` row idx 217); ONE ordered table in
  `vu1_library.cpp` generated from `registrations.tsv` — never sorted, never split per family.
- `P5-5D-FULL-TABLE-AT-5D`: all 223 rows registered now with `DeclineStub` for groups B/C (group-A gate cases
  include 3 crystal-table siblings; the catalog `hle` column is identical from 5D).
- `P5-5D-BUILDER-TOOLSIDE`: the library never uses `gsp::Builder` (kicks are lifted from the working copy by
  `KickAt`); `GifPacketBuilder` kept in `engine/vu/hle` per the plan row, consumers = `VuGsPacketCodec` + tools.
- `P5-5D-TOPS-SEAM`: `VuUnit` carries TOP/ITOP only; `Vu1HleRuntime::SetVifTops(tops, itops)` and
  `BumpExternalGeneration()` are the seams the VIF (5E) and the EE window (integration) drive; `ext_gen` starts 0.
- `P5-5D-HLE-DIRECT-FN`: the tool compares the library program called DIRECTLY (OLD linked no runtime TU);
  `Vu1HleRuntime` is covered by `Vu1HleMechanism` instead.
- `P5-5D-CATALOG-TIES`: `std::sort` over the `(image_hash, start_pc)` map order reproduces OLD's tie order
  exactly (diff empty) — no `stable_sort` needed.
- `P5-5D-CLASS-NAMES`: the plan's `*Program`/`*Programs` class names conflict with its file names under the
  lint's snake_case rule; classes are `FamilyTristrip` / `TexturedTristrip` / `PoscolorStrip` / `LitTristrip`.
- `P5-5D-CLIPNF-ENGINE`: `ClipNf` (generic VU CLIP rule) added to `VuFloat` (engine, OWNED) — the only helper
  NEW lacked; all other library helpers are game-convention and live in `games/`.
- `P5-5D-CRYS-FAN-EARLY`: `NearClipper::EmitCrysClipFan` (fan_kind 3) is written at 5D (self-contained, same
  switch); reachable only from the 5D″ crystal programs.
- `P5-5D-QWPTR-OUTLINE`: `QwPtr::W/SetW/F` are out-of-line (the lint's header-body rule); bit-exactness is
  unaffected, per-call cost is a note for the integration perf pass (LTO or `.inl`).

### HANDOFF (5D part 1 → 5D part 2 / 5D′)

**Done:** `engine/vu/hle/*` (mechanism, 7 headers / 5 TUs), `VuFloat::ClipNf`, `tools/vu1_replay` `--hle` /
`--catalog` / full `--dump-kicks` / `--registry` + `gs_packet_diff` + `gs_direct_ops_check` + `hle_gate.py`,
`games/…/vu1_library` (signature, 223-row table, common helpers, `QwPtr`, `ClipGate`, `NearClipper` with all four
fans, `FamilyTristrip`, `TexturedTristrip`, `PoscolorStrip`, `LitTristrip`), ctest `VuGsPacketCodec` /
`Vu1HleMechanism` / `Vu1HleGroupA`, READMEs, root CMake +1 line. Group A 49/49 line-identical to OLD, SKIP 2/2,
`--catalog` diff-empty (G4), `REGISTRY entries=223 distinct_variant_hashes=176`. Tree GREEN. No commits (the user commits).

**Not done (5D part 2 = group B: skn / idx / zk / spr):** transplant `SkinnedLitStrip` (OLD `:3016-3650`; entries
`any`: `0x1400 → DisplacePrefix::RunDisplaceInt`, `0x15a0 → RunDisplaceFloat`, `0x12a0 → RunScalePhase`, `0` node
walk over `0xcb0/0xb30/0x1040/0x1720/0x1aa0@98164f64`, `0x90` register pass-through with `has_regs`),
`DisplacePrefix` (OLD crys `RunDisplaceInt :5809-5952`, `RunDisplaceFloat :5953-6065` — needed by skn, reused by
5D″), `IndexedDisplace` (`:3788-3989`, bw link 0x335 / cw 0x336, fan 0x1be8), `ZeroKick` (`:4055-4203`,
`:4310-4657`, `:4703-4988`; classes a/b/c + `c_cont` @0x1820/0x1ac8 + `a_cont` @0x1750; `has_regs` with the MAC/
status pushes — check `VuFloat::MacFlags1/PackMacBits` for `MacLane/ApplyMac`), `SpriteText` (`:5110-5315`,
entries 0x1748/0x1980, `has_regs`). Then replace `Vu1Library::DeclineStub` IN PLACE on the rows whose trailing
comment names `skn::`, `idx::`, `zk::`, `spr::` (the row order never moves; edit `/tmp/p5/5D/gen_table.py`'s `FN`
map and regenerate, or edit the rows). Gate: `python3 /tmp/p5/5D/run_new_hle_group.py /tmp/p5/5D/cases_group_B.txt
/tmp/p5/5D/new_hle_B.log` → `same=54 diff=0`; extend `Vu1HleGroupA`'s `--programs` (or add `Vu1HleGroupB`).
Notes for part 2: zk reads `in.data` read-only (give `QwPtr` a const view rather than `const_cast`); spr writes
`r.VI[n]` directly (`VUregs::VI` is `u16[16]`); `-Wunused-parameter` traps at `stale_w_out` / defaulted `gr`.

**Not done (5D″ = group C):** `CrysPrograms` (`:5386-6066`: `RunCrysEnv1720`, `vs_crys_dual` with the lifted
gate pre-scan NOT written, `vs_crys_displace` over `SkinnedLitStrip::RunScalePhase` / `DisplacePrefix`), the
`0x21b0 → LitTristrip::RunLitCall(m, node, top, out, 0x44B)` link, `vu0_programs` data (`Vu0Registry` mechanism
row of PLAN §2.4 also still open — no NEW consumer yet), the group-C gate (`cases_group_C.txt`, 21 cases), the
full 126-case `--mode hle` identity (G3), ctest `Vu1Hle_<case>` ×126 / `Vu1HleRef_*` (first 8 cases `--hle
--hle-ref --exact`) / `Vu1HleCatalog` rows, README count fix already applied here (223/176/140).

**Integration list (for 5H's checklist):** `app/host` constructs `Vu1HleRuntime(Vu1Library::Get(), arbiter)`, calls
`vu1.dispatch().SetHleHook(&Vu1HleRuntime::Hook, &runtime)` and sets `settings().hle_enabled = true` (the
`PS2_VU1_HLE` winner; `--vu1-library none` leaves it false); the VIF calls `runtime.SetVifTops(tops, itops)` on
every TOPS/ITOPS update; the EE micro-window store path calls `runtime.BumpExternalGeneration()` (OLD
`g_vu1_micro_extgen`, `memory.cpp:433`); the renderer phase subclasses `KickLowering::TryLower` for DrawIR / typed ops.

**Gate status:** build 0 warnings; `LintOneClass: 411 files scanned, 0 findings -> PASS`; `LintEnginePurity: 333
files scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; group A 49/49 identical; catalog diff-empty;
full ctest (`ctest --preset macos-dev`):

```
110/113 Test #110: Vu1ReplayCompareCorpus ..............   Passed  326.97 sec
111/113 Test #111: VuGsPacketCodec .....................   Passed    0.02 sec
112/113 Test #112: Vu1HleMechanism .....................   Passed    0.27 sec
113/113 Test #113: Vu1HleGroupA ........................   Passed    0.94 sec

100% tests passed, 0 tests failed out of 113

Total Test time (real) = 499.89 sec
```

**Next command (5D part 2):** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R "VuGsPacketCodec|Vu1HleMechanism|Vu1HleGroupA"`
(expect 3/3), then write `games/…/vu1_library/{skinned_lit_strip, displace_prefix, indexed_displace, zero_kick,
sprite_text}.{h,cpp}` from OLD `hle/vu1_hle_programs.cpp:3016-5361` per FACTS §C.8–C.11, add them to the library's
CMake, swap the table rows' `DeclineStub`, and run the group-B driver.

### 5D part 2 (group B: skn / idx / zk / spr) (2026-09-28, ≈45 min)

FACTS: `/tmp/p5/5D/FACTS.md` §C.8–C.11 (skn / idx / zk / spr), §C.12 (the displace passes skn needs), §C.1 (helper
map), §E (tool grammar), §G (`cases_group_B.txt` = 54 cases; reference `/tmp/p5/5D/old_hle_all.log`), §H.4 (-Werror
traps). Every OLD anchor below is a `host_app/hle/vu1_hle_programs.cpp` line range re-read this session. Pattern = part
1's exactly: one class per pair, `Run(const Vu1HleInput&, Vu1HleOutput&)` registered by pointer, arithmetic transcribed
statement for statement (`std::fma` on floats → `std::fmaf`, `CF()` placement intact), `VuChopRoundGuard` where OLD
had `ChopGuard`, `WhyNote…; return false;` → `return false;`.

### Landed (files)

| New file (lines) | Class / content | OLD source transplanted |
|---|---|---|
| `games/…/vu1_library/library_common.h/.cpp` (76 → 87, 109 → 141) | + `ApplyMac(mac, status, mac_bits)` (MAC replaces, STATUS Z/S/U/O rewritten from the nibbles, sticky 6-9 OR'ed, D/I + DS/IS untouched), `PushMac(mac, status, raw[4], mask)` (= `VuFloat::PackMacBits` + `ApplyMac` — the ONE copy of OLD's `zk::ApplyMac` / `MacQuadXYZW` / `spr::PushMac` / `crys::ApplyMacQuad` / the two `push` lambdas), `DivQ(status, num_bits, den_bits)` (the FDIV commit: `den == 0` → sign-xor MAX with I\|IS (num 0) or D\|DS; else `CF(num/den)`; bits 4-5 replace, 10-11 OR — the ONE copy of the two `div_q` lambdas) | `:4059-4087`, `:5113-5121`, `:5392-5414`, `:4441-4455`, `:4805-4819` |
| `games/…/vu1_library/displace_prefix.h/.cpp` (27 + 228) | `DisplacePrefix` — `kIntEntryPc 0x1400` / `kFloatEntryPc 0x15a0`; `RunDisplaceInt` (XTOP vi13, QW7 bank cursor MR32 + store-back, per group: ITOF0 vertex pair × QW[TOP+2].xy broadcast, ITOF0 bank row × row8.xyz then .w, ADD into the first vertex; every FMAC = raw result → `PushMac` → clamped store; `TPC 0x1598`), `RunDisplaceFloat` (vi13 inherited, one vertex per group, `TPC 0x1720`); both `has_regs`, `kLoopBound 4096`, own `EmitDataWrites` | `crys::RunDisplaceInt :5809-5952`, `RunDisplaceFloat :5953-6065` (moved into part 2 per P5-5D-SKN-NEEDS-DISPLACE; 5D″ reuses them) |
| `games/…/vu1_library/skinned_lit_strip.h/.cpp` (43 + 584) | `SkinnedLitStrip` — `Run` dispatching on entry pc (`0x1400`/`0x15a0` → `DisplacePrefix` BEFORE the guard as OLD; `0x12a0` → `RunScalePhase(nullptr)`; `0` → beacon + node walk ≤16 over `0xcb0`/`0xb30`/`0x1040` (with `in.regs->VF[19].I[3]`)/`0x1720`/`0x1aa0` only for image `0x98164f645013a94d`, else decline; `0x90` → `out.regs = *in.regs; TPC = 0xA0; has_regs`; else decline), `RunScalePhase` (EXPORTED for 5D″; the mod-4/8/12/0 stale-`vf19.w` rule), `Bone`/`LoadBone` (QW[idx+13..16], anonymous namespace, no member functions), `RunSkinCb0` (weights `& 0x1FF` palette / `& 3` count; the cnt 1 / 2 / 3 accumulation orders incl. the `CF(v31)*1.0` and `CF(two)*1.0` re-entries; MULw copy stores, `ADDw.z = 1.0`, `+0`-normalised normal, sext16 ADC), `RunSkin1040` (tr-first order, chained bones, raw-bit stores with `.z = 0x3F800000`, `.w = entry vf19.w` / original `wq.w`), `RunClipMark0B30` (rows QW[head+89..93], `XformClip` + `GuardScale` + `ClipNf != 0`, `((cur\|h1\|h2) + 0x7FFF) \| orig` sext16), `RunSkinnedLitCall` (rows QW[f+78..88], `Fmax_raw` vs +0, `Fmin_raw` 255, own Q, `RotateQw1` BEFORE the two unconditional kicks), `RunSkinEnv1aa0` (the `0x4e7e0000`/`0xbf000000` magic constants, `x²+z²+y²` order, ITOF0/FTOI0 rsqrt trick, `2·dot·n − d`, env rows QW[f+89..91] z/y/x order, MINI 1.0 / MAX +0 raw, kicks THEN rotation) | `:3024-3056`, `:3057-3068`, `:3070-3182`, `:3183-3279`, `:3280-3303`, `:3304-3408`, `:3409-3563`, `:3564-3648` |
| `games/…/vu1_library/indexed_displace.h/.cpp` (35 + 187) | `IndexedDisplace` — `RunBroadcast` (`componentwise=false`, link `0x335`) / `RunComponentwise` (`true`, `0x336`) over `Run` (entry 0, beacon, 2 `clip_image_ok` hashes, fan `0x1be8` kind 0, node walk: `0x90` break / `0x1720` / else decline), `RunIndexPrepass` (`kScaleImmediate 0x39c38f36`, `f2bits(sc) & 15` lane gather from QW[node+3+idx], the 1-deep pending store, `fmaf(pos, 1, ACC)`), `RunIdxCall` (mesh ≤64 masked `& 0xFFFF`, beacon-gated pre-pass then `QW9.x = 0`, gate flag → `ClipGateB0` with the body's vi09/vi13/obj pins, `(low16(pos.w) & flag) \| low16(aux.w)` ADC, node packet then main kick, `RotateQw1`) | `:3792-3835`, `:3836-3924`, `:3925-3987` |
| `games/…/vu1_library/zero_kick.h/.cpp` (38 + 730) | `ZeroKick` — `RunA/RunB/RunC` over private `RunStub(in, out, cls)` (read-only data via the working copy — no `const_cast`; the ONE explicit data write `{0x90, {1,0,0,0}}`; vi14/vi12/vi13/vi15 = `0x0A`; empty list → `TPC 0xA0`; class A LQ 82/81/80/79/83 → `TPC 0x1750`; class B `vi13 = node+1, vi12 = 0`, LQ node+3/4 → `0x1748`; class C the 12 `kSubRegs` zero-fills each `ApplyMac(0x000F)`, the constant loads `0x47000000`/`0x427b999a`/`0x477fff00`/`0x45d20000`, `ADDx vf07` input-dependent push, MR32 shuffles, `vi11 = 1023`, `vi04 = TOP`, `I = 0x45d20000` → `TPC 0x1820`), `RunCCont` (entries `0x1820` / `0x1ac8` (+`vi13 = vi14 + 4`); the lambda set `sub_lane/add_lane/mul_lane/vec_add/vec_sub/vec_mul/mul_bc/mula_bc/madda_bc/madd_bc/div_q/sq/lq` transcribed verbatim (A-form bc RAW, dest-form bc CLAMPED, `std::fmaf` accumulation), the three pinned FSAND producers `f1870/f1900/f1950`, the IBGTZ full-quad vs degenerate paths, guards 256/16/64, `TPC 0x1ac8`), `RunACont` (entry `0x1750`; `itof12`, `RINIT` = `(vf00.w & 0x7FFFFF) \| 0x3F800000`, the `q1..q4` FDIV-pipeline consumption order, the same-bundle MR32-before-MADDz and SQ-before-MAX hazards, degenerate `vf06` stores, `TPC 0x19e0`); all `has_regs` | `VsZeroKick :4090-4197`, `vs_zero_kick_c_cont :4312-4656`, `vs_zero_kick_a_cont :4705-4987` |
| `games/…/vu1_library/sprite_text.h/.cpp` (29 + 196) | `SpriteText` — `Run` (entries `0x1748` / `0x1980`; `r.VI[n]` written directly — no `reinterpret_cast`; the header over the class-B stub's entry vi12/vi13/vf01/vf02; per-A-entry prologue with the first-entry-only `LQI vf05`; glyph body with `PushMac` masks `0xC/0x2/0x1/0x4/0x8/0x8/0x4/0x8` in program order, the same-bundle OLD-vf09 XYZ2 A store, the `goto countdown` resume; kick block `tag = sext16(0x8000 \| (20 − remaining))` into `vf02.x`, phase `(vi12 + 1) & 1`, `KickAt(vi01)`; guards 512/8192/128; `TPC 0x1980`, `has_regs`) | `:5122-5314` |
| `games/…/vu1_library/vu1_library.cpp` (256 → 261) | 154 rows swapped IN PLACE from `DeclineStub` to `&SkinnedLitStrip::Run` (40), `&IndexedDisplace::RunBroadcast` (6) / `RunComponentwise` (3), `&ZeroKick::RunC` (15) / `RunA` (18) / `RunB` (34), `&ZeroKick::RunCCont` (18), `RunACont` (3), `&SpriteText::Run` (17); the 10 `crys::` rows stay `DeclineStub`; order untouched (`static_assert` 223 holds) | the same registration sites as part 1 |
| `games/…/vu1_library/CMakeLists.txt` (+5) | `displace_prefix.cpp skinned_lit_strip.cpp indexed_displace.cpp zero_kick.cpp sprite_text.cpp` in `ps2r_title_vu1_library` | — |
| `games/…/vu1_library/README.md` | + 5 file rows, the group-B table rows with class / part / the 54 oracle case ids (and the corpus-unproven arms named), provenance anchors | — |
| `tests/vu/CMakeLists.txt` (+8) | ctest `Vu1HleGroupB` = `hle_gate.py --programs vs_skinned_lit_strip,vs_indexed_displace,vs_zero_kick,vs_zero_kick_cont,vs_zero_kick_a_cont,vs_sprite_text` (SKIP 77 without the corpus) | — |

Placement decisions: class names `SkinnedLitStrip` / `IndexedDisplace` / `ZeroKick` / `SpriteText` / `DisplacePrefix`
(the brief's `*Program` suffix would force `*_program.h` file names under the lint's snake_case rule — P5-5D-CLASS-NAMES
applied as in part 1); `ApplyMac/PushMac/DivQ` live in `vu1lib` (games) because the engine's `FlagPipeline::
ApplyMacToStatus` is an instance method over `VUregs` and the library threads `mac/status` locals — same bits, one
copy in the library; zk's read-only stub uses `WorkingCopy` (never written) instead of a const `QwPtr` view — reads are
identical and `EmitDataWrites` is not called there, so the data-write list stays OLD's single explicit write.

### Flags applied

None new in this group (no `getenv` sites in `:3016-5361`); `PS2_VU1_HLE_WHY` decline notes not written (DELETE),
as in part 1. `getenv sites: 1 -> PASS`.

### Tests added (ctest names)

```
$ ctest --preset macos-dev -R "VuGsPacketCodec|Vu1HleMechanism|Vu1HleGroup"
3/4 Test #113: Vu1HleGroupA .....................   Passed    0.99 sec
4/4 Test #114: Vu1HleGroupB .....................   Passed    0.96 sec
100% tests passed, 0 tests failed out of 4
$ python3 tools/vu1_replay/hle_gate.py --bin build/tools/vu1_replay/vu1_replay --cases .../vutrace_cases2 --programs vs_skinned_lit_strip,vs_indexed_displace,vs_zero_kick,vs_zero_kick_cont,vs_zero_kick_a_cont,vs_sprite_text
HLE-GATE cases=126 judged=54 pass=54 fail=0 unregistered=2 other_programs=70
```

Full run (`ctest --preset macos-dev`, `/tmp/p5/5D/ctest_full_p2.log`):

```
112/114 Test #112: Vu1HleMechanism .....................   Passed    0.01 sec
113/114 Test #113: Vu1HleGroupA ........................   Passed    0.85 sec
114/114 Test #114: Vu1HleGroupB ........................   Passed    0.79 sec

100% tests passed, 0 tests failed out of 114

Total Test time (real) = 492.12 sec
```

### Oracle / gate status

```
$ cmake --build --preset macos-dev -j4        # 0 warnings (-Wall -Wextra -Werror), /tmp/p5/5D/build_p2.log
$ python3 /tmp/p5/5D/run_new_hle_group.py /tmp/p5/5D/cases_group_B.txt /tmp/p5/5D/new_hle_B.log
HLE cases=54 same=54 diff=0                    # skn 16 / idx 13 / zk 17 / spr 8 -- every stdout line + exit code == old_hle_all.log
$ python3 /tmp/p5/5D/run_new_hle_group.py /tmp/p5/5D/cases_group_A.txt /tmp/p5/5D/new_hle_A2.log
HLE cases=49 same=49 diff=0                    # group A unchanged
$ python3 /tmp/p5/5D/run_new_hle_group.py /tmp/p5/5D/cases_group_SKIP.txt /tmp/p5/5D/new_hle_SKIP2.log
HLE cases=2 same=2 diff=0
$ python3 /tmp/p5/5D/run_new_hle_group.py /tmp/p5/5D/cases_group_C.txt /tmp/p5/5D/new_hle_C.log
HLE cases=21 same=0 diff=21                    # expected: 20 x `HLE FALLBACK: vs_crys_dual declined image 8980533e` + 1 x `vs_crys_displace ... 2e48aa24 (entry 0x1400)`, exit 1 (DeclineStub until 5D″); image= headers identical
$ verify/run_bounded.sh 600 /tmp/p5/5D/new_catalog_p2.rb.log -- build/tools/vu1_replay/vu1_replay --catalog .../vutrace_cases2
$ diff <(grep -v '^\[VU-MODE\]' /tmp/p5/5P/old_catalog.log) <(grep -v '^\[VU-MODE\]\|^\[run_bounded\]' /tmp/p5/5D/new_catalog_p2.rb.log) && echo "CATALOG DIFF EMPTY"
CATALOG DIFF EMPTY                             # all 63 rows incl. the C rows (the hle column prints the table name); footer cases=126 images=63 hle_covered=61/63 bundle_coverage=100.00%
$ build/tools/vu1_replay/vu1_replay --registry
REGISTRY entries=223 distinct_variant_hashes=176
$ python3 tests/lint/lint_one_class.py --root .    -> LintOneClass: 421 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . -> LintEnginePurity: 333 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py        -> getenv sites: 1 -> PASS
$ grep -rnE "0x1720|0x090E0003|exit_stubs\s*=|0xA0\b|vs_(family|textured|poscolor|lit|skinned|indexed|zero_kick|sprite|crys)|0x[0-9a-f]{16}" engine/vu engine/gif tools/ | wc -l
0                                              # G8
```

Scratch proof of `DisplacePrefix::RunDisplaceInt` (its only corpus capture is `vu1_case_crys_2e48aa24`, entry 0x1400,
whose table row is a 5D″ `vs_crys_displace` row): the row was pointed at `RunDisplaceInt` in a scratch build, the case
printed `HLE PASS: vs_crys_displace vs interp -- writes A=0 B=0 … data=ok regs=ok direct=n/a …` identical to OLD, then
the table was restored from `/tmp/p5/5D/vu1_library.cpp.bak` and rebuilt (`grep -c DisplacePrefix vu1_library.cpp` = 0;
the case is back to `HLE FALLBACK`, groups A/B re-run 49/49 + 54/54). `RunDisplaceFloat` has no corpus capture.

### Findings (→ plan §7.1)

- `P5-5D-GROUPB-COVERAGE`: the 54 group-B captures exercise skn ONLY through the `0xcb0 → 0x1720 → 0x90` chain
  (`RunScalePhase` with the stale-w rule, `RunSkinCb0`, `RunSkinnedLitCall`), zk ONLY at entry 0 (classes A/B/C), spr
  ONLY at `0x1748`, idx both parameterizations. Transcribed but corpus-UNPROVEN here (as in OLD, which proved them from
  interpreter traces): `RunSkin1040`, `RunClipMark0B30`, `RunSkinEnv1aa0`, the skn `0x12a0` / `0x90` entries,
  `ZeroKick::RunCCont` (`0x1820`/`0x1ac8`), `RunACont` (`0x1750`), the spr `0x1980` re-entry, `DisplacePrefix::
  RunDisplaceFloat`. They are exercised live at 5H; any divergence surfaces as an `HLE FALLBACK`/mismatch there, never
  silently (the runtime's `has_regs` install is all-or-nothing).
- `P5-5D-SKN-NEEDS-DISPLACE` (closed): `DisplacePrefix` landed in part 2 with skn; 5D″'s `vs_crys_displace` calls
  `SkinnedLitStrip::RunScalePhase` (0x12a0) / `DisplacePrefix::RunDisplaceInt` (0x1400) / `RunDisplaceFloat` (0x15a0).
- `P5-5D-MAC-HELPERS-GAMES`: `ApplyMac/PushMac/DivQ` are generic VU rules but live in `vu1lib` (games) because the
  engine exposes them only as `FlagPipeline` instance methods over `VUregs`; an engine `VuFloat::ApplyMacToStatus(u16&,
  u16&, u16)` static would let the library drop its copy — deferred (bit-identical either way).
- `P5-5D-CLASS-NAMES` (re-applied): `SkinnedLitStrip` / `IndexedDisplace` / `ZeroKick` / `SpriteText` /
  `DisplacePrefix`, not the brief's `*Program` names (file-name lint rule).
- `P5-5D-ZK-READONLY-COPY`: the zero-kick stub reads data memory through the 16 KB `WorkingCopy` instead of a const
  `QwPtr` view (a second `struct` in `qw_ptr.h` would be a second primary type); one memcpy per stub run.

### HANDOFF (5D part 2 → 5D part 3 / 5D″)

**Done:** `DisplacePrefix`, `SkinnedLitStrip` (all six subs + the entry dispatch), `IndexedDisplace` (bw/cw),
`ZeroKick` (A/B/C stubs + both continuations), `SpriteText`; `vu1lib::ApplyMac/PushMac/DivQ`; 154 table rows swapped in
place; ctest `Vu1HleGroupB`; README rows. Group B 54/54 line-identical to OLD, group A still 49/49, SKIP 2/2,
`--catalog` diff-empty, `REGISTRY entries=223 distinct_variant_hashes=176`. Tree GREEN (build 0 warnings, ctest 114/114,
both lints PASS, getenv 1, G8 0). No commits (the user commits).

**Not done (5D part 3 = group C):** `crys_programs.{h,cpp}` (`CrysPrograms`: `ApplyMacQuad` → use `vu1lib::PushMac`;
`RunCrysEnv1720 :5415-5636` with `kMeshRingBound`, gate link `0x306`, node header kick at node+5, the env-mapped ST
channel, declines at `:5443, :5449, :5467, :5475, :5626` → `return false`; `vs_crys_dual :5637-5765` — beacon, the
`!CrysGateLiftEnabled()` pre-scan `:5671-5700` NOT written (NORM lift), node calls `0x1720 → RunCrysEnv1720`, `0x21b0 →
LitTristrip::RunLitCall(m, node, top, out, 0x44B)`, else decline; `vs_crys_displace :5767-5801` — `0x12a0 →
SkinnedLitStrip::RunScalePhase(m, in.top, nullptr)` on its own working copy + `EmitDataWrites`, `0x1400 →
DisplacePrefix::RunDisplaceInt`, `0x15a0 → RunDisplaceFloat`, else decline; `SeedGateRegs(in, 0x1D10, image_ok)` with
fan_kind 3 for `NearClipper::EmitCrysClipFan`). Swap the 10 `crys::` rows (2 `vs_crys_dual`, 8 `vs_crys_displace`) in
place. Gate: `python3 /tmp/p5/5D/run_new_hle_group.py /tmp/p5/5D/cases_group_C.txt /tmp/p5/5D/new_hle_C.log` →
`same=21 diff=0`; add ctest `Vu1HleGroupC` (`--programs vs_crys_dual,vs_crys_displace`) or fold A+B+C into one
`--all` gate; then the full 126-case identity (G3): every `--hle --skip-unregistered` line + exit code == `old_hle_all.log`
(124 PASS + 2 × 77), `Vu1Hle_<case>` ×126 / `Vu1HleRef_*` (first 8 cases `--hle --hle-ref --exact`) / `Vu1HleCatalog`
ctest rows per FACTS §F, `vu0_programs` data + `Vu0Registry` (PLAN §2.4 row, no NEW consumer yet), PLAN §7.1 rows for the
findings above.

**Integration list (unchanged from part 1):** `app/host` constructs `Vu1HleRuntime(Vu1Library::Get(), arbiter)`,
installs `SetHleHook`, sets `hle_enabled = true`; the VIF drives `SetVifTops`; the EE window drives
`BumpExternalGeneration`; the renderer phase subclasses `KickLowering::TryLower`.

**Gate status:** build 0 warnings; `LintOneClass: 421 files scanned, 0 findings -> PASS`; `LintEnginePurity: 333
files scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; G8 grep 0; group A 49/49, group B 54/54, SKIP 2/2,
catalog diff-empty; `ctest --preset macos-dev`: `100% tests passed, 0 tests failed out of 114` (`Total Test time (real)
= 492.12 sec`).

**Next command (5D part 3):** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R "Vu1HleGroup"`
(expect 2/2), then write `games/…/vu1_library/crys_programs.{h,cpp}` from OLD `:5386-5801` per FACTS §C.12 (reusing
`LitTristrip::RunLitCall`, `SkinnedLitStrip::RunScalePhase`, `DisplacePrefix`, `NearClipper::EmitCrysClipFan`,
`vu1lib::PushMac`), add it to the library's CMake, swap the 10 `crys::` rows, run the group-C driver.

### 5D part 3 (group C, VU0 table, gates) (2026-09-28, ≈45 min)

FACTS: `/tmp/p5/5D/FACTS.md` §C.12 (crys), §C.13 (the table), §D (the VU0 registry), §E (tool grammar), §F (OLD ctest
rows), §G (`cases_group_C.txt` = 21 cases; reference `/tmp/p5/5D/old_hle_all.log`), §H.3 (the G8 literal list). No
`/tmp/p5/5D/REVIEW.md` existed at start (no review fixes to apply). Every OLD anchor below is a
`host_app/hle/vu1_hle_programs.cpp` line range re-read this session; pattern = parts 1–2 (`Load` lambda, `WorkingCopy`,
`EmitDataWrites`, `RotateQw1`, `KickAt`, `std::fmaf` at every fused step, statement-for-statement arithmetic).

### Landed (files)

| File | Class | Content / OLD source |
|---|---|---|
| `games/…/vu1_library/crys_programs.h/.cpp` (38 + 258) | `CrysPrograms` | `RunCrysEnv1720` (`:5415-5632`: mesh ring walk under `kMeshRingBound`, gate flag → `ClipGate::ClipGateB0(…, 0x306, …)`, the env-mapped ST channel with the 4-lane `end_stp` leftover, RGBA lighting chain, ADC sext, node-header kick at node+5 when `node.w != 0`, strip kick, `RotateQw1`, `gr->vf16/vf18/vf27` export; the five `WhyNotef(...); return false;` declines `:5443, :5449, :5467, :5475` → `return false`, the ring-exhaustion `ProbeMeshRing` probe `:5626-5631` → `return false`); `RunDual` = `vs_crys_dual :5637-5765` (beacon, ONE `GateRegs` seeded `SeedGateRegs(in, 0x1D10, image == 0x8980533e9df9f1fa)` fan_kind 3, node walk `0x1720 → RunCrysEnv1720`, `0x21b0 → LitTristrip::RunLitCall(m, node, top, out, 0x44B, &gr)` with fan 0x2628 / kind 1, unknown → decline, self-loop → decline, `EmitDataWrites`); `RunDisplace` = `vs_crys_displace :5767-5801` (`0x12a0 → SkinnedLitStrip::RunScalePhase` on the working copy + `EmitDataWrites`; `0x1400 → DisplacePrefix::RunDisplaceInt`; `0x15a0 → RunDisplaceFloat`; else decline). `ApplyMacQuad :5392-5414` not re-written (it is `vu1lib::PushMac`, used inside `DisplacePrefix` since part 2) |
| `games/…/vu1_library/vu1_library.h/.cpp` | `Vu1Library` | the 10 `crys::` rows swapped in place (`vs_crys_dual` ×2 → `&CrysPrograms::RunDual`, `vs_crys_displace` ×8 → `&CrysPrograms::RunDisplace`); `DeclineStub` REMOVED (every row now binds its program; header comment finalised); `static_assert(223)` kept |
| `games/…/vu1_library/vu0_programs.h/.cpp` (16 + 54) | `Vu0Programs` | the VU0 macro-program census table as data: 15 entries (`vu_function_registry.cpp:7-64` + `:186` — 1 HLE `0x00f0` libsin/cos whose fingerprint pair is `Pair(LibVu0SinCos::kFingerprint[0..1])`, 13 UNIMPL incl. `0x0790` with its captured fingerprint `0x01e329bc8000033c/0x01e330bd8000033c`, 1 INTERP VU1 dispatcher) + 1 subroutine (VU1 `0x1720` setup pass); `Register(Vu0Registry&)`, `Entries(n)`, `Subroutines(n)` |
| `engine/vu/vu0/vu0_registry.h/.cpp` (76 + 59) | `Vu0Registry` | mechanism only (`vu_function_registry.h:39-80`, `.cpp:66-88`, census `vu.cpp:5395-5520`): `Kind {Hle, Interpreted, Unimplemented}`, `Descriptor`, `AddEntry/AddSubroutine`, `Lookup/LookupSubroutine`, `NoteEntry(vu, pc, micro, size) → Note {Event FirstKnown/FirstUnknown/Periodic(1k/10k/100k/1M)/None, fingerprint_mismatch (once per (vu, pc)), call_number, live_lo/hi}`, `KindName`. Zero title rows; no logger dependency (the caller turns a `Note` into `PS2R_LOG(VU, …)`) |
| `tools/vu1_replay/vu1_replay.cpp` | — | `--registry` completed from the table: after the pinned first line, one row per program name in first-registration order (`entries= distinct_hashes= entry_pcs=`) and `REGISTRY programs=12` |
| `tools/vu1_replay/hle_vs_old.py` (114; = `/tmp/p5/5D/hle_vs_old.py`) | — | the G3 driver: NEW `--hle --skip-unregistered` per case vs the OLD `### CASE/### EXIT` block (log) or a bounded OLD re-run (cwd pinned); `SAME|DIFF <case>` + `HLE cases=N same=S diff=D`; exit 0/1/77 |
| `tools/vu1_replay/catalog_vs_old.py` (72) | — | the G4 driver: OLD (log or bounded run) vs NEW `--catalog`, `[VU-MODE]` skipped, unified diff empty → `CATALOG DIFF EMPTY (66 lines)`; exit 0/1/77 |
| `tests/vu/check_vu1_library_purity.py` (74) | — | the G8 gate as a file: family literals (`0x1720`, `0x090E0003`, `exit_stubs =`, `0xA0`, `kVariantBase`), every `vs_<family>*` stamp, and EVERY 16-hex hash literal found under the library directory (207: the 176 table hashes + the image guards) over `engine/` + `tools/` → 0 hits |
| `tests/vu/test_vu0_registry.cpp` (125) | — | `Vu0RegistryCensus` (table shape 15/1, kind census 1/1/13, lookup per unit, libsin/cos fingerprint == `LibVu0SinCos::Matches` image, first/periodic/unknown events, mismatch once, 0/0 unchecked, image-end guard) |
| `tests/vu/CMakeLists.txt` | — | rows `Vu1HleGroupC`, `Vu1ReplayHleCorpus` (TIMEOUT 1800, SKIP 77), `Vu1CatalogIdentity` (SKIP 77), `Vu1LibraryPurity`, `Vu0RegistryCensus`, `Vu1HleRef_vu1_case_000{0..7}_1748` (`--hle --hle-ref --exact`, registered when the corpus exists) |
| `games/…/vu1_library/CMakeLists.txt`, `engine/vu/CMakeLists.txt` | — | `crys_programs.cpp`, `vu0_programs.cpp`; `vu0/vu0_registry.cpp` |
| READMEs | — | `games/…/vu1_library/README.md` (crys + vu0 rows, per-program entry / distinct-hash / entry-pc counts from `--registry`, the 126-case identity statement, the crys oracle case ids, DeclineStub text removed, the NORM flag text), `engine/vu/README.md` + `engine/vu/vu0/README.md` (`Vu0Registry` row + provenance), `tools/vu1_replay/README.md` (`--registry` rows, `hle_vs_old.py`, `catalog_vs_old.py`) |

### Flags applied

- `PS2_VU1_CRYS_RING` → NORM (ON): the crystal mesh walk is data-driven under `vu1lib::kMeshRingBound = 4096` (the
  named constant with its PS2 rule, 4× the 1024 physical quadwords); the `MeshRingBound()` reader `:251-258` and the
  fixed-64 arm are not written.
- `PS2_VU1_CRYS_GATE` → NORM (ON): the gate LIFT is the only arm — the `!CrysGateLiftEnabled()` pre-scan decline
  `:5671-5700`, `CrysGateLiftEnabled :265-271` and `ProbeMeshRing :277-291` are not written.
- `PS2_VU1_HLE_WHY` → DELETE (as parts 1–2): every `WhyNotef(...); return false;` is `return false;`.
- The VU0 census `g_logFile` lines (`[VUn-FN-FIRST]` / `-CALLS` / `-UNKNOWN` / `-FINGERPRINT-MISMATCH`) → DIAG-shaped
  mechanism: `Vu0Registry::NoteEntry` returns the event; no logging is emitted by the engine module (no caller yet).

### Tests added (ctest names)

Targeted run after the build (`ctest --preset macos-dev -R "Vu1HleGroupC|Vu1CatalogIdentity|Vu1LibraryPurity|Vu0RegistryCensus|Vu1HleMechanism|VuGsPacketCodec"`):

```
1/6 Test #111: VuGsPacketCodec ..................   Passed    0.01 sec
2/6 Test #112: Vu1HleMechanism ..................   Passed    0.27 sec
3/6 Test #115: Vu1HleGroupC .....................   Passed    1.63 sec
4/6 Test #117: Vu1CatalogIdentity ...............   Passed    0.62 sec
5/6 Test #118: Vu1LibraryPurity .................   Passed    0.92 sec
6/6 Test #119: Vu0RegistryCensus ................   Passed    0.14 sec
100% tests passed, 0 tests failed out of 6
```

Full `ctest --preset macos-dev` (bounded, `verify/run_bounded.sh 1500`):

```
110/127 Test #110: Vu1ReplayCompareCorpus ..............   Passed  332.39 sec
113/127 Test #113: Vu1HleGroupA ........................   Passed    0.99 sec
114/127 Test #114: Vu1HleGroupB ........................   Passed    1.02 sec
115/127 Test #115: Vu1HleGroupC ........................   Passed    0.96 sec
116/127 Test #116: Vu1ReplayHleCorpus ..................   Passed    2.46 sec
117/127 Test #117: Vu1CatalogIdentity ..................   Passed    0.45 sec
118/127 Test #118: Vu1LibraryPurity ....................   Passed    0.76 sec
119/127 Test #119: Vu0RegistryCensus ...................   Passed    0.01 sec
120/127 Test #120: Vu1HleRef_vu1_case_0000_1748 ........   Passed    0.01 sec
121/127 Test #121: Vu1HleRef_vu1_case_0001_1748 ........   Passed    0.01 sec
122/127 Test #122: Vu1HleRef_vu1_case_0002_1748 ........   Passed    0.01 sec
123/127 Test #123: Vu1HleRef_vu1_case_0003_1748 ........   Passed    0.01 sec
124/127 Test #124: Vu1HleRef_vu1_case_0004_1748 ........   Passed    0.01 sec
125/127 Test #125: Vu1HleRef_vu1_case_0005_1748 ........   Passed    0.01 sec
126/127 Test #126: Vu1HleRef_vu1_case_0006_1748 ........   Passed    0.01 sec
127/127 Test #127: Vu1HleRef_vu1_case_0007_1748 ........   Passed    0.01 sec
100% tests passed, 0 tests failed out of 127
Total Test time (real) = 500.24 sec
```

(The first full run of this part hit a one-off 120 s timeout in the Phase-4 row `EeRunTokenAndEventQueue` (untouched
here; 0.08 s in every earlier run); re-run alone 5/5 `Passed 0.07 sec`, and the full run above is clean.)

### Oracle / gate status

Group C first (`verify/run_bounded.sh 600 … python3 /tmp/p5/5D/run_new_hle_group.py /tmp/p5/5D/cases_group_C.txt /tmp/p5/5D/new_hle_C3.log`):
`HLE cases=21 same=21 diff=0` on the FIRST run — every stdout line + exit code identical to `old_hle_all.log`
(20 × `vs_crys_dual` `regs=n/a direct=ok`, chains `0x1720->0x21b0->0x90` incl. the gate-flagged `crysnc*` captures,
e.g. `HLE PASS: vs_crys_dual vs interp -- writes A=241 B=241 vertices A=74 B=74 kicks A=30 B=30 packets A=4 B=4
mismatches=0 data=ok regs=n/a direct=ok (tol xy=1 z=1 rgba=1 st=1e-05)`; 1 × `vs_crys_displace` @0x1400 `regs=ok
direct=n/a`). No root-causing needed.

**G3** — `verify/run_bounded.sh 1200 /tmp/p5/5D/rb_G3.log -- python3 tools/vu1_replay/hle_vs_old.py --new-bin
build/tools/vu1_replay/vu1_replay --cases OLD/vutrace_cases2 --old-log /tmp/p5/5D/old_hle_all.log --old-bin
OLD/build/tests/vu1_replay --old-cwd /tmp/p5/5D/oldcwd --out /tmp/p5/5D/new_hle_all.log --quiet`:

```
HLE cases=126 same=126 diff=0
```

(`new_hle_all.log` = 505 lines like `old_hle_all.log`: 124 `HLE PASS`, 2 `### EXIT … = 77`.) Driver honesty checks:
a 3-case mini corpus with NO log (OLD re-run bounded from `/tmp/p5/5D/oldcwd`) → `HLE cases=3 same=3 diff=0`; a fake
OLD log with a bogus line → `HLE cases=3 same=0 diff=3`, exit 1. The frozen tree's `git status` is unchanged (every
modified path predates the freeze, newest 2026-09-26 00:22).

**G4** — `verify/run_bounded.sh 600 … vu1_replay --catalog OLD/vutrace_cases2`, `[VU-MODE]`/`[run_bounded]` stripped,
`diff /tmp/p5/5D/old_catalog_stripped3.log /tmp/p5/5D/new_catalog_p3.log` → **`CATALOG DIFF EMPTY`** (66 lines; footer
`CATALOG cases=126 images=63 hle_covered=61/63 bundle_coverage=100.00% (corpus-weighted; live weights are in
docs/loop_state/W14_PROFILE.md)`). The ctest driver (`catalog_vs_old.py`, OLD re-run bounded) → `CATALOG DIFF EMPTY (66 lines)`.
Footer counts pinned from the table (`vu1_replay --registry`):

```
REGISTRY entries=223 distinct_variant_hashes=176
  vs_family_tristrip     entries=12  distinct_hashes=12  entry_pcs=0x0
  vs_textured_tristrip   entries=15  distinct_hashes=15  entry_pcs=0x0
  vs_poscolor_strip      entries=15  distinct_hashes=15  entry_pcs=0x0
  vs_lit_tristrip        entries=17  distinct_hashes=17  entry_pcs=0x0
  vs_skinned_lit_strip   entries=40  distinct_hashes=38  entry_pcs=0x0,0x90,any
  vs_indexed_displace    entries=9   distinct_hashes=9   entry_pcs=0x0
  vs_zero_kick           entries=67  distinct_hashes=67  entry_pcs=0x0
  vs_zero_kick_cont      entries=18  distinct_hashes=13  entry_pcs=0x1820,0x1ac8
  vs_zero_kick_a_cont    entries=3   distinct_hashes=3   entry_pcs=0x1750
  vs_sprite_text         entries=17  distinct_hashes=14  entry_pcs=0x1748,0x1980
  vs_crys_dual           entries=2   distinct_hashes=2   entry_pcs=0x0
  vs_crys_displace       entries=8   distinct_hashes=4   entry_pcs=0x12a0,0x1400,0x15a0
REGISTRY programs=12
```

→ PLAN P5-FAMILY-COUNT / P5-5D-ENTRY-COUNT: the true numbers are **223 entries / 176 distinct variant hashes / 12
program names / 15 program functions** (140 was the OLD `Register(` SITE count); the plan's "expect 140/176" is superseded.

**G8** — `python3 tests/vu/check_vu1_library_purity.py --root .` →
`Vu1LibraryPurity: 342 files scanned under engine,tools, 207 library hashes, 0 hits -> PASS` (self-check: `--scan games`
→ 751 hits → FAIL, so the regexes bite). The plan's raw grep
`grep -rnE "0x1720|0x090E0003|exit_stubs\s*=|\b0xA0\b|vs_(family|…|crys)|0x[0-9a-f]{16}" engine/ tools/` returns 4 lines,
ALL generic constants outside the VU1 library's data: `engine/core/hash/README.md:8` + `hash.h:3` (the FNV-1a offset basis
`0xcbf29ce484222325`) and `engine/ee/hle/libc.cpp:122,156,157` (the memset/strlen byte masks `0x0001000100010001`,
`0x0101010101010101`, `0x8080808080808080`) — none is a title hash (P5-5D-G8-GENERIC-HEX); the title-constant hit count is 0.

Lints / gates: `LintOneClass: 428 files scanned, 0 findings -> PASS`; `LintEnginePurity: 335 files scanned, 0 game
tokens -> PASS`; `getenv sites: 1 -> PASS`; build `cmake --build --preset macos-dev -j4` → 0 warnings / 0 errors
(`-Wall -Wextra -Werror`).

**Gate status: G3 ✔ (126/126) · G4 ✔ (diff empty, 223/176 pinned) · G8 ✔ (0 hits).**

### Findings (→ plan §7.1)

- `P5-5D-CRYS-COVERAGE`: the 21 group-C captures exercise `vs_crys_dual` on ONE image (`8980533e`, both subs incl. the
  gate-flagged meshes through `ClipGateB0` link 0x306 and the crystal fan) and `vs_crys_displace` only at entry
  `0x1400` on image `2e48aa24`. The second dual row (variant `0x0e07e0f0c55d22a4` / `0xaf75c3cf33d0a783`) and the
  `0x12a0` / `0x15a0` displace entries are transcribed but corpus-unproven — exactly as in OLD (the `0x12a0` body is the
  corpus-proven skinned scale pass, `0x15a0` the float displace proven only through the skinned images' `any` rows if
  captured). Recorded in the library README's oracle column.
- `P5-5D-G8-GENERIC-HEX`: the plan's `0x[0-9a-f]{16}` grep flags 4 generic constants (FNV basis, libc byte masks);
  `Vu1LibraryPurity` therefore matches the library's OWN hash literals (207, harvested from `games/…/vu1_library/*.{h,cpp}`)
  rather than any 16-hex literal, keeping the gate exact and false-positive-free.
- `P5-5D-REGISTRY-REPORT`: `--registry` grows per-program rows + `REGISTRY programs=12` after the pinned first line
  (the README's per-family counts are generated from it; the first line the module gate pins is unchanged).
- `P5-5D-VU0-CENSUS-NO-CALLER`: `Vu0Registry` + `Vu0Programs` land as data + mechanism with NO NEW consumer (PLAN §2.4
  "PS2R_LOG census only"); the interpreter tier's MSCAL/VCALLMS entry is the intended `NoteEntry` site (HANDOFF row). The
  `FUN_002b18f8` token of the bootstrap row lives in the games table's name string only (engine purity untouched).
- `P5-5D-CTEST-ROWS`: FACTS §F's OLD rows `Vu1Hle_<case>` ×126 are replaced by ONE identity row `Vu1ReplayHleCorpus`
  (line-identity vs OLD is strictly stronger than 126 PASS verdicts) plus the three program-group gates
  (`Vu1HleGroupA/B/C`, fast, no OLD needed); `Vu1HleRef_<first 8>` and `Vu1HleCatalog` (as `Vu1CatalogIdentity`, vs OLD) are
  kept as in OLD.
- `P5-5D-DECLINESTUB-REMOVED`: with every row bound, `Vu1Library::DeclineStub` is deleted (no decline-by-construction
  rows remain; declines are the programs' own unproven-shape verdicts).

### HANDOFF (5D → 5E)

**Done:** the whole VU1 HLE library (12 program names / 15 functions / 223 rows / 176 hashes) bit-identical to OLD on
all 126 corpus cases (G3), `--catalog` identical (G4), title constants confined to `games/…/vu1_library/` (G8 as a
ctest row), the VU0 census mechanism + title table, `--registry` per-program report, ctest rows
`Vu1HleGroupC` / `Vu1ReplayHleCorpus` / `Vu1CatalogIdentity` / `Vu1LibraryPurity` / `Vu0RegistryCensus` / `Vu1HleRef_*`×8,
READMEs. Tree GREEN. No commits (the user commits). Helper processes: none left.

**Not done (moved to 5E / later):** wiring `Vu0Registry::NoteEntry` into the interpreter tier's entry path with a
`PS2R_LOG(VU, …)` emitter (no NEW caller); the `Vu1HleRuntime` TOPS/ITOPS seam is still driven by `SetVifTops` (the VIF
lands at 5E); `ext_gen` stays 0 until the EE window epoch bump exists; `KickLowering::TryLower` stays the raw tier
(Phase 6 subclasses it). Nothing of 5D remains open.

**Integration list (for 5H's checklist, unchanged + 1):** `app/host` constructs `Vu1HleRuntime(Vu1Library::Get(),
arbiter)`, installs `SetHleHook`, sets `hle_enabled = true`; the VIF drives `SetVifTops`; the EE window drives
`BumpExternalGeneration`; the renderer phase subclasses `KickLowering::TryLower`; **NEW:** the VU0/VU1 entry census —
`Vu0Registry reg; Vu0Programs::Register(reg);` in `app/host`, `reg.NoteEntry(id, pc, micro, size)` at each VU program
start → `PS2R_LOG(VU, Info, "[VU%d-FN-…] …")` per `Note.event` / `fingerprint_mismatch`.

**Gate status:** build 0 warnings; `LintOneClass: 428 files scanned, 0 findings -> PASS`; `LintEnginePurity: 335 files
scanned, 0 game tokens -> PASS`; `getenv sites: 1 -> PASS`; `HLE cases=126 same=126 diff=0`; `CATALOG DIFF EMPTY`;
`Vu1LibraryPurity … 0 hits -> PASS`; full ctest quoted above.

**Next command (5E):** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R "Vu1|Vu0"` (expect all
green), then start 5E per PLAN §7 (the VIF: `engine/vif/**` with `tests/vif/**` and `tools/vif_replay/**`), reading
`/tmp/p5/5E/FACTS.md` first.

## 5E — engine/vif + vif_replay

### 5E part 1 (engine/vif + VifUnpackScalar / VifCommands) (2026-09-28, ≈40 min)

Facts: `/tmp/p5/5E/FACTS.md`. Scope = the 5E′ split point of PLAN §7 row 5E plus `vif_fast_span`
and the seam members (all of `engine/vif`); the tool + the two arms are part 2.

### Landed (files)

| File | Class | OLD source transplanted (`host_app/`) |
|---|---|---|
| `engine/vif/vif_regs.h` | types only: `VifRegs` (23 × u32, `static_assert(sizeof == 92)`, frozen `TAG_VIFS` order), `VifRegOffset`/`VifStat`/`VifErr`/`VifFbrst` constexpr, `VifCommand`, `MaskAction`, `VifState{Idle,WaitData,DirectStream}` (the two never-assigned STALL states dropped), `VifUnitState` POD (latch, FIFO deque, the per-unit `current_qw`/`word_index` cursor that OLD kept as file statics, pending fields, `unpack_phase`, `tte_feed`, `direct_buffer`, payload scratch) | `vif.h:106-218, :225-258`; `vif.cpp:917-918` |
| `engine/vif/vif.h/.cpp` | `Vif : MmioHandler` — `Vif(VuUnit& vu0, VuUnit& vu1, GifArbiter&, GifRegs&, VifSettings{fast_span=true, unpack})`; `Reset` (cycle 0x0101, clears every unit field); `ProcessData` (span parser first, word latch as the decline target — not selectable by env; `VifSettings.fast_span=false` is the proof knob the tool needs for the no-flag arm, P5-5E-FAST-DEFAULT); `WriteFifo` (OLD `WriteToLatch`, slot = `(addr & 0xC) >> 2`, submit on slot 3, `latch_fill` untouched); `WriteToLatch2`; `ProcessFifo` (3-state machine verbatim: DirectStream discards the rest of the code word's QW, WaitData drain + lookahead word rule, Idle decode, TTE pad skip `force_align && (4-word_index) < words_needed && tte_feed`, `0x10000` loop cap, trailing `UpdateStat(Idle)`); `ExecuteCommand` for every command per FACTS §2.7 (i-bit → STAT.INT unless ERR.MII at entry; STCYCL full 16 bits; OFFSET VIF1 only → DBF=0, TOPS=BASE; BASE/ITOP/STMOD widths; MSKPATH3 → `GifRegs::SetPath3MaskedByVif`; MARK → MRK; FLUSH* no-ops; STMASK/STROW/STCOL guarded memcpy; MPG → `CtxFor(v).WriteMicroMem(imm*8, …)`; MSCAL/MSCALF/MSCNT → `SwapDoubleBuffer` (`top=tops; itops=itop; if (ofst) tops = DBF ? base : base+ofst; stat ^= DBF`) then `vu.SetVifTop(top, itops)`, `TopsObserver(tops, itops)`, `vu.Execute(imm*8)` / `Execute(regs().TPC)`; VIF0 MSCAL → `vu0_.Execute` with no swap, VIF0 MSCALF/MSCNT no-ops; unknown codes ignored); `SubmitDirect/CompleteDirect` (`arbiter_.submit(Path2, buf, size, pending_directhl)`, INT from `pending_irq`); `UpdateStat` (VPS + FQC `fifo.size()/16` verbatim); `Read/Write` over the full file (`kVif0Base 0x10003800`, `kVif1Base 0x10003C00`, `kRegsSize 0x180`, ROW/COL at +0x100/+0x140 word stride, FIFO window `0x10005000..+0x10` → `WriteFifo(1)`), STAT w1c `0x3F40`, FBRST RST/FBK/STP/STC, ERR & 7, MARK clears MRK, MODE & 3, read-only NUM/CODE/ITOPS/BASE/OFST/TOPS/ITOP/TOP, VIF0 BASE/OFST/TOPS/TOP read 0; the one-line comment recording OLD's routed observable (P5-VIF-MMIO-OBSERVABLE); seam members `CtxFor(int) → VuUnit&` (VU1 → the bound unit), `BindVu1(VuUnit*)`, `SetTteFeed`, `ResetLatch` (latch_fill 0 + latch zero), `IsIdle` (state Idle && latch empty && cursor drained && FIFO empty); `SetTopsObserver` replaces 5D's direct `SetVifTops` drive | `vif.cpp:39-42, :703-830, :831-903, :1229-1866, :1906-2528, :3504-3602`; `vu1_async.cpp:517-519` |
| `engine/vif/vif_unpack.h/.cpp` | `VifUnpack` + `VifUnpackSettings{bulk_lanes=true}` — `kElems/kBits` tables, `IsUnpackCmd`, `GetUnpackSize` (num 0→256, `cl>=wl||cl==0||wl==0` skipping arm, filling `full*cl+min(rem,cl)`, byte round-up to 4), `MaskActionFor` (`bit = (cycle&3)*8 + field*2`), `Execute` = the scalar loop verbatim (TOPS add VIF1 only; cl/wl 0→1; `cycle_pos = i % cl`, skip cycles advance without consuming; V4-5 `(raw&0x1F)<<3 … (raw&0x8000)>>8`; S replicate; V2 `z=x,w=y`, V2-32 W=0 on odd phase; V3 W = peek not consumed, V3-32 W=0 when `(written&1) != phase`; MODE row/col adds with `col_idx = write_cycle & 3`; masked path reads the existing QW through `VuUnit::ReadDataMem`; `dest = (addr_qw*16) & 0x3FFF`; `write_cycle` wraps at WL; `max_iter = n*cl+16`); bulk dispatch with the `On()` and `!g_logFile.is_open()` terms removed (NORM) and the census as plain `uint64_t` | `vif.cpp:168-276, :769-775, :2680-3496` (`:2825-2870`) |
| `engine/vif/vif_unpack_bulk.h/.cpp` | `VifUnpackBulk::Unpack` (lane A: `need = n × {12,16,8,4}`, decline on short payload, runs of `min(n-done, 1024-aqw)`, V4-32 memcpy, V4-16/V4-8 widen, V3-32 `off+16 <= psz` peek + parity zero) and `UnpackSkip` (lane B: stride `cl`, per-vector `& 0x3FF`); NEON bodies under `#if PS2R_SIMD` (`vmovl_u16/vld1_u16`, `vmovl_s16/vld1_s16`, `vcreate_u8 → vmovl_u8 → vget_low → vmovl`, signed twins), scalar bodies otherwise | `vif.cpp:309-311, :420-533, :576-687` |
| `engine/vif/vif_fast_span.h/.cpp` | `VifFastSpan::Process` (friend of `Vif`): decline ladder (`tte_feed`, `size&15`, FIFO/latch non-empty, `word_index<4`), DirectStream continuation, WaitData completion (phase 0, lookahead `(words_needed&3) ? span[words_needed] : 0`) or latch accumulation, Idle loop (DIRECT in-span → immediate PATH2 submit + INT, else stream + latch the tail; zero-payload commands inline; payload crossing the span → WaitData with the partial QW seeded into `current_qw`/`word_index`; full payload → `unpack_phase = s & 1` + lookahead `(e&3) ? span[e] : 0`) | `vif.cpp:967-1227` |
| `engine/vif/CMakeLists.txt` | `ps2r_vif` STATIC → `ps2r_vu ps2r_gif ps2r_ee ps2r_core`, `-Wall -Wextra -Werror -O2 -fno-strict-aliasing -fwrapv`, `PS2R_SIMD` passthrough | — |
| root `CMakeLists.txt` (+1 line after `engine/vu`), `tests/CMakeLists.txt` (+1 line at the end) | `add_subdirectory(engine/vif)      # Phase 5: VIF0/1 command processor + UNPACK (ps2r_vif)`; `add_subdirectory(vif)     # Phase 5: VIF suites` | append-only |
| `engine/vif/README.md` (rewritten: Files table, behaviour notes, `VifMtvuSeam` dropped, `VifUnpackBulk` added), `tests/vif/README.md`, `tests/vif/CMakeLists.txt` (`ps2r_vif_test`, TIMEOUT 120, SKIP 77), `tests/vif/test_vif_unpack_scalar.cpp`, `tests/vif/test_vif_commands.cpp` | | |

No edit to `engine/vu` was needed: `VuUnit::SetVifTop(top, itop)` + `Execute` are the seam and
`vu1_replay` keeps driving `SetVifTop` / `Vu1HleRuntime::SetVifTops` itself (5B/5D decisions stand).

### Flags applied

| Flag | Outcome | What was written |
|---|---|---|
| `PS2_VIF_FAST` | NORM | `ProcessData` always tries `VifFastSpan` first; the word path exists only as its decline target. `VifSettings.fast_span` (default true) is the equivalence-proof knob (the tool's no-flag arm pins the word path, FACTS §4). |
| `PS2_VIF_UNPACK_SIMD` | NORM | lane A always eligible; `VifUnpackSettings.bulk_lanes` is the proof knob (OLD `w10unpkb::OverrideForTest`). |
| `PS2_VIF_UNPKBX` | NORM | lane B likewise (one knob drives both, as OLD's test did). |
| `PS2_UNPKT_CENSUS`, `PS2_VIF_ALIGN_LOG`, `PS2_SHADOW8_KTAP` | DIAG | omitted (not mechanism-shaped: `g_vsync_frame` latch / per-command logs / game address `0x19e0`). |
| `PS2_S8_VIFTAP`, `PS2_VIF_WTRACE` | DELETE | not written (`s8viftap` namespace, `[VIF-WTRACE]`). |
| `PS2_VU1_EXEC_ASYNC` seam (`W13E3_WaitPoint`, `unpack_shadow`, `CommitStagedUnpacks`) | DELETE | not written; FLUSH*/MSCAL*/MPG/DIRECT carry no wait point. |
| `PS2_MTVU` (`Vif_VuCtx`) | NORM (5G) | `CtxFor(1)` returns the bound unit (`BindVu1`), no env. |
| every `g_logFile` / probe block (`[KICKCNT]`, `midc`, `mipsrc`, `[BULLSEYE-DIRECT]`, `[CAMSRC]`, `[STRIPE-DEBUG]`, `[UNPKDST]`, …) | DIAG/DELETE | none written (several read EE game addresses — LintEnginePurity 0). |

### Tests added (ctest names)

```
128/129 Test #128: VifUnpackScalar .....................   Passed    0.01 sec
129/129 Test #129: VifCommands .........................   Passed    0.01 sec
100% tests passed, 0 tests failed out of 129
Total Test time (real) = 492.21 sec
```

`VifUnpackScalar` (`tests/vif/test_vif_unpack_scalar.cpp`): `GetUnpackSize` ×17 (num 0→256, CL<WL
`5,1,2 → 48` / `6,2,4 → 64`, cl 0 arm, byte rounding S-8/S-16/V2-8, undefined 0x3/0x7/0xB); the mask
table on `0x55AAFFE4` (row 0 W/NW/ROW/COL, rows 1-3, `cycle & 3`); hand-computed vectors for every
format × USN (V4-32, V4-16 `FF FF 00 80 FF 7F 01 00 → FFFFFFFF FFFF8000 7FFF 1` / unsigned, V4-8, S-32/16/8,
V2-32 phase 0/1, V2-16/V2-8, V3-32 both phases incl. the lookahead word `0x77` landing in W, V3-16/V3-8,
V4-5 `0x0421FFFF → {248,248,248,0x80},{8,8,8,0}`), MODE 1/2/3 (u32 wrap, COL index wrapping at WL 4 and 2),
MASK ×5 vectors with all four actions + WL=2 alternation + mask-after-mode, CL=4/WL=1 and CL=4/WL=2 skip
shapes (source not consumed, trailing skips never run), CL=1/WL=2 fill (`{0x77,0,0,0}` then zeros —
P5-5E-CLWL-FILL), CL/WL 0 → 1, wrap 1022→0 and skip-lane wrap 1020→0, TOPS 512/1000 on VIF1 and ignored
on VIF0, partial payloads (V4-32, V4-5, V3-32), VU0 masked read through the 4 KB mask
(P5-5E-VU0-MASKED-READ), and 27 lane shapes (11 lane A, 11 lane B, 5 declines) run with
`bulk_lanes` off/on on the `0xA5 + (i & 0xFF)` canvas → byte-identical 16 KB + exact census, plus the
short-payload decline of every lane case.

`VifCommands` (`tests/vif/test_vif_commands.cpp`, rig = `RegisterOpStream ← GifParser ← GifArbiter`,
`VuUnit vu0(0), vu1(1, &arbiter)`, `Vif`): register effects of STCYCL/BASE/OFFSET/ITOP/STMOD/MARK/
STMASK/STROW/STCOL (+ 10-bit / 2-bit widths, VIF0 ignoring BASE/OFFSET/MSKPATH3, `Reset`); MSCAL
(BASE 20 / OFFSET 8 / ITOP 7 → `top 20, itops 7, tops 28, DBF 1`, a real micro reading XTOP/XITOP
into vi1/vi2, TPC 0x18, observer `(28,7)`), MSCALF (second swap `top 28, tops 20, DBF 0`), MSCNT (resumes
at TPC → vi3 = 9, swaps again), OFST 0 (DBF toggles, TOPS stays), VIF0 MSCAL runs VU0 with no swap and
VIF0 MSCALF/MSCNT do nothing, `BindVu1` redirects VIF1 kicks; MPG (2 bundles at 0x20·8, generation +1,
micro wrap 0x3FF8→0, VU0 4 KB mask, num 0 = 2048 bytes); DIRECT/DIRECTHL on both feeds (2-QW PACKED
packet → 3 ops `Tag(path 2) / Rgbaq / Eop`, PendingBytes 32, a transfer split across two feeds through
`DirectStream`, i-bit → INT at completion, ERR.MII masking, MARK after the transfer, i-bit on a NOP);
MSKPATH3 on/off + VIF0 ignored; MMIO (CYCLE/MODE/MASK/ERR/ROW2/COL3 write + read back, read-only
TOPS/NUM/CODE, VIF0 BASE/TOPS/TOP/OFST read 0 with ITOP live, MARK write clears MRK, STAT w1c keeps
MRK, FBRST STP/FBK/STC, FBRST.RST dropping a WaitData UNPACK (VPS Wait → Idle, FIFO cleared), the FIFO
window executing on slot 3); the TTE pad skip (`{NOP, UNPACK V4-32 x1, pad, pad}` bracketed → payload from
the next QW; unbracketed → `{0,0,1,2}`), `ResetLatch` / `IsIdle`; and a 21-QW mixed stream (STCYCL, STMASK,
STROW, V4-32 ×3 at an odd word, MARK, STMOD 1 + V3-32 ×5 with ROW add, MPG + MSCAL, masked V4-32, DIRECT,
STCYCL 4/1 + V3-32 skip lane whose last W is the next code word, V4-8 skip lane) fed whole on both paths and
split at every quadword boundary on both paths (40 two-way + 26 three-way splits) → identical `VifRegs`
×2, VU1 data + micro, ops hash/count, vi1.

### Oracle / gate status

```
$ cmake --preset macos-dev  → exit 0 (Build files have been written to: …/build)
$ cmake --build --preset macos-dev -j4  → exit 0, 0 warnings ([12/13] Linking CXX executable app/host/nativeee)
$ ctest --preset macos-dev  → 100% tests passed, 0 tests failed out of 129  (Vu1ReplayCompareCorpus 331 s, all Vu1*/Vu0* rows green)
$ python3 tests/lint/lint_one_class.py --root .  → LintOneClass: 439 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root .  → LintEnginePurity: 345 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py  → getenv sites: 1 -> PASS
NEON twin (P5-NEON-DEFAULT): cmake -S . -B /tmp/p5/5E/build-simd -G Ninja -DPS2R_SIMD=ON → exit 0;
  --target test_vif_unpack_scalar test_vif_commands → exit 0, 0 warnings; both print PASS
  (VifUnpackScalar's 27 lane shapes then exercise the NEON kernels against the scalar loop).
```

No OLD oracle this part (the `--unpack-bulk` arms are part 2); no OLD binary was run.

### Findings (→ plan §7.1)

| Id | Finding | Disposition |
|---|---|---|
| P5-5E-FAST-DEFAULT | Engine default `fast_span = true` (NORM); the tool's no-flag arm must run the word path (OLD pinned `PS2_VIF_FAST=0`). | `VifSettings.fast_span` set by the tool from `--vif-fast`; not env, not Config. |
| P5-5E-RESET-SCOPE | OLD `Reset()` left latch/cursor/direct buffer/tte/phase; the tool's `reset_case` cleared them. | NEW `Reset()` clears everything (superset); FBRST.RST keeps OLD's narrower scope. README notes it. |
| P5-5E-FQC-DIV16 | `UpdateStat` FQC = `fifo.size()/16` over a quadword deque (16× under-report). | Carried verbatim (`TAG_VIFS` identity). |
| P5-5E-VIF0-MSCALF | VIF0 MSCALF/MSCNT no-ops; only MSCAL kicks VU0; VIF0 UNPACK ignores TOPS. | Carried verbatim; tested. BUG-KB candidate. |
| P5-5E-NUM-STATIC | `VifRegs.num` never updated (hardware counts down). | Carried. |
| P5-5E-UNKNOWN-CMD | Unknown VIFcodes ignored (no ER1/stall). | Carried. |
| P5-5E-ROWCOL-UNREACHABLE | OLD `Read/Write` masked `addr & 0xFF` → ROW/COL dead. | NEW: full file addressable (`kRegsSize 0x180`); the router observable stays OLD's (checklist row 1). |
| P5-5E-CLWL-FILL | CL<WL writes every cycle from the exhausted source (lookahead leak then zeros), no hardware ROW fill. | Carried verbatim; tested (`{0x77,0,0,0}`). BUG-KB candidate (EE UM 6.3.5). |
| P5-5E-VU0-MASKED-READ | OLD read `vu0_data_memory` (4 KB) unmasked at `dest_addr` up to 0x3FF0 (UB). | NEW reads through `ReadDataMem` (masked); tested. |
| P5-5E-ITOP-WORD | `VuUnit` carries (top, itops); `Vu1HleInput.itop` will hold ITOPS. | Unobservable (no library program reads it); `TopsObserver` hands `(tops, itops)` to `SetVifTops`. |
| P5-5E-CTXFOR-TYPE | PLAN §3.5 wrote `Vu1State& CtxFor`; the worker needs the unit (Execute), so `VuUnit& CtxFor(int)` + `BindVu1(VuUnit*)`. | Recorded; 5G may wrap it. |
| P5-5E-VPS-TRANSIENT | Mid-DIRECT STAT.VPS differs between feeds (word path Transfer, span path Idle) exactly as in OLD; identical once the stream ends. | Carried; the feed-equivalence test compares end states only. |
| P5-5E-TEST-LINT | `tests/**` is linted by LintOneClass: one primary type per test TU (`TestVifCommands`), streams as POD + free functions. | Applied. |

### HANDOFF (5E part 1 → 5E part 2)

**Done:** all of `engine/vif` (`ps2r_vif`: regs / unpack / bulk lanes with NEON twins / span parser /
`Vif` incl. every command, MMIO, seam members, `TopsObserver`), `tests/vif` with `VifUnpackScalar` +
`VifCommands`, READMEs, the two append-only CMake lines. Tree GREEN (build 0 warnings, ctest 129/129,
both lints PASS, getenv 1). No commits. Helper processes: none left; `/tmp/p5/5E/build-simd` left
for part 2's `VifUnpackBulkEquivalence` NEON quote.

**Not done (part 2):** `tools/vif_replay/{vif_replay.cpp, CMakeLists.txt, README.md, vif_arms_vs_old.py}`
per FACTS §3.4/§6.4 — `--unpack-bulk [--vif-fast]` (30 generators verbatim, seed `0xC0FFEE ^ index`,
canvas `0xA5 + (i & 0xFF)`, `regs(1).tops = pre_tops`, one `Vif` per leg with
`VifSettings{vif_fast, VifUnpackSettings{force}}`, deltas of `unpack().bulk_cmds()/skip_cmds()`,
`unit(1).state/pending_bytes`, the exact `[UNPKB][%s] PASS (bulk_cmds=%llu skip_cmds=%llu)` / summary
strings on stdout and the header on stderr); `--mtvu`/`--runahead*` parsed → exit 2; root CMake line
`add_subdirectory(tools/vif_replay)   # Phase 5: VIF arm replay tool` after the `tools/vu1_replay` line;
ctest rows `VifUnpackBulkEquivalence`, `VifUnpackBulkEquivalenceFast` (+ `VifArmsVsOld` SKIP 77 without
the OLD binary) in `tests/vif/CMakeLists.txt`; the textual diff against `/tmp/p5/5P/old_vif_arms.log`;
the NEON quote from `/tmp/p5/5E/build-simd`.

**Integration list (5H):** (1) `MmioRouter` registers `Vif` for `[0x10003800,+0x180)`, `[0x10003C00,+0x180)`,
`[0x10005000,+0x10)` while preserving OLD's observable (reads 0 except FBRST/ERR + the MTVU VIF1_STAT view;
writes only FBRST/ERR + the FIFO window); (2) `TAG_VIFS` = `regs(0)`, `regs(1)` raw (92 B each), restore
forces the unit idle, admissibility `IsIdle(0) && IsIdle(1)`; (3) `vif.SetTopsObserver(…SetVifTops…, &runtime)`;
(4) `BindVu1` for the 5G worker; (5) DMAC (5F): `SetTteFeed(1, on/off)` around the TTE quadword,
`ResetLatch(1)` at each chain-walk head, FDR read `regs(1).stat & VifStat::kFdr`; (6) `arbiter.drain()` is
owned by the DMAC / boundary, not by `Vif` (P5-5E-NO-DRAIN).

**Gate status:** GREEN as quoted above.

**Next command (part 2):** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R Vif`
(expect 2/2), then write `tools/vif_replay` per FACTS §3.4 and run
`verify/run_bounded.sh 120 /tmp/p5/5E/new_unpkb.log -- build/tools/vif_replay/vif_replay --unpack-bulk`
against `/tmp/p5/5P/old_vif_arms.log`.

### 5E part 2 (tools/vif_replay + the UNPACK arms vs OLD) (2026-09-28, ≈30 min)

Facts: `/tmp/p5/5E/FACTS.md` §3 (tool), §6.4 (shape), §3.4 (the 30 generators — transcribed and
re-read from `OLD/tests/vif_replay.cpp:1292-1620` today). No `REVIEW.md` existed (first pass).

### Landed (files)

| File | Contents | OLD source transplanted |
|---|---|---|
| `tools/vif_replay/vif_replay.cpp` (386 ln) | `main` + free functions, zero primary types. `--vif-fast` scrubbed from anywhere on the line → `VifSettings.fast_span` (word latch when absent — the OLD `PS2_VIF_FAST=0` pin). `--unpack-bulk` → `RunUnpackBulk`: the 30 `Case`s (`name, expect_bulk, pre_tops, build, lane`) with the generators byte-for-byte (`Code/Word/PadToQw` on a `Stream` POD, the LCG `s_rng = s_rng*1664525+1013904223`, every `| 0x80008000` / `| 0x80808080` / NOP / STCYCL / STMASK / STROW / STMOD prefix as OLD); `RunLeg` builds a FRESH rig per leg (`RegisterOpStream ← GifParser ← GifArbiter`, `VuUnit vu0(0), vu1(1,&arbiter)`, `Vif(…, VifSettings{vif_fast, VifUnpackSettings{force}})`), `Reset()`, `regs(1).tops = pre_tops`, canvas `0xA5 + (i & 0xFF)` via `WriteDataMem`, seed `0xC0FFEE ^ index`, `ProcessData(1, words, n*4)`, deltas of `unpack().bulk_cmds()/skip_cmds()`, `ReadDataMem(0, …, 16384)`, `unit(1).state/pending_bytes`; the six FAIL reasons and the PASS / summary `printf` formats verbatim (`%llu` via `unsigned long long`, `%zu` for the count); header on stderr, case lines + summary on stdout. `--mtvu` / `--runahead` / `--runahead-off` → stderr `vif_replay: <arm> not available until stage 5G`, exit 2. Anything else → the usage text (states the file mode is not ported), exit 2. | `tests/vif_replay.cpp:1295-1307` (Stream/rng), `:1309-1318` (Case), `:1322-1477` (30 generators), `:1479-1511` (kCases), `:1516-1551` (reset_case/run_leg), `:1553-1618` (Run), `:1728-1788` (main) |
| `tools/vif_replay/CMakeLists.txt` | `add_executable(vif_replay vif_replay.cpp)`, `-Wall -Wextra -Werror -O2 -fno-strict-aliasing -fwrapv`, `target_link_libraries(vif_replay PRIVATE ps2r_vif)` — `ps2r_vu ps2r_gif ps2r_ee ps2r_core ps2r_gs_format` arrive as PUBLIC transitive links (naming them again makes ld warn about duplicate libraries; `tools/vu1_replay` precedent) | `tests/CMakeLists.txt:1379-1403` |
| `tools/vif_replay/vif_arms_vs_old.py` (202 ln; copy at `/tmp/p5/5E/vif_arms_vs_old.py`) | The driver: `ARMS` table (6 arms: OLD env pins / OLD args / NEW args / owning stage), `--arms 5E` (default: the two UNPACK arms) / `all` / names; parses the `### ARM <name> env=[…] args=[…]` … `### EXIT <name> = N` blocks of `--old-log` (default `/tmp/p5/5P/old_vif_arms.log`); with `--refresh-old` or a missing log it re-runs the OLD arms bounded (`verify/run_bounded.sh`, `RB_POLL_S=1`, cwd pinned `--out/oldcwd`, the OLD env pins applied to the oracle run only) into `--out/old_vif_arms.log`; runs each NEW arm bounded; normalises both sides (drop `[VU-MODE]` / `[run_bounded]` lines; blank the PLAN §4.2 timing tokens `N busy views`, `N pkts eager-drained`, `(k/N iters)`, `parked Nms`, `at Nms`, `(Nms)` — none occurs in the UNPACK arms, whose lines are compared exactly); requires identical ordered lines AND exit codes; prints `ARM <name> SAME|DIFF old_exit= new_exit= lines_old= lines_new=` (+ first differing lines) and `VIFARMS arms=N same=M diff=K`; exit 0 iff diff 0, 77 when neither OLD log nor OLD binary exists, 2 on unknown arm names. | `/tmp/p5/5P/run_old_vif_arms.sh` (block grammar), `tools/vu1_replay/compare_vs_old.py` (bounded-run shape) |
| `tools/vif_replay/README.md` | purpose / grammar / arms / driver / belongs / OLD provenance (file mode not ported — P5-VIF-ARMS-SYNTHETIC) | — |
| root `CMakeLists.txt` (+1 line after `tools/vu1_replay`) | `add_subdirectory(tools/vif_replay)   # Phase 5: VIF chain replay + equivalence arms tool` | append-only |
| `tests/vif/CMakeLists.txt` (+3 rows) | `VifUnpackBulkEquivalence` (`vif_replay --unpack-bulk`), `VifUnpackBulkEquivalenceFast` (`--unpack-bulk --vif-fast`), TIMEOUT 120; `VifArmsVsOld` (`vif_arms_vs_old.py --new-bin $<TARGET_FILE:vif_replay> --old-bin ${PS2R_OG_ROOT}/build/tests/vif_replay --refresh-old --out ${CMAKE_BINARY_DIR}/tests/vif/vif_arms_vs_old`, TIMEOUT 600, SKIP_RETURN_CODE 77) | `tests/CMakeLists.txt:1433-1434` |
| `tests/vif/README.md` (+2 rows), `engine/vif/README.md` (+ Proofs bullet) | | |

### Flags applied

| Flag | Outcome | What was written |
|---|---|---|
| `PS2_VIF_FAST` (test-side pin `tests/vif_replay.cpp:1743-1744`, read-back `:947`) | TEST → CLI | `--vif-fast` → `VifSettings.fast_span`; the flag's absence forces the word latch exactly as OLD's `=0` pin did. No env is read or set. |
| `PS2_MTVU_MAX_FRAMES` (`:1774/:1779`) | SETTING (5G) | `--depth N` accepted syntactically; the arm exits 2 until 5G. |
| `VIF_REPLAY_RGBDUMP`, `VIF_REPLAY_DUMPW`, `VIF_REPLAY_LOG`, `VIF_REPLAY_TTE2`, `VIF_REPLAY_MTVU_DEBUG` | TEST-ONLY | not written (file mode not ported; 5G decides `--mtvu-debug`). |
| `w10unpkb::OverrideForTest` / `w10unpkbx::OverrideForTest` (`:1541-1542`) | NORM proof knob | `VifUnpackSettings{bulk_lanes = force}` per leg (one knob, both lanes, as OLD forced both). |

`grep -rn getenv tools/vif_replay/` → 0; `python3 tests/core/check_getenv_sites.py` → `getenv sites: 1 -> PASS`.

### Tests added (ctest names)

```
$ ctest --preset macos-dev -R Vif
2/5 Test #129: VifCommands ......................   Passed    0.01 sec
3/5 Test #130: VifUnpackBulkEquivalence .........   Passed    0.01 sec
4/5 Test #131: VifUnpackBulkEquivalenceFast .....   Passed    0.01 sec
5/5 Test #132: VifArmsVsOld .....................   Passed    4.43 sec
100% tests passed, 0 tests failed out of 5
```

### Oracle / gate status

```
$ cmake --preset macos-dev → exit 0;  cmake --build --preset macos-dev -j4 → exit 0, 0 warnings
$ verify/run_bounded.sh 120 /tmp/p5/5E/new_unpkb.log -- build/tools/vif_replay/vif_replay --unpack-bulk         → exit 0
$ verify/run_bounded.sh 120 /tmp/p5/5E/new_unpkb_fast.log -- build/tools/vif_replay/vif_replay --unpack-bulk --vif-fast → exit 0
  unpack-bulk equivalence selftest (vif_fast=0)            (stderr; the fast log says vif_fast=1)
  [UNPKB][v432_unsigned_n7] PASS (bulk_cmds=1 skip_cmds=0)
  … cases 0-10 (bulk_cmds=1 skip_cmds=0), 11-15 (bulk_cmds=0 skip_cmds=0),
    16-26 (bulk_cmds=0 skip_cmds=1), 27-29 (bulk_cmds=0 skip_cmds=0) …
  [UNPKB][skx_mode_cl4_decline] PASS (bulk_cmds=0 skip_cmds=0)
  UNPKB EQUIVALENCE PASS: 30 cases, byte-identical VU1 data memory, must-execute/must-decline census clean
$ python3 tools/vif_replay/vif_arms_vs_old.py --out /tmp/p5/5E/arms_vs_log        (reference = /tmp/p5/5P/old_vif_arms.log)
  ARM VifUnpackBulkEquivalence SAME old_exit=0 new_exit=0 lines_old=32 lines_new=32
  ARM VifUnpackBulkEquivalenceFast SAME old_exit=0 new_exit=0 lines_old=32 lines_new=32
  VIFARMS arms=2 same=2 diff=0
$ python3 tools/vif_replay/vif_arms_vs_old.py --out /tmp/p5/5E/arms_vs_bin --refresh-old   (OLD re-run bounded, cwd /tmp/p5/5E/arms_vs_bin/oldcwd)
  reference remade from the OLD tool: /tmp/p5/5E/arms_vs_bin/old_vif_arms.log
  ARM VifUnpackBulkEquivalence SAME old_exit=0 new_exit=0 lines_old=32 lines_new=32
  ARM VifUnpackBulkEquivalenceFast SAME old_exit=0 new_exit=0 lines_old=32 lines_new=32
  VIFARMS arms=2 same=2 diff=0
negative control: a reference with `v432_wrap … bulk_cmds=2` → both arms DIFF with the old[10]/new[10] lines printed, `VIFARMS arms=2 same=0 diff=2`, exit 1;
  `--old-log /nonexistent --old-bin /nonexistent` → `SKIP: …`, exit 77;  `--mtvu 20` / `--runahead --depth 2` → exit 2; no args / file-mode args → usage, exit 2.
NEON twin (P5-NEON-DEFAULT): cmake -S . -B /tmp/p5/5E/build-simd -G Ninja -DPS2R_SIMD=ON → exit 0; --target vif_replay → exit 0, 0 warnings;
  /tmp/p5/5E/build-simd/tools/vif_replay/vif_replay --unpack-bulk [--vif-fast] (bounded) → both exit 0, both
  `UNPKB EQUIVALENCE PASS: 30 cases, …`; vif_arms_vs_old.py --new-bin <simd tool> → `VIFARMS arms=2 same=2 diff=0`.
$ python3 tests/lint/lint_one_class.py --root .   → LintOneClass: 439 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . → LintEnginePurity: 345 files scanned, 0 game tokens -> PASS
```

**G6 (partial):** ✔ `VifUnpackBulkEquivalence`, ✔ `VifUnpackBulkEquivalenceFast` (the 30 lines + summary
textually identical to OLD on both feeds, scalar and NEON lanes, exit codes identical). Pending 5G:
`VifReplayMtvuEquivalence`, `VifReplayMtvuEquivalenceFast`, `VifRunaheadBound`, `VifRunaheadBoundOff`
(the driver already carries their rows + token exclusions; the NEW tool exits 2 for them).
G1 for this stage: build 0 warnings, both lints PASS, getenv 1, root/tests CMake diffs = the
append-only lines only (`git -P diff CMakeLists.txt tests/CMakeLists.txt` quoted in the session).

### Findings (→ plan §7.1)

| Id | Finding | Disposition |
|---|---|---|
| P5-5E-ARMS-FRESH-RIG | OLD ran every leg on one static `g_vif` + `Reset()` and the global atomics; NEW builds a fresh rig per leg (units, arbiter, `Vif`). The observables (deltas, memory, end state) are unaffected; the census counters start at 0 per leg. | Recorded; identical output proven. |
| P5-5E-ARMS-REFERENCE | `/tmp/p5/5P/old_vif_arms.log` is a session artifact; the ctest row therefore passes `--refresh-old` and re-makes the reference from the frozen OLD binary bounded (≈4 s), SKIP 77 when it is absent. Hand runs default to the 5P log. | Recorded. |
| P5-5E-USAGE-TEXT | The usage text is NEW (the OLD line promised the file mode); exit code 2 is the same. No arm compares it. | Accepted (P5-VIF-ARMS-SYNTHETIC). |
| P5-5E-VU-MODE-LINE | OLD prints `[VU-MODE] active mode: cached` from `vu.cpp` static init; NEW does not. | Dropped by the driver's `SKIP_PREFIXES` (PLAN §2.9 row 1 precedent). |
| P5-5E-TOOL-LINK | The tool links `ps2r_vif` only; the brief's six-library list is satisfied transitively (PUBLIC links). Naming them again would emit ld duplicate-library warnings. | Recorded. |

### HANDOFF (5E → 5F)

**Done:** stage 5E complete — `engine/vif` (part 1) + `tools/vif_replay` with `--unpack-bulk [--vif-fast]`,
`vif_arms_vs_old.py`, ctest rows `VifUnpackBulkEquivalence`, `VifUnpackBulkEquivalenceFast`, `VifArmsVsOld`
(all Passed), the two UNPACK arms textually identical to OLD (`VIFARMS arms=2 same=2 diff=0`, scalar + NEON).
Tree GREEN:

```
$ ctest --preset macos-dev
128/132 Test #128: VifUnpackScalar .....................   Passed    0.01 sec
129/132 Test #129: VifCommands .........................   Passed    0.01 sec
130/132 Test #130: VifUnpackBulkEquivalence ............   Passed    0.01 sec
131/132 Test #131: VifUnpackBulkEquivalenceFast ........   Passed    0.01 sec
132/132 Test #132: VifArmsVsOld ........................   Passed    4.38 sec
100% tests passed, 0 tests failed out of 132
Total Test time (real) = 493.39 sec
```

(A first full run, taken while the NEON twin was building and its bounded arms ran on the same
machine, timed out `EeRunTokenAndEventQueue` (Phase 3, not owned) at 120 s; standalone it passes in
0.08 s and the quiet re-run above is 132/132 — load-induced, no code involved.)
No commits. Helpers: none left running (`/tmp/p5/5E/build-simd` retained as the NEON twin build).

**Not done (moved on):** the 5G arms (`--mtvu`, `--runahead*`) — tool exits 2; driver rows ready
(`--arms all`). The 5H integration list of part 1 stands unchanged (router registration + OLD observable,
`TAG_VIFS`, `TopsObserver → SetVifTops`, `BindVu1`, DMAC `SetTteFeed/ResetLatch/FDR`, `arbiter.drain()` ownership).

**For 5F (engine/dmac):** the VIF surface the DMAC drives is `Vif::ProcessData(1, span, qwc*16)`,
`SetTteFeed(1, on/off)` around the tag's TTE quadword, `ResetLatch(1)` at each chain-walk head,
`regs(1).stat & VifStat::kFdr` for the FDR check, `IsIdle(1)` for admissibility; `arbiter.drain()` is the
DMAC's / boundary's call (P5-5E-NO-DRAIN). `Vif` is NOT registered with the router.

**Gate status:** G6 partial as above; G1 GREEN.

**Next command (5F):** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R "Vif|Gif|Vu"` (expect all
Passed), then read `/tmp/p5/5F/FACTS.md` and PLAN §2.7 / §7 row 5F.

## 5F — engine/dmac

### 5F part 1 (engine/dmac + DmacRegs / DmacChainWalk) (2026-09-28, ≈45 min)

Facts: `/tmp/p5/5F/FACTS.md`. Scope = all five `engine/dmac` rows of PLAN §2.7 (the 5F′ split point was
passed: `chain_walker`, `dmac_deferred_vif1`, `dmac_handler_registry` landed with `dmac`) + two of the three
test TUs; the deferred-VIF1 / end-to-end suite is part 2.

### Landed (files)

| File | Class | OLD source transplanted (`host_app/`) |
|---|---|---|
| `engine/dmac/dmac_channel.h` | types only: `DmacChannelRegs` (28 B, `static_assert`), `DmacChannelId`, `DmacChcr` (bits + `Mode/Asp/WithAsp`), `DmacStat` (`Cis/Cim`), `DmacRegOffset`, `DmacAddr`, `DmacTagId` | `dmac.h:23-61` |
| `engine/dmac/dmac.h/.cpp` | `Dmac : MmioHandler` (+ `DmacGlobalRegs` POD) — ctor `(MainMemory&, Scratchpad&, Vif&, GifArbiter&)`, NOT registered; `Read32` by channel RANGE + globals + `0xF520`/`0xF590`; `Write32` by 256-byte PAGE keyed on `0xX000` (PCR the only writable global, CPC flush; other globals dropped; **IPU TO / SIF1 / SIF2 / SPR TO pages dropped** — P5-5F-WRITE-PAGE-DROP), VIF1 flush-before-write, CHCR STR → `StartChannel` → STR clear unless VIF1 parked; `Read/Write(addr, size)` widening (`addr & ~3`, raw sub-word store, 8 bytes = two halves); `StartChannel` (VIF1 chain → park; VIF0 chain inline; normal; interleave → complete; GIF flushes the parked VIF1 first, chain → `WalkGifChain` + complete, normal → `GifNormal` without CIS; IPU FROM/TO via `IpuCallback` slots with the `qwc > 0 && madr != 0` guard + translate; SIF1 via `SifCallback`, SIF0/2 complete; SPR = complete only with the P5-SPR-STUB rule comment); `CompleteChannel` (+ `InterruptRaise` slot fired only when CIS & CIM ≠ 0 — null and never true on a cold boot); `CheckInterrupt`; `DispatchInterrupt` (`$a0` UL[0] = channel, `$gp` UL[0], pc + `$ra` saved/restored, `-1` stops the chain, CIS acked even with no handlers); `SavestateSection()` (`TAG_DMAC`, v1, required, owner "dmac") + `Serialize/Validate/RestoreSection` (324 + 25·nq, records cause-then-id, restore clears the parked kick + tails); `ChannelForRead/Write` static | `dmac.cpp:1045-1253, :1254-1590, :2851-2864, :2927-3000`; `savestate.cpp:1183-1208, :1810-1832` |
| `engine/dmac/chain_walker.h/.cpp` | `ChainWalker` (+ `DmacVifWalkResult` POD) — `WalkVifChain(ch, regs)` (ResetLatch / staged reset, FDR early return, mode ≠ 1 early return, `tadr & 0x0FFFFFFF` + `Scratchpad::Contains` prefix, VIF1 `tadr == 0` early return, `lo == ~0 && hi == ~0` terminator, CHCR.TAG mirror, the eight ids verbatim incl. ASR push/pop and RET-at-ASP-0 end, NEXT/END `do_transfer = qwc > 0`, TTE QW `{hi, 0}` inside `SetTteFeed`, spans `spr ? spad : ram & 0x01FFFFFF` through `Vif::ProcessData`, IRQ+TIE, `kMaxTags = 10000`; returns `completed` + last-tag fields for the tail record — the caller clears STR / QWC and completes), `VifNormal`, `WalkGifChain` (full-address tag read, `lo == ~0 \|\| qwc == 0xFFFF`, REFE/REF by SPR bit / others by address prefix, ONE `submitOwned(Path3)` from an `acquireBuffer()` staging vector, no drain), `GifNormal` (`submit(Path3)`, MADR/QWC advanced regardless), `Translate(addr, bytes)` (full-address span, shared by the IPU arms), `ReadTagWord` (tail chase), `SetVif1StagedFeed(feed, flush, reset, user)` = the B1 seam | `dmac.cpp:495-987, :988-1044, :1592-1650, :1664-2707` |
| `engine/dmac/dmac_deferred_vif1.h/.cpp` | `DmacDeferredVif1` (+ `DmacVif1TailRecord` POD) — `Park`, `Active`, `Snapshot`, `Clear`, `SetRunner(fn, user)` (the DMAC binds `RunVifChain(Vif1)`), `FlushDeferredVif1(vif1, reason)` (snapshot restore, run, STR clear, drain if pending), `ChaseVif1Tail(vif1)` (flush, swap the batch, `ReadTagWord`, `IsSaneRewrite`, resume with `end_tadr \| SPR prefix` / `chcr \| STR` / `qwc 0`, drop the batch, drain if resumed), `RecordTail` (END + QWC 0 only, dedup per slot, pre-STR-clear CHCR), `IsSaneRewrite` (changed, not 0/~0, `qwc <= 0x4000`, address-bearing ids point into RAM/SPR); `FlushReason` enum carried, behaviour-neutral | `dmac.h:111-161`; `dmac.cpp:1383-1414, :2580-2600, :2708-2850` |
| `engine/dmac/dmac_handler_registry.h/.cpp` | `DmacHandlerRegistry` (+ `DmacHandler` POD) — flat table grouped by cause ascending, insertion order within a cause; `Add` (cause 0..15, pc ≠ 0, next 0/−1/before-id), `Remove` (cause 0..9, remaining count / −1), `SetActive`, `ForCause`, `Table`, `Install`, `NextId` (starts at 1), `Clear` | `dmac.h:9-16, :174-177`; `dmac.cpp:3003-3030`; `syscalls.cpp:1725-1810, :2275-2310` |
| `engine/dmac/CMakeLists.txt` | `ps2r_dmac` STATIC → `ps2r_vif ps2r_gif ps2r_vu ps2r_ee ps2r_core`, `-Wall -Wextra -Werror -O2` | — |
| root `CMakeLists.txt` (+1 line after `engine/vif`), `tests/CMakeLists.txt` (+1 line at the end) | `add_subdirectory(engine/dmac)     # Phase 5: EE DMAC channels + chain walker (ps2r_dmac)`; `add_subdirectory(dmac)     # Phase 5: DMAC suites` | append-only |
| `engine/dmac/README.md` (Files table, observables, not-written list), `engine/dmac/hle/README.md` (libdma NOT installed row), `tests/dmac/README.md` | — | — |
| `tests/dmac/CMakeLists.txt`, `tests/dmac/test_dmac_regs.cpp`, `tests/dmac/test_dmac_chain_walk.cpp` | `ps2r_dmac_test(name ctest)`; the two suites below | NEW-only (OLD has no DMAC tests, FACTS §6) |

### Flags applied

- `PS2_EEFEED` (B1, P5-EEFEED-P3) → NORM: the only MTVU feed shape is the staged one — `ChainWalker::SetVif1StagedFeed`
  hooks (feed per TTE QW / span, ONE flush at every walk exit incl. the early returns, reset via the hook, FDR test
  skipped under the staged feed); the per-span direct-submit arm is not written. Null hooks (this phase, and every
  harness leg) = the inline `Vif::ProcessData` path.
- `PS2_BAKE_DIAG`, `PS2_SHADOW8_KTAP` → DIAG, omitted (no mechanism shape).
- `PS2_S8_ARENA_MINVF`, `PS2_S26_DECAL` → not written.
- Probes not written: `DmacPerfScope` / `PERF_SCOPE`, `midc::ChainWalkGuard`, `mipsrc::`, `bullseye_trace`, `[KICKCNT]`,
  KICK-CHAIN-DUMP, the FMV-VRAM scan / hex dumps / OCEANSRC preview, `[R101-SLOTW]`, every `g_logFile` / `hwlog` line.

### Tests added (ctest names)

```
$ ./build/tests/dmac/test_dmac_regs
sizeof(DmacChannelRegs) = 28
DmacRegs: PASS
$ ./build/tests/dmac/test_dmac_chain_walk
DmacChainWalk: PASS
$ ctest --preset macos-dev -E "VifArmsVsOld|Vu1CachedCompare|Vu1Hle"      (bounded, 1500 s)
120/121 Test #120: DmacRegs ............................   Passed    1.10 sec
121/121 Test #121: DmacChainWalk .......................   Passed    2.26 sec
100% tests passed, 0 tests failed out of 121
```

`DmacRegs`: page map (reads by range for all 10 channels; writes land on the six `0xX000` pages, the other four are
dropped and a CHCR kick through them never starts; `0x8100` dropped / reads 0), dropped globals + PCR, cold
D_ENABLER = 0 / D_ENABLEW = 0, member-backed global reads, widening (byte/half reads, 8-byte read with a zero upper
half, raw sub-word stores, 8-byte store), SIF0 kick → complete (STR clear, CIS(5), QWC 0), `CheckInterrupt` false with
mask 0, CIS accumulation, a restored `Cim` → true, `InterruptRaise` slot fires only then, SIF1/SIF2 via
`StartChannel` (+ the SIF callback sees the registers), dispatch order (head / tail / before-id inserts, disabled
skipped, `$a0`/`$gp`, pc/`$ra` restored, `-1` stop, ack incl. a cause with no handlers), `Remove` counts, SPR FROM
(MMIO) / SPR TO (`StartChannel`) stubs leave RAM + SPAD bytes untouched, the deferred VIF1 observation points (CHCR
read, MADR read, PCR CPC, VIF1 QWC write keeps the written value, `FlushDeferredVif1()` uses the snapshot, SPR kick is
not a point, `ChaseVif1Tail()` flushes; D_STAT read and a VIF0 read are not), `TAG_DMAC` serialize size `324 + 25·2`,
cause-then-id order, next id, validate/restore round trip byte-identical, 323-byte / ragged / count-mismatch
rejections, restore drops a parked kick.

`DmacChainWalk`: CNT/CNT/END(0) with final TADR/MADR/QWC/CHCR.TAG/CIS + one tail record (VIF0 inline, no record);
REF/REFS/REF-SPR/REFE (+ the tag after REFE unread); NEXT qwc 0 / qwc 2 / SPR-bit jump to a scratchpad tag; CALL/RET
with the exact ASR0/ASR1/ASP trace, third nested CALL (no push, jumps), RET at ASP 0 ends; IRQ without TIE continues,
IRQ + TIE ends; VIF terminator `lo && hi`, `lo == ~0 / hi == 0` decoded as END qwc 0xFFFF with the overrunning span
skipped, GIF terminator `lo \|\| qwc 0xFFFF` without decode; `kMaxTags` self-loop = exactly 10000 tags then complete;
TTE MARK words fed before the payload (TTE off → ignored, unit idle after); VIF normal (25-bit window incl. the
0x70000000-prefixed MADR reading RAM), QWC 0 completes, interleave completes without a transfer, VIF1 TADR 0 → STR
clear without CIS, FDR → no feed, no CIS; GIF chain CNT+REFS(spr, RAM address)+END = ONE PATH3 submit whose
`RegisterOpStream` hash equals a direct `ReceiveData(Path3)` of the same 64 bytes, GIF REF-SPR + NEXT into a
scratchpad tag (80 bytes), GIF CALL/RET/ASP trace, parked VIF1 flushed by a GIF kick that itself does not drain; GIF
normal (STR clear, no CIS, MADR/QWC advanced, QWC 0 → nothing, scratchpad MADR).

### Oracle / gate status

```
$ cmake --build --preset macos-dev -j4          -> 0 warnings, 0 errors ([12/13] Linking ... nativeee)
$ python3 tests/lint/lint_one_class.py --root .  -> LintOneClass: 450 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . -> LintEnginePurity: 355 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py --root . -> getenv sites: 1 -> PASS
```

No OLD binary oracle exists for the DMAC (FACTS §6); the suites are NEW-only with per-assert OLD anchors in the
README / this record. `git status --porcelain` shows only OWNED paths + the two append-only CMake lines.

### Findings (→ plan §7.1)

| Id | Finding | Disposition |
|---|---|---|
| **P5-5F-WRITE-PAGE-DROP** | OLD `GetDmaChannelID` switches on `offset & 0xFF00` with cases `0x8000..0xD000` only; the inner `offset & 0x400/0x800` tests are dead, so MMIO writes to the IPU TO (`0xB400`), SIF1 (`0xC400`), SIF2 (`0xC800`) and SPR TO (`0xD400`) pages are DROPPED — those four channels can never start through the register file in OLD (`StartChannel` has no other caller; `ProcessSifDma`, the IPU TO arm and the SPR TO stub are unreachable). Reads still map them by range. | Replicated exactly (`Dmac::ChannelForWrite`), pinned by `DmacRegs`; the arms are kept as the PS2-rule bodies reachable through `StartChannel` (kernel HLE / library bindings). Bug-KB + accepted-difference candidate for the checklist; the FACTS §3.5 page-rule note is corrected by this row. |
| P5-5F-SUSPEND-DEAD | `SuspendedVifChain` never arms in OLD (§2); retry loops, CHCR force-complete and `MAX_RETRIES` unreachable. | Not written; observable identical. |
| P5-5F-RESET-UNCALLED | `DMAC::Reset()` (`enabler = 0x1201`) has no caller; cold-boot D_ENABLER reads 0. | No `Reset()`; ctor leaves everything 0; pinned by `DmacRegs`. |
| P5-5F-GIF-NORMAL-NO-CIS | GIF normal mode clears STR (via `Write`) but never sets CIS(2). | Replicated; pinned by `DmacChainWalk`. |
| P5-5F-WALK-GRAMMARS | GIF and VIF walks differ (terminator, REFS/SPR, TADR masking, TTE, tag-read address). | Two bodies transcribed; both pinned. |
| P5-5F-ENABLEDMAC-RETURN | Phase-3 `SyscallTable::ToggleMask(dmac_mask_)` sets mask bits and returns 1-then-0 where OLD's `_EnableDmac` returned 1 every call and never changed `stat`. | Checklist row for the Phase-3/integration owner: never mirror `DmacMask()` into `Dmac::globals().stat`. |
| P5-5F-INT1-SLOT | OLD never asserted INT1 from the DMAC (`CheckInterrupt` polled only at `sceSifSetDma`). | `InterruptRaise` slot fires from `CompleteChannel` iff CIS & CIM ≠ 0 — never on a cold boot; null in every harness leg. Integration decides whether to bind it (binding changes nothing while the mask is 0). |

### HANDOFF (5F part 1 → 5F part 2)

**Done:** all of `engine/dmac` (five rows), `ps2r_dmac` linked, `DmacRegs` + `DmacChainWalk` Passed, lints 0, getenv gate 1,
build 0 warnings. No commits. Helper files under `/tmp/p5/5F/` (fix scripts, logs); no processes left running.

**Not done (moved to part 2):** `tests/dmac/test_dmac_deferred_vif1.cpp` → ctest `DmacDeferredVif1` (FACTS §8 row 3: two-kick
park order, the full flush-point matrix incl. PATH2-before-PATH3 ordering in `RegisterOpStream` after a GIF kick + explicit
drain, tail chase resume / unchanged / 0 / ~0 / `qwc 0x4001` / bad REF address rejections, batch consumption, END-with-payload
not recorded, dedup, `IsSaneRewrite` table); PATH3/VIF end-to-end tests through the arbiter with DIRECT + PATH3 interleave; the
libdma rows (README states NOT installed — part 2 only re-reads it, no bindings this phase); optional: a `ChainWalker` staged-feed
hook test (fake hooks recording feed/flush/reset order and the skipped FDR test) ahead of 5G.

**Integration checklist rows added (FACTS §11 + this stage):** router `Register` of the OLD read/write windows
(`0x10008000..0x1000D7FF` + `0x1000E000..0x1000E060` + `0x1000F520` reads; `0x10008000..0x1000E063` + `0x1000F590` writes)
displacing `MmioSink` rows; `SyscallTable` → `DmacHandlerRegistry::Install` + `sceSifSetDma` → `stat |= Cis(6); if (CheckInterrupt())
DispatchInterrupt(ctx)`, `sceSifStopDma` → `channel(5).chcr &= ~kStr`, `_EnableDmac/_DisableDmac` keep the dropped-write observable
(P5-5F-ENABLEDMAC-RETURN); SIF1 `SetSifCallback` (`engine/sif`), IPU `SetIpuFeedInput/DrainOutput` (Phase 7; note P5-5F-WRITE-PAGE-DROP:
IPU TO / SIF1 / SIF2 / SPR TO start only via `StartChannel`); `BoundarySequencer` `ChaseVif1Tail()` at SyncV / WaitForVblank before the
VSync job; HLE seams `sceGsSyncPath/sceGsResetPath/sceDmaSync/sceGifSyncDma` → `FlushDeferredVif1()` + `v0 = 0`; `SavestateSection()`
registered after `TAG_INTC`, quiesce = flush then refuse if `HasDeferredVif1() || channel(1).chcr & kStr`; 5G installs
`ChainWalker::SetVif1StagedFeed` from `MtvuWorker`.

**Gate status:** G1 GREEN — the full battery (bounded, 1500 s):

```
$ ctest --preset macos-dev
133/134 Test #133: DmacRegs ............................   Passed    1.13 sec
134/134 Test #134: DmacChainWalk .......................   Passed    2.01 sec
100% tests passed, 0 tests failed out of 134
Total Test time (real) = 502.91 sec
``` **Next command (part 2):** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R Dmac` (expect 2/2
Passed), then write `tests/dmac/test_dmac_deferred_vif1.cpp` per FACTS §8 row 3 and register `ps2r_dmac_test(test_dmac_deferred_vif1 DmacDeferredVif1)`.

### 5F part 2 (end-to-end DMAC suites + deferred VIF1 + libdma rows) (2026-09-28, ≈45 min)

Facts: `/tmp/p5/5F/FACTS.md` (§3.3/§3.8/§3.9 walks + deferred, §4 layout, §5 libdma, §8 test design). No
`/tmp/p5/5F/REVIEW.md` existed at the start of this stage (no review-fix items to apply).

### Landed (files)

| File | Class | OLD source transplanted (`host_app/`) |
|---|---|---|
| `engine/dmac/hle/libdma_bindings.h/.cpp` | `LibdmaBindings` — one instance over `Dmac&` + `MainMemory&`; `void (CpuContext&)` members = the `SdkBindingManifest::Handler` shape minus the object: `SceDmaGetChan` (`$a0` 0..9 → `kChannelArenaBase 0x01CFF000 + 0x40·ch`, descriptor zero-filled on its first request, bit-mask `descriptor_inited_`; otherwise 0), `SceDmaPutEnv` / `SceDmaReset` (→ 0), `Memclr` (`$a0` dst, `$a1` n: 16-byte chunks as two 8-byte stores then bytes, each store dropped when outside RAM through `MainMemory::Pointer(Phys(a), w)`; NULL dst = nothing; → 0), `SceDmaSync` / `SceGifSyncDma` (`Dmac::FlushDeferredVif1()` then 0). **NOT registered** with any manifest; the thunk that binds an instance is the composition root's (integration). Added to `ps2r_dmac` sources | `sce_hle/libdma.cpp:22-79`; `gs_hle.cpp:936-957, :965-986` (the MTVU barrier / `IsRunning`-gated drain there = 5G worker rows) |
| `engine/dmac/hle/README.md` | rewritten: status "NOT installed", member table with anchors, the arena divergence row (HACK_LEDGER libdma), suite pointer | — |
| `engine/dmac/README.md`, `tests/dmac/README.md` | `hle/libdma_bindings` Files row; the six new suite rows | — |
| `tests/dmac/test_dmac_gif_path3.cpp` → `DmacGifPath3` | GIF chain → CHCR kick → `ChainWalker::WalkGifChain` → ONE `submitOwned(Path3)` → `drain()` → `GifParser` → `RegisterOpStream` | `dmac.cpp:495-987` end to end |
| `tests/dmac/test_dmac_vif_chain.cpp` → `DmacVifChain` | VIF1/VIF0 chains through `Vif` into `VuUnit` (UNPACK, TTE STCYCL, MSCAL micro) and the arbiter (DIRECT → PATH2) | `dmac.cpp:1664-2707` end to end |
| `tests/dmac/test_dmac_deferred_vif1.cpp` → `DmacDeferredVif1` | park / append / observation-point matrix / kick order / snapshot / tail chase / termination / frame sync | `dmac.cpp:1383-1414, :2580-2600, :2708-2850` |
| `tests/dmac/test_dmac_spr_stub.cpp` → `DmacSprStub` | SPR FROM/TO complete without copying (P5-SPR-STUB) | `dmac.cpp:1562-1581` |
| `tests/dmac/test_dmac_section.cpp` → `DmacSectionRoundTrip` | `TAG_DMAC` byte layout + Encode→Decode→Encode identity + validate | `savestate.cpp:1183-1208, :1810-1832` |
| `tests/dmac/test_dmac_libdma.cpp` → `DmacLibdmaRows` | the libdma bodies + the two sync seams driven with a `CpuContext` | `sce_hle/libdma.cpp` |
| `tests/dmac/CMakeLists.txt` | six `ps2r_dmac_test(...)` registrations | — |

No engine behaviour changed in part 2 (`dmac`, `chain_walker`, `dmac_deferred_vif1`, `dmac_handler_registry` untouched);
the only `ps2r_dmac` edit is the added `hle/libdma_bindings.cpp` source.

### Flags applied

None new. The libdma bodies carry no flag sites (`log_call` lines not written). `PS2_EEFEED` B1 / `PS2_BAKE_DIAG` /
`PS2_SHADOW8_KTAP` / `PS2_S8_ARENA_MINVF` / `PS2_S26_DECAL` dispositions stand as recorded in part 1.

### Tests added (ctest names)

```
$ cmake --build --preset macos-dev -j4        -> 0 warnings, 0 errors ([19/22] Linking CXX executable app/host/nativeee)
$ ctest --preset macos-dev -R Dmac            (bounded, 300 s)
1/8 Test #133: DmacRegs .........................   Passed    1.12 sec
2/8 Test #134: DmacChainWalk ....................   Passed    2.00 sec
3/8 Test #135: DmacGifPath3 .....................   Passed    1.05 sec
4/8 Test #136: DmacVifChain .....................   Passed    0.60 sec
5/8 Test #137: DmacDeferredVif1 .................   Passed    3.90 sec
6/8 Test #138: DmacSprStub ......................   Passed    0.53 sec
7/8 Test #139: DmacSectionRoundTrip .............   Passed    1.20 sec
8/8 Test #140: DmacLibdmaRows ...................   Passed    0.52 sec
100% tests passed, 0 tests failed out of 8
```

`DmacGifPath3`: canonical chain CNT(3)+REF(1)+NEXT(0)→END(1) whose four DMA segments form one PACKED A+D packet —
observer sees ONE PATH3 submit of 80 B (not IMAGE), channel completes (STR clear, CIS(2), TADR/MADR 0x9020) with the
packet still queued; after `drain()` the op sequence is exactly Tag, Reg 0x47/0x48/0x49/0x4A, Eop, Finish on path 3
and `Hash()` equals the same 80 bytes fed by `ReceiveData(Path3)`; two kicks = two packets drained in kick order with
one Finish; IMAGE packet (observer sees the arbiter's `is_path3_image` rule = first GIFtag FLG IMAGE; 2 Image ops with
the exact lo/hi; Eop, Finish; transfer closed), a partial IMAGE (NLOOP 4, 2 QW) keeps `in_transfer` across the drain's
`FinishPath3` (no Finish op) and the next PATH3 chain abandons it (PATH3 stale reset) and parses on its own; M3R: the
DMAC completes and queues regardless (no mask test in the DMAC), the mask lifting before the drain parses everything
(hash-identical), still masked at the drain = held in the parser tail (`BufferedBytes`) then abandoned by
`FinishPath3` (Finish only), VIF MSKPATH3 does not hold; a parked VIF1 DIRECT lands on PATH2 at the GIF kick before the
GIF chain's PATH3 (3 ops then 4 after the explicit drain; the kick itself does not drain); sub-GIFtag payload dropped,
head terminator completes with TADR unchanged.

`DmacVifChain`: TTE on/off — STCYCL CL2/WL1 in the tag's words 2-3 lands BEFORE the UNPACK (cycle 0x0102, skipping
writes qw 0x10 / 0x12 with 0x11 untouched) vs contiguous 0x10 / 0x11 with TTE off (cycle 0x0101); MSCAL 0 runs
`iaddiu vi1, vi0, 5` through `VuUnit` (VI1 5, TPC 0x10); `Vif::IsIdle(1)`, TADR 0x4070, QWC 0, CIS(1), one tail record;
VIF0 chain inline (REF with the SPR bit from the scratchpad, MSCAL on VU0 → VI1 9, MADR mirrors the LAST tag, no tails)
and a whole chain read from a scratchpad TADR (masked TADR kept); DIRECT 2 QW → PATH2 ops (Tag/Reg 0x47/Eop on path
2, no Finish) at the MADR-read flush; VIF1 normal mode is not deferred and drains at the kick; a DIRECT split across two
CNT tags streams through.

`DmacDeferredVif1`: park (STR set through the member, no data, no CIS, no record, snapshot TADR) → the game appends
CNT+UNPACK+END at the END slot → the CHCR read walks the appended chain (both UNPACKs, TADR slot+0x40, one record for
the final END with the mirrored TAG half `0x70000000 | 0x105`); a second CHCR read is not a flush; 21-row observation
point table — CHCR / MADR / TADR byte / ASR1 / non-register page reads, QWC / SADR writes, PCR with CPC(VIF1), GIF kick,
`FlushDeferredVif1()`, `ChaseVif1Tail()` flush; D_STAT / D_CTRL reads, PCR without CPC, VIF0 / GIF CHCR reads, SPR /
SIF0 / VIF0-chain kicks, dropped D_STAT / D_ENABLEW writes do not (and the frame sync then flushes in order); a VIF1
register write flushes first and the value survives; realistic two-kick order (the TADR write flushes A, CHCR parks B,
CHCR read flushes B; records 0x4030, 0x5030) and a CHCR re-kick over the post-walk registers (snapshot TADR 0x4040);
snapshot restore (member-mutated TADR / CHCR.TTE / ASR0 ignored, CALL pushes over the restored ASR0); tail chase —
unchanged slot consumed, dedup, sane rewrite resumes from the slot with the resumed walk's own record dropped, reject
table (0, ~0, QWC 0x4001, REF / REFE / NEXT / CALL beyond RAM), accept table (QWC 0x4000, REF into RAM, REF SPR bit →
scratchpad, `0x7000`-prefixed REF without the bit accepted by the gate and read from RAM at the 25-bit address, END
with payload), END-with-payload and REFE chains not recorded, scratchpad slot resumes with the SPR prefix, scratchpad
offset 0 = the VIF1 TADR 0 early return (STR clear, no CIS, nothing fed); a self-loop NEXT rewrite terminates at
`kMaxTags` (TADR = slot, completed, no records, nothing parked); `ChaseVif1Tail` with a parked kick flushes it first,
then no-ops; a flush drains a GIF-normal PATH3 packet queued earlier.

`DmacSprStub`: SPR FROM via MMIO (normal; chain + TTE + TIE) and `StartChannel`, SPR TO via `StartChannel` (its
`0xD400` page write is dropped, so a CHCR write there starts nothing) — RAM (first 128 KB patterned) + scratchpad
byte-identical, MADR / SADR / TADR / ASR0 / ASR1 untouched, no TAG mirror, STR clear, QWC 0, CIS, `CheckInterrupt`
false; an SPR kick (either path) is not an observation point and does not drain; with a restored CIM the stub's
completion raises through the slot exactly once and a handler-less dispatch acknowledges.

`DmacSectionRoundTrip`: `kSectionFixedBytes 324` / `kHandlerRecordBytes 25` pinned; empty state = 324 bytes all zero
except next id 1; layout — 28-byte channel records at stride 28 (chcr, madr, qwc, tadr, asr0, asr1, sadr), nine globals
at 280 in order, next id at 316, count at 320, records cause-then-id grouped by cause ascending with head inserts first
and cause 15 allowed; Encode→Decode→Encode byte identity for the populated state (4 handlers), a 64-handler state and a
decode over a populated, different DMAC; field-for-field equality; a restored `0x00400040` stat makes
`CheckInterrupt()` true; the `SectionSpec` closures (tag/version/required/owner, `absent` null); validate rejects 323 B,
+1 B, a missing record, a count mismatch and an empty payload, and a rejected restore leaves the DMAC byte-identical;
restore drops the parked kick and the tail records.

`DmacLibdmaRows`: arena descriptors zero-filled once per channel with the byte past the arena untouched and a second
request not re-zeroing; NULL for 10 / −1 / 0x80000000 / 11 without marking; `SceDmaPutEnv` / `SceDmaReset` → 0 with the
serialized DMAC unchanged; `Memclr` chunk + 3-byte tail, n 0, NULL dst with n > 0, KSEG0 mirror, a span running off the
top of RAM (in-range stores land, the rest dropped); `SceDmaSync` / `SceGifSyncDma` flush the parked kick (UNPACK
lands, CIS(1), arbiter drained) → 0 and are state-neutral no-ops otherwise.

### Oracle / gate status

```
$ python3 tests/lint/lint_one_class.py --root .      -> LintOneClass: 458 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root .  -> LintEnginePurity: 357 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py --root .  -> getenv sites: 1 -> PASS
$ ctest --preset macos-dev                           (bounded, 1500 s; full battery)
135/140 Test #135: DmacGifPath3 ........................   Passed    1.02 sec
136/140 Test #136: DmacVifChain ........................   Passed    0.62 sec
137/140 Test #137: DmacDeferredVif1 ....................   Passed    3.89 sec
138/140 Test #138: DmacSprStub .........................   Passed    0.54 sec
139/140 Test #139: DmacSectionRoundTrip ................   Passed    1.19 sec
140/140 Test #140: DmacLibdmaRows ......................   Passed    0.52 sec
100% tests passed, 0 tests failed out of 140
Total Test time (real) = 505.57 sec
```

**Gate status vs PLAN §7 row 5F green criterion:** GREEN. `DmacRegs` (page map, dropped globals, CHCR-read flush,
`CheckInterrupt` false with mask 0), `DmacChainWalk` (8 ids, ASR stack, terminators, `kMaxTags`, TTE, SPR sources,
GIF chain = ONE PATH3 submit, VIF1 END → tail record) and `DmacDeferredVif1` (deferred kick, flush points, tail-chase
resume on a sane terminator, SPR = `CompleteChannel` only) are Passed; `sizeof(DmacChannelRegs) == 28` pinned
(`static_assert` + `DmacRegs`); lints 0; `kMaxRetries = 120` is NOT present by design (P5-5F-SUSPEND-DEAD: the
mechanism never arms in OLD — the plan row's literal token is the one criterion knowingly not written). The
end-to-end PATH3 / VIF / SPR / section / libdma suites exceed the row. No OLD binary oracle exists for the DMAC
(FACTS §6). Process note: one helper edit in this stage ran through an inline `python3 -` heredoc before switching to
the file-based helpers (`/tmp/p5/5F/fix_p2_1.py`, `fix_p2_2.py`) the contract asks for; no other process deviation.

### Findings (→ plan §7.1)

| Id | Finding | Disposition |
|---|---|---|
| **P5-5F-REKICK-DEAD-ARM** | `StartChannel`'s VIF1 "re-kick" flush (`dmac.cpp:1383-1386`) is unreachable through the register file: `Write` flushes any parked kick BEFORE storing CHCR, so `deferred_vif1.active` is always false when `StartChannel(VIF1)` runs. The arm is reachable only by a direct `StartChannel` call (none in OLD), where it would park the POST-walk registers (STR already clear). | Kept verbatim (defensive, behaviour-neutral); not pinned by a test — `DmacDeferredVif1` pins the reachable order (TADR write flushes, CHCR parks, read flushes) and the same-TADR re-kick (snapshot = where the previous walk ended). |
| **P5-5F-TAG-MIRROR-ID** | The CHCR.TAG mirror copies bits 16-31 of tag word 0, which includes the ID field: after an END the channel's CHCR reads `0x7000xxxx` and the tail record stores that value and resumes with it (`chcr | STR` — the mirrored bits are overwritten by the next tag). | Observable kept; pinned by `DmacDeferredVif1` (record CHCR) and `DmacChainWalk` (final CHCR). |
| **P5-5F-SPAD-TADR0** | A VIF1 chain whose TADR is scratchpad offset 0 (`0x70000000`) reads as TADR 0 through the 28-bit mask and takes the "VIF1 TADR 0" early return (STR clear, no CIS, nothing fed). | Reference behaviour; pinned by `DmacDeferredVif1`. |
| **P5-5F-CHASE-GATE-VS-WALK** | `IsSaneRewrite` accepts an address-bearing tag whose ADDR carries the `0x7000` prefix without the SPR bit, but the VIF walk then reads RAM at `addr & 0x01FFFFFF` (the walk honours the bit, not the prefix). | Two rules transcribed as-is; pinned by the accept table. |
| **P5-5F-P3-STALE-RESET-VIA-DMA** | A PATH3 IMAGE left mid-transfer by one GIF chain survives the drain's `FinishPath3` but the next GIF chain's `ReceiveData(Path3)` abandons it (5A's stale-reset rule) — a PATH3 IMAGE can only be completed by PATH1/PATH2 continuation. | Pinned by `DmacGifPath3`; no DMAC change. |
| **P5-5F-M3R-DRAIN-ABANDON** | With MODE.M3R set at drain time the PATH3 packet is held in the parser tail and then cleared by the drain's `FinishPath3` (the data is lost unless the mask lifts before the drain). | Pinned as the NEW-inline observable (5A parser rules); flagged for the 5G/6 owners to confirm against OLD's drain cadence once `GifRegs` is routed. |
| **P5-5F-LIBDMA-ARENA** | `sceDmaGetChan` hands out an invented descriptor arena at `0x01CFF000` (HACK_LEDGER libdma row). | Transcribed with the divergence stated in the header + README; bodies uninstalled; root fix = manifest-resolved symbol. |

### HANDOFF (5F → 5G)

**Done:** all five `engine/dmac` rows (part 1) + `hle/libdma_bindings` (uninstalled) + eight `tests/dmac` suites
(`DmacRegs`, `DmacChainWalk`, `DmacGifPath3`, `DmacVifChain`, `DmacDeferredVif1`, `DmacSprStub`,
`DmacSectionRoundTrip`, `DmacLibdmaRows`) Passed; full battery 140/140; lints 0; getenv gate 1; build 0 warnings; READMEs
(`engine/dmac`, `engine/dmac/hle`, `tests/dmac`) current. No commits. Helper files under `/tmp/p5/5F/` (fix scripts, logs);
no processes left running.

**Not done (integration rows, not 5G engine work unless stated):**
1. `MmioRouter::Register` of the OLD windows — reads `0x10008000..0x1000D7FF`, `0x1000E000..0x1000E060`, `0x1000F520`;
   writes `0x10008000..0x1000E063`, `0x1000F590` (i.e. `Register(0x10008000, 0x6064, dmac, "DMAC")` + `0x1000F520`/`0x1000F590`
   16-byte rows) displacing `MmioSink` rows `mmio_sink.cpp:18-37`. `Dmac::Read/Write` already reproduce the widening.
2. `SectionSpec` registration: `SectionRegistry::Register(dmac.SavestateSection())` after `TAG_INTC`, before
   `TAG_TIMR/GSPR/GSST/VIFS/GIFS`; quiesce = `FlushDeferredVif1()` then refuse if `HasDeferredVif1() || channel(1).chcr & kStr`.
3. `SyscallTable` → `DmacHandlerRegistry` handover: `Install(DmacHandlers()-shaped records, next id)`; `AddDmacHandler /
   RemoveDmacHandler / EnableDmacHandler / DisableDmacHandler` → registry; `sceSifSetDma` → `globals().stat |= Cis(6); if
   (CheckInterrupt()) DispatchInterrupt(ctx)`; `sceSifStopDma` → `channel(5).chcr &= ~kStr`; `_EnableDmac/_DisableDmac` keep the
   dropped-write observable — NEVER mirror `DmacMask()` into `stat` (P5-5F-ENABLEDMAC-RETURN, P5-DMAC-MASK); `SetGuestCall`
   bound to the dispatch table.
4. SIF / IPU callback binding: `SetSifCallback` → `engine/sif` (FACTS §3.2 body; SIF0/SIF2 stay complete-only);
   `SetIpuFeedInput / SetIpuDrainOutput` → Phase 7 IPU FIFOs; `libipu` / `libmpeg` direct `channel()` pokes → Phase 7. Note
   P5-5F-WRITE-PAGE-DROP: IPU TO / SIF1 / SIF2 / SPR TO start only through `StartChannel`.
5. `BoundarySequencer`: `ChaseVif1Tail()` at the SyncV / WaitForVblank step BEFORE the VSync job (OLD `gs_hle.cpp:446`,
   `sif_hle.cpp:225`); the `FlushDeferredVif1()` slot for the HLE syncs — `sceGsSyncPath` / `sceGsResetPath` (gs/hle) and
   `sceDmaSync` / `sceGifSyncDma` → `LibdmaBindings::SceDmaSync/SceGifSyncDma` replacing `games/…/hle/glue/sce_dma_glue.cpp:6-7`.
6. libdma manifest rows: `SdkBindingManifest::Register("libdma.a", …)` thunks over one `LibdmaBindings` instance in `app/host`
   (the handler is a plain function pointer); `InterruptRaise` slot binding (changes nothing while the mask is 0).
7. **5G (engine work):** install `ChainWalker::SetVif1StagedFeed(feed, flush, reset, user)` from `MtvuWorker` (B1); the
   `SceDmaSync / SceGifSyncDma` MTVU barrier + `IsRunning`-gated drain rows; confirm P5-5F-M3R-DRAIN-ABANDON against OLD's
   drain cadence when the drain thread lands.

**Gate status:** GREEN (quoted above).

**Next command (5G):** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R "Dmac|Vif|Gif"` (expect all
Passed), then read `/tmp/p5/5G/FACTS.md` for the arbiter cross-thread half + `MtvuWorker`.

## 5G — cross-thread half

### 5G part 1 (GifArbiter completion + GifDrainThread + RunaheadGuard + MtvuBackpressure) (2026-09-28, ≈45 min)

Sources: `/tmp/p5/5G/FACTS.md` §A, §B.4–B.5, §C, §E, §F; PLAN §2.1 rows `gif_arbiter` / `gif_drain_thread`, §2.3 rows
`mtvu_backpressure` / `runahead_guard`, §3.5, §7 row 5G. The worker (`MtvuWorker`), `MtvuSync`, the dispatcher step-2
wiring and the four `vif_replay` arms are 5G part 2 (HANDOFF below).

### Landed (files)

| File | Class | OLD source transplanted |
|---|---|---|
| `engine/gif/gif_mtvu_marker.h` | `GifArbiterPacket` (moved from `gif_arbiter.h`, unchanged fields) + `GifMtvuMarker {filled, sealed, detached}` PODs | `host_app/gif_arbiter.h:85-121` |
| `engine/gif/gif_marker_owner.h` | `GifMarkerOwner` — abstract `Running / OnWorkerThread / ActiveMarker` (null = inline routing; the worker implements it in part 2) | the `vu1_async::IsRunning / OnWorkerThread / ActiveMarker` queries of `gif_arbiter.cpp:657-716, :738-775` |
| `engine/gif/gif_boundary_hook.h` | `GifBoundaryHook` — abstract `OnBoundary(seq, enqueue_ns)`, ctor-injected (`GifArbiter(GifParser&, GifBoundaryHook* = nullptr)`); OLD's static `SetBoundaryHook` slot is not written | `host_app/gif_arbiter.h:335-402` |
| `engine/gif/gif_arbiter.h/.cpp` (completed) | `GifArbiter` — `openMarker / sealMarker / hasSealedReady / collectReady` (private; ready-prefix promotion), `inbox_` (`std::deque<InboxSlot>` under `mtvu_mtx_`), routing in one private `Enqueue` (worker thread → active marker; worker or drain thread running → inbox tail + kick; else `queue_`), `openBoundary(seq, enqueue_ns)` (0 → 1, feed-note tripwire) / `noteFeedForBoundary` / `dispatchBoundaryInline` (called FIRST per packet in the pass, before the empty-data guard) / `boundary_stats()` (`GifBoundaryStats {enqueued, delivered, order_violations}` replaces the three `vu1_async::NoteBoundary*` counters), `PendingBytes / PendingSlots` relaxed atomics (`pending_bytes_`, `inbox_slots_`; `drained_packets_` atomic too), `clear()` (detach + settle), `hasPending()` three-way, `SetMarkerOwner`, `AttachDrainThread / DrainThreadRunning / OnDrainThread / DrainFence`, `drain()` (foreign call while the thread runs = `Notify`; body = private `DrainPass()`, `friend class GifDrainThread`), `SetDrainObserver` (per-packet tap before delivery, boundaries included). Header carries the ordering contract + the per-member cross-thread one-liners | `host_app/gif_arbiter.h:123-195, :229-297, :300-333, :335-402, :403-452, :559-606`; `host_app/gif_arbiter.cpp:657-716, :738-775, :933-951, :1377, :1626, :1755-1808` |
| `engine/gif/gif_drain_thread.h/.cpp` | `GifDrainThread(GifArbiter&)` — `Start(on_start, user)` (attaches; `tid_` then `running_` release) / `Stop()` (stop+kick, join, `running_ = false`, detach, `done_cv_.notify_all`) / `Running` / `OnDrainThread` / `Working` / `Notify` / `PassGeneration` / `DrainFence` (gen ≥ mark+2 with 1 ms re-kick waits) / `DrainIdleGeneration` (1 stopped, 0 working or holding, else gen+1) / private `Loop` (one pass per kick, the stop request runs one final pass). `std::thread` + `std::mutex` + two `std::condition_variable` (precedent `engine/iop/iop_host_thread.cpp`, `engine/ee/hle/*`) | `host_app/gif_arbiter.h:454-557, :607-679` |
| `engine/vu/tiers/mtvu_settings.h` | `MtvuSettings {bool enabled = true; int depth_frames = 2;}` types-only; comment names `engine.mtvu` / `engine.mtvu_depth`; no Config read | `vu1_async.cpp:1011` (`PS2_MTVU`), `runahead_guard.h:60-68` (`PS2_MTVU_MAX_FRAMES`) |
| `engine/vu/tiers/mtvu_clock.h/.cpp` | `MtvuClock` — virtual `NowNs()` (default `ps2r::Park::NowNs()`), `Steady()` shared instance; tests derive a fake | `perf::NowNs()` uses in `vu1_async.cpp:861-989` |
| `engine/vu/tiers/runahead_guard.h/.cpp` | `RunaheadGuard(int depth_frames, std::function<bool()> low_band = {}, const MtvuClock& = Steady())` — `DepthFramesSetting` (negative → 0), `EffMaxFrames` (exactly 2 → 3 while `low_band()`), `static DepthFrames(declared, presented)` clamp, `Over()`, `FramePark(declared, presented, engaged_at_ns) → RunaheadFramePark {park, valve_hit}` (`kFrameValveNs = 100 ms`), `NowNs()`, nested `Tripwire {kPersistPresents = 30, kMaxReports = 5; Feed(depth, cap)}` | `host_app/runahead_guard.h:60-75, :104-131, :134-177`; valve `vu1_async.cpp:872-873, :926-931` |
| `engine/vu/tiers/mtvu_pipeline.h` | `MtvuPipeline` — abstract `Running / MayPark / PipelineBytes / WorkerIdle / SealCount / EagerDrain / WaitIdleOrSeal / WaitFrameProgress / EnterSyncWait / LeaveSyncWait` (what `BackpressureIfNeeded` read of `g_vu1q_bytes + PendingBytes()`, `g_inflight`, `g_seal_count`, `g_cv_idle` waits, `g_sync_waiters`, `t_is_worker / t_in_eager_drain`); the worker implements it in part 2 | `vu1_async.cpp:861-989` (the accessed globals) |
| `engine/vu/tiers/mtvu_backpressure.h/.cpp` | `MtvuBackpressure(RunaheadGuard&, const MtvuClock&)` — `kPipelineHighBytes = 256 MiB`, `kLowWaterBytes = high − high/8`, `kPollNs = 500 µs`, `kStallValveCount = 2`, `Bind(MtvuPipeline*)`, `SetFrameCounters(declared, presented)` (null = depth 0), `SetShutdownFlag`, `Engage()` (= `BackpressureIfNeeded`: fast path, eager drain per iteration, byte hysteresis + idle-worker stall valve, frame arm via `RunaheadGuard::FramePark`, `WaitIdleOrSeal` vs `WaitFrameProgress`), `DepthFrames`, `Engagements / FrameValveHits / StallValveHits`; spin tier `kSpinBudgetNs = 40000`, `kSpinBudgetMinNs/MaxNs = 1000/1000000` + `ClampSpinBudgetNs`, `kRecentNs = 200000`, `SpinArmed(prev, now)`, `CpuRelax()` (= `std::this_thread::yield`) | `vu1_async.cpp:186-194, :247-258, :608-650, :861-989` |
| `engine/gif/CMakeLists.txt`, `engine/vu/CMakeLists.txt` | `gif_drain_thread.cpp`; `tiers/mtvu_clock.cpp`, `tiers/runahead_guard.cpp`, `tiers/mtvu_backpressure.cpp` | — |
| READMEs | `engine/gif/README.md` (marker/owner/hook/arbiter/drain-thread rows, the order contract, the collect gate, the MMIO-fence row, 5G flags), `engine/vu/tiers/README.md` (settings/clock/guard/pipeline/backpressure rows, OLD anchors, flags), `tests/gif/README.md` (two rows, TSAN note), `tests/vu/README.md` (new) | — |

Deviations from the PLAN / brief (recorded, all in the findings table): `GifDrainThread` takes `(GifArbiter&)` only — the
brief's `GifParser&` is unused because the arbiter owns the parser (P5-5G-DRAIN-CTOR); the boundary hook is an abstract
class injected at construction, not a `(fn, user)` pair (the brief asked for an interface); `RunaheadGuard` gets the
park/valve decision (`Over`, `FramePark`) and the clock, `MtvuBackpressure` drives the loop through `MtvuPipeline`
(P5-5G-PIPELINE-SEAM); `collectReady` gate = `PendingSlots() != 0` (P5-5G-COLLECT-GATE).

### Flags applied

| Flag | Outcome |
|---|---|
| `PS2_GIF_DRAIN_THREAD` (`gif_arbiter.h:44-51`) | NORM — class permanent; started only by an explicit `Start()` from the composition root under `MtvuSettings::enabled` (`main.cpp:14228-14239` rule); `DrainThreadOverrideForTest` not written (tests construct the thread) |
| `PS2_MTVU` (`vu1_async.cpp:1011`) | SETTING mirror — `MtvuSettings::enabled` (Config key `engine.mtvu` exists; copy = integration row) |
| `PS2_MTVU_MAX_FRAMES` (`runahead_guard.h:60-68`) | SETTING mirror — `MtvuSettings::depth_frames = 2`; `RunaheadGuard` takes the int (negative → 0) |
| `PS2_MTVU_DYNDEPTH` (`:105-111`) | NORM — `EffMaxFrames` lifts exactly 2 → 3 on the injected `low_band`, unconditionally considered |
| `PS2_RUNAHEAD_ALARM` (`:69-75`) | NORM — `Tripwire` mechanism always available; its feeder/consumer is the Phase-6 graphics thread |
| `PS2_MTVU_MAX_MB` (`vu1_async.cpp:186-194`) | NORM — `kPipelineHighBytes = 256ull << 20` (the `<0 → 0 disables` arm not written) |
| `PS2_MTVU_SPIN` (`:610`) | NORM — spin tier always armed by recency (`SpinArmed`) |
| `PS2_MTVU_SPIN_NS` (`:627`) | NORM — `kSpinBudgetNs = 40000`; the clamp bounds kept as `ClampSpinBudgetNs` for the worker's use |
| `PS2_PERF` drain-loop census (`gif_arbiter.h:630-633, :657-673`), `perf::`/`ringlog::` in the park loop | DIAG — omitted |
| `PS2_R198_PATH3DEFER*`, `PS2_SHADOW_TAILFIX` (`s8tail::DropHeld` in `clear()`), `PS2_S8_PKTDUMP*`, `OnExecThread()` disjuncts, `PS2_VU1_EXEC_*`, `PS2_MTVU_PATHSYNC_RELAX` | not written |

### Tests added (ctest names)

```
 92/144 Test  #92: GifDrainThread ......................   Passed    0.01 sec
 93/144 Test  #93: GifDeferBoundaryOrder ...............   Passed    0.22 sec
130/144 Test #130: VuRunaheadGuard .....................   Passed    0.01 sec
131/144 Test #131: VuMtvuBackpressure ..................   Passed    0.01 sec
```
Test outputs: `[GIFDRAIN] RESULT: PASS (33/33 checks passed)`; `case i: 21 register ops identical inline vs deferred
(Finish: 1 inline, 2 deferred passes)` + `soak: 10000 randomized iterations, delivered order == submission order with
boundaries in position` + `DeferBoundaryOrder: 36/36 checks passed; RESULT: PASS`; `test_runahead_guard: ALL PASS`;
`test_mtvu_backpressure: ALL PASS`. Stability: `ctest -R "GifDrainThread|GifDeferBoundaryOrder" --repeat until-fail:15`
→ `100% tests passed` (no wall-clock assertion in either; the fake `MtvuClock` drives every valve/park check).

- `GifDrainThread` (`tests/gif/test_gif_drain_thread.cpp`): OLD cases a–d ported over the REAL arbiter + parser
  (`CountingGsSink`); the worker side is a fake `GifMarkerOwner` whose filling thread answers the routing questions,
  so fills go through `GifArbiter::submit` (ledger + marker fill = the real worker-side path). Adds: `boundary_stats`
  census, `DrainIdleGeneration` (nonzero+stable idle / 0 holding / advances / 1 stopped), foreign `drain()` = kick
  (`PassGeneration ≥ gen+2`), `clear()` after stop settles the ledger, restart delivers again.
- `GifDeferBoundaryOrder` (`tests/gif/test_gif_defer_boundary_order.cpp`): OLD cases a–g (A+D TEXFLUSH packets so
  the `RegisterOpStream` carries the order) + (h) `clear()` detaches unsealed markers and settles bytes at their seal,
  (i) inline vs deferred (drain thread) identical register ops (minus the per-pass `Finish`) and identical hook order,
  hook on the drain owner in both modes, + the 10k soak with a real worker thread (`mt19937(0x51680)`), zero
  violations, `enqueued == delivered == hook fires`, ledger 0 after every iteration.
- `VuRunaheadGuard` (`tests/vu/test_runahead_guard.cpp`): every OLD assert (1–8) + `EffMaxFrames` {2,off}→2 /
  {2,on}→3 / {0,1,3,4,on} unchanged / no predicate / negative → 0, `Over()`, `FramePark` on the fake clock (park at
  t0, still parked at +100 ms−1 ns, valve at +100 ms, progress release without valve, cap 0 never).
- `VuMtvuBackpressure` (`tests/vu/test_mtvu_backpressure.cpp`): byte high water (33 × 1 MiB frees to ≤ 7/8, no
  release before), stall valve (1 wait, `StallValveHits == 1`), shrinking bytes never trip it, frame park released by
  progress (50 × 500 µs = 25 ms fake, no valve, one eager drain per iteration), frozen depth → valve at exactly 200
  polls = 100 ms (per-engagement re-arm), depth 0 never parks, lifted cap, both arms in turn, shutdown flag, sync
  waiter balance, spin clamp / recency.

### Oracle / gate status

```
$ cmake --build --preset macos-dev -j4          # 0 warnings (grep -ci warning → 0)
$ ctest --preset macos-dev                       # bounded: verify/run_bounded.sh 900 /tmp/p5/5G/ctest_full.log
100% tests passed, 0 tests failed out of 144
Total Test time (real) = 513.80 sec
  1/144 Test   #1: LintOneClass ........................   Passed
  3/144 Test   #3: LintEnginePurity ....................   Passed
 19/144 Test  #19: CoreSingleGetenvSite ................   Passed
 91/144 Test  #91: GifArbiterOrder .....................   Passed      (5A inline contract intact)
$ python3 tests/lint/lint_one_class.py --root .   → LintOneClass: 475 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . → LintEnginePurity: 370 files scanned, 0 game tokens -> PASS
```
No OLD binary was run (this half has no tool oracle; the `vif_replay` arms are part 2).

### Findings (→ plan §7.1)

| Id | Finding | Disposition |
|---|---|---|
| P5-5G-DRAIN-CTOR | The brief's `GifDrainThread(GifArbiter&, GifParser&)`: the parser reference has no use — the arbiter owns the parser and the loop calls `GifArbiter::DrainPass()`. | Ctor is `GifDrainThread(GifArbiter&)`; PLAN §2.1 row text to be aligned. |
| P5-5G-COLLECT-GATE | OLD promoted the inbox only while `vu1_async::IsRunning()`; with the drain thread up and the worker down (lazy MTVU start) submits reached the inbox but `drain()` never promoted them. | `DrainPass` runs `collectReady()` whenever `PendingSlots() != 0` (lock-free check; observably identical wherever OLD's gate held; the inline harness path never holds a slot). |
| P5-5G-PIPELINE-SEAM | `BackpressureIfNeeded` read nine `vu1_async` globals; the class must be proven before the worker exists. | `MtvuPipeline` abstract (10 methods) implemented by the worker in part 2; the fake in `VuMtvuBackpressure` advances fake time inside the waits. |
| P5-5G-FRAME-WAIT | OLD's frame-only wait is `sleep_for(500 µs)`; the tree forbids `sleep_for` in `engine`. | `MtvuPipeline::WaitFrameProgress(timeout_ns)` — the worker implements it as a timed `ps2r::Park::WaitFor` (timeout IS the OLD behaviour; the present thread may notify later). |
| P5-5G-GUARD-CLOCK | The 100 ms frame valve lived in the park loop (`frame_deadline`). | `RunaheadGuard::FramePark(declared, presented, engaged_at_ns)` owns it with the injected `MtvuClock`; `MtvuBackpressure` passes its engagement `t0`. |
| P5-5G-TSAN-TWIN | OLD `GifDrainThreadTsan` instrumented the header-inline arbiter inside the test TU; here the arbiter body is in `ps2r_gif`, so a per-test `-fsanitize=thread` row would instrument only the test. | Not added as a ctest row; a whole-tree TSAN configure is the honest twin (part 2 may run one bounded under `/tmp/p5/5G/build-tsan` and quote it). |
| P5-5G-LINT-QUOTE | `lint_one_class.py` treats C++14 digit separators (`1'000'000`) as a string quote, hiding the rest of the line from the primary-type scan (`FakeClock` in `test_runahead_guard.cpp` was not counted). | Tests nest their fakes inside the rig named after the file regardless; lint fix is a `tests/lint` row (read-only this phase). |

### HANDOFF (5G part 1 → 5G part 2)

**Done:** `GifArbiter` MTVU half + `GifDrainThread` + `RunaheadGuard` + `MtvuBackpressure` (+ `MtvuSettings`,
`MtvuClock`, `MtvuPipeline`, `GifMarkerOwner`, `GifBoundaryHook`, `GifMtvuMarker`) with four ctest rows green; full
battery 144/144; lints 0; getenv gate 1; build 0 warnings; READMEs current (`engine/gif`, `engine/vu/tiers`,
`tests/gif`, `tests/vu` new). No commits. Helper scripts under `/tmp/p5/5G/` (`fix*.py`, `readmes.py`, `ctest_full.log`);
one CMake edit used a `python3 -` heredoc before switching to files (process note); no process left running.

**Not done (part 2):**
1. `MtvuWorker` (FACTS §B.1–B.3; placement per §F.1 — recommended `engine/vif/` since it takes `Vif&`): implements
   `GifMarkerOwner` (registers via `arbiter.SetMarkerOwner(this)` in `Start`, clears in `Stop`) and `MtvuPipeline`
   (binds `backpressure.Bind(this)`); `Segment {Data, ResetLatch, Batch}`, record `[u32 len][u8 tte][pad3]`,
   `kBatchFlushBytes/Records`, `SubmitVif1 / SubmitVif1Staged / FlushVif1Staged / SubmitResetLatch` (each opens a
   marker via `arbiter.openMarker()`; `SubmitVif1*` call `backpressure.Engage()` first), `ProcessSegment` (seal via
   `arbiter.sealMarker`, `seal_count` seq_cst bump, `inflight` release), `WorkerLoop` (batched pop, spin tier from
   `MtvuBackpressure::SpinArmed / kSpinBudgetNs / CpuRelax`), 32 MiB pthread stack, `vif.BindVu1(&worker_vu1)`.
2. `MtvuSync` (§B.6) + `SyncSite` enum; `Sync` idle fast path + eager loop + `arbiter.DrainFence()` tail;
   `EagerDrainInvoke` guard (`drain_busy_` exchange + thread_local) — `MtvuPipeline::EagerDrain / MayPark` route
   through it; `TryEagerDrain / TryParkDrain` on `arbiter.hasSealedReady()`; `TryVif1StatBusyView` `0x03|1<<2|16<<24`.
3. Dispatcher step 2: `VuTierDispatch::SetMtvuHook(MtvuWorker::Redirect, &worker)` on the INLINE unit (§B.7/§F.4).
4. DMAC seam: `ChainWalker::SetVif1StagedFeed(FeedThunk, FlushThunk, ResetThunk, &worker)` (5F HANDOFF row 7).
5. `tools/vif_replay`: `--mtvu N [--vif-fast]`, `--runahead --depth 2`, `--runahead-off --depth 0` per FACTS §D
   (rig: `GifArbiter arbiter(parser, &hook)`, `SetSubmitObserver` + `SetDrainObserver`, two `VuUnit(1,&arbiter)`,
   `GifDrainThread` started for pass 3; the OLD output grammar tokens verbatim — P5-5G-ORACLE-GRAMMAR); ctest rows
   `VifReplayMtvuEquivalence(Fast)` RUN_SERIAL, `VifRunaheadBound(Off)` RUN_SERIAL, `VifArmsVsOld --arms all`;
   bounded OLD twins quoted.
6. `tests/vif/test_mtvu_worker.cpp` (`VifMtvuWorker`, FACTS §F.6) — record layout, flush thresholds, marker per
   segment, `IsBusy`, STAT busy view, Stop drains queued work.
7. Integration rows carried (FACTS §F.10): MMIO fence (`GifArbiter::DrainFence()` before GIF-register access while
   the thread runs), VIF1/VU1 window syncs, boundary HLEs (`ChaseVif1Tail → noteFeedForBoundary → openBoundary +
   drain` under MTVU), frame counters → `SetFrameCounters`, `low_band` from the pacer, `Tripwire` feed on the
   graphics thread, `Config → MtvuSettings`, thread start/stop order (worker before drain thread; CPU → worker →
   drain thread → GFX), savestate quiesce via `DrainIdleGeneration` twice-equal.

**Deterministic profile (P5-SYNC-FIRST):** `engine.mtvu = false` → no `MtvuWorker`, no `GifDrainThread`,
`SetMarkerOwner / AttachDrainThread` stay null, `drain()` is the 5A inline body (`PendingSlots() == 0` → no
`collectReady`), none of 5G's code runs on a harness leg; it is proven only by `tests/gif`, `tests/vu` and (part 2)
`vif_replay`.

**Gate status:** GREEN (quoted above).

**Next command (part 2):** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev -R "Gif|Vu|Vif|Dmac"`
(expect all Passed), then read `/tmp/p5/5G/FACTS.md` §B, §D, §F.1/F.3–F.6 for `MtvuWorker` / `MtvuSync` / the arms.

### 5G part 2 (MtvuWorker + MtvuSync + dispatcher hook + DMAC staged feed + arms) (2026-09-28, ≈45 min + 40 min finish)

Sources: `/tmp/p5/5G/FACTS.md` §B.1–B.3, §B.6–B.8, §D, §F.3–F.5, §F.7–F.10; PLAN §2.3 rows `mtvu_worker` /
`mtvu_sync`, §3.5, §4.2, §7 row 5G. Part 2 was interrupted after the engine files, the tool arms and the worker test
had been written and resumed by a finishing session (P5-5G-RESUME): the finishing session registered the worker
test, wrote `VuMtvuSync` and `DmacMtvuFeed`, reran the gates and wrote this record.

### Landed (files)

| File | Class | OLD source transplanted |
|---|---|---|
| `engine/vu/tiers/mtvu_vif_feed.h` | `MtvuVifFeed` abstract (`ProcessVif1 / SetVif1TteFeed / ResetVif1Latch / BindVu1`) — the worker's view of VIF1 so `engine/vu` never includes `engine/vif` (FACTS §F.1 cycle fix); `Vif` implements it (`engine/vif/vif.h:39, :71`) | the `vif::ProcessData(1, …)` / `SetTteFeed` / `ResetLatch` / `CtxFor(1)` rebinding calls of `vu1_async.cpp:516-606`, `vif.cpp:39-42` |
| `engine/vu/tiers/mtvu_worker.h/.cpp` | `MtvuWorker(MtvuVifFeed&, VuUnit& worker_vu1, GifArbiter&, MtvuBackpressure&, MtvuSettings) : GifMarkerOwner, MtvuPipeline` — `Segment {Data, ResetLatch, Batch}`, Batch records `[u32 len][u8 tte][pad3]`, `kBatchFlushBytes = 32 KiB`, `kBatchFlushRecords = 32`, `kStackBytes = 32 MiB`; `Start(on_start, user)` (pthread with the 32 MiB stack — `thread_manager.cpp:259-270` precedent; `regs().Reset()`, `BindVu1(worker)`, `SetMarkerOwner(this)`, `backpressure.Bind(this)`; a failed `pthread_create` undoes the binding) / `Stop` (stop flag + final pass, join, unbind) / `IsRunning`; producer edge `SubmitVif1` / `SubmitVif1Staged` / `FlushVif1Staged` / `SubmitResetLatch` (+ `StagedFeedThunk / StagedFlushThunk / StagedResetThunk` for the DMAC seam; `Engage()` before every data submit; one `openMarker()` per Data / Batch segment); `Push` (inflight++ before publication, batched wake on the empty→non-empty edge, `submit_seq_` release); `WorkerLoop` (batched pop under one lock, spin tier polling `submit_seq_` for `kSpinBudgetNs` armed by `SpinArmed(prev, now)`, `cv_idle_` on empty); `ProcessSegment` (ReplaySpan with the TTE bracket, Batch record walk, `active_marker_` on the worker thread only) + `SealAndRetire` (`sealMarker`, `seal_count_` seq_cst, locked wake only if `sync_waiters_ > 0`, `inflight_` release LAST); `GifMarkerOwner` (`Running / OnWorkerThread` via a thread-local / `ActiveMarker`); `MtvuPipeline` (`MayPark / PipelineBytes = pending + arbiter.PendingBytes() / WorkerIdle / SealCount / EagerDrain / WaitIdleOrSeal / WaitFrameProgress (ps2r::Park timed wait) / Enter-LeaveSyncWait`); the wait surface `WaitIdle / TryDrainGuarded (drain_busy_ exchange + thread-local in-drain) / InEagerDrain / PendingBytes / NotifyFrameProgress` | `vu1_async.cpp:47-90` (ring + segment), `:1235-1364` (producer edge), `:516-606` (ProcessSegment), `:652-762` (WorkerLoop), `:1084-1112` (Start), `:1127-1155` (Stop), `:433-440` (EagerDrainInvoke), `:608-650` (spin constants) |
| `engine/vu/tiers/mtvu_sync.h/.cpp` | `SyncSite` enum (OLD ids 0..11 in order) + `MtvuSync(MtvuWorker&, GifArbiter&)` — `Sync(site)`: reentrancy guard (worker down / on the worker), idle fast path (+ `DrainFence` when the drain thread runs and `hasPending`), else the eager loop (`EnterSyncWait`; `seen = SealCount()` BEFORE the guarded drain; `WaitIdleOrSeal(500 µs, seen)`; `LeaveSyncWait`) or a plain `WaitIdle` when nested in a drain, then the `DrainFence` tail; `NoteSiteSkipped` (per-site relaxed counter, census only); `TryVif1StatBusyView` → `kVif1StatBusyView = 0x03 | 1<<2 | 16<<24` while in flight; `TryEagerDrain` / `TryParkDrain` on `hasSealedReady`; `DeferOn ≡ worker.IsRunning()`; `SyncCount / SkippedCount`; `Redirect(user, unit, pc)` (false if the worker is down / on the worker / `&unit == &worker.Vu1Unit()`; else `Sync(Vu1EeKick)` + `Vu1Unit().Execute(pc)`) with `InstallInto(VuTierDispatch&)` / `UninstallFrom` | `vu1_async.cpp:1364-1530` (Sync), `:1531-1565` (eager / TryEagerDrain), `:1569` (NoteSiteSkipped), `:1586-1599` (STAT view), `:1615-1687` (gates / TryParkDrain), `vu1_async.h:176-195` (sites); `vu.cpp:4808-4816` (the EE-kick redirect) |
| `engine/vu/tiers/mtvu_clock.h/.cpp` (part 1; consumed here) | `MtvuClock` — the fake clock drives `VuMtvuSync` / `VuMtvuWorker` backpressure and the `--runahead` arm's consumer | `perf::NowNs()` uses in `vu1_async.cpp:861-989` |
| `engine/vu/tiers/mtvu_pipeline.h` (part 1; implemented here) | `MtvuPipeline` — implemented by `MtvuWorker` (the 10 members above) | `vu1_async.cpp:861-989` (the accessed globals) |
| `engine/vu/tiers/vu_tier_dispatch.h/.cpp` (5C; step 2 wired) | the `mtvu` rung of the ladder `hle → mtvu → tier` is now installed by `MtvuSync::InstallInto` on the INLINE unit's dispatch (composition root / tool); the worker unit's dispatch keeps the hook null | `vu.cpp:4808-4816` |
| `engine/dmac/chain_walker.h/.cpp` (5F seam; consumed) | `ChainWalker::SetVif1StagedFeed(feed, flush, reset, user)` / `HasVif1StagedFeed` — the only DMAC→VIF1 feed shape under MTVU (`PS2_EEFEED` B1) | `dmac.cpp:1664-2707` staged submit |
| `engine/dmac/dmac_mtvu_feed.h/.cpp` | `DmacMtvuFeed(ChainWalker&, MtvuWorker&)` — installs the three worker thunks on the seam at construction, `Uninstall` / destruction restore the inline `Vif` feed; constructed by the composition root only under `MtvuSettings::enabled` | the `PS2_EEFEED` B1 install of `dmac.cpp` + `vu1_async.cpp:1235-1364` |
| `tools/vif_replay/vif_replay.cpp` (+ `CMakeLists.txt`, `README.md`) | `--mtvu <N> [--vif-fast]` (`mtvu_test`: three synthetic chains per iteration walked inline / through the worker ring staged+per-span with the VIF1_STAT poll loop / through the ring with mid-stream eager drains + one deferred boundary per chain under `GifDrainThread`; the fixed structural wait-point case; OLD output grammar verbatim — P5-5G-ORACLE-GRAMMAR), `--runahead [--depth N]` / `--runahead-off [--depth N]` (`runahead_arm`: A / B / C on an injected `MtvuClock` whose reads advance fake time by the 500 µs poll and present frame 9 at +25 ms) | `tests/vif_replay.cpp:357-1272` (`mtvu_test::Run`), `:1642-1726` (`runahead_test::Run`), `:1728-1781` (main / arm dispatch); `tests/CMakeLists.txt:1408-1444` |
| `tools/vif_replay/vif_arms_vs_old.py` | `--arms all` = the 6 surviving arms (2 UNPACK + 2 MTVU + 2 runahead) with `TIMING_TOKENS` blanked on both sides (`N busy views`, `N pkts eager-drained`, `(k/N iters)`, `parked Nms`, `at Nms`, `(Nms)`), `[VU-MODE]` dropped, OLD reference re-made bounded with `--refresh-old` | `old_vif_arms.log`; PLAN §4.2 |
| `tests/vif/CMakeLists.txt` | `VifMtvuSubmitEquivalence` (`--mtvu 20`), `VifMtvuSubmitEquivalenceFast` (`--mtvu 20 --vif-fast`) RUN_SERIAL TIMEOUT 300; `VifRunaheadBound` (`--runahead --depth 2`), `VifRunaheadBoundOff` (`--runahead-off --depth 0`) RUN_SERIAL; `VifArmsVsOld` switched to `--arms all` | OLD rows `tests/CMakeLists.txt:1408-1444` |
| `tests/vu/test_mtvu_worker.cpp` → `VuMtvuWorker` (RUN_SERIAL; links `ps2r_vif`) | the worker over the real `Vif` + two `VuUnit(1)` + arbiter (see Tests) | — |
| `tests/vu/test_mtvu_sync.cpp` → `VuMtvuSync` (RUN_SERIAL; links `ps2r_vif`) | the EE side (see Tests) | — |
| `tests/dmac/test_dmac_mtvu_feed.cpp` → `DmacMtvuFeed` (RUN_SERIAL) | the DMAC walk through the worker (see Tests) | — |
| READMEs | `engine/vu/tiers/README.md` (feed / worker / sync rows replace the part-2 placeholder), `engine/dmac/README.md` (`dmac_mtvu_feed` row), `tests/vu/README.md` (two rows), `tests/dmac/README.md` (one row), `tools/vif_replay/README.md` (the arm rows replace the "not available until 5G" stub text) | — |

Deviations from FACTS / the part-1 HANDOFF (recorded): the worker lives in `engine/vu/tiers/` (not `engine/vif/`)
behind `MtvuVifFeed` (P5-5G-VIF-FEED-SEAM); the ctor is `(MtvuVifFeed&, VuUnit&, GifArbiter&, MtvuBackpressure&,
MtvuSettings)` — the backpressure is constructed by the owner and bound, not owned by the worker; the redirect is
`MtvuSync::Redirect` installed by `MtvuSync::InstallInto` (the HANDOFF named `MtvuWorker::Redirect`); the NEW ctest
rows for the `--mtvu` arms are named `VifMtvuSubmitEquivalence(Fast)` (the driver's arm names keep the OLD
`VifReplayMtvuEquivalence(Fast)`) — P5-5G-ARM-NAMES; the worker test lives in `tests/vu` (not `tests/vif`).

### Flags applied

| Flag | Outcome |
|---|---|
| `PS2_MTVU` (`vu1_async.cpp:1011`) | NORM (P3 row) — `MtvuSettings::enabled`; every MTVU object exists only when the composition root constructs it |
| `PS2_MTVU_DEFER` (`:1656`) | NORM — `MtvuSync::DeferOn() ≡ worker.IsRunning()`; no latch, no off arm |
| `PS2_MTVU_EAGER_DRAIN` (`:1537`) | NORM — the eager loop is the only non-nested `Sync` path (the plain wait survives only for a `Sync` nested in a drain) |
| `PS2_MTVU_VBSYNC` (`:1619`) | NORM — the vblank boundary is unconditional (no conditional-skip arm; `VbsyncGateOn` not written) |
| `PS2_MTVU_MAX_FRAMES` (`runahead_guard.h:60-68`, tool `vif_replay.cpp:1774/:1779`) | SETTING — `MtvuSettings::depth_frames`; the tool's `--depth N` |
| `PS2_MTVU_SPIN` / `PS2_MTVU_SPIN_NS` (`:610`, `:627`) | NORM — the worker's spin tier always armed by recency, `kSpinBudgetNs = 40000` (part-1 constants consumed) |
| `PS2_EEFEED` (`dmac.cpp`) | NORM B1 — the staged Batch is the only DMAC→worker feed shape (`ChainWalker::SetVif1StagedFeed` ← `DmacMtvuFeed`) |
| `PS2_MTVU_PATHSYNC_RELAX` (`:1642`) | not written — `SceGsSyncPath` under `DeferOn` skips like the other GS syncs, unconditionally |
| `PS2_VU1_EXEC_ASYNC` / `_IDLE_SPIN_NS` / `_WAIT_SPIN_NS`, `OnExecThread()` disjuncts | not written (DELETE); the two `ExecOn` arms dropped |
| `PS2_PERF` announce / census lines, `VIF_REPLAY_MTVU_DEBUG`, `ringlog::` | DIAG — omitted |

### Tests added (ctest names)

```
132/151 Test #132: VuMtvuWorker ........................   Passed    0.01 sec
133/151 Test #133: VuMtvuSync ..........................   Passed    0.01 sec
138/151 Test #138: VifMtvuSubmitEquivalence ............   Passed    0.01 sec
139/151 Test #139: VifMtvuSubmitEquivalenceFast ........   Passed    0.01 sec
140/151 Test #140: VifRunaheadBound ....................   Passed    0.19 sec
141/151 Test #141: VifRunaheadBoundOff .................   Passed    0.01 sec
142/151 Test #142: VifArmsVsOld ........................   Passed   13.22 sec
151/151 Test #151: DmacMtvuFeed ........................   Passed    0.19 sec
```
Test outputs: `test_mtvu_worker: ALL PASS`, `test_mtvu_sync: ALL PASS`, `DmacMtvuFeed: PASS`.

- `VuMtvuWorker` (`tests/vu/test_mtvu_worker.cpp`): one VIF1 chain (STCYCL, MPG 4 bundles with `vi1 = 2; XGKICK
  vi1; E`, UNPACK V4-32 packet, MSCAL, DIRECT packet) fed inline, per span through `SubmitVif1`, staged (one Batch
  per walk) and through the three `Staged*Thunk`s → identical VU1 data + micro memory, identical `VUregs`,
  identical `RegisterOpStream` hash + count after the drain; `Start` binds VIF1 to the worker unit and `Stop`
  rebinds the inline unit; the TTE bracket equals `SetTteFeed` inline; the reset-latch segment drops a partial
  quadword like `ResetLatch`; Batch record layout `kBatchRecordHeader + 16`, the 32nd record and the 32 KiB byte
  limit flush, an empty flush submits nothing, a flush / submit on a stopped worker drops; Stop drains 50 queued
  chains (ledger 0, every marker sealed and drained, kicks delivered) and restarts.
- `VuMtvuSync` (`tests/vu/test_mtvu_sync.cpp`): a gated fake `MtvuVifFeed` parks the worker inside `ProcessVif1`
  so `Sync` takes the loop path deterministically — an A+D PATH3 packet submitted ahead of the span is what the
  loop's guarded drain delivers on the EE thread, and the drain observer releases the gate (`delivered == 1` after
  `Sync`, 0 before); `TryVif1StatBusyView` true with `0x03 | 1<<2 | 16<<24` (== `kVif1StatBusyView`) while in
  flight, false when idle / down; `SyncCount` per site (nothing counted while down; the idle fast path counts and
  drains nothing); `NoteSiteSkipped` per-site counters; `DeferOn` follows `IsRunning`; `TryEagerDrain` with
  nothing sealed drains nothing. Real rig: the chain through the worker + `Sync(SceGsSyncV)` + `TryEagerDrain` →
  `PendingSlots() == 0`, ops hash + count == inline; `TryParkDrain` delivers nothing more. Redirect: `InstallInto
  (vu1.dispatch())` then `vu1.Execute(0)` on the INLINE unit → `SyncCount(Vu1EeKick) == 1`, worker idle at the
  run, ops hash == the inline `Execute`, the worker unit's `VUregs` / micro / data == inline, the inline unit's
  micro all zero and `VI[1] == 0`; `Redirect` on the worker unit itself → false; after `UninstallFrom` + `Stop`
  → false, no count. Fake `MtvuClock` on the backpressure; every wait is a predicate wait.
- `DmacMtvuFeed` (`tests/dmac/test_dmac_mtvu_feed.cpp`): the `DmacVifChain` TTE chain (CNT with STCYCL CL2/WL1 in
  the tag words → UNPACK V4-32 → MSCAL `iaddiu vi1, 5`) walked inline vs through `DmacMtvuFeed` → worker unit after
  `FlushDeferredVif1` + `Sync(SceDmaSync)`: identical data memory + `VUregs`, `cycle == 0x0102`, one Batch segment,
  stage empty, the inline unit's `VI[1] == 0`; `Install / Uninstall` and destruction toggle `HasVif1StagedFeed`.

Stability: `ctest -R 'Mtvu|GifDrainThread|GifDeferBoundaryOrder|VifMtvu|VifRunahead' --repeat until-fail:3` →
`100% tests passed, 0 tests failed out of 9` (`/tmp/p5/5G/fin_repeat.log`; run before `DmacMtvuFeed` existed).

### Oracle / gate status

The six arms, NEW tool vs the frozen OLD `tests/vif_replay` re-made bounded (`python3 tools/vif_replay/vif_arms_vs_old.py
--arms all --out /tmp/p5/5G/orch/arms`, log `/tmp/p5/5G/orch/arms.log`):

```
ARM VifReplayMtvuEquivalence SAME old_exit=0 new_exit=0 lines_old=4 lines_new=4        # vif_replay --mtvu 20
ARM VifReplayMtvuEquivalenceFast SAME old_exit=0 new_exit=0 lines_old=4 lines_new=4    # vif_replay --mtvu 20 --vif-fast
ARM VifUnpackBulkEquivalence SAME old_exit=0 new_exit=0 lines_old=32 lines_new=32      # vif_replay --unpack-bulk
ARM VifUnpackBulkEquivalenceFast SAME old_exit=0 new_exit=0 lines_old=32 lines_new=32  # vif_replay --unpack-bulk --vif-fast
ARM VifRunaheadBound SAME old_exit=0 new_exit=0 lines_old=4 lines_new=4                # OLD PS2_MTVU_MAX_FRAMES=2 --runahead  | NEW --runahead --depth 2
ARM VifRunaheadBoundOff SAME old_exit=0 new_exit=0 lines_old=3 lines_new=3             # OLD PS2_MTVU_MAX_FRAMES=0 --runahead-off | NEW --runahead-off --depth 0
VIFARMS arms=6 same=6 diff=0
```
NEW arm lines (`/tmp/p5/5G/arm.log`):
```
[W13R3-E3] structural wait-point case: PASS (3 packets, vif_fast=0)
[W10-EEFEED] staged-feed iterations: 10 of 20 (worker Batch segments: 30)
MTVU EQUIVALENCE PASS: 20 iterations, 120 packets total, byte-identical VU1 data+code mem, identical packet census+order, identical VF/VI/TPC; STAT poll: 969 busy views, converged every iteration, monotone idle; EAGER-DRAIN pass: order+mem identical, 88 pkts eager-drained before the post-wait drain (19/20 iters); BOUNDARIES [R168-DEFER]: 60 enqueued+delivered position-exact (one per chain, drain-thread only, hook fired 60)
[W13R3-E3] structural wait-point case: PASS (3 packets, vif_fast=1)
MTVU EQUIVALENCE PASS: 20 iterations, 120 packets total, byte-identical VU1 data+code mem, identical packet census+order, identical VF/VI/TPC; STAT poll: 1150 busy views, converged every iteration, monotone idle; EAGER-DRAIN pass: order+mem identical, 100 pkts eager-drained before the post-wait drain (20/20 iters); BOUNDARIES [R168-DEFER]: 60 enqueued+delivered position-exact (one per chain, drain-thread only, hook fired 60)
A ok: parked 25ms, released by frame progress
B ok: frozen depth released by valve at 100ms
runahead selftest: PASS
C ok: gate off, no park (0ms)
runahead selftest: PASS
```
(the busy-view / eager-drained / iters counts are the timing tokens PLAN §4.2 excludes; the OLD lines carry different
values there — 720 / 87 / 17 of 20 in `old_vif_arms.log` — and every other token is identical).

**G6 ✔** — the 6 surviving arms `VifReplayMtvuEquivalence`, `VifReplayMtvuEquivalenceFast`, `VifUnpackBulkEquivalence`,
`VifUnpackBulkEquivalenceFast`, `VifRunaheadBound`, `VifRunaheadBoundOff` SAME vs OLD; the exec-async arms
`VifReplayMtvuEquivalenceExecOn(Fast)` (`PS2_VU1_EXEC_ASYNC=1`) are dropped by plan P5-VIF-ARMS-SYNTHETIC.

```
$ cmake --build --preset macos-dev -j4          # 0 warnings (grep -ci warning /tmp/p5/5G/fin_build8.log → 0)
$ ctest --preset macos-dev                       # bounded: verify/run_bounded.sh 900 /tmp/p5/5G/fin_ctest_full2.log (two full runs, identical outcome)
99% tests passed, 1 tests failed out of 151
Total Test time (real) = 830.50 sec
The following tests FAILED:
	 56 - EeRunTokenAndEventQueue (Timeout)       # read-only Phase-4 row; see P5-5G-EESCHED-FLAKE
  1/151 Test   #1: LintOneClass ........................   Passed
  3/151 Test   #3: LintEnginePurity ....................   Passed
 19/151 Test  #19: CoreSingleGetenvSite ................   Passed
 91/151 Test  #91: GifArbiterOrder .....................   Passed
 92/151 Test  #92: GifDrainThread ......................   Passed
 93/151 Test  #93: GifDeferBoundaryOrder ...............   Passed
$ ctest --preset macos-dev -R EeRunTokenAndEventQueue --repeat until-fail:5   # /tmp/p5/5G/fin_eesched_rep.log
1/1 Test #56: EeRunTokenAndEventQueue ..........   Passed    0.09 sec
100% tests passed, 0 tests failed out of 1
$ ctest --preset macos-dev -R 'Mtvu|GifDrainThread|GifDeferBoundaryOrder|VifMtvu|VifRunahead' --repeat until-fail:3
100% tests passed, 0 tests failed out of 9
$ python3 tests/lint/lint_one_class.py --root .   → LintOneClass: 485 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . → LintEnginePurity: 377 files scanned, 0 game tokens -> PASS
```

**Deterministic profile (P5-SYNC-FIRST):** unchanged from part 1 — `engine.mtvu = false` constructs no `MtvuWorker`,
`MtvuSync`, `DmacMtvuFeed` or `GifDrainThread`; the dispatcher's MTVU rung stays null; the DMAC feeds `Vif` directly.
None of 5G runs on a harness leg; it is proven by `tests/gif`, `tests/vu`, `tests/dmac` and the `vif_replay` arms.

### Findings (→ plan §7.1)

| Id | Finding | Disposition |
|---|---|---|
| P5-5G-RESUME | Part 2 was interrupted after the engine files, the tool arms and `test_mtvu_worker.cpp` were written (the test unregistered, `test_mtvu_sync.cpp` absent, no record). Inherited-code deviations found on resume: (a) `DmacMtvuFeed` existed, compiled and installed the seam but had no test and no README row; (b) the `--mtvu` ctest rows are named `VifMtvuSubmitEquivalence(Fast)` while the driver keeps the OLD arm names; (c) the worker test links `ps2r_vif`, which the `ps2r_vu_test()` helper cannot express (a second `ps2r_vu` link produced an `ld: duplicate libraries` warning → the row is written out longhand); (d) the worker's spin tier reads `ps2r::Park::NowNs()` directly (no injected clock) — a wall-clock upper bound only, never asserted. | (a) `DmacMtvuFeed` test + README rows added; (b) recorded (P5-5G-ARM-NAMES), names kept; (c) longhand rows for `VuMtvuWorker` / `VuMtvuSync`; (d) recorded, no change. |
| P5-5G-VIF-FEED-SEAM | PLAN §3.5 / FACTS §F.1 placed the worker in `engine/vif/` because it takes `Vif&`; `ps2r_vif` links `ps2r_vu` and the dispatcher hook + backpressure live in `engine/vu/tiers`. | `MtvuVifFeed` abstract in `engine/vu/tiers/`, implemented by `Vif`; the worker stays in `engine/vu/tiers/` with the rest of the MTVU classes; the tests / tool that need the real `Vif` link `ps2r_vif`. |
| P5-5G-ARM-NAMES | NEW ctest rows `VifMtvuSubmitEquivalence(Fast)` vs OLD / driver `VifReplayMtvuEquivalence(Fast)`. | Recorded; the driver (the oracle) keeps the OLD names, so the identity log reads as OLD's. PLAN §4.2 row text to be aligned at 5H. |
| P5-5G-ORACLE-GRAMMAR | The `--mtvu` arm's `[W13R3-E3]` / `[W10-EEFEED]` / `[R168-DEFER]` tokens look like session tags but are OLD output grammar the driver's line identity requires. | Kept in the tool's printf strings only; never in comments or identifiers (lint clean). |
| P5-5G-SYNC-SEAL-RACE | After `Sync` returns, whether the sealed markers are still awaiting collection depends on whether the last seal landed before or after the loop's last drain (both are correct: the stream is inline either way). | `VuMtvuSync` asserts the post-`TryEagerDrain` state (`PendingSlots() == 0`, ops == inline), not `hasSealedReady()` after `Sync`. |
| P5-5G-EESCHED-FLAKE | With two foreign `ctest` batteries + a harness leg running on the same host (the neighbouring `RecompSymbolCensus` took 231 s vs its usual tens of seconds), the read-only Phase-4 row `EeRunTokenAndEventQueue` (`tests/ee/test_ee_sched.cpp`, `sleep_for` / `yield`-paced) printed `FAIL change_priority_moves_to_tail` and then hung past its 120 s TIMEOUT in BOTH full runs (`/tmp/p5/5G/fin_ctest_full.log`, `fin_ctest_full2.log`, 150/151 each); it passes standalone 3/3, under ctest 3/3, with its neighbours 2/2 and `--repeat until-fail:5` 5/5. No `engine/ee` / `tests/ee` file is touched by 5G (`git status` clean there); part 1's full run had it at 0.07 s. | Recorded for the Phase-4 owner (a wall-clock-paced scheduler test: not fixable from this tree). The 5G rows are unaffected; the full battery must be re-run on an idle host before the phase gate. |

### HANDOFF (5G → 5H)

**Done:** `MtvuWorker`, `MtvuSync` (+ `SyncSite`), `MtvuVifFeed`, `DmacMtvuFeed`, the dispatcher step-2 hook
(`MtvuSync::InstallInto`), the DMAC staged feed on the 5F seam, the six `vif_replay` arms + `vif_arms_vs_old.py
--arms all`, ctest rows `VuMtvuWorker`, `VuMtvuSync`, `DmacMtvuFeed`, `VifMtvuSubmitEquivalence(Fast)`,
`VifRunaheadBound(Off)`, `VifArmsVsOld`; G6 ✔ (`VIFARMS arms=6 same=6 diff=0`); READMEs current. No commits.
Helper scripts under `/tmp/p5/5G/` (`fin_*.py`, `fin_*.log`); no process left running.

**Not done (moved to 5H):** the integration rows below (all outside this phase's OWNED paths); the PLAN §2.3 /
§4.2 row-text alignments (P5-5G-DRAIN-CTOR, P5-5G-ARM-NAMES); a whole-tree TSAN configure of the MTVU suites
(P5-5G-TSAN-TWIN); the optional `MtvuClock` injection into the worker's spin tier.

**Integration rows (FACTS §F.10; the 5H checklist for the composition root):**
- MMIO fence: the GIF-register range handler calls `GifArbiter::DrainFence()` before `GifRegs::Read/Write` while the drain thread runs.
- VIF1 reads: `Sync(Vif1RegRead)` on non-STAT VIF1 registers, `Sync(Vif1RegWrite)` on writes + the FIFO window, `TryVif1StatBusyView` on VIF1_STAT (P5-VIF-MMIO-OBSERVABLE: applied by the MMIO router, not by `Vif`).
- VU1 window (`0x11008000+`): `Sync(Vu1Mem)`.
- Boundary HLEs (`gs_hle.cpp:446-494`, `sif_hle.cpp:225-240`): `ChaseVif1Tail → noteFeedForBoundary → if (sync.DeferOn() && worker.IsRunning()) { openBoundary(seq, now); drain(); } else Sync(SceGsSyncV | WaitVblank)`; `SceGsSyncPath / SceDmaSync / SceGifSyncDma` → `NoteSiteSkipped` under `DeferOn` (`gs_hle.cpp:798-802, :944-948, :974-978`); `SceGsResetPath` always `Sync` (`:918`); `PrivPush` (`:1023`).
- Frame counters: `declared` bumped at the boundary enqueue, `presented` at the GFX VSync job (`arg1 == 1`) → `MtvuBackpressure::SetFrameCounters`; `low_band` from the present pacer (Phase 6); `worker.NotifyFrameProgress()` from the present.
- `RunaheadGuard::Tripwire` fed once per genuine present on the graphics thread (`graphics_thread.cpp:2404-2411`; the alarm line is Phase-6 diag).
- Threads: `MtvuWorker::Start` before producer traffic, `GifDrainThread::Start` iff `engine.mtvu`, both with `on_start` → `PlatformMacos::PinCurrentThreadInteractive` (+ thread name `vu1worker`); teardown order CPU thread → worker → drain thread → GFX.
- `Config → MtvuSettings` copy (`engine.mtvu`, `engine.mtvu_depth`); shutdown flag → `MtvuBackpressure::SetShutdownFlag`.
- `DmacMtvuFeed(dmac.walker(), worker)` constructed at composition iff `engine.mtvu`; `MtvuSync::InstallInto(inline_vu1.dispatch())` there too.
- Savestate quiesce: `Sync(Other)` + `GifDrainThread::DrainIdleGeneration()` two equal nonzero reads (OLD `DrainIdleGeneration` contract).
- `TopsObserver` (`Vu1HleRuntime::SetVifTops`) now fires on the worker thread while MTVU runs (FACTS §F.9 item 7).

**Gate status:** build 0 warnings, both lints PASS, getenv gate PASS, every 5G row green (3× repeat), G6 ✔; the full battery is 150/151 twice with only the read-only, load-sensitive `EeRunTokenAndEventQueue` timing out under foreign host load (passes 5/5 alone) — P5-5G-EESCHED-FLAKE; re-run the full battery on an idle host first thing in 5H.

**Next command:** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect all Passed on an idle host —
the 151st row is the Phase-4 `EeRunTokenAndEventQueue`), then the 5H sweep script.

## 5H — sweep + finalization (2026-09-28, ≈ sweep-bound; the 3,293-case sweep started 17:30:53 UTC by the orchestrator)

### Landed (files)

| Path | Class / content | OLD source |
|---|---|---|
| `tools/vu1_replay/sweep_vs_old.sh` | the G5 sweep driver (bash, no primary type): `OUT_DIR [CHUNK] [--old-root DIR] [--new-bin FILE] [--old-bin FILE] [--corpus DIR] [--expect N] [--budget S]`; sorts `<old-root>/vutrace_collect/*.bin`, `split -l CHUNK`, symlinked per-chunk dirs, ONE `verify/run_bounded.sh <budget> logs/chunk_NN.log -- compare_vs_old.py --mode compare --cases dir_NN --out out_NN` per chunk, `logs/chunk_NN.done` (`exit=N`), `progress.txt`, `SWEEP_DONE`, then `aggregate_sweep.py`; resumable (skips chunks with a `.done`); exit 77 when the OLD tool / corpus is absent, 2 on usage / NEW tool missing, else the aggregate's status | `/tmp/p5/5H/sweep.sh` (the orchestrator's one-off, same body) → generalised; PLAN §4.5 |
| `tools/vu1_replay/aggregate_sweep.py` | sums the `CMP cases=N same=M diff=K old_fail=A new_fail=B` footers over `--logs/chunk_*.log`, requires one footer + a `.done` (exit 0/1) per chunk, prints `SWEEP cases= same= diff= old_fail= new_fail= chunks=`, lists `DIFF <case>` rows (copies them to `--diffs` when `--corpus` is given, writes `DIFF_CASES.txt`), `SWEEP PASS|FAIL`; exit 0 iff `diff=0` ∧ all chunks complete ∧ (`--expect` given ⇒ `cases == N`); 1 otherwise; 77 with no chunk log | PLAN §4.5 (`aggregate.py`, never written before 5H) |
| `tests/vu/CMakeLists.txt` | ctest `Vu1CollectSweep` → `sweep_vs_old.sh ${CMAKE_BINARY_DIR}/tests/vu/sweep_vs_old 200 --old-root ${PS2R_OG_ROOT} --new-bin $<TARGET_FILE:vu1_replay> --expect 3293`; `LABELS long TIMEOUT 18000 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE`; `option(PS2R_VU1_SWEEP … OFF)` → `DISABLED TRUE` unless ON | PLAN §7 5H row |
| `tools/vu1_replay/README.md`, `tests/vu/README.md` | rows for the two files and the ctest row incl. the by-hand invocation | — |
| `docs/phases/PHASE5_PLAN.md` | §7.1: `**Disposition at 5H:**` note on all 41 seeded rows (27 planning + 14 `P5-5D-*`); the `(stage rows)` template row replaced by 109 rows (98 stage findings extracted from each stage's `### Findings` table/list, `P5-5E-NO-DRAIN` written by hand from its HANDOFF mentions, the two reviewer-suggested ids `P5-5E-MODE2-COL` / `P5-5F-MEMCLR-SPR`, and eight `P5-5H-*` rows); §6 G4 `entries=140` → `entries=223` (P5-5D-ENTRY-COUNT), G5 registration shape, G6 ctest names (P5-5G-ARM-NAMES). `grep -c "Disposition at 5H"` → 41; `grep -c "^| P5-"` → 150 | — |
| `docs/phases/PHASE5_IMPL.md` | this section; the 5B title (`:295`) now says "three parts" (5B REVIEW N2) | — |

Helpers (FILES under `/tmp/p5/5H/`, all finished, nothing left running): `extract_rows.py` (IMPL → PLAN rows), `apply_plan.py` (the PLAN edit), `add_rows.py`, `fill_g5.py` (the G5 fill), `rows.md`, `PHASE5_PLAN.before.md` (pre-edit copy), the bounded logs `configure.log`, `build.log`, `ctest_half1.log`, `ctest_eesched.log`, `ctest_half2.log`. The sweep (`/tmp/p5/5H/sweep.sh`, started by the orchestrator) was never touched, and no OLD/NEW `vu1_replay` was run over the corpus by this stage.

Exclusion choice recorded (P5-5H-SWEEP-CTEST): `CMakePresets.json` carries no `filter.exclude.label` and is not OWNED, so the row is registered `DISABLED` unless `-DPS2R_VU1_SWEEP=ON` — every edit stays inside `tests/vu/` + `tools/vu1_replay/`. Verified: `ctest --preset macos-dev -N | tail -1` → `Total Tests: 152`; `ctest -N -L long` → `Test #113: Vu1CollectSweep (Disabled)`; `ctest -N -LE long | tail -1` → `Total Tests: 151`. By hand: `cmake --preset macos-dev -DPS2R_VU1_SWEEP=ON && ctest --preset macos-dev -L long`, or `tools/vu1_replay/sweep_vs_old.sh /tmp/sweep 200 --expect 3293` directly (README rows). Self-checks of the tools before wiring (quoted): `aggregate_sweep.py --logs /tmp/p5/5H/logs --expect 3293` on the then-partial logs → `SWEEP cases=800 same=800 diff=0 old_fail=0 new_fail=0 chunks=5` / `CHUNK 04 INCOMPLETE: no \`CMP cases=\` footer (done=None)` / `SWEEP FAIL: cases=800 != expected 3293` / exit 1; `sweep_vs_old.sh /tmp/p5/5H/skiptest --old-root /nonexistent` → `SWEEP SKIP: OLD tool (/nonexistent/build/tests/vu1_replay) or corpus (/nonexistent/vutrace_collect) absent`, exit 77; a fabricated two-line log with one `DIFF` → `SWEEP cases=2 same=1 diff=1 …` + `DIFF b.bin …` + `DIFF_CASES.txt`, exit 1, and the same log with the DIFF turned SAME → `SWEEP PASS`, exit 0.

### Flags applied

None new. 5H writes no module code; the tally against PLAN §5 is below (FLAG TALLY).

### Tests added (ctest names)

`Vu1CollectSweep` (Disabled by default, see above). Under the concurrent sweep load the battery was run in two halves (-j2, each bounded ≤ 900 s):

```
$ verify/run_bounded.sh 900 /tmp/p5/5H/ctest_half1.log -- ctest --preset macos-dev -j2 -E '^(Vu|Vif|Dmac|Gif)'
89/89 Test #56: EeRunTokenAndEventQueue .............***Timeout 120.04 sec
99% tests passed, 1 tests failed out of 89        Total Test time (real) = 189.93 sec
$ verify/run_bounded.sh 700 /tmp/p5/5H/ctest_eesched.log -- ctest --preset macos-dev -R '^EeRunTokenAndEventQueue$' --repeat until-fail:3
    Test #56: EeRunTokenAndEventQueue ..........   Passed    0.07 sec   (×3)
100% tests passed, 0 tests failed out of 1        Total Test time (real) =   0.23 sec
$ verify/run_bounded.sh 900 /tmp/p5/5H/ctest_half2.log -- ctest --preset macos-dev -j2 -R '^(Vu|Vif|Dmac|Gif)'
63/63 Test #113: Vu1CollectSweep ..................***Not Run (Disabled)   0.00 sec
100% tests passed, 0 tests failed out of 62       Total Test time (real) = 334.52 sec
The following tests did not run:  113 - Vu1CollectSweep (Disabled)
```

Half 1 + the standalone re-run + half 2 = 88 + 62 = 150 Phase 0–5 rows PASS + the Phase-4 `EeRunTokenAndEventQueue` PASS ×3 alone (0.07 s each) after a 120 s timeout under the sweep + `-j2` load — the sixth sighting of P5-5G-EESCHED-FLAKE (5C p1, 5D p3, 5E p2, 5G p2 ×2, 5H): the row is `sleep_for`/`yield`-paced Phase-4 code, never Phase-5 code, and passes every time it runs alone; disposition unchanged ("recorded for the Phase-4 owner"). The 5G part-1 full battery on an idle host (`:2144`) remains the last single-invocation `100% … out of 144`; the orchestrator can repeat `ctest --preset macos-dev` once the sweep has released its core (expected `100% tests passed, 0 tests failed out of 151` + the Disabled line).

### Oracle / gate status

```
$ verify/run_bounded.sh 900 /tmp/p5/5H/build.log -- cmake --build --preset macos-dev -j2      # exit 0; grep -ci warning → 0
$ python3 tests/lint/lint_one_class.py --root .        → LintOneClass: 485 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root .    → LintEnginePurity: 377 files scanned, 0 game tokens -> PASS
$ ctest --preset macos-dev -R CoreSingleGetenvSite     → 1/1 Test #19: CoreSingleGetenvSite ... Passed; 100% tests passed
$ python3 tests/core/check_getenv_sites.py --root .    → getenv sites: 1 -> PASS
$ ctest --preset macos-dev -R Vu1LibraryPurity -V      → Vu1LibraryPurity: 389 files scanned under engine,tools, 207 library hashes, 0 hits -> PASS
$ grep -rn '#include.*engine/gs/' engine/gif engine/vu --include='*.h' --include='*.cpp' --include='*.inl' | grep -v 'engine/gs/format/' | wc -l   → 0
$ git -P diff --stat CMakeLists.txt tests/CMakeLists.txt → 2 files changed, 11 insertions(+)      (the 11 lines are quoted in the G1 row)
$ grep -rnoE '\bPS2_[A-Z0-9_]+' engine/gif engine/vu engine/vif engine/dmac tools/vu1_replay tools/vif_replay games/…/vu1_library --include='*.cpp' --include='*.h' --include='*.inl' | wc -l → 1
      (tools/vif_replay/vif_replay.cpp:1246 — the usage text naming the dropped OLD `PS2_VU1_EXEC_ASYNC=1` arms; no getenv, no flag)
```

#### GATE TABLE G1–G9 (PLAN §6)

| Gate | Criterion | Evidence (quoted line + IMPL line ref) | ✔/✘ |
|---|---|---|---|
| G1 | tree green: build 0 warnings; ctest 100 %; both lints; `CoreSingleGetenvSite`=1; CMake diffs = the append-only lines | build (this section, `build.log`): exit 0, `warnings=0`. ctest, single invocation on the idle host after the sweep (`ctest_full_idle.log`): **`100% tests passed, 0 tests failed out of 151`**, `Total Test time (real) = 559.18 sec`, `56/152 Test #56: EeRunTokenAndEventQueue ... Passed 0.08 sec`, `113/152 Test #113: Vu1CollectSweep ...***Not Run (Disabled)`. (Under the sweep load the two-half run had one `EeRunTokenAndEventQueue` timeout, then `Passed 0.07 sec` ×3 alone — P5-5G-EESCHED-FLAKE / P5-5H-EESCHED-LOAD, quoted above.) Lints: `LintOneClass: 485 files scanned, 0 findings -> PASS`, `LintEnginePurity: 377 files scanned, 0 game tokens -> PASS`. getenv: `getenv sites: 1 -> PASS` + `CoreSingleGetenvSite … Passed`. CMake: `git -P diff CMakeLists.txt tests/CMakeLists.txt` = 11 added / 0 removed lines — root: `add_subdirectory(engine/gif)`, `(engine/vu)`, `(engine/vif)`, `(engine/dmac)`, `(tools/vu1_replay)`, `(tools/vif_replay)`, `(games/${PS2R_TITLE}/vu1_library)` (each `# Phase 5: …`); tests: `add_subdirectory(gif)`, `(vu)`, `(vif)`, `(dmac)`. **Deviation recorded:** the two `tools/*` lines exceed PLAN §1.5 / CONTRACT §2's literal 9-line list (P5-5H-CMAKE-TOOL-LINES; accepted by the 5C/5D/5G reviews) | ✔ (with the flake and the 2-line deviation recorded) |
| G2 | `--compare` 126/126 vs OLD + per-tier postdata `cmp` | `CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0` (5C p2 `:888`); 16 × `interp=cmp_ok cached=cmp_ok` (`:889-896`); ctest `Vu1ReplayCompareCorpus` re-runs 12 postdata cases (PASS in half 2) | ✔ |
| G3 | `--hle` 126/126 vs OLD (124 PASS + 2 × exit 77) | `HLE cases=126 same=126 diff=0` (5D p3 `:1385`); "124 `HLE PASS`, 2 `### EXIT … = 77`" (`:1388`); ctest `Vu1ReplayHleCorpus`, `Vu1HleGroupA/B/C`, `Vu1HleRef_vu1_case_000{0..7}_1748` PASS (half 2). PLAN's `Vu1Hle_<case>` ×126 collapsed into ONE identity row (P5-5D-CTEST-ROWS) | ✔ |
| G4 | `--catalog` identity + footer + registry self-report | `CATALOG DIFF EMPTY` (66 lines; `:1394`), footer `CATALOG cases=126 images=63 hle_covered=61/63 bundle_coverage=100.00% …`; `REGISTRY entries=223 distinct_variant_hashes=176` + 12 program rows + `REGISTRY programs=12` (`:1399-1413`); 15 program functions (`:1417`). PLAN §6 text `entries=140` corrected to 223 at 5H. ctest `Vu1CatalogIdentity` PASS (half 2) | ✔ |
| G5 | 3,293-case sweep `diff=0`; every chunk log present; `Vu1CollectSweep` registered + run once, line quoted | `$ python3 tools/vu1_replay/aggregate_sweep.py --logs /tmp/p5/5H/logs --expect 3293` → **`SWEEP cases=3293 same=3293 diff=0 old_fail=0 new_fail=0 chunks=17`** / `SWEEP PASS` (exit 0); 17 × `logs/chunk_NN.done` = `exit=0`; 16 chunks `CMP cases=200 same=200 diff=0 old_fail=0 new_fail=0` + chunk 16 `CMP cases=93 same=93 diff=0 old_fail=0 new_fail=0`; `grep ' DIFF '` over the 17 logs → 0; no bounded-runner kill; sweep 17:30:53 → 19:32:13 UTC (2 h 01 min, ≈ 2.2 s/case). `old_fail=0` ⇒ no OLD-own-failure row needed. ctest row `Vu1CollectSweep` registered (Disabled by default — P5-5H-SWEEP-CTEST); the quoted line is the hand run of the same script body (`/tmp/p5/5H/sweep.sh` ≡ `sweep_vs_old.sh`, same driver, same binaries) over the 17 chunk logs (`### G5 result`) | ✔ |
| G6 | `vif_replay` 6 arms identical | `VIFARMS arms=6 same=6 diff=0` (5G p2 `:2314`); six `ARM … SAME old_exit=0 new_exit=0` (`:2308-2313`); every PLAN §4.2 invariant token present in both (`:2317-2328`); UNPACK `VIFARMS arms=2 same=2 diff=0` + 30 `[UNPKB][…] PASS` (5E p2 `:1674-1684`). ctest `VifMtvuSubmitEquivalence(Fast)` (PLAN name `VifReplayMtvuEquivalence(Fast)` — P5-5G-ARM-NAMES), `VifUnpackBulkEquivalence(Fast)`, `VifRunaheadBound(Off)`, `VifArmsVsOld` PASS (half 2) | ✔ |
| G7 | unit-test ports green; SKIP without OLD; NEON twins | 63 Phase-5 rows (62 enabled + the Disabled sweep row) PASS in half 2; PLAN→NEW names: `DmacChainWalker`→`DmacChainWalk`, `Vu1ReplaySelfTest`→`Vu1ReplaySelftest`, `Vu1CachedCompare_<case>`×126→`Vu1ReplayCompareCorpus` (+`VuCachedVsInterpCorpus`), `Vu1Hle_<case>`×126→`Vu1ReplayHleCorpus`+`Vu1HleGroupA/B/C`, `Vu1HleCatalog`→`Vu1CatalogIdentity`, `VifReplayMtvuEquivalence(Fast)`→`VifMtvuSubmitEquivalence(Fast)`, `GifDrainThreadTsan` NOT registered (P5-5G-TSAN-TWIN). NEON twins from bounded `-DPS2R_SIMD=ON` configures: `GifParserEquivalence` case lines IDENTICAL (5A `:245-250`), `VuLazyFlagsAlgebra: PASS (400 sequences, 18060 actions, eager == lazy [NEON fold])` (5B `:380-382`, `:527-530`, `:659-660`), 5C `:794-795`, `VifUnpackBulkEquivalence` SIMD `VIFARMS arms=2 same=2 diff=0` (5E `:1684`, `:1692-1694`; the 5E p1 NEON twin at `:1571-1573` covers the scalar/commands suites only) | ✔ |
| G8 | purity of title data + layering | `Vu1LibraryPurity: 389 files scanned under engine,tools, 207 library hashes, 0 hits -> PASS` (this section; 5D `:1420` said 342 files — the count grew with 5E–5H files, still 0 hits); `LintEnginePurity … 0 game tokens -> PASS`; layering grep `engine/gif` + `engine/vu` → non-`format/` `engine/gs/` includes = 0 (this section); the PLAN's raw 16-hex grep's 4 generic hits are FNV/libc constants (P5-5D-G8-GENERIC-HEX `:1441`) | ✔ |
| G9 | `PHASE5_IMPL.md` complete | 16 stage sub-sections (5A×2, 5B×3, 5C×2, 5D×3, 5E×2, 5F×2, 5G×2) + this 5H section with the G1–G9 table, Module checklist, FLAG TALLY, findings, INTEGRATION CHECKLIST (a)–(k) incl. the two MOVES as rows (h)1–2, and the MODULE GATE SUMMARY; PLAN §7.1 has a disposition on every row (150 `P5-` rows) | ✔ (this file) |

### Module checklist (PLAN §2 rows → landed-in-stage / deferred-with-reason)

| PLAN §2 row | NEW file(s) | Stage |
|---|---|---|
| 2.1 `gif_tag` | `engine/gif/gif_tag.{h,cpp}` `GifTag` | 5A p1 |
| 2.1 `gif_regs` | `engine/gif/gif_regs.{h,cpp}` `GifRegs : MmioHandler` (unregistered) | 5A p1 |
| 2.1 `gs_sink.h` | `engine/gif/gs_sink.h` `GsSink` | 5A p1 |
| 2.1 `register_op_stream` | `engine/gif/register_op_stream.{h,cpp}` (MOVE → `engine/gs/hle/ir/` deferred: `engine/gs` READ-ONLY — checklist (h)1) | 5A p1 |
| 2.1 `gif_parser` | `engine/gif/gif_parser.{h,cpp}` `GifParser` (+`GifParserSettings`) | 5A p2 |
| 2.1 `gif_arbiter` | `engine/gif/gif_arbiter.{h,cpp}` synchronous half (5A p2) + markers/inbox/boundary (5G p1); `gif_boundary_hook.h`, `gif_marker_owner.h`, `gif_mtvu_marker.h` types-only headers | 5A p2 + 5G p1 |
| 2.1 `gif_drain_thread` | `engine/gif/gif_drain_thread.{h,cpp}` `GifDrainThread` (started only under `engine.mtvu` — composition) | 5G p1 |
| 2.1 dropped/folded: `packet_template`, `gif_fast` lanes | decision recorded (5A README row); strip3/rowable lanes inside `gif_parser` | 5A |
| 2.2 `vu1_state.h` | `engine/vu/vu1_state.h` `Vu1State` | 5B p1 |
| 2.2 `vu_unit` | `engine/vu/vu_unit.{h,cpp}` `VuUnit` | 5B p2 |
| 2.2 `decode` (+`.inl`) | `engine/vu/vu_decoder.{h,cpp}` `VuDecoder` (file name follows the class — P5-5B-FILENAME-LINT) | 5B p1 |
| 2.2 `span_classify` | `engine/vu/span_classifier.{h,cpp}` `SpanClassifier` | 5B p1 |
| 2.2 `flag_pipeline` | `engine/vu/flag_pipeline.{h,cpp}` `FlagPipeline` | 5B p2 |
| 2.2 `stall_model` | `engine/vu/stall_model.{h,cpp,inl}` `StallModel` | 5B p2 |
| 2.2 `xgkick` | `engine/vu/xg_kick.{h,cpp}` `XgKick(GifArbiter&, bool paced_drain)` | 5B p2 |
| 2.2 `vu_float` | `engine/vu/vu_float.{h,cpp,inl}` `VuFloat` | 5B p1 |
| 2.2 `chop_round_guard` | `engine/vu/vu_chop_round_guard.{h,cpp}` `VuChopRoundGuard` | 5B p1 |
| 2.2 `vu_image_hash` | `engine/vu/vu_image_hash.{h,cpp}` `VuImageHash` | 5B p1 |
| 2.2 `vu1_case` | `engine/vu/vu1_case.{h,cpp}` `Vu1Case` + `ReadCase/WriteCase` (MOVE → `engine/core/diag/vutrace_case` with the Phase-9 writer — checklist (h)2) | 5B p1 |
| 2.3 `interp_tier` | `engine/vu/tiers/vu_interpreter_tier.{h,cpp}` + `_upper.cpp` / `_lower.cpp` `VuInterpreterTier` | 5B p3 |
| 2.3 `cached_tier` | `engine/vu/tiers/vu_cached_tier.{h,cpp}` + `vu_cached_tier_exec.cpp` `VuCachedTier` | 5C p1 |
| 2.3 `tier_dispatch` | `engine/vu/tiers/vu_tier_dispatch.{h,cpp}` `VuTierDispatch` + `VuSettings` (`hle_enabled=false` default — P5-5H-HLE-DEFAULT-FALSE) | 5C p1 (+5D hook, +5G step 2) |
| 2.3 `mtvu_worker` | `engine/vu/tiers/mtvu_worker.{h,cpp}` `MtvuWorker`; `mtvu_vif_feed.h`, `mtvu_pipeline.h` interfaces; `mtvu_settings.h` `MtvuSettings` | 5G p2 |
| 2.3 `mtvu_backpressure` | `engine/vu/tiers/mtvu_backpressure.{h,cpp}` `MtvuBackpressure`; `mtvu_clock.{h,cpp}` `MtvuClock` | 5G p1 |
| 2.3 `mtvu_sync` | `engine/vu/tiers/mtvu_sync.{h,cpp}` `MtvuSync` + `SyncSite` | 5G p2 |
| 2.3 `runahead_guard` | `engine/vu/tiers/runahead_guard.{h,cpp}` `RunaheadGuard` | 5G p1 |
| 2.4 `hle/vu1_hle_contract.h`, `hle/vu1_family_signature.h` | `engine/vu/hle/vu1_hle_contract.h`, `vu1_family_signature.h`, (+`vu1_library_entry.h`) types-only | 5D p1 |
| 2.4 `hle/vu1_registry` | `engine/vu/hle/vu1_registry.{h,cpp}` `Vu1Registry` | 5D p1 |
| 2.4 `hle/vu1_image_analyzer` | `engine/vu/hle/vu1_image_analyzer.{h,cpp}` `Vu1ImageAnalyzer` | 5D p1 |
| 2.4 `hle/vu1_hle_runtime` | `engine/vu/hle/vu1_hle_runtime.{h,cpp}` `Vu1HleRuntime` (memo, `SetVifTops`, `BumpExternalGeneration`, `Hook`) | 5D p1 |
| 2.4 `hle/kick_lowering` | `engine/vu/hle/kick_lowering.{h,cpp}` `KickLowering` (`TryLower` seam for the renderer phase) | 5D p1 |
| 2.4 `hle/gif_packet_builder` | `engine/vu/hle/gif_packet_builder.{h,cpp}` `GifPacketBuilder` | 5D p1 |
| 2.4 `vu0/libvu0_sincos` | `engine/vu/vu0/lib_vu0_sin_cos.{h,cpp}` `LibVu0SinCos` | 5B p3 |
| 2.4 `vu0/vu0_registry` | `engine/vu/vu0/vu0_registry.{h,cpp}` `Vu0Registry` (no NEW caller — checklist (e)) | 5D p3 |
| 2.4 `gs_packet` / `gs_direct_ops` placement | tool-side `tools/vu1_replay/gs_packet_diff.{h,cpp}` `GsPacketDiff`, `gs_direct_ops_check.{h,cpp}` `GsDirectOpsCheck` | 5D p1 |
| 2.5 `vu1_library` | `games/…/vu1_library/vu1_library.{h,cpp}` `Vu1Library` (223 ordered rows, `Vu1FamilySignature` value, `Get()`) | 5D p1 (+p2/p3 program swaps) |
| 2.5 `family_tristrip`, `textured_tristrip`, `poscolor_strip`, `lit_tristrip` (group A) | `FamilyTristrip`, `TexturedTristrip`, `PoscolorStrip`, `LitTristrip` pairs | 5D p1 |
| 2.5 `skinned_lit_strip`, `indexed_displace`, `zero_kick`, `sprite_text` (group B) | `SkinnedLitStrip`, `IndexedDisplace`, `ZeroKick`, `SpriteText` pairs | 5D p2 |
| 2.5 `crys_programs` (group C) | `crys_programs.{h,cpp}` `CrysPrograms` + `displace_prefix.{h,cpp}` `DisplacePrefix` (split out for the lint) | 5D p3 |
| 2.5 `library_helpers` | `library_common.{h,cpp}`, `qw_ptr.{h,cpp}` `QwPtr`, `near_clipper.{h,cpp}` `NearClipper` (one class per pair — P5-5D-CLASS-NAMES) | 5D p1–p3 |
| 2.5 `clip_gate` | `clip_gate.{h,cpp}` `ClipGate` | 5D p1 |
| 2.5 `vu0_programs` | `vu0_programs.{h,cpp}` `Vu0Programs` (15 entries + 1 subroutine) | 5D p3 |
| 2.6 `vif_regs.h` | `engine/vif/vif_regs.h` `VifRegs` (92 B pinned) + enums | 5E p1 |
| 2.6 `vif` | `engine/vif/vif.{h,cpp}` `Vif : MmioHandler` (unregistered; `VifSettings`) | 5E p1 |
| 2.6 `vif_unpack` | `engine/vif/vif_unpack.{h,cpp}` `VifUnpack` (`VifUnpackSettings`) | 5E p1 |
| 2.6 `vif_fast_span` | `engine/vif/vif_fast_span.{h,cpp}` `VifFastSpan` | 5E p2 |
| 2.6 `vif_unpack_bulk` | `engine/vif/vif_unpack_bulk.{h,cpp}` `VifUnpackBulk` | 5E p1 |
| 2.6 not written: `PS2_VU1_EXEC_ASYNC` seam | not written (DELETE) | 5E |
| 2.7 `dmac_channel.h` | `engine/dmac/dmac_channel.h` `DmacChannelRegs` (28 B) + ids | 5F p1 |
| 2.7 `dmac` | `engine/dmac/dmac.{h,cpp}` `Dmac : MmioHandler` (unregistered; `SavestateSection()` = the ONE `SectionSpec` factory) | 5F p1 |
| 2.7 `chain_walker` | `engine/dmac/chain_walker.{h,cpp}` `ChainWalker` (staged VIF1 seam) | 5F p2 |
| 2.7 `deferred_vif1` | `engine/dmac/dmac_deferred_vif1.{h,cpp}` `DmacDeferredVif1` (`kMaxRetries=120` knowingly not written — P5-5F-SUSPEND-DEAD) | 5F p2 |
| 2.7 `dmac_handlers` | `engine/dmac/dmac_handler_registry.{h,cpp}` `DmacHandlerRegistry` (not handed the syscall table — checklist (d)3) | 5F p2 |
| 2.7 hle NOT installed | `engine/dmac/hle/libdma_bindings.{h,cpp}` `LibdmaBindings` (manifest registration = checklist (d)6) | 5F p2 |
| (extra) `dmac_mtvu_feed` | `engine/dmac/dmac_mtvu_feed.{h,cpp}` `DmacMtvuFeed` (B1 staged submit on the 5F seam) | 5G p2 |
| 2.8 `core/diag/vutrace_case` | written as `engine/vu/vu1_case` (reader + frozen codec); WRITER deferred — `engine/core` READ-ONLY, DIAG outcome, Phase 9 | deferred |
| 2.8 `core/diag/vutrace_collector` | deferred to Phase 9 (triage producer) | deferred |
| 2.8 other DIAG (omit) | all 15 DIAG rows omitted (none mechanism-shaped) | — |
| 2.9 `tools/vu1_replay/vu1_replay.cpp` | `main` + all grammars (`<case>`, `--selftest`, `--mode`, `--compare`, `--bench`, `--dump-kicks`, `--cycle-cap`, `--hle …`, `--catalog`, `--registry`, `--gsops`) | 5C p2 + 5D |
| 2.9 `xgk_capture_sink` | `tools/vu1_replay/xgk_capture_sink.{h,cpp}` `XgkCaptureSink` | 5C p2 |
| 2.9 `gs_packet_diff`, `gs_direct_ops_check` | as 2.4 placement above | 5D p1 |
| 2.9 `tools/vif_replay/vif_replay.cpp` | `main` — `--unpack-bulk [--vif-fast]` (5E p2), `--mtvu N [--vif-fast]`, `--runahead --depth N`, `--runahead-off --depth 0` (5G p2); `vif_arms_vs_old.py` | 5E p2 + 5G p2 |
| 2.9 `tests/gif/` | 6 rows (`GifRegsStat`, `GifParserEquivalence`, `GifArbiterOrder`, `GifDrainThread`, `GifDeferBoundaryOrder`, `Vu1GsopsSeed`); `GifDrainThreadTsan` not registered (whole-tree TSAN configure deferred — P5-5G-TSAN-TWIN) | 5A/5C/5G |
| 2.9 `tests/vu/` | 40 rows incl. `Vu1CollectSweep` (Disabled) | 5B–5H |
| 2.9 `tests/vif/`, `tests/dmac/` | 9 + 9 rows | 5E/5G, 5F/5G |
| drivers (PLAN §4) | `compare_vs_old.py`, `hle_gate.py`, `hle_vs_old.py`, `catalog_vs_old.py`, `vif_arms_vs_old.py`, `check_gsops_seed.py`, `check_vu1_library_purity.py`, `sweep_vs_old.sh`, `aggregate_sweep.py` | 5C–5H |

### FLAG TALLY (PLAN §5: 114 rows = 32 NORM / 2 SETTING / 15 DIAG / 8 TEST-ONLY / 57 DELETE)

| Outcome | PLAN | Applied form (from every stage's "Flags applied") | Named individually / by family / never named |
|---|---|---|---|
| NORM | 32 | winning arm written for 31; `PS2_VU_O3` NOT applied — `ps2r_vu` pinned `-O2 -fno-strict-aliasing -fwrapv` (5B `:322`, 5C `:752`; PLAN §5.1 allowed "-O3 (or -O2 …)") | 32 / 0 / 0 |
| SETTING | 2 | `MtvuSettings::depth_frames = 2` (`:2097`), `VuSettings::vu1_mode = Cached` (`:747`); Config keys exist, the copy is checklist (c) | 2 / 0 / 0 |
| DIAG | 15 | all 15 OMITTED (none trivially mechanism-shaped; the `Vu1Case` writer is Phase 9) | 7 / 8 (`PS2_VUTRACE_*` family `:334`, `:613`, `:865`) / 0 |
| TEST-ONLY | 8 | `PS2_VU1_CYCLE_CAP` → `--cycle-cap N`; `PS2_VIF_FAST` test pin → `--vif-fast`; `PS2_MTVU_MAX_FRAMES` pin → `--depth N`; `VIF_REPLAY_*` ×5 not written; `PS2_GS_DRAWIR_VERIFY` nothing; `PS2_MID_TEMPLATE_FORCEMISS` died with the template | 8 / 0 / 0 |
| DELETE | 57 | not written; proof this section: `grep -rnoE '\bPS2_[A-Z0-9_]+'` over the 7 Phase-5 code dirs → 1 hit = the `vif_replay` usage text naming the dropped `PS2_VU1_EXEC_ASYNC` arms (no getenv) | 23 / 29 (`PS2_S13_*`, `PS2_S15_*`, `PS2_S26_*`, `PS2_S8_*` `:614`; `PS2_R198_*`, `PS2_S8_PKTDUMP*`, `PS2_VU1_EXEC_*` `:2104`; VU-AOT family `:751`) / **5 never named**: `PS2_VU0_SHADOWGATE`, `PS2_VU0_VISCAP`, `PS2_VU0_VISCAP_MINVF`, `PS2_VU0_VISCAP_N`, `PS2_VU0_XCHECK` — inside the `vu.cpp:4334-4999/:5027-5195` probe forest 5B p3 dropped as a RANGE; each grepped today → 0 hits (P5-5H-VU0-DELETE-ROWS-UNNAMED) |
| total | 114 | 114 dispositioned = 32/2/15/8/57 ✔ | 72 / 37 / 5 |

Ledger-gap rows outside the 114 (PLAN §5.6): `PS2_EEFEED` (B1 `:1779`, B2 `:159`), `PS2_MTVU` (`:2247`), `PS2_MTVU_DEFER` (`:2248`), `PS2_PERF` (`:2100`, `:2257`), `PS2_MIPCEN` (`:163`), `PS2_S26_DECAL` (`:165`, `:1783`) — all mentioned. Slip: 5B p2 `:474` labels `PS2_VU0_MODE`/`PS2_VU1_MODE` "SETTING (tier choice)"; PLAN §5.5 says DELETE (5C `:751` is right; nothing was written for them). Zero `getenv` added at any stage.

### Findings (→ plan §7.1)

| Id | Finding | Disposition |
|---|---|---|
| P5-5H-SWEEP | the G5 sweep result | `SWEEP cases=3293 same=3293 diff=0 old_fail=0 new_fail=0 chunks=17` — 0 DIFF, 0 OLD-own failures, 17/17 chunks `exit=0`; nothing to root-cause, nothing masked. |
| P5-5H-SECTIONSPEC-GAP | PLAN §6 DoD: "the module classes expose … `SectionSpec` factories" — only `Dmac::SavestateSection()` (`TAG_DMAC`) exists; `grep -rn SectionSpec engine/gif engine/vu engine/vif` → 0. | DoD clause PARTIAL (1 of 8): the 7 missing factories are OWNED-path work for a follow-up owned stage 5H′ (checklist (f) lists each tag with OLD payload + restore order); only their `SectionRegistry::Register` calls are carried-to-integration. Stated in the MODULE GATE SUMMARY. |
| P5-5H-HLE-DEFAULT-FALSE | `engine/vu/tiers/vu_tier_dispatch.h:29` `bool hle_enabled = false;` vs PLAN §5.1 `PS2_VU1_HLE` NORM "`VuSettings::hle_enabled = true`"; P5-5C-HLE-DEFAULT (`:806`) said 5D flips it — 5D did not. Every gate sets it explicitly. | left as is (the corpus tools' `--mode` arms rely on the explicit set); checklist (c) row made MANDATORY: `hle_enabled = (library != none)` at the Config copy. |
| P5-5H-CMAKE-TOOL-LINES | 11 added CMake lines vs the 9 enumerated by PLAN §1.5 / CONTRACT §2 — `add_subdirectory(tools/vu1_replay)`, `(tools/vif_replay)`. | recorded in G1; the tools are OWNED dirs; no change. |
| P5-5H-SWEEP-CTEST | `Vu1CollectSweep` registration/exclusion needed a choice with no edit outside OWNED paths. | `LABELS long TIMEOUT 18000 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE` + `DISABLED TRUE` unless `option(PS2R_VU1_SWEEP OFF)`; `ctest -N` 152 rows / 151 enabled; the quoted G5 line is the hand run of the same script body (17 chunk logs under `/tmp/p5/5H/logs`). |
| P5-5H-VU0-DELETE-ROWS-UNNAMED | 5 DELETE rows never named by a stage (list above). | grepped → 0 hits each; tally unchanged. |
| P5-5H-EESCHED-LOAD | `EeRunTokenAndEventQueue` (Phase-4, `tests/ee/test_ee_sched.cpp`) timed out at 120 s in half 1 under sweep + `-j2`, then `Passed 0.07 sec` ×3 alone — a 1,700× spread that points at a `yield`/`sleep_for` pacing loop starving under load, not at a Phase-5 change. | recorded for the Phase-4 owner as the 6th sighting of P5-5G-EESCHED-FLAKE; suggested fix (outside OWNED): replace the pacing with a condition-variable wait or raise the row's TIMEOUT. |
| P5-5H-TSAN-NOT-RUN | the whole-tree TSAN configure (5G N3 / P5-5G-TSAN-TWIN) was not run at 5H: the sweep occupied the host for the whole stage and the CONTRACT forbids the battery under contention. | deferred to integration — exact command in checklist (k). |

### G5 result

```
$ ls /tmp/p5/5H/SWEEP_DONE                       → /tmp/p5/5H/SWEEP_DONE        (touched 2026-09-28T19:32:13Z; sweep start 17:30:53Z)
$ python3 tools/vu1_replay/aggregate_sweep.py --logs /tmp/p5/5H/logs --expect 3293
SWEEP cases=3293 same=3293 diff=0 old_fail=0 new_fail=0 chunks=17
SWEEP PASS                                       (exit 0)
$ cat /tmp/p5/5H/logs/chunk_*.done | sort | uniq -c   →   17 exit=0
$ grep -h ' DIFF ' /tmp/p5/5H/logs/chunk_*.log | wc -l →   0
$ tail -3 /tmp/p5/5H/progress.txt
chunk 15 done (16) 2026-09-28T19:28:51Z CMP cases=200 same=200 diff=0 old_fail=0 new_fail=0
chunk 16 done (17) 2026-09-28T19:32:13Z CMP cases=93 same=93 diff=0 old_fail=0 new_fail=0
sweep end 2026-09-28T19:32:13Z
```
All 17 `chunk NN done` lines of `progress.txt` read `CMP cases=200 same=200 diff=0 old_fail=0 new_fail=0` (chunk 16: `cases=93 same=93`). No DIFF case → no `/tmp/p5/5H/diffs/`, no root-cause, no accepted-difference row. The sweep ran through the orchestrator's `/tmp/p5/5H/sweep.sh`; the checked-in `tools/vu1_replay/sweep_vs_old.sh` is the same body generalised (same `compare_vs_old.py --mode compare`, same OLD `build/tests/vu1_replay` and NEW `build/tools/vu1_replay/vu1_replay`, same 200-case chunks) and `aggregate_sweep.py` is what produced the `SWEEP` line above. **G5 ✔.**

Full battery on the idle host after the sweep (single invocation, bounded 900 s):
```
$ verify/run_bounded.sh 900 /tmp/p5/5H/ctest_full_idle.log -- ctest --preset macos-dev
 56/152 Test  #56: EeRunTokenAndEventQueue .............   Passed    0.08 sec
113/152 Test #113: Vu1CollectSweep .....................***Not Run (Disabled)   0.00 sec
100% tests passed, 0 tests failed out of 151
Total Test time (real) = 559.18 sec
```

### HANDOFF (5H → integration)

See the two sections below: they ARE the handoff. **Done:** sweep tooling checked in + ctest row; PLAN §7.1 closed (150 rows); the G1–G9 table; module checklist; flag tally; this file's final sections. **Not done (integration):** every row of the INTEGRATION CHECKLIST (all FORBIDDEN this phase) — except the 7 `SectionSpec` factories in (f), which are OWNED-path work for a follow-up owned stage 5H′ (P5-5H-SECTIONSPEC-GAP). **Gate status:** see the MODULE GATE SUMMARY. **Next command (integration owner):** `cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` on an idle host (expect `100% tests passed, 0 tests failed out of 151` + `Vu1CollectSweep (Disabled)`), then checklist (a) — `MmioRouter` registration in `app/host`.

## INTEGRATION CHECKLIST

Every edit the integration stage must make OUTSIDE the Phase-5 owned paths (`engine/{gif,vu,vif,dmac}`, `games/…/vu1_library`, `tests/{gif,vu,vif,dmac}`, `tools/{vu1_replay,vif_replay}`, the two PHASE5 docs). Each row was FORBIDDEN this phase (CONTRACT §2) and is executed by the composition / integration owner (Phase 6 `app/host`). OLD anchors are the frozen tree's `host_app/*` files, re-verified by the 5H investigator with `grep -n` on 2026-09-28; NEW signatures are the headers named.

### (a) `MmioRouter` ranges — handler class ← OLD `memory.cpp` observable (today every range is an `MmioSink` row, `engine/ee/mem/mmio_sink.cpp:18-37`)

| Address range (EE physical) | Handler class / call | OLD anchor | Observable to preserve | Displaces sink row |
|---|---|---|---|---|
| GIF regs `0x10003000..0x100030AF` | `GifRegs : MmioHandler` (`engine/gif/gif_regs.h:33-35`) → `mmio.Register(0x10003000, 0x100, gif_regs, "GIF")` | `memory.cpp:891` (read), `:1074` (write) | STAT composition as 5A (`:33`): APATH=3 whenever a tag is in transfer, P1Q/P2Q/P3Q never set, FQC=0; TAG1 nregs 0 after RST (P5-GIF-TAG1-NREG16/APATH3). **MMIO fence:** while `GifDrainThread` runs, call `GifArbiter::DrainFence()` before `GifRegs::Read/Write` (5G `:2386`; OLD `gif_arbiter.h:454-557`) | `{0x10003000,0x100,"GIF"}` |
| VIF0 regs `0x10003800..+0x180`, VIF1 regs `0x10003C00..+0x180` (`Vif::kVif0Base/kVif1Base/kRegsSize`, `engine/vif/vif.h:41-43`; ROW/COL at +0x100/+0x140) | `Vif : MmioHandler` (`vif.h:62-63`) — the FULL register file | reads `memory.cpp:548-580`, writes `:993-996` | OLD routed ONLY: reads of FBRST/ERR (+ VIF1_STAT busy view under MTVU); every other VIF register read returns 0; ROW/COL unreachable (`offset & 0xFF`, P5-5E-ROWCOL-UNREACHABLE); writes ONLY FBRST/ERR. The router must reproduce that (P5-VIF-MMIO-OBSERVABLE): register the full class but gate the routed offsets, or register two 0x100 windows + the FIFO window and keep "reads 0" for the rest. MTVU (router-applied, not `Vif`): `Sync(SyncSite::Vif1RegRead)` on non-STAT VIF1 reads (`memory.cpp:579`), `Sync(Vif1RegWrite)` on VIF1 writes + FIFO (`:996`), `MtvuSync::TryVif1StatBusyView` on VIF1_STAT (`:562-563`) | `{0x10003800,0x800,"VIF"}` |
| VIF1 FIFO `0x10005000..0x1000500C` (`Vif::kFifoSize 0x10`) | `Vif::Write` → `WriteFifo(1)` (slot `(addr&0xC)>>2`, submit on slot 3; 5E `:1495`) | `memory.cpp:1021-1024` (`WriteToLatch(1,…)`) | VIF0 FIFO `0x10004000`, GIF FIFO `0x10006000`, IPU FIFOs `0x10007000` were NOT routed in OLD (stay sink; IPU = Phase 7) | the 16-byte VIF1 window out of `{0x10004000,0x4000,"FIFO"}` |
| DMAC reads `0x10008000..0x1000D7FF` (channels) + `0x1000E000..0x1000E060` (globals) + `0x1000F520` (D_ENABLER); writes `0x10008000..0x1000E063` + `0x1000F590` | `Dmac : MmioHandler` (`engine/dmac/dmac.h:67-68`; widening inside) → `Register(0x10008000, 0x6064, dmac, "DMAC")` + 16-byte rows at `0x1000F520` / `0x1000F590` (5F `:2033-2035`) | reads `memory.cpp:909-943`; writes `:1002`, `:1087` (`< 0x1000E064 || == 0x1000F590`) | pinned by `DmacRegs`: non-channel global writes DROPPED except PCR (`dmac.cpp:1153-1161`); the channel pages IPU TO `0xB400` / SIF1 `0xC400` / SIF2 `0xC800` / SPR TO `0xD400` DROP writes (P5-5F-WRITE-PAGE-DROP); a CHCR read of VIF1 flushes a parked kick; D_ENABLER cold = 0; D_ENABLEW reads 0 | `{0x10008000,0x7000,"DMAC"}`, `{0x1000F520,0x10}`, `{0x1000F590,0x10}` |
| VU memory windows (VIRTUAL, `AddressMap` step-2 hw windows): VU0 micro `0x11000000` (4 KB), VU0 data `0x11004000` (4 KB), VU1 micro `0x11008000` (16 KB), VU1 data `0x1100C000` (16 KB) | `AddressMap::RegisterHwWindow(virt_base, size, backing, name)` (`engine/ee/mem/address_map.h:43`) with `vu0.state().micro/.data`, `vu1.state().micro/.data` (`Vu1State`, `engine/vu/vu1_state.h`; pattern `tests/ee/test_address_map.cpp:57-64`) | `memory.cpp:405-440` | **micro-epoch store hook:** every EE store into the VU1 micro window must call `VuUnit::BumpMicroEpoch()` (cached-tier generation, 5B `:450`) AND `Vu1HleRuntime::BumpExternalGeneration()` (`vu1_hle_runtime.h:47`; OLD `g_vu1_micro_extgen.fetch_add`, `memory.cpp:433`) — a store hook on that window (FastMem slow path); under MTVU `Sync(Vu1Mem)` on the VU1 windows (`memory.cpp:425, :439`, `IsRunning()`-guarded) | (none — the windows are unmapped today) |
| result | `MmioRouter::UnhandledRanges()` lists only GS-priv `0x12000000` + IPU `0x10002000` / `0x10007000` afterwards (PLAN §6 `:684-686`) | | | |

### (b) `BoundarySequencer` steps (`engine/ee/timing/boundary_sequencer.h`, read-only this phase; none added)

| Step | NEW call | OLD anchor |
|---|---|---|
| DMAC tail chase at the SyncV / WaitForVblank boundary, BEFORE the VSync job / `TickAdvance` (a `Step::MaintDuties`-adjacent slot or a new `Step::DmacTailChase`) | `dmac.ChaseVif1Tail()` (`dmac.h:83`: flush parked kick + resume sane rewrites + `arbiter.drain()` if resumed) | `gs_hle.cpp:446` (`hle_sceGsSyncV`), `sif_hle.cpp:225` (WaitForVblank) |
| `FlushDeferredVif1()` slot for the HLE syncs | `dmac.FlushDeferredVif1()` (`dmac.h:82`) from `sceGsSyncPath` / `sceGsResetPath` (gs/hle, Phase 6) and `LibdmaBindings::SceDmaSync / SceGifSyncDma` (`engine/dmac/hle/libdma_bindings.h`, replacing `games/…/hle/glue/sce_dma_glue.cpp:6-7`) → `v0 = 0` | `gs_hle.cpp:768`, `:910`, `:939`, `:968` |
| The arbiter drain the VIF never performs (P5-5E-NO-DRAIN): `arbiter.hasPending()/drain()` after DMAC-fed VIF1 traffic is the DMAC's / boundary's call, never `Vif`'s | **The DMAC-side drain ALREADY EXISTS** (owned, 5F): `engine/dmac/dmac.cpp:199` (`if (ch == kVif1 && arbiter_.hasPending()) arbiter_.drain();` at VIF1 channel completion), `engine/dmac/dmac_deferred_vif1.cpp:45` (deferred VIF1 flush) and `:76` (resumed sane rewrites). Only the boundary-side drain (row 1, inside `ChaseVif1Tail()` / the boundary step) remains integration work | OLD drained from the DMA path (`dmac.cpp` VIF1 completion), not from `vif.cpp` |
| MTVU boundary (only when `engine.mtvu`): `ChaseVif1Tail → arbiter.noteFeedForBoundary() → if (sync.DeferOn() && worker.IsRunning()) { arbiter.openBoundary(seq, now_ns); arbiter.drain(); } else sync.Sync(SyncSite::SceGsSyncV)` (SyncV) / `Sync(SyncSite::WaitVblank)` (WaitForVblank) | `GifArbiter::openBoundary/noteFeedForBoundary` (`engine/gif/gif_arbiter.h`), `MtvuSync::Sync/DeferOn` (`engine/vu/tiers/mtvu_sync.h`) | `gs_hle.cpp:470-494` (DeferOn branch `:470-486`, `Sync(SCEGSSYNCV)` `:494`), `sif_hle.cpp:236, :263, :267` |
| Arbiter boundary hook = the presenter push (Phase 6): `GifArbiter(parser, &hook)` with `hook : GifBoundaryHook::OnBoundary(seq, enqueue_ns)` (`engine/gif/gif_boundary_hook.h`) | ctor-injected (5G `:2074-2075`, P5-5G-DRAIN-CTOR) | `gif_arbiter.h:335-402` |
| Frame counters: `declared` bumped at the boundary enqueue, `presented` at the GFX VSync job (`arg1 == 1`) → `MtvuBackpressure::SetFrameCounters(&declared, &presented)`; `worker.NotifyFrameProgress()` from the present; `low_band` predicate from the present pacer → `RunaheadGuard(depth, low_band)`; `RunaheadGuard::Tripwire::Feed` once per genuine present on the graphics thread | 5G `:2390` | `vu1_async.cpp:254-258`, `graphics_thread.cpp:2404-2411` |
| Savestate quiesce under MTVU: `sync.Sync(SyncSite::Other)` + `GifDrainThread::DrainIdleGeneration()` two equal nonzero reads; `dmac.FlushDeferredVif1()` then refuse if `HasDeferredVif1() \|\| channel(1).chcr & kStr` | 5F `:2036-2037`, 5G `:2395` | `savestate.cpp:823, :983` |

### (c) Config keys → module `Settings` copies at composition — **no new Config key is needed** (all three exist, `engine/core/config/config.h:56-59`)

| Config key (exists) | Module field (peak default) | Copy rule | Deterministic profile (`deterministic_profile.cpp:40`) |
|---|---|---|---|
| `engine.mtvu` (`bool mtvu = true`) | `MtvuSettings::enabled = true` (`engine/vu/tiers/mtvu_settings.h:12`) | direct; when false NOTHING of 5G is constructed (`MtvuWorker`, `MtvuSync`, `DmacMtvuFeed`, `GifDrainThread`, `MtvuBackpressure`, `RunaheadGuard`) | `c.engine.mtvu = false` |
| `engine.mtvu_depth` (`int mtvu_depth = 2`) | `MtvuSettings::depth_frames = 2` (`:13`); `RunaheadGuard(depth_frames, …)` (negative → 0) | direct | 2 (unused) |
| `engine.vu1_mode` (`Vu1Mode {Hle=0, Cached, Interp}`, default `Cached`, `config.h:20, :57`) | `VuSettings::vu1_mode : VuTierMode {Cached, Interp}` + `VuSettings::hle_enabled` (`engine/vu/tiers/vu_tier_dispatch.h:27-30`) | `Hle → Cached`, `Cached → Cached`, `Interp → Interp` (PLAN §3.3 / P5-VU1MODE-HLE). **MANDATORY (P5-5H-HLE-DEFAULT-FALSE): `hle_enabled = (library != none)`** — the header default is `false`; `--vu1-library none` leaves it `false` | `Cached` |
| (none) | `VifSettings{fast_span = true, unpack{bulk_lanes = true}}` (`engine/vif/vif.h:34-37`, `vif_unpack.h:24-26`) — NORM winners; the `false` arms are tool proof knobs | construct with defaults | — |
| (none) | `GifParserSettings{bulk_lanes = true}` (`engine/gif/gif_parser.h:19-21`) — test knob, NOT a Config key (5A `:283`) | construct with defaults | — |
| (none) | `XgKick::paced_drain` (ctor bool via `VuUnit(id, arbiter, xgkick_paced_drain)`) ← `TitleProfile.quirks.xgkick_defer` (5B `:570`; games/hle, read-only) | title profile, not Config | harness certifies `true` |
| (none proposed) | `vu.xgkick_quantum` NOT needed (constexpr 32); `PS2_MTVU_MAX_MB` → constexpr 256 MiB; spin budget constexpr 40 µs | — | — |
| shutdown | `MtvuBackpressure::SetShutdownFlag(&running)` (5G `:2393`) | `app/host` `std::atomic<bool> running` (`app/host/ee_bootstrap.cpp:129`) | — |

### (d) SIF / IPU callbacks on `Dmac` + `SyscallTable → DmacHandlerRegistry` + libdma (`engine/dmac/dmac.h:55-57, :86-90`)

1. `dmac.SetSifCallback(fn)` — `SifCallback = std::function<void(int ch, const DmacChannelRegs&)>`; bind SIF1 → `engine/sif` `ProcessSifDma` HLE of the SIF cmd header `{psize|dsize<<8, dest, command_id, opt}` at MADR (OLD `dmac.cpp:137-253`); SIF0/SIF2 complete-only. **P5-5F-WRITE-PAGE-DROP:** SIF1/SIF2/IPU TO/SPR TO start only through `Dmac::StartChannel` (their CHCR pages drop MMIO writes in OLD) — the binding is exercised only by kernel-HLE/library callers.
2. `dmac.SetIpuFeedInput(fn)` / `SetIpuDrainOutput(fn)` — `IpuCallback = std::function<void(uint8_t* host, size_t bytes)>` (IPU0 FROM / IPU1 TO with the `qwc > 0 && madr != 0` guard + translate, 5F `:1766`) → Phase 7 IPU FIFOs; `libipu`/`libmpeg` direct `channel()` pokes → Phase 7.
3. **`SyscallTable → DmacHandlerRegistry`** (`engine/dmac/dmac_handler_registry.h`): `registry.Install(records, next_id)` from `SyscallTable::dmac_handlers_` (`engine/ee/hle/syscall_table.h:60-108`); `AddDmacHandler / RemoveDmacHandler / EnableDmacHandler / DisableDmacHandler` → `Add / Remove / SetActive`; `dmac.SetGuestCall(fn)` bound to the recompiled dispatch table (`DispatchInterrupt`: `$a0` = channel, `$gp`, pc/`$ra` saved, `-1` stops).
4. **`sceSifSetDma` CIS rule:** `dmac.globals().stat |= DmacStat::Cis(6); if (dmac.CheckInterrupt()) dmac.DispatchInterrupt(ctx);` (OLD `syscalls.cpp:783-786`); `sceSifStopDma` → `channel(5).chcr &= ~kStr`. **Mask rule (P5-DMAC-MASK / P5-5F-ENABLEDMAC-RETURN):** `_EnableDmac/_DisableDmac` NEVER mirror `SyscallTable::DmacMask()` into `Dmac::globals().stat` (OLD never set CIM; `CheckInterrupt()` false on every cold boot; only a savestate restore carries a mask). Phase-3's `ToggleMask` returning 1-then-0 vs OLD's always-1 = the Phase-3 owner's row (`:1846`).
5. `dmac.SetInterruptRaise(fn)` — INT1 slot fired from `CompleteChannel` iff CIS & CIM ≠ 0 (never on a cold boot); binding it changes nothing while the mask is 0 (P5-5F-INT1-SLOT); OLD never asserted INT1 from the DMAC.
6. **libdma manifest thunks:** `SdkBindingManifest::Register("libdma.a", …)` over ONE `LibdmaBindings(dmac, ram)` instance in `app/host` (plain function-pointer handlers; members `SceDmaGetChan / SceDmaPutEnv / SceDmaReset / Memclr / SceDmaSync / SceGifSyncDma`, `engine/dmac/hle/libdma_bindings.h`; NOT registered today — `engine/dmac/hle/README.md`). **P5-5F-LIBDMA-ARENA:** `sceDmaGetChan` hands out an invented descriptor arena at `0x01CFF000 + 0x40·ch` (HACK_LEDGER libdma row) — root fix = manifest-resolved symbol. **P5-5F-MEMCLR-SPR:** `Memclr`/`ZeroGuest` store through `MainMemory::Pointer(Phys(a), w)` — every store OUTSIDE RAM (incl. a scratchpad `0x7000xxxx` destination) is DROPPED; OLD `memclr` via `memory::write<T>` reached the scratchpad → route through `Scratchpad` when the row is installed, or file it with P5-SPR-STUB through the accepted-difference protocol. The MTVU barrier for `SceDmaSync`/`SceGifSyncDma` is NOT inside the 5F bodies — wrap them: `if (sync.DeferOn()) sync.NoteSiteSkipped(site); else sync.Sync(site); dmac.FlushDeferredVif1();` (see (g)).
7. `DmacMtvuFeed(dmac.walker(), worker)` constructed iff `engine.mtvu` (installs the three staged thunks on `ChainWalker::SetVif1StagedFeed`; destruction restores the inline `Vif` feed) — `engine/dmac/dmac_mtvu_feed.h:19`.

### (e) `app/host` wiring — construction order (ctor signatures verified in the NEW headers), thread start rule, `--vu1-library none`, teardown

```
GifRegs gif_regs;                                        // engine/gif/gif_regs.h:27
GsSink& sink = <Phase-6 GsState : GsSink>;               // until then RegisterOpStream (engine/gif/register_op_stream.h) = the gsops.ohash recorder
GifParser parser(sink, gif_regs);                        // gif_parser.h:25 (GifParserSettings{} default)
GifBoundaryHook* hook = <presenter, Phase 6> / nullptr;
GifArbiter arbiter(parser, hook);                        // gif_arbiter.h:62
VuUnit vu0(0);  VuUnit vu1(1, &arbiter, title.quirks.xgkick_defer);   // vu_unit.h:31 (4th arg lazy_flags = FlagPipeline::kDefaultLazy)
vu1.dispatch().settings() = { Hle->Cached | Cached | Interp, hle_enabled = (library != none) };   // vu_tier_dispatch.h:27-30  (MANDATORY, (c))
Vu1HleRuntime hle(Vu1Library::Get(), arbiter);          // vu1_hle_runtime.h:36; Vu1Library::Get() = games/…/vu1_library/vu1_library.h:14 (223 rows)
                                                         // `--vu1-library none` → skip this + leave hle_enabled=false: registry EMPTY, every image → fallback tier [GD1]
vu1.dispatch().SetHleHook(&Vu1HleRuntime::Hook, &hle);  // vu1_hle_runtime.h:41
Vu0Registry vu0_reg; Vu0Programs::Register(vu0_reg);    // engine/vu/vu0/vu0_registry.h, vu0_programs.h:12
                                                         // NoteEntry(id, pc, micro, size) at each MSCAL/MSCNT/VCALLMS → PS2R_LOG(VU, …) per Note.event (5D :1447, :1464; P5-5D-VU0-CENSUS-NO-CALLER)
Vif vif(vu0, vu1, arbiter, gif_regs);                    // vif.h:47 (VifSettings{} default); vif.SetTopsObserver(→ hle.SetVifTops, &hle) (vif.h:80)
Dmac dmac(ram, spad, vif, arbiter);                      // dmac.h:63; then the (d) callbacks
// MTVU half — ONLY if cfg.engine.mtvu (the deterministic profile sets it false: none of this is constructed on a harness leg):
VuUnit worker_vu1(1, &arbiter, …);  RunaheadGuard guard(mtvu.depth_frames, low_band);  MtvuBackpressure bp(guard, MtvuClock::Steady());
MtvuWorker worker(vif /*MtvuVifFeed*/, worker_vu1, arbiter, bp, mtvu);   // mtvu_worker.h:58 — Start() binds VIF1 to worker_vu1 (BindVu1), SetMarkerOwner, bp.Bind
MtvuSync sync(worker, arbiter);  sync.InstallInto(vu1.dispatch());      // mtvu_sync.h:50 — the ladder's step 2 on the INLINE unit
DmacMtvuFeed feed(dmac.walker(), worker);                // dmac_mtvu_feed.h:19
GifDrainThread drain(arbiter);                           // gif_drain_thread.h:24
worker.Start(on_start = PlatformMacos::PinCurrentThreadInteractive, "vu1worker");  drain.Start(...);
```
Thread start rule: threads start ONLY under `engine.mtvu` (OLD `main.cpp:14228-14239`); `worker.Start` BEFORE any producer traffic; `GifDrainThread::Start` iff `engine.mtvu`. Teardown (5G `:2392`): savestate quiesce (`Sync(Other)` + `DrainIdleGeneration` twice-equal) → CPU thread → `worker.Stop()` → `drain.Stop()` → GFX (OLD `savestate.cpp:983` flush precedent). Other wires: `VuUnit::Execute` ← VIF1 MSCAL/MSCNT/MSCALF (already inside `Vif`, 5E) and CMSAR0/VCALLMS (VU0, EE COP2 — Phase-3 owner); `VuUnit::ResumeIfWaiting` ← CTC2-to-VI (VU0); the renderer phase subclasses `KickLowering::TryLower` (DrawIR / typed-ops seam, `engine/vu/hle/kick_lowering.h`); `TopsObserver` fires on the worker thread while MTVU runs (FACTS §F.9 item 7).

### (f) Savestate `SectionSpec` registrations (`ps2r::savestate::SectionRegistry::Register`, `engine/core/savestate/section_registry.h:41`; tags `savestate_format.h:61-82`) + restore order

Codec that EXISTS: **`Dmac::SavestateSection()` only** (`TAG_DMAC` v1, required, owner "dmac"; 324 + 25·nq bytes; `DmacSectionRoundTrip`; restore clears the parked kick + tails). **The PLAN §6 DoD clause "the module classes expose `SectionSpec` factories" is therefore PARTIAL — 1 of 8 (P5-5H-SECTIONSPEC-GAP).** The other 7 are **OWNED-path work, NOT integration work**: writing the raw-copy factories (`VuUnit`, `Vif`, `GifRegs`, the VU1R context) inside `engine/{vu,vif,gif}` is deferred to a follow-up owned stage **5H′** (same ownership as 5A–5H, no `app/host` edit needed to write them). Only the `SectionRegistry::Register(...)` calls in `app/host/ee_bootstrap.cpp:247-250` (which registers THRD/kernel/INTC/TIMR today), in the OLD restore order below, are integration rows. Rows in the table: "NEW owner / factory" = the 5H′ deliverable; "Restore order" = the integration registration order:

| Tag | Payload (OLD `savestate.cpp`) | NEW owner / factory | Restore order (OLD) |
|---|---|---|---|
| `TAG_VU0C`, `TAG_VU0D` | raw 4 KB micro / 4 KB data (`:1116-1119`, sizes `:1582-1585`) | `vu0.state().micro[0..4096)` / `.data` — `VuUnit` raw-memory spec; restore must `BumpMicroEpoch()` | memories first (after MEM sections, before DMAC/GSST/VIFS/GIFS) |
| `TAG_VU1C`, `TAG_VU1D` | raw 16 KB micro / 16 KB data | `vu1.state().micro/.data` (+ the worker unit under MTVU after quiesce); restore bumps `micro_generation` + `hle.BumpExternalGeneration()` | with VU0 |
| `TAG_DMAC` | channels[10] ×28 B, 9 globals, next id, nq, records (`:1185-1213`, `:1811-1832`) | `dmac.SavestateSection()` — EXISTS; quiesce = `FlushDeferredVif1()` then refuse if `HasDeferredVif1() \|\| channel(1).chcr & kStr` | after `TAG_INTC`, before `TAG_TIMR/GSPR/GSST/VIFS/GIFS` (5F `:2036-2037`) |
| `TAG_VIFS` | `VifRegs regs[2]` raw 2 × 92 B (`:1855-1864`; restore resets state/latch/fifo/direct_buffer) | `Vif` — `regs(0)`, `regs(1)` raw (`engine/vif/vif_regs.h:36` pins 92 B); restore forces both units idle; admissibility `IsIdle(0) && IsIdle(1)` (5E `:1618`; NEW `IsIdle` is stricter than OLD's predicate — 5E REVIEW (c), keep parity in mind) | after DMAC |
| `TAG_GIFS` | `ctrl, mode, stat` u32 + `u8 path3_masked` (13 B; `:1866-1874`; restore clears dma_buffer/fifo/in_transfer) | `GifRegs` (+ parser tail clear) | after VIFS |
| `TAG_VU1R` | `u8 running, u32 sizeof(CpuContext)=1600, CpuContext bytes` (`:1317-1323` / `:2000-2019`) | the MTVU worker's VU1 register context — under the deterministic profile `running = 0` and the context is `vu1.regs()` (608 B `VUregs` inside the 1600-byte `CpuContext` image); layout needs the OLD `CpuContext` offset map (Phase-3 `cpu_state.h`) | LAST (after GIFS) |
| gate | `RestoreMhashAnchors` re-run after registration = the integration check (PLAN §6 `:692`) | | |

### (g) `MtvuSync` sites — the 12 `SyncSite` ids (`engine/vu/tiers/mtvu_sync.h`, OLD ids 0..11 in order) → callers with OLD anchors (`grep -n sync_site::` verified)

| SyncSite | OLD caller | NEW caller (integration) |
|---|---|---|
| `Vif1RegRead` (0) | `memory.cpp:579` (non-STAT VIF1 register read) | MMIO router VIF1 window (a) |
| `Vif1RegWrite` (1) | `memory.cpp:996` (VIF1 reg write + FIFO window) | MMIO router (a) |
| `Vu1Mem` (2) | `memory.cpp:425, :439` (VU1 micro/data windows, `IsRunning()`-guarded) | VU window store hook (a) |
| `SceGsSyncV` (3) | `gs_hle.cpp:494` (else-branch of the DeferOn boundary `:470-486`) | boundary step (b) |
| `SceGsSyncPath` (4) | `gs_hle.cpp:799` `NoteSiteSkipped` under DeferOn / `:801` `Sync` | gs/hle Phase 6 |
| `SceGsResetPath` (5) | `gs_hle.cpp:918` (always `Sync`) | gs/hle Phase 6 |
| `SceDmaSync` (6) | `gs_hle.cpp:945` skipped / `:947` `Sync` | wrap `LibdmaBindings::SceDmaSync`: `if (sync.DeferOn()) sync.NoteSiteSkipped(SceDmaSync); else sync.Sync(SceDmaSync);` then `dmac.FlushDeferredVif1()` (the barrier is not inside the 5F body, `:1891`; 5G did not add it) |
| `SceGifSyncDma` (7) | `gs_hle.cpp:975` skipped / `:977` `Sync` | same wrapping over `LibdmaBindings::SceGifSyncDma` |
| `WaitVblank` (8) | `sif_hle.cpp:236` skipped, `:263` `Sync`, `:267` skipped | boundary step (b) / `engine/sif` WaitForVblank |
| `Vu1EeKick` (9) | `vu.cpp:4810` (EE-side VU1 kick → the dispatcher's MTVU rung) | DONE in 5G: `MtvuSync::Redirect` via `InstallInto(vu1.dispatch())` |
| `Other` (10) | `savestate.cpp:823, :2297, :2381` (quiesce / load), the tool | savestate quiesce (b) |
| `PrivPush` (11) | `gs_hle.cpp:1024` | gs/hle Phase 6 (GS privileged push) |

### (h) File moves and doc deltas deferred (targets outside OWNED)

1. **MOVE** `engine/gif/register_op_stream.{h,cpp}` (`RegisterOpStream : GsSink`, `RegisterOp` 24 B, `Hash()`) → `engine/gs/hle/ir/` (CONSTRUCTION §4.5 row 10; P5-GS-READONLY). `GsSink` itself STAYS in `engine/gif/`. Update the includes in `tools/vu1_replay`, `tests/gif`, `tests/vu`, `engine/vu/tiers` (MTVU tests) when moving.
2. **MOVE** `engine/vu/vu1_case.{h,cpp}` (frozen `Vu1Case` POD + `ReadCase/WriteCase`) → `engine/core/diag/vutrace_case` when the WRITER/collector lands (Phase 9; P5-CORE-READONLY / P5-VU1CASE-WRITER); tools keep including the reader.
3. `gsops.ohash` per-frame emitter: `RegisterOpStream::Hash()` per present (PLAN §4.4; today `tests/gif/fixtures/gsops_seed.txt` = 126 corpus hashes via `vu1_replay --gsops`) → wired at the present boundary in Phase 6/8 (`engine/core/verify` emitter, NEW-only lane).
4. Doc deltas outside OWNED: CONSTRUCTION §4.5 / ARCH §14.2 "137 entries / 120 hashes" → 223 / 176 / 12 / 15; FLAG_LEDGER notes for `PS2_EEFEED` / `PS2_MTVU` / `PS2_MTVU_DEFER` (consumed in Phase 5, ledgered P3); `tests/lint/lint_one_class.py` digit-separator quote bug (P5-5G-LINT-QUOTE); optionally PLAN §7 row 5A `:709` / HANDOFF `:818` still cite `gif.cpp:3183-3258` (5A REVIEW N2, cosmetic — PLAN is OWNED, left as planning history).

### (i) Lane-gate commands for the integration stage (copy-pasteable) + the Phase-5 stopping rule

**Phase-5 stopping rule (CONSTRUCTION §4.5 DoD):** `audio` + `mem` lanes IDENTICAL on all 12 legs — the 11 anchors END TO END (≥ 60 boundaries, `--frames 600`) and `coldboot` through the frame before its first FMV (`--frames 1500`; IPU = Phase 7; "RAM independent of GS local→host readbacks and of IPU decode — the two sinks left"). The deterministic profile runs MTVU OFF and the drain thread OFF, so the certified path is the synchronous VIF1→VU1→GIF inline drain; the `frames` lane is NOT claimed (Phase 6). Nothing in Phase 5 claimed a lane.

```sh
# one leg (coldboot: --frames 1500; every anchor: --frames 600)
python3 verify/run_leg.py --binary build/app/host/nativeee --tree new --anchor <A> --profile deterministic --frames <1500|600> --out /tmp/p6/<leg>
python3 verify/diff_legs.py --cross-engine --lanes audio,mem verify/baseline/<leg> /tmp/p6/<leg>
#   AFTER Phase 4 lands PHASE4_PLAN §4.4 step 5 (PHASE4_PLAN.md:493 — `diff_legs.py` gains `--claim mem=<checkpoint>,audio=<window>`),
#   the shrunk-claim form is:  python3 verify/diff_legs.py --cross-engine --lanes audio,mem --claim mem=<K>,audio=<W> verify/baseline/<leg> /tmp/p6/<leg>
# the whole battery: coldboot ×3 + 11 anchors ×3
python3 verify/battery.py --binary build/app/host/nativeee --lanes audio,mem --reps 3
# the empty-profile contract [GD1] — NOT runnable through run_leg.py today (see the note below); the intended leg is
#   the same run_leg.py line as above with the app/host CLI switch `--vu1-library none` reaching the binary, --out /tmp/p6/<leg>-nolib
# the module-side gates that must stay green under integration
ctest --preset macos-dev                                                   # 151 + Vu1CollectSweep (Disabled)
cmake --preset macos-dev -DPS2R_VU1_SWEEP=ON && ctest --preset macos-dev -L long   # or: tools/vu1_replay/sweep_vs_old.sh /tmp/sweep 200 --expect 3293
```
Harness flags valid at HEAD `78b5655` (bounded `--help` runs, 2026-09-28): `verify/diff_legs.py A B [--allow-build-diff] [--cross-engine] [--lanes meta,frames,audio,mem] [--selftest]` — **`--claim` does not exist yet**; it is the PHASE4_PLAN §4.4 step-5 addition (`PHASE4_PLAN.md:493`, `:705`) and is used only once Phase 4 lands it. `verify/run_leg.py` accepts `--binary --anchor --frames [--inputs] [--profile] --out [--tree {legacy,v2,new}] [--repo] [--timeout] [--state-every] [--game-files]` and builds the command from those parsed fields only — **there is no passthrough of extra arguments to the binary**, so `--vu1-library none` is rejected (`unrecognized arguments`). `--vu1-library none` is the `app/host` CLI switch ((c)/(e), P5-5H-HLE-DEFAULT-FALSE); the integration owner adds either a `run_leg.py` passthrough (e.g. `--binary-arg`) or a profile field carrying it (`verify/` is read-only this phase). The intent of the [GD1] leg is unchanged; it is not presented as runnable today.
Legs (`verify/baseline/`): `coldboot` (1500) + `artdet_f37152`, `ps2state_slot0_f36935`, `_f39807`, `_f40391`, `_f44111`, `_f46203_red_crystal`, `_f46949_yellow_crystal`, `_f57292`, `_f59991` (the P4FIX-F1 pair — verdict recorded on the integration leg, CONSTRUCTION §7), `slowspot_f37092_s1789428268`, `slowspot_f40298_s1789693059` (600 each). [GD1]: `--vu1-library none` (the `app/host` switch, once `run_leg.py` can carry it) boots with the registry EMPTY; `mem` identity unchanged on anchors that never enter the HLE path. `MmioRouter::UnhandledRanges()` → GS-priv + IPU only.

### (j) BUG-KB candidates (none filed — `docs/bugs/` is read-only this phase; each cites its IMPL row)

| Candidate | Rule OLD violates | IMPL |
|---|---|---|
| P5-SPR-STUB | SPR0/SPR1 DMA copies nothing (`dmac.cpp:1562-1576`) | `:1766`, `:1897` (`DmacSprStub`) |
| P5-DMAC-MASK (+ P5-5F-ENABLEDMAC-RETURN, P5-5F-INT1-SLOT) | D_STAT CIM never set → DMAC handlers never dispatch; INT1 never asserted | `:1846-1847`, `:2041` |
| P5-5F-WRITE-PAGE-DROP | IPU TO / SIF1 / SIF2 / SPR TO CHCR pages drop MMIO writes (channels unstartable via the register file) | `:1841` |
| P5-5F-MEMCLR-SPR / P5-5F-LIBDMA-ARENA | `Memclr` drops non-RAM stores; `sceDmaGetChan` invents a descriptor arena at `0x01CFF000` | `:1891`, `:2022` |
| P5-REGLIST-ADC-TEXCLUT (+ P5-5A-DROP-TABLE) | REGLIST XYZ2 kicks ignore ADC; TEXCLUT (0x1C) dropped | `:115`, `:126`, `:258` |
| P5-GIF-PACKED-FOG-FGE | PACKED FOG stores `f != 0` into PRIM.FGE | `:101` |
| P5-GIF-P3-STALE-RESET / P5-5F-P3-STALE-RESET-VIA-DMA / P5-5F-M3R-DRAIN-ABANDON | a PATH3 IMAGE mid-transfer is abandoned by the next PATH3 packet; M3R-held PATH3 data lost at the drain's FinishPath3 (M3R-DRAIN-ABANDON still to be confirmed against OLD's drain cadence once `GifRegs` is routed — 5G did not revisit) | `:102`, `:2020-2021` |
| P5-GIF-TAG1-NREG16 / P5-GIF-APATH3 | TAG1 reads nregs 0 after RST; APATH always 3, FQC always 0 | `:96-97` |
| P5-5B-DISCARD-LOWER-SIDE-EFFECTS / P5-5B-DELAYSLOT-CONTROL | discard_lower skips LQI's VI++; branch/E in a delay slot ignored; TPC not set on redirect | `:675-676` |
| P5-5E-VIF0-MSCALF / P5-5E-CLWL-FILL / P5-5E-NUM-STATIC / P5-5E-UNKNOWN-CMD / P5-5E-FQC-DIV16 / P5-5E-MODE2-COL | VIF0 MSCALF/MSCNT no-ops; CL<WL fills from the exhausted source (EE UM 6.3.5); NUM never counts down; unknown VIFcodes ignored; FQC /16; MODE 2 is `+COL` not the difference mode | `:1585-1587`, 5E REVIEW §4 (a) |
| P5-5F-GIF-NORMAL-NO-CIS / P5-5F-TAG-MIRROR-ID / P5-5F-SPAD-TADR0 / P5-5F-CHASE-GATE-VS-WALK | GIF normal never sets CIS(2); CHCR.TAG mirror includes the ID bits; SPR offset-0 TADR = early return; chase gate honours the prefix, the walk the bit | `:1844`, `:2017-2019` |
| P5-5C-MATERIALIZE-K3 | cached-tier `MaterializeFmac` rebuilds only 3 slots (clock may lag by 1 after a cap cut) | `:804` |

### (k) Advisories carried (reviewer nits; none is a gate failure)

| Source | Advisory | Action |
|---|---|---|
| 5G REVIEW N1 | worker spin tier reads `ps2r::Park::NowNs()` directly, not the injected `MtvuClock` (`mtvu_worker.cpp:246, 252, 277`) — bounds a busy-wait only | optional `MtvuClock` injection (P5-5G-RESUME d) |
| 5G N2 | ctest rows `VifMtvuSubmitEquivalence(Fast)` vs OLD/driver `VifReplayMtvuEquivalence(Fast)` | PLAN §6 G6 text aligned at 5H (P5-5G-ARM-NAMES) |
| 5G N3 / P5-5G-TSAN-TWIN / P5-5H-TSAN-NOT-RUN | `GifDrainThreadTsan` not a ctest row; whole-tree TSAN configure never run | run once on an idle host: `verify/run_bounded.sh 1800 /tmp/p6/tsan.log -- cmake -S . -B /tmp/p6/build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread && cmake --build /tmp/p6/build-tsan -j4 && ctest --test-dir /tmp/p6/build-tsan -R 'Mtvu\|GifDrainThread\|GifDeferBoundaryOrder\|DmacMtvuFeed'` |
| 5G N4 / P5-5H-CMAKE-TOOL-LINES | the two `tools/*` root CMake lines | recorded in G1 |
| 5G N5 | `tests/gif/test_gif_drain_thread.cpp:37, :277` print `[GIFDRAIN]` (OLD test's prefix) | note only |
| 5G N6 | `MtvuSync::Sync` counts the site on the idle fast path (OLD had no counter); pinned by the test | note only |
| 5G N7 | `--runahead` arm runs the REAL `Engage` loop on a fake clock; tokens identical after blanking | note only |
| 5F REVIEW §5.1 | `libdma_bindings.cpp:22-35` `ZeroGuest`/`Memclr` scratchpad drift | P5-5F-MEMCLR-SPR minted; (d)6 |
| 5F §5.2 | `chain_walker.cpp:118-121`: a VIF tag at scratchpad offset `0x3FF8` reads its high word from `spad.Pointer(0x4000, 8)` = null → 0 (OLD wrapped through translate); unreachable by a sane chain | note only |
| 5E REVIEW §4 (a) | MODE 2 `+ COL[cycle&3]` verbatim | P5-5E-MODE2-COL minted; (j) |
| 5E (b) | word path iterates `i + 4 <= size` (OLD over-read a sub-word tail) — unobservable | note only |
| 5E (c) | `IsIdle` stricter than OLD's admissibility predicate | (f) VIFS row |
| 5D REVIEW §3 A | `ZeroKick::RunCCont` 308 lines (soft budget ~300; OLD 345 straight-line) | note; `P5-5D-FN-SIZE` if the budget is enforced |
| 5D §3 B | `hle_vs_old.py` / `catalog_vs_old.py` bound OLD with `subprocess.run(timeout=300, cwd=--old-cwd)` rather than `verify/run_bounded.sh` | optional follow-up |
| 5D §3 D | `OLD/logs/mpegpic_*.ppm` modified 2026-09-27 23:55 by another tree (documented `logs/*` side effect) | freeze-ledger note, out of scope |
| 5D §3 E | `Vu0Registry::NoteEntry` has no NEW caller | (e) |
| 5C REVIEW §6.1 items 2–6 | `check_gsops_seed.py` does not assert 126 == corpus count; `Usage()` adds `--gsops`; `VuSettings::hle_enabled` default | (c) MANDATORY row; P5-5H-HLE-DEFAULT-FALSE |
| 5B REVIEW N1–N4 | `set_vi` guards on the 4-bit index (OLD 5-bit) — unobservable; `:295` title (fixed at 5H); `bc_val` evaluated before the switch; corpus smoke asserts structure only | done / note only |
| 5A REVIEW N1–N7 | CMake line comments descriptive not the literal `# Phase 5`; PLAN §7 5A `:709` anchors cosmetic; `####` sub-headings; `EeRunTokenAndEventQueue` 120 s TIMEOUT under load (first sighting); `GifRegs::Reset()` does not reset `tag_class_` (unobservable); `is_directhl`/`is_path3_image` stored never read (as OLD) | note only |
| corpus-unproven arms (P5-5D-GROUPB-COVERAGE / P5-5D-CRYS-COVERAGE) | `RunSkin1040`, `RunClipMark0B30`, `RunSkinEnv1aa0`, skn `0x12a0`/`0x90`, `ZeroKick::RunCCont/RunACont`, spr `0x1980`, `DisplacePrefix::RunDisplaceFloat`, the second `vs_crys_dual` row, displace `0x12a0`/`0x15a0` | a future capture goes through `hle_vs_old.py`; the harness `mem` lane is the integration-time proof |
| process | inline `python3 -` heredocs used once each in 5F p2 (`:2004-2006`), 5G p1 (`:2196`), the 5H investigator, and once (an empty no-op) by 5H | CONTRACT §4 helper-shape slips on `/tmp` only; no tree effect |

## MODULE GATE SUMMARY

**Phase 5 module gate: PASSED (G1–G9 each quoted above).** **DoD clause `SectionSpec` factories is PARTIAL — 1 of 8 (`Dmac::SavestateSection()`), P5-5H-SECTIONSPEC-GAP; the other 7 are OWNED-path work deferred to 5H′ (checklist (f)), not integration work.** G2 `--compare` 126/126 vs the frozen OLD `vu1_replay` (`CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0`, 32 postdata dumps + 32 `.regs` sidecars `cmp`-identical per tier); G3 `--hle` 126/126 line-identical (`HLE cases=126 same=126 diff=0` = 124 `HLE PASS` + 2 exit-77 exactly as OLD); G4 `--catalog` 66 lines identical (`CATALOG DIFF EMPTY`, footer `cases=126 images=63 hle_covered=61/63 bundle_coverage=100.00%` — 63 images, 61 covered) with the registry self-report `REGISTRY entries=223 distinct_variant_hashes=176`, 12 program names, 15 program functions; G5 the whole collected corpus `SWEEP cases=3293 same=3293 diff=0 old_fail=0 new_fail=0 chunks=17` (17 chunks, 17 × `exit=0`, 0 DIFF, 2 h 01 min); G6 `VIFARMS arms=6 same=6 diff=0` (every PLAN §4.2 invariant token present in both trees); G7 all ported unit tests green incl. the NEON twins under `-DPS2R_SIMD=ON`; G8 `Vu1LibraryPurity … 207 library hashes, 0 hits -> PASS`, `LintEnginePurity … 0 game tokens`, layering grep 0; G1 build 0 warnings, `ctest --preset macos-dev` on the idle host `100% tests passed, 0 tests failed out of 151` (152 rows registered — 63 Phase-5 rows + the Disabled `Vu1CollectSweep`; Phase 0–4 = 88), `LintOneClass: 485 files scanned, 0 findings -> PASS`, `LintEnginePurity: 377 files scanned, 0 game tokens -> PASS`, `getenv sites: 1 -> PASS` (`CoreSingleGetenvSite` Passed). Code landed: 181 `.cpp/.h/.inl` files / 30,567 lines across the eleven new directories — `engine/gif` 16 / 1,761, `engine/vu` (+`tiers`, `hle`, `vu0`) 62 / 6,133, `engine/vif` 9 / 1,357, `engine/dmac` (+`hle`) 13 / 1,557, `games/<title>/vu1_library` 32 / 5,100, `tools/vu1_replay` 7 / 1,753 (+ 6 driver scripts incl. `sweep_vs_old.sh` 110 ln and `aggregate_sweep.py` 102 ln), `tools/vif_replay` 1 / 1,290 (+1), `tests/gif` 7 / 2,031 (+1), `tests/vu` 23 / 5,720 (+1), `tests/vif` 2 / 1,024, `tests/dmac` 9 / 2,841 — with 17 READMEs (8 new, 9 extended); the two shared CMake files changed by exactly 11 append-only added lines (7 root + 4 tests; the two `tools/*` lines are the recorded P5-5H-CMAKE-TOOL-LINES deviation), nothing removed, nothing else touched outside the OWNED paths (`git status --porcelain` filtered to non-owned paths is empty). Flags: 114/114 dispositioned as 32 NORM / 2 SETTING / 15 DIAG / 8 TEST-ONLY / 57 DELETE, zero `getenv` added, zero latches, no losing arm written; PLAN §7.1 carries 150 rows, every one with a disposition (41 seeded + 109 stage rows). **No harness lane is claimed** (the deterministic profile runs MTVU + drain thread OFF; the certified path is the inline VIF1→VU1→GIF drain; `frames` is Phase 6). What remains for integration, in one sentence: register the four `MmioHandler`s and the four VU windows with `MmioRouter` (with the micro-epoch store hook and OLD's routed-offset observable), add the `ChaseVif1Tail`/`FlushDeferredVif1`/MTVU-boundary steps to `BoundarySequencer`, copy the three existing Config keys into `MtvuSettings`/`VuSettings` (setting `hle_enabled = (library != none)`), bind SIF1/IPU/`SyscallTable`/libdma to `Dmac`, construct and tear down the modules in `app/host` in the order of (e), register the `SectionSpec`s in the OLD restore order (the 7 missing raw factories are written first by the owned follow-up 5H′, (f)), wire the 12 `SyncSite` callers, move the two files, and run the `audio`+`mem` lane battery on the 12 legs through the Phase-5 stopping rule (with `diff_legs.py --claim` once Phase 4 lands it, (i)).
