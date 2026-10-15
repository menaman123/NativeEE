# Live continuation provider

The production provider is `GameBoundary`, in `engine/ee/timing/game_boundary.*`.
It owns `ThreadCapture` with installed dispatch-entry, wait-restart and
shadow-stack proofs. It attaches to the generic completed-service boundary;
GsSync is only the temporary SDK adapter. No root file is changed here.

Parent adds these files to `ps2r_ee` (shared CMake remains parent-owned):

- `savestate/thread_capture.cpp` (earlier stage, if not already integrated)
- `timing/boundary_completion.cpp`
- `timing/game_boundary.cpp`

The generic order/slot runner moved from `boundary_sequencer.cpp` to
`boundary_completion.cpp`; the eight slots and their order are unchanged.
The former file still provides service wiring and pacing. Both are required.

## Root binding

Construct before workers, keep until all workers join. There is one capture
observer and one completed-service consumer; `Attached()` must succeed.
Do not also construct a separate `ThreadCapture` for the same manager.

```cpp
GameBoundary game_boundary(ThreadManager::Instance(), sequencer);
game_boundary.SetConsumer([&](uint64_t frame) {
    if (!session->Boundary(frame)) throw ThreadExitUnwind{};
});
```

Move the existing `session->Boundary` call out of Verify into this consumer.
`SetConsumer` is a void callback. Translate the runtime's false return into
the existing `ThreadExitUnwind` path; ignoring it would resume the guest tail
after close/stop. The callback may throw: completion is consumed, and both
the capture scope and dispatch observation unwind before the supervisor runs.
Keep regression/readback/verification work in its existing order. This consumer
runs on the same game-token owner after `RunBoundary`, completed counters,
frame publication, field return and any enclosing installed-wrapper PC/register
writes. No notification is emitted for a stopped/throwing boundary. A restore
reset invalidates queued old notifications. `RejectedServiceReturns()` exposes
an adapter that could not find a safe enclosing dispatch return.

Use the following actual `SessionCheckpoint::Sources` callbacks:

```cpp
sources.on_game_owner = [] {
    return EeRunToken::Instance().CanCaptureCurrentThread();
};
sources.fresh_threads = [&](ThreadCapture::Records& out, std::string& error) {
    return game_boundary.FreshThreads(out, error);
};
sources.can_resume = DispatchTable::CanResumeAt;
// sources.quiescent remains the root's complete registry/service check,
// including game_libraries.CanCheckpoint: movie EOF is insufficient.
```

