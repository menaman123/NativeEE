# `engine/pad/savestate/`

**Phase:** integration I4-C (plan item 4.8, PADS half; user decision (i), 2026-09-29)  
**Purpose:** `PadsSection` -- the PADS `.ps2st` section over `PadProtocol`: the ports' pad areas and PortOpen boundaries, their staging, the last stamped wire word, the READY-image flags and the title's readiness event (26 bytes), followed by the original's 140 press-latch bytes written at their boot values (all zero) and never consumed on restore -- 166 bytes, the original writer's byte order, so the frozen anchors load unchanged. Validate: exact length, stage in {0, 1, 2}, each area 0 or a 256-byte area inside EE main RAM (`MainMemory::Phys`). OPTIONAL (`required=false`; absent = `SeedBootDefault()`: both ports closed at DISCONN, wire idle, no readiness event). Registered in `app/host/ee_bootstrap.cpp` after `TimerSection` and before the sound sections (the original file order `..., TIMR, ..., PADS, ..., SDRX, SNDC, MCHL, ...`).

## Belongs here

- PadsSection

## Does not belong here

- the protocol itself (`hle/pad_protocol`); the press latch (not implemented in this tree: P4-PAD-LATCH-DELETED, `docs/phases/PHASE4_IMPL.md`); the slots' contents (guest RAM, the memory sections)

## Transplanted from (OLD, read-only)

- `host_app/pad_input.cpp:21-24, 33, 81, 241-244` (the statics and their boot values), `:167-171` (`PadStage` 0/1/2), `:1015-1053` (`PadInput_SerializeState` / `PadInput_RestoreState`: the 166-byte field order and widths); `host_app/savestate.cpp:1245-1250` (capture), `:1875-1879` (restore: every field read back, no validation)

## Accepted difference

- The 140 latch bytes (`s_pad_read_count[2]`, `s_latch_bits[2]`, `s_latch_read_at[2][16]`) are non-idle in every frozen anchor and have no counterpart here; a re-save writes zeros there. Recorded as `docs/design/EQUIVALENCE_PLAN.md` §7.1 `P4-PADS-LATCH-BYTES`.
- The two clocks (`opened_at`, `readiness_event`) are u64 boundaries here and u32 on the wire: the low 32 bits are written, the u32 is widened on restore.
- The slot sequence counter (`PadProtocol::seq_`) is NOT a wire field: on restore it is DERIVED from the restored pad area (slot 0's +0x58 word of the first open port, read through the `Reader` the owner passes; the memory sections restore ahead of PADS), so the next stamp publishes image+1 — the console's PADMAN continues its frame counter. The original's counter was a process static that restarted at 1 after every load (`host_app/pad_input.cpp:829`), the value its frozen probe images carry (5 at b2, 63 at b60); `PadsSection::SeqRestore::Restart` is the one-line toggle back to it (DP-24, BUG-074). `DerivedSeq()` reports the value a restore derived (`run.meta pad_seq=<live>,<derived>`).

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
