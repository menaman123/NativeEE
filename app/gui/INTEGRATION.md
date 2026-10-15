# Session product integration

The product worktree contains the command queue, GUI, SDL input handoff, main
session loop, and targeted bootstrap/video integration. Shared/root CMake is
parent-owned. After the `nativeee` target exists, parent adds:

```cmake
add_subdirectory(app/gui)
target_link_libraries(nativeee PRIVATE ps2r_session_runtime ps2r_session_checkpoint)
```

Prerequisite input commit: `4b0d22f` (local cherry-pick `06b05aa`). Integrate
it separately; the product integration commit does not contain its engine or
title-provider files. Product command/menu commits are `c4b7912`, `7a78847`.

## Input/Save binding API

`PlatformInput` keeps its existing name and legacy sampling/census signatures:

```cpp
PadProtocol::Input Sample();             // game owner; waits for main service
bool Connected() const;                  // connection from the returned sample
bool QueueMotors(unsigned small, unsigned big); // 0/1 and 0..255; actual SDL result
void ResetPending();                     // clears host edges/mouse; stops motors
void SetUiCapture(bool captured);
void SetKeyboardEnabled(bool enabled);
bool KeyboardEnabled() const;
uint64_t Samples() const;
uint64_t EventDrains() const;
bool GamepadActive() const;
```

`Connected()` is latched with `Sample()`, so a keyboard setting or hotplug
event between those calls cannot change that accepted report's connection.
After service stops, a sample returns neutral/disconnected. Main alone calls
`HandleEvent(const SDL_Event&)` and `ServicePending()`. `EventDrains()` now
counts accepted events, not calls to drain SDL's queue. `Samples()` counts
completed physical samples. GUI-only pumps never consume button edges.

Parent's `title::Input::MotorOutput` can accept only port/slot 0/0 and return
`live_input && live_input->QueueMotors(motors.small, motors.big)`. Other slots
must return false. The method waits for main's actual capability/SDL result;
queueing alone is not success. Unsupported/disconnected devices, invalid
values and SDL failures return false. Main services this while the game
owner retains its execution token; it never calls title code.

`SessionRuntime::Hooks` separates action owners:

- `stop()` on main requests cancellation; it must not join or block.
- `pause(bool)` on the game boundary stops/starts pacing. Parent can also
  pause/resume its title Input provider here to manage desired motor state.
- `capture(const SessionControls::Request&, uint64_t frame)` on the game
  owner returns `{Phase, detail}`. `Waiting` retries at a later game boundary;
  `Working` means owned bytes are admitted and requires `poll_capture`.
  `Succeeded`, `Refused`, `Failed` or `Cancelled` finish synchronously.
  It receives the submitted destination.
- `poll_capture()` on main returns an optional
  `{request_id, {terminal_phase, detail}, artifact_path}`. It must not block.
  It runs after admission, including while paused. A successful async result
  requires its actual artifact path; stale IDs cannot complete another save.
- `stopped()` on the supervisor joins the renderer and flushes the recorder
  and checkpoint writer after game execution ends. Main keeps pumping and
  enforcing the shutdown deadline until this callback returns, then consumes
  the last writer result before cancelling unfinished UI requests. No SDL
  window calls belong here.

The independent `ps2r_session_checkpoint` target provides owned capture and
the existing async state writer. Its root adapter is:

```cpp
hooks.capture = [&](const SessionControls::Request& request, uint64_t frame) {
    auto meta = make_capture_metadata(frame);
    auto result = checkpoint.Capture(request.id, choose_path(request), meta);
    return SessionRuntime::CaptureResult{result.phase, result.detail};
};
hooks.poll_capture = [&]() -> std::optional<SessionRuntime::CaptureCompletion> {
    auto result = checkpoint.Poll();
    if (!result) return {};
    return SessionRuntime::CaptureCompletion{
        result->request, {result->phase, result->detail}, result->path};
};
// In the existing stopped hook, after game capture admission has ended:
checkpoint.Join();
```

The stage8 integration root supplies fresh continuations through
`GameBoundary::FreshThreads` and exact installed-body validation through
`DispatchTable::CanResumeAt`. `THREAD_CAPTURE_INTEGRATION.md` describes the
completed-service callback and its lifetime; `CHECKPOINT_DESIGN.md` tracks
capture readiness and the remaining restore work. Successful
async capture selects its path for reproduction handling; it does not enable
the unavailable restore capability. A failed write keeps the previous path.

