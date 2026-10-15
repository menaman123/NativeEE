# M0 / M10–M12 and product obligations — phase record

Owner: Bernoulli (whole phases M0; M10 → M11 → M12; product obligations).
Worktree `port-product`. Status words per phase are at the end; none is `complete`.
This record is the worktree-side counterpart of the HANDOFFs under
`int_work/m_series/input_saves/`. It claims no game-leg result: this owner ran
no game process (parent-only), and F8 runtime restore is refused in the actual
root until service-only startup is accepted.

## M0 — cross-service ownership/order/generation/persistence contracts

Settled and composed in the root (`app/host/ee_bootstrap.cpp`, `SessionRestore`,
`ServiceRestore`, `SessionClock`, `SessionWorkers`, `SectionRegistry`):

| Contract | Where it lives | Pin |
|---|---|---|
| Stable handle + generation; owned immutable job inputs; once-only delivery; callbacks never from host workers | `engine/services/*`, `app/host/service_*` | `SessionRestoreComposition`, `ServiceCheckpoint` tests |
| Logical clock owns completion (GCLK v2 incl. pending tick requests); registered before timers; ticker stopped/joined before GCLK restore | `SessionClock` | `SessionClock`, `CheckpointClock`, `InputSaveClock` |
| Validate before mutation; required-section ownership guard; refusal of missing/mixed/mismatched owners (ISCP) | `StateRestorer::SetImageValidation`, `ServiceRestore::ValidateImage` | `InputSaveComposition`, `SessionRestart` |
| Quiescence refusal named per owner (STREAM boot ran, MTVU, EE event queue, RPC router, delivery meter, active Movie loans, pending durable Save mutation) | `restore_lifecycle.inspect` in the root | `SessionRestoreComposition` (missing owner/prepare failure leave the live worker intact) |
| Allocator bookkeeping follows restored bytes (BUG-085 HEAP/META) | `HeapSection` + arena codec in the root | `SavestateHeapSection`, `RootColdbootProbeResumes` (parent) |

Open for M0 closure (dependencies on other owners, exact names):

| Owner | Needed | Why M0 cannot close alone |
|---|---|---|
| Bacon (M2) | an approved **active Movie** restore contract (decoder/player state or named refusal while loans are owned) | active Movie ownership refuses live restore today |
| Herschel (M3→M5) | STREAM/bank/voice/music pending-work state so `irx_boot.entry_ran` and the asynchronous owners (`EeEventQueue`, `rpc_router`, `delivery_meter`) stop refusing | the root refuses live rewind while STREAM owns running state |
| Zeno (M6→M7) | renderer resource/content identities and fence state at a supported boundary | `graphics_application.CanRestore` is the current gate |

## M10 — input and persistent game saves

Implemented and tested in isolation and in root composition (see
`hle/input/SNAPSHOT_INTEGRATION.md`, `hle/saves/LEGACY_AUDIT.md`,
`hle/saves/CALLERS.md`, `app/gui/ROOT_PRODUCT_INTEGRATION.md`): 17 verified pad
entries, 9 complete save entries, selected `mcInit` → `SaveStorage::Start`,
INPT v2 / SVST / ISCP sections, strict preflight, legacy import, durable atomic
writes, rumble/readiness/disconnect/pressure behaviour. Toggles
`hle.input_service`, `hle.save_storage` default **off** (control arm = the
actual captured PADMAN/MCSERV path).

OLD-bug pins carried onto the native path: BUG-065/074 (sticky READY and
sequence across clock rebase; DP-24's restored sequence is the INPT state, not a
restart at 1), BUG-089 (12-byte Info at the explicit t0 argument), R200/R202/R203,
INPUT_LAG_FINAL root 1/2, SELECT_IMPL, E2_SAVE_IMPL/REVIEW, BUG-036 (joined
incarnations), BUG-085 (via the composed HEAP owner above).

Pending (parent-run, DT-12): the two 13×3 arms and per-leg
`padman_calls`/`pad_stamps`/`mcserv_calls` = 0 with positive
`input_service`/`save_storage` work; the commands are in
`app/gui/ROOT_PRODUCT_INTEGRATION.md`. A leg without a save operation does not
prove persistence retirement; the user's play sessions (below) are the legs
that exercise saves.

## M11 — startup and service cutover

Enumerated in `hle/saves/LEGACY_AUDIT.md` ("M11 startup dependencies"): the
startup loop in `0x17ce50` loads eight IRX from `0x2e6e10` via LoadModule
`0x2d4ac0` (return PC `0x17cf34`); `mcInit` and `padInit` are owned when the
M10 services are selected; `sceSifInitIopHeap 0x2d4558` and the sound remote
init `0x2c4c30` belong to the services/audio owners (Herschel); STREAM/CDVDSTM
to M3. Status: **awaiting dependency** (Herschel: IOP heap/sound/STREAM
startup ownership; Bacon: Movie). No no-op or fabricated success is installed.

