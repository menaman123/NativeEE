# Product root integration: Input/Save and reproduction restart

This stage composes the existing title services in the actual bootstrap. It
does not declare M0, M10, M11 or M12 accepted. No default changes, title runs,
hardware-reference comparisons or retirement claims were made by this worker.

## Changes for parent integration

Apply after the preserved restart helper and product commits `48a6594`,
`de84dea`, `8f29070`, `515c2dc` (or their integrated equivalents). Shared
changes are root/app CMake, Config and its generated settings table, and
targeted `ee_bootstrap.cpp` composition. Other owner implementation files
are unchanged.

- `hle.input_service` selects the existing Input service and its complete
  SDK interfaces. Sampling uses accepted logical ticks, the
  serialized SDL provider and actual rumble capability. Selected replay
  does not stamp PadProtocol or register PADMAN RPC servers.
  Readiness follows the accepted sample; it neither observes the frontend
  function nor delays availability through the retired connection timer.
  INPT v2 carries coherent connection/setup state with bounded v1 migration.
- `hle.save_storage` selects the existing durable game-save service. It
  does not register MCSERV. Its verified `mcInit` startup interface calls
  actual host storage preparation and reports failures; Main continues.
  Pending durable mutation refuses a reproduction
  checkpoint; restoring reproduction state never rolls back save files.
- Both selections default off until the parent completes the gates below.
  They are integration toggles, not ordinary product UI settings.
- ISCP publishes the exact selection; selected INPT/SVST are required.
  Pure whole-image validation rejects missing, mixed and mismatched owners.
  Historical boot migration is explicit (`harness.legacy_input_save=on`
  when selecting a service). New marked images cannot use that escape.
  `harness.legacy_input_save` and `harness.live_restore` have no CLI flag of
  their own: `--set` is their only route (`config_loader.cpp`, the two named
  opt-ins exempt from the harness.* refusal; ps2r.ini/--config still refuse
  them), so `battery.py --set harness.legacy_input_save=on` reaches the binary.
- `SessionWorkers` implements nonblocking cancellation/wake and supervisor
  joins. The root and the composed SDL restart test use this same class.
  Cold boot releases its adopted context before the supervisor observes a
  restart. Restored startup uses the supervisor's boundary-owner observer.
- GCLK is registered before timers. StreamLibrary advancement remains after
  the delivery meter. Boot migration runs MovieBoot before InputSave;
  ServiceRestore validates the original owner image and saved clock.
  Accepted Movie/Audio/Graphics state is not blanket-reset after restoration.
  GCLK v2 includes pending tick requests. The boot restore stops/joins the
  ticker before applying the clock and resumes it after owner resets.
- Pause/step also pauses/resumes the SDL audio device through the actual
  main-owner handoff. Pausing retains queued samples. A separate
  `AudioPullRing::DiscardStopped()` discards obsolete output only after
  producer join and device callback stop. SessionAudio uses it during F8,
  then reopens the prior device on SDL/main and preserves the pause state.
  Game Audio/Music cursors and voices are preserved.
- The actual present path now uses `VerifyHost::PresentKey`. Interactive
  output segments continue from their saved frame; initial cold/CLI-restore
  keys retain their historical convention. Old-epoch keys and overflow
  refuse. The menu shows the configured persistent progress folder and an
  explicit unavailable message while reproduction restore is disabled.
- `SessionFiles` now owns the actual diagnostic output handles. They close
  on teardown and switch to exclusive files with each output epoch; frame
  dumps use that epoch's directory. Diagnostic hashes remain process-wide.
  The supervisor selects these files before relaunch, so a path error is
  reported within the restore transaction. The first epoch can start before
  any readable/dumped frame has created its directory.