The runtime's game-owner capture handler calls `checkpoint.Capture(request.id,
path, meta)`. Return its phase/detail; `Working` means admitted, not saved.
`poll_capture` forwards actual `checkpoint.Poll()` completion and path.
The existing runtime polls during pause and after the supervisor's final
writer drain. A busy movie returns visible `Waiting`, no output file and no
writer admission; real file publication alone returns `Succeeded`.

`FreshThreads` sees only its caller's dynamic completed-boundary scope. Calls
from another host thread, after the consumer returned, or inside a nested
completion are refused. The writer receives owned bytes and never a live
context pointer. Quiescence and strict section-owner checks remain mandatory.

Cold boot must call `DispatchTable::Invoke(title.entry, ctx)` in place of
direct `entry(...)`, then `ReleaseCurrentThread` before context destruction.
Otherwise CRT0 is missing from the logical stack. Managed starts already use
dispatch. Any chain ending in a nonzero return address is refused as missing
its root; restored deferred outer frames are composed before this check.

After preflight, old-owner unwind/join and `RestartAfterJoin`, apply the image,
then run `game_libraries.ResetAfterRestore()`,
`game_boundary.ResetAfterRestore()` and `input.ResetPending()` before relaunch.
Use the exact installed-entry check in restore preflight before teardown too;
the late InvokeAt guard cannot substitute for preflight. The production restart
details remain below. This does not advertise an F8
handler: interactive restore orchestration/preflight is still root-owned.

A future host frame service calls `sequencer.CompleteServiceReturn(ctx, frame)`
after finalizing its results. GsSync uses `DeferServiceReturn` and dispatch's
`AfterCurrentReturn` so generated SDK wrapper epilogues finish first. The
provider contains no hardware model or title-function hook.
`AttachCompletedService(owner, consumer, scheduler)` takes an optional
`ReturnScheduler`; `GameBoundary` supplies `DispatchTable::AfterCurrentReturn`.
Without a scheduler, direct `CompleteServiceReturn` still works and deferred
completion refuses. The generic sequence object therefore has no dispatch
link dependency. Detach clears both callbacks and invalidates queued returns.
The standalone `BoundaryCompletion` test links only the generic runner, with
no title/dispatch/shadow-stack fixture; `ContinuationProofs` still exercises
the production dispatch scheduler and completed capture.

With `PS2R_PRODUCT_GUI` enabled, the same owned CMake fragment also registers
`SessionBoundaryCapture`, a separate executable linking the session loop and
the narrow continuation fixture support. Its SDL F5 request reaches this
actual provider while the execution owner is paused in the completion scope.
It writes a real state file, publishes the writer result on main, handles F10
and window close, then restores and relaunches the saved caller tails once.
The fixture uses exact generated entry metadata and stack ABI with synthetic
bodies; it does not link title bodies. F8 remains disabled. The original
`ContinuationProofs` executable retains its SDL-free dependency graph.

## Exact installed-body proof

`DispatchTable::CanResumeAt(fn, entry)` accepts only exact supported entries
of the current installed callable; zero denotes its function entry.
`CanRestartWait(fn, wait_type)` separately proves entry replay performs no
completed side effects before the wait. `ShadowCallStack::Complete()` proves
no active opaque body or rejected push was omitted. Saturation remains latched
until the represented owner stack empties, even after depth falls below 128.

Metadata is generated into `recomp/continuations.h` from emitted entry switches
by `tools/recompiler_v2/continuations.py`, part of the regeneration pipeline.
All 32 body shards and `recompiled.h` retain their hashes. Common dispatch/stack
instrumentation preserves existing body calls, delay slots and return semantics.
The proof belongs to the callable's `std::function` target; installing an
opaque callable revokes it, including via generated `update_function_impl`.
An exact captured original retains its proof. `InvokeAt` refuses unsupported
entries before execution. Neither `IsHooked` nor containment is consulted.
The source-adapter compatibility audit is in `CONTINUATION_WRAPPER_AUDIT.md`.
Opaque counting/delegating wrappers can reject previously supported historical
restore entries even when their selection is OFF. Those legs remain acceptance;
fresh-capture refusal does not excuse a regression. Refusals now name the
thread, logical level, function and requested entry for source attribution.

Three literal entry thunks are proven restartable: sleep `0x2cf200`, semaphore
`0x2cf320`, event `0x2d65c8`. The event thunk shares a generated function with
other blocks; its exact first block alone is repeatable. Extra work before a
wait revokes proof. All unreviewed special prologues remain unproven.

OLD E2_SAVE_IMPL's documented menu chain identifies two diagnostic cases:
main's installed wrapper `0x17ce50` and caller prologue `0x17d558`. Both only
read guest state and update log output/private log budgets. Their exact source
hashes and reasoning are in `tools/recompiler_v2/continuation_audits.json`.
They forward the same generated continuation without guest writes or extra
game calls; diagnostic text after restore can differ. Changes revoke the audit,
including mutation of an already instrumented wrapper. No address-only exemption.

## Test registration

`ContinuationProofs`, `ContinuationMetadata` and `ContinuationMetadataPins`
register whenever `PS2R_PRODUCT_TESTS` is on, in root and isolated builds.
The executable uses the same production source with a narrow fixture support
library plus a separate SessionCheckpoint object. It does not link production
`ps2r_ee`, a title library or body shards, avoiding duplicate generated ABI
providers. Its link line is audited in the evidence directory.

Root-mode verification configured the real project and its `ps2r_ee` target,
then added this owned module with a test-only CMake injection (this worktree's
frozen root predates the parent's module integration). Only the focused target
was built at -j2; only these three tests were run. All three pass in root mode
and isolated ASan/UBSan mode. The broader eight focused product/lifecycle tests
also pass under ASan/UBSan. Generated common syntax, strict edited-engine syntax,
manifest integrity and RecompToolProvenanceIntact pass. No root CMake edit is
part of this commit.

## Evidence and remaining shapes

The isolated provider fixture executes production GsSync, dispatch, generic
boundary, ThreadCapture, ThreadSection, SessionCheckpoint/writer and production
join/restart/relaunch primitives. Its callables are synthetic; no title bodies
are linked or run. It captures and publishes a real file, validates/restores
its thread section, resumes the innermost caller, and captures again with the
full deferred outer chain. Movie ownership deferral, final wrapper writes,
foreign/expired context access, duplicate consumers, stop/unwind and stale
notification invalidation have separate assertions.

OLD pins retained: E2_SAVE_IMPL 220-248 (never skip chain[0]'s post-sync arena
flip), 355-405 (owner TLS and truncation refusal), 418-431 (full root/menu chain
and waits); E2_SAVE_REVIEW ownership/admissibility. Local BUG-028 preserves
generated caller/delay-slot semantics; BUG-036 pins incarnation/restart;
BUG-085 preserves the complete owner registry and heap bookkeeping.

Live unrepresented preemption, suspended workers, nested resume composition,
multiple waiters needing replay-order preservation, and unproven installed
bodies still refuse with a reason. No stale restored-context fallback exists.
Root integration plus actual title acceptance remain parent-owned. No game,
PCSX2, frame-time or retirement-census measurement is claimed by these tests.

## Production restart primitives

```cpp
bool ThreadManager::ReleaseCurrentThread(std::string* error = nullptr);
bool ThreadManager::RestartAfterJoin(std::string* error);
```

`ReleaseCurrentThread` runs on the adopted boot execution owner before its
stack-local context is destroyed. It clears that incarnation's context and
live metadata, invalidates its captured park, and exits its token identity.
Use it in cold boot's normal and exceptional unwind paths in place of the
bare token `GuestExit`. Normal ThreadManager workers already perform the
same cleanup in their own epilogue. A foreign host thread or an old
incarnation cannot clear a current context.

After parent preflight has retained a fully validated owned input image:

1. Stop external delivery producers and drain old pending deliveries on
   their execution owner. Then request the old game unwind. Do not drain
   stale callbacks into the restored game.
2. The adopted owner calls `ReleaseCurrentThread`; normal workers unwind
   through their existing epilogues. Terminate and join all old workers
   under main's existing deadline. Main continues pumping.
3. On the supervisor, call `RestartAfterJoin`. It refuses without changes
   while execution is running, any host worker remains unjoined, a live
   context/started thread remains, old event deliveries remain queued, or a
   guest/HostGate token owner is still attached. It performs no implicit join.
4. On success, the old table and token ready/retired state are cleared,
   shutdown is lifted and new starts are allowed. The incarnation counter,
   scheduling sequence and diagnostic counters remain monotonic. This uses
   no `ResetForTest` call.
5. Apply the prepared image through parent-owned StateRestorer, then call
   `game_libraries.ResetAfterRestore`, `game_boundary.ResetAfterRestore` and host
   input `ResetPending` before relaunch. Keep the movie buffer-ownership
   guard and strict section-owner preflight. Rebase timing/presentation and
   the existing recorder's segment through their root owners.

The token's reset is private to ThreadManager so callers cannot bypass the
joined-worker/context guards. It tracks actual guest and host attachments;
a shutdown acquire used only to unwind is not permission to restart.
Root must still prevent new foreign work from being submitted during this
sequence. A failure after old execution has ended is a visible restore/session
failure; it is not permission to resume a partially restored state.

## Evidence

`ThreadCapture` component tests use actual ThreadManager workers and the
existing run token, with a test-only TLS ABI fixture instead of title code.
They cover a fresh capture from cold-created workers through the actual
state writer, distinct main/parked chains, immutable prior images after tid
reuse, dormant records without stale ctx/chain, pending wake and suspension
refusal, exact-entry/truncation proof failures, foreign capture refusal,
shutdown refusal, and full deferred tails at every real WalkChain level.
The restart pin additionally rejects live/unjoined/foreign ownership and
queued old deliveries, then restores a real THRD image, relaunches real
workers, and captures fresh complete chains again. A second production
restart verifies that the lifecycle is reusable without test resets between
runs. Existing host thread-lifecycle and relaunch-order tests run unchanged.

The pins carry the relevant OLD findings from E2 save/review (owner-local
harvest, exact return continuation, caller arena flip), BUG-036 (incarnation
identity/old epilogue), BUG-085 (all registered state owners) and BUG-028
(preserve generated caller semantics). References were read-only evidence;
no reference code was copied. The lifecycle fixtures execute no title code.
The completion fixture also exercises the temporary GsSync adapter, without
DMA/VIF/GIF packet processing or rendering-model execution.
