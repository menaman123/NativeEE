# PHASE2_IMPL — execution record for Phase 2 (`engine/gs/format/`, pure math)

Plan: `docs/phases/PHASE2_PLAN.md` (revised by p2a per the p2pr review). Rules: `docs/phases/STAGE_RULES.md`.
No harness lane is claimed by Phase 2; no game-visible behaviour changes. OLD read-only throughout.

## 2A — plan corrections + `PsmLayout` + differential oracle (stage p2a, 2026-09-26)

### Part 1 — PHASE2_PLAN.md corrections (p2pr review, 6 items)

| # | Correction | Where |
|---|---|---|
| 1 | §4 rule **2b** "OLD unanimous but ≠ GS manual", two pre-registered instances with DECISIONs: **PSMCT16S** (OLD aliases to CT16: `gs_state.cpp:4534/:5110/:5362`, `texture_decoder.cpp:1046`; manual §8.3.2 has its own table) and **PSMT4** (OLD linear intra-column `texture_decoder.cpp:1017-1025`, `PageLUTs :2313-2320`; manual §8.3.4 non-linear). DECISION RULE: NEW implements the MANUAL; oracle asserts NEW == manual and reports OLD's deviation; EQUIVALENCE_PLAN §7.1 rows **P2-CT16S**, **P2-T4** (candidates); Phase 6 frame-lane gate arbitrates, manual = truth | PHASE2_PLAN §4 item 2 |
| 2 | Layout citations → GS_SKILL **chapter 8** (§8.1 table, §8.2, §8.3.1–8.3.4, §8.4, §8.5) in the PsmLayout / Swizzle / BlockDecode rows; the false "block dimensions are NOT spelled out in GS_SKILL" sentence deleted (they are the §8.1 table). §2.3/§2.7/§3.4 citations kept for value/CLUT/MIP rows (those sections exist in GS_SKILL) | §2 intro + table |
| 3 | Lift list += `columnTable32 (:285)`, `columnTable16 (:616)`, `columnTable8 (:883)`, T4 column formula `(:1017-1025)` evaluated to a `[16][32]` table | §2 PsmLayout row, §4 mechanism |
| 4 | Grid budget (bp **sampled**: 0, block 1, block 9, mid page 256, OLD `test_page_invariance` bases 0x330000/0x3c0000 bytes, top pages 509/510+5/511; `bw` 11 values; `x < 64·bw`, `y < 2·page_h`), **units** stated (OLD F1 tbp bytes / tbw pixels; F2 bw pixels no base; manual bp 64-word blocks / bw 64-px; NEW element units + `PixelByteAddress`), **4 MB rule** defined (element address mod 4 MB-in-elements == OLD's byte `% VRAM_SIZE`; tested) | §4 Units + Grid budget |
| 5 | 2C split → **2C** (`TexaExpand` + `PixelConvert` + property tests + §7.1 CT16-TEXA row) and **2D** (`BlockDecode` + `MipChain` + block-decode port + `PS2R_SIMD` + DoD + README + gate) | §6 |
| 6 | Naming drift recorded: oracle TU + fixtures live in `tests/oracle/` (`og_gs_format_oracle.cpp`, `lift_og_tables.py`, `og_tables.bin`, `og_tables.sha256`), not `tests/gs/format/fixtures/`; unit tests in `tests/core/test_psm_layout.cpp` until a `tests/gs/` suite exists | §4 mechanism, §5, §6, §7 |

### Part 2 — files landed

| File | Content | OLD source read (read-only) |
|---|---|---|
| `engine/gs/format/psm.h` | `Psm` (13 field values), `PsmClass` (4 geometry classes), `PsmDescriptor` POD (bits, index bits/shift, page/block/column W×H, `is_z`, `is_indexed`, `shares_ct32_word`) | — (GS_SKILL §8.1 table) |
| `engine/gs/format/psm_layout.{h,cpp}` | `PsmLayout`: descriptor table, `FromField`, elements-per-page/block/column, `PagesPerRow` (§8.2), `BlockInPage` (six block tables, §8.3.1–8.3.4, incl. **CT16S** and **Z16S**), `ElementInBlock` (column arrangement: §8.3.1 8×2 word order; 16-bit halves; T8/T4 four-row columns with the rotated rows 2–3, columns-1/3 swap, even/odd byte/nibble rule), `ElementInPage`, `PixelAddress` (§8.4 steps 1–6, element units, 4 MB wrap), `PixelByteAddress`, `PixelBitInByte`. Units documented in the header, incl. the §8.5 block-boundary base reading (plain `bp + block` addition; the "wrap modulo 32" is the figure's labelling) | tables written from GS_SKILL ch. 8; OLD `texture_decoder.cpp:263-1025`, `gs_deswizzle.h:12-137`, `gs_state.cpp:4534/:5110/:5362` read to identify the deviations |
| `engine/gs/CMakeLists.txt` | `ps2r_gs_format` static lib (`-Wall -Wextra -Werror`, no SDL/GL); root `CMakeLists.txt` `add_subdirectory(engine/gs)` | — |
| `tests/oracle/lift_og_tables.py` | lifts the 7 block + 3 column tables by name from OLD `texture_decoder.cpp` + evaluates the OLD T4 formula → `og_tables.bin` (`OGT1`, 11 tables, 2,772 bytes) + `og_tables.sha256` = `fbbe095b2b0f0a0b49472daeb6572f0852ed314fc5232dc23851e9e06ad22a91`. `--check` (no OLD needed), `--lift` (re-lift must equal the fixture), `--write` | `texture_decoder.cpp` `:263/:275/:577/:590/:604/:872/:922/:285/:616/:883/:1017-1025` |
| `tests/oracle/og_gs_format_oracle.cpp` | per-PSM differential: **M** (manual §8.4 spelled out independently over re-typed §8.3 tables, bit-address units), **F1** (OLD `GetAddressPSM*` re-evaluated from the lifted tables with OLD's index expressions and aliases), **F2** (`gs_deswizzle.h` `#include`d read-only via `PS2R_OG_DESWIZZLE_H`, CT32/CT16/T8, bp=0 only). Fails only on NEW ≠ M; OLD deviations printed as `DEVIATION(count, first (x,y,bp,bw) NEW= OLD=)` + candidate line | `gs_deswizzle.h` (compile-time include) |
| `tests/oracle/CMakeLists.txt` | `OgTablesFixtureSha`, `OgTablesReLift`, `OgGsFormatOracle_<PSM>` × 13 (TIMEOUT 60, `-O2`) | — |
| `tests/core/test_psm_layout.cpp` (+ `tests/core/CMakeLists.txt`) | descriptors (13 defined of 64 field values; §8.1 size identities), **bijection** per PSM (page → elements; block tables are permutations of 0..31; elements stay in their column), §8.3 spot values (CT32 word order; CT16 halves; T8 33/96/65; T4 2/65/8), class sharing (T8≡CT32, T4≡CT16 block tables), §8.2 page arithmetic, **units** (block = 256 B for every mode; T4 odd nibble → bit 4), **4 MB wrap** (block 16384 ≡ 0; last page + one page row ≡ page 0) | — |

Lints: `LintOneClass`, `LintEnginePurity`, `CoreSingleGetenvSite` all pass on the new files (no `getenv`, no tags,
one class per pair, `psm.h` is data-only). No Config field, no flag consumed in 2A (the 14 rows are 2B–2D).

### Per-PSM oracle table (`./build/tests/oracle/og_gs_format_oracle tests/oracle/og_tables.bin all`, 7.6 s)

| PSM | samples | M (NEW vs manual) | F1 (OLD `GetAddressPSM*`, lifted) | F2 (OLD `gs_deswizzle.h`) |
|---|---|---|---|---|
| PSMCT32 | 3,981,312 | NEW == manual | IDENTICAL | IDENTICAL |
| PSMCT24 | 3,981,312 | NEW == manual | IDENTICAL | n/a |
| PSMCT16 | 7,962,624 | NEW == manual | IDENTICAL | IDENTICAL |
| **PSMCT16S** | 7,962,624 | NEW == manual | **DEVIATION(count=5,971,968, first (x=32,y=0,bp=0,bw=1) NEW=0x001000 OLD=0x000800)** — by design, §7.1 **P2-CT16S** | n/a |
| PSMT8 | 7,962,624 | NEW == manual | IDENTICAL | **DEVIATION(count=442,368, first (x=0,y=4,bp=0,bw=1) NEW=0x000060 OLD=0x000040)** — OLD-copy disagreement (F2 omits the §8.3.3 column-1/3 swap; F1 has it), §7.1 **P2-T8-F2** |
| **PSMT4** | 15,925,248 | NEW == manual | **DEVIATION(count=15,676,416, first (x=1,y=0,bp=0,bw=1) NEW=0x000004 OLD=0x000000)** — by design, §7.1 **P2-T4** | n/a |
| PSMT8H | 3,981,312 | NEW == manual | IDENTICAL | n/a |
| PSMT4HL | 3,981,312 | NEW == manual | IDENTICAL | n/a |
| PSMT4HH | 3,981,312 | NEW == manual | IDENTICAL | n/a |
| PSMZ32 | 3,981,312 | NEW == manual | IDENTICAL | n/a |
| PSMZ24 | 3,981,312 | NEW == manual | IDENTICAL | n/a |
| PSMZ16 | 7,962,624 | NEW == manual | IDENTICAL | n/a |
| PSMZ16S | 7,962,624 | NEW == manual | IDENTICAL | n/a |

Result: 13/13 NEW == manual; 11/13 IDENTICAL to OLD F1; the two pre-registered rule-2b modes deviate as
predicted (counts above); one unplanned rule-2a finding (F2 T8) recorded. Accepted-difference candidates:
EQUIVALENCE_PLAN §7.1 rows **P2-CT16S**, **P2-T4**, **P2-T8-F2** (Phase 6 frame lane arbitrates; manual = truth).

### Decisions taken in 2A

1. **Manual over OLD** for CT16S and T4 (PHASE2_PLAN §4 rule 2b) — no OLD-compatible arm exists in NEW.
2. **T4 nibble reading of GS_SKILL §8.3.4**: the chapter compresses the bit-range header; NEW reads it as the
   §8.3.3 T8 rule transposed to nibbles — rows 0–1 use the even nibbles (bits 0-3, 8-11, 16-19, 24-27), rows
   2–3 the odd nibbles — the same even/odd storage-unit rule §8.3.3 draws for T8's bytes. Written as a
   named helper (`FourRowColumnWord`) with the rule in its comment; the oracle's independent M copy encodes
   the same reading, so a future correction is a two-site edit and the bijection test pins totality.
3. **§8.5 block-boundary bases** are plain addition (`bp + block`), data running into the next page; the
   "wrap modulo 32" is the figure's labelling. Matches OLD's `tbp + addr`.
4. **4 MB rule**: wrap, never clamp (20-bit word address space). Equals OLD's `% VRAM_SIZE`.
5. **Units API**: `PixelAddress` in storage-element units + `PixelByteAddress`/`PixelBitInByte`; `bp` in
   256-byte blocks, `bw` in 64-pixel units; `PagesPerRow` clamps to ≥ 1 like OLD (T8/T4 with bw = 1).

### Gate status after 2A

`cmake --build --preset macos-dev -j4` → 0 warnings; `ctest --preset macos-dev` →
**`100% tests passed, 0 tests failed out of 34`** (18 Phase-1 + `Coretest_psm_layout` + `OgTablesFixtureSha` +
`OgTablesReLift` + 13 `OgGsFormatOracle_*`; slowest `OgGsFormatOracle_PSMT4` 2.00 s). `macos-diag` preset not
rebuilt this stage (no diag code touched) — 2D's gate row runs it.

### HANDOFF (2A → 2B)

**Done:** the six plan corrections; `psm.h` / `psm_layout.{h,cpp}` / `ps2r_gs_format`; lift script + fixture +
sha; per-PSM oracle (13 ctest cases) with F1/F2/manual; `test_psm_layout`; §7.1 rows P2-CT16S / P2-T4 / P2-T8-F2;
tree green.

**Not done (2A scope, deferred to 2B):** `engine/gs/format/README.md` family table with line ranges (the
Phase-0 skeleton README is still in place — its "Belongs here" list should be refreshed to the §2 class names
`PsmLayout, Swizzle, ClutLayout, TexaExpand, PixelConvert, BlockDecode, MipChain`); `tests/oracle/README.md`
one-line mention of `lift_og_tables.py` / `og_tables.bin`; `docs/phases/README.md` line for PHASE2_IMPL.

**Next command:**
```
cd <repo> && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev
ctest --preset macos-dev -R 'OgGsFormatOracle|OgTables|Coretest_psm_layout'
./build/tests/oracle/og_gs_format_oracle tests/oracle/og_tables.bin all      # the per-PSM table above
```
Then 2B per PHASE2_PLAN §6: `Swizzle` (rect walker over `PsmLayout::PixelAddress`, scalar + word-unrolled,
byte-identical test) + `ClutLayout` (`Csm1Index`, `ReadClut32/16`), port of `test_page_invariance`, oracle
step 2 for the rect families (F3 write-side / F4 encode-side sites listed in §2).

## 2B — `Swizzle` + `ClutLayout` + oracle step 2 + page-invariance port (stage p2b, 2026-09-26)

### Part 1 — deferred 2A items and the p2ar review notes

| # | Item | Where |
|---|---|---|
| 1 | `engine/gs/format/README.md`: class table (the §2 names `PsmLayout, Swizzle, ClutLayout, TexaExpand, PixelConvert, BlockDecode, MipChain`), the four OLD swizzle families with line ranges, the CSM1 copies, scope cuts, flags consumed | file rewritten |
| 2 | `tests/oracle/README.md`: `lift_og_tables.py` / `og_tables.bin`, the oracle TU, the page-invariance port | "Belongs here" |
| 3 | `docs/phases/README.md`: PHASE2_IMPL line (2A/2B/2C/2D) | one sentence |
| 4 | PHASE2_PLAN §4 grid text corrected to what the oracle runs: **11 `bw` values** `{1,2,3,4,5,7,8,10,16,20,32}`, **9 `bp` bases**, measured sample counts | §4 Grid budget |
| 5 | EQUIVALENCE_PLAN §7.1 row **P2-BASE-8.5** — the block-boundary base reading (plain `bp + block`, data continues into the next page; the figure's "mod 32" is labelling) recorded as an assumption both trees share | §7.1 |
| 6 | Descriptor lookup hoisted in the rect walker: `Hoist(psm)` resolves the descriptor, linear bpp, pixel mask, index shift, elements-per-block/page, 4 MB size and the per-mode `ElementInBlock` table ONCE per call; `WalkRuns` then does one `BlockInPage` per block-aligned run and no per-pixel division | `swizzle.cpp` |

### Part 2 — files landed

| File | Content | OLD source read (read-only) |
|---|---|---|
| `engine/gs/format/swizzle.{h,cpp}` | `Swizzle`: `Buffer{psm,bp,bw}` / `Rect` in manual units; `LinearToVram` (F3 transfer write role = 1-row rects per arriving segment; F4 RT encode role = the sync rect) and `VramToLinear` (F1/F2 decoder read role) as the same two primitives over `PsmLayout`; linear side pixel-typed (`LinearBitsPerPixel`: index width for indexed modes incl. T8H/T4HL/T4HH, storage width otherwise; nibbles low-first), `ElementWriteMask` (CT24/Z24 `0x00FFFFFF`, T8H `0xFF000000`, T4HL `0x0F000000`, T4HH `0xF0000000`) ∧ caller mask (FBMSK) with read-modify-write; 4 MB wrap; `*Generic` per-pixel reference kept public for the identity test | `gs_state.cpp:5042-5170` (XFER_FAST word path) `:5362-5445` (legacy loop); `opengl_backend.cpp:15922-16080 EncodeRTToVRAM`, `:16272 SyncRTToVRAM`, `:18157-18420 SyncZToVRAM`; `texture_decoder.cpp` `GetAddress*` (2A) |
| `engine/gs/format/clut_layout.{h,cpp}` | `ClutLayout`: `Csm1Index` (THE reorder: GS_SKILL §2.7.3 figure, rows of eight in order 0,2,1,3 per 32-group — entries 8–15 ↔ 16–23; involution), `Csm2Index`, `Csm1Position(entry, entries)` (16×16 grid / 8×2 rectangle = the same formula's first 16), `EntryAddress(cpsm, cbp, csm2, entries, entry)` = `PsmLayout::PixelAddress(cpsm, x, y, cbp, 1)` (CSM1 rectangle stored in the CPSM layout, §2.7.4/§3.4.7; CSM2 CT16 = linear halfwords at CBP — scope cut kept; CSM2 CT32 = rectangle without reorder, OLD's behaviour), `ReadClut32/16` raw (no alpha prefold: `PS2_GS_CLUTRAW` winning arm), `ReadClut` bulk. CSA documented as a temporary-buffer offset, not a local-memory address (no parameter); COU/COV documented as the Phase-6 gap | `texture_decoder.cpp:944 CSM1Swizzle`, `:1146 ReadCLUT32`, `:1217 ReadCLUT16`; `opengl_backend.cpp:21667 R164Csm1Swizzle`; `s9_composite.h:322 Csm1Swizzle`; `gs_deswizzle.h:140 csm1_unswizzle`; `tests/test_page_invariance.cpp:118` authoring formula |
| `engine/gs/CMakeLists.txt` | `ps2r_gs_format` += `swizzle.cpp`, `clut_layout.cpp` | — |
| `tests/gs/CMakeLists.txt`, `tests/gs/format/test_swizzle.cpp` | new `tests/gs/` suite (`Gsformat_*`). **fast == generic** byte-identical for all 13 PSMs × 15 rects (1-row odd segments, page-sized, multi-page, sub-block, block-straddling, bases 0/1/9/0x3300/0x3c00/12345/511·32+3 incl. the 4 MB wrap) × 5 masks, both directions; **round trip** `LinearToVram ∘ VramToLinear = id` on in-width rects with a **footprint bit count** proving no two pixels share an element (bijection); two-page bijection per PSM; mask/bpp/pitch spot values; T8H-over-CT24 co-page coexistence (`0x10ABCDEF` in the CT32 view). 112,934 checks | — |
| `tests/gs/format/test_clut_layout.cpp` | involution + permutation on 0..255; the §2.7.3 figure row by row; 16-entry identity `(i%8, i/8)` = the 16×16 formula's prefix; CSM1/CT32 = the 1 KB of blocks 0–3 at CBP each word once, spot words `0,1,4,5,8,9,12,13 / 2,3,… / 64+`; CSM1/CT16 IDTEX4 = `{0,2,8,10,16,18,24,26,4,6,12,14,20,22,28,30}` (the §8.3.2 column order; = PCSX2 `clutTableT16I4`); CSM1/CT16 IDTEX8 covers 512 B each halfword once; CT16S ≡ CT16 for CLUT addresses; CSM2 linear; raw reads little-endian. 3,931 checks | — |
| `tests/oracle/og_gs_format_oracle.cpp` (+ `CMakeLists.txt`: cases `F3F4`, `CLUT`) | **step 2**: `RunRectFamily('3'/'4')` — OLD F3/F4 loop structure LIFTED (member functions with GSState/GL closures are not header-visible): per pixel of the rect, OLD F1 address through each family's own PSM dispatch (F3: CT32/CT24→CT32, CT16/CT16S→CT16, T8; F4: CT32/CT24, CT16, Z32/Z24→Z32, Z16, Z16S), bpp byte stores (CT24/Z24 3 bytes = F4's "byte 3 untouched"); 4 MB image compare vs `Swizzle::LinearToVram`. `RunClut`: NEW `Csm1Index` vs the three lifted copies (each cited) + `csm1_unswizzle` through the read-only include, OLD copies cross-checked; `ReadClut32` vs the lifted `ReadCLUT32` grid walk; `ReadClut16` vs the lifted linear `ReadCLUT16`; `Csm1Position` vs the OLD page-invariance authoring formula | as above |
| `tests/oracle/og_page_invariance.cpp` (ctest `OgPageInvariance`) | port of OLD `tests/test_page_invariance.cpp` (457 ln GL harness) to the VRAM-level contract: same 8 KB image (16×16 T8 tile, 8×8 L1 at +2 blocks, 256-entry CT32 CSM1 CLUT at +8 blocks) at blocks 0x3300 and 0x3c00, different neighbour noise; page images byte-identical / neighbours differ (the original's two sanity checks); tile, L1 and CLUT decode identically at A and B and equal the authored values; containment (authoring A touches no byte outside A); **Z rows never alias colour rows** (Z32/CT32, Z24/CT24, Z16/CT16, Z16S/CT16S: element differs at every pixel of a page — the Z block tables are the colour tables XOR 24); a full Z16 page covers exactly 8 KB. The `s_env_pin` and the GL arms are dropped (no render path yet). 16 checks | `tests/test_page_invariance.cpp` |

Lints: `LintOneClass`, `LintEnginePurity`, `CoreSingleGetenvSite` pass on the new files (one class per pair, headers
declarations-only, no tags, no `getenv`, no game tokens). Flags consumed: `PS2_GS_CLUTRAW` (NORM: raw CLUT alpha),
`PS2_GS_ALT16UV` (NORM: no arm to write — the 16-bit companion view's geometry is `PsmLayout`'s class table; consumer
Phase 6). No Config field.

### Oracle step 2 results (`./build/tests/oracle/og_gs_format_oracle tests/oracle/og_tables.bin F3F4` / `CLUT`)

**F3 (transfer write side, `gs_state.cpp WriteHWReg`)** — 16 rect cases:

| PSM | Cases | Result |
|---|---|---|
| PSMCT32 | one page; 16×16 at 0x3300; 16×16 CLUT rect at cbp; 1-row odd segment (per-qword role); 512×448 fbw 8; rect past the 4 MB | **IDENTICAL** ×6 |
| PSMCT24 | 512×448; block-straddling window at base block 9 | **IDENTICAL** ×2 |
| PSMCT16 | 640×224 at 0x2f0000; 2-row window; last page | **IDENTICAL** ×3 |
| PSMCT16S | 640×64 at 0x2f0000 | **DEVIATION(bytes=61,185, first addr=0x2f0400 NEW=36 OLD=12)** — by design, §7.1 **P2-CT16S** (OLD aliases to the CT16 helper) |
| PSMT8 | 16×16 tile; 8×8 L1; 256×256 atlas tbw 4; 1-row segment crossing a page | **IDENTICAL** ×4 |
| PSMT4 | — | **NO-WRITE**: OLD computes the address but has no `0x14` store case in either loop → §7.1 **P2-T4-HWREG** (new finding) |

**F4 (RT encode/sync, `EncodeRTToVRAM` / `SyncZToVRAM`)** — 15 rect cases: PSMCT32 ×6, PSMCT24 ×2, PSMCT16 ×3,
PSMZ32, PSMZ24, PSMZ16, PSMZ16S (512×448 at zbp 0x1000, fbw 8) — **IDENTICAL** ×15. (F4 covers no CT16S / indexed mode.)

**CSM1 (the three copies + the variant)**:

| Comparison | Result |
|---|---|
| NEW `Csm1Index` vs `texture_decoder.cpp:944 CSM1Swizzle` (lifted) | **IDENTICAL** |
| NEW vs `opengl_backend.cpp:21667 R164Csm1Swizzle` (lifted) | **IDENTICAL** |
| NEW vs `s9_composite.h:322 Csm1Swizzle` (lifted) | **IDENTICAL** |
| NEW vs `gs_deswizzle.h:140 csm1_unswizzle` (read-only include, row-order form) | **IDENTICAL** |
| OLD copies vs each other | **AGREE** (0 disagreements) |
| NEW `ReadClut32` CSM1/CT32 vs `ReadCLUT32 :1146` (grid walk through `GetAddressPSMCT32(x,y,cbp,64)`), 4 cbp × 256 | **IDENTICAL** |
| NEW `ReadClut16` CSM1/CT16 vs `ReadCLUT16 :1217` (linear `cbp + Csm1(i)·2`) | **DEVIATION(count=896 of 1024, first entry 1; IDTEX4 56 of 64)** → §7.1 **P2-CLUT16-CSM1** (rule 2b: manual stores the rectangle through the PSMCT16 layout; PCSX2 `clutTableT16I4` confirms) |
| NEW `Csm1Position` vs OLD `test_page_invariance.cpp:118` authoring formula | **IDENTICAL** |

Accepted-difference candidates added this stage: EQUIVALENCE_PLAN §7.1 **P2-BASE-8.5** (assumption note),
**P2-CLUT16-CSM1**, **P2-CT24-HWREG-A** (OLD F3 stores 0x80 in the CT24 upper byte and unpacks 32 bpp; F4 preserves it —
OLD copies disagree, rule 2a; NEW = F4 = manual), **P2-T4-HWREG**.

### Decisions taken in 2B

1. **Linear side is pixel-typed** (index width for indexed modes): the transfer engine hands `Swizzle` unpacked pixels
   (24→32 for CT24, nibbles for T4) and the decoder receives indices for T8H/T4HL/T4HH — the same primitive serves all
   four OLD roles; pack/unpack of the IMAGE stream is 2C `PixelConvert` / Phase 6.
2. **Write masks are the mode's pixel bits ∧ the caller's mask** with read-modify-write: CT24/Z24 preserve byte 3 (OLD F4,
   PCSX2 `WritePixel24`), T8H/T4HL/T4HH write only the index field (the §2.6.3 co-page use, pinned by the T8H-over-CT24 test).
3. **CSM1 CT16 CLUT = the rectangle in the CT16 layout** (manual, rule 2b), CSM2 = linear (plan's scope cut, unchanged).
4. **Bijection proof shape**: round trip + footprint bit count on in-width rects only (a rect wider than the buffer aliases
   the next page row on hardware too, §8.2 — not a defect).

### Gate status after 2B

`cmake --build --preset macos-dev -j4` → 0 warnings; `ctest --preset macos-dev` →
**`100% tests passed, 0 tests failed out of 39`** (34 from 2A + `Gsformat_test_swizzle` 2.05 s, `Gsformat_test_clut_layout`,
`OgGsFormatOracle_F3F4`, `OgGsFormatOracle_CLUT`, `OgPageInvariance`). `cmake --build --preset macos-diag -j4` → 0 warnings;
`ctest --preset macos-diag` → **`100% tests passed, 0 tests failed out of 40`** (+ `CoreBenchReportFormat`).

Process note: one helper edit was run through `python3` stdin instead of a file under `/tmp/p2b/` (STAGE_RULES rule 2);
the later helper (`/tmp/p2b/add_row.py`) follows the rule. No OLD file was touched; no commits.

### HANDOFF (2B → 2C)

**Done:** the six review items (docs, grid text, §8.5 note, hoisted descriptor); `swizzle.{h,cpp}`, `clut_layout.{h,cpp}`;
`tests/gs/` suite with the fast==generic / bijection / involution property tests; oracle step 2 (F3, F4, CSM1 ×3 + variant,
ReadCLUT32/16) with the results above; `og_page_invariance` port; §7.1 rows P2-BASE-8.5, P2-CLUT16-CSM1, P2-CT24-HWREG-A,
P2-T4-HWREG; tree green (dev 39/39; diag built and tested once).

**Not done (2B scope, carried):** none of the planned 2B items is open. Carried observations for 2C/Phase 6: the OLD F3
CT24 `ppw = 2` (32-bit stream unpack) and `0x80` upper byte are `PixelConvert` / transfer-engine facts (P2-CT24-HWREG-A);
the CSM2 hardware rule (row through the CT16 layout at the TEXCLUT offset) stays a documented scope cut.

**Next command:**
```
cd <repo> && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev
./build/tests/oracle/og_gs_format_oracle tests/oracle/og_tables.bin F3F4     # F3/F4 table above
./build/tests/oracle/og_gs_format_oracle tests/oracle/og_tables.bin CLUT     # CSM1 x3 + ReadCLUT table above
```
Then 2C per PHASE2_PLAN §6: `TexaExpand` (`ApplyTexa24`, `Expand16To32` bit-replicate + MSB alpha, `Texa16` written but
not wired — §7.1 CT16-TEXA candidate row) + `PixelConvert` (pack/unpack per PSM, `ScaleAlphaPs2ToGpu` + inverse, 5-bit
truncation convention) + property tests (replicate round trip, pack∘unpack identity per PSM) in `tests/gs/format/`.

## 2C — `TexaExpand` + `PixelConvert` + p2br review items (stage p2c, 2026-09-26)

### Part 1 — the four p2br review items (done first)

| # | Item | Where |
|---|---|---|
| 1 | **§7.1 row P2-Z-L2L**: OLD `PerformLocalToLocalTransfer` routes Z32/Z24 through the CT32 helper (`gs_state.cpp:4539/:4592`) and Z16/Z16S through CT16 (`:4543/:4595`) while F4 `SyncZToVRAM` uses the Z tables (rule 2a); `:4504` census: the game never issues TRXDIR=2. Oracle `RunRectFamily('L')` added (the lifted local→local `writePixel` dispatch) with the four Z-mode 512×448 cases so the deviation is **counted**: Z32 913,978 B / Z24 685,470 B / Z16 457,004 B / Z16S 342,763 B (+ CT16S as P2-CT16S). Runs under `OgGsFormatOracle_F3F4` | `EQUIVALENCE_PLAN.md` §7.1, `og_gs_format_oracle.cpp` |
| 2 | `engine/gs/format/README.md` scope-cuts list now names every §7.1 row from this directory (added P2-T4-HWREG, P2-BASE-8.5 as the shared assumption, and the 2C rows P2-Z-L2L, P2-CT16-TEXA, P2-EXP16-REPL) | README |
| 3 | `og_` prefix rule extended in `tests/oracle/README.md`: the prefix marks a TU whose content derives from the OLD tree — read-only include, lifted model/fixture, **or a port of an OLD test** (`og_page_invariance.cpp`, no OLD include); lint exemptions key on the prefix, a port needs none. File not renamed | tests/oracle/README.md |
| 4 | `test_clut_layout.cpp`: the CSM1 **CT16 IDTEX8 order pinned explicitly** — the formula (grid `(gx,gy)` = `Csm1Index(i)` → block `gy/8`, column `(gy%8)/2`, word `0,1,4,5,8,9,12,13` / `2,3,6,7,10,11,14,15` by row parity, `gx ≥ 8` takes the high halfword) for all 256 entries + a literal 32-entry table (`0,2,8,10,16,18,24,26, 4,6,12,14,20,22,28,30, 1,3,9,11,17,19,25,27, 5,7,13,15,21,23,29,31`) + spot entries 32/64/96/128/255 + IDTEX4 = the prefix. 4,240 checks now | tests/gs/format |

### Part 2 — files landed

| File | Content | OLD source read (read-only) |
|---|---|---|
| `engine/gs/format/texa_expand.{h,cpp}` | `TexaExpand`: `DecodeTexaRegister` (TA0 7:0, AEM 15, TA1 39:32), `Replicate5To8` / `Truncate8To5`, `Expand16To32` (replicated RGB + A bit as 0xFF/0x00 — the TEXA-less frame-readback form; = OLD `Expand16to32`), `Truncate32To16` (A bit = alpha ≥ 0x80: `PS2_GS_ABIT16` winning arm), `Alpha16` / `Alpha24` (the §3.4.6 table cell by cell), `ApplyTexa16` / `ApplyTexa24`. Header cites GS_SKILL §3.4.6 + TEXA register, BUG-020 / A3 §2 for the replicate adjudication, and the P2-CT16-TEXA / P2-EXP16-REPL rows | `texture_decoder.cpp:502-553` (CT24 TA0/AEM), `:740-780` → `:1946 Expand16to32`, `opengl_backend.cpp:1395/:1620/:2157-2197`, `s9_shadow.h:54/:102/:138 Abit16Alpha` |
| `engine/gs/format/pixel_convert.{h,cpp}` | `PixelConvert`: `StorageBitsPerPixel` / `BytesPerPixel` (via the `PsmLayout` descriptor; T4 excluded — no whole byte) / `StreamBitsPerPixel` (CT24/Z24 = 24: the manual's IMAGE packing; OLD's 32-bpp unpack + `0x80` byte documented as P2-CT24-HWREG-A, not written), `IsColor/IsZ/IsIndexed`, `ElementPixelMask`, `UnpackColor/PackColor` (CT32 raw; CT24 pixel bits only, alpha 0 → TEXA's job; CT16/CT16S through `TexaExpand`), **`Depth` as a distinct type** with `UnpackZ/PackZ` (Z24 keeps 24 bits, Z16/Z16S 16) and `DepthFromColorWord` (`PS2_GS_ZSRC` value half), `UnpackIndex/PackIndex` (RMW: T8H/T4HL/T4HH keep the co-paged colour), `ScaleAlphaPs2ToGpu` (= OLD `a ≥ 128 ? 255 : a·2`) / `ScaleAlphaGpuToPs2` (= OLD `a ≥ 255 ? 128 : a/2`), RGBA32 field access | `texture_decoder.h:263`, `opengl_backend.cpp:15992/:1676/:15207`, `gs_state.cpp:4731 GetPixelSize`, `:5066/:5332` ppw tables, `:5148/:5424` (0x80 byte), `s9_shadow.h:62` |
| `engine/gs/CMakeLists.txt` | `ps2r_gs_format` += `texa_expand.cpp`, `pixel_convert.cpp` | — |
| `tests/gs/format/test_texa_expand.cpp` (`Gsformat_test_texa_expand`) | replicate∘truncate = id on every 8-bit value's lane and truncate∘replicate = id on all 32 lanes; replicate ≠ manual shift on 28/32 lanes (the deviation visible); `Expand16To32` over all 65,536 halfwords with channel placement and `Truncate32To16` round trip; A-bit convention (0x80 sets, 0x7F does not); quantize idempotence; register decode; the **§3.4.6 truth table** (8 RGBA16 cells + 4 RGB24 cells) and its composition into `ApplyTexa16/24` (black = all 15 colour bits clear; bits 31:24 of the CT24 input ignored; TA > 0x80 passes). 657 checks | — |
| `tests/gs/format/test_pixel_convert.cpp` (`Gsformat_test_pixel_convert`) | widths/classes/masks for all 13 PSMs vs the descriptor (popcount of the pixel mask = pixel bits); colour pack∘unpack identity per mode (200 k random words + all 65,536 halfwords; CT16S ≡ CT16 values); Z identity on the Z width (Z24 byte 3 never written); index RMW identity for the five indexed modes with the non-index bits preserved; alpha vectors == OLD for all 256 bytes both ways, PS2→GPU→PS2 identity on 0..128 and saturation above. 907 checks | — |
| `tests/oracle/og_gs_format_oracle.cpp` (+ `CMakeLists.txt` case `TEXA`) | `RunTexa`: the OLD value leaves lifted (each cited): `Expand16to32`, `DecodePSMCT24` alpha, `ScaleAlphaPS2ToGPU`, `EncodeRTToVRAM` inverse, `GetPixelSize`, pixels-per-write; results below. `RunRectFamily('L')` for item 1 | as cited in the TU |

Lints: `LintOneClass` 90 files 0 findings (one `[WN4-CT16Q]` tag in a header comment was caught on the first run and
replaced by the BUG-020 file citation), `LintEnginePurity` 85 files 0 tokens, `CoreSingleGetenvSite` = 1. Flags consumed:
`PS2_GS_CT16_QUANT` (NORM: 5-bit truncation convention), `PS2_GS_ABIT16` (NORM: A bit ⇄ 0xFF/0x00 value convention;
the CT16S-only frame scope is Phase 6), `PS2_GS_ZSRC` (NORM: `DepthFromColorWord`; route predicate Phase 6). No Config field.

### Oracle results (`./build/tests/oracle/og_gs_format_oracle tests/oracle/og_tables.bin TEXA`)

| Comparison | Result |
|---|---|
| `Expand16To32` vs `texture_decoder.cpp:1946 Expand16to32` (65,536) | **IDENTICAL** |
| `Expand16To32` vs the manual's shift `in5 << 3` (GS_SKILL §3.4.6) | **DEVIATION(count=65,408 of 65,536)** — adjudicated replicate (BUG-020 / A3 §2); OLD deviates identically → §7.1 **P2-EXP16-REPL** (recorded adjudication) |
| `ApplyTexa24` vs `DecodePSMCT24` alpha rule (`:540-546`; 6,656 cases: 4³ RGB × 52 TA0 × AEM) | **IDENTICAL** |
| `ApplyTexa16` vs OLD's CT16 texel (`Expand16to32`, TEXA omitted) at TA0=0x00/TA1=0x80/AEM=0 | **DEVIATION(count=32,768 of 65,536 — every A=1 texel: 0x80 vs 0xFF; RGB differs in 0)** — pre-registered rule 2b → §7.1 **P2-CT16-TEXA** (vanishes after `ScaleAlphaPs2ToGpu` at the common setup; Phase 6 arbitrates) |
| `ScaleAlphaPs2ToGpu` vs `texture_decoder.h:263` (256) | **IDENTICAL** |
| `ScaleAlphaGpuToPs2` vs `opengl_backend.cpp:15992` (256) | **IDENTICAL** |
| `BytesPerPixel` vs `gs_state.cpp:4731 GetPixelSize` (byte-or-wider modes) | **IDENTICAL** (T4: OLD rounds to 1 byte; NEW exposes 4 bits, no byte width) |
| `64 / StreamBitsPerPixel` vs `gs_state.cpp:5066/:5332` pixels-per-write | IDENTICAL-BY-DIVISION (CT24: OLD unpacks the stream at 32 bpp → **P2-CT24-HWREG-A**; NEW `StreamBitsPerPixel(CT24)` = 24) |

`F3F4` family `FL` (item 1): PSMZ32 **DEVIATION(bytes=913,978, first 0x100000)**, PSMZ24 **685,470**, PSMZ16 **457,004**,
PSMZ16S **342,763**, PSMCT16S 61,185 (P2-CT16S) → §7.1 **P2-Z-L2L** (candidate, expected no-op: TRXDIR=2 never issued).

### Decisions taken in 2C

1. **TEXA applies to CT16 texels in NEW** (manual §3.4.6; p2br revision of the plan row). `Expand16To32` stays the
   TEXA-less form for frame readback / FBMSK merge; `ApplyTexa16` is the texture rule. No OLD-compatible arm.
2. **Replicate, not the manual's shift**, for 5→8 (BUG-020 adjudication) — recorded as a shared deviation (P2-EXP16-REPL)
   rather than silently; zero fill is not written in the format layer (OLD's `:2175` blender-Cd zero fill noted for Phase 6).
3. **`Depth` is a type**: Z helpers cannot take colour words and colour helpers assert on Z modes (debug); the
   colour-authored-Z route is expressed as `DepthFromColorWord`.
4. **`BytesPerPixel` excludes T4** (assert) instead of OLD's rounded 1; `StorageBitsPerPixel` is the primary width;
   `StreamBitsPerPixel(CT24/Z24) = 24` is the manual's IMAGE packing — the transfer engine (Phase 6) consumes it.
5. Alpha values stay in the PS2 domain through `TexaExpand`; the one GPU scaling site is `ScaleAlphaPs2ToGpu`.

### Gate status after 2C

`cmake --build --preset macos-dev -j4` → 0 warnings; `ctest --preset macos-dev` →
**`100% tests passed, 0 tests failed out of 42`** (39 from 2B + `Gsformat_test_texa_expand`, `Gsformat_test_pixel_convert`,
`OgGsFormatOracle_TEXA`). `macos-diag` not rebuilt this stage (no diag code touched; 2D's gate row runs it).
Process: helper edits via files under `/tmp/p2c/`; every build/ctest bounded; no OLD file touched; no commits.

### HANDOFF (2C → 2D)

**Done:** the four p2br items (P2-Z-L2L counted, README scope list, `og_` rule, CT16 IDTEX8 pin); `texa_expand.{h,cpp}`,
`pixel_convert.{h,cpp}`; property tests (657 + 907 checks); oracle `TEXA` case + `FL` family; §7.1 rows P2-Z-L2L,
P2-CT16-TEXA, P2-EXP16-REPL; PHASE2_PLAN §2/§6 revised for the TEXA-16 decision; tree green (dev 42/42).

**Not done (carried to 2D / Phase 6):** `macos-diag` rebuild (2D gate row); the CT24 24-bit IMAGE-stream unpack itself
(transfer engine, Phase 6 — `StreamBitsPerPixel` is its input); dither (Phase 6); OLD `opengl_backend.cpp:2175` zero-fill
blender-Cd convention to be re-examined when the render path lands.

**Next command:**
```
cd <repo> && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev
./build/tests/oracle/og_gs_format_oracle tests/oracle/og_tables.bin TEXA     # table above
./build/tests/oracle/og_gs_format_oracle tests/oracle/og_tables.bin F3F4     # F3 / F4 / FL (Z local->local counts)
```
Then 2D per PHASE2_PLAN §6: `BlockDecode` (per-PSM block → RGBA8 through `PixelConvert`/`TexaExpand`/`ClutLayout`, LUT
generated from `PsmLayout`, `PS2R_SIMD` NEON pass identical) + `MipChain` (MIPTBP1/2 → per-level `(tbp, tbw)`, sizes,
`MaxLevels`) + port of OLD `tests/test_block_decode_equivalence.cpp` (bounded OLD run for the case list) + DoD grep +
README flag rows complete (9 remaining) + `macos-diag` build/ctest + PHASE2_IMPL §2D gate table + HANDOFF to review 2R.

## 2D — `BlockDecode` + `MipChain` + block-decode oracle port + DoD + gate (stage p2d, 2026-09-26)

### Files landed

| File | Content | OLD source read (read-only) |
|---|---|---|
| `engine/gs/format/block_decode.{h,cpp}` | `BlockDecode`: `Texture{psm,tbp,tbw,w,h}` / `Palette{cpsm,cbp,csm2}` / `Rect` in manual units; **page LUT GENERATED at first use** (`PageLut(psm)` = `PsmLayout::ElementInPage` over page_w×page_h, `std::call_once` per mode, 16-bit entries — never a typed table); `Decode` (page-column walk: runs inside one page, element fetch by storage width, 4 MB wrap by conditional reduce), `DecodeScalar` (always compiled), `DecodeGeneric` (per-pixel `PixelAddress` reference), `TexelValue` (CT32 raw; CT24 `ApplyTexa24`; CT16/CT16S `ApplyTexa16`; indexed → palette; Z32 word, Z24 low 24, Z16/Z16S `Expand16To32` TEXA-less frame form = OLD), `ReadPalette` (`ClutLayout` reads; CT16 entries through `ApplyTexa16`), `PaletteEntries`, `DecodeFootprint` / `DecodeRowBand` (§8.2 page arithmetic in bytes). NEON pass for the 16-bit expansion behind `PS2R_SIMD` (`SimdEnabled()`) | `texture_decoder.cpp` `DecodePSMCT32 :365`, `Z32 :460`, `CT24 :499`, `CT16 :723`, `Z16/Z16S :794`, `T8 :1252`, `T4 :1764`, `T8H :1863`; `r121blk` `PageLUTs :2293-2346`, `PageWalk :2378`, decoders `:2446-3006`; `ComputeDecodeFootprint :1039`, `ComputeDecodeRowBand :1088` |
| `engine/gs/format/mip_chain.{h,cpp}` | `MipChain`: `Mxl`/`Mtba` (TEX1 4:2 / 9), `PairTbp`/`PairTbw` (MIPTBP {TBP 14, TBW 6} at 20·slot; TBW 0 → 1 per register note *2), `LevelSize` (max(1, base ≫ l)), `LevelTbp`/`LevelTbw` (levels 1–3 MIPTBP1, 4–6 MIPTBP2), `Build` (chain 0..MXL, MXL clamped to 6, MTBA reported not applied), `PackMiptbp1/2` inverse. Header cites GS_SKILL §3.4.11 / MIPTBP register tables / TEX1 §3.4.3; LOD/K/L/MMIN policy stated as Phase 6 | `gs_mipmap.h:158 MiptbpPair`, `:213-228` level loop; flag sites `:64/:126/:128/:187/:278/:326` read for the §3 outcomes |
| root `CMakeLists.txt`, `engine/gs/CMakeLists.txt`, `CMakePresets.json` | `option(PS2R_SIMD … OFF)` → `PS2R_SIMD=1` on `ps2r_gs_format`; **`macos-diag` preset sets `PS2R_SIMD=ON`** so the SIMD == scalar property runs with the vector path live (dev preset stays scalar) | — |
| `tests/gs/format/test_block_decode.cpp` (`Gsformat_test_block_decode`) | LUT == `ElementInPage` entry by entry + permutation of the page (13 PSMs); **LUT path == per-pixel reference == `Decode`** over 10 shapes × 13 PSMs + 12 random rects per PSM (7,330,624 texels × 3 paths; sub-page, page-straddling, wider-than-buffer, tbw 0, block-boundary base, 4 MB wrap); whole-texture overload; `TexelValue` rules per mode; `ReadPalette` = `ClutLayout` + TEXA-16; footprint == brute-force max page over `PixelByteAddress` (7 dims × 13 PSMs), row band, 4 MB clamp, degenerate sizes. **970 checks**; prints `SIMD ON/OFF` | — |
| `tests/gs/format/test_mip_chain.cpp` (`Gsformat_test_mip_chain`) | halving to 1 (bases 2^0..2^10 × levels 0..6), field positions, TBW 0 → 1, MXL/MTBA decode, **the manual's §3.4.11 example** (512×512 CT16 at 7200: 9248/4, 9760/2, 9888/1, 9920/1, 9928/1, 9930/1; sizes 256…8), MXL 7 → 6 clamp, non-square floors, pack/unpack round trip (2,000 random register pairs). 186 checks | — |
| `tests/oracle/og_block_decode.cpp` (`OgBlockDecode`, TIMEOUT 120) | **port of OLD `tests/test_block_decode_equivalence.cpp`** as a differential oracle: the original's xorshift64* stream (seed `0x9E3779B97F4A7C15`), VRAM fill, `RandomCase`/`RandomRect` call-for-call, 29 edge cases and 14 targeted rects transcribed → **843 cases** (400 random + 29 edge + 400 random rect + 14 targeted rect; the 12 key-fold assertions are texcache facts, Phase 6). OLD leaves LIFTED (cited) as a per-pixel model factored into steps **A** (OLD F1 address + OLD values + OLD palette = the original's output), **B** (NEW address), **C** (+ NEW palette order), **D** (NEW `BlockDecode`); A/B, B/C, C/D differences attributed byte-by-byte to §7.1 rows; anything unattributed FAILS. Dropped harness mechanics: `vram_base` window offset, Z16 `zsize` guard, byte-granular `tbp` (snapped to the 256-byte grid, count printed) | `tests/test_block_decode_equivalence.cpp` (case list, RNG, edges); `texture_decoder.cpp` leaves above; `:944 CSM1Swizzle`, `:1146 ReadCLUT32`, `:1217 ReadCLUT16`, `:1946 Expand16to32`, `:1922` (T8H second reorder) |
| `engine/gs/format/README.md` | class table complete (2D rows), decode-leaf + mip provenance, **all 14 flag rows** with outcome/stage/what-is-written, §7.1 list += P2-T8H-CSM1, P2-CLUT32-I4-CSM2 | — |
| `docs/design/EQUIVALENCE_PLAN.md` §7.1 | rows **P2-T8H-CSM1** (rule 2a: OLD's T8H leaf reorders the CSM1 palette twice = net none; T8 leaf once) and **P2-CLUT32-I4-CSM2** (manual-undefined CT32/CSM2 16-entry CLUT: OLD 16×1 row, NEW 8×2 rectangle) | — |

Lints: `LintOneClass`, `LintEnginePurity`, `CoreSingleGetenvSite` (= 1) pass. Flags consumed this stage (README table): `PS2_GL_FAST`, `PS2_GL_TEXCACHE`, `PS2_GS_MIPMAP`, `PS2_GS_MIP_ALLCLASS`, `PS2_GS_MIP_TRILINEAR`, `PS2_GS_RTKEEP` (NORM — winning arm / nothing to write), `PS2_GS_READER_CENSUS`, `PS2_MIPCEN` (DIAG — no census code here), `PS2_GS_MIP_LODREF` (DELETE). All 14 rows of PHASE2_PLAN §3 are now applied. No Config field.

### OLD binary re-run (bounded, for the case count)

`verify/run_bounded.sh 300 … OLD/build/tests/test_block_decode_equivalence` → exit 0:
`[block-decode-equivalence] 855/855 cases byte-identical` = 400 random + 29 edge + `[R159 rect-crop-equivalence] 414/414` + 12 key-fold (the OLD harness is a self-equivalence test — legacy loop vs its LUT path; NEW's port compares against NEW).

### Block-decode oracle (`./build/tests/oracle/og_block_decode tests/oracle/og_tables.bin`, 3.6 s)

```
og_block_decode: 843 cases replayed from the original's list (400 random + 29 edge + 400 random rect + 14 targeted rect), 18986567 texels
  tbp snapped to the 256-byte block grid: 599 cases
  per PSM: PSMCT16=123 PSMCT24=110 PSMCT32=110 PSMT4=123 PSMT8=127 PSMT8H=95 PSMZ16=53 PSMZ16S=49 PSMZ32=53
  IDENTICAL to the original: 457 cases
  DEVIATING, every byte attributed to a registered 7.1 row: 386 cases
    P2-T4              exposed 123 cases, deviating 123 cases,    1851071 texels
    P2-CT16S           exposed   0 cases, deviating   0 cases,          0 texels (no CT16S case in the original's pool)
    P2-CLUT16-CSM1     exposed  59 cases, deviating  59 cases,     573302 texels
    P2-CLUT32-I4-CSM2  exposed  30 cases, deviating  30 cases,     169758 texels
    P2-T8H-CSM1        exposed  55 cases, deviating  55 cases,    1023849 texels
    P2-CT16-TEXA       exposed 304 cases, deviating 304 cases,    2739510 texels
    P2-EXP16-REPL      both trees replicate 5->8: 0 texels differ by it
  UNATTRIBUTED: 0 cases
og_block_decode: PASS
```

Reading: every case whose modes are exposed to a row deviates by exactly that row and nothing else (exposed == deviating for
all six rows; 0 unattributed bytes). The deviation is confirmed to be exactly the registered one because each row is checked
in isolation: **P2-T4** = A vs B (only the address changed, OLD values/palette held) on every PSMT4 case; **P2-CLUT16-CSM1** /
**P2-CLUT32-I4-CSM2** / **P2-T8H-CSM1** = B vs C (only the palette order changed); **P2-CT16-TEXA** = C vs D where every differing
texel has identical RGB and alpha `0xFF → 0x80` (TA1 at the common setup TA0=0x00/TA1=0x80/AEM=0; any other byte would fail).
**P2-CT16S**: the OLD pool has no CT16S case (its address deviation is counted by `OgGsFormatOracle_PSMCT16S` / `_F3F4`).
**P2-EXP16-REPL**: shared, 0 texels. CT24 (TA0/AEM), CT32, Z32, Z16, Z16S, T8 with CT32 palettes, CSM2 CT16 palettes: IDENTICAL.
Two rows were discovered by the port and registered (P2-T8H-CSM1, P2-CLUT32-I4-CSM2) — neither is hidden.

### Definition-of-done grep

```
$ grep -rnE 'GetAddressPSM|swizzle|Swizzle\(' engine app games --include=*.cpp --include=*.h --include=*.inl | grep -v '^engine/gs/format/' | wc -l
0
```
(no second address formula exists outside `engine/gs/format`; the tests reference `Swizzle` only as a consumer).

### Gate table (PHASE2_PLAN §5) — evidence

| Gate row | Evidence |
|---|---|
| `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` 100 %, 0 warnings | build: 0 warnings (`-Wall -Wextra -Werror` on every gs/format TU and test); **`100% tests passed, 0 tests failed out of 45`** (42 from 2C + `Gsformat_test_block_decode` 0.7 s, `Gsformat_test_mip_chain`, `OgBlockDecode` 3.6 s) |
| `macos-diag` builds and passes | reconfigured (`cmake --preset macos-diag`: `PS2R_DIAGNOSTICS=ON, PS2R_SIMD=ON`), build 0 warnings; **`100% tests passed, 0 tests failed out of 46`**; `test_block_decode: SIMD ON (Decode uses the NEON 16-bit expansion)` … `PASS (970/970 checks passed)` — the SIMD == scalar proof |
| `LintOneClass` / `LintEnginePurity` 0 findings; `CoreSingleGetenvSite` = 1 | all three `Passed` in both presets |
| OG address oracle: 13 PSMs IDENTICAL except the rule-2b rows | unchanged from 2A/2B/2C: `OgGsFormatOracle_*` 16 cases pass; CT16S / T4 DEVIATION counts as recorded in §2A; F2 T8 (P2-T8-F2), F3 CT16S, FL Z (P2-Z-L2L) as recorded |
| Ported `test_block_decode_equivalence` + `test_page_invariance` pass; OLD binaries re-run bounded once | `OgBlockDecode` PASS (843 cases; OLD count 855 = 843 + 12 key-fold); `OgPageInvariance` PASS (2B); OLD `test_block_decode_equivalence` re-run: `855/855` |
| Property tests | bijection (2A), fast == generic (2B), involution (2B), replicate round trip / pack∘unpack (2C), **LUT-vs-direct identity + SIMD == scalar (970 checks) + mip halving (186 checks)** (2D) — all PASS |
| DoD grep = 0 outside `engine/gs/format` | 0 (quoted above) |
| README: four families + line ranges + 14 flag rows | present: F1–F4 table, decode-leaf/mip/CLUT provenance, 14 flag rows, 12 §7.1 rows |
| Accepted differences recorded in §7.1 | P2-CT16S, P2-T4, P2-T8-F2, P2-BASE-8.5, P2-CLUT16-CSM1, P2-CT24-HWREG-A, P2-T4-HWREG, P2-Z-L2L, P2-CT16-TEXA, P2-EXP16-REPL, **P2-T8H-CSM1, P2-CLUT32-I4-CSM2** (12 rows, all candidates; Phase 6 frame lane arbitrates) |
| No harness lane claimed | none: Phase 2 has no runtime consumer; nothing game-visible changed |

### Phase 2 gate: PASSED

All §5 rows have quoted evidence; both presets build warning-free and pass 100 %; every OLD deviation is a registered §7.1
candidate with its witness/count; no address formula exists outside `engine/gs/format`. Review stage 2R may re-run the table.

### Decisions taken in 2D

1. **Z reads keep the TEXA-less frame form** (`Expand16To32` for Z16/Z16S; Z24 = low 24 bits, alpha 0): a Z buffer read
   back as colour is a buffer readback, not texture sampling — equals OLD (IDENTICAL in the oracle). If Phase 6 needs a
   Z16 texture sampled with TEXA it is a one-line `TexelValue` change plus a §7.1 row.
2. **T8H uses the single CSM1 reorder** (= T8 = manual); OLD's double reorder is P2-T8H-CSM1, not carried.
3. **`vram_base` / `zsize` are not decode rules**: `BlockDecode` reads the 4 MB view; snapshot windows belong to the Phase-6
   texture source (`DecodeFootprint` / `DecodeRowBand` are its inputs, and the footprint test proves them against brute force).
4. **`PS2R_SIMD` lives on the diag preset** so the vector path is exercised by ctest without changing the dev/release shape;
   the NEON pass covers the 16-bit expansion (the leaf OLD measured as the expensive one); other modes run the scalar loop.
5. The original harness's byte-granular `tbp` cannot be expressed by TBP0 (256-byte units); the port snaps (599 of 843 cases)
   rather than dropping them — the address rule under test is unchanged, only the base grid is the hardware's.

### HANDOFF (2D → 2R review, then Phase 3)

**Done:** `block_decode.{h,cpp}`, `mip_chain.{h,cpp}`, `PS2R_SIMD` option (+ diag preset), `test_block_decode` (970),
`test_mip_chain` (186), `og_block_decode` (843 cases, 0 unattributed), README complete (14 flags, families, §7.1 list),
§7.1 rows P2-T8H-CSM1 / P2-CLUT32-I4-CSM2, DoD grep 0, both presets green (dev 45/45, diag 46/46), gate PASSED.

**Not done / carried:** nothing from the 2D scope is open. Carried to Phase 6: the P2-* arbitration (frame lane), MTBA
automatic bases (`MipChain` reports the flag), CSM2 TEXCLUT offsets (scope cut), snapshot-window semantics.
Process notes: one helper was run through `python3` with an inline script file under `/tmp/p2d/` (rule 2 kept); the OLD
binary was run once, bounded, from `/tmp/p2d`; no OLD file touched; no commits.

**Next command (2R re-runs the gate table):**
```
cd <repo> && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev
cmake --preset macos-diag && cmake --build --preset macos-diag -j4 && ctest --preset macos-diag
./build/tests/oracle/og_block_decode tests/oracle/og_tables.bin      # the attribution table above
./build-diag/tests/gs/test_block_decode                              # "SIMD ON" + PASS
grep -rnE 'GetAddressPSM|swizzle|Swizzle\(' engine app games --include=*.cpp --include=*.h --include=*.inl | grep -v '^engine/gs/format/'
```
Then **Phase 3**: the next stage writes `docs/phases/PHASE3_PLAN.md` from `docs/design/CONSTRUCTION_PLAN.md` §4.3 (same shape
as PHASE2_PLAN: module table with OLD sites, flag rows, oracle strategy, gate, stage breakdown) before any Phase-3 code.

### Review 2D (stage p2dr, 2026-09-26) — verdict: APPROVED (Phase 2 gate confirmed PASSED)

Method (own runs; helpers/logs under `/tmp/p2dr/` only; OLD read-only — `find OLD -type f -mmin -240` outside
`logs/ build*/ .git/` → 0 files; OLD binary run once through `verify/run_bounded.sh 120 … --`; no commits — `git log`
still "no commits yet"): both build directories moved aside and rebuilt from a fresh `cmake --preset` configure.

| Gate row | Own evidence |
|---|---|
| dev build + ctest, 0 warnings | `cmake --preset macos-dev` (`PS2R_DIAGNOSTICS=OFF, PS2R_SIMD=OFF`) → 77 build steps, `grep -c "warning:"` → **0** → `ctest --preset macos-dev` **`100% tests passed, 0 tests failed out of 45`** (35.3 s) |
| diag build + ctest | `cmake --preset macos-diag` (`PS2R_DIAGNOSTICS=ON, PS2R_SIMD=ON`) → 84 steps, warnings **0** → **`100% tests passed, 0 tests failed out of 46`** (the +1 is `CoreBenchReportFormat`, diag-only) |
| SIMD == scalar | `./build-diag/tests/gs/test_block_decode` → `SIMD ON (Decode uses the NEON 16-bit expansion)` / `identity sweep: 7330624 texels x 3 paths` / **`PASS (970/970 checks passed)`**; dev binary prints `SIMD OFF (Decode == DecodeScalar by identity)` / `PASS (970/970)`. The sweep's TEXA is `{TA0 0x33, TA1 0x80, AEM 1}` so all three NEON alpha branches (TA1 / TA0 / AEM-zero) are live |
| Lints | `LintOneClass: 96 files scanned, 0 findings -> PASS`; `LintEnginePurity: 89 files scanned, 0 game tokens -> PASS`; `CoreSingleGetenvSite` Passed (= 1) in both presets |
| OG address oracle | re-run direct: `PSMT4 F1 DEVIATION(count=15676416, first (x=1,y=0,bp=0,bw=1) NEW=0x000004 OLD=0x000000)`, `PSMCT16S F1 DEVIATION(count=5971968, first (x=32,y=0,bp=0,bw=1) NEW=0x001000 OLD=0x000800)`, `PSMT8 F1 IDENTICAL / F2 DEVIATION(count=442368 …)` — each `PASS (NEW == manual)`; = the §7.1 P2-T4 / P2-CT16S / P2-T8-F2 signatures verbatim |
| Block-decode oracle | `./build/tests/oracle/og_block_decode tests/oracle/og_tables.bin` → **843 cases, 18,986,567 texels; IDENTICAL 457; DEVIATING 386** — `P2-T4 123/123 (1,851,071)`, `P2-CT16S 0/0`, `P2-CLUT16-CSM1 59/59 (573,302)`, `P2-CLUT32-I4-CSM2 30/30 (169,758)`, `P2-T8H-CSM1 55/55 (1,023,849)`, `P2-CT16-TEXA 304/304 (2,739,510)`, `P2-EXP16-REPL 0 texels`; **UNATTRIBUTED: 0**; `PASS`. Attribution code read (`RunCase` :259-318): A/B → address rows only for T4/CT16S, B/C → palette rows only for indexed cases, C/D → accepted only when every differing texel has identical RGB and alpha exactly `0xFF → 0x80` on a 16-bit mode; every other byte prints `UNATTRIBUTED …` and fails. Counts match the §7.1 rows P2-T8H-CSM1 / P2-CLUT32-I4-CSM2 exactly |
| OLD binary re-run (bounded, once) | `[block-decode-equivalence] 855/855 cases byte-identical`, `[R159 rect-crop-equivalence] 414/414`, `PASS`, exit 0 (= 843 + 12 key-fold, as recorded) |
| LUT generated, not typed | `block_decode.cpp:144-157 PageLut`: `std::call_once` per mode, `lut[ly*page_w+lx] = PsmLayout::ElementInPage(psm, lx, ly)`; no literal table anywhere in the TU; `test_block_decode` §1 pins entry-by-entry equality + permutation + generated-once for all 13 PSMs |
| MipChain vs GS_SKILL §3.4.11 | `PairTbp` bits 13:0 / `PairTbw` bits 19:14 at `20·slot` (= MIPTBP1 table TBP1 13:0, TBW1 19:14, TBP2 33:20, TBW2 39:34, TBP3 53:40, TBW3 59:54); TBW 0 → 1 (note *2); `Mxl` = TEX1 4:2, `Mtba` = bit 9 (TEX1 table); the manual's 512×512 CT16 @ 7200 example (9248/4, 9760/2, 9888/1, 9920/1, 9928/1, 9930/1) pinned in `test_mip_chain.cpp:58-72`; **`PASS (186/186)`** |
| DoD grep | `grep -rnE 'GetAddressPSM|swizzle|Swizzle\(' engine app games --include=*.cpp --include=*.h --include=*.inl \| grep -v '^engine/gs/format/' \| wc -l` → **0**; a broader case-insensitive sweep (`columnTable|blockTable|PageLUT`, roots + `tools`) hits only `tools/flag_phase_counts.py:40` (OLD file-name strings, not a formula) |
| README | 8 class rows incl. `BlockDecode` / `MipChain` (2D); four OLD families F1–F4 with line ranges; decode-leaf / mip / CSM1 / CLUT-read provenance; **14 flag rows** = exactly PHASE2_PLAN §3's 14 names (11 NORM / 2 DIAG / 1 DELETE); §7.1 list names all 12 rows |
| §7.1 | 12 P2-* rows present in `EQUIVALENCE_PLAN.md` §7.1 (:218-229), the two 2D rows carrying the oracle's exact counts and witnesses |
| One class per file / tree hygiene | lint above; `git status --untracked-files=all` shows no stray artifacts (`*.ps2st/ppm/bin/log` → none besides `tests/oracle/og_tables.bin`); no helper processes left |

Non-blocking notes (no gate impact; fold into a later hygiene pass, not before Phase 3):

1. `tests/oracle/CMakeLists.txt` compiles `og_gs_format_oracle` with `-Wall -Wextra -O2` but without `-Werror`
   (the other four format/oracle targets have it). The fresh builds show 0 warnings either way; add `-Werror` for uniformity.
2. `og_block_decode` B-vs-C attribution is an `else-if` chain: a T8H / cpsm CT16 / csm 0 case combines the CT16 CLUT layout
   difference (P2-CLUT16-CSM1) with the double reorder (P2-T8H-CSM1) but is counted under P2-T8H-CSM1 only (`exposed[]`
   deliberately excludes T8H from CLUT16-CSM1 so exposed == deviating holds). Both rows are registered, nothing is hidden;
   only the per-row texel totals are cross-attributed for those cases. Optional: attribute to both rows when both apply.
3. The NEON AEM-zero branch is exercised by whichever random VRAM halfwords have RGB == 0 (~1/32768 per texel; ~17 expected
   per CT16 sweep, deterministic under the fixed seed). Optional: seed one known zero-RGB texel so the coverage is by construction.

Phase 2 gate stands as PASSED. Next: `docs/phases/PHASE3_PLAN.md` from `docs/design/CONSTRUCTION_PLAN.md` §4.3 (per the 2D HANDOFF).