The current root capture handler calls the existing quiescence registry.
Busy owners give a visible wait, bounded to 120 boundaries; a paused request
is refused immediately because waiting cannot advance it. When all owners
and continuations are admissible, `SessionCheckpoint` serializes owned state,
replacing the historical `ThreadSection::Serialize` output with fresh THRD
records. Completion is published only after the writer finishes.
This binding alone does not establish complete game capture: the root must
also prepare GPU target contents before serializing graphics, preserve the
completed frame's restored offset, and migrate stream-owned state. Those are
open findings in the frozen stage8 source review. Runtime restore remains disabled.
Persistent in-game saves remain the title SaveStorage provider's concern.

When a complete runtime restore handler is added, successful `RestoreFile`
must be followed by **`game_libraries.ResetAfterRestore()` before any guest
resumes**, plus `live_input->ResetPending()` to discard host input edges.
Keep parent's strict section-owner guard. Keep `game_libraries.CanCheckpoint`
as the movie quiescence owner: EOF alone is insufficient while active buffers
are still owned; they must close.

## Main / game / renderer handoff

The interactive root runs cold boot on a large-stack supervisor thread. A
restored game keeps its existing ThreadManager/run-token ownership; the
supervisor observes the restored boundary thread and joins only after
termination. Main continuously pumps SDL and services input/window requests.
It never blocks in a live `JoinAll`, whose table mutex would obstruct new
guest threads. The existing run token remains the only game execution owner.

Construct `SessionRuntime` before root's guest-binding rounding setup, as
the current root does. It remembers main's host rounding mode and restores
that mode before entering `Run`'s interactive SDL loop. Cold boot and
ThreadManager workers still establish their own guest rounding at entry.
The runtime component test pins both nearest and a nondefault host mode,
while the separate game owner stays round-toward-zero through pause/step.

F9 parks at the end of a complete game boundary while retaining that token.
F10 admits one next boundary, then parks again. Main continues to draw the
menu and process settings/close throughout. Window close and GUI/hotkey
commands use the same queue. Pending requests are cancelled visibly on exit;
a stuck worker hits `engine.shutdown_wait_s`, reports the timeout and exits
with code 2 instead of destroying resources still referenced by that worker.

The renderer keeps its existing context/consumer. After its regression grab
and final display composition, the swap callback copies the completed RGBA
picture to a bounded latest-frame mailbox. A separate visible window and GL
context belong only to main; they show that picture, then the menu. The old
render window is hidden, and its resize operations are dispatched to main.
Main never borrows the rendering context during play. Queued and inline
rendering follow the same presentation handoff.

This first integration adds a display readback and upload per submitted
picture. Its frame-time cost is **unmeasured**; no game/performance run was
authorized for this worker. Main's presentation cadence is independent of
game boundaries. Capture/hash pixels do not include the menu. Existing
deterministic/headless modes do not create the interactive session.

Preserve parent-owned bootstrap hunks when applying the integration diff:
`StreamLibrary::AdvanceBoundary` after `delivery_meter`, `park_file_frame`
shared with LoaderModel/StreamLibrary, SDK/provider/persistence/census work,
the exact section-owner exceptions, `CanCheckpoint`, and the post-restore
reset. Product does not replace any of those implementations.

## GUI handoff

Link `ps2r_session_gui` (it brings `ps2r_session_controls`); root still owns
SDL polling, the window, the GL context and swap. `SessionGui` lives on main:

```
SessionControls controls;
SessionGui gui(controls);
// After root creates its window/context, current on main:
gui.Initialize(window, gl_context, error);
// Main event pump (also runs while simulation is paused):
const auto routing = gui.ProcessEvent(event);
// Route uncaptured events to input, except close/hotkeys marked consumed.
gui.BuildFrame();
// Current GL owner, after game picture composition / regression readback:
gui.Render();
// Root swaps. At shutdown, quiesce renderer and return context to main:
gui.Shutdown(error);
// Only now destroy root's GL context/window.
```