Actual root F8 hooks now bind through `SessionRestore`; since track 3a
(2026-10-11) the live route is the default (`save.restore_mode=live`;
`harness.live_restore` is the opt-in that forces it; `LiveRestoreGate` chooses
the route and names each refusal — `docs/design/F8_LIVE_RESTORE.md` section 5). Missing selected state,
continuation proofs or clock/provider identity refuse before live mutation.
The exact retained bytes and registry revision bind resource staging to one
publication. Workers, ticker and checkpoint writer join before reconstruction;
output files and recorder segments change before relaunch. A paused restore
stays paused before its first guest instruction. F5 at that initial resume
gate refuses until a real completed boundary supplies fresh caller records.

**Remaining activation dependency:** the actual root refuses live restore if
STREAM boot ran or an asynchronous model has no approved restore contract.
The existing IOPM boot import is not a safe live rewind. Service-only startup
and remaining source caller closure therefore precede usable title F8; this
composition does not declare that dependency complete. Active Movie ownership
also refuses until its owner supplies an approved active-state contract.
No default change or live title restore is claimed.

CLI boot requires
pure validation of the original metadata and sections before publishing its
normalized image. A marked reproduction missing GCLK refuses; only explicitly
classified unmarked historical boot files may omit it. A present GCLK must
always match the saved frame, provider identity and policy.

## Pins and evidence

OLD `E2_SAVE_IMPL.md` distinguishes the stale STREAM state popup from the
card-detection edge; preserving Input/Save alone does not close that defect.
`E2_SAVE_REVIEW.md` requires validation before teardown. U4/U5 require real
artifact results, bounded requests and visible refusal. Local BUG-065/074
require readiness and sequence to survive clock rebasing, BUG-089 requires
correct output-pointer semantics, and BUG-036 requires joined incarnations.

`InputSaveComposition` exercises the actual service bindings, RAM aliases,
absence of frontend readiness hooks, rumble rejection, disconnect, same-tick sampling,
selected-vs-retained replay output, selected zero pad stamping, durable
create/write and injected IO failure. Missing/mixed/malformed state leaves
live owners unchanged. Restoring older reproduction state preserves a newer
durable save file. Explicit boot migration reads and retains one image.

`SessionRestartRuntime` uses actual ThreadManager/token lifecycle,
SessionWorkers and the SDL command path. It proves worker cancellation,
clock stop/wake, one incarnation and resumed caller behavior with a bounded
fixture. It is not a title-play test.

Evidence is under `int_work/m_wave/product-ui/` outside the tree:

- `root-components-tests.log`: InputSaveComposition, SessionClock,
  SessionRestartRuntime, SessionRestoreDeadline, 4/4 ASan/UBSan passes.
- `config-checks.log`, `config-schema-checks.log`: Config/schema pass;
  deterministic fingerprint remains `0x25cf829dd6193659`.
- `root-whole-syntax2.log`: actual Release bootstrap syntax passes.
- `session-audio-tests.log`: actual SDL dummy-device pause/resume and
  stopped-ring discard pass under ASan/UBSan. `root-audio-syntax.log`
  validates the actual bootstrap handoff. This is device lifecycle
  evidence, not positive in-game sound playback or M5 acceptance.
- `pending-clock-tests.log`: SessionClock and CheckpointClock, 2/2
  ASan/UBSan passes, including saved pending requests and next-tick equality.
  `root-pending-clock-syntax.log`: Release bootstrap source syntax passes.
- `output-failure-tests.log`, `present-key-tests.log`: core verification
  checks pass under ASan/UBSan, including terminal filesystem failure and
  actual resumed frame lanes. `product-path-tests.log`: GUI/runtime, 2/2.
- `session-files-tests.log`: actual file ownership, epoch selection and
  foreign-file collision pins pass under ASan/UBSan. The production source
  compiles through the existing product module dependency.
- `root-restore-tests4.log`: CheckpointClock, SessionFiles and
  SessionRestoreComposition, 3/3 ASan/UBSan passes. The composed test uses
  actual capture, worker, clock, owner registry, RAM reconstruction, SDL
  command and recorder/output code. It performs two rewinds, pins pre-mutation
  refusal and unpublished-resource failure, preserves durable game saves,
  and checks pause before resumed instructions. Its generated ABI fixture is
  not title play or service/graphics coverage.
