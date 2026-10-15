# `verify/bench/` — the Release ms/frame baseline per leg

**Purpose:** the performance round's starting table (CONSTRUCTION_PLAN §4.8; PHASE8 product half,
SHARED policy of 2026-10-06 item 4: "baseline Release ms/frame per leg; re-land OLD's NORM fast paths one
at a time under the regression gate; target OLD's Release frame time on slowspot/island").
Informed by OLD's `scripts/run_bench.sh` (read-only): one launcher, one report per run, flags for the
frame target and the report path. Not copied: OLD's canonical `PS2_*` env set (NEW has no env knobs — the
profile and `--set` are the toggles) and OLD's `--window-start`/`WARP=` (no bench report to window yet).

## The exact command

```
cp build-release/app/host/nativeee <workdir>/bin/nativeee_release        # copy: build-release/ may be rebuilt mid-run
python3 -u verify/bench/run_bench.py --binary <workdir>/bin/nativeee_release --out <workdir>/bench \
        --note "<stage id>" \
        --perf-targets slowspot_f37092_s1789428268,slowspot_f40298_s1789693059   # -> <workdir>/bench/BASELINE.tsv
python3 -u verify/bench/run_bench.py --compare verify/bench/BASELINE.tsv <workdir>/bench/BASELINE.tsv
```

Rules (the table lies otherwise): run legs ONE at a time with the machine otherwise idle — the runner
records `load_1m` (the 1-minute load average) and `other_legs` (foreign `run_leg` processes) per row so a
busy-machine row is visible; the committed `BASELINE.tsv` is the frozen reference the `--compare` ratios are
taken against; a new reference is written by a stage that names the build it measures. A bench taken under
load is not a baseline: wait for `uptime` load_1m < 3.0 before the first leg, check it before every leg, and
re-run once any leg whose launch load exceeded 4.0 (keep the quieter row, say so in its `note`). The same
binary measured under a sibling's build/ctest load read 1.3–3.7× slower per leg (`ps2state_slot0_f57292`
191 vs 52 ms/frame) — the per-row `load_1m` is what makes such a table recognisable as contaminated.

**The committed reference (2026-10-06 18:14, BUILD-ID 2026-10-06 06:34:15, Apple M4 10 cores / 24 GiB):**
taken with the machine alone — `other_legs` 0 on every row, load_1m 2.37 at the first leg, 2.4–4.0 across
the rows (the rise is the back-to-back legs' own decaying average); `slowspot_f40298` launched at 4.29 and
was re-run once at 2.58 (41.18 ms/frame; the 4.29 row read 46.32 with a 73 ms p95). Reading the table: the
boot legs run 17–19 ms/frame (the BIOS/loader frames are cheap); the gameplay anchors 38–52 ms/frame, i.e.
2.3–3.1× the 16.7 ms a 60 Hz present allows, with `ps2state_slot0_f57292` (52.0), `slowspot_f37092` (51.0)
and `f36935` (49.5) the slowest; `win_p95` close to `win_p50` on every gameplay row says the cost is steady,
not spiky (the two boot rows' high p95 is the restore/first-frame window). The instrument behind every
row is the Release build's GL 4.1 backend on a hidden window plus the harness lanes (per-present GL
readback + hashing) — see "Profile" below; every row's `run.meta` says `video=gl gl_renderer=Apple M4`.
These are NOT play-mode presented frame times and not a bound on them: play mode keeps the same GL
backend but shows the window, paces to 60 Hz, runs the GUI, defaults `engine.mtvu` on and does no
readback/hashing — a different instrument, to be measured in its own right once `Bench` is armed.
The header's line 1 names the work-dir copy the table was taken from; the BUILD-ID + md5 on the same
line identify the binary (it is `build-release/app/host/nativeee` of that build, copied); later tables
record the repo-relative origin via `--origin` (default `build-release/app/host/nativeee`). Header line
3 was corrected in review loop 1 (it said "no window, soft video backend"); the 13 rows are untouched.

## What the numbers are

Every leg of the frozen battery is resolved through `verify/leg_spec.py` (`BASELINE_DIR` — the title's
`games/<t>/verify/baseline/` once the P8.36-D move landed, `verify/baseline/` before it) and
`verify/battery.py`'s leg reader (frames from the frozen `frames.fhash` / `LEG.tsv`, anchor, wall bound,
stall watchdog) and run once through `verify/run_leg.py --tree new`, exactly as the regression gate runs
it: `--profile deterministic --headless --frames N` from `coldboot` or the leg's `save_states/<leg>.ps2st`.
`N` = the leg's frozen count capped at `--cap` (default 2000: `coldboot_attract` is 6000 frames and is cut
at 2000 so the table costs minutes; the cap is in the row's `note`).