Check Initialize/Shutdown results and surface their error strings. Construct
only in an interactive windowed session; deterministic legs do not create an
overlay. UI rendering must occur **after** the frame-hash/capture operation,
so opening settings cannot contaminate game rendering evidence.

The UI locks its sole ImGui context for each operation. No callback, file IO,
game work, blocking runtime wait, thread join or swap runs under that lock.
`BuildFrame` has SDL calls but **no GL calls**, so main does not borrow the
consumer's GL context. `Render` has GL calls but **no SDL window/event calls**;
the current context is checked before drawing. Caller binds the presentation
target and supplies the existing owner barrier; the official GL backend
preserves GL state. Do not call ImGui directly elsewhere concurrently.

`ProcessEvent` consumes function keys and this window's close event, and
queues process-wide SDL quit. GUI and hotkeys use the same actions.
When its `capture_keyboard` / `capture_mouse` is true, suppress game sampling
of those devices, including SDL's cached key/mouse state (merely dropping the
event does not suppress the existing poll path). Release relative mouse mode
on menu open through root's existing input capture API. Still deliver focus
and device lifecycle events to their owners. The menu claims keyboard/mouse
while open; no controller remapping or binding editor is advertised.

The panels include pause/one-tick controls, explicitly supported settings,
input recording status/path, reproduction save/restore, and pending/results.
With the menu closed, a compact notice displays outstanding requests and
completed results for six seconds without capturing keyboard/mouse input.
When neither menu nor notice is active, no UI frame/backend polling occurs.
Recording control is hidden unless root enables `Capabilities::recording`;
the existing launch-time recorder remains visible without that capability.
The path field calls `SelectRestorePath`, and **both** F8 and the Restore
button consume that selection. No scan or implicit file load runs on the UI
thread. A newly published `RuntimeState::restore_path` selects the latest
completed save. Re-publishing the same value does not erase a UI selection.

This lower-level API remains available separately. The integrated
`SessionWindow` owns it on main; `SetPicture` supplies an owned bottom-up
texture before `BuildFrame`, which letterboxes it behind the controls.

## Root contract

1. Own one `SessionControls` for the entire play session, outside any runtime
   restore/relaunch scope. Publish `Capabilities` and `RuntimeState`. Every
   optional capability defaults false: enable it only with a working handler.
2. Submit GUI and hotkey actions through `Submit(Action, value, path)`.
   `HandleFunctionKey(number, down, repeat, text_input)` implements F1 menu,
   F5 reproduction save, F8 restore, F9 pause/resume, F10 one game tick.
   It consumes known function keys, ignores repeat/key-up, suppresses F5–F10
   while entering text, and keeps F1 usable to dismiss the menu.
3. Main is the sole `TakeNext(Request&)` consumer. It applies host settings
   there and transfers game actions to the **one game owner** at a boundary.
   Never call game handlers from the window event or GL thread. The queue
   transfers commands only; it never calls root while holding a mutex.
   Already delivered requests remain pending while root completes them.
4. `SaveReproduction` with empty `path` means root chooses a unique output.
   A restore without an explicit path captures the selected `restore_path`
   at submission. Validate the whole input **before** changing live state.
   Do not route either action to the game's persistent progress/save storage.
5. Call `Waiting(id, actual_reason)` after a quiescence refusal while a bounded
   retry remains. It stays visibly pending. Root owns retry/time limits and
   must eventually call `Finish(id, Refused/Failed/Succeeded, detail)`.
   Success means the write or restore completed, not merely that it was
   accepted. Report the final artifact path and frame in the detail.
6. `TogglePause` and `Step` change no simulation state in this class. Root
   applies pause on the game owner, publishes `paused`, and for Step admits
   exactly one simulation tick then remains paused. Continue pumping window
   events, UI, render work and control requests while paused. A pending pause
   and step cannot overlap. Recheck eligibility when consuming requests.
7. `Close` has reserved queue space and priority, including when a save is
   waiting. The owner initiates its existing bounded shutdown, acknowledges
   Close, then `CancelPending(reason)` surfaces unfinished requests.
   Root still joins/drains writers and flushes the existing input recorder.
   Neither a close button nor a restore creates a second game owner.
