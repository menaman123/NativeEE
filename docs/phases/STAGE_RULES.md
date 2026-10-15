# STAGE_RULES — binding rules for every construction stage (read fully before working)

Current direction (user, 2026-10-07; DT-12): complete the host experience,
with graphics M6→M7→M8→M9 and services M3→M4→M5 in parallel worktrees.
Recompiled gameplay remains; no packet path, IOP/STREAM, IPU command interface,
VU interpreter or GS register model may execute during play at completion.
Read DECISIONS_LOG DT-12 for the reference roles and ownership contract.
The missing user savestate is not a prerequisite. The user will play Release
at milestone reports and provide new bug evidence.

Current user design rule (2026-10-07): **retire hardware scaffolding; do not
deepen it**. A defect involving DMAC/VIF/GIF, IOP kernel/STREAM.IRX or IPU
commands is fixed by HLE'ing the game's intent using the decomp's platform
layer, bucket meanings or RPC meanings as the spec, until that model path
stops executing. Track the intent, native replacement and retirement evidence
in `docs/design/HLE_COVERAGE.md`. This overrides conflicting hardware
completion/transplant instructions below.

Current retirement gate (DT-12 with DT-11 memory advisory): same-input
NEW model/replacement self-oracle with a one-line toggle and attributed
differences before default; all 13 legs ×3 stable with non-regressing
frame/audio first-divergent keys; replaced-model census zero with positive
replacement work; OLD-bug pins on the replacement. Before each replacement,
grep OLD's records and list the relevant bugs. No PCSX2 use or acceptance in
this wave; its existing pins remain unchanged and excluded. Do not recreate
dead transport state to match memory or use a gameplay hook to hide a defect.

Trees:
- NEW = `<repo>` (integration on `main`).
- SERVICES = sibling `NativeEE-services`, branch `port-services`.
- GRAPHICS = sibling `NativeEE-graphics`, branch `port-graphics`.
- OLD = `$PS2R_OLD_ROOT` — FROZEN READ-ONLY per `docs/REFERENCE_FREEZE.md`.
  Read its sources; run its existing binaries only as the harness oracle; never build/edit/git there.
- SPEC = `<projects>/the title-reversed`, read-only specification, never copied.
- ARCHITECTURE REFERENCE = `<projects>/jak-project`, read-only after clone, never copied.

Process:
1. Bounded runs only. macOS has no `timeout`: use `verify/run_bounded.sh <secs> <log> -- <cmd>`.
   Exit codes via `${PIPESTATUS[0]}`, never `tail`'s.
2. Helper scripts go to files under `/tmp/<stage>/`; never `python3 -` / stdin; never multiprocessing.
   Kill your helper processes before finishing.
3. Work in bounded, reviewable stages. Record completed work and open items,
   integrate and commit approved stages, then continue the authorized roadmap.
   An old stage time budget is not a reason to abandon the full objective.
4. The tree must build (`cmake --build --preset macos-dev -j4`) and pass `ctest --preset macos-dev`
   at EVERY stopping point — including the two lints. Never leave a red tree.
5. Commit after every approved stage (user DT-12). The initial checkpoint
   `22c7556` is complete. Own implementation only; no reference code/license copying.
6. Record after every step: `docs/phases/PHASE<N>_IMPL.md` (what landed: files, OLD sources
   transplanted, flags consumed with their outcome, tests added, gate status, handoff).

Code rules (ARCHITECTURE §11/§13; enforced by `tests/lint`):
- One primary type per `.h/.cpp` pair; file = snake_case(type). Headers = declarations
  (trivial accessors ok; templates in `.inl`). No textual `.inc` code includes.
- No session tags (`[R123]`, `r120gl::`) or history narration in code; comments describe the present
  and cite the PS2 rule. No game constants/addresses in `engine/` (LintEnginePurity).
- Flags are applied at transplant time per `docs/filemap/FLAG_LEDGER.md`:
  NORM → write only the winning arm; SETTING → `Config` field (peak default);
  DIAG → `core/diag` category under `PS2R_DIAGNOSTICS`; DELETE / TEST-ONLY → do not transplant.
  There is exactly ONE `getenv` site in the whole tree (ConfigLoader's dev override).
- Transplant means: read the OLD implementation, understand the PS2 rule it implements, write it
  cleanly here with the same observable behaviour. Bit-exact behaviour is the goal; the harness
  (`verify/`) and the phase's oracle tests are the proof — not resemblance to OLD code.

Proof:
- Each phase's gate is in `docs/design/CONSTRUCTION_PLAN.md` §4.<N> and `docs/phases/PHASE<N>_PLAN.md`.
- Harness: `verify/run_leg.py --tree new|old`, `verify/diff_legs.py --cross-engine --lanes …`,
  frozen OLD baselines in `verify/baseline/`. A difference where OLD is wrong follows
  `EQUIVALENCE_PLAN.md` §7 (accepted-difference record), never a silent tolerance.
