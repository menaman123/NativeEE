# F8 restore LIVE, in-session — design (doc only; Bernoulli, STANDING CHARTER track 3a, 2026-10-11)

Status: IMPLEMENTED (2026-10-11, port-product, after Herschel's M11 service-only startup landed on
main as b07c419d). F8 restores LIVE by default (`save.restore_mode=live`); the relaunch of
`app/host/session_relaunch.*` is the fallback by setting or when the composition retains IOP state.
Section 5 records what was built against this design and where the implementation deviated.

## 1. What exists today (read before changing anything)

The live transaction is already composed and bound behind the root opt-in `harness.live_restore`
(`engine/core/config/config.h`, CLI-only `--set`; files refuse it):

| piece | where | role |
|---|---|---|
| `SessionRestore` | `app/host/session_restore.{h,cpp}` | the F8 transaction over the real registry: `PrepareFile` (admission + unpublished preparation) on the completed game owner, `RestartPrepared` on the joined supervisor; output epoch (`VerifyHost::BeginOutputSegment`), recording segment, `SessionClock` rebase, a paused restore's resume gate. |
| `SessionRestart` | `app/host/session_restart.*`, `app/gui/SESSION_RESTART_INTEGRATION.md` | the hook order (`prepare_image` → `join_workers_and_writers` → `begin_epoch` → `apply_image` → `reset_after_restore`), epoch validation, guest restart on the restored boundary thread. |
| `SessionRestore::Lifecycle` (root) | `app/host/ee_bootstrap_leg.cpp` ~L130–170 | `inspect` (observational, refuses by name), `prepare` (`GraphicsApplication::CanRestore`), `joined` (capture writer join + `SessionAudio::StopForRestore`), `resumed` (frame rebase, `audio_pumped_tick`, live input edges, `SessionAudio::ResumeAfterRestore`, `running`, the pacer restart). |
| `StateRestorer` | `engine/ee/savestate/` | `PrepareImage` (every owner's preflight against INCOMING state, nothing mutated) → `RestorePrepared`. `SetPreRestore` (`ee_bootstrap_sections.cpp` ~L244) runs the owners' `CanCheckpoint`s, `graphics_output.ResetAfterQuiescence()`, `domain.ReAnchor`. |
| `SessionRelaunch` | `app/host/session_relaunch.*` | the composed F8 of today (relaunch). Bound only when `!harness.live_restore`. |
| `StartupOwnership` | `app/host/startup_ownership.*` | the M11 reading of `title::ServiceStartup::Report`: which retained IOP modules / the IOP heap still have a consumer. `Refusal()` is the named list a live restore quotes. |
| `SessionAudio` | `app/host/session_audio.*` | stop/resume of the SDL device around a restore (close on main, `AudioPullRing::DiscardStopped`, reopen + `Prime`). |
| `GraphicsApplication::CanRestore` / `StopAfterQuiescence`, `GraphicsOutput::StopAfterQuiescence`, `SettleOwnedForCapture` | `app/host/graphics_application.cpp`, `graphics_output.cpp`; INTERFACES.md §13 | the owned Graphics closure; the capture-time settle that admits an in-play boundary (GXPD digest). |

The `inspect` refusals that stop a live F8 today, in order (`ee_bootstrap_leg.cpp`):

1. `irx_boot.entry_ran || !startup_ownership.Owned()` → `StartupOwnership::Refusal()` — STREAM.IRX,
   CDVDSTM.IRX, LIBSD.IRX, SDRDRV.IRX and the IOP heap retained: the IOP's running state (its
   kernel threads, the streamer's ring, SDR's voice state) has no section and no rewind.
   **Herschel's 3a removes this one** (service-only startup: the modules are never loaded, the
   native services own the state and already have sections: `service_restore`).
2. `mtvu || EeEventQueue::Instance().HasPending() || rpc_router.PendingCount() || delivery_meter.Pending()`
   → "A remaining asynchronous owner has no approved live-restore contract." — **all four are
   live in the all-on play composition** (`mtvu=on,worker=1`; the RPC router and the delivery
   meter carry the native services' in-flight calls; the EE event queue the kernel's deferred
   events). This is the second gate and it is Bernoulli's.
3. `input_save.CanCheckpoint && movie_checkpoint.CanCheckpoint && service_restore.CanCheckpoint`
   — already the F5 predicates; they admit at an ordinary boundary.

