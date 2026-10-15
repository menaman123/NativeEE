# `tools/gsdump/`

**Phase:** 8 (P8.20-B)
**Purpose:** read the reference emulator's GS dumps (PCSX2 `GSDump`, zstd-framed `.gs.zst`) and
compare their per-draw register state with the `.gsdl` recordings of the OLD and NEW trees.

```
gsdump_census.py <dump> [--frame N] [--draws] [--screenshot out.ppm] [--json out.json]
draw_census_compare.py SRC[:frame] [SRC2[:frame] ...] [--draws] [--json out.json]
gsdl_slice.py <in.gsdl> <out.gsdl> <first> [last]
replay_diff_partition.py --frames <dir of k<idx>.ppm + groups.tsv> --new <frame.ppm> --ref <shot.ppm> [--quant5] [--json out]
gsf1_export.py <dump.gs.zst> --frame N --out <stream.gsf1> [--inspect IDX ...] [--no-carry]
<tool> --selftest
```

## Contents

- `gsdump_census.py` — the dump reader: header (serial, crc, screenshot), the GSState freeze
  (initial env + both contexts), the privileged registers (DISPFB/PMODE per `Regs` packet), the
  GIF packet walk (PACKED with A+D vertex kicks, REGLIST, IMAGE; PRMODECONT=0 → PRMODE) into an
  ordered draw list per VSYNC frame; draws grouped by state; `--screenshot` writes the embedded
  frame as P6 PPM (the reference image for `tools/frame_diff.py`).
- `draw_census_compare.py` — `.gsdl` (v1/v2, with the HLE handler names) and dump sources reduced
  to the same address-free blend-state key; per-source group tables and the N-way present/missing
  table (draws/kicks per source).
- `gsdl_slice.py` — streams a frame range (and every name record) out of a large recording.
- `replay_diff_partition.py` — the last-writer map of a replayed dump frame (one `test_title_frame_replay
  <gsf1> --only 0-<idx> --dump` grab per draw, folded: a pixel's writer is the last draw whose frame changed
  it) applied to a NEW-vs-screenshot difference: per writer draw / per address-free class the SUB-STEP and
  OBJECT-level pixel counts, coverage, example pixels; `--quant5` compares 5-bit (PSMCT16) values. Needs numpy.
- `gsf1_export.py` — one dump frame as a `GSF1` replay stream for `tests/render/test_title_frame_replay`: every
  host→local IMAGE upload with its BITBLTBUF/TRXPOS/TRXREG fields and every draw (the census's grouping) with its
  context registers and the vertices assembled into a triangle list by the GS primitive rules. Vertex kicks: XYZ2/XYZF2
  draw; XYZ3/XYZF3 and ADC-set vertices QUEUE without drawing (GS User's Manual 7.3.1.5-8) — in PACKED, REGLIST and
  through A+D alike — and belong to the draw the next drawing kick opens. EVERY PRIM write initialises the vertex
  queue (GS User's Manual, PRIM), inside an open draw too: the strip/fan window restarts at the next vertex (the
  title sends one PRIM per trifan — the fans must not bridge through the previous centre). The 64-bit XYZF forms
  carry Z in bits 32-55 (F in 56-63 is not a vertex attribute of the record). A sprite's two kicks are normalised to
  TL/BR with EVERY texture coordinate (s/t AND u/v) following its corner. `--inspect IDX` prints a draw's kicks
  (K/q, `|` = restart) and bbox; the summary line names `EXPORTER_VERSION` (a stream's provenance = dump name +
  version + date). `tests/render/attract_replay_pin.py` runs it at test time on the five `coldboot_attract` dumps
  (`Render_AttractReplay_<dump>`): the stream is a derived artefact, the dump is the frozen input.

## Rules

- The first exporter (stage P8.21-A, `docs/phases/PHASE8_IMPL.md`, 2026-10-04) dropped vertices queued before a draw's
  first drawing kick (the A+D `XYZ3` letterbox strips of the cutscene became zero-width), left an fst sprite's TR/BL
  corners with the other corner's u/v (the replay's text sprites and the ×2 sky-layer copy came out wrong where the
  live engine is right) and kept the strip/fan window across a mid-draw PRIM write (bridging triangles between the
  title's trifans). `games/<title>/verify/baseline/title_screen/pcsx2_start_screen_f1.gsf1` was
  exported with it and is a FROZEN test input: it is not regenerated here (`gsf1_export.py` output differs from it at
  byte 206 under either queue rule; its draw `[9]` carries two bridging triangles of 2 px); its `Render_TitleFrameReplay*`
  gates pass on both the frozen and the regenerated stream.

- A dump's GSState freeze carries the 4 MiB local memory (offset 425 of the state block, located by the
  title's uploaded atlas page), but the hardware renderer's TARGETS are not in it: the display buffer (FBP 0)
  and the scene buffer (0x70000) read as zeros. The embedded 640x480 screenshot is the CRTC merge output
  (512x448 scaled 5/4 x 15/14, two field circuits offset by one line): its channel values are not 5-bit.
  A per-pixel comparison with it carries that resampling and the animation phase (screenshot = the frame
  presented at the vsync before the recorded frames) — compare floors first (stage P8.33-B, `docs/phases/PHASE8_IMPL.md`).

- The dump's packet `path` index is PCSX2's `GSgifTransfer` template index (3 = the merged GIF
  path in current PCSX2); no PATH1/2/3 attribution is available from a dump.
- The `.gsdl` vertex count is the post-expansion array (strips as triangles, ~3× the kicks); the
  dump's count is kicks. Compare keys and ratios, not raw counts.
- No session tags; no game constants (addresses printed are the stream's own).

## Transplanted from (OLD, read-only)

- `tools/parse_gsdump.py`, `tools/gsdump_state.py`, `tools/dump_transfers.py` (the format and
  the register bit layouts; the code here is NEW's own).
