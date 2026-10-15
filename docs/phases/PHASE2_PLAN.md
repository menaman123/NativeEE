# PHASE2_PLAN — `engine/gs/format/` (pure math): CONSTRUCTION_PLAN §4.2 made executable

Status: plan (stage p1d, 2026-09-26; revised by stage p2a 2026-09-26 per the p2pr review — six items:
§4 rule 2 extension + two pre-registered decisions, manual citations → GS_SKILL ch. 8, lift list completed,
grid budget/units/4 MB rule, 2C split into 2C/2D, naming drift of the oracle files). Binding rules:
`docs/phases/STAGE_RULES.md` (bounded runs, no commits, OLD read-only, one class per pair, ONE `getenv`
site, flags applied at transplant time).
Inputs, cited not re-derived: `docs/design/CONSTRUCTION_PLAN.md` §4.2; `docs/filemap/INDEX.md`
rows 287–294/367/371/375–376/695/793; `docs/filemap/FLAG_LEDGER.md` master rows 368–513;
`docs/audit/A3_GS_RENDER.md` §2 (format-layer inventory + 4 inconsistencies); `docs/filemap/HACK_LEDGER.md`
:241/:266/:267/:382; the GS manual as `.kiro/skills/GS_SKILL/SKILL.md` (OLD tree; read-only).

## 1. Scope and claim

Phase 2 writes the ONLY address/format math in the tree: PSM descriptors, page/block/column addressing,
rect swizzle/deswizzle, CLUT layout, TEXA + 16→32 expansion, pixel pack/unpack, block decode, MIPTBP
addressing. No runtime consumer exists yet (GS state/VRAM/HLE are Phase 6), so **no harness lane is
claimed**; the proof is the OG oracle (§4) + property tests + lints. Nothing game-visible changes.

Out of scope (recorded so nobody "helpfully" adds it): texture-source serving (`gs/hle/texsrc`, Phase 6),
dither application (BUG-020 [WN4-CT16Q] is a Phase-6 write-path rule; only the *quantize convention* is
fixed here), the shader-side alias remaps (fmt 3/4/5 — Phase 6 `render/gl`, expressed from `PsmLayout`),
CSM2 COU/CSA offset loading (A3 §2 inconsistency 3 — scope cut, documented in the README, unchanged).

## 2. Module table (one class per `.h/.cpp` pair; file = snake_case(type), `tests/lint/lint_one_class.py`)

Naming drift vs CONSTRUCTION_PLAN §4.2, recorded here per §0.1 practice: `clut.h` → `clut_layout.{h,cpp}`
(`ClutLayout`); `texa.h` + `expand.h` → `texa_expand.{h,cpp}` (`TexaExpand`); `alpha.h` folds into
`pixel_convert.{h,cpp}` (`PixelConvert`); `BlockDecoder` → `BlockDecode` (file = type rule). `psm.h`
(enum `Psm`, 13 formats) stays a header-only enum owned by `PsmLayout`. Manual citations: layout facts
(page/block/column geometry, block tables, column word/byte/nibble order, address summary, block-boundary
bases) cite GS_SKILL **chapter 8** (§8.1–8.5), where the tables actually are; §2.2/§2.4.5/§2.6.3 only
introduce them. Value-format, CLUT and MIPMAP rows keep their §2.3/§2.7/§3.4 citations.