- `root-restore-tests.log`: SessionAudio passes 33 device lifecycle pins,
  including unavailable SDL ownership, repeated stop and paused reopen.
- `root-f8-syntax3.log`: actual Release root/coordinator/audio syntax passes.
- `input-clock-tests.log`: SessionClock, InputSaveComposition and InputSaveClock
  pass under ASan/UBSan. Pending setup/read outputs occur on exactly the same
  keys after GCLK rewind; duplicate input keys are retained; future owner
  times refuse before mutation. Clockless legacy import remains explicit.
  `input-clock-root-syntax.log` checks the real root binding.
- `root-owner-test3.log`: the composed F5/two-F8 test now registers actual
  INPT/SVST/ISCP providers. Startup creates a game save through SaveBindings,
  F5 captures pending input setup, and a newer durable write finishes before
  rewinding logical save state. Both F8s preserve that file with exactly two
  commits and no replay. Missing clock/Input/Save sections and an active save
  mutation refuse before worker joins; a refused F5 writes no file.
  The first stronger run exposed an F9/F10 race: Step used a stale main-loop
  pause snapshot after the game owner had acknowledged pause. Step admission
  now queues to the game owner, which already validates the actual pause
  state. SessionControls pins that ordering without a timing delay.
- Size and session-tag lint pass without widening an allowance.

No frame-time delta has been measured. No new 13-leg or user-play coverage
is claimed.

Product now consumes Movie `0d44c41`/`a93211e` as `94d5a27`/`27a586e` and
Graphics `0f9ee7e`/`02be496` as `a26ff0c`/`b67404d`. Integrate each owner change
once; do not replay their copied M0 prerequisites. Movie preparation is
registered after MOVI/GCLK in the real root. Its decoder resources are staged
before unwind and cleaned on cancellation or failure. Graphics adds authored
display layout and All-mode Strip submission through the shared scene.
`owners-stage3-root-syntax.log` passes the combined Release source check;
`owners-stage3-product-tests.log` passes five sanitizer checks: restart,
runtime, deadline, CheckpointClock and the actual two-F8 owner composition.

M0 remains implementing through cross-owner acceptance. M10's selected
bindings, coherent input/rumble/readiness, durable saves and error behavior
are prepared in this worktree; parent game gates and any resulting repairs
remain. M11 still depends on source-enumerated service status, heartbeat,
startup and effects ownership before STREAM can stop executing. M12 final
deletion follows those cutovers and their acceptance. None is marked complete
by a component pass or by an unavailable restore handler.

## Parent-only acceptance commands

Run from the integration checkout after serially merging and building.
These commands are supplied for the parent; the worker has not run them.
Keep the distinct Debug binary at `build/app/host/nativeee` and Release at
`build-release/app/host/nativeee`.

```sh
ctest --test-dir build-release --output-on-failure -j2 \
  -R '^(SessionControls|SessionGui|SessionRuntime|SessionShutdown|SessionAudio|SessionFiles|SessionCheckpoint|SessionCheckpointRuntime|CheckpointClock|ThreadCapture|ContinuationProofs|SessionBoundaryCapture|ContinuationMetadata|ContinuationMetadataPins|SessionRestart|SessionRestartRuntime|SessionRestoreDeadline|SessionRestoreComposition|SessionClock|InputSaveComposition|InputSaveClock|InputCallers|SaveCallers|InputService|InputVersion|SaveStorage|InputSaveStateCodecs|InputLegacyImport|InputSaveCheckpoint|InputEdges|Coretest_verify_units|Coretest_config|Coretest_config_schema|SettingsTableReproducible|PacerReleaseDomain|PacerReleaseSequencer|SessionPanels|VerifyFreezeSessionLegSelftest|ToolsSifStartupCallersSelftest|RootRecordInputRoundTripSelftest)$'

ctest --test-dir build-release --output-on-failure -j1 \
  -R '^(RootRegistersVuVifGifSections|RootColdbootProbeResumes|RootPlayWallLock|RootAnchorPadReady|RootReleaseLanesIdentical|RootRecordInputRoundTrip)$'
```

