# Fresh reproduction capture and live restore

Status: fresh capture, exact installed continuation proofs, stack truncation
detection, completed-service ownership and production release/restart guards
are implemented. The stage8 integration root binds F5 to these providers and
publishes actual writer results. Current APIs and supported shapes are in
`THREAD_CAPTURE_INTEGRATION.md`; the source audit below records the original
gaps and their resolution.

Complete title capture is still unproven. The frozen stage8 review found
missing save-time GPU target preparation and lost restored frame offsets.
Stream-owned state migration and parent-owned live restore orchestration
remain open. F8 is unavailable. This is an M12 slice, not M12 completion.
Worker validation uses component fixtures only: no title execution, shared
build, hardware change or PCSX2 work.

## App API available now

`app/host/session_checkpoint.{h,cpp}`, target `ps2r_session_checkpoint`:

```cpp
SessionCheckpoint checkpoint(restorer.Registry(), {
    on_game_owner, quiescent, fresh_threads, can_resume
});
auto admission = checkpoint.Capture(request.id, path, meta); // game owner
auto completion = checkpoint.Poll();                        // main, nonblocking
checkpoint.Join();                                         // exit supervisor
```

The registry must outlive this component and remain fixed during a capture.
`on_game_owner` proves the execution token is held, not just a thread ID.
`quiescent` uses the existing owner registry, including
`game_libraries.CanCheckpoint`: movie EOF is not enough while buffers remain
owned. `fresh_threads` returns owned records from the same boundary and
incarnation epoch. It must prove stable peer contexts, complete logical call
chains and reproducible waits; it may not substitute `ThreadSection::Restored`.
`can_resume(function, entry)` proves an exact supported entry of the installed
body. It is not an address-containment or `IsHooked` test.

Capture returns `Working` only after copying all registered owner payloads,
replacing THRD with those fresh records, validating the payloads through their
actual owners, and admitting one job to the existing `SavestateWriter`.
Only `Poll` returns `Succeeded`, after that writer reports successful file
publication. A request remains busy until its completion is consumed. A
second request cannot overwrite an unreported result. IO failures remain
failures and carry the actual error. A missing capture provider, including
an optional registered section, cannot silently become an empty section.
The writer owns the bytes; the next frame cannot mutate its image.

The writer's existing temporary-file/rename behavior is reused unchanged.
This does not add a durable-on-power-loss guarantee. `Join` runs only after
capture admission stops, under the root's existing shutdown deadline. Main
must consume its last completion before cancelling outstanding UI requests.
The runtime accepts `Working` with a nonblocking `poll_capture` hook. It
consumes actual results on main while paused and once more after the exit
supervisor drains the writer. The installed root adapter is described in
`INTEGRATION.md`. Readiness still depends on every state's owner, not merely
the existence of a fresh THRD provider.

## Original source findings

These were the starting gaps; the authorized engine changes below now
provide the missing continuation ownership and proofs:

| Source | Finding and implication |
| --- | --- |
| `engine/ee/savestate/thread_section.cpp`, `Serialize` | Copies restored ctx/chain while refreshing only thread metadata; new records have `has_ctx=false`. It cannot create a fresh live image. Preserve its existing image round-trip contract while adding a separate fresh capture path. |
| `engine/ee/hle/thread_manager.cpp`, `SnapshotTable`, `RunGuestThread` | Table snapshots hold shared `ThreadInfo` objects. `ctx` points into a worker's stack. Shared ownership protects the object, not context freshness or thread suspension. |
| `ThreadManager::SleepThread`, `Semaphores::Wait`, `EventFlags::Wait` | `NotePark(true)` executes on the owning thread before token release. `NotePark(false)` executes before token reacquisition. Other worker metadata and release results can change while waiting for the token. |
| `ThreadManager::WaitWhileSuspended`; `EeRunToken` preemption/rotation | Not covered by the park observer. A thread absent from the harvest cannot be treated as dormant or dropped. |
| `engine/ee/savestate/thread_relauncher.cpp`, `WalkChain` | Saved outer frames await later `InvokeAt` calls and are absent from the active TLS stack. Mid-entry wrappers also record the internal resume RA, not the original caller RA. |
| `engine/gs/hle/sce/gs_sync.cpp`, `SyncV` | `RunBoundary` invokes Verify before `g_vsync_frame` and the return register are finalized. Capturing there and resuming at the caller's RA skips those effects. |
| `engine/ee/timing/boundary_completion.cpp`, `RunBoundary` (formerly in `boundary_sequencer.cpp`) | The completed count advances after its step loop. Capture metadata must describe the finalized boundary. |
| Generated `recomp/recompiled.cpp`, `EeCallFrameGuard` | TLS has a fixed capacity and silently omits pushes beyond it. Saturation is not proof of a complete stack. Generated callers/guards are not changed by this app slice. |
| `tests/ee/test_dispatch_table.cpp` | Classic dispatch marks all 8807 generated bodies hooked. `DispatchTable::IsHooked` cannot distinguish nonresumable replacements. |