## M12 — checkpoint closure and deletion

Blocked on M11 and on every owner's acceptance; nothing deleted. The legacy
INPT v1/PADS/MCHL compatibility stays until the parent accepts the selected
services under the gate.

## Product obligations

| Obligation | State | Evidence |
|---|---|---|
| Input recorder `--record-input [<file>]` | done: explicit first-segment path, exclusive create, run.meta `pad_record_path`; restore segments unchanged | `Coretest_verify_units` (`TestExplicitRecordingPath`), `Coretest_config` |
| Exact two-session commands | `docs/design/PLAY_SESSIONS.md`, `verify/pads/README.md` | — (user plays; not automated) |
| Freezing procedure `source=new` | `verify/freeze_session_leg.py`; LEG.tsv `pad_script`/`mc_dir`; battery `--inputs` + per-rep `mc_before` copy; anchors kind `session` | `VerifyFreezeSessionLegSelftest`, `VerifyBatterySelftest`, `VerifyLegSpecSelftest`, `VerifyAnchorsTable(+Selftest)` |
| Hotkeys F1/F5/F8/F9/F10 on the main-thread loop, shared command queue with the GUI | present (`SessionControls::HandleFunctionKey`, `SessionRuntime::PumpMain/Boundary`) | `SessionControls`, `SessionGui`, `SessionRuntime`, `SessionShutdown` |
| Resize belongs to the presenter; vsync/pacing readers | `SessionRuntime::ConfigurePresentation(video.vsync, video.fps_cap)`; drawable size published; logical target untouched | `SessionPanels` (report), `SessionRuntime` |
| GUI: ImGui vendored as-is under `app/gui/third_party/imgui` (own license, lint-exempt) | moved by `git mv`, SOURCE_SHA256 verifies | `LintOneClass`, `LintSessionTags` |
| GUI: settings window, savestate browser, sound and triage panels | `SessionPanel` + `SavestateBrowser`/`SavestateRows`, `SoundSection`, `TriageSection` | `SessionPanels`, `SessionGui` |
| F5 reproduction save / F8 restore | component-proven; **F8 refused in the real root** until service-only startup; never a usable save/restore claim | `SessionRestoreComposition`, `ROOT_PRODUCT_INTEGRATION.md` |

## Round 2 (2026-10-08, on `main` b39a543)

| Item | State | Evidence |
|---|---|---|
| Refactor-queue item 1: `EeBootstrap::Run` split (the stage-12 allowlist raise 2427→2493 / 2118→2154 removed) | done: fourteen phase functions in construction order across `app/host/ee_bootstrap_*.cpp`, `EeBootstrap::LegState` (`leg_state.h`) hands each phase the earlier objects; bodies moved verbatim; both ee_bootstrap allowlist entries deleted | `LintSizeBudget` PASS (largest phase `InstallDispatch` 283 lines < 300 hard); Release whole tree 0 FAILED; `-DPS2R_DIAGNOSTICS=0/1` `-Werror -fsyntax-only` clean; lane identity of the split root = the parent's five `Root*` checks (no game process here) |
| `ld: warning: ignoring duplicate libraries: engine/core/libps2r_core.a` (test_session_restore) | fixed: `restore_tests.cmake` names `ps2r_core` only when no `ps2r_platform_sdl` carries it | relink of `test_session_restore` without the warning; `SessionRestoreComposition` Passed |
| Play-session recorder command end to end | `RootRecordInputRoundTrip` (parent-run) + `RootRecordInputRoundTripSelftest` | selftest PASS; the game-process test is the parent's |
| `port-input` re-base | `backup/port-input-20261007-r2` created; `port-input-r2` = `main` b39a543: every one of the six commits is cherry-equivalent (`git cherry main port-input` → 6×`-`) and every blob of the 35 files it touched is in `main`'s history — nothing absent, nothing re-applied | HANDOFF table |
| M11 startup contract | explicit request filed: `int_work/m_series/input_saves/REQUEST_HERSCHEL_STARTUP_CONTRACT_2026-10-08.md` (per root: name, signature, owner) | — |

## Round 3 (2026-10-08, on `main` 041f47fc) — gate-5 repairs and the M11 contract

