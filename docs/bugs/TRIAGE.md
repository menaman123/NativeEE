# docs/bugs/ TRIAGE — symptom → likely subsystem → first checks → tools

[REV2 2026-09-23 D4-D8] Decision tree distilled from the 30 seeded entries.
Use with INDEX.md (symptom table) and the escalation ladder in
EQUIVALENCE_PLAN §5. Rule zero: **reproduce on an anchor savestate with a pad
script before touching code** — every entry in this KB started that way.
[REV3 2026-09-23 D9] Rule zero, step one: **check the triage bundles first** —
`tools/triage/triage list` over `games/<title>/triage/`. If the runtime already
captured the event, the bundle IS your anchor+pad-script pair
(`triage repro <bundle>`), the fallback bundles name the exact code to lift
(`triage lift`), and the perf/spike bundles ship a pre-computed attribution
(`triage perf`). Only when no bundle matches do you hand-build a repro
(TRIAGE_CAPTURE.md §10).

## 0. Classify the symptom in 60 seconds

0. `triage list --since <session>`: did a bundle already fire for this moment?
   fallback bundle → §"lift it" (the manifest's What-to-do); perf bundle → §5
   with the bundle's attribution table; spike bundle → the frame_series
   deltas name the suspect (uploads/compiles/IO/parks) before you guess.

1. Does the HOST stay alive (presents/sound continue)? → guest-side problem.
2. Is it deterministic under the deterministic profile? → logic/emission bug.
   Load/timing-dependent? → race or wall-clock leak.
3. Scale test: does it exist at `PS2_GS_SCALE=1`? NO → enhancement-layer
   coordinate bug (§4). YES → semantics bug.
4. Interp test: gone with `PS2_GS_INTERP=0`? → gs-interp pairing (BUG-009).

## 1. Freeze / hang / livelock

- **EE pegged (`t1{busy≈1000}`), sound+vsync alive** → guest spin.
  First: `/usr/bin/sample` the process → decompile the spin site → what list/
  sentinel is it walking? Corrupted ring = look UPSTREAM for a NULL alloc or
  a bzero (BUG-001). Tools: lldb attach, S15 write-watch, call-trace hooks.
- **Loader never completes, low CPU** → completion-visibility deadlock: dump
  the hold/release arms; any release denominated in EE progress while the EE
  waits = livelock (BUG-004). Stream engine idle (q=0)? → voice pose (BUG-005).
- **Only on exit/close (zombies, RAM reports)** → shutdown wedge: `pgrep` for
  old processes first (BUG-027).
- **A unit test hangs under `ctest` but exits 0 when run directly, only under
  CPU load** → run-token wedge: a READY tid with no attached host thread got
  elected and nothing re-elects past it (BUG-040, `EeRunTokenAndEventQueue`;
  a wrong-order election `FAIL` printed just before the park is the tell).
  First: `ctest -R <name> --repeat until-fail:10 --timeout 120` bounded under
  deliberate CPU load (the idle machine does NOT reproduce); on the hang,
  `lldb -p` → `thread backtrace all` and dump `EeRunToken::owner_` before
  touching the test.

## 2. Black screen / no picture

- 0 FATAL + presents running → game draws nothing: census content jobs/frame
  (`g_draws_total`). Near-zero draws at boot → budget/loader starvation
  (BUG-019). Tools: PS2_BB_SNAP luma series; [BUDGET-EMU] diag.
- Presents NOT running → loop admission / present pacing (BUG-002).
- Never trust exit-code anchors: gate on pixels (the black-boot lesson).

## 3. Audio

- **Silence after a mode change (menu, save-select)** → dropped module
  command or voice pose: dump voice regs (pitch/ownership/NAX), byte-compare
  vs healthy (BUG-003, BUG-005).
- **Progressive desync, no underruns** → loop admitted slower than clocks:
  compare frame rate vs tick rate slopes (cue-lag probe) (BUG-002).

## 4. Visual — use the shape of the artifact

| Shape | Likely layer | First check | Entry |
|---|---|---|---|
| Whole-region two-state flip (sync, holds frames) | game DECISION flipping | diff display lists per frame; hunt decision inputs for host clocks | BUG-013/014 |
| Page/block-grid rectangles (8/16/32/64 px) | swizzle/aliasing/stride | which serve ROUTE did the read take (admission census)? mirror vs live? | BUG-015/016/017/022 |
| Fixed-period horizontal bands (32 rows) | stride/geometry displacement | overlay decode at candidate row offsets (meandiff 0 at offset = displaced) | BUG-017 |
| Fine screen-door in DARK regions only | 16-bit quantize/dither | is DTHE/DIMX stored? applied native-space? once? | BUG-020 |
| Static speckle far, crawls when moving | LOD/mip | texels-per-pixel census from recorded verts; MXL/MIPTBP present? | BUG-011/012/021 |
| Rainbow/stripes on an RT-fed layer | read-view geometry | TEX0.TW/TH vs bound surface dims; prescale uniforms at the draw | BUG-018 |
| One-frame pops, shifts when captured | cross-thread write race | epoch snapshot timing; two writers on `gs_vram`? | BUG-010, BUG-026 |
| ~1 s garbage during loads, self-heals | stall fallback path | serve-route census during the hitch; time-window carve-outs | BUG-030 |
| Shadows missing/displaced | deposit lifetime / scale map | still vs motion? defaults vs scale=2? live vs replay bands | BUG-006/007/008/024 |
| Flicker only with interp | pairing identity | matched-pair dump at fire frame | BUG-009 |

Tools for all of §4: drawlist recorder (`.gsdl` + verts), `gs_replay classdiff`,
PS2_SNAP/PPM present-hook captures, per-16x16-block CRC/luma analyzers,
serve-admission censuses, forced arms (`PS2_S32_FORCESTALL`,
`PS2_FTT_FORCE_DECLINE` — scoped `:tbp:psm:twlog:thlog`), scale-1 arm as oracle.

## 5. Performance

- Direction/content-dependent cliff in VU scenes → decline storm: check
  fallbacks==execs and per-exec cycle counts; exactly-watchdog-limit execs are
  walking corrupted data (BUG-023).
- Flat regression, no hot leaf → distributed tax: sampling profiler folded by
  module; check logging/probe cost first (BUG-025).
- Zoom/stream transitions → texture upload churn + cache keys (BUG-021).
- "Input lag" → measure the stamp chain; lag ∝ frame time is an fps bug
  (BUG-027).

## 6. Physics / game logic

- Rare periodic anomaly (every N ticks) → clock wrap/quantization: N × quantum
  vs 2^k counter boundaries; interpolate delivery, never patch the guest
  (BUG-029).
- Works in menus, dead in gameplay (or inverse) → different translation paths:
  diff emissions before logic (BUG-028).

## 7. When the artifact resists every hatch

The defect is on the SHARED path all hatches ride (BUG-018's lesson):
instrument the actual bound texture/uniforms/bytes at the point of use, not
the configuration space. Build a hardware-exact reference for ONE frame and
diff in index space — "which pages differ" localizes writer vs reader
instantly (BUG-016's method).

## 8. Fix rules (from HOOK_RECLASSIFICATION + the KB)

- Root-fix at the layer that violated the PS2 rule; hatches are for proof and
  rollback, default ON, hatch-0 bit-exact.
- Class/shape predicates, never addresses or scene constants, in any rescue.
- Every fix lands with its discriminator (forced arm) and a regression pin
  (named test, corpus capture, or counter assertion).
- Mods are NOT fixes and fixes are NOT mods — workarounds retire at root
  fixes (HOOK_RECLASSIFICATION §4); the mods surface is for enhancement only.