8. Settings carry integer values: volume 0–100; fullscreen, keyboard input
   and input recording 0/1. Root dispatches SDL window operations to main,
   audio operations to their owner, and acknowledges only after application.
   Publish actual state separately; a queued request never fakes a new value.
   Recording is the existing NEW recorder; no second recorder is created.

`Read()` returns bounded copies for UI use. Pending requests cannot be
evicted by later refusal messages. Recent terminal results retain the last
32 requests. IDs stay unique for the session and stale completions are
ignored. No file IO, waiting for runtime work or teardown occurs in this
layer. Root must remain the sole consumer even if its game owner moves to a
different host thread after restore.

## OLD risks and pins

The following records were read as behavior evidence only. No OLD or
OpenGOAL implementation is copied.

| Evidence | Risk | Product pin / remaining root obligation |
| --- | --- | --- |
| OLD `docs/loop_state/U4_SAVESTATE_{IMPL,REVIEW}.md`, refusal and user-request sections | Duplicate requests and async drops masqueraded as saves; delayed refusals were silent | `SaveNeverSucceedsBeforeOwnerCompletion`: queued/working/waiting remain pending, duplicate requests visibly refused, write failure and final refusal remain failures, stale success rejected |
| OLD `docs/loop_state/E2_SAVE_{IMPL,REVIEW}.md`, universal pre-teardown validation | Malformed input could abort after teardown | `RestoreValidationFailureLeavesPublishedStateAlone`: rejection preserves published live state; root must run existing full validation before mutation |
| Local `docs/bugs/BUG-036-savestate-relaunch-token-handoff.md` | Restore accidentally created two execution owners | `RuntimeKeepsMainResponsive(true)` pins an independent boundary worker with main servicing, no additional game dispatch. Existing root relaunch gate remains required |
| Local `docs/bugs/BUG-027-input-lag-pipeline.md` | Window close stranded game threads and left a process behind | `CloseCannotBeStarvedByPendingSave`, both runtime layouts closing from a pause, and `SessionShutdown` verifying the actual deadline/exit code for a stuck synthetic worker |
| OLD `docs/loop_state/U4_SAVESTATE_REVIEW.md`, scan/IO placement | Scans and load waits stalled the render owner | Command layer performs no IO, callbacks or blocking waits; GUI must preserve this |
| OLD `docs/loop_state/U5_BUGREPORT_{IMPL,REVIEW}.md` | Requested artifacts were reported before files actually landed | Completion is explicit; no bundle/save success is inferred from an acknowledged request. Recorder path/status comes from root |
| OLD input lag/selection records; local input-readiness bugs | Quick taps were lost between pumps; connection and sample could disagree | `InputIsSampledOnlyOnMain` preserves a down/up edge across event-only pumps, pins same-sample connection and restore reset |
| OLD save continuation/arena-flip records | Stale continuations resumed different work against current RAM | `SessionBoundaryCapture` routes SDL F5 through the real completed-boundary owner, saves finalized context and mutable data, then replays each saved caller tail exactly once; no runtime restore handler is advertised |

The checkpoint component also writes and parses actual state files and runs
their registered validators. `SessionCheckpointRuntime` pins completion while
paused, failed IO retaining the last successful path, stale result IDs, and
shutdown draining before final UI cancellation. These synthetic-owner tests
do not claim title continuation correctness, crash/power-loss durability or
an integrated game shutdown. Parent's integration gate still needs to
establish those behaviors. There are no hardware replacement
changes, model retirement claims or external-emulator comparisons here.

`SessionBoundaryCapture` combines SDL hotkeys, pause/one-step/close,
`GameBoundary`, the actual thread/token owner, installed dispatch proofs,
`SessionCheckpoint`, file publication and production restart/relaunch in one
fixture. The movie ownership refusal writes no file; admission stays pending
until main polls the writer. Restoring the file replays the innermost and
deferred outer work once without replaying the completed service. This test
uses synthetic callable bodies and a small game-data section, never a title
body or hardware execution. F8 is checked as unavailable.

## Isolated validation

```
cmake -S app/gui -B <work>/int_work/m_wave/product-ui/build
cmake --build <work>/int_work/m_wave/product-ui/build -j2
ctest --test-dir <work>/int_work/m_wave/product-ui/build --output-on-failure
```
