# BUG-019 — Cold boot renders 100 % black: the frame-budget emulation charged empty frames a full gameplay frame

| Field | Content |
|---|---|
| **ID** | BUG-019 |
| **Title** | [S112-BOOTPHASE] [S96]'s mode-1 synthetic budget base (8800 counts ≈ a full gameplay frame) starved the boot loader: every pump lap saw 0 headroom, so pump=0 KB, sceMpeg getpic=0, black blits forever |
| **User-visible symptom** | Cold boot (no savestate) = permanently black game viewport (0 FATAL, presents run, GUI visible); any savestate load works. 4 days of "passing" exit-code anchors over a black screen is this bug family's history (the black-boot lesson, A5 §4). |
| **Scene / savestate** | Cold boot, `--bench-frames 1500`, `PS2_BB_SNAP` luma series: every sampled present luma=0.00 under user env AND defaults. |
| **Subsystem** | ee-timing (primary), cdvd-loader |
| **Root cause** | `[BUDGET-EMU]` diag: every boot frame runs one `FUN_0017d6b8` pump lap = 4 budget queries; the mode-1 synthetic clock (now = anchor + base 8800 + reads×96) leaves rem 704/608/512 < thresholds 976/781/585 → fracA/B/C DENIED every frame. Base 8800 models a FULL gameplay frame ([BH13] tuned 8400-9700), but boot/load frames are EMPTY (a fullscreen blit + HUD ≈ 3-9 content jobs): real hardware reaches the end-of-frame pump with almost the whole 9600-count field remaining and grants reads in volume. Charging phantom counts starved the loader forever. |
| **PS2 rule violated** | The budget the game computes is derived from T0 counts actually consumed by the frame's work; an emulated budget must model the frame the game actually ran (content-dependent), not a fixed archetype. |
| **Fix + hatch** | `[S112-BOOTPHASE]` (generic, no boot special-case): classify each guest frame by its OWN render work (cumulative content-job census `g_draws_total`) and select the budget base accordingly — empty frames get an empty-frame base. Rides the `PS2_BUDGET_EMU` family (default ON). Post-refactor: the phase classifier is part of `DerivedClocks.budget_anchor` (ARCH §4.7); this record is also why the harness's cold-boot PIXEL gate exists (a luma gate on every leg). |
| **Regression test** | Cold-boot luma gate (nonblack fraction) on every harness leg — the structural fix for "passing anchors over a black screen"; boot leg on both binaries in the FOGCONTENT battery (12/12 anchors incl. cold boot). |
| **Source record** | `Ps2Recomp/docs/loop_state/BLACKBOOT_IMPL.md` ([BB1]-[BB3]), `CAVERN_BUDGET_IMPL.md` ([BH13]) |
| **Triage tips** | Black screen + zero FATALs + healthy presents = the game is drawing nothing, not failing to present: census content jobs per frame first. When an emulation model is TUNED on one workload (gameplay), ask what happens on the degenerate workload (empty frames) — fixed "typical" constants starve edge phases. Exit-code anchors are blind; always gate on pixels. |