| Item | State | Evidence |
|---|---|---|
| Gate 5b: `--set harness.legacy_input_save=on` refused, the documented selected arm not executable | `--set` accepts the two root acceptance opt-ins (`harness.legacy_input_save`, `harness.live_restore`); files refuse them naming the route | `Coretest_config` (new cases) |
| Gate 5c: `hle.save_storage=on` cold boot aborts ≈ frame 374 (`SaveStorage: game output no longer writable`) | the title's main thread stack is in the scratchpad (TR `src/platform/ps2/entry.cpp:8-9,83`); Poll `0x2a2430` hands GetResult its stack slot. `SaveBindings::RamRange`/`InputBindings::RamRange` admit the scratchpad; `InputSave::RamAccess(MainMemory&, Scratchpad&)` maps it; the root passes `spad` | `SaveStorage` (270 checks), `InputSaveComposition` (116), the 18-test Input/Save set |
| Gate 5c: `hle.input_service=on` frames diverge from key 120 (audio identical) | attributed (`hle/input/READINESS.md` §"Cold-boot frame-lane attribution"): the control arm's OLD 8/214 + case-2 staging vs the owned first-sample Stable, through the title's own sixty-tick reconnect delay and setup (`TR:src/game/pads.cpp:402-536`). Both arms record their availability keys in `run.meta` (`pad_ready=`, `input_service=…,opened=,stable=,setup=`) | `PadProtocol`, `InputService`; the parent's cold-boot pair reads the keys |
| M11 startup contract | consumed in Herschel's shape (`title::ServiceStartup::Inspect(Current(root))`) through `app/host/startup_ownership.{h,cpp}` in `InstallDispatch`: `run.meta service_startup=`, named live-restore refusal; nothing skipped while retained | `StartupOwnership` (32 checks), `ServiceStartup`, `SessionRestoreComposition`; `docs/design/M11_STARTUP_AUDIT.md` |
| Budget allowlist raise | dropped in `main` (`tests/lint/budget_allowlist.txt` has no `ee_bootstrap.cpp` entry) | `LintSizeBudget: 108 warnings, 0 failures -> PASS` |

## Round 4 (2026-10-08, on `main` 45685d8b) — cutover support: the smokes' verdict

| Item | State | Evidence |
|---|---|---|
| `RootPlayModeSmoke` / `RootMovieLifecyclePlay` (and `RootPlayFmvPresents`) turned a CRASHED game process (runner exit 134, run.meta unfinalized, no `video=`) into a ctest SKIP — flips 2 and 3 printed `100% tests passed` over two dead processes | fixed: the judge reads runner exit 0 → finalized=1 → video=gl → pins, in that order; any non-zero runner exit (134 crash, 124/125/137 kills), a missing run.meta or finalized≠1 is a FAIL. SKIP 77 only for absent title files or a COMPLETED run (exit 0, finalized=1) reporting `video=stub` (display-less host) | the test of the test: `tests/app/smoke_fake_runner.py` (a fake `run_bounded.sh` that writes a given run.meta and exits a given code; no game process) drives `--selftest` on both scripts — `RootPlayModeSmokeSelftest` (10 cases), `RootPlayFmvPresentsSelftest` (11 cases); the `main` scripts on the same fake exit 77 (`SKIP: no window (video=<absent>)`), the fixed ones exit 1 |
| `--game-files` parsed but absent from `nativeee --help` (the PLAY_SESSIONS.md command could not be checked by "--help shows the flags") | documented in `kUsage` and the header grammar; `Coretest_config` pins that `Usage()` names every flag the parser accepts (26) and that `--game-files` sets `paths.game_files` | `./build-release/app/host/nativeee --help` line 16; `Coretest_config Passed`; shared `engine/core/config/*` → isolated patch `bernoulli-13` |
| `EeBootstrap::Run` split allowlist | nothing raised: `budget_allowlist.txt` carries no `ee_bootstrap` entry; `InstallDispatch` is 313 lines = WARN(hard) (fail line 400), the orchestrator's refactor-queue item, not an allowlist | `LintSizeBudget: 109 warnings, 0 failures -> PASS` |

## Round 5 (2026-10-09, on `main` 7e5f0abb) — the s3 play bug: F5 wrote nothing, said nothing

