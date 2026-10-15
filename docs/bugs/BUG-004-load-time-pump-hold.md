# BUG-004 — 36-75 s load screens: the ordering hold WAS the load time; then one poll-spin body was 71.6 % of the rest

| Field | Content |
|---|---|
| **ID** | BUG-004 |
| **Title** | Loader HLE: per-read pump hold multiplied load time (4,408 reads × ~8.6 ms valve); after its removal, `FUN_002aa8d0` busy-wait dominated the span |
| **User-visible symptom** | Level loads take 36-75 s (vs ~1-2 s target); under turbo the ordering guarantee silently voided itself. |
| **Scene / savestate** | Load-transition anchors (island load, save-select→level); loader-FSM profiler `PS2_LOADER_PROF=1` (12 dual hooks on the FSM set). |
| **Subsystem** | cdvd-loader (primary), ee-timing |
| **Root cause** | Two stacked defects. (1) The S57 `[CHUNK-APPLY-ORDER]` gate held EVERY poll-path read ≥1 SyncV boundary with an 8-vblank-tick escape valve — the hold itself was the load time, and under turbo the valve fired at lat_frames≈0.5, voiding the ordering contract it existed for. (2) With the hold scoped away (F1+F2), wall-ms attribution showed **71.6 %** of the remaining span was ONE busy-wait body — `FUN_002aa8d0` at 13.7 µs × ~110 iterations/op inside the `FUN_0017d6b8` poll sub-loop; parse+drain+requester were 0.2 % (O(n²)-drain hypothesis falsified). |
| **PS2 rule violated** | Hardware polling/sync functions busy-wait on status our emulation doesn't advance in real time — the project's HLE strategy says these are HLE candidates by design. Ordering must be enforced at the game's own completion-visibility points (per BUG-001's rule), never by throttling delivery. |
| **Fix + hatch** | F1+F2 `[CHUNK-APPLY-SCOPED]`: pump delivers at its readiness floor; ordering enforced at the poll hook + windowed-read re-entry, released by the teardown-drain edge (one decision core `chunk_gate::HoldCompletionDecision`). Then the loader-HLE stage replaced the `FUN_0017d6b8` poll-spin with a native chunk-load pipeline (target ~1 s visible at 1×). Hatches: `PS2_CHUNK_APPLY_ORDER`, `PS2_LOADER_HLE` family (default ON). Post-refactor: cdvd/hle loader completion model (HOOK_RECLASSIFICATION §3.6) — HARDWARE/TIMING HLE class. |
| **Regression test** | Load-span wall-time budget on the load-transition anchor leg; loader-oracle check (`tests/loader_oracle_check.py`); harness identity on load legs. |
| **Source record** | `Ps2Recomp/docs/loop_state/LOADTIME_IMPL.md` (F1/F2), `LOADTIME_REPORT.md`, `LOADER_HLE_IMPL.md` (§1 attribution table), `LOADER_HLE_BRIEF.md` |
| **Triage tips** | Never "fix" ordering by slowing delivery — a hold that scales with op count IS the regression. Before optimizing a load path, wall-attribute it with self-time partitioning across the whole FSM: one spin body often owns >70 % and the "obvious" suspects (parsers, drains) own ~0. If a fix behaves differently under turbo, the fix is clock-anchored to the wrong domain. |
