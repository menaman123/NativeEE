# Checkpoint ownership and reconstruction

M0 owns the composition contract; each service owns its own values, codec,
validation and migration. This contract does not declare M0 or M12 accepted.
DT-12 applies. No hardware reference captures are used in this work.

## Admission and ordering

1. Capture immutable values on the completed game owner. A worker may publish
   an immutable completion but cannot inspect another owner's TLS or memory
   concurrently. A loan identifies its owning handle, generation and range.
2. Read and retain the complete reproduction image. Check section envelopes,
   versions, selected owners, logical clocks, cross-owner references and exact
   installed continuations before stopping, draining or resetting anything.
3. Pure validation is separate from drain/join. Missing selected state is an
   error. An explicitly classified older image may use an owner's migration;
   absence alone never means “reset to boot.”
   After those checks, stage unpublished resources while the current owner
   graph is intact. `StateRestorer::PrepareImage` binds the immutable image
   and registry/policy revisions; `RestorePrepared` consumes that exact
   preparation once, without repeating allocation after worker teardown.
4. Stop admission, unwind the game owner, wake its waiters and join workers
   and writers on the supervisor. SDL/main continues pumping and enforcing a
   deadline. A post-unwind failure closes; it cannot resume partial state.
5. Restore logical clocks before owner-relative deadlines, producers before
   consumers, then reconnect validated loans and publish callbacks in their
   saved sequence. Reject stale-generation completions. Invoke callbacks
   outside provider locks, once their bytes/results are visible.
6. Rebuild process resources only after approved state has been installed.
   Pending file/audio/movie work is owner state, not permission to discard it.
   Persistent game-save files are durable external state: reproduction restore
   must not roll them back or replay a completed write.

GCLK v2 includes the boundary's pending tick requests as logical work.
The completed game owner samples the atomic request count while its clock
is stable; new notes after that point belong to subsequent execution.
Restoration installs the saved count only after all note producers and
guest consumers stop. It does not zero the queue, manufacture a wall-clock
equivalent, or reset lifetime diagnostics.

## Registry API available now

`SectionRegistry::RegisterValidation(owner, callback)` installs a named pure
whole-image validator. It receives immutable, individually validated blobs
before the first restore callback. Use it for selection markers, generation
and loan relationships. Duplicate names and duplicate section tags reject.
`Revision()` changes when an owner or whole-image validator is registered;
`SessionRestart` invalidates a prepared image if that composition changes.

`SectionSpec::capture_required` marks newly captured state REQUIRED without
making the section mandatory for every historical file. Such historical
absence still needs the root's explicit migration/preflight policy.
`required` continues to mean every accepted image must contain this owner.

Owners must provide their tag/version, selection identity, pure validators,
capture/restore owner, restoration dependencies, pending-work representation,
loan relationships and exact legacy migration rules. Root registers in
dependency order. A validator must not drain, reset, open files or execute
game callbacks.

## Requested owner contracts

| Owner | Required information |
|---|---|
| Graphics M6/M7 | TXAS identity, converted assets, named target contents, producer/consumer completion order; GPU synchronization belongs after admission. |
| Services M3/M4/M5 | Pending file bytes/deadlines, Audio/Music cursors, callback order, outstanding loans and mixer logical sample position. |
| Movie M2 | File/buffer ownership, decoder/player state or a pure refusal while those resources remain owned; EOF alone is insufficient. |
| Geometry M1/M8 | Persistent resource/generation state and references to graphics-owned assets; immutable algorithms need no fabricated state. |
| Product M10 | INPT/SVST selection marker, readiness, pending read results and durable-save errors; no replay of durable writes. |

The owner request is also published in
`int_work/m_wave/product-ui/WHOLE_PHASE_CONTRACT_REQUEST.md` outside the tree
for parent coordination. No direct agent messaging tool was available.

## Regression pins and current evidence

OLD `E2_SAVE_IMPL.md` and `E2_SAVE_REVIEW.md` document CRC-valid truncated
sections reaching teardown, stale boot-era service state after load, and
unbounded pre-drain cost. Local BUG-036 records old thread incarnations;
BUG-085 records allocator bookkeeping that must follow restored bytes.
U4/U5 require queued hotkeys, visible pending/refused/error results and no
silent save success.

`test_savestate_format` pins cross-owner mismatch and duplicate tags refusing
before any restore, required-on-write flags, and explicit optional policy.
`SessionRestart` pins retained bytes, owner/proof changes, joined incarnation
checks, cancellation and failure ordering. `SessionRestartRuntime` composes
the actual supervisor and SDL command path; `SessionRestoreDeadline` checks
bounded main-owner failure. These tests do not replace actual title capture,
root continuation, play-session, or 13-leg acceptance.

`SessionRestoreComposition` now exercises the actual root coordinator with
ThreadManager, SessionWorkers, GameBoundary, StateRestorer, SessionClock,
SessionRuntime, VerifyHost and SessionFiles. Two restores reconstruct saved
RAM/clock/callers, preserve durable save files and pause before resumed
instructions. Missing owner state and resource preparation failure leave
the live worker intact. Root activation remains off: STREAM boot and unowned
asynchronous work refuse live rewind until M11 closes their ownership.