| Item | State | Evidence |
|---|---|---|
| F5 silent under all-on (s2/s3: `logs/reproductions/` created, empty; no stderr; the refusal only in the menu) | fixed: F5 / `--set harness.save_at_frame=N` share `SessionCapture::Capture/Poll` → `<save root>/savestates/slot<save.slot>.ps2st` + `.meta` (frame, wall_ms, build_id, composition = every hle.* key + profile, section tags); `SessionControls::SetObserver` delivers Queued / admission refusal / terminal / cancelled OUTSIDE the lock → `nativeee: savestate F5 save|F8 restore <Phase>: <message>` on stderr + SAVE log; the exhausted wait is `No admissible boundary in N consecutive boundaries; the last owner to refuse: <owner>: <reason>`; the on-screen route is the existing notice/menu | `SavestateSlots` (39 checks), `SessionControls` (+2 cases, 210 checks), `RootPlaySaveRefusal` (real process: run.meta `savestate=refused,... graphics: Graphics owned pending=12,failed=0`, stderr Queued + Refused lines) |
| F8 "does nothing" (capability false → "unavailable", menu only) | F8 = `SessionRelaunch` when live restore is not approved: preflight the selection or the newest slot against the live registry (`required section missing: <TAG> (<owner>)`), close, `main` execv's the same binary + `--load-state <slot>` (`SavestateSlots::RelaunchArguments`); F8 with nothing selected → newest slot (`SetRestoreFallback`) | `SavestateSlots` (Preflight/Latest/RelaunchArguments), `SessionControls` (fallback); the execv route is root-untested (no key injector in the harness — next boundary) |
| `--load-state <F5 slot>` on the all-on binary | works end to end: `RootPlaySaveRestore` (TIMEOUT 600) — leg A `--frames 180 --set harness.save_at_frame=60` writes the slot (40 sections), leg B `--load-state <slot> --frames 120` finalizes, `restored_vsync_frame=60`, every restored frame key 61..179 hashes equal to the uninterrupted key k+1 (119/119; the constant shift is the restored-lane key convention, reported) | `RootPlaySaveRestore Passed 7.42 s` (worktree Release, all-on compiled defaults) |
| Blockers removed to get there (each named by the refusal text first) | (1) `Thread 2: logical stack may contain omitted frames (depth=1, inside fn=0x001e7f80)`: the EE RPC server thread sleeps in the SDK-bound `sceSifRpcLoop` (opaque body) → `SdkBindingManifest::MarkWaitBody` + root wrapper (guest frame + proof `restart_wait=TSW_SLEEP`); (2) `SessionClock::Validate` compared the saved bypass with the domain latch a `--load-state` precedes → the tick source's selection, field named; (3) `TextureSelection::PreflightSnapshot` refused a boot image's unconstructed (all-zero) TextureTable → admitted with no TXAS assets (Zeno's module: isolated patch `bernoulli-16d`) | `ThreadCapture`, `SessionClock`, `TextureSelection` ctests green; the b60 leg logs under `int_work/m_series/input_saves/bernoulli/root_play_save_restore_allon_b60_r6*.log` |
| F8 relaunch end to end in a live window | `harness.restore_at_frame=N` presses F8 in a play session; `RootPlayRelaunchRestore` (TIMEOUT 600): a play-mode F5-path slot at 60, F8 at 100 → stderr Queued → Completed → Relaunch, `park_detail=session_closed`, the relaunched process (same pid) restores and runs 400 frames (`restores/<stamp>/run.meta finalized=1 frames=400 restored_vsync_frame=60 video=gl park=none`) | `RootPlayRelaunchRestore Passed 41.00 s` (worktree Release, all-on defaults, a display) |
| The remaining user-facing blocker | from the renderer's first owned recording (≈ b70) every SyncV boundary is refused `graphics: Graphics owned pending=N` (GraphicsOutput::CanCheckpoint: recorded owned groups are host closures until Link); under the retained graphics arm `graphics: Graphics unsubmitted=8 (recording open before Link)` and at the title `graphics_resources: Graphics shared surfaces: unsupported target/alias state` — all Zeno's capture-time contract. F5 in play refuses LOUDLY with that line | `RootPlaySaveRefusal`; legs `root_play_save_restore_{allon_b75,allon_b90,allon_b105,retained_b120,retained_r6}.log` |

## Phase status

- M0: **implementing** (contracts settled and composed; cross-owner closure awaits Bacon/Herschel/Zeno restore contracts; the startup refusal is now the named consumer list).
- M10: **acceptance failing (gate 5)** → repairs committed on `port-product` (CLI route, scratchpad admission, frame-120 attribution); **awaiting integration** and the parent's re-run of the selected arm and the cold-boot pair.
- M11: **implementing** (Herschel's contract consumed; every module retained today; load-loop/heap gating lands with the first release — awaiting dependency on Herschel's M4 transport owner and Bacon's Movie restore contract).
- M12: **awaiting dependency** (M11 and all owners' acceptance).
- Product obligations: recorder/commands/freeze/hotkeys/readers/GUI **in `main`** (round 2 merged; `RootRecordInputRoundTrip` Passed under the parent); F5/F8/`--load-state` savestates **awaiting integration** (round 5: `ed5423bc`, `a97aa01e`; patches `bernoulli-16a..f`) and **awaiting dependency** for an in-play capture (Zeno: the Graphics owned-pending capture contract); F8 live restore **awaiting dependency** (M11) — composed as the relaunch meanwhile.
