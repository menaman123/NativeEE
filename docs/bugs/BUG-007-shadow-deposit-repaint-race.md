# BUG-007 — Camera-motion shadow dropout: deferred deposit acquired AFTER its content was repainted

| Field | Content |
|---|---|
| **ID** | BUG-007 |
| **Title** | [SH7] Read-time acquisition of a pending RT deposit raced the same target's next display-list repaint — the flush deposited SATURATED alpha instead of the fog gradient |
| **User-visible symptom** | Shadows drop out under camera rotation: the surviving mask sliver tracks fresh rows and slides off the bottom edge → stretches of zero shadow (nz=0) while rotating; healthy when still. |
| **Scene / savestate** | `ps2state_bad_shadows.ps2st`, camera-sweep pad script (`RX=255` / `RX=1` sweeps around vf 11990-12350). |
| **Subsystem** | gs-vram (primary), gs-targets |
| **Root cause** | 0x70000's fog alpha is authored by END-OF-LIST 16-bit draws (CT16-family writes into the CT32 alpha bytes — the classic PSMCT16-into-PSMCT32 alpha alias trick). Their mirror deposit is Phase-B DEFERRED (`RecordPendingRTSync`); the pending entry {start=0x70000 size=0xe0000} is acquired lazily at the next masked-draw kPreDraw flush — but by then the NEXT display list's synthetic clear + opaque CT32 repaint have ALREADY rewritten the FBO. `SyncRTToVRAM` reads the repainted FBO and deposits saturated alpha (vram_a=205,068 vs healthy 72). The S9 stencil transfer finds no fog contrast; the DATE eraser wipes the mask. One-sentence defect: the r121/Phase-B contract "a flush deposits exactly the bytes the eager per-draw sync would have" is violated whenever the recording target's own next display list repaints before any consumer triggers the flush — read-time acquisition races content death. |
| **PS2 rule violated** | Deferred work must observe stream-order content: a deposit recorded at stream position P must contain the bytes as of P, no matter when it is physically executed. |
| **Fix + hatch** | Acquisition-before-repaint (`opengl_backend.cpp` DrawBatch, before the clear-boundary block): any non-16-bit draw into an fbp with a pending entry RECORDED BY THE SAME TARGET flushes that entry NOW, while the 16-bit-phase bytes are still in the FBO and `render_psm` still describes them. Cross-target entries stay on the demand path. Gate `PS2_SH7` (default ON; 0 = defect arm). Post-refactor: the VramImage StageDeposit/CommitDeposits bracket makes this structural (ARCH §4.3). |
| **Regression test** | 3 camera-sweep legs at defaults, ZERO=0 across 361-frame sweeps; corpus byte-identity; `PS2_GS_RESIDENT=0` control (eager deposits) as the reference arm. |
| **Source record** | `Ps2Recomp/docs/loop_state/PLAYTEST_R1_IMPL.md` (§ [SH6]+[SH7]) |
| **Triage tips** | "Correct when still, dies under camera motion" points at content-lifetime races, not math: motion raises repaint cadence and shrinks the window between record and acquire. For any lazily-acquired snapshot, ask "who can rewrite the source before the acquire?" — if the answer includes the recorder itself, acquire eagerly at the boundary where the content dies. |