| Class (`engine/gs/format/`) | Replaces — OLD sites (file:line, from A3 §2 / INDEX / HACK_LEDGER) | Inconsistency resolved | GS-manual rule written (GS_SKILL §) |
|---|---|---|---|
| `PsmLayout` — `psm.h` + `psm_layout.{h,cpp}`: constexpr per-`Psm` descriptor (bpp, page w×h, block w×h, column w×h, `is_z`, `is_indexed`, `alias_class`, `pixels_per_write`) + `PixelAddress(psm, x, y, bp, bw)` for ALL 13 PSMs incl. Z formats (units: §4 item 4) | block tables `texture_decoder.cpp:263/:275` (CT32/Z32 `[4][8]`), `:577/:590/:604` (CT16/Z16/Z16S 8×4), `:872/:922` (T8 4×8 / T4 8×4); column tables `:285 columnTable32`, `:616 columnTable16`, `:883 columnTable8`, T4 column formula `:1017-1025` (linear — deviates from the manual, §4 rule 2b); `GetAddressPSM*` `:306` CT32 `:336` Z32 `:645` CT16 `:675` Z16 `:694` Z16S `:964` T8 `:996` T4 (no CT16S table anywhere in OLD — `gs_state.cpp:4534/:5110/:5362` alias CT16S → the CT16 helper, §4 rule 2b; Z24 reuses Z32); `gs_deswizzle.h:12-55` page/col tables; `gs_vram_census.h` `PagedGeom` (CT32-class 64×32, CT16-class 64×64, T8 128×64, T4 128×128); `gs_state.h GetPixelSize`; `gs_state.cpp:5066/:5332` bpp / pixels-per-write switches; `ComputeDecodeFootprint/RowBand` page math | A3 §2 #2 (four independent page/block implementations, cross-proved only decoder↔LUT/decoder↔rect) and #4 (`GetPixelSize` is a 5th bpp table); HACK_LEDGER:267 (bpp switches ≥2×, census 5th page-geometry copy); HACK_LEDGER:382 (`gs_deswizzle.h:118-137` derivation narrative + dead variable) — replaced by one table + one invariant test | GS_SKILL §8.1 (units: page 8 KB = 32 blocks, block 256 B = 4 columns, column 64 B; the page/block/column W×H table for all four geometry classes), §8.2 (page row-major arrangement, `page = (Y/page_h)·BW + X/page_w`), §8.3.1 (CT32/CT24/Z32/Z24 block tables + 8×2 column word order; T8H/T4HL/T4HH share CT32's structure), §8.3.2 (CT16/Z16/**CT16S**/Z16S block tables + 16×2 column), §8.3.3 (T8 block table + 16×4 column with the columns-1/3 half swap), §8.3.4 (T4 block table + 32×4 column, non-linear like T8), §8.4 six-step address summary, §8.5 block-boundary base pointers (wrap modulo 32 within the page) |
| `Swizzle` — `swizzle.{h,cpp}`: rect-level `LinearToVram` / `VramToLinear` (read and write side, any PSM, any `bw`, clipped to the 4 MB), plus the word-unrolled fast path that must stay byte-identical to the scalar one | the 4 families: F1 decoder `texture_decoder.cpp GetAddressPSM*` + `~:2300` r121blk block-LUT; F2 `gs_deswizzle.h:57 addr_psmct32 / :78 addr_psmct16 / :101 addr_psmt8`; F3 write-side `gs_state.cpp WriteHWReg / writePixel / local→local` (23 `GetAddress` refs; `PS2_XFER_FAST` unrolled variant); F4 encode-side `opengl_backend.cpp SyncRTToVRAM / SyncZToVRAM / EncodeRTToVRAM` (34 refs). Derived consumers that must be expressible from the same table (not swizzlers): shader-side closed-form remaps `opengl_backend.cpp` fmt 3/4/5 + `s9_composite_native.inc`; `shaders/swizzle.glsl` (INDEX:695 — DELETE, reference extracted first, HACK_LEDGER:241) | A3 §2 #2: "write-side and encode-side have no direct cross-proof; they agree today by discipline, not by construction" — NEW has ONE rect walker over `PsmLayout::PixelAddress`, so F1–F4 agree by construction and the oracle proves it against each OLD family | GS_SKILL §8.4 (the six-step page → block → column → word address; the LUT-per-page practice), §8.1/§8.3 (per-PSM column/block interleave), §8.2 (buffer-relative x,y; width in `bw` units per §4 item 4) |
| `ClutLayout` — `clut_layout.{h,cpp}`: `Csm1Index(i)` (the ONE CSM1 index reorder), `Csm2Index`, `ReadClut32/16(vram, cbp, csm, csa, cpsm, entry)`; CSM2 = linear (COU/CSA offset loading NOT modeled, scope cut kept) | `texture_decoder.cpp:944 CSM1Swizzle` (8-entry half-block swap within 32), `:1146 ReadCLUT32`, `:1217 ReadCLUT16`; byte-identical copies `opengl_backend.cpp:21667`, `s9_composite.h:322`; variant `gs_deswizzle.h:140 csm1_unswizzle`; `opengl_backend.h EnsureClutTex` (prefolded 256×1 — Phase 6 consumer, not here) | HACK_LEDGER:266 / A1 finding 7 (3 copies + 1 variant → one implementation); A3 §2 #3 (CSM2 treated as linear — kept, now documented as the scope cut in the README, with the involution + linear property tests pinning what IS claimed) | 2.7.1 CLUT Configuration (16/256 entries, CT32/CT16 entry formats), 2.7.3 CLUT Storage Mode (CSM1 = the 16×16-in-CT32-page / 8-entry swap layout; CSM2 = sequential), 2.7.4 CLUT Storage Format; 3.4.7 CLUT Buffer Control (CBP/CSA/CPSM/CSM field meaning) |
| `TexaExpand` — `texa_expand.{h,cpp}`: `ApplyTexa24(rgb, ta0, aem)`, `Expand16To32(rgba5551)` = **bit-replicate** + MSB→alpha (`msb ? 0xFF : 0` — the TEXA-less frame-readback / FBMSK-merge form, = OLD), `ApplyTexa16(rgba5551, texa)` = the manual's §3.4.6 table (revised by p2c per the p2br review: NEW's CT16 texture decode APPLIES it — no OLD-compatible TEXA-less texture arm; EQUIVALENCE_PLAN §7.1 **P2-CT16-TEXA** candidate, Phase 6 frame lane arbitrates). Replicate-vs-manual-shift recorded as §7.1 **P2-EXP16-REPL** | `texture_decoder.cpp:502–553 DecodePSMCT24` (ta0 + AEM applied ✔); `:740–780 DecodePSMCT16 → Expand16to32 (:1946)` (TA0/TA1/AEM NOT applied — A3 §2 inconsistency 1; `opengl_backend.cpp:1395` "no TEXA" comment); RT-store 5551 expansion `opengl_backend.cpp ~:1620/:2157` ("R162-proven bit-replicate + msb"); `s9_shadow.h` value logic (`:54 PS2_GS_ABIT16`, `:102 PS2_GS_CT16_QUANT`) | Zero-fill vs replicate adjudicated: **replicate** — A3 §2 "replicate is the settled convention; any zero-fill variant is legacy and must not survive the refactor", and the WALLNOISE chain (BUG-020 [WN4-CT16Q]: CT16/CT16S accumulator decode→re-render round trips, bounded residual ≤ one 5-bit step) is only stable when expansion is the inverse of the 5-bit truncation. Zero-fill is not written anywhere. CT16-TEXA gap: NOT fixed here (CONSTRUCTION_PLAN §4.2 accepted-difference protocol) | 3.4.6 Format Conversion (TEXA: 16/24-bit texel → 32-bit RGBA; TA0 for A=0 / 24-bit, TA1 for A=1, AEM: RGB==0 → alpha 0); 2.3.1 Color Data Format (RGBA16 bit positions 5-5-5-1) |
| `PixelConvert` — `pixel_convert.{h,cpp}`: pack/unpack per PSM (RGBA8 ↔ CT32/CT24/CT16/CT16S bytes; Z32/Z24/Z16/Z16S values; T8/T4/T8H/T4HL/T4HH index extraction), `ScaleAlphaPs2ToGpu` (0–128 → 0–255, ≥128 → 255) and inverse, `BytesPerPixel(psm)` delegating to `PsmLayout` | `texture_decoder.cpp DecodePSMCT32/CT24/CT16/Z32/Z16/Z16S` leaves; `texture_decoder.h ScaleAlphaPS2ToGPU`; `gs_state.h GetPixelSize`; `gs_state.cpp:5066/:5332` pixels-per-write switches; `s9_shadow.h` value logic (`:62 PS2_GS_ZSRC` colour-authored Z routing, `:54 PS2_GS_ABIT16` A-bit convention on CT16S) — values only, serve-side consumers Phase 6 | A3 §2 #4 + HACK_LEDGER:267 (per-PSM bpp tables scattered) → one source; the Z-helper contract ("PSMCT helpers on Z bytes silently corrupt co-paged data") becomes a type distinction (`is_z`) enforced by the API, not a comment | 2.3.1 Color Data Format (CT32/CT24/CT16/CT16S layouts, alpha 0x80 = 1.0), 2.3.2 Z Value Format (Z32/Z24/Z16/Z16S), 2.5.4 FB/Z combinations, 2.6.3 (T8H/T4HL/T4HH index in the upper byte/nibbles of a CT32 word) |
| `BlockDecode` — `block_decode.{h,cpp}`: per-PSM block → RGBA8 decode into a linear tile; the block-LUT fast path is GENERATED from `PsmLayout` at startup (not hand-typed); NEON pass behind `PS2R_SIMD`, identical output | `texture_decoder.cpp` decode leaf (per-PSM block decode), `~:2300` r121blk LUT-flattened decode ("same tables, LUT-flattened; equivalence-tested"), `ComputeDecodeFootprint` / `ComputeDecodeRowBand` (mirrors GetAddress page math; consumed by flush narrowing + `texcache_key.h` — Phase 6 consumers) | A3 §2 "Equivalence tests" row: decoder ↔ LUT ↔ rect identity was proven by a separate harness (`tests/test_block_decode_equivalence.cpp`, 1,150 ln); NEW makes it true by construction (LUT derived from the same table) and keeps the ported harness as the regression pin | GS_SKILL §8.1 (block = 64 words = 256 bytes; page = 32 blocks = 8 KB; the block W×H per class: CT32-class 8×8, CT16-class 16×8, T8 16×16, T4 32×16 — spelled out in the §8.1 table and re-drawn per format in §8.3.1–8.3.4; `PsmLayout` is the single source, the lifted OLD tables are only the oracle's comparand) |
| `MipChain` — `mip_chain.{h,cpp}`: MIPTBP1/MIPTBP2 → per-level `(tbp, tbw)`, level dimensions (`w>>l`, `h>>l`, min 1), `MaxLevels(MXL)`; **addressing half only** — LOD/K/L, trilinear and sampling policy go to `gs/hle/texsrc/mip_sampler` (Phase 6) | `gs_mipmap.h` (358 ln; INDEX:367 "split") addressing half; flag sites `:64 PS2_GS_MIPMAP`, `:126 PS2_GS_MIP_LODREF`, `:128 PS2_GS_MIP_ALLCLASS`, `:187/:326 PS2_MIPCEN`, `:278 PS2_GS_MIP_TRILINEAR` | `gs_mipmap.h` mixes MIPTBP addressing with LOD policy and a census (INDEX:367: 5 flags → 3 NORM / 1 DIAG / 1 DELETE); NEW separates address math (here) from policy (Phase 6) | 3.4.11 MIPMAP (MIPTBP1: TBP1–3/TBW1–3, MIPTBP2: TBP4–6/TBW4–6; level size halving; MXL), 3.4.3 Texture Information Setting (TEX1 MXL/MTBA — MTBA auto-addressing is Phase 6 if the game uses it) |

`README.md` for the directory lists the four OLD swizzle families with their line ranges (DoD).

## 3. Flags applied (14 rows — FLAG_LEDGER master rows; 11 NORM / 2 DIAG / 1 DELETE)

All NORM rows are `ON-unless-0` / peak values (FLAG_LEDGER "PEAK CONFIG BECOMES NORM"; FILEMAP_graphics
:414–419): write ONLY the winning arm's value semantics; the serve-side consumers land in Phase 6.

| Flag | Outcome | OLD site (ledger) | Phase-2 owner / what is written |
|---|---|---|---|
| `PS2_GL_FAST` | NORM (=1) | `render.h:449/:503/:2489`, `texture_decoder.h:28` | `BlockDecode`: only the FAST decode leaf is written (no slow arm) |
| `PS2_GL_TEXCACHE` | NORM (on) | `texture_decoder.h` | `BlockDecode`/`PsmLayout`: footprint math assumes the cache path exists (consumer Phase 6) |
| `PS2_GS_ABIT16` | NORM (on) | `s9_shadow.h:54` | `PixelConvert`/`TexaExpand`: CT16S A-bit convention = winning arm |
| `PS2_GS_ALT16UV` | NORM (on) | `s9_shadow.h:78` | `PsmLayout::alias_class` sizing of the 16-bit companion view (Phase 6 consumer) |
| `PS2_GS_CLUTRAW` | NORM (on) | `s9_shadow.h:70` | `ClutLayout::ReadClut*` returns raw CLUT alpha (no prefold) |
| `PS2_GS_CT16_QUANT` | NORM (on) | `s9_shadow.h:102` | `PixelConvert`: 5-bit truncation convention for 32→16 (dither itself Phase 6) |
| `PS2_GS_MIPMAP` | NORM (peak value) | `gs_mipmap.h:64` | `MipChain`: chain addressing always available; enable policy Phase 6 |
| `PS2_GS_MIP_ALLCLASS` | NORM (peak) | `gs_mipmap.h:128` | `MipChain`: no per-class exclusion arm written |
| `PS2_GS_MIP_TRILINEAR` | NORM (peak) | `gs_mipmap.h:278` | policy — nothing in `MipChain`; recorded for the Phase-6 sampler |
| `PS2_GS_RTKEEP` | NORM (on; invariant #15/#16) | `s9_shadow.h:86` | serve predicate — nothing here; recorded (Phase 6 `texsrc`) |
| `PS2_GS_ZSRC` | NORM (on) | `s9_shadow.h:62` | `PixelConvert`: colour-authored-Z value routing = winning arm |
| `PS2_GS_READER_CENSUS` | DIAG | `gs_vram_census.h:120` | `core/diag` `DiagRegistry` category; census body lands with `vram_census` (Phase 6) |
| `PS2_MIPCEN` | DIAG | `gif.cpp:5043`, `opengl_backend.cpp:10332`, `gs_mipmap.h:187/:326`, `gs_state…` | `core/diag` category; `MipChain` has no census code |
| `PS2_GS_MIP_LODREF` | DELETE | `gs_mipmap.h:126` | not written (LOD reference arm) |

No `getenv` anywhere in `gs/format` (ctest `CoreSingleGetenvSite` stays at 1). No Config field is added
in Phase 2 (settings table stays at 59 rows; `SettingsTableReproducible` unchanged).

## 4. Oracle strategy — `tests/oracle/og_gs_format_oracle.cpp` (differential against the frozen OLD)

Mechanism (extends the Phase-1 `CoreRingDecodeOracle` pattern): the test TU `#include`s OLD
`host_app/gs_deswizzle.h` read-only by absolute path (`PS2R_OG_ROOT`, std-only closure; skipped with a
printed message when unset), and reads the OLD `texture_decoder.cpp` tables through
`tests/oracle/og_tables.bin` lifted by `tests/oracle/lift_og_tables.py` (parses the constexpr initializers
of the seven block tables `:263/:275/:577/:590/:604/:872/:922` AND the three column tables
`:285 columnTable32[8][8]`, `:616 columnTable16[8][16]`, `:883 columnTable8[16][16]`, and re-evaluates the T4
column formula `:1017-1025` `column = (y%16)/4; nibble = (x%32) + (y%4)*32` into a `[16][32]` table;
checked in with sha256 next to it, `og_tables.sha256`). OLD is never built or edited (REFERENCE_FREEZE);
every OLD binary run goes through `verify/run_bounded.sh`.

Naming drift (p2pr item 6, recorded per §0.1 practice): the oracle TU and its fixtures live under
`tests/oracle/` (`og_gs_format_oracle.cpp`, `lift_og_tables.py`, `og_tables.bin`, `og_tables.sha256`) —
the directory whose README defines the read-only OLD-include mechanism and whose `og_` prefix is
lint-exempt for the OLD includes — not under `tests/gs/format/fixtures/` as the p1d draft wrote. The
NEW-only unit tests go to `tests/core/test_psm_layout.cpp` (Phase-1 suite mechanism, `ps2r_core_test`
pattern) until a `tests/gs/` suite exists (2B).

**Units** (p2pr item 4). Three conventions are in play and every comparison states its conversion:
- OLD F1 (`texture_decoder.cpp GetAddressPSM*`): `tbp` in **bytes**, `tbw` in **pixels**
  (`pages_per_row = tbw / page_w`, clamped to ≥ 1); returns a **byte** address `% VRAM_SIZE`.
- OLD F2 (`gs_deswizzle.h addr_psm*`): `bw` in **pixels**, **no base** (page-relative byte offset).
- GS manual (`PsmLayout`): `bp` in **64-word (256-byte block) units** (TBP0/DBP/CBP convention; FBP/ZBP
  are 32× coarser and convert as `fbp·32`), `bw` in **64-pixel units** (TBW/FBW), `x,y` buffer-relative
  pixels. `PixelAddress` returns the address in the PSM's **storage-element** unit (word for the 32-bit
  class incl. T8H/T4HL/T4HH, halfword for the 16-bit class, byte for T8, nibble for T4);
  `PixelByteAddress` = element · bits / 8. Oracle conversion: `tbp = bp·256`, `tbw = bw·64`, compare bytes.
  `pages_per_row = max(1, bw·64 / page_w)` — for T8/T4 (page_w 128) an odd `bw` floors, matching OLD's clamp.
- **4 MB boundary rule**: OLD wraps the byte address `% VRAM_SIZE` (4 MB). NEW defines the same rule —
  the element address is reduced modulo the local memory size expressed in elements (4 MB · 8 / bits),
  which equals OLD's byte wrap for every PSM (T4: nibble wrap at 8 M nibbles = 4 MB) — and tests it
  (`test_psm_layout` wrap cases + oracle `bp` samples in the top pages). No clamping, no error: the GS
  address space is 20-bit words and wraps.

**Grid budget** (p2pr item 4; counts corrected by p2b to what `og_gs_format_oracle.cpp` runs). Per PSM, as
separate bounded ctest cases: `bw` **sampled** over 11 values `{1,2,3,4,5,7,8,10,16,20,32}`, `x < 64·bw`,
`y < 2·page_h`, and `bp` **sampled** over 9 bases (not every page start): `{0, block 1, block 9 (§8.5
examples), mid (page 256 = 0x100·32 blocks), the two fixture bases the OLD `test_page_invariance` uses
(`0x330000`/`0x3c0000` bytes → blocks 0x3300/0x3c00), top-3 pages (509·32, 510·32+5, 511·32 — exercise the
4 MB wrap)}`. Cost: 11 × 9 × 2·page_h × 64·bw ≈ 4–16 M address triples per PSM (measured 2A: 3.98 M for the
32-bit class, 7.96 M for 16-bit/T8, 15.9 M for T4), ≤ 2 s per PSM at -O2, whole oracle bounded at 60 s per case.

1. **Grid**: for every `Psm` (13) over the sampled grid: `PsmLayout::PixelByteAddress == OLD` address for
   every OLD family that covers that PSM (`gs_deswizzle.h` addr_* for CT32/CT16/T8; lifted
   `GetAddressPSM*` tables for all 7 OLD F1 variants; CT24 via CT32, Z24 via Z32, CT16S via CT16 —
   OLD's own aliasing, `gs_state.cpp:4534/:5110/:5362` / `texture_decoder.cpp:1046`).
2. **Where OLD copies disagree with each other** (the A3 §2 "by discipline" gap) — rule 2a: the oracle
   does NOT pick one — it asserts `NEW == GS-manual value` (computed from the §2 rule, spelled out in the
   test as the formula from GS_SKILL §8.4 over the §8.3 tables) and records the disagreeing OLD copy as an
   **accepted-difference CANDIDATE** in `EQUIVALENCE_PLAN.md` §7.1 (family, site, PSM, the (x,y,bp,bw)
   witness). Silent tolerance is forbidden. Any NEW≠manual is a construction bug — fix NEW.
   **Rule 2b — OLD unanimous but ≠ GS manual** (p2pr item 1): the same rule applies when every OLD copy
   agrees with every other but not with the manual. Two instances are pre-registered:
   - **PSMCT16S** — OLD has no CT16S table; all three write/serve sites alias CT16S to the CT16 helper
     (`gs_state.cpp:4534`, `:5110`, `:5362` "use same swizzle for now"; `texture_decoder.cpp:1046`).
     GS_SKILL §8.3.2 gives CT16S its own block table (`{0,2,16,18},{1,3,17,19},{8,10,24,26},…` — blocks
     4–7 and 8–11 swap rows relative to CT16). **DECISION: NEW implements the manual's CT16S table.**
   - **PSMT4** — OLD's intra-column order is linear (`texture_decoder.cpp:1017-1025`:
     `nibble = (x%32) + (y%4)·32`; `PageLUTs` `:2313-2320` is built from the same formula, so F1 and the
     LUT agree by construction). GS_SKILL §8.3.4 (+ the §8.3.3 T8 pattern it says T4 follows) is
     non-linear: word order `0,1,4,5,8,9,12,13 / 2,3,6,7,10,11,14,15` per 8-texel group, rows 2–3
     rotate the pattern by four words, columns 1/3 swap the rotation, rows 0–1 use the even nibbles of the
     word and rows 2–3 the odd nibbles (the same even/odd-unit rule §8.3.3 draws for T8's bytes).
     **DECISION: NEW implements the manual's non-linear T4 column.**
   - **DECISION RULE** for both: the differential oracle asserts `NEW == manual`, reports OLD's deviation
     as `DEVIATION(count, first (x,y,bp,bw))` — never as a failure — and the deviation is recorded as
     EQUIVALENCE_PLAN §7.1 accepted-difference rows **P2-CT16S** and **P2-T4** (candidate status). The
     **arbiter is Phase 6's frame-lane gate**: if the game never touches a CT16S/T4 layout where OLD and
     the manual differ (CT16S at all; T4 within a column), no pixel changes and the rows close as
     "no observable difference"; if pixels change, adjudicate per EQUIVALENCE_PLAN §7 with the manual as
     truth (the OLD frame is then the wrong one and the baseline is re-frozen under the §7 record). NEW
     never carries an OLD-compatible arm for either.
3. **Lifted fixtures**: the OLD `test_block_decode_equivalence` case list (its printed cases, re-run
   bounded from OLD `build/tests/test_block_decode_equivalence`) drives ported cases: decoder ↔ LUT ↔ rect
   identity over the same `(psm, tbp, tbw, w, h)` set, now against `BlockDecode` + `Swizzle` (2D).
4. **Ports**: `tests/gs/format/test_block_decode_equivalence.cpp` (from OLD 1,150 ln; the TU-static copies
   are replaced by linking `ps2r_gs_format`) and `tests/gs/format/test_page_invariance.cpp` (from OLD 457
   ln: two pages `0x330000`/`0x3c0000`, tile/L1 authored indices, neighbour-page noise — the
   "co-page corruption" contract of GS_SKILL §8.3 ("same block and column number in the same page occupy
   the same physical memory regardless of format") expressed as: writes through `Swizzle` at page A never
   change a byte of page B and Z rows never alias colour rows). The OLD env pin (`s_env_pin`) is dropped
   (tests take arguments).
5. **Property tests** (`tests/core/test_psm_layout.cpp` in 2A; `tests/gs/format/test_psm_properties.cpp`
   from 2B): page/block/column bijection per PSM (every `(x,y)` in a page maps to a unique element, all
   covered); `Csm1Index` is an involution on the 256-entry grid and identity on the 16-entry grid;
   `Expand16To32 ∘ Truncate32To16 == id` on 5-bit values (the replicate adjudication); `PixelConvert`
   pack∘unpack identity per PSM; `MipChain` level sizes halve to ≥1 and MIPTBP fields round-trip.

## 5. Proof gate and Definition of Done

| Gate row | Evidence to quote |
|---|---|
| `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` 100 % (18 existing + the new `Gsformat_*` / `OgGsFormatOracle_*` cases), 0 warnings | ctest summary line |
| `macos-diag` preset builds and passes | ctest summary line |
| `LintOneClass` / `LintEnginePurity` 0 findings over the new files; `CoreSingleGetenvSite` = 1 | lint lines |
| OG oracle: 13 PSMs × sampled grid IDENTICAL vs every OLD family that covers the PSM, EXCEPT the two pre-registered rule-2b rows (CT16S, T4) which must report DEVIATION with counts (never hidden); any further OLD-copy disagreement listed as a §7.1 candidate (count quoted, 0 or N) | oracle summary line per PSM |
| Ported `test_block_decode_equivalence` + `test_page_invariance` pass against NEW; OLD binaries re-run bounded once for the case list (quoted case count) | both PASS lines + OLD case count |
| Property tests pass (bijection, involution, replicate round trip, pack/unpack, mip halving) | PASS line |
| DoD grep: `grep -rnE "GetAddressPSM|swizzle" engine app --include=*.{h,cpp,inl}` outside `engine/gs/format` → 0 | grep count |
| `engine/gs/format/README.md` lists the four OLD families with line ranges + the 14 flag rows | file present, row counts |
| Accepted-difference: CT16-TEXA gap recorded in EQUIVALENCE_PLAN §7.1 as a candidate (not applied); rule-2b rows P2-CT16S and P2-T4 recorded with the Phase-6 arbiter caveat; any OLD-copy disagreement recorded likewise | §7.1 row ids |
| No harness lane claimed; no behaviour claim made | PHASE2_IMPL statement |

## 6. Stage breakdown (each ≤ 45 min wall; STAGE_RULES rule 3 — stop adding scope at ~35 min, leave green)

**2A — tables + `PixelAddress` + oracle.** `engine/gs/format/{psm.h, psm_layout.h/.cpp}`,
`engine/gs/CMakeLists.txt` (`ps2r_gs_format`, no SDL/GL), `tests/oracle/lift_og_tables.py` +
`tests/oracle/og_tables.bin` (+ `og_tables.sha256`), `tests/oracle/og_gs_format_oracle.cpp` running grid
step 1 for all 13 PSMs vs the lifted F1 tables and CT32/CT16/T8 vs `gs_deswizzle.h` (F2); per-PSM ctest
cases; `tests/core/test_psm_layout.cpp` (bijection, arithmetic, units, 4 MB wrap). README skeleton (family
table with line ranges). PHASE2_IMPL §2A with the per-PSM oracle table (CT16S and T4 expected DEVIATION —
counts quoted; all others IDENTICAL) and the P2-CT16S / P2-T4 §7.1 rows. Stop rule: if the lift script
fights the initializer syntax past 15 min, hand-lift the tables into the fixture with the source line cited
and let the script only verify the sha.

**2B — `Swizzle` + `ClutLayout`.** Rect walker (scalar + word-unrolled fast path, byte-identical test),
oracle step 2 (manual-formula assertions where OLD families disagree), `ClutLayout` with the ONE CSM1
reorder, CLUT read 32/16, involution property test, port of `test_page_invariance` (co-page contract).
Flags: `PS2_GS_CLUTRAW`, `PS2_GS_ALT16UV` value semantics. PHASE2_IMPL §2B.

**2C — `TexaExpand` + `PixelConvert`.** TEXA-24 and TEXA-16 as the manual's table (p2br revision: NEW applies
TEXA to CT16 texels; OLD's omission is the §7.1 P2-CT16-TEXA candidate), 16→32 replicate (§7.1 P2-EXP16-REPL); pack/unpack per PSM + alpha domain (`ScaleAlphaPs2ToGpu` and inverse,
5-bit truncation convention); property tests (replicate round trip, pack∘unpack identity per PSM).
PHASE2_IMPL §2C.

**2D — `BlockDecode` + `MipChain` + ports + gate.** Block decode with the generated LUT and the `PS2R_SIMD`
option (identical output); MIPTBP addressing; port of `test_block_decode_equivalence` (bounded OLD run for
the case list); DoD grep; README flag rows complete; PHASE2_IMPL §2D + gate table + HANDOFF. Review stage
2R follows (own runs of the gate table, as p1cr did).

## 7. HANDOFF template (PHASE2_IMPL, per stage)

Done / Not done / Next command (`cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`, then
`ctest --preset macos-dev -R OgGsFormatOracle` — `PS2R_OG_ROOT` is a CMake cache path defaulting to
`$PS2R_OLD_ROOT`; the oracle SKIPs loudly when the OLD tree is absent).