`RootRecordInputRoundTrip` is the play-session recorder command (`--inputs` script,
`--record-input <file>`) end to end on the binary: the explicit file is named in run.meta,
replays nothing the script did not hold, carries every scripted range, and a second leg naming
the same file is refused with the first recording's bytes intact. After the EeBootstrap::Run
phase split (ee_bootstrap_*.cpp) the five historical Root checks are also the lane-identity
gate of the split root: this owner runs no game process.

The second group uses existing historical NEW restore checks, not new
hardware-reference captures. Missing fixture skips are not acceptance.
Do not add RootTypeRegistryComplete's external rendering pins to this wave.

For the actual 13-leg, three-repetition arms:

```sh
python3 verify/battery.py --binary "$PWD/build-release/app/host/nativeee" \
  --lanes frames,audio,mem --advisory-mem --reps 3 \
  --out ../int_work/m_wave/product-ui/root-off-13x3

python3 verify/battery.py --binary "$PWD/build-release/app/host/nativeee" \
  --lanes frames,audio,mem --advisory-mem --reps 3 \
  --set hle.input_service=on --set hle.save_storage=on \
  --set harness.legacy_input_save=on \
  --out ../int_work/m_wave/product-ui/root-input-save-13x3
```

Battery's opt-in arm reports FP-MISMATCH because its fingerprint differs
from the standing default. Do not edit the baseline or turn that report into
an unqualified pass. Inspect all thirteen legs, each repetition's frame/audio
stability, and both arms' first divergent keys against the standing baseline.

Round-2 gate 5 (`int_work/m_series/gates/GATES_LOG.md` 5a–5c) and the
round-3 repairs, per finding:

| Finding | Repair | Where |
|---|---|---|
| `--set harness.legacy_input_save=on` refused (`harness.*` CLI-only), the documented arm not executable | `--set` accepts the two root acceptance opt-ins (`harness.legacy_input_save`, `harness.live_restore`); files still refuse them | `engine/core/config/config_loader.cpp`, `Coretest_config` |
| `hle.save_storage=on` cold boot aborts ≈ frame 374: `SaveStorage: game output no longer writable` | the title's main thread stack is in the scratchpad (TR `src/platform/ps2/entry.cpp:8-9,83`); Poll `0x2a2430` hands GetResult its stack slot as the output; `SaveBindings::RamRange`/`InputBindings::RamRange` admitted main RAM only and `InputSave::RamAccess` mapped only `MainMemory`. Both now admit the scratchpad; the root passes `Scratchpad` next to `MainMemory` | `hle/saves/save_bindings.cpp`, `hle/input/input_bindings.cpp`, `app/host/input_save.cpp`, `ee_bootstrap_play.cpp`; `SaveStorage`, `InputSaveComposition` |
| `hle.input_service=on` alone: frames diverge from key 120 (928 keys), audio identical | attributed to when each arm tells the title its pad is live (control: OLD's 8/214 staging or case-2 hook + 2; owned: first connected sample), through the title's own sixty-tick reconnect delay and setup (`TR:src/game/pads.cpp:402-536`); both arms now record their availability keys in `run.meta` (`pad_ready=…`, `input_service=…,opened=,stable=,setup=`) for the parent's pair | `hle/input/READINESS.md` §"Cold-boot frame-lane attribution"; `InputService`, `PadProtocol` |

The restored legs' `Restore needs owned logical clock state.` (5c, without the
opt-in) is the designed refusal `harness.legacy_input_save=on` approves; with
the `--set` route the documented arm is runnable as written above.
The baseline directory is not a battery output directory: the latter uses
`<leg>.repN`, not thirteen original leg directories.

Compare corresponding NEW arms (for every leg):