So after Herschel's stage the live F8 is still refused at (2). This design is about (2) and about
what the three process-level owners the charter names — the composition settle, the GL device,
the audio sink — must do on a LIVE session.

## 2. The restore point and what "live" means

The restore point is the one F5 uses: an admitted `sceGsSyncV` boundary (INTERFACES §13: `Present`
→ `WaitIdle` → `SyncV`; the frame's owned groups Recorded behind their predecessors, not yet
Linked). A live F8 therefore starts on the GAME OWNER (the boundary thread, inside the Savestate
slot of `BoundarySequencer`) with the same quiescence the capture proved, and must end with the
same process — window, GL context, audio device, pacer, recorder — running the restored title
from `restored_vsync_frame + 1` without a relaunch. Everything below is organised by who owns what
and whether that owner can run its restore path while the process is live.

### 2.1 Owners that can run on a live session TODAY (no change)

- **The EE/kernel image** (`StateRestorer`: RAM, scratchpad, the meta arena, THRD, the kernel
  sections, the DMAC/VIF/GIF/GS register models): `PrepareImage` validates against the incoming
  bytes, `RestorePrepared` applies them after `join_workers_and_writers` joined every guest
  thread (`SessionWorkers::Join` → `ThreadManager::JoinAll`). Pinned by `RootPlaySaveRestore`,
  `RootPlaySaveAtTitle` through `--load-state`; the live path differs only in WHO holds the
  threads (the joined supervisor instead of a fresh process) — `SessionRestart` already models
  that (`on_supervisor`, `validate_epoch`, `reset_after_restore`).
- **Input/Save** (`InputSave`): `CanCheckpoint` admits at a boundary; `Restore(state, boundary)`
  keeps the game-save DIRECTORY untouched (reproduction files never roll it back — the user's
  card is not part of the image) and rebases its polling keys from the restored `SessionClock`.
  Live: identical; `lifecycle_.resumed` re-applies `Pause` and resets live-input edges.
- **Movie** (`MovieCheckpoint`): captures refuse during a movie (`fmv_active`), so a restored
  point is never inside one; the decoder is reopened from the image's idle context
  (`MpegApi::PrepareIdle`/`PublishIdle`). Live: identical to `--load-state`.