Initial engine scope was authorized and is now implemented:

- `engine/ee/savestate/thread_section.{h,cpp}`: extract common logical-field
  copying without changing historical `Serialize`; expose explicit fresh
  record construction with no restored continuation fallback.
- New `engine/ee/savestate/thread_capture.{h,cpp}`: own copied contexts and
  logical chains, capture TLS only on its owner, and report which record
  cannot yet be safely reproduced.

The implemented capture owner exposes:

```cpp
bool Attached() const;
bool CaptureBoundary(int tid, const CpuContext& completed_context,
                     std::vector<ThreadSection::Record>& out, std::string& error);
void ResetAfterRestore();              // after old workers have joined
```

Its private observer associates snapshots with table identity and worker
incarnation, not tid alone or a recycled ctx address. Immutable records own
the context and logical chain. A final table/identity/revision recheck rejects
peers that departed the captured park shape during collection. No caller
reads another thread's TLS. The root constructs `GameBoundary`, which owns
this provider; constructing a second capture observer would refuse attachment.

The authorized dependent changes are implemented:

1. `ThreadRelauncher::WalkChain` exposes an owning-thread resume scope for
   the current saved level. Reconstruction keeps active descendants, restores
   that level's original RA, and appends deferred outer frames. A save-after-restore must
   produce the complete logical chain again; the real file/relaunch fixture
   pins this recapture.
2. `ThreadManager` start/end, suspend and park observation provide
   incarnation identities and synchronized lifecycle notifications. `released`,
   `force_release`, event-flag result and object wait queues are not all
   represented by THRD v1; replaying an already released wait can lose its
   result. Unsupported suspension/preemption/release shapes and multiple
   waiters on one object still refuse capture.
3. `GameBoundary` calls its consumer after the service return value, boundary
   accounting and wrapper epilogue are finalized. The generic sequencer accepts
   an injected return scheduler, preserving its title-free linkage.
4. Dispatch proves exact continuation support for the installed callable.
   The shadow stack tracks omitted scopes and truncation. Separate metadata
   proves the three restartable wait thunks. Frozen generated body shards
   and caller semantics remain unchanged.

StateRestorer preflight and root Input/Save composition remain parent-owned.

## Remaining capture readiness work

The read-only stage8 review is evidence in
`int_work/m_wave/product-ui/stage8-root-review.md`, outside the worktree.

- Drain alone does not copy GPU-only render targets into serialized state.
  Root/graphics must prepare those contents on the render owner before
  capture. OLD `E2_SAVE_IMPL.md:249–252` and `E2_SAVE_REVIEW.md:668` document
  the corresponding historical failure and save-time sync requirement.
- Root currently supplies the reset-local completed boundary count as
  `FileMeta::vsync_frame`, losing the restored frame offset. Reuse the
  completed Verify presentation key, including its existing first-frame
  convention; do not introduce a different regression key policy.
- The root explicitly refuses while stream file/sample/music ownership has
  no capture migration. A quiet queue cannot stand in for those owners.

`SessionBoundaryCapture` proves the combined SDL F5 → completed service →
fresh records → owned file → caller replay route with synthetic callables.
It also pins movie ownership refusal, pending publication, F10, paused close,
and disabled F8. It does not supply missing graphics or stream state.