**Profile — what the instrument is:** the deterministic headless profile is the comparable, repeatable
measurement. Read from the tree, not assumed: `Config.video.backend` defaults to `VideoBackend::Gl`
(`engine/core/config/config.h`) and `DeterministicProfile::Apply` sets no `video.backend`
(`engine/core/config/deterministic_profile.cpp`: scale 1, interp off, fps_cap 0, vsync off, mtvu off);
`--headless` only HIDES the window — `VideoStack::CreateContext(.., headless, ..)` creates the window
hidden and a GL 4.1 context (`app/host/video_stack.cpp`, log `video: GL <ver> / <renderer> (WxH, hidden)`).
So every bench frame is rendered by the GL backend on the GPU (`run.meta`: `video=gl`,
`gl_renderer=Apple M4` on all 13 rows), swap interval 0 (no vsync wait), no wall pacing
(`engine.pacing=freerun`), `engine.mtvu=off`, the GS consumer inline on the EE thread
(`gs_queue=mode=inline`), with the harness lanes on: a per-present display grab (GL readback,
`glReadPixels`) hashed into `frames.fhash`, the audio lane, the mem lane (RAM hashed every 60 frames),
drawir/gsops hashing. `ms_frame` is therefore EE recompiled code + IOP/VU work + GL draw submission +
per-present readback + harness hashing per present, on one machine and one GPU. It is NOT a software
rasterizer, and NOT play mode's presented frame time (play mode: window shown, 60 Hz pacing, GUI,
`engine.mtvu` on by default, no readback/hashing); neither bounds the other without measuring both.
OLD's `run_bench.sh` measured PLAY mode (`PS2_NOVSYNC=1`, MTVU, cached VU, the GL fast paths, a window)
from the host's `bench_report.json`; the two are different instruments, so OLD's play-mode numbers are
the TARGET of the performance round, not a row of this table (see "OLD target"). The header's
`renderer:` line (written by the runner from the rows' `run.meta`) names the GPU behind the numbers.

| column | meaning |
|---|---|
| `frames_req` | `--frames` passed (frozen count, capped) |
| `frames` | presents counted by `run.meta` (≤ `frames_req`: present-skips / FMV coalescing) |
| `proc_wall_s` | the whole process wall (restore + run + shutdown) |
| `run_wall_ms` | `run.meta wall_ms` (arming → finalize, includes the savestate restore) |
| `ms_frame` | **the number to compare**: (last present − first present) / presents between, from a 20 ms sampler of the leg's `frames.fhash` (one line per present) — steady-state cost per present |
| `ms_frame_total` | `run_wall_ms / frames` (includes the restore/boot) |
| `win_p50` / `win_p95` | per-1 s window ms/frame, p50 / p95 over the windows — a coarse spread; true per-frame percentiles need the host's bench report (below) |
| `load_1m`, `other_legs` | machine state at launch (idle = low load, 0) |
| `status` | `OK` or `FAIL:<run_leg verdict>` |

**No per-frame report yet.** The NEW binary parses `--bench-report <path>` (`engine/core/config/
config_loader.cpp`) and `engine/core/diag/bench.h` carries the Bench harness with the frozen report
renderer (`CoreBenchReportFormat` pins it), but no `app/` code arms it (`run_leg.py`: "The NEW binary has
no bench report"). When a stage wires `Bench::Configure` + `OnVsyncFrame` into the composition root, this
runner should read `per_frame_ms` from the report and replace the sampler's `win_*` columns with real
mean/median/p95 (finding `INT-P8.36-L-BENCH-REPORT-UNWIRED`).

## `--compare A.tsv B.tsv [--threshold 1.10]`

Per-leg ratio `B/A` of `ms_frame` (and of `ms_frame_total`); a ratio above the threshold is printed as
`REGRESSION-CANDIDATE`, below `1/threshold` as `faster`. A REPORT for the performance round: the round
decides its own threshold and its KEEP/REVERT rule (policy: the regression gate's score tuple decides
correctness; this table decides speed).

## Performance targets

The policy names "slowspot/island". The two slowspot legs are `slowspot_f37092_s1789428268` and
`slowspot_f40298_s1789693059` (OLD's auto-captured 20 fps spots; flagged `PERF-TARGET slowspot` in the
row note). The runner names no leg itself (`verify/` carries no title data): a row is flagged when the
leg's `LEG.tsv` `notes` (read through `verify/leg_spec.py`) contains `perf-target[:<class>]`, or when
the leg is passed as `--perf-targets leg[:class],...`. The 13 OLD-derived legs' frozen directories carry
no such note (only `coldboot_attract` has a `LEG.tsv`, and frozen files are never edited), so the stage
that writes a table passes the slowspot pair on the command line; a `source=new` leg frozen later puts
the word in its own `LEG.tsv`. No leg of the 13 is NAMED island: the gameplay anchors `ps2state_slot0_f36935 … f59991`
(A5_VERIFICATION §5.1 "gameplay" states, incl. the two crystal caverns and the `f39807` wall-noise scene)
are the island-class rows; the one capture named island (`island_slot7` = `ps2state_slot7_f36230`, A5
§2.1) is NOT a battery leg — recorded as finding `INT-P8.36-L-ISLAND-LEG` for the orchestrator.

## OLD target: to be measured

OLD's Release play-mode frame time on the same anchors is the target. Its deterministic-profile legs are
NOT a performance measurement (the FRAME policy paces OLD at ~1 s/vf on boot, minutes per leg), so the
target must come from OLD's own instrument, bounded and read-only:

```
cd $PS2R_OLD_ROOT && env PS2_PERF=1 PS2_LOG=0 PS2_VU_MODE=cached PS2_VIF_FAST=1 \
  PS2_GL_FAST=1 PS2_GL_FAST2=1 PS2_MTVU=1 PS2_NOVSYNC=1 PS2_VU1_HLE=1 \
  <NEW>/verify/run_bounded.sh 300 <wd>/old_bench_<leg>.log -- build/host_app/host_app \
  --load-state save_states/<leg>.ps2st --bench-frames 600 --bench-report <wd>/old_bench_<leg>.json
python3 -c 'import json;d=json.load(open("<wd>/old_bench_<leg>.json"));print(d["derived"], d["frames_run"])'
```
(`scripts/run_bench.sh` is the same launch without `--load-state`; it hard-codes `build-v2/host_app/host_app`
and the CWD report path, so the anchors are launched directly.) The report's `per_frame_ms` /
`derived.fps_*` give the play-mode mean/p50/p5. Not run in P8.36-L: OLD opens a GL window and writes
`logs/` under the frozen tree (accepted side effects, REFERENCE_FREEZE) and the stage's budget went to the
NEW table; the performance round runs it first.