- **Native services** (`ServiceRestore`: the stream/sound/file services' owned state): already
  the owner of record for `service_restore.CanCheckpoint`; live-capable once the IOP modules are
  not loaded (Herschel).
- **The recorder / output lanes** (`VerifyHost::BeginOutputSegment`): a new `output-NNNNNN/`
  with its own `frames.fhash`/`audio.ahash`/`mem.mhash`/`input.padlog` (the recording's new
  segment header `# restore_tick=… previous_segment=…` then the padlog header; the frame clock
  continues at `segment_restore_frame + 1` through `PresentKey`). Live-capable; pinned by the
  component test `SessionRestoreComposition` (two rewinds while recording).
- **The clock** (`SessionClock` rebase; `TickThread` stopped by `SessionWorkers::Join` and
  restarted by `resumed`; `EmuDomain::ReAnchor(tick_source.RawCount())` in the pre-restore hook).
  Live-capable. One rule to add: the pacer's `TickSync` pending notes must be CLEARED at the
  rebase (a tick noted while the workers were joining would otherwise fire as a "late" field
  into the restored title's first boundary — DT-14 rule 1 says surplus is never converted into
  ticks). `TickThread::Stop` then `Start` already drops the thread; the sync's pending count
  must be reset in the same place (`protocol.NotifyTick` consumers see zero).

### 2.2 The composition settle — what must change

The settle (`GraphicsOutput::SettleOwnedForCapture`) proves nothing is in flight at the boundary:
consumer drained, every sealed submission delivered, the pending owned set == the bridge's open
recording, and publishes that set as GXPD. For a CAPTURE that is enough: the image carries the
digest and the restoring process attributes the first field's missing groups. For a LIVE restore
the same process keeps living objects that the image does not describe, and each must be put
into the state a fresh `--load-state` process would have at the same point:

| live object | fresh-process state | live action (owner) |
|---|---|---|
| the bridge's OPEN recording (groups/items Recorded, not Linked) | empty: the restored session draws its first field without those groups (`graphics_restore_unlinked`, detail-5 ordering refusals) | `GraphicsOutput::StopAfterQuiescence`'s body minus the stop: cancel Pending owned batches (`State::Cancelled`, `owned_pending_ → 0`), `service_->ResetAfterQuiescence()` (invalidates deferred bridge callbacks), `environment_.ResetAfterQuiescence()`. Today that routine also sets `stopped_ = true` and is final (destructor path), and the pre-restore hook's `GraphicsOutput::ResetAfterQuiescence` (`graphics_output.cpp` L447) THROWS `logic_error("Graphics reset before owned output drained")` when `owned_pending_ != 0` — which is exactly the settled in-play boundary (pending set == open recording, so pending > 0). A live F8 at a title/gameplay boundary would therefore fault in the pre-restore hook today. It needs a **`ResetForRestore()`** sibling: cancel the Pending batches as the stop does, reset service/environment, keep the output usable; the pre-restore hook calls it when the settle admitted the boundary (`UnlinkedAtBoundary().items > 0`), the throwing form stays for closed boundaries. Also `RequireClosedRecording` (L440; used by the GFRM restore spec, L480/L503, "a restoring session must have a closed recording") must accept the settled state on the RESTORING live session — the open groups are cancelled by the reset before the image applies, so the recording IS closed by then; the order (reset before `apply_image`) is the contract. |
| producer tickets (sprites/text/sky; shadow/distortion passes in Ready) | none | each application's `StopAfterQuiescence` has the drain; the live form is the same drain without `stopped_`: `ShadowApplication`/`DistortionApplication`/`EffectsApplication::ResetForRestore` (the passes' Ready batches are discarded — they were one of the open recording's groups and are attributed with it). |
| the MTVU worker (`MtvuStack`) | started empty before any producer traffic | `Quiesce` (already registered as the `mtvu` predicate before `graphics`) proves the pipeline + arbiter empty; a live restore then needs `MtvuStack::ResetForRestore()`: drop the VU1 program/data mirrors the worker holds (they are re-uploaded by the restored title's first VIF1 traffic — the image carries VU memory), reset the arbiter's sequence, keep the thread. This is **gate (2)'s `mtvu` term** and it is a contract to write with Socrates (the worker is his): the predicate exists; the reset does not. |
| the Texture/Asset owners (`TextureAssets`, the load-time texture bindings) | rebuilt from the restored image's RAM (`TextureBindings` "pure preflight against INCOMING RAM") | already restore-capable through `PrepareImage` (the preflight) — live needs the same code path and the GL texture objects of the OLD session released on the consumer thread BEFORE the new bindings upload (see 2.3). |
| the Graphics scene targets (GSCN v3: RGBA8 attachments of the title's display buffers) | restored from the image | `GraphicsScene::RestoreState` writes the attachments; live: the same, on the consumer thread with the device current. |

Order on the game owner (`SessionRestart::PrepareFile` → `prepare_image`): `GraphicsApplication::
CanRestore` (strict closed-recording rule through `RequireClosedRecording` **is wrong for live** —
the live boundary is admitted by the SETTLE, exactly as F5's capture was; `CanRestore` must take the
settle's answer, i.e. `CanCheckpoint(reason)` of the output with the unlinked digest, and the
restore then attributes the open groups as `--load-state` does). That is one line in
`GraphicsApplication::CanRestore` plus the `graphics_restore_unlinked` census emitted from the live
path too.

### 2.3 The GL device — what must change

Facts: one `VideoStack` owns the window, the GL context, `GlDevice`, the scene and the VRAM image
(`video_stack.h`). The DrawIR consumer thread (`DrawIrThreadQueue`, `gs.queue=thread`,
`Policy::Pipelined` in play) is the context's owner while running (`EnsureCurrent` re-binds when
the calling thread changes; `ContextRebinds()` counts it). The presenter on main has its own
context and receives only completed display pixels (`SetSessionOutput`). SDL/Cocoa window geometry
is main-thread only; deferred resizes (`OnWindowResizedOffMain` → `ApplyPendingWindowResize` at
the frame boundary; the pipelined-GS deadlock fix ff12dca8).

A fresh `--load-state` process creates the device, uploads VRAM from the image, warms the programs,
then runs. The live session must reach the same device state WITHOUT recreating the window or the
context (that is the whole point: no relaunch, no black window, the user's fullscreen/scale kept):

1. **Drain, do not stop, the consumer.** `hooks.stopped` today does `gs_queue->Stop()` (joins the
   thread: final). The live path needs `DrawIrThreadQueue::Sync()` (drain to empty — the settle
   already requires it) and then runs the restore's device work THROUGH the consumer
   (`RunOrdered`), never from the supervisor thread: the context stays with its owner, no rebind
   storm, no "context current on two threads". `GraphicsOutput::StopAfterQuiescence` already shows
   the shape (`consumer_->RunOrdered(release)` with `EnsureCurrent` + `ClearMovieFrame`).
2. **Pipelined pending frame.** Rule 2's one-pending-frame slot (`frame_waits=0`, drop the older)
   may hold a frame of the OLD session at the join: it must be DROPPED (counted
   `frames_dropped`, never presented — a presented stale frame after the restore is a user-visible
   wrong image). `DrawIrThreadQueue` needs `DiscardPending()` for the restore path (today only the
   producer's drop-the-older rule exists).
3. **VRAM.** The GS local-memory image is the model's section (the transfer engine's `VramImage`);
   `RestorePrepared` writes it. With `gs_queue_split_image=1` (play, GL) there are TWO images —
   the EE-side one the model writes and the pipeline's copy the consumer reads
   (`pipeline_vram`, the VRAM-image barrier of DP-25). The live restore must copy the restored
   EE image into the pipeline image on the consumer thread and bump `UploadEpoch` so the texture
   cache/decode tiers and the target cache invalidate everything (a fresh process has them
   empty). Owner: Zeno (the pipelined GS stage); the hook is one `RunOrdered` call.
4. **Render targets / texture cache.** Every cached target and decoded texture belongs to the
   old session's VRAM. Invalidate by epoch (3) — do not delete and recreate GL objects per
   target: the GL device, programs and the window-sized present framebuffer (the capture fix
   334086fb) are session-independent and stay.
5. **Window geometry / scale.** Unchanged by a restore (the slot's `.meta composition=` must
   equal the running `hle.*` keys + profile; `video.scale` is part of the fingerprint for the
   frames lane but the WINDOW size is the user's — kept). A deferred resize pending at the join
   is applied at the first restored boundary as today.
6. **The presenter.** Receives the first restored display pixels like any frame; nothing to do.
   The "one field without the owned groups" attribution shows for 1/60 s as it does after
   `--load-state` (INTERFACES §13).

Gate (2)'s `mtvu` term is the only one of the four that touches the GL device: with
`MtvuStack::ResetForRestore` (2.2) and the drain-not-stop above, it is satisfied.

### 2.4 The audio sink — what must change

`SessionAudio` already models the live stop/resume: `StopForRestore` closes the SDL device on main
and `AudioPullRing::DiscardStopped()` drops the obsolete buffered output; `ResumeAfterRestore`
re-`Prime`s the ring (the clock contract's standing lead, rule 4) and reopens the device paused
or running. What is missing is on the MIXER side, which the image owns:

- the SPU2/Mixer sections restore the voice state and the external-input (movie PCM) gate; the
  `audio_pumped_tick` is re-anchored in `resumed` (done); the field pump (`audio_field_pump`)
  then catches up from the restored domain tick — correct.
- the **hashed audio lane** restarts in the new output segment with a fresh `AudioHasher`
  (`BeginOutputSegment` does this); the DEVICE ring must not receive the boundary-pump quantum of
  the join boundary twice: `push_audio_ring` is called from the Verify slot of the boundary that
  ADMITTED the restore (before the join) — then the ring is discarded — so nothing is double
  pushed. Keep the order (`joined` after the boundary's Verify slot completed; `SessionRestart`
  guarantees it by running `join_workers_and_writers` on the supervisor).
- the **movie sound path** (`movie_sound.StateOwner()`, the Movie PCM sink under `hle.audio_mix`)
  has no in-flight PCM at an admitted boundary (captures during a movie are refused), so its
  restore is the owner's section; nothing live-specific.
- **Pause state**: a restore pressed while paused stays paused before the first restored
  instruction (the resume gate in `SessionRestore`); the device reopens paused
  (`ResumeAfterRestore(session->Paused())`). Done today.

Conclusion: the audio sink is live-capable now; its only coupling is the ordering above.

### 2.5 Gate (2): the three other asynchronous owners

| owner | why it refuses | the live contract |
|---|---|---|
| `EeEventQueue::HasPending()` | kernel events (alarms, deferred wakeups) queued for a later boundary are not in a section | at an admitted SyncV boundary the queue is normally empty (the Savestate slot runs after `EventDrain`); make the predicate exact (`HasPending()` is already the check) and, when non-empty, REFUSE BY NAME (count + first event's kind) — never drop them. No new section: a boundary with pending events is simply not admissible, the user presses F8 again. |
| `rpc_router.PendingCount()` | native services' in-flight calls (the title's EE→service RPCs) | after Herschel's stage every call completes within the boundary's `MaintDuties` (`rpc_router.CompletePending`) — pending at the Savestate slot means a call spanning boundaries; refuse by name (the server id). The services' own state is `service_restore`'s. |
| `delivery_meter.Pending()` | modelled disc deliveries due at a later tick | the delivery meter's due list is a pure function of the loader model's state, which IS a section (`LoaderModel`); on restore the meter is rebuilt from the restored model (`DeliveryMeter::RebuildFrom(loader_model)` — to add) instead of refusing. Owner: Herschel (files/stream). |
| `mtvu` | see 2.2/2.3 | `MtvuStack::ResetForRestore` after `Quiesce` — Socrates. |

## 3. The staged plan (3a-Bernoulli, after Herschel's 3a lands)

1. **Predicate exactness** (no behaviour change for the user): split gate (2) into the four named
   refusals above (`live restore refused: mtvu worker holds state` / `ee events pending=N` /
   `rpc pending=N (server …)` / `disc deliveries pending=N`); run.meta `live_restore_refusals=`
   census. Pin: unit on the lifecycle lambda (a fake composition per term).
2. **DeliveryMeter rebuild + RPC completion at the admitted boundary** (with Herschel): the two
   service terms stop refusing at an ordinary boundary.
3. **Graphics live reset** (with Lagrange/Zeno): `GraphicsOutput::ResetForRestore`,
   `Effects/Shadow/DistortionApplication::ResetForRestore`, `DrawIrThreadQueue::DiscardPending`,
   the pipeline-VRAM copy + epoch bump through `RunOrdered`; `GraphicsApplication::CanRestore`
   admits by the settle. Pin: the component test with a fake consumer; `RootPlayLiveRestore`
   leg below.
4. **MTVU live reset** (with Socrates): `MtvuStack::ResetForRestore`.
5. **Flip**: `hooks.stopped` no longer `gs_queue->Stop()`s on the restore path (only on close);
   `SessionRestore::Bind(hooks, true)` in play when `startup_ownership.Owned()`; `SessionRelaunch`
   remains the fallback bound when the composition is NOT owned (the refusal names why the
   relaunch was taken). `harness.live_restore` stops being an opt-in and becomes the census
   word `f8=live|relaunch`.
6. **Root pin `RootPlayLiveRestore`** (red-before = today's refusal text): a play-profile window
   session, `--set harness.save_at_frame=1400` (F5 at a title boundary) then
   `--set harness.restore_at_frame=1600` (the F8 path), `--frames 1900`: ONE process
   (`run.meta` of the session has `output_next=` the restored segment; no `restores/` directory;
   the PID unchanged), the restored segment `output_restore_frame=1400`, `frames=300`,
   `restored_vsync_frame=1400`, `park=none`, `graphics_restore_unlinked` present, `frames_dropped`
   ≥ the pending frame at the join, `audio_ring dropped=0 underruns=0` AFTER the resume (the
   clock-contract census per window), the game_hz windows after the restore within 59.5–60.5
   (DT-14 invariant), and the restored segment's `frames.fhash` keys 1401… equal to a
   `--load-state slot1400 --frames 300` deterministic run's keys 1401… on the frames lane
   (identity between the live restore and the process restore of the same slot — the real proof
   that the live owners reached the fresh-process state). Deterministic-profile lanes of every
   existing leg unchanged (the live path is play-only).

## 4. Out of scope / refused on purpose

- No IOP rewind: the IOP never boots in play after 3a (Herschel); before that F8 relaunches.
- No synthetic section for the open graphics recording: the first restored field attributes it,
  as the process restore does (INTERFACES §13).
- No restore across compositions or profiles (the slot `.meta composition=` preflight stays).
- No GL context/window recreation: the device is session-independent; invalidation is by epoch.

## 5. What was built (2026-10-11) and where it deviates from the plan above

Pins: `LiveRestoreGate` (unit, 25 checks), `RootPlayLiveRestore` (the real binary: play, F5 at 300,
F8 at 600 live in the same window, then `--load-state` of the slot), `RootPlayRelaunchRestore`
(`save.restore_mode=relaunch`), `Coretest_config`. Evidence under
`int_work/m_series/input_saves/f8_live/` and the handoff `TRACK3A_F8LIVE.md`.

| plan item | what was built |
|---|---|
| 3.1 predicate exactness | `app/host/live_restore_gate.{h,cpp}`: the four named refusals + the owners' predicates; census `live_restore_refusals=`. The `mtvu` term is `MtvuStack::Quiesce` (the F5 predicate), not the worker's existence. The route (`f8=live` / `relaunch,setting` / `relaunch,fallback=startup_retained`) is `LiveRestoreGate::Choose`. |
| 3.2 DeliveryMeter rebuild / RPC completion | NOT needed: in the all-on play composition both are empty at every admitted boundary (`cdvd_deliveries=0/0`, `iop_pending=events=0,rpc=0` — the native services complete within the boundary's MaintDuties; the delivery meter is unused under native startup). They stay refusals by name; a `DeliveryMeter::RebuildFrom` was not written. |
| 3.3 Graphics live reset | `GraphicsOutput::ResetForRestore` (cancel the Pending owned batches of the open recording, reset service + environment, keep the output usable); the pre-restore hook takes it when the live settle recorded an open recording (`UnlinkedAtBoundary().items > 0`), the strict throwing reset otherwise (the fresh `--load-state` process). `GraphicsApplication::CanRestore` needed no change: `RequireClosedRecording` runs in the GFRM section's restore, AFTER the pre-restore hook closed the recording. `DrawIrThreadQueue::DiscardPending` was not needed: the settle's `Sync()` drained the queue and nothing is enqueued between the admitted boundary and the join. The pipeline-VRAM copy / epoch bump was not needed either: `GsState::RestoreLocalMemory` writes through the VRAM mirror (the op log replays on the pipeline image in stream order) and emits `InvalidateTextures` over the whole image. What WAS needed beyond the plan: the display's memory of the old session — the consumer's published grab and the present policy's held image — is forgotten at the join (`DrawIrQueueConsumer::ForgetLastFrame` on the consumer, `PresentPolicy::ForgetImage`); without it the first restored boundaries presented a picture of the previous session under load. |
| 3.4 MTVU live reset | `MtvuStack::MirrorVu1`: the sections' VU1C/VU1D/VU1R land on the inline unit; with the stack running the worker's unit is the VU1 the title drives, so the restored memory and register file are mirrored into it and its decoded image invalidated (census `mtvu=...,mirrors=`). Applies to the live F8 AND to `--load-state` with mtvu on (before this the worker ran with its previous program memory; the deterministic profile, mtvu off, is untouched). No arbiter/sequence reset was needed: `Quiesce` leaves the pipeline and the arbiter empty. |
| 3.5 flip | `save.restore_mode=live|relaunch` (default live) + `harness.live_restore` (the opt-in, kept). `hooks.stopped` needed no change (it only runs at close; the supervisor keeps running after a restore). The live route keeps the relaunch route's composition preflight (`SavestateSlots::PreflightComposition`). The restored segment's run.meta carries `restore_route=live restored_vsync_frame restore_mhash`. `save.restore_mode` is excluded from the composition fingerprint. |
| 3.6 Root pin | `RootPlayLiveRestore` at 300/600 (the charter's boundaries) instead of 1400/1600. The frames identity the plan asked for ("the restored segment's keys equal a `--load-state` run's") is NOT a property of play mode: two uninterrupted play runs of 300–600 agree on a minority of keys (frame-skip under rule 2, the FMV's wall-clocked decode), so the pin asserts the first restored frame (the uninterrupted run's picture, or the no-image field followed by it within five keys; never the previous session's last picture), `restore_mhash` identity with the process restore of the same slot, the digests, the segment's provenance and the clock-contract rows, and REPORTS the frame agreement for both routes. The deterministic identity stays `RootPlaySaveRestore`'s. |
| 2.1 pacer pending notes | not needed: the post-restore `pace_late_trace` shows no restore-induced late field (`TickThread::Stop`/`Start` around the join already drops the thread). |

One fix outside the plan: `GraphicsMovie::Dispatch` treated the boundary thread's `ThreadExitUnwind`
through Present's SyncV as a presentation fault (`failed_ = true`), so the first live F8 failed with
"Graphics Movie owns an active picture/presentation or failed output"; the unwind is now passed
through untouched.
