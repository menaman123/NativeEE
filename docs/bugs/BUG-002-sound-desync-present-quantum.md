# BUG-002 — Sound cues drift behind gameplay (loop admitted at the present grid, not the vblank quantum)

| Field | Content |
|---|---|
| **ID** | BUG-002 |
| **Title** | Sound desync: present-pacer sleep in the graphics consume loop back-pressured the EE loop onto a 35 Hz grid while vblank/T0/audio ran wall rate |
| **User-visible symptom** | Audio cues (jumps, hits, music stingers) lag progressively behind on-screen action; game feels "underwater" at ~35 fps while audio plays at full rate. |
| **Scene / savestate** | Any gameplay scene under the capped preset; measured with the `[P0-CUELAG]` probe (frames vs ticks vs device-audio slopes, `PS2_CUELAG=1`). |
| **Subsystem** | ee-timing (primary), gs-present |
| **Root cause** | The `[R193-PACE]` present-pacer slot sleep ran INLINE in the graphics thread's consume loop. Sleeping there delayed the next VSync job pop, which delayed the EE's depth-1 backpressure unpark — so the game loop was admitted at the ~35 Hz present grid while `g_vblank_count`, the T0 timer domain, and the audio device clock all ran at wall rate. Frames (the game's cue source) fell behind the clocks its audio rides. |
| **PS2 rule violated** | Loop admission on retail is the vblank: the game runs a 60 Hz quantum and slips to the next tick (30/s) when a frame overruns. Host present pacing must never gate the guest loop. |
| **Fix + hatch** | Mode Q quantum lock (TIMING_NATIVE P1): the consume path never sleeps; loop admission is only the two parks the EE already had — the gfx-caught-up depth bound and the edge-triggered 60 Hz `waitForNextVBlankTick`. `PS2_FPS_CAP` becomes present-side decimation (completed frames skipped, next due slot shows the newest complete picture; FMV bypass preserved via `Pacer::FmvBypassAllowed`). Hatch `PS2_QUANTUM` (default ON; =0 restores legacy exactly). Post-refactor home: ee/timing BoundaryProtocol + gs/hle/present pacing (ARCH §4.7); PresentPolicy "can never influence loop admission" is the codified rule. |
| **Regression test** | Cue-lag slopes ≈ 0 under the probe on the canonical play leg; `test_time_domain` + pace unit gates (MIGRATION Phase 7); the deterministic profile pins pacing so the harness would catch a reintroduction as a frame-hash timing shift. |
| **Source record** | `Ps2Recomp/docs/loop_state/TIMING_NATIVE_IMPL.md` (P0 cue-lag probe, P1 Mode Q), `TIMING_NATIVE_ROADMAP.md`, `TIMING_RESEARCH_*.md` |
| **Triage tips** | If audio drifts but never crackles (no underruns), the mixer is healthy — compare the FRAME rate against the TICK rate (`slope_tick`): frames slow + ticks nominal = loop-admission bug, not an audio bug. Grep for sleeps on the graphics consume path; any sleep there is a loop-admission gate in disguise. |
