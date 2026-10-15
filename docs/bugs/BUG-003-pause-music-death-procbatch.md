# BUG-003 — Music never returns after a long pause-menu dwell (libsd ProcBatch dropped)

| Field | Content |
|---|---|
| **ID** | BUG-003 |
| **Title** | Pause-menu music death: the libsd batch-processing entry (stub 8) discarded the game's staged voice commands |
| **User-visible symptom** | Open the pause menu and stay ~26 s: music mutes (correct), but after exiting the menu the music NEVER returns — only a frozen SFX loop floor (constant rms 0.01651 repeated verbatim 40+ windows). |
| **Scene / savestate** | `save_states/…slot7_f36230` island state; pad script `36900-36920:START` (open), 1,560 vf dwell, `38460-38480:START` (exit). |
| **Subsystem** | iop-sound |
| **Root cause** | The game re-arms music after a menu dwell through libsd's batch entry (SdProcBatch-shape, iop_stubs.cpp `libsd_stub_8`). The stub dropped the batch entirely, so the module's own staged voice values (pitch/ownership/start pose) never flowed to the voices — voices left in `pit=0x0 own=0 sp=0xaaa sg=0xaaa` pose, byte-matching the user's dead state. |
| **PS2 rule violated** | libsd `SdProcBatch` semantics (SDK 3.0.x): each batch entry executes against the SPU2 voice/effect registers and stores per-entry results into `rets[i]` when non-NULL. A stock-Sony IOP module surface must be implemented to its documented API, not stubbed silently. |
| **Fix + hatch** | Implement the batch executor in `libsd_stub_8`: run each entry through the module's own dispatch, store `rets[i]`, skip unknown funcs, cap num at 512 — "nothing forced, nothing faked". Hatch `PS2_SND_PROCBATCH` (default ON; =0 reproduces the death, proven in legP2). Probe `[PBQ]` under `PS2_MUSQ=1`. Post-refactor home: iop/hle libsd module (engine — generic Sony surface). |
| **Regression test** | Scripted menu-dwell leg: rms returns to varying music band within ≤2 windows of exit; `[PBQ]` shows batch entries executing (e=0x200..0x206). Record tag `[MU1-MUSIC-DEATH s70]`. |
| **Source record** | `Ps2Recomp/docs/loop_state/PLAYTEST_R1_IMPL.md` (§ [MU1-MUSIC-DEATH]) |
| **Triage tips** | "Audio stuck on a loop after a mode change" is almost always a dropped or half-implemented sound-module command, not a mixer bug — dump the voice pose (pitch/ownership/staged regs) and byte-compare against a healthy run. If a stubbed IOP entry point "seems unused", prove it with a call census before leaving it a stub. |