## Live restore sequence still required

1. Parent preflight validates an owned input image, exact section ownership,
   every owner's payload, and continuation admissibility before disturbing
   the running game. Keep those validated bytes; do not reread a mutable
   path after validation.
2. Request unwind, wake blocked owners, and join every old game worker under
   the main-owned deadline. No new worker with an old tid may start earlier.
3. Use the production `ThreadManager::RestartAfterJoin` operation after the
   join. It checks old contexts, host workers, pending deliveries and token
   attachments before clearing the old table and lifting shutdown. The
   adopted boot owner must first use `ReleaseCurrentThread`; ordinary
   workers perform the same cleanup in their exit epilogue. `BeginRelaunch`
   alone does not reset shutdown. No test reset is used.
4. Apply the prepared image through its registered owners. On success call
   **`game_libraries.ResetAfterRestore()` before guest execution**, reset the
   continuation owner, and call **`live_input->ResetPending()`**. Preserve the
   strict section-owner guard and the movie buffer-ownership check.
5. Rebase presentation/boundary timing and the existing input recorder.
   Backward frame numbers need an explicit new recorder segment/restart
   policy, not appending nonmonotonic samples to the current recording.
6. Relaunch once, retaining the original single execution-token contract.
   A post-teardown failure must be surfaced as a failed session, not a
   successful restore or a silently running partial state.

## OLD evidence and pins

References are read-only behavior evidence; no reference implementation was
copied. The later correction in E2 takes precedence over its older
`skip_innermost` description.

| Evidence | Risk | Pin in this slice / remaining obligation |
| --- | --- | --- |
| OLD `docs/loop_state/E2_SAVE_IMPL.md:218–248` | Skipping the boundary caller loses its post-sync arena flip. | `SessionBoundaryCapture` restores a real F5 file and executes the innermost and deferred outer tails exactly once, retaining final wrapper writes and skipping the already completed service. |
| OLD `E2_SAVE_IMPL.md:355–405`; `E2_SAVE_REVIEW.md:651` | TLS chains must be harvested by their owners and support exact re-entry. | `ThreadCapture` and `ContinuationProofs` pin owner-local live records, immutable parks, complete deferred tails, original RA, exact installed entry, and saturation refusal. |
| Local `BUG-036-savestate-relaunch-token-handoff.md` | Two incarnations of one tid run; an old epilogue can clear a new worker's ctx. | Production release/restart pins require old workers joined and attachments cleared, preserving monotonic incarnation identity. F8 must use these operations in parent-owned lifecycle orchestration. |
| Local `BUG-085-heap-section-meta-arena-bookkeeping-not-restored.md` | Memory bytes without allocator bookkeeping corrupt the resumed game. | Capture keeps all registered owners and refuses missing serializers even for optional sections. Owner validation failure writes no image. Parent's strict restore-owner guard stays intact. |
| Local `BUG-028-pause-delay-slot.md` | Handwritten caller changes drop call/delay-slot effects. | Frozen body shards stay unchanged; generated sidecar/stack pins and exact installed proofs validate capture admissibility without changing caller effects. |
| OLD `U4_SAVESTATE_REVIEW.md`; `U5_BUGREPORT_REVIEW.md` | Pending or dropped IO requests masquerade as successful artifacts. | `CapturedStateIsOwnedAndWriteResultsAreReal` writes and reads a real state file, checks copied data after live mutation, preserves request/frame identity, delivers completion once and reports a real missing-directory write failure. |

The same file test checks quiescence before capture and keeps the specific
movie active-buffer refusal visible. Neither EOF nor callback admission is
reported as a successful save. These are component pins, not a title
save/load/save continuation run or a performance measurement.

## Isolated verification

```sh
cmake --build <work>/int_work/m_wave/product-ui/build --target test_session_controls -j2
ctest --test-dir <work>/int_work/m_wave/product-ui/build -R '^SessionCheckpoint$' --output-on-failure
```

The app module compiles selected existing codec/host-thread support for this
test only. It does not compile generated title code or run the game. Parent's
integration remains responsible for title continuation/self-oracle evidence,
the adjusted regression battery and measured frame-time changes.