```sh
for product_leg_dir in ../int_work/m_wave/product-ui/root-off-13x3/*.rep1; do
  product_leg="${product_leg_dir##*/}"
  python3 verify/diff_legs.py "$product_leg_dir" \
    "../int_work/m_wave/product-ui/root-input-save-13x3/$product_leg" \
    --cross-engine --lanes frames,audio
done
```

Here the cross-configuration option permits the intentional fingerprint
difference. Both inputs must be the same NEW executable/renderer and
corresponding inputs. Inspect complete reports; do not mask differences.
Attribute any frame/audio change before a default flip. Memory is advisory.
Check `padman_calls`, `pad_stamps`, `mcserv_calls` are zero on each selected
leg and `input_service`/`save_storage` show the corresponding positive owned
work. A leg with no save operation does not prove persistence retirement.

The two user play legs record NEW input and share a persistent save directory:

```sh
./build-release/app/host/nativeee --profile default --harness-window \
  --record-input --frames 36000 \
  --game-files $PS2R_OLD_ROOT/game_files \
  --harness-out "$PWD/play_sessions/session-1" \
  --set "paths.log_dir=$PWD/play_sessions/session-1/logs" \
  --set "paths.mc_dir=$PWD/play_sessions/saves"

./build-release/app/host/nativeee --profile default --harness-window \
  --record-input --frames 36000 \
  --game-files $PS2R_OLD_ROOT/game_files \
  --harness-out "$PWD/play_sessions/session-2" \
  --set "paths.log_dir=$PWD/play_sessions/session-2/logs" \
  --set "paths.mc_dir=$PWD/play_sessions/saves"
```

After service-only startup is composed and accepted, add
`--set harness.live_restore=on` to these commands. Record F5/F8, paused restore, F10, repeated
restore while recording, malformed/missing-owner refusal and close during
pending work. Before that dependency closes, the truthful root result is a
visible STREAM ownership refusal. Running the component tests above cannot
stand in for the missing live checks.

The worktree includes Movie through `8ce298f`, Graphics through `a7a8714` and
services through `50a39b1` dependencies. Parent should integrate those owner changes once,
then merge the product root hunks; copied older product commits in a peer
history are not instructions to replay them.

Include `InputCallers` in the focused CTest run. Its four unchanged generated
controller callers execute against actual host Input bindings with a bounded
allocator/libc fixture. It validates setup/reconnect/rumble/restore at the
game's real interface while leaving full gameplay and startup acceptance to
the parent. `input-callers-test4.log` passes under ASan/UBSan.


## Savestate slots (2026-10-09, round 5)

F5 and `--set harness.save_at_frame=N` share `SessionCapture::Capture/Poll` (`app/host/session_capture.cpp`):
the slot `SavestateSlots::SlotPath(cfg)` = `<paths.save_dir | harness out | log_dir>/savestates/slot<save.slot>.ps2st`
and the `.meta` sidecar (`SavestateSlots::WriteMeta`). `SavestateTrigger` (`app/host/savestate_trigger.cpp`) is the
harness driver from the `GameBoundary` consumer; `Finish()` runs before the frame cap's `_Exit`. The LOUD route is
`SessionControls::SetObserver` (every Queued / refused / terminal / cancelled result outside the lock) → the root's
observer → `SavestateSlots::Report` (stderr + SAVE log). F8 without live-restore approval is `SessionRelaunch`
(`app/host/session_relaunch.cpp`): preflight (`SavestateSlots::Preflight`), unwind, close, `SessionRelaunch::TakePending`
in `main.cpp` → `execv(argv[0], SavestateSlots::RelaunchArguments(...))`. Pins: `SavestateSlots`, `SessionControls`
(observer/fallback), `RootPlaySaveRestore`, `RootPlaySaveRefusal` (+ selftest). The in-play capture under all-on awaits
the Graphics owner's capture-time contract (`graphics: Graphics owned pending=N`).

## Savestate slots — review loop 1 (2026-10-10, round 7)

