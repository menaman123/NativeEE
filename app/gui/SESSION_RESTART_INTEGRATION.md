# Reproduction restore composition

## Prepared selected resources

`StateRestorer::PrepareImage(bytes, length, error)` validates the original
image, every owned section and cross-owner policy, then runs the existing
unpublished resource preflights. It retains the exact bytes and parsed owners
without invoking pre-restore teardown or writing game RAM.
`RestorePrepared` checks exact bytes plus registry/policy revisions and
consumes that preparation once, without running staging again. A failed or
throwing apply cannot be retried. Explicit discard is required before another
prepare. Owner staging remains unpublished on failure; existing Graphics
owners discard their preceding candidates on the next prepare or destruction.

`SessionRestart::Hooks::{prepare_image,discard_prepared}` are a paired
contract. Preparation runs after original-image, exact continuation, clock,
recording and observational current-owner checks, before `Working` authorizes
unwind. Root binds `apply_image` to `RestorePrepared` using the same retained
bytes. The older combined `RestoreImage` remains available for boot/tools.

ASan/UBSan isolated checks pass for `CheckpointClock`, `SessionRestart`,
`SessionRestartRuntime` and `SessionRestoreDeadline`. Pins cover allocation
refusal before teardown, CRC-valid invalid clock state, byte substitution,
changed owner policy, one preparation/one publication, and existing restart
deadline/lifetime behavior. OLD E2_SAVE_REVIEW's post-teardown corrupt-state
failure is the regression motivating this ordering. These are component
results, not root F8 or phase acceptance.

`app/host/session_restart.{h,cpp}` implements retained-image preflight and the
supervisor restart transaction. It uses `SessionCheckpoint`'s continuation
validator, the section owners, `ThreadManager::RestartAfterJoin` and
`ThreadRelauncher`. Root composition and root tests remain necessary before
F8 is advertised. `SessionRuntime` now owns the supervisor loop described
below; the root must release its adopted cold-boot owner and return without
closing when a restart is pending. A second `Run`, clearing `closing_`, or
relaunching inside the completed boundary is not an acceptable substitute.

`SessionRuntime::Hooks` adds `restore(request, frame)`, `unwind_restore()`,
`restart()` and `alive()`. All four must be bound to expose restore. The first
returns `Working` only after retained-image preflight succeeds. The second
requests cancellation; `Boundary` then returns false to unwind. After the
initial game callback returns, `Supervise` gives a pending restart priority
over the old boundary owner's exit. `restart()` returns the original request
id, terminal result, restored frame, recorder path and close requirement.
Main publishes the result; post-unwind failure closes instead of resuming a
partial state. `alive()` observes the current boundary incarnation.

Main enforces the restore deadline from the start of preflight, including a
blocked file provider or join. `RebaseFrame(frame)` must run on the supervisor
before relaunch; publishing success must never overwrite a newer guest frame.
Pause is retained across restore. `Paused()` selects ticker policy before
launch. Close remains monotonic and has priority throughout.

The isolated `SessionRestartRuntime` test composes the real runtime,
ThreadManager, section registry, restart helper and relauncher. A missing
required section refuses without join/reset/mutation; SDL F8 then restores a
valid image, keeps pause, performs one F10 boundary and joins on close.
`SessionRestoreDeadline` blocks the preflight provider in a subprocess and
checks the main-owned bounded exit. These are contract tests, not title
continuation, game rendering or phase acceptance.

## API and ownership

Construct one `SessionRestart` before starting game execution. Keep it,
its callbacks, the registry and all selected owners alive until every worker
has joined. The helper retains immutable file bytes and continuation records:
`ThreadRelauncher` keeps references to these records while workers execute.
The previous image is released only after the actual engine join/restart
contract succeeds.

```cpp
SessionRestart restart(threads, restorer.Registry(), sources, hooks,
    [](uint32_t function, uint32_t entry, CpuContext& context) {
        return DispatchTable::InvokeAt(function, entry, context);
    });

// Completed GameBoundary consumer, on the existing game execution owner:
auto prepared = restart.PrepareFile(request.id, request.path);
// PrepareImage(id, owned_bytes) is also available.
// Refused/Failed: publish detail and continue this live session.
// Working: publish pending, stop admission/ticking, request unwind.

// Existing SessionRuntime::Run supervisor, after old owner release:
auto restored = restart.RestartPrepared();
// Publish the original request id and terminal result.
// requires_close: cancel/wake/join all workers; never resume partial state.
```

