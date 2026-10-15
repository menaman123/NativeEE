# BUG-028 — In-game pause dead: a hand-written emitter dropped a jal delay slot

| Field | Content |
|---|---|
| **ID** | BUG-028 |
| **Title** | [S31-PAUSE] The hand-written recompiler special case for FUN_00177168 ("Main Dispatcher") misplaced the delay slot of the per-tick START-edge query — the edge test read the wrong object and returned 0 forever |
| **User-visible symptom** | Pressing Start in-game never pauses (and pause-menu resume dead); menus unaffected (they poll the pad through ordinary recompiled code). |
| **Scene / savestate** | Any gameplay scene; press Start. |
| **Subsystem** | recompiler |
| **Root cause** | Real MIPS: `jal 0x002B3548` with delay slot `lw a0, 0x34(s1)` — the call argument is the pad DEVICE pointer loaded in the slot. The hand emitter (recompiler.cpp `func.base_address == 0x00177168` special case) passed `a0 = s1` (the state-machine object itself) and performed the `+0x34` load AFTER the call. FUN_002b3548's rising-edge test (`(dev+0 cur & 1<<ctl) && !(dev+4 prev & 1<<ctl)`) then tested the SM word's own halves (bit22 always 0) and returned 0 unconditionally — the SM word0 bit31 START latch never armed. |
| **PS2 rule violated** | MIPS delay-slot semantics (DS1-DS7 of the recompiler contract): the delay-slot instruction executes BEFORE control transfers; arguments staged in the slot must be materialized before the callee reads them. |
| **Fix + hatch** | Correct the emission: load `a0 = *(s1+0x34)` before dispatching the call (delay slot sequenced with the transfer). No hatch — an emission correctness fix. Post-refactor: this whole class dies with D1 — the hand emitter is deleted (v2 general emission, oracle-verified: RECOMP_P3A covers FUN_00177168 with its 23-target table), and IR terminators carry their slot node explicitly with a verifier obligation (ARCH §5.1). |
| **Regression test** | Recompiler oracle delay-slot fixtures + the 18 legacy-divergence findings (RECOMP_P3A); golden-emission snapshots (Phase 8) pin the emitted shape; live: pause/resume works. |
| **Source record** | `Ps2Recomp/docs/loop_state/PAUSE_IMPL.md` |
| **Triage tips** | "Feature works in menus but not in gameplay" (or vice versa) = the two paths run DIFFERENT translations of the same primitive — diff the emission paths before the game logic. Any hand-maintained emitter special case is a delay-slot bug farm; re-verify against live disassembly, or better, delete the special case and let the general path emit it. |