The F8 preflight is now the whole contract (`SessionRelaunch::Bind`, `hooks.restore`, on the game owner, with the
session still open): (1) `SavestateSlots::Preflight` — every required section present; (2)
`SavestateSlots::PreflightComposition` — the slot's `.meta` is READ (`SavestateSlots::ReadMeta`) and its `composition=`
compared with the live `SavestateSlots::Composition(cfg)`; a difference refuses by name
(`composition differs: profile(saved=deterministic,live=default)` — `CompositionDifference` lists every differing key,
absent keys as `<absent>`), a differing `build_id=` is reported (`F8 restore Warning: build_id differs ...`) and restored;
(3) `SavestateSlots::RunningImage` — the executable resolved from the kernel (`_NSGetExecutablePath`, canonical) must
resolve, else refused. Only then `Working` → unwind → close → `main.cpp` exec's THAT image (fallback `execvp(argv[0])`;
on failure the stderr line says the session is already closed and prints the argv to restart by hand). Pins:
`SavestateSlots` (72 checks: ReadMeta round trip, CompositionDifference, PreflightComposition accepted / warned /
refused-by-name / no sidecar, RunningImage), `RootPlayRelaunchRestore` legs C/D (a deterministic headless slot pressed in a
default play session: `F8 restore Refused: ... composition differs: profile(saved=deterministic,live=default)`, no
Relaunch, leg D `finalized=1 frames=160 park=none park_detail=frames_cap video=gl` — the window stayed open), and a
bare-name launch (`PATH=... nativeee ...`, argv[0] without a slash) whose relaunch completed
(`int_work/m_series/input_saves/root_pins/RootPlayRelaunchRestore/bare_name_argv0/`). The Root legs' raw artefacts
(run.meta, frames.fhash, slot .meta, restores/<stamp>/run.meta) are retained under
`int_work/m_series/input_saves/root_pins/` with a re-check recipe for the 119/119 identity.

## Hotkey routing — the s5 "F5 does nothing" bug and its review loop 1 (2026-10-10, round 8)

