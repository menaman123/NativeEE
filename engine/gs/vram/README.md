# `engine/gs/vram/`

**Phase:** 6  
**Purpose:** VramImage: 4 MB mirror + ONE PageRecord + single-writer commit (ARCH §4.3). Replaces five trackers.

## Contents (stage 6B)

- `page_record.h/.cpp` — `PageRecord`: author (Cold/Cpu/GpuTarget+fbp), upload/deposit/shadow epochs, commit state (Clean/Pending/Encoding/Applying), 64 × 128 B write bits, stale row band, reader flag; the page-band math (`FoldByteRangeToBand`, `ClampToRowBand`, `FoldByteRangeToPageRect`, `FullyCoveredPages`, `PageHeightForPsm`).
- `vram_image.h/.cpp` — `VramImage`: 4 MiB bytes + 512 records + lazy 4 MiB shadow; `WriteStream` / `NoteStreamWrite` / `SettleWrites` (EE side), `StageDeposit` / `CommitDeposits` / `NoteGpuAuthor` / `NoteGpuElided` (GPU side), `ReadView` (bytes ⊕ pending shadow), `MaterializePages`, `AssertNoStaleAtSave`, reader set (`ReaderPagesSeed` / `NeedsEagerDeposit` / `ReaderTouch`), `PagesOf`, counters.
- `vram_mirror.h` — `VramMirror`: the observer interface of an image's EE-side mutations (`VramImage::SetMirror`).
- `vram_op_log.h/.cpp` — `VramOpLog : VramMirror`: records the EE-side calls of one image (`WriteStream` runs coalesced, `NoteWriteNoBytes`, `NoteStreamWrite`, `SettleWrites`, `NoteCpuWrite`) as an op stream and `Replay`s them on another image — the pipelined GS queue's split image (DP-25 (b)): the EE's image and the pipeline's image evolve identically to one shared image fed the same calls, the upload epoch being a function of the EE-side calls alone. Pin `Gs_VramOpLogReplay`.
- Library `ps2r_gs_vram` (no dependencies). Tests: `tests/gs/vram/test_vram_image.cpp`.

## Belongs here

- VramImage, PageRecord

## Does not belong here

- GL surfaces (render/backends), target cache (gs/hle/targets)

## Transplanted from (OLD, read-only)

- `gs_page_tracker.h†, gs_deposit_overlay.h†, idx_epoch_guard.h†, alias_rect.h band math, opengl_backend.cpp VRAM regions`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

## Reader rule (P8.21-A)

`ReadView` overlays the pending shadow only where `CommitDeposits` would apply it (`upload_epoch <= shadow_epoch`): a page stamped by a newer EE transfer shows the transferred bytes. Pin `Gs_Inv39_ViewHonoursNewerWriter` (BUG-082).
