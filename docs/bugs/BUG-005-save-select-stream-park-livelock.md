# BUG-005 — Save-select livelock: stream voices stuck in pitch-0 park, NAX engine never ticks

| Field | Content |
|---|---|
| **ID** | BUG-005 |
| **Title** | Save-select fast-stop→reopen leaves STREAM.IRX voices parked at pitch 0; the NAX-paced request engine deadlocks (leader q=0, follower q=1 refused forever) |
| **User-visible symptom** | On the save-select screen (fast stop then reopen of the music stream), music dies and the load flow livelocks — the stream engine stops requesting data. |
| **Scene / savestate** | Save-select anchor state; reproduced in the LOADTIME ld4 stage legs. |
| **Subsystem** | iop-sound (primary), cdvd-loader |
| **Root cause** | After the stream module's cmd-46 (fast stop) → reopen sequence, the per-voice unpark state — ownership word 0x14a60, stored pitch 0x159de, staged blocks 0x13110+0x13170 — is not in its retail post-start pose. The stream voices therefore remain in the module's pitch-0 park, so the NAX-paced engine never ticks: the leader voice never requests (q=0) and the follower's q=1 is permanently refused. Root-caused by decompiling the recompiled STREAM.IRX against live voice-register dumps. |
| **PS2 rule violated** | SPU2/STREAM.IRX feed protocol: voice NAX progress paces the stream request engine; a stopped-then-reopened stream must restore the module's own post-start voice pose (ownership + pitch + staged buffers) exactly as the IOP-side code leaves it on retail. `[title-idiom]` — the protocol is TT's `lti_streamer`, but the lesson (voice pose drives request pacing) is SPU2-generic. |
| **Fix + hatch** | LOADTIME F3: restore the retail post-start unpark pose on the fast-stop→reopen path (per-voice ownership/pitch/staged state), letting the NAX engine tick. Landed with the LOADTIME fix family (hatched, default ON). Post-refactor home: games/<title>…/iop/stream_irx (the recompiled module + its glue own this state). |
| **Regression test** | Save-select leg: music resumes + load completes; sound ring hash lane (EQUIVALENCE §1.3) pins the mixed output on the anchor. |
| **Source record** | `Ps2Recomp/docs/loop_state/LOADTIME_IMPL.md` (ld4 root-cause §, "ROOT CAUSE: the stream voices remain in the module's pitch-0 park after cmd-46…"), `LOADTIME_REVIEW.md` |
| **Triage tips** | A stream that stops REQUESTING (not just playing) is paced by voice hardware state — dump NAX/pitch/ownership for the stream voices before touching the CD side. Fast-stop→restart transitions are the classic place where HLE'd module state diverges from the module's own idea of "started". |