Where the key was lost: `SessionGui::ProcessEvent`'s strict `event.key.windowID != window_id_` test
(`session_gui.cpp:88` at `main` 8bd76e91). SDL stamps a key event with its keyboard-focus window's id and `0` when it has
recorded none; in a `--harness-window` play session that macOS does not activate (no `SDL_WINDOWEVENT_FOCUS_GAINED` for the
whole run — the s5 session's shape, and the shape of every retained leg tonight) every press arrived `windowID=0` and was
discarded before `HandleFunctionKey`, silently; the pad mapper never saw it either (`focused_` false). Fix (`1b9245dc`): a
press is ours unless SDL names a window that is not ours (`windowID == ours || 0 || SDL_GetWindowFromID(id) != nullptr`;
non-hotkey text for a foreign-of-ours id still declined), `SessionWindow::Open` raises the game window and asks for input
focus (a refusal is not an error), a LOUD `nativeee: F<n> discarded: SDL stamped window id U (session window W)` for any
hotkey the GUI declines, a key-route census in run.meta (`hotkeys=key_events=,function_keys=,discarded=`), the
the per-event attribution trace (`[sdl-trace]`, armed by `--set dev.log_level=narrate`; the integrator replaced the `PS2R_SDL_EVENT_TRACE` getenv — `CoreSingleGetenvSite` allows one site, ConfigLoader's), and `SessionRuntime::InjectFunctionKey(n)` — a REAL SDL_KEYDOWN/KEYUP
pair pushed into SDL's own queue, stamped as SDL stamps a physical press, behind the harness knob
`--set harness.inject_key=F5@N` (`4ae95ea2`, shared `engine/core/config/*`: integration patch
`int_work/m_series/input_saves/integration/f5_routing_config.patch`). Pins: `SessionHotkeyRoute` (`test_session_controls
--hotkey-route`: windowID 0 / the session window's id / InjectFunctionKey through `SessionRuntime::Run`'s pump, each a
Queued SaveReproduction; 15 checks) and `RootPlayHotkeyF5` (`2bdbdb8a`; a 420-frame play leg pressing F5 at boundary 300
through the production route; 14 judge checks + selftest).

Review loop 1 (`F5_ROUTING_REVIEW.md`, NEEDS_CHANGES, items 1–6): the uncommitted revert of the fix found in the tree
(the loop-0 "red arm" experiment's overlay) was dropped (`git checkout --`), `build-release` rebuilt from the committed fix
(tree clean) and both pins re-run on it (SessionHotkeyRoute 0 failures; RootPlayHotkeyF5 Passed + a `--keep` leg 14/14
with `KEYDOWN windowID=0 ... accepted=1` → `Completed`, i.e. the s5 shape itself, fixed). The "red before" was re-done
honestly in a COPY of the build dir with the 8bd76e91 router (old GUI test, no focus request; knob/census/trace kept): the
unit pin is red every run (`F5 discarded: SDL stamped window id 0`, 5 failures); the Root pin was red 2/2 tonight (same
discard line, `hotkeys ... discarded=1`, `savestates/` empty) but PASSED in the loop-0 run because macOS granted focus
then (`event=12`, press stamped with the window's id) — so the Root red is reproducible but focus-dependent and the
deterministic red is the unit pin (`303c75a5` says so in `tests/app/CMakeLists.txt` and the script docstring; the
`2bdbdb8a` message's unqualified claim is superseded). Artefacts, SHA256SUMS and the per-arm README:
`int_work/m_series/input_saves/root_pins/f5/`.

## The recorder's clock — the s4 recording hazard (2026-10-11, round 9; STANDING CHARTER prerequisite 2)

`play_sessions/s4/session.padlog` did not replay (Lagrange: the load-file menu with EMPTY slots from frame ~3000).
Attributed to the RECORDER, not the card: `VerifyHost::OnPadServe` was fed the logical input tick
(`domain.DomainTick()`), which in play is wall-paced (a late boundary consumes the elapsed fields, a pace park the fields
it waited; s4 tick 8823 for 3993 frames) and whose in-park ticks are never served — so a tick-keyed recording of a play
session landed on the wrong frames and LOST every range whose ticks fell in a park. The copied card is byte-identical
to `memcards/` (four identical `Bank*.bin` = four EMPTY slots), the content the s4 session itself saw (`committed=0`).
Fix (`78ec3cee`): the recording is keyed by the GAME FRAME — `host.PresentKey(restored_vsync_frame, host.Boundaries())`
at the MaintDuties pad step, the frames-lane key of the same boundary — and the padlog header names its clock
(`clock=frame`; absent = tick, so every earlier recording/fixture replays as before); `InputLog::Key(tick, frame)`,
`SampleInput(…, tick, frame, …)`, `PadStampDuty::Run(now, script_key)`; run.meta `pad_record_clock=frame`.
Pin (`1218b91b`): `RootPlayRecordReplay` — play `--harness-window` 600 frames replaying a frame-clock script while
recording; the recording's per-frame state == the script's EXACTLY; a deterministic replay of the recording IDENTICAL
(frames+audio) to a direct deterministic run of the script. Red on the frozen `dc1a8a3a` (3 FAIL: three of seven ranges
lost, `differs at 83 frames`), green here (9/9; ctest `Passed 46.96 s`). s5's deterministic replay: two reps identical on
frames/audio/mem but on the title route (`graphics_shadow_scene published=0`; the session reached the beach) — the same
hazard; a title-route workload until a frame-clock session exists. `RootClockContract` stays green (gated A/B against the
frozen binary, 2/2 ALL PASS each; two earlier FAILs were foreign game processes + load 8–12).
F8 live, in-session: design only in `docs/design/F8_LIVE_RESTORE.md` (after Herschel's service-only startup).
Record: `int_work/m_series/input_saves/TRACK_PRODUCT_1.md`, evidence `input_saves/recorder_clock/`.
