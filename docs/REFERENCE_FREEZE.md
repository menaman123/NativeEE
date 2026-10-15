# REFERENCE_FREEZE — the original repo is read-only from 2026-09-26

**Decision (user, 2026-09-26):** `$PS2R_OLD_ROOT` is the
frozen REFERENCE. No edits, no deletions, no rebuilds. It exists only to:

1. be the OLD side of every `verify/diff_legs` comparison (its binaries are the oracle);
2. be READ (source, docs/loop_state, corpora, savestates) when transplanting logic
   into this tree.

## Frozen state

| Item | Value |
|---|---|
| HEAD | `5e1b17fe` ("Fixed crystal issue") + ~120 uncommitted source/test/doc files (harness, IOP kernel, EE run token, ee_timing, all September fixes) — the user owns committing these |
| `build/host_app/host_app` (legacy engine, the binary the user plays) | Sep 25 21:26, md5 `80e3689a9e376263003dcd9c45fae83f` |
| `build-v2/host_app/host_app` (v2 engine) | Sep 25 21:26 |
| Frozen OLD hash baselines | `games/<title>/verify/baseline/<leg>/` (moved by path from `verify/baseline/` in P8.36-D, byte-identical; 12 legs, 3-rep stable, env_hash re-stamped 2026-09-26) |

## Rules for every stage from now on

- Open OG files read-only. Never run `cmake --build`, `git`, or any deliberate write in
  the OG tree.
- Its binaries may be executed (bounded, via `verify/run_bounded.sh` of THIS tree) as the
  OLD side of the harness and for `gs_replay`/`vu1_replay` oracles.
- Known open items in the OG runtime are NOT fixed there; they become construction
  work items here: BUG-026 (IOP kernel realtime boot stall), BUG-038 (scripted cold-boot
  leg stalls — human cold boot works), v2 defects P4FIX-F1 (f57292 / f59991).
- If a harness difference is adjudicated as a bug in OLD, record it under
  EQUIVALENCE_PLAN §7 (accepted-difference protocol); the OLD binary is not patched.

## Accepted side effects of running the OLD binaries (amendment 2026-09-26)

The frozen `host_app` cannot be run without touching its own tree: on startup it
`chdir`s to the OLD project root (`host_app/main.cpp:4976-4979`, "exe/../../.. if it
contains game_files/") and writes relative paths from there. "Any write" therefore
means "any write we can avoid"; the residue is enumerated here and nowhere else:

| Path under OLD | Written by | Status |
|---|---|---|
| `logs/cpu_diag.txt` | `main.cpp:704` (unconditional) | ACCEPTED — gitignored (`logs/`) |
| `logs/ring_exit.rlog` | `ring_log.cpp:324` (exit dump) | ACCEPTED — gitignored |
| `logs/runtime_log.txt` | `main.cpp:5003` when `PS2_LOG` is not `0` | ACCEPTED — gitignored; the deterministic profile sets `PS2_LOG=0`, so harness legs do not touch it |
| `logs/ring_crash.rlog`, `logs/crash_report.txt` | the crash handler — written when a run dies by signal. Harness legs exit via the bench `std::_Exit(0)` (`main.cpp:14714`) and do NOT write them; PLAY-mode quits do, every time (BUG-039: SIGABRT from `IopKernel::~IopKernel()` under `exit()`) | ACCEPTED — gitignored |
| `bug_reports/<UTC>_crash/` (~43 MB: crash report, ring dump + decode, log tails, a copy of the newest `save_states/slowspot_*.ps2st`) | `bug_report.cpp` STARTUP crash-scan: every launch — harness legs included — bundles a leftover `logs/crash_report.txt` from the previous aborted exit, then truncates it (`bug_report.cpp:1-10`, "assembled at the NEXT launch"). Observed 2026-09-26: the 60-frame redirect probe created `bug_reports/20260926-065023_crash/` (43 MB) from the previous play-mode quit's report; 7 such bundles exist since the freeze | ACCEPTED — gitignored (`bug_reports/`); cost ~43 MB per launch that follows a play-mode quit (none when the previous exit was a bench leg: the report is then empty). Cannot be suppressed without changing the leg's `PS2_*` environment (`PS2_BUGREPORT=0` would change `env_hash` and invalidate the frozen meta gate); the user may prune `OLD/bug_reports/2026*_crash/` (gitignored) at will — this tree's tooling never deletes in OLD |
| `bench_report.json` | `bench.h:114` default report path, written at exit under `--bench-frames` | **NOT accepted** — TRACKED file. `verify/run_leg.py` always passes `--bench-report <leg dir>/bench_report.json` (absolute; `bench::ParseArgs` resolves the path before the chdir, `bench.h:680-688`). Verified 2026-09-26: a 60-frame leg left `OLD/bench_report.json` at its pre-run mtime/md5 and wrote the report into the leg dir |
| `save_states/slowspot_*`, `bug_reports/*_slowspot*` | slowspot capture | does not fire under `--bench-frames` unless `PS2_SLOWSPOT=1` (`slowspot.cpp:35-38`); the profile never sets it |
| `display.ini`, `bindings.ini`, `mc0/` | GUI "save as default", memory-card HLE | GUI is never opened headless (`PS2_GUI_OPEN=0`); MC writes only when the game saves (no leg does) |
| `logs/recompiler_tool_v2/runtime_log.txt` (and reads of `tmp_analysis/tiny_*.txt`) | the frozen `recompiler_tool_v2`, RELATIVE TO CWD | avoided by construction: the tool is only ever run with cwd pinned to a NEW-side scratch directory (CONSTRUCTION_PLAN §A.1, R16) |

The OLD working tree already carried ~120 uncommitted files and a modified
`bench_report.json` before the freeze (the user owns committing them); nothing in this
tree's tooling adds to that set after 2026-09-26.
