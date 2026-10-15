# verify/ — Phase-0 deterministic verification harness

**Baselines live in `games/<title>/verify/baseline/`** (P8.36-D, CONSTRUCTION_PLAN §4.8: moved
there from `verify/baseline/` by path, byte-identical — `baseline/MANIFEST.sha256`; every tool here defaults
`--baseline` to it through `leg_spec.BASELINE_DIR`, discovered as the one `games/*/verify/baseline`; the old path is
gone, no symlink — `verify/anchors_table.py` checks it). `games/<t>/verify/anchors.tsv` indexes the 13 legs.

**Current retirement acceptance is DT-12; see Typical use and the evidence
contract below.** Earlier console/word-attribution policies in the tool catalog
are retained as history and are not acceptance for this wave.

The two-binaries × one-deterministic-profile comparison rig from
EQUIVALENCE_PLAN.md §2/§4 (MIGRATION Phase 0). The in-binary emitters live
in the OLD tree under `host_app/verify/` (default-off; `PS2_VERIFY_OUT`
arms them); this directory holds the runner, the differ, the
admissibility self-test and the profile definition.

## Scripts

| Script | What |
|---|---|
| `run_leg.py` | Run ONE bounded, headless leg on the deterministic profile. `--binary <host_app> --anchor <state.ps2st\|coldboot> --frames N [--inputs padlog] --out <dir> [--tree legacy\|v2\|new] [--state-every K] [--timeout S] [--stall-s S]` (`--stall-s` = the progress watchdog `RB_STALL_S` for this leg, else the inherited variable, else 300 s OLD / 60 s NEW). Wraps OUR `verify/run_bounded.sh` (wall timeout + RSS kill + progress watchdog), strips inherited `PS2_*`, passes `--bench-report <out>/bench_report.json` for OLD trees so the frozen tree's tracked report is never written (REFERENCE_FREEZE "Accepted side effects"), asserts run.meta completeness. |
| `run_leg.py --tree new` | (Phase 1+) the NEW `nativeee` binary: NO environment is set; the leg is armed on the command line — `--profile deterministic --headless --harness-out <out> --frames N`, with `--anchor S` → `--load-state S`, `--inputs F` → `--inputs F`, `--state-every K` → `--state-every K`. Timeout `max(120, 60 + N/10)` s; the runner's own `RB_*` variables are the only env it touches. |
| `run_bounded.sh` | The bounded process runner (ours, 2026-09-26): `run_bounded.sh TIMEOUT_S LOGFILE -- cmd args…`; exit 124 wall timeout, 137 RSS > `RB_RSS_KB` (8 GiB), 125 no growth of `RB_PROGRESS_FILE` for `RB_STALL_S` (300 s); otherwise the command's exit code. Never changes cwd. Used for every OLD-oracle run (legs, `gs_replay`/`vu1_replay`, the frozen recompiler tools). |
| `diff_legs.py` | `diff_legs.py A B [--allow-build-diff]` — asserts run.meta equality (gl_renderer / profile / env_hash) FIRST, then reports the first divergent frame, audio window and mem checkpoint. Exit 0 identical / 1 divergent / 2 meta-gate or usage error. The verdict line names the lanes it covers, the masked byte count per lane AND the coalesced-present count (`diff_legs: IDENTICAL (lanes=meta,frames,audio,mem, masked 0 B, coalesced 0)` — a plain run always says `masked 0 B, coalesced 0`). `--selftest` runs its unit checks (the ctest `VerifyDiffLegsSelftest`). |
| `diff_legs.py` frames lane | **Paired BY KEY** (DP-9 (a), user decision 2026-10-01): each `frames.fhash` line is `<key>,<hash>`; the lane pairs equal keys, never line indices. OLD's graphics thread skipped a present when the next queued job was also a VSync, so an OLD leg lacks the keys of those coalesced presents (coldboot 64, 65, 420, 421, 619, 966) while NEW writes one line per boundary. When exactly one leg is `tree=legacy`, a key that leg lacks inside its span is a **coalesced present**: skipped, listed (`frames.fhash: coalesced presents (keys the A leg lacks, …): 2 -- 64,65`) and counted on the verdict line (`coalesced 2`) — never silent. Same-tree pairs (OLD vs OLD, NEW vs NEW) get no allowance: a missing key is `ABSENT` = divergent. Values are never tolerated (`FIRST DIVERGENT frame at key K (paired by key)`); different last keys are a `LENGTH divergence`. NEW keys a RESTORED leg by the original's vsync-frame convention (DP-9 (c)): key 1 for its first present, then `restored_vsync_frame + 1, + 2, …` (`games/<t>/verify/baseline/artdet_f37152/frames.fhash`: `1, 37157, 37158, …`); a cold boot keys `1, 2, 3, …`. **The restore-point present** (DP-26 (a)): a restored anchor's key 1 is NOT a value pair — OLD's line 1 is its boot-era present BEFORE the load (no readable scanout → `1,0x0000000000000000` on all 11 baselines) and its first post-load present is `restored_vsync_frame + 1`; NEW's boundary 0 IS the restore point, so its key 1 is the restored scanout (the DISPFB buffer out of the restored local memory), an event OLD has no line for. When exactly one leg is `tree=legacy` with line 1 `1,0x0` and the other leg's `run.meta` carries `restored_vsync_frame=<vf>` ≠ 0, key 1 is listed (`frames.fhash: restore-point present (OLD has none): key 1 -- A: 1,0x0… (the original's pre-load present …), B: 1,0x… (the restored scanout); values compared from key vf+1 …`) and carried on the verdict line (`…, coalesced 0, restore-point present (OLD has none): key 1)`); the value pairing starts at key vf + 1. A cold boot, a legacy line 1 that is not 0 and same-tree pairs are unaffected (key 1 pairs by value); a restored leg carrying a key between 1 and vf is named as a lane error, never skipped. Pins: `RootFramesKeyConvention`, `RootDiffLegsCoalesced`, `RootAnchorGsRestored` (key 1 == the anchor's DISPFB decode), the `--selftest` restore-point checks. |
| `diff_legs.py --fmv-pictures MANIFEST [--fmv-window-legacy LO-HI]` | **The FMV window paired by PICTURE** (DP-12 (a)). The window cannot be paired by frame hash (NEW's whole-movie demux loop raises no boundary; OLD free-ran ≈1100 wall-clock presents). Each leg's window is EXCLUDED from the by-key pairing — the legacy leg's declared on the command line from its baseline (coldboot: `172-976` = the hold frame `0x3473b62d15ba180c` ×216 from key 172, movie 1 `431-620`, the splash, movie 2 `631-967`, the splash to 976; post-FMV content from 1010), the NEW leg's from its census `fmv_window=<first>-<last>` (boundaries at which the Verify slot found pictures served since the previous one; keys = boundary + 1) — and the keys ABOVE the window pair by KEY OFFSET from each window's end (A's `hi_a + n` with B's `hi_b + n`; the legacy leg's coalesce allowance holds there, its missing key listed as `hi_legacy + n`). **Window starts:** pre-window keys pair only below the LOWER of the two starts; the later-starting leg's keys from there up to its own window are **unpaired** — reported (`frames.fhash: FMV windows start at different keys: A 172, B 171 -- keys 171 of A are unpaired (1; …)`) and counted on the verdict line as `unpaired N` (appended only when N > 0), never ABSENT, never coalesced. Choose LO equal to NEW's first window key when the baseline allows it (coldboot: NEW's census `fmv_window=170-183` → keys 171–184, so `171-976` aligns; `172-976` reports OLD's key 171 unpaired). A NEW leg without a census (`fmv_window=none`) leaves its keys at/above the legacy start unpaired (counted) and the lane NOT PAIRED; nothing it has is counted as a coalesced present of the legacy leg. The window is `frames: FMV window PAIRED-BY-PICTURE (547/547, plane-check 2/2 movies)` only when (1) the NEW leg's `fmv_pictures_served` equals the manifest's `served_total` and row count (MANIFEST = `tests/ipu/fixtures/coldboot_pictures.sha256`, the `FmvPictureOracle` golden proved against the movie files by its ctest) and (2) the **plane check** `<NEW leg>/fmv_plane_check.txt` covers every movie with every strip bit-exact. Plane-check procedure = `verify/fmv_plane_check.py --leg <NEW leg> --og-root <OLD root> --manifest tests/ipu/fixtures/coldboot_pictures.sha256` (the tool; `int_work/i72a/b1/` was the hand-run reference): the leg must carry `state_b<b>.ps2st` images across its `fmv_window` (`--state-every 1`, with `run_leg.py --timeout` raised — 38 MB per image slows the leg past the default bound; delete images outside the window as they appear); the tool dumps the oracle's pictures (`build/tests/ipu/fmv_picture_oracle --dump-dir`), lays each out through the REAL `FrameWriter::WriteRgb32Strips` with `build/tools/fmv_layout_ppm/fmv_layout_ppm` (16-px column strips of `aligned_h` rows, A = 0x80), locates each movie's RGB32 plane in every window state's RAM0 (a high-entropy 4 KiB probe, 16-byte aligned), compares strip-wise against every picture of that movie and writes one line per movie `movie<N> boundary=<b> plane=0x<addr> picture=<k> strips_exact=<n>/<total>` — per movie the LAST decoded picture's reading with the most bit-exact strips (ties → the earliest boundary); every plane of every window state is read (both double-buffer halves) and listed as `#` lines, `picture` = the oracle's DECODED index — plus `#` lines with every state's reading. Anything less prints `frames: FMV window NOT PAIRED (<served>/<total>, plane-check <m>/<movies> movies): <why>` and the lane is divergent — there is no partial credit (`30/32` is NOT PAIRED). |
| `diff_legs.py --cross-engine` | OLD vs NEW binary (CONSTRUCTION_PLAN §2): the two trees have different config vocabularies, so the meta gate is `profile` equality + `finalized=1` on both; `gl_renderer` / `env_hash` / `build_id` / `tree` differences are printed as `NOTE (cross-engine)` lines, never gated. |
| `diff_legs.py --lanes meta,frames,audio,mem` | Restrict the hash lanes compared (`meta` = the gate only; `\|` also separates). Exit 0 means "every REQUESTED lane identical" — the verdict therefore prints the lane set, e.g. `diff_legs: IDENTICAL (lanes=meta, masked 0 B)`, so a META-only pass cannot be read as lane identity. |
| `diff_legs.py --mask-ranges FILE` | The mem lane compared UNDER A MASK (EQUIVALENCE_PLAN §7 accepted differences): the paired `state_b<N>.ps2st` images of both legs (`--state-every K`) are hashed with the masked bytes zeroed — RAM0, then SPAD, then META (never maskable), `state_hash.py`'s chain. FILE grammar: `0xLO-0xHI  <ROW-ID>  # why` (HI exclusive). **A mask range is never added without a §7.1 row:** `<ROW-ID>` is mandatory, matches `^P[0-9]+-[A-Z0-9][A-Z0-9-]*$` and must head a §7.1 table row in `docs/design/EQUIVALENCE_PLAN.md` or `docs/phases/PHASE*_PLAN.md` / `PHASE*_IMPL.md`; a line without a resolvable row is a usage error (exit 2) naming the line. Ranges lie in RAM0 (`0x0-0x2000000`) or the scratchpad (`0x70000000-0x70004000`, image offset `addr & 0x3fff`); **the only SPAD bytes a mask may cover are the boot stack's 13 dead words `0x70003ef0-0x70003f44`** (HI exclusive = the word-inclusive `0x70003ef0..0x70003f40` of PHASE4_IMPL :942 / user decision 2; `SPAD_MASK_WINDOW`, one constant — any other SPAD range is a usage error). The report lists every range with its lane, size and row, and EVERY verdict line carries the masked byte count per lane: `state_b0.ps2st: IDENTICAL-UNDER-MASK (masked RAM0 1940968 B, SPAD 84 B, META 0 B) hash 0x…` / `… DIVERGENT under mask: A 0x… B 0x… (masked …)` / `diff_legs: IDENTICAL-UNDER-MASK (lanes=mem, masked RAM0 … B, SPAD … B, META … B)`. The word is never plain identity. Masking is symmetric (a leg vs itself → IDENTICAL-UNDER-MASK, equal counts) and applied at compare time only — the frozen baselines are untouched. Current file: `mask/coldboot_elf_text.ranges` (15 ranges, 6 rows). |
| `battery.py` | The 13-leg battery (12 + `coldboot_attract`) against the frozen `games/<t>/verify/baseline/` legs — run on the ORACLE binary (`nativeee_oracle`, the retained IOP arm the baselines were frozen on; M12 TRACK 3b: the play binary `nativeee` has no IOP and runs the deterministic profile as `hle.startup=native`): `battery.py --binary build/app/host/nativeee_oracle [--lanes meta] [--reps N] [--out DIR] [--legs a,b] [--timeout S]`. `--timeout S` is passed through to `run_leg.py --timeout` for every NEW leg (the per-leg wall bound; absent → run_leg.py's `max(120, 60 + frames/10)`, which the anchors' 600-frame legs exceed since DP-8 (1) — DECISIONS_LOG DP-16 (d)); the stall watchdog `RB_STALL_S` is untouched unless the leg's `LEG.tsv` names `stall_s` (then `run_leg.py --stall-s`); a leg's `LEG.tsv` also supplies its frame count and raises its timeout (`leg_spec.py` row). `--selftest` checks the argv pass-through and the LEG.tsv-driven frames/bounds/anchor. Per leg: runs the NEW binary through `run_leg.py --tree new` for the baseline's own frame count, `diff_legs --cross-engine` over the requested lanes (`META-OK` / `META-OK IDENTICAL(lanes)` / `META-OK DIVERGENT(lanes)` / `META-FAIL`), reps compared with each other when `--reps > 1` (`ADMISSIBLE` / `UNSTABLE`), and `FP-OK` when the NEW `run.meta` `env_ps2` equals the deterministic fingerprint. Exit 0 only when every leg is META-OK (+ ADMISSIBLE + FP-OK). Phase 1 claims META only. |
| `score.py` | **The score instrument (DECISIONS_LOG DT-1).** `score.py <battery_dir> [--probes <dir> \| --probe-words <json>] [--mask mask/coldboot_elf_text.ranges] [--ctest-log <log>] [--out score.json] [--reps-not-required]` → `score.json` + a table. Tuple `(legs_identical, outside_mask_words, pins_green)`: `legs_identical` = battery legs whose row reads `ADMISSIBLE META-OK IDENTICAL(frames,audio,mem)` (every rep; with `--reps 1` the cell is `n/a` and the leg counts only under `--reps-not-required`, printed as a 1-rep preview); `outside_mask_words` = Σ over the probe pairs (frozen OLD `baseline/<leg>/probe/old_b<N>.ps2st` vs NEW `<probes>/<leg>/new_b<N>.ps2st`, same boundary) of the RAM0 + SPAD words differing outside the mask (I8.3-G's `region_summary.py` rule; the differing-offset list built once per lane; META never counted); a pair whose NEW image is missing counts the OLD image's FULL word count (a stall is not a free pass); `pins_green` = the ctest pass count from `--ctest-log` (`N% tests passed, F tests failed out of T` → T−F) or −1 = not run. Also printed (progress, not score): per leg the first-divergent key per lane (from `<battery_dir>/<leg>.diff.out`) and the per-pair word counts. `--compare A.json B.json` → `BETTER / WORSE / EQUAL` for B against A: legs_identical higher wins; equal → outside_mask_words lower wins; equal → EQUAL (pins alone never improve); ANY pins regression (both run) → WORSE regardless. `--selftest` = ctest `VerifyScoreSelftest`. **DT-7 (P8.30-T):** every pair also carries `words_raw` (= total), `words_booked_console` (the words `attribute_probe.py` books to a FILED §7.1 row whose directive carries `proof=console:<image>@<leg>:b<N>` for THIS pair and whose console word == NEW ≠ OLD — checked word by word; 0 for a pair without a proved rule) and `words_unbooked = raw − booked`; the score prints `tuple=` (the REPORTED number, Section 6 unchanged) and `tuple_unbooked=` (the DECISION number); `--compare` prints the raw verdict `[REPORTED]`, the unbooked verdict `[DECISION]` and `decision: …` (a file without `tuple_unbooked` decides on the raw tuple and says so). `--probes DIR` / `--book-from DIR` let `score.py` read a run's `PAIRING.tsv` (per-run scene pairing) and compute the booked column from images on disk (`--classes` = the rules). Nothing is masked and no baseline moves. |
| `score.py --regression REF.tsv candidate/score.json` / `--write-regression-ref gate/score.json REF.tsv` / `regression_ref.py` | **The standing REGRESSION gate (DECISIONS_LOG DT-8, P8.35-C).** The memory-identity goal against OLD is RETIRED (the user's decision of 2026-10-06 11:23; PCSX2 is the reference); the 13-leg battery is a regression gate only. `baseline/REGRESSION_REF.tsv` (next to the frozen legs; written from the gate of record S12 by `--write-regression-ref`; columns `leg frames_first_div audio_first_div mem_first_div raw_words booked unbooked source_gate` + an `ALL` row; header `# legs_identical= / # pins_green= / # tuple=`) is the reference; `--regression` prints `regression: PASS|FAIL vs REGRESSION_REF.tsv`, the reasons and a per-leg table (exit 0 / 1). Rule: `legs_identical` not lower; the RAW and the UNBOOKED word totals not higher; every leg's FRAMES and AUDIO first-divergent key ≥ the reference's (a key that moved LATER is better; `IDENTICAL` = the lane never diverges and ranks above every key — 0 is a REAL key: audio W0 / mem b0 occur); a leg missing / RUN-FAIL fails closed; a pins regression (both ran) fails; the MEM lane and the per-leg word counts are REPORTED, never a criterion. `gate_run.py` runs it when the reference exists (`--regression-ref` overrides the path) and writes the verdict into SUMMARY.md `## Regression`. Never edited by hand: regenerate from the next gate of record. `regression_ref.py --selftest` = ctest `VerifyRegressionRefSelftest`. |
| `probe_images.py` | The probe images. `--old [--legs all\|a,b] [--proof-only]` (re)produces the FROZEN OLD set under `baseline/<leg>/probe/`: anchors `old_b2.ps2st` (OLD `--frames 4 --state-every 1` → `state_b1`) and `old_b60.ps2st` (`--frames 62 --state-every 59` → `state_b59`); coldboot `old_b60` (`--frames 121 --state-every 59`) and `old_b120` (`--frames 121 --state-every 119` → `state_b119`); a leg with a `LEG.tsv` takes its plan from the file's `probes` (coldboot_attract: `old_b1500/2700/3780/4200/5400`, one OLD leg each at `--state-every B-1`, every one proved by its `mem.mhash` line; NEW: one `--frames 6000 --state-every 60` leg, the five boundaries kept) — OLD `state_b<N>` = boundary N+1 (the request at boundary n ≠ 0 lands one boundary later; OLD writes no image at 0/1, so 2 is the first probe). `PROOF.txt` per leg: the `state_hash.py` chained hash of each image == the baseline `mem.mhash` line of its boundary (60/120); boundary 2 has no line (the lane writes every 60) and is proved by the header `vsync_frame(old_b2) == vsync_frame(old_b60) − 58` against the hash-proved sibling. `--binary <nativeee> --out <dir> [--legs …] [--set k=v …] [--timeout S]` produces NEW images for a binary: `<dir>/<leg>/new_b2.ps2st` (`--frames 3 --state-every 2`), `new_b60.ps2st` (`--frames 61 --state-every 60`); coldboot `new_b60`/`new_b120` (`--frames 121 --state-every 60`); NEW labels by the boundary itself. Images are ≈ 42 MB each (the OLD set ≈ 1 GB, kept; NEW images are transient). **A full disk drops images silently** (the binary removes a `.tmp` whose write failed) — a MISSING image is then a disk event, not a stall; check `df` first. **Per-run pairing (P8.30-T; `scene_pairing.py` row):** for a leg with a LEG.tsv `pair_map`, `--pairing run` (default) first runs the edge census leg (`--frames <frozen NEW edge + --edge-margin 150> --state-every 1`, each image read for the boot-SM word and deleted as it lands; one retry at 4× the margin; EDGE-NOT-FOUND = no mapped image, the pairs count MISSING) and then produces the mapped images at `B + the run's offset`, printing `scene OK` / `SCENE-MISMATCH` of each against the OLD probe's scene words; `--verify-edge2` checks the predicted pass-2 edge with two one-image legs; `--pairing frozen` reproduces the LEG.tsv map. |
| `gate_run.py` | **One command = guard → battery → probes → score (DT-1/DT-2).** `gate_run.py --release <bin> --debug <bin> --out <dir> [--set k=v …] [--reps 3] [--skip-guard] [--lanes frames,audio,mem] [--timeout 900] [--legs …] [--ctest-log <log>] [--keep]`. (1) the identity guard `tests/app/root_release_lanes_identical.py` as a leg pair (coldboot 200 + artdet 60 on both binaries; `guard=DIVERGENT` voids the run, exit 2); (2) `battery.py` on the Release binary with the reps, lanes, `--timeout` and every `--set`, table → `<dir>/battery/battery.log`, rep-1 cross-engine report → `<dir>/battery/<leg>.diff.out`; (3) the NEW probe legs leg by leg, each pair scored (`score.pair_words` under the mask) as soon as its images exist and the images deleted (unless `--keep`) → `<dir>/probe_words.json`; (4) `score.py … --probe-words` → `<dir>/score.json`, and `<dir>/SUMMARY.md` (guard line, battery table, per-leg first-divergent keys, per-pair words, the tuple). `--reps 1` passes `--reps-not-required` (a preview tuple, labelled). Bounded throughout (run_leg.py → run_bounded.sh). **P8.30-T:** `--pairing run\|frozen` (default `run`: probe_images.py derives each `pair_map` leg's scene edge from THIS binary's images and writes `<dir>/probes/<leg>/PAIRING.tsv`; the leg's `<leg>.diff.out` is computed AFTER the probe stage with that record; SUMMARY.md gains `## Scene pairing (per run)`), `--verify-edge2` (two one-image legs at the predicted pass-2 edge), `--classes` (DT-7: every scored pair carries `words_booked_console` / the `booked`/`unbooked` columns; `score.py` is called with `--probes` so the per-run notes reach the table). |
| `battery.py --set key=val` / `run_leg.py --set key=val` | The one-line toggles of DECISIONS_LOG's decision loop, repeatable, passed through unchanged to the NEW binary as `--set key=val` (order kept) — `battery.py` → `run_leg.py` → `nativeee`. NEW tree only: a legacy/v2 leg with `--set` is a usage error. A `--set` that differs from the deterministic default changes `env_ps2`, so battery.py's fingerprint cell reads `FP-MISMATCH` by design. Pinned by `battery.py --selftest` (`VerifyBatterySelftest`) and `run_leg.py --selftest` (`VerifyRunLegSelftest`). |
| `attribute_probe.py` | **The residual attribution ledger of one probe pair (P8.11-B, made trustworthy by P8.12-B).** `attribute_probe.py <old.ps2st> <new.ps2st> [--classes attribution/classes.tsv] [--mask …] --leg <leg> --boundary <b> [--watch watch.log] [--reloc] [--old-trace <OLD alloc log>] [--out ledger.json] [--unattributed N\|all] [--row-cap-words 64]`. Every RAM0 + SPAD word differing outside the mask (score.py's word set) gets ONE class, in this order: (1) the first matching exact rule of the classes TSV (`range \| writer-regex \| class \| row-or-DP \| kind \| legs`; a writer regex is tested against the writer a `--watch` WordWatch census names for the word — a sub-word store names its word — and never matches a word the census does not name; the `legs` column scopes a rule to legs); (2) with `--reloc`, the word's STRUCTURAL class: `relocate_compare.py`'s both-sided class (the title heap, record pool and the pool's live counter from the TSV's `@heap`/`@pool`/`@pool-used` directives), then `slab_walk.py`'s (the page pool, `@slabpool <addr> <classes>`), then `spad_stack.py`'s (the scratchpad stack, `@spad-fill <word>`), mapped to (ledger class, row, kind) ONLY by the TSV's `@reloc-class <structural class> <ledger class> <row> <kind> [legs]` directives — several directives may name the same structural class with disjoint leg scopes (one row per leg family, e.g. `reloc-content` → `P4-CKPT120-OLD-SESSION14-POPULATE-ALLOCS` on `coldboot` and → `P8-ATTRACT-OLD-COMPACTOR-STARVED` on `coldboot_attract`): they accumulate in file order and the first whose scope admits the leg books (P8.28-B; before, a later directive silently overwrote an earlier one for the same class); a structural class without an admitting directive is kind OPEN (`OPEN-RELOC-…` / `OPEN-SLAB-…` / `OPEN-SPAD-…`); (3) the first matching MIXED region rule; (4) `UNATTRIBUTED` (every such word is in the JSON; the text lists `--unattributed` of them). Honesty rule: a ROW/DP rule wider than `--row-cap-words` (64) with no writer regex and no `recorded:<record>` token in its row column is demoted to MIXED at load time and reported (a region rule without a recorded word is never ROW). Kinds: `ROW` (a §7.1 OLD-defect row), `DP` (decided, no row yet), `OPEN` (an OPEN decision), `MIXED` (a named region several classes write), `NONSTATE` (the DT-3 column: words PROVED not to be guest state — dead stack, free allocator residue — reported on its own, never folded into attributed, never excluded; a directive may book a structural class to it only once DT-3 is decided). Verdict per pair: `ATTRIBUTED-100%` only when every word is ROW/DP — the re-baseline rule's condition; otherwise `BLOCKED` (or `IDENTICAL`); `not_attributed` = MIXED + UNATTRIBUTED is printed next to the kinds. `--selftest` = ctest `VerifyAttributeSelftest` (the rule order, the demotion, a NEW-only record classed OPEN, the directive, two leg-scoped directives on one class). The 24-pair ledger of gate S6's probe set is in DECISIONS_LOG's LEDGER section. **DT-7 (P8.30-T):** a ROW rule or `@reloc-class` directive may carry `proof=console:<raw EE image>@<leg>:b<N>`; with `--boundary N` the ledger prints `DT-7: booked_console=K (… console == OLD n, console == neither m, unreadable u) -> words_unbooked=…` — a word counts only on the proof's pair and only where the console image's word == NEW ≠ OLD (`console_booking`); `booked_console_only()` is the fast path gate_run.py uses per pair (0 at once without a proved rule; only the proved exact rules' ranges are attributed, in the full rule order). The FILED condition is enforced: `load_rules` REFUSES a `proof=console:` token whose row id (the first token of the row column / the directive's row) is not a FILED §7.1 row per `diff_legs.known_row_ids()` — nothing is booked from an unfiled, proposed or mistyped id. |
| `relocate_compare.py` | **Relocation-aware equality over the title's allocators (P8.11-B; free descriptors, idx matching and both-sided classing by P8.12-B).** `relocate_compare.py <old.ps2st> <new.ps2st> --heap <addr> --pool <addr> [--pool-used <addr>] [--pool-head auto] [--old-trace <OLD [ALLOC-TRACE] log>] [--mask …] [--out reloc.json] [--min-sim 0.9]`. Walks, in BOTH images, the title heap (header: +0 free head, +4/+8 arena, +0xc/+0x10 counts, +0x14/+0x18 bytes; blocks = contiguous 16-byte headers `next-free, prev-free, prev-block, size \| bit31 FREE`; the walk must meet the header's counts) and the record pool (descriptors in the META arena, 0x40 bytes: `+0 handle idx, +4 buf, +8 size, +0x10 free link, +0x14 prev, +0x18 next, +0x1c flags`; address-ordered list from the header's +0x50 word; the free-list heads at +0xc..+0x48 and the free count at +0x4c). The pool walk honours the allocator's OWN state: flags bit 0 = FREE (must equal the free-list-reachable set and the +0x4c count), bit 2 = PENDING (released, not yet free-listed), and with `--pool-used` the used count must equal the live-record counter — free/pending chunks are never matched units. Heap blocks are matched by (size, pointer-insensitive fingerprint) → unmoved → ≥ `--min-sim`; pool records FIRST by the handle idx (same idx + size + bytes equal under relocation), THEN the leftovers by content (`rehandled` = matched in the CONTENT pass; split by the handle test — `rehandled-same-idx`: the SAME handle on both sides, pass 1's exact equality failed on in-place differing words and the record only MOVED (compaction); `rehandled-other-idx`: ANOTHER handle = the allocation ORDER differs. On the attract leg 1029 of the 1108 content-pass records keep their handle (P8.27-B); the JSON's `census.pool.rehandled_same_idx` / `rehandled_other_idx`, the `rehandled_list` entries' `old_idx`/`new_idx`/`same_idx`, the layout/`--blocks` class column and the word sub-counts `words.reloc-rehandled-same-idx` / `-other-idx` carry the split; the WORD class stays `reloc-rehandled` so the `@reloc-class` directives are unchanged); same idx with another size = UNMATCHED (both-sided, never 'old-only'); what no pass pairs = UNPAIRED. Every differing outside-mask RAM0 word is classified from BOTH sides (OLD[x] must be found at its relocated place AND NEW[x] must be a relocated copy): `reloc-content`, `reloc-rehandled`, `reloc-pointer`, `old-only-record`/`new-only-record`/`only-records-both` (pool), `heap-old-only-block`/`heap-new-only-block`, `unmatched-record`, `unpaired-record`, `free-residue`, `genuine` (a matched record's word no relocation explains — listed), `other` (outside both arenas). Reports the census, the FIRST layout divergence in address order (with the OLD allocation seq/frame from `--old-trace`), the side-status matrix. `--selftest` = ctest `VerifyAttributeRelocateSelftest` (29 checks incl. a free descriptor is not a unit, the counter check, a NEW-only record over a moved record, an unmatched same-idx pair). **P8.26-B (the asset heap = this record pool, base `0xf59ac0`, 16.6 MiB — the title's COMPACTING handle heap):** `--layout` adds to the JSON the pool's per-record table (both sides, address order: rank, lo, size, handle idx, match class idx-equal / rehandled / unmatched / unpaired / only / free, counterpart, delta, name), the ORDER analysis over the matched records (the longest common address order on both sides, the first record out of order) and the free-chunk census per side (count, bytes, largest, used span — the compactor's progress); `--blocks <tsv>` writes that table; `--names <names.tsv>` (from `asset_names.py`) names a record holding a whole archive file at its start (the title's loader streams SUB-chunks of its package files into records, so most records name nothing: identity is then (package, offset) by content search). Render lines `pool free chunks: …`, `pool ORDER over the N matched records …`. |
| `asset_names.py` | **A content-fingerprint name table for the title's packed asset archive (P8.26-B).** `asset_names.py --index <archive index> --archive <archive> --out names.tsv [--probe 64]`: the index = a u32 header word then `{u32 name_len, name, u32 byte offset, u32 byte size}` entries to EOF (contiguous extents, checked against the archive's size); one line per file `<sha1 of the first 64 bytes>\tname\toffset\tsize\tflag` (`short` / `ambiguous` flagged, never used for naming). The archive paths are title data on the command line. `--selftest` = ctest `VerifyAssetNamesSelftest`. |
| `slab_walk.py` | **The title's PAGE POOL (slab allocator) walked in both images (P8.13-B).** `slab_walk.py <old.ps2st> <new.ps2st> --pool <header> --classes <n> [--arena lo-hi …] [--mask …] [--out slab.json]`. Header (`+4` descriptors, `+8` u16 class-per-page table, `+0xc` pages, `+0x10..+0x2c` the free-page stack over the descriptor array, `+0x30+cls*4` the class-list heads), 32-byte page descriptors (`+0` page / free-stack link, `+8` chunk size, `+0xc` free-chunk head, `+0x10` chunks per page, `+0x14` free chunks, `+0x18/+0x1c` list prev/next) — the layout the title's own allocator code defines (`FUN_00204d48` init, `FUN_00204c38` free, `FUN_00202458` alloc, list primitives `FUN_00204dc0/00204df0/00205670/002057e0`). The walk asserts the allocator's invariants on each image (free stack == header count, every page on exactly one list, class table == list, free-chunk list == descriptor count). Word classes: `slab-header`, `slab-class-table`, `slab-desc-free` / `slab-desc-state`, `slab-free-link` (free on both sides and the FIRST word of a free chunk on either: the page's free-chunk list link — allocator STATE, never non-state; P8.14-B), `slab-free-residue` (free page or free-chunk BODY on BOTH sides), `slab-alloc-differs` (free on one side, used on the other, or another class), `slab-reloc-equal` (both used, EACH side's chunk content exists as a used chunk of the other image), `slab-reloc-partial` (one side's does), `slab-genuine` (neither). `attribute_probe.py --reloc` runs it from the TSV's `@slabpool <addr> <classes>` directive AND passes the `@heap`/`@pool` arenas to the chunk fingerprints — standalone without `--arena` the reloc-equal/partial split differs slightly (cb120: 5,385/837 vs 5,407/815; P8.13-B review MINOR 5). `--selftest` = ctest `VerifySlabWalkSelftest`. |
| `spad_stack.py` | **The scratchpad as the main thread's stack: the dead-stack proof with its per-pair conditions (P8.13-B, corrected P8.14-B/P8.15-B).** `spad_stack.py <old.ps2st> <new.ps2st> --fill 0xA1B2C3D4 [--text lo-hi] [--decoder-ctx off:val,...] [--decoder-text lo-hi]`: every THRD record's saved `$sp` and chain on both images; the 14-bit scratchpad mirror; the live frames bounded by the crt0 fill; conditions C1 (boundary `$sp` in the scratchpad, equal), C2 (no other stack in the scratchpad), C3 (no decode in flight: IPU DMA ch3/ch4 idle on both images AND no saved frame of ANY thread inside the libmpeg+libipu band -- fail-closed: no DMAC section or no band -> `spad-sp-unproved`; the decoder contexts with scratchpad work buffers reported with their state words); classes `spad-dead-below-sp` / `spad-live-frame` / `spad-decoder-work` / `spad-sp-unproved`; `--selftest`. |
| `attribute_audio.py` | **The audio lane's first divergent window per leg and its cause class BY AUDIBILITY (P8.15-B; the audibility split P8.17-B).** `attribute_audio.py --leg L --old-ahash F --new-ahash F [...] --pair N=old.ps2st,new.ps2st ... [--anchor-vf V] [--heartbeat-row ROW]`: the first differing 4800-frame window (6 EE boundaries) with the NEW reps' agreement checked; per image pair (SNDC voices incl. volume/envelope + the IOP sound module's voice records in IOPM) every differing voice (type/countdown/phase/nax/ssa/pitch/adsr2/vol_l/vol_r/env) is classed INAUDIBLE — `bookkeeping` (SNDC identical, only the record differs; `heartbeat-shape` noted), `save-instant-kon-confirmed/-unconfirmed` (one side's just-processed key-on, env 0, confirmed only when the NEXT pair shows the other side playing it), `record-only` — or ORDER (P8.18-B; HASH-RELEVANT, never NONSTATE) — `realloc-permutation` (the same sound on another slot of the other image: the lane hashes the raw float PCM and the mixer sums the voices in slot order in float, so a slot permutation changes the window hash while being perceptually equivalent; the gate kind `ORDER` blocks like OPEN and the verdict is `OPEN order (permutation N …)`, the candidate cause observed AT the pair) — or AUDIBLE — `one-sided-audible` (`heartbeat-shape` when it is DP-20 (a)'s pattern), `restore-phase` (inside W0), `content`, `cross-slot-content` (the same sample keyed on another slot with another pitch/position), `other`; the verdict is derived from the AUDIBLE subset only (`candidate` = every audible voice a heartbeat-shaped one-sided voice; empty audible subset with permutations → `OPEN order`, without → `OPEN-unobserved`; else `OPEN …`), and the JSON carries `rebaseline_gate` (by_kind ROW/OPEN/ORDER/NONSTATE/UNATTRIBUTED + rows) and the window's `old_window_hash`/`new_window_hash` (the binding `apply_rebaseline.py` checks). Heartbeat beats are MEASURED (NEW's `iop_hb_beats` from the rep's `run.meta`; OLD's = I8.9-B's measurement) and the pair labels CHECKED (`--anchor-vf`: the OLD offset consistent across the leg, NEW `vf == label`). The frozen OLD probe `old_b<N>` is a boundary N-1 voice image (pair it with NEW `state_b<N-1>`). `--onset`: the coldboot first non-silent window on each side and the content compared aligned by the edge. `--selftest` = ctest `VerifyAttributeAudioSelftest`. |
| `slab_census.py` | **The page pool's differing objects joined with NEW's allocation census (P8.17-B).** `slab_census.py <old.ps2st> <new.ps2st> --watch <watch.log> --pool <hdr> --classes <n> --window <boundary> --alloc-site <re> --free-site <re> --page-init-site <re> [--census-from B] [--arena …] [--out census.json]`: the census is a diag leg armed with `--watch-word` on every page descriptor's free-chunk head (`desc+0xc`) + the header's heads/counts; a store there is a POP (chunk alloc), a PUSH (chunk free) or a page init, told by the allocator site (the regexes = title data; the census records no caller above the allocator). Every differing word of the pool's pages (slab_walk's classes) gets its NEW-side history inside the window: `slab-new-progress` (NEW allocated it: boundary + site), `slab-new-only-inherited`, `slab-old-held-new-freed` (OLD still holds what NEW FREED at boundary b: the DP-19/DP-20 domain), `slab-old-only-unobserved` (OLD's allocation, invisible to NEW's census in `[census-from, window]`), `slab-page-reassigned` (P8.18-B: the page's NEW init event — the pool INIT routine's `0x0 -> first chunk` store, `init_from_zero` — and its TRIGGER, the first pop on the page at/after the init with its size class), and slab_walk's content classes with the alloc event named. `slab-old-held-new-freed` chunks are split `same-object` / `address-only` by the chunk body after the link word (P8.18-B). A descriptor store whose site matches none of the three regexes is counted `other` AND reported (`unmatched_sites`) — never silent. Per-boundary histograms, the triggers histogram and per-class chunk lists. `--selftest` = ctest `VerifySlabCensusSelftest`. |
| `console_compare.py` | **The CONSOLE as the arbiter between the two trees over the title's allocators (P8.29-B, DECISIONS_LOG LEDGER "console arbitration").** `console_compare.py <console> <old.ps2st> <new.ps2st> --slabpool <hdr> --classes <n> --pool <hdr> [--pool-used <addr>] [--heap <hdr>] [--moment a,b,…] [--bootsm <addr>] [--word addr[:bytes] …] [--arena lo-hi …] [--meta-routed 0x20] [--fp-limit bytes] [--out json]`. `<console>` is a raw 32 MB EE RAM image (PCSX2 `eeMemory.bin`, e.g. `int_work/p828a/p2s_ee.py` out of a `.p2s`) or a `.ps2st`. The same walkers as `slab_walk.py` / `relocate_compare.py` run on all three sides through ONE word accessor that resolves the record pool's descriptors in RAM (the console keeps them on the shared page pool) or in the META section (the trees route them to the META arena, HOOK row 0x181560) — so the record pool, the page pool and the title heap are compared by CONTENT (pointer-insensitive fingerprints over the WHOLE object — `--fp-limit` caps it and the output then says CAPPED — with ONE pointer-arena list applied on every side: the union of the sides' heap/pool/slab arenas plus the META range whenever a side carries a META section — its span is the images' META section length (a `.ps2st` writes the whole arena), never a constant of the tool — so a raw data word in the META range tokenises the same on the console and the trees), never by address. Reports: the matched-moment words (record counter, boot-SM case), heap block counts + three-way block match, per slab class the in-use pages / used chunks per side and the three-way object sets (`all / con+old / con+new / con-only / old-only / new-only / old+new`; the class holding the console's routed nodes flagged by `--meta-routed`), the PAGE verdict over every page whose class differs between OLD and NEW (`con==new / con==old / con-neither / con-free`, weighted by the page's OLD≠NEW words), the record census per side (descriptors in RAM vs META), the three-way record match and each tree's one-sided records with their presence on the console, plain words/byte ranges. `--selftest` = ctest `VerifyConsoleCompareSelftest` (20 checks: RAM/META descriptor resolution, the live-counter check, the META span derived from the images' META section, OLD's extra record found on the console, a META-range DATA word shared by all sides still matches (one arena list), a record equal in its first 4 KB and differing beyond does NOT match uncapped and DOES under `--fp-limit 4096` (stated CAPPED), the reassigned page classed NEW's, the routed-node class flagged, refusals). |
| `rebaseline.py` / `apply_rebaseline.py` | **The re-baseline manifest and the effective reference (DECISIONS_LOG DT-4, PROPOSED; P8.17-B).** The frozen OLD files (`frames.fhash`/`audio.ahash`/`mem.mhash`/`run.meta`/`probe/*`) are NEVER edited. A window re-baselined from NEW is a ROW of `baseline/<leg>/REBASELINE.tsv` (tab-separated: `lane window rows commit proof old_line new_line image note`; window `b<N>` mem / `W<N>` audio / `k<N>` frames) with its artefacts under `baseline/<leg>/rebased/` (the proof ledger copy; for mem the NEW probe image `new_b<N>.ps2st`). READERS: `diff_legs.py` replaces the re-based window's lane line by the manifest's NEW line before comparing and prints `<lane>: rebased mem b120 (ROW-IDS) in <leg>: frozen '…' -> effective '…' (commit, proof)` + `, rebased: mem b120 (ROW-IDS)` on the verdict line; `score.py` compares the probe pair against `rebased/new_b<N>.ps2st` and prints `REBASED (rows; commit)` on the pair + a `rebased_pairs=N: …` line (JSON `rebased_pairs`); `gate_run.py` writes a `## Re-based windows` section into SUMMARY.md and tags the scored pair; `battery.py` appends `rebased=…` to the leg's note. A manifest that does not parse is a usage error. WRITER: `apply_rebaseline.py --leg L --lane mem\|audio\|frames --window b120 --rows R1,R2 --ledger <ledger.json> --source <NEW leg dir> [--image …] [--commit sha] [--dry-run]` REFUSES (exit 3, writes nothing) unless the ledger (attribute_probe's `summary.by_kind` / attribute_audio's `rebaseline_gate`) names the leg + window and reads **0 OPEN / 0 MIXED / 0 UNATTRIBUTED** with ≥ 1 word (ROW/DP + NONSTATE only), every booked row is in `--rows`, every `--rows` id heads a §7.1 row of the docs, the NEW source (`run_leg.py` dir with `<lane file>`, or the probe set's `f<N>_<lane file>`) carries the window's line (+ image) — or, for a mem boundary with NO lane line on either side (the anchors' b2), the image alone (an IMAGE-ONLY window: manifest `old_line`/`new_line` = `-`, the image is the probe pair's reference, the lane lines untouched; P8.18-B) — the PROOF IS BOUND TO THE ARTEFACTS (P8.18-B): mem → the ledger's `old_md5`/`new_md5`/`old_vsync_frame`/`new_vsync_frame` must equal the frozen probe's and the `--image`'s, and the image's state hash the NEW line's; audio → the frozen line's hash = the ledger's `old_window_hash`, the source line's = `new_window_hash`; the ledger must name the leg (fail-closed); ORDER words block like OPEN; and no row exists for the window yet; on acceptance it writes the TSV row (`commit` = `git describe --always --dirty`; the note carries the binding digests and the source's `build_id`) + `rebased/` copies, hashes the frozen files before and after and fails (exit 4) if any changed. Selftests = ctests `VerifyRebaselineSelftest`, `VerifyApplyRebaselineSelftest`, `VerifyGateRunSelftest` (+ the diff_legs/score/battery selftests' effective-reference checks). |
| `fmv_plane_check.py` | The FMV plane check (DP-12 (a)); see the `--fmv-pictures` row. Exit 0 only when every movie's line is bit-exact (`strips_exact=n/n`); the file is written either way so the NOT PAIRED reason is on record. |
| `selftest.py` | The §4 admissibility gate: each leg run `--reps` times (sequentially), reps diffed; prints ADMISSIBLE / NONDET(first divergence) / ERROR per leg. `--anchors @file` or comma list; `--coldboot-frames` overrides the coldboot leg length. |
| `leg_spec.py` / `games/<t>/verify/baseline/<leg>/LEG.tsv` | **The per-leg run contract (P8.22-T, DECISIONS_LOG DT-5).** One header + one value row, tab-separated: `frames` (OLD `--bench-frames` = NEW `--frames`), `state_every` (the NEW probe leg's `--state-every`), `timeout_s` (the leg's wall bound — a caller's `--timeout` never lowers it: the larger wins), `stall_s` (the progress watchdog, `run_leg.py --stall-s` → `RB_STALL_S`), `anchor` (`coldboot` or a repo-relative `save_states/<x>.ps2st`), `probes` (`B[@F]` list: the probe pair boundaries, each a multiple of 60 — proved by the `mem.mhash` line — and of `state_every`; `@F` = the vsync-frame count the OLD probe run needs to pass boundary B+1), `notes`. Read by `battery.py` (frames, timeout, stall, anchor), `probe_images.py` (plans: OLD one run per probe `--frames F --state-every B-1` → `state_b<B-1>` = boundary B; NEW one run `--frames frames --state-every state_every` keeping the probe boundaries' images; bounds; anchor) and through them `gate_run.py`; `score.py` enumerates whatever `probe/old_b<N>.ps2st` exist. The twelve original legs have no LEG.tsv and behave exactly as before; a malformed file is a `LegSpecError`, never a default. Only `coldboot_attract` carries one today (6000 frames, cadence 60, 3000 s, stall 600 s, probes `1500@2100,2700@3300,3780@4400,4200@4800,5400`; plus the optional `pair_map` / `vf_offset` columns of the `clock_pair.py` row). `python3 verify/leg_spec.py` = the selftest (ctest `VerifyLegSpecSelftest`). |
| `scene_pairing.py` / `<probe dir>/<leg>/PAIRING.tsv` | **The scene edge derived PER RUN (P8.30-T; DECISIONS_LOG DT-6 per-run status).** The frozen LEG.tsv `pair_map` is the REFERENCE: any NEW change that moves the load timeline moves the edge and the frozen offset goes stale (P8.29-A's ring cache: edge new_b1133 → new_b1048, offset +283 → +198). `probe_images.py --pairing run` runs one census leg `--frames <frozen NEW edge + margin> --state-every 1`, reads every `state_b<N>.ps2st` for the two `SCENE_KEYS.tsv` words AS IT LANDS (the harness renames `.tmp` → final, so a file that exists is complete) and deletes it, finds the first case-7 boundary whose DENSE predecessor b − 1 is present and reads case 6 (`find_edge`; a refused/unreadable image at the edge → no edge, never one a boundary late; `census_gaps` names missing boundaries on the log line), predicts the pass-2 edge at the same offset (`--verify-edge2` checks it with two one-image legs: VERIFIED / EDGE2-MISMATCH) and writes `PAIRING.tsv` (`old_edge new_edge offset ref_old ref_new ref_offset deviation status method census_frames edge_words edge2_old edge2_new edge2_status pair_map`). Consumers: `score.py` (`new_boundary` = B + the run's offset; note `paired by scene edge (run: old_b850=new_b1048, offset +198; frozen reference +283, deviation -85)`), `diff_legs.py --pairing <dir\|file>` (the lanes from the run's edge; verdict `scene-paired: offset +198 from old_b850=new_b1048 (run; frozen reference +283, deviation -85)`), `gate_run.py` (SUMMARY section). Cost ≈ 0.25 s per boundary of image I/O (≈ 5 min to b1283). The zero-cost exact method is an app/host census field `boot_sm_case7_b=` (specified in DT-6's per-run status; not built). `--selftest` = ctest `VerifyScenePairingSelftest` (20 checks incl. a synthetic writer process). |
| `clock_pair.py` / LEG.tsv `pair_map` + `vf_offset` | **Scene-aligned pairing of a leg whose two timelines differ by a rowed OLD timing defect (P8.25-T, DECISIONS_LOG DT-6; `coldboot_attract`).** `clock_pair.py --keys <leg>/SCENE_KEYS.tsv --read <img>…` / `--census <NEW leg dir> [--out census.tsv]` read the title clock word (u32) and the boot-SM word (case = bits 44..49, bit 63 = the cutscene flag) from `.ps2st` images — their guest addresses are title data in the leg's `SCENE_KEYS.tsv` (`clock_word` / `boot_sm_word`), never in the tool (LintEnginePurity). Measured law on BOTH trees while the title runs: the clock advances **+149 per EE boundary** (+2048 periodically every ~500–600 boundaries, +4 across the attract movie/reload); through the boot FMVs and the Beach load it runs at each tree's own pace (OLD's wall-pumped boot leaves its title-entry value at ≈1.0 M where NEW's is ≈0.36 M), so equal clock VALUES never meet inside one leg — `--pair` tries the exact match and prints `exact-clock match: none (NEW census range lo..hi)`. The pairing key is therefore the **scene edge**: the boot-SM case 6 → 7 entry (the Beach load's completion; cutscene pass 1 starts; after the attract's reload the same edge starts pass 2) on each side, located on OLD by its `[BOOT-SM-EDGE]` log line (boundary = vf − `vf_offset`) and on NEW by a `--state-every 1` census around the 60-cadence bracket; an OLD probe at boundary B pairs with NEW's `B + (new_edge − old_edge)` of the last edge ≤ B. `clock_pair.py --pair --old-probes <probe dir> --new-census <leg dir|census.tsv> --edges old_b<E>=new_b<E'>[,…] [--new-images <dir>]` prints per probe the offset, both clock values, the scene words (case + bit 63 must agree: `SCENE-MISMATCH` exits 1), the clock residual against the 149/boundary law (the 2048-jump phase, reported) and the `pair_map=` line. That line is the LEG.tsv's optional 8th column **`pair_map`** (`old_b<E>=new_b<E'>,old_b<B>=new_b<N>,…` — the edge FIRST, then every probe) with the 9th **`vf_offset`** (OLD's `vsync_frame − boundary` once its FMVs coalesced: 530). Consumers: `probe_images.py` takes the NEW image of probe B at the MAPPED boundary N (`new_b<N>.ps2st`, named by the NEW boundary; off-cadence N → its own `--frames N+1 --state-every N` run — with `coldboot_attract`'s map no mapped boundary is on the 60 cadence, so a gate run of this leg costs five one-image legs ≈ 19,000 Release frames ≈ 12–15 min instead of one 6000-frame run); `score.py` pairs `old_b<B>` with `new_b<N>` and prints `new_b<N>: paired by scene edge old_b<E>=new_b<E'> (offset +N)` on the pair (JSON `new_boundary`/`pairing`); `diff_legs.py` pairs the lanes from the edges by the same offset (frames: legacy keys from E + vf_offset with NEW keys from E' + 1; audio: windows from E // 6 with E' // 6, the edges' sub-window residual noted; mem: only when the offset is a multiple of 60, else the post-edge lines are UNPAIRED and named — the probe pairs carry the lane), keys below the LOWER edge by key as before, a side's keys between the lower edge and its own edge UNPAIRED (`keys … are unpaired (N; its load-duration keys below its edge …)`), and every verdict line ends `…, scene-paired: offset +N from old_b<E>=new_b<E'>`. `--selftest` = ctest `VerifyClockPairSelftest`; the consumers' selftests carry pair_map cases. The leg's frames against the console itself: ctest `RootAttractCutsceneFrames` (`tests/app/root_attract_cutscene_frames.py` — the four cutscene dumps' screenshots under `baseline/coldboot_attract/gsdumps/`, absolute 12/channel on six region means + the caption's bright share; `gsdumps/PROVENANCE.md`). |
| `profile_deterministic.py` | The OLD profile env dict (shared import; var-by-var rationale: `PROFILE.md`) and `NEW_DETERMINISTIC_FINGERPRINT`, READ from `docs/filemap/settings_table.tsv`'s `# deterministic_fingerprint=` header (generated by `tools/gen_settings_table.py`; the same single source `tests/core/test_config*.cpp` use). |

## Leg artifacts (written by the binary when `PS2_VERIFY_OUT=<dir>`)

| File | Content |
|---|---|
| `frames.fhash` | one `vf,hash` line per present (OLD) / per boundary (NEW; key = the original's vsync-frame convention, see the frames-lane row above): FNV-1a64 of the native-scale scanout (native dims header + RGB top-down; at scale S>1 the native image is point-sampled from each SxS block's top-left texel — scale 1 is the bit-identity contract). |
| `audio.ahash` | one `window,hash` line per 100 ms of mixer output (4800 stereo frames @48 kHz), pumped tick-clocked at the SyncV boundary. |
| `mem.mhash` | one `boundary,hash` line per 60 frame boundaries (internal counter covering BOTH boundary sites, sceGsSyncV + WaitForVblank): FNV over main RAM (32 MB) + scratchpad (16 KB) + HLE meta arena (2 MB). |
| `run.meta` | build id, GL renderer, tree, profile, sorted `PS2_*` fingerprint + hash, presents, wall ms, finalized flag. Asserted by `diff_legs.py` before hashes are compared. NEW adds its censuses, among them `fmv_pictures_served=<n>` and `fmv_window=<first>-<last>|none` (the FMV window's boundaries, consumed by `--fmv-pictures`). |
| `fmv_plane_check.txt` | (NEW leg, written by `verify/fmv_plane_check.py`, never by the binary) one `movie<N> boundary=<b> plane=0x<addr> picture=<k> strips_exact=<n>/<total>` line per movie — the evidence `--fmv-pictures` needs to declare the FMV window PAIRED-BY-PICTURE. |
| `input.padlog` | (with `PS2_VERIFY_RECORD_INPUT=1`) served pad state as PS2_PAD_SCRIPT-grammar ranges keyed by the GAME FRAME (the `frames.fhash` key of the served boundary; header `clock=frame`; a header without `clock=` is the tick clock of the earlier recordings) — directly replayable via `--inputs` / `PS2_PAD_SCRIPT_FILE`. |
| `memory_b<N>.bin` | NEW diagnostic RAM captured at `--state-every K` boundaries, immediately after hashing and before save preparation. Exactly 32 MiB, atomically published. This is **not a runtime save**. `memory_capture.py` reads it for game-data invariants; scene pairing reads its clock/state words and removes both memory and state artifacts as it harvests. |
| `state_b<N>.ps2st` | Runtime snapshot at a requested boundary, only when every owner can save safely. Pending library work may refuse it; `state_b<N>.refused` records the owner/reason while the memory capture remains available. NEW labels boundary N; OLD labels boundary N+1. Existing full-state attribution tools still require a real snapshot and must report its absence, not substitute RAM for complete state. Capture write failures now fail the run instead of silently dropping evidence. |

## Typical use

**DT-12 is the default retirement policy.** PCSX2 dumps, pins, captures and
comparisons are excluded from acceptance in this wave. Their existing tests
are unchanged. Retirement requires same-input NEW model/replacement oracles,
all 13 scenarios with three stable frame/audio repetitions and first-divergent
keys unchanged or improved against the frozen OLD reference, zero executions
of the replaced **model operation**, and OLD-bug pins exercised on the
replacement. Memory remains advisory under DT-11.

The runner does not enter the historical 29-probe/console-attribution pipeline.
It never substitutes missing OLD baselines, incomplete batteries or missing
evidence with a passing result. Original baselines and attribution rules are
unchanged. `RootPlayPolicyInert` keeps its advisory memory comparison.

Capture the **unfiltered** CTest inventory after building. Run the same preset
with the exact DT-12 exclusions, using a fresh log. These commands are for the
parent's integration window; do not run the suite alongside another game run:

```sh
python3 verify/retirement_scope.py                  # names + source files
python3 verify/retirement_scope.py --regex          # exact -E expression
ctest --preset macos-dev --show-only=json-v1 > /work/ctest-inventory.json
ctest --preset macos-dev --timeout 300 -j8 \
  -E "$(python3 verify/retirement_scope.py --regex)" > /work/ctest.log 2>&1
python3 verify/retirement_scope.py --inventory /work/ctest-inventory.json
```

The current exclusion list has 20 names: the ten title/attract rendering pins,
`Texsrc_TitleAtlasDecode`, `LoaderRingCache`, `RootLoaderRingCache`,
`RootMetaArenaOff`, `RootTypeRegistryComplete`, `Vu0ShadowReplay`, `Vu0VisMicro`,
`Vu0DecalEval`, `VuMacroBridge`, and `IopSysmemHeap`. This includes tests with
embedded console answer keys. Synthetic parser/writer tests and NEW comparisons
remain selected, including `RootReleaseLanesIdentical`, `VuCachedSuperblock`,
`Render_AttractReplaySelftest`, and the scene-pairing selftests.

The verifier checks exact test names and numbers against that inventory,
complete progress, a single matching summary, and `Passed` for every selected
enabled test with an executable body. A skipped executable test fails acceptance.
Four pre-existing declarations are reported as unavailable: the three named
`Gs_Inv25`/`26`/`30` skip-stub registrations and CoreDrawlistRecorder when the
inventory contains its exact diagnostics-disabled placeholder command. Their
source commands are checked; an actual compiled DrawlistRecorder must pass.
They cannot supply OLD-bug or self-oracle evidence and do not become passing
tests. This does not certify the deferred interpolation feature.
Tests configured
`DISABLED` in the inventory must appear as disabled, never count as passing
OLD-bug or oracle tests. Concatenated logs, selected subsets, passing counts
without test results, and excluded console results are rejected. A newly
registered command that directly names a PCSX2 capture or console-image
argument requires an explicit scope review.

Prepare the evidence contract below before collecting the battery:

```sh
# Fresh battery; default native build, no --set overrides. CTest was run separately.
python3 verify/gate_run.py --retirement \
  --release /work/bin/nativeee-release --out /work/gate \
  --ctest-log /work/ctest.log --ctest-inventory /work/ctest-inventory.json \
  --retirement-evidence /work/evidence.json

# Consume an already collected battery without launching games or tests.
python3 verify/gate_run.py --retirement --evaluate-only --out /work/gate \
  --ctest-log /work/ctest.log --ctest-inventory /work/ctest-inventory.json \
  --retirement-evidence /work/evidence.json

# Standalone evaluator, also defaulting to DT-12.
python3 verify/retirement_gate.py --evidence /work/evidence.json \
  --battery /work/gate/battery --ctest-log /work/ctest.log \
  --ctest-inventory /work/ctest-inventory.json --json /work/retirement.json
```

`gate_run.py` checks the binary/source fingerprint before and after collecting
a fresh battery, then writes `retirement-run.json` into every completed run.
`--evaluate-only` requires these receipts; it does not retrospectively invent
them. A parent's separate trusted collector can write the same receipt around
its own runs. The receipt schema is:

```json
{
  "schema": "dt12-battery-run-v1",
  "binary_sha256": "<candidate executable hash>",
  "source_sha256": "<source manifest hash>",
  "artifacts": {
    "run.meta": "<hash>", "frames.fhash": "<hash>", "audio.ahash": "<hash>"
  }
}
```

The verifier reads and hashes the actual files; it checks their completeness,
configuration/build identity, independent run artifacts and stable lane
values, then recomputes first-divergent keys with `diff_legs`. It does not trust
`score.json`, the battery's summary, or candidate-supplied comparison overrides.
A failed or interrupted invocation replaces any previous acceptance report
with FAIL. The detailed verdict is `retirement.json`; exit zero means PASS.

### DT-12 evidence producer contract

All artifact references below are `{"path": "...", "sha256": "<64 lowercase hex>"}`.
Paths are absolute or relative to the evidence manifest, including references
inside receipts. Source references are
`{"file": <artifact>, "needle": "<short exact search text>"}`. The gate reads
the source, checks the needle and its authority directory, and binds the hash.
No decomp or OLD implementation is copied into the verifier.

`build.sources` references a `dt12-sources-v1` JSON object:
`{"schema":"dt12-sources-v1","root":"/absolute/source/tree",
"files":{"relative/source.cpp":"<sha256>", "...":"..."}}`.
Every listed file must still match. Include the relevant implementation,
counter increments/publication, defaults and test sources. The parent owns
the trustworthy build/source manifest and artifact collection.

An evidence manifest has this structure (angle-bracket objects below are
placeholders to replace with artifact references, not valid acceptance data):

```json
{
  "schema": "dt12-retirement-v1",
  "authorities": {"decomp": "/absolute/read-only/decomp", "old": "/absolute/frozen/OLD"},
  "build": {"binary": "<artifact>", "sources": "<artifact>"},
  "ctest": {
    "inventory": "<unfiltered CTest JSON artifact>",
    "log": "<selected full-suite log artifact>",
    "source_sha256": "<same source manifest hash>"
  },
  "replacements": [{
    "id": "MOVIE-PICTURE",
    "intent": "Decode the movie picture requested by the player",
    "source_intents": ["<decomp source reference>"],
    "toggle": {
      "name": "movie.decode", "model": "off", "native": "on", "default": "native",
      "source": "<implementation reference to the one-line default>"
    },
    "model_counters": ["IPU.command.idec"],
    "native_counter": {
      "field": "movie_decoder", "member": "pictures",
      "source": "<implementation reference to completed work>"
    },
    "oracles": ["movie-reopen"],
    "old_bug_pins": [{
      "id": "BUG-XXX",
      "record": "<OLD loop record or refactor docs/bugs reference>",
      "test": "MovieReplacementOracle", "oracle": "movie-reopen"
    }]
  }],
  "oracles": [{
    "id": "movie-reopen",
    "test": "MovieReplacementOracle",
    "executable": "<actual CTest command[0] artifact>",
    "model": {
      "input": "<owned input bytes artifact>",
      "receipt": "<model receipt artifact>",
      "outputs": {"picture": "<model output bytes artifact>"}
    },
    "native": {
      "input": "<same owned input bytes artifact>",
      "receipt": "<native receipt artifact>",
      "outputs": {"picture": "<native output bytes artifact>"}
    },
    "attributions": {}
  }],
  "pairings": {}
}
```

Each oracle is an actual test in the selected passing CTest scope. Its two
receipts have the following shape; both outputs and receipts must be separate
regular files, not symlink/hardlink aliases:

```json
{
  "schema": "dt12-oracle-run-v1",
  "mode": "native",
  "test": "MovieReplacementOracle",
  "command": ["/absolute/path/to/test_executable", "--both-paths"],
  "binary_sha256": "<oracle executable hash>",
  "source_sha256": "<candidate source manifest hash>",
  "input_sha256": "<hash of actual owned input bytes consumed>",
  "exit_code": 0,
  "completed": true,
  "settings": {"movie.decode": "on", "shared": "deterministic"},
  "outputs": {"picture": "<actual output bytes hash>"},
  "counters": {"ipu_commands": "idec=0", "movie_decoder": "pictures=3"}
}
```

The model receipt changes `mode`, the replacement toggle, measured counters,
and output hashes as appropriate. All other settings and owned input bytes
must agree. The test executes both paths inside NEW; no console oracle is
accepted. Output channels may be canonical draw IR, vertices, PCM or file
bytes. Each side must produce observable output. Inputs and outputs are read
and compared byte for byte, including missing tails. The producer must record
the inputs actually consumed and counters actually measured, not expectations.

Any difference requires an entry under `attributions.<channel>` containing
`ranges` (the complete exact list of half-open differing byte intervals),
`reason`, a decomp `specification` source reference, `expected` output artifact
matching the native result, and a passing `pin` test name. Omitted, partial,
spurious or unbound differences fail before a native default can be accepted.
The parent reviews the specification/expected-result attribution; an arbitrary
expected file and a sentence do not establish semantic correctness by themselves.

`retirement_counters.py` is the **reviewed counter catalog**, outside candidate
evidence. Each ID names its exact `run.meta` field/member, the model's actual
increment and its publication source. Unsupported/fallback/error counts and
wrapper totals cannot be renamed into a model-execution claim. The initial
catalog covers IPU command dispatches, scratchpad DMA starts, and specific
Thbase operations. These are narrow operations, not proof that their entire
models retired. GIF/VIF/GS/VU/STREAM retirement needs additional reviewed
execution instrumentation/catalog entries from the implementation owner.

A title descriptor may name the reviewed `DispatchTable.CountCalls` provider.
Its title-local `source`/`increment` identifies the entry and atomic-cell binding;
the verifier additionally requires the actual core increment/delegation source
in the build manifest and byte-identical to the reviewed implementation.
The catalog cannot supply arbitrary provider paths or new provider definitions.
This preserves original continuation proof without treating a wrapper total,
fallback or unsupported count as executed model work.

Every listed model count must be present and zero in **all 39 battery runs**.
The model side of the self-oracles must actually exercise every claimed
operation across those cases; the native side must have zero model work and
positive completed replacement work. The native work counter must reproduce
across each leg's three runs and be positive somewhere in the battery.
Legs with zero native work are reported explicitly as unexercised, not used as
coverage claims. An entirely unobserved replacement cannot pass by reporting
zeros. Each OLD-bug pin must link its source record to one of these executed
replacement oracles and the same passing CTest test. Completeness of the
subsystem's OLD-bug inventory remains a parent review responsibility.

Scene-mapped legs also require `pairings.<leg>` with `record` (actual
`PAIRING.tsv` artifact), `run_meta` (completed NEW census run), candidate
`binary_sha256` and `source_sha256`, `before` and `after` raw RAM capture
artifacts, plus `before_boundary` and `after_boundary`. Retain only the adjacent
case-6/case-7 captures at the measured edge; `memory_b<N>.bin` is supported.
Their clock/state words, boundary labels and pairing offsets are verified.
The second edge may be explicitly unclaimed or predicted; a `VERIFIED` claim
needs a future additional proof contract and is currently rejected. A frozen
offset or a copied pairing report without the captured edge cannot stand in
for measurement. This small scene proof is required for the deciding lane
alignment; the 29 legacy memory probes and `.ps2st` device snapshots are not.

Digests establish integrity and bind evidence to the parent's collection.
They cannot attest that a malicious evidence producer ran a program. The
parent supplies and reviews the absolute `authorities.decomp` and
`authorities.old` reference roots, collection, full inventory, counter sites,
toggle and OLD-bug coverage. A `PASS`/skip count in a report never replaces
those artifacts.

### Historical DT-11 evaluation

DT-11 remains explicitly available for old records:

```sh
python3 verify/retirement_gate.py --policy dt11 \
  --score /old-gate/score.json --battery /old-gate/battery \
  --ctest-log /old-gate/full-ctest.log --json /work/dt11-history.json
```

The historical full runner is `gate_run.py --retirement --retirement-policy
dt11 ...`; it retains its old probe/pin requirements and must not be used for
this wave. DT-12 reports those requirements as historical, not run, and not
acceptance. Synthetic DT-11 parser fixtures stay in `--selftest`; they do not
load PCSX2 artifacts. `python3 verify/retirement_gate.py --selftest` also runs
the DT-12 adversarial fixtures in `tests_retirement_dt12.py`.

```sh
# admissibility self-test over the anchor battery (both binaries):
python3 selftest.py --binary $REPO/build/host_app/host_app \
    --anchors @anchors.txt --frames 600 --coldboot-frames 1500 \
    --reps 2 --tree legacy --out /tmp/p0/legacy

# one cross-tree comparison (legacy vs v2):
python3 run_leg.py --binary $REPO/build/host_app/host_app    --anchor S --frames 600 --tree legacy --out /tmp/x/legacy
python3 run_leg.py --binary $REPO/build-v2/host_app/host_app --anchor S --frames 600 --tree v2     --out /tmp/x/v2
python3 diff_legs.py /tmp/x/legacy /tmp/x/v2 --allow-build-diff

# record a human session as a replayable leg:
PS2_VERIFY_OUT=/tmp/rec PS2_VERIFY_RECORD_INPUT=1 $REPO/build/host_app/host_app
python3 run_leg.py ... --inputs /tmp/rec/input.padlog
```

Phase-0 status, self-test table and the nondeterminism findings:
`../docs/phases/PHASE0_IMPL.md`. Profile rationale: `PROFILE.md`.
All legs run mods-off by construction (no `--mods` exists yet; the
EQUIVALENCE REV2 (D6) runner assertion becomes real at MIGRATION Phase 11).
