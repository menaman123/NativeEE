# BUG-031 — The LIVE T0 read path interpolated COUNT from raw wall time; PS2_T0_SUBTICK never covered it

| Field | Content |
|---|---|
| **ID** | BUG-031 |
| **Title** | Game master clock's low bits came from `steady_clock` wall ns: the deterministic profile's PS2_T0_SUBTICK=0 pin only gates the default-OFF PS2_EE_TIM0 path, not the live memory.cpp reconstruction |
| **User-visible symptom** | None in play (that's the point — it IS the wall clock). Harness-visible: every paired deterministic leg's memory lane diverged at checkpoint 0 through the game's per-frame delta-seconds (`0x3098C4`) and its accumulators (`0x3723AC` family), with LSB echoes in VU0D/SPAD/VRAM and the parked main thread's saved `f11` (THRD @0x9E8). |
| **Scene / savestate** | `save_states/ps2state_slot0_f46203_red_crystal.ps2st`, any paired ≥8-vf profiled leg; amplified by PS2_VERIFY_STATE_EVERY save stalls. |
| **Subsystem** | ee-timing |
| **Root cause** | The live T0_COUNT read at memory.cpp `address==0x10000000` computes `cur_syncv*TICKS_PER_FRAME + carry + sub_ticks` where `sub_ticks = (steady_clock now − per-SyncV anchor)/16667us × 9600` — raw wall time. The A4 §5.1 nine-clock row "Timer sub-tick | off" was a coverage error: `PS2_T0_SUBTICK` exists only in ee_timers.cpp (`PS2_EE_TIM0` path, default OFF). The game's clock feeder `FUN_001b74a0` (from main loop `FUN_00100d78`) turns consecutive reads into delta-seconds at `0x3098C4`; `FUN_001b6b40` accumulates them per object. |
| **PS2 rule violated** | Timing contract of the deterministic profile (EQUIVALENCE_PLAN §3): every game-readable clock must be a pure function of the EE stream under the profile. T0 on hardware is BUSCLK/256 — never a host wall proxy. |
| **Fix + hatch** | `PS2_T0_TICKPACED` (default OFF = legacy verbatim; profile sets 1): the returned COUNT zeroes the sub-frame term — base + busy-wait carry only. The busy-wait carry promotion (loads) keeps its legacy >1s wall hysteresis (unreachable in paced anchor windows). Ledger: SETTING. |
| **Regression test** | Paired [DETW] watch legs on `0x3098C4`: delta stream bit-identical across reps (was [3,2,1] vs [2,3,1] tick binning). Battery f46203 first frame divergence deepened line 3 → 397. |
| **Source record** | `Ps2Recomp/host_app/memory.cpp` (T0 branch comments), PHASE0_IMPL.md §9.2. |
| **Triage tips** | When a pin "should" cover a clock, grep for the getenv — a gate that lives in a default-OFF sibling path covers nothing. Frame-fraction-shaped floats in RAM diffs = go find the delta-seconds feeder and watch its input, not its consumers. |