`Pending()` is synchronized. `DiscardPrepared(id)` is allowed only on the
game owner before stopping, for an explicit cancelled request. A second
prepare cannot replace an accepted image. A prepared result is `Working`;
only successful reconstruction and complete relaunch return `Succeeded`.
This is a relaunch acknowledgement, not proof of a rendered frame.

`Sources` bindings:

| Field | Root contract |
|---|---|
| `on_game_owner` | `EeRunToken::Instance().CanCaptureCurrentThread()`; preflight runs in the completed boundary scope. |
| `on_supervisor` | True only on the existing `SessionRuntime::Run` supervisor. Cold boot must first call `ReleaseCurrentThread`. The helper also rejects a live `CurrentIncarnation`, including after token shutdown. |
| `closing` | `session->Closing()`, the monotonic atomic close latch. Close wins before restart, during join, between restore stages and after relaunch. Owner/close predicates must not throw. |
| `current` | Return `Current{plan, completed_game_frame, recording_enabled}` on both owners. The logical frame stays frozen from acceptance through join. |
| `can_resume` | `DispatchTable::CanResumeAt`, never address containment or `IsHooked`. |
| `can_restart_wait` | `DispatchTable::CanRestartWait`, separate from exact interior-entry proof. |
| `validate_epoch` | Pure check that the selected clock/recorder transition is actually supported. |
| `current_quiescent` | Pure refusal checks for current service ownership, including Movie buffer/file ownership. No drains, resets, stop, joins or writes. |
| `validate_image` | Required pure check over original metadata and unfiltered blobs. Bind saved-clock/frame agreement and title/provider selection here; registry callbacks alone cannot see file metadata. |

`SessionClock` supplies GCLK v2. New captures mark it REQUIRED; interactive
restore requires the saved frame to equal its logical boundary count and the
saved provider/timing policy to match the running composition. Capture and
restore retain the existing domain counter, quantum bookkeeping and detector
window and deferred tick requests. Both off-owner demand and host-pacer notes
already enter the same logical request count; capture preserves that count
without classifying it as disposable wall time. `ReAnchor` alone deliberately
preserves the current counter and is not
a rewind. Register this owner before relative timers and service deadlines.
Historical boot files retain their separate, explicit migration policy.
GCLK v1 is refused because it lacks these requests; a present incomplete
clock cannot use the historical clock-absence rule.

The constructor now takes the actual boundary's `TickSync&`. Capture refuses
inside a partially executing tick body. Restore requires all guest workers
and note producers to have stopped first; root stops the ticker before its
boot `RestoreImage`, and the interactive supervisor must use its joined
worker lifecycle. `TickSync::RestorePending` restores only logical requests,
preserving lifetime fired/noted counters. The root's fresh F8 binding remains
pending and must enforce that lifecycle before any owner restore.

`SessionClock` tests compare uninterrupted and restored clocks through
adaptive windows, idle release and play/pinned modes, reject malformed
payloads and policy/frame mismatches, and prove invalid restore does not alter
the live clock. Pending-request pins compare the next uninterrupted and
restored emitted ticks in both play and deterministic policies, retain
diagnostic counters, and reject overflow/active-body restore before mutation.
The composed automatic capture test checks the actual saved pending count.
This closes the clock component only; graphics content,
service pending state, audio cursors and root output segmentation remain
separate owners/composition work.

Do not bind `SessionCapture::Conditions().Check` directly to the last field:
its graphics predicate calls consumer `Sync`, and its MTVU predicate calls
`Quiesce`. Those operations may execute pending work. Keep them in the
post-approval drain/join phase; use observational guards for preflight.
Movie EOF does not release buffer or file ownership.

`Plan.providers` identifies the exact selected provider versions/bindings.
Any replacement of a callback or owner must change that identity; registry
descriptors alone cannot compare the identity of arbitrary captured lambdas.
This identity freezes the live configuration during the transaction; it is
not a newly introduced on-disk build signature.

