# `engine/gs/xfer/`

**Phase:** 6  
**Purpose:** Host↔local / local↔local transfers with write-side swizzle via gs/format.

## Contents (stage 6B)

- `transfer_engine.h/.cpp` — `TransferEngine`: BITBLTBUF/TRXPOS/TRXREG staging, the TRXDIR latch, host→local HWREG cursor (per-PSM pixels per word, the `PsmLayout::RowAddresser` address per pixel, completion at RRW·RRH, IMAGE re-arm + destination re-stamp, partial flush of a superseded upload); the store is STAGED per 64-byte column (track 1d: a full column = one `VramImage::WriteStream`, a partial one its contiguous runs at the stage flush — end of every `WriteImage` span / HWREG word, before completion / re-arm / TRXDIR; bytes, marks and the `VramOpLog` runs identical to the per-pixel store, pinned by `GsTransferStagedStore` against a per-pixel reference), local→local copy in TRXPOS.DIR order, local→host request count; `InvalidateTextures`/`UploadVRAMTexture` jobs to the `DrawIrSink`, RGBA8 linear copy kept in `LastUploadPixels()`; `NoteOwnedUpload(dbp, bytes)` (track 1c-2): the pending-draw flush, destination marks (upload epoch, CPU authorship, write bits, settle) and `InvalidateTextures` job a completed host→local transfer of `bytes` bytes produces, WITHOUT the bytes, the payload copy or the `UploadVRAMTexture` job -- an owner placed the texture by another route (the resolver's owned slots); counted `Counters::owned` (`run.meta gs_xfer owned=`).
- Bug-compatible store rules kept from the original runtime: CT24 byte 3 forced to 0x80; a re-armed transfer emits no second upload payload. PSMT4 host→local data IS stored (one nibble per texel at PsmLayout's byte / nibble, the other nibble of the byte kept through a read-modify-write in the column stage) and so are the index-in-word modes (PSMT8H into byte 3 of the CT32-layout word, PSMT4HL / PSMT4HH into its low / high nibble; IMAGE data 8 / 4 bits per texel) — the original stored none of these (its title never issued them); a title whose textures are PSMT4 uploads sampled stale pages instead (the texture oracle's finding, `tests/gs/xfer/test_transfer_engine.cpp` Cursor / Staged pin the stores).
- Library `ps2r_gs_xfer` (links `ps2r_gs_vram`, `ps2r_gs_format`, `ps2r_gs_ir`). Tests: `tests/gs/xfer/test_transfer_engine.cpp`.

## Belongs here

- TransferEngine

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `gs_state.cpp WriteHWReg/TRXDIR paths, gs_hle.cpp hle_sceGsSendImage`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