`Plan.required_sections` must enumerate every selected provider's state,
including necessary allocator bookkeeping and current Input/Save services.
THRD is always required. Missing selected state is refused before any reset,
even if that owner is normally an optional section.

Each absent optional owner also needs an explicit `optional_missing` rule:
`Preserve` requires no absent callback; `Reconstruct` requires that owner's
existing absent callback. These rules are for an approved default legacy
plan, not a waiver for selected service state. Unowned sections are rejected,
including optional ones. `legacy_unowned` may explicitly allow only a subset
of the root's five frozen exceptions: FILE, CNTR, MISC, STRM, SIFS.

Every owned payload must have a validator and restore handler. Envelope CRC,
section validation, duplicate detection, required ownership and every exact
continuation are checked before current-state guards. Started peers also
need proven wait-entry replay, no suspended/banked-wakeup state and no
ambiguous multiple waiters on the same object. A chain missing its root is
rejected. Default historical compatibility must pass this explicit plan;
do not weaken the checks to accommodate an unclassified image.

## Supervisor hook order

1. Root keeps SDL pumping, starts a main-owned restart deadline, stops new
   game/action admission and the ticker, requests termination, wakes semaphore
   and event waiters, and unwinds the completed boundary. The adopted cold-boot
   owner releases its context/incarnation before supervisor restart.
2. `join_workers_and_writers(error)` joins old game/service workers and all
   reproduction writers, including any automatic writer. Drain any renderer
   work before its owner is destroyed. Publish the preceding F5 write result.
   Failure closes the session. Root must not block SDL/main while joining.
3. The helper rechecks provider selections, registry descriptors, the frozen
   game frame and installed continuation proofs, then calls the real
   `ThreadManager::RestartAfterJoin`. A lingering owner, host worker, event or
   token attachment prevents reconstruction.
4. `begin_epoch(epoch,error)` calls `VerifyHost::BeginOutputSegment` with the
   saved game frame and logical tick. It exclusively reserves a new output
   directory for frame/audio/hash files and the input recording, records the
   predecessor, and keeps the preceding segment unchanged. `SessionFiles`
   selects the same epoch. Post-unwind output failure closes the session.
   SDL presentation accounting is rebased separately from logical game time.
5. `apply_image(bytes,error)` calls
   `restorer.RestorePrepared(bytes.data(), bytes.size(), &error)` on the exact
   retained bytes. `PrepareImage` validates and stages resources before
   unwind; apply requires the same bytes and registry/policy revision.
   Never reopen the submitted path or stage resources again after joining.
6. `reset_after_restore(error)` clears the completed-boundary capture epoch
   and input device edges, updates audio accounting to the restored tick,
   and reopens the stopped host audio device with the current pause policy.
   GCLK restores logical time before service deadlines. Input/Save retain
   saved polling keys. Selected Movie, audio, files and graphics reconstruct
   through their sections; no blanket GameLibraries/Movie reset erases them.
   The ticker remains stopped until owner publication has completed.
7. The helper relaunches exactly the validated started contexts using the
   existing election and caller-tail walker, after rechecking the restored
   frame, selected providers and installed entries following owner resets.
   Root's supervisor continues watching the new boundary owner instead of
   returning from `Run`.

`cancel_workers()` is mandatory, nonblocking and idempotent: terminate/wake
game and object waiters, stop admission and notify the boundary protocol.
After any post-unwind failure the helper calls it, returns `Failed` with
`requires_close=true`, and retains records that partial workers might use.
Root must still join before destruction. There is no rollback or automatic
fallback after old owners have stopped. Window close can race any hook; the
root must keep its close deadline active and must never clear the close latch.

Persistent in-game saves are separate from reproductions. None of the helper
operations writes the progress store. Root's Save section must reconstruct
runtime handles/cursors without rolling disk progress back. Pin this with the
actual provider when composing root, as well as with the helper's sentinel.

`Clock::RefuseRewind` rejects a target older than the completed game key.
`Clock::Rebase` allows an explicit epoch transition. There is no implicit
clock policy. `Recording::Disabled` requires recording already disabled.
An enabled recorder requires `Recording::NewSegment`; flush the previous
segment and write a distinct file, leaving its contents unchanged.
The recorder now supports explicit output segments; the composed test performs
two rewinds while recording and retains both predecessors. The supervisor,
deadline, SDL request/results, joined reconstruction and output epochs are
connected through `SessionRestore`. See `ROOT_PRODUCT_INTEGRATION.md`.

UPDATE 2026-10-11 (track 3a): the live F8 is the compiled DEFAULT
(`save.restore_mode=live`; `harness.live_restore` stays the root opt-in that
forces it). Service-only startup landed (`hle.startup=native`, the IOP never
boots in play), the asynchronous owners are admitted by name through
`app/host/live_restore_gate.*` (the MTVU quiesce, no pending kernel event /
service RPC / disc delivery), the Graphics owner resets the open recording on
the live session (`GraphicsOutput::ResetForRestore`) and the MTVU worker's
unit mirrors the restored VU1. Pinned on the real binary by
`RootPlayLiveRestore`; `docs/design/F8_LIVE_RESTORE.md` section 5. The relaunch
route stays the fallback (`save.restore_mode=relaunch`, or a composition that
retains IOP state). No IOP rewind, missing-section reset or synthetic proof is
used to bypass a refusal. Component fixtures still do not establish title play.

## Pins and build registration

OLD was inspected read-only; none of its code was copied.

| Source risk | Pin in this stage |
|---|---|
| OLD `E2_SAVE_REVIEW.md`, CRC-valid oversized GSST accepted until teardown | CRC-valid invalid owned payload and missing selected state refuse with zero joins, resets or absent callbacks; owner guards have not run. |
| OLD `E2_SAVE_IMPL.md`, restore lost private frame-counter epoch | Begin-epoch precedes apply/reset/relaunch; first resumed tail observes restored frame. RefuseRewind and frame movement after preflight reject. |
| OLD `U4_SAVESTATE_REVIEW.md`, asynchronous acknowledgement mistaken for success; silent duplicates | Working preparation, explicit duplicate refusal and terminal result only after actual restart/relaunch. A preceding real writer is joined. |
| OLD `E2_SAVE_IMPL.md` caller replay; local BUG-028/BUG-036 | Real caller tails run once with saved registers after owner release; restart is refused even after shutdown while the adopted incarnation is still live. No generated body changes. |
| Local BUG-085, memory restored without allocator ownership | Required selected state cannot use optional reconstruction or a new legacy-owner exception. Actual heap/provider pins remain required in root. |
| OLD restore reconstruction and U5 artifact ownership | Source file deletion after preflight does not affect retained restore; persistent progress and previous recording remain byte-unchanged. |
| Provider replacement, join failure, window close or failed reset | Rechecked proofs/selections and cancellation between stages prevent relaunch; post-teardown failure requires close. |

`tests/app/test_session_restart.cpp` uses actual file parsing, registry,
writer, ThreadManager, ThreadRelauncher and run-token code with synthetic
continuations. It provides four no-stack shadow ABI functions because this
test never captures a live generated stack. Existing ContinuationProofs
remain the exact installed-body/metadata proof. This fixture cannot establish
title rendering, root F8, SDL responsiveness or selected-service correctness.

Parent source registration: add `app/host/session_restart.cpp` to the
production host or a helper library linked to `ps2r_session_checkpoint`.
For the test, compile that same source with `test_session_restart.cpp` and
link `ps2r_continuation_checkpoint` and its narrow checkpoint support.
Do not link this fixture to generated title bodies or `ps2r_ee`: its shadow
ABI must have one owner. No CMake files were changed in this stage.

Isolated ASan/UBSan build and test commands used, from the product worktree:

```sh
make -f <work>/int_work/m_wave/product-ui/f8-restart-test.mk -j2
<work>/int_work/m_wave/product-ui/f8-restart-build/test_session_restart
```

Evidence and bounded-run logs are under
`<work>/int_work/m_wave/product-ui/f8-restart-*`.
No title runs, shared builds or external rendering comparisons were performed.
