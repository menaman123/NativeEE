# Phase 7 — implementation record (`ipu`/FMV + host device backends, MODULE GATE only)

**Phase:** 7 (CONSTRUCTION_PLAN §4.7 to the module gate; no lane claim, no integration wiring)  
**Branch:** `phase7` (worktree `<repo>-p7`)  
**Start HEAD:** `78b5655 Phase 4 WIP (4A-4B'')`  
**Plan:** [PHASE7_PLAN.md](PHASE7_PLAN.md) (frozen for the stages; deviations are §7.1 rows below)  
**Rules:** [STAGE_RULES.md](STAGE_RULES.md); orchestrator decisions `/tmp/p7/DECISIONS.md`; facts `/tmp/p7/INVESTIGATION.md` (INV)

One section per stage (7A–7E), each ending in a HANDOFF in the §9 template form. Pre-flight before 7A
(the last green stop of the branch point): `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`
→ `100% tests passed, 0 tests failed out of 88` (quoted from `/tmp/p7/7A/preflight.log`, 2026-09-27).

---

## Stage 7A — audio sink (2026-09-27)

Scope row: PHASE7_PLAN §7 "7A audio sink"; module rows §2.3; design §3.1; tests §2.5 + §4.4.
`pwd && git branch --show-current` → the worktree path and `phase7` before the first edit.

### Files landed

| Path | Lines | What |
|---|---|---|
| `engine/platform/sdl/audio_pull_ring.h` | 73 | `AudioPullRing` — NEW mechanism (§3.1): SPSC stereo float ring, `kQuantumFrames = 800`, `kCapacityQuanta = 8` (`kCapacityFrames = 6400`), `Push` / `Pull` / `Available`, counters `pushed / pulled / dropped / underruns / overruns`; monotonic 64-bit positions, release/acquire atomics |
| `engine/platform/sdl/audio_pull_ring.cpp` | 122 | overrun → discard the OLDEST audio up to the next quantum boundary of the stream position (CAS on the read position; `overruns++`, `dropped += n`); underrun → zero-fill the remainder (`underruns++`); a pull that raced a drop discards its copy and re-reads; never blocks either side |
| `engine/platform/sdl/sdl_audio_sink.h` | 57 | `SdlAudioSink` — `Open(const platform::AudioSpec&, AudioPullRing&)` / `Close()` / `Running()` + the negotiated `DeviceSampleRate/Channels/BufferFrames` accessors; the "caller decides whether to open" rule stated in the header |
| `engine/platform/sdl/sdl_audio_sink.cpp` | 92 | `SDL_InitSubSystem(SDL_INIT_AUDIO)` inside `Open`; `SDL_AudioSpec{freq = spec.sample_rate, AUDIO_F32SYS, 2 ch, 512 samples}`; `SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0)`; `SDL_PauseAudioDevice(dev, 0)`; callback `= ring.Pull(out, len / (2 * sizeof(float)))`; idempotent `Open`; `Close` = `SDL_CloseAudioDevice` + `SDL_QuitSubSystem(SDL_INIT_AUDIO)`, idempotent; destructor closes |
| `engine/platform/sdl/README.md` | 63 | body replaced: purpose / Belongs (rows for both classes) / the NEW-mechanism note (§3.1 consequences) / Does-not-belong / OLD provenance table / rules |
| `engine/platform/CMakeLists.txt` | +13 (append-only `# Phase 7` block) | `find_package(SDL2 CONFIG REQUIRED)`; `add_library(ps2r_platform_sdl STATIC sdl/audio_pull_ring.cpp sdl/sdl_audio_sink.cpp)`; PUBLIC include `${CMAKE_SOURCE_DIR}`; `-Wall -Wextra -Werror`; `target_link_libraries(PUBLIC ps2r_platform SDL2::SDL2)` |
| `tests/platform_sdl/CMakeLists.txt` | 14 | `ps2r_platform_sdl_test(name ctest_name)` helper (links `ps2r_platform_sdl`, `-Wall -Wextra -Werror`, TIMEOUT 120); ctests `AudioPullRing`, `SdlAudioSinkDummy` |
| `tests/platform_sdl/test_audio_pull_ring.cpp` | 263 | the six §4.4 contracts as real assertions (see "Tests added") |
| `tests/platform_sdl/test_audio_sink.cpp` | 91 | `SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy")` before `Open`; open / running / negotiated format == request / idempotent open / close / close / reopen; `SKIP: SDL audio unavailable` + exit 0 when `SDL_INIT_AUDIO` itself fails |
| `tests/CMakeLists.txt` | +4 (append-only `# Phase 7` block) | `add_subdirectory(platform_sdl)` |
| `docs/phases/PHASE7_IMPL.md` | this file | created |

`git -P diff --stat engine/platform/CMakeLists.txt tests/CMakeLists.txt` → `2 files changed, 17 insertions(+)` (0 deletions; both blocks at end of file).

### OLD sources transplanted (read-only, `$PS2R_OLD_ROOT`)

| NEW | OLD `file:line` |
|---|---|
| `SdlAudioSink::Open` spec + `allowed_changes = 0` + start | `host_app/sound/mixer.cpp:38-43`, `:45-49`, `:54`; idempotence `:36`; doc block `mixer.h:1-29` |
| `SdlAudioSink::Callback` (frames = `len / (2 * sizeof(float))`) | `mixer.cpp:17-21` — `MixInto(out, frames)` replaced by `ring.Pull(out, frames)` |
| `SdlAudioSink::Close` / `Running` | `mixer.cpp:62-69`, `:71` |
| `AudioPullRing` quantum + producer contract | `host_app/verify/verify_boundary.cpp:22` (`kFramesPerVsync = 48000 / 60`), `:60-73` (`MixInto(b.mixbuf, 800)` once per boundary, then `hasher.Push`); `sound_core.h:114-118` (the `MixInto` contract the ring replaces) |
| NOT written | `mixer.cpp:26-35` (`Enabled()` / `TickClocked()` gates — the caller's `Config` rule), `:77-82` (`MixerLockAudioImpl/Unlock`, no consumer here), `:86-92` (`DefaultConfigPathImpl`, superseded by `Config`) |

### Flags applied

| Flag | Outcome |
|---|---|
| `PS2_SND_TICKCLOCK` | read only (NORM in Phase 4's count, FLAG_LEDGER `:695`): the device-clock arm is `time.tick_sync = false`; the sink has no gate — the composition root simply never calls `Open` under the deterministic profile. No env site, no `Config` edit |
| `PS2_SOUND` (`Enabled()` gate at `mixer.cpp:26`) | not written (caller's rule; Phase 4 row) |

No flag of §5's 12-row P7 count is consumed by 7A (the count is unchanged).

### Tests added

| ctest | TU | Assertions |
|---|---|---|
| `AudioPullRing` | `tests/platform_sdl/test_audio_pull_ring.cpp` | **arithmetic** (`static_assert` 800 / 8 / 6400; `Available()` after `k` pushes `== 800k`, `k ≤ 8`, counters) · **order** (25 quanta of a counting pattern pushed while pulling 512-frame chunks; concatenated output `memcmp`-equal to the input; 0 overruns/underruns/dropped; `pulled == pushed`) · **underrun** (512 from empty → 1024 zero floats, `underruns == 1`, `pulled == 512`; 300 pushed → 300 real + 212 zero, `underruns == 1`) · **overrun** (9 quanta → `overruns == 1`, `dropped == 800`, next pull `==` quantum 2's bytes; after a 512 pull a 10th push drops exactly the 288-frame rest of quantum 2 and the next pull opens quantum 3) · **no drift** (10^5 push-800 / pull-512 iterations; `pulled == pushed − dropped − Available()` checked every 1000 steps; 0 underruns; overruns > 0) · **SPSC** (producer thread 10^4 quanta, consumer thread pulling 512 until done+empty: real frames strictly increasing, every gap resumes on a quantum boundary, L/R pairs consistent, `real == pushed − dropped`) |
| `SdlAudioSinkDummy` | `tests/platform_sdl/test_audio_sink.cpp` | dummy driver: `Open` true, `Running()` true, `DeviceSampleRate/Channels/BufferFrames == 48000/2/512`, `SDL_GetCurrentAudioDriver() == "dummy"`, second `Open` true, 4 quanta pushed without blocking, `Close` → not running, `Close` again idempotent, `SDL_WasInit(SDL_INIT_AUDIO) == 0` after close, reopen works |

Result lines (`/tmp/p7/7A/t1.log`, `/tmp/p7/7A/finish.log`):

```
spsc: pushed=8000000 pulled=5772288 dropped=2792000 overruns=6976 underruns=1668 real=5208000
test_audio_pull_ring: PASS (arithmetic, order, underrun, overrun, no-drift, spsc)
test_audio_sink: PASS (dummy driver: open/running/idempotent-open/close/close/reopen; ring pushed=3200 pulled=1024)
1/2 Test #89: AudioPullRing ....................   Passed    0.06 sec
2/2 Test #90: SdlAudioSinkDummy ................   Passed    0.11 sec
```

(`pulled=1024` in the sink test = two real 512-frame dummy-device callbacks pulled from the ring.)
Stability: `test_audio_pull_ring` run 10× in a loop → 10/10 PASS. TSan probe (out of tree, not a preset:
`clang++ -std=c++17 -O1 -g -fsanitize=thread -I. engine/platform/sdl/audio_pull_ring.cpp tests/platform_sdl/test_audio_pull_ring.cpp`)
→ PASS with no `WARNING: ThreadSanitizer` line on the run taken (recorded, not required — §4.4).

### Finish checks (quoted from `/tmp/p7/7A/finish.log`)

```
$ cmake --build --preset macos-dev -j4 2>&1 | grep -c "warning:"
0
$ ctest --preset macos-dev 2>&1 | tail -3
100% tests passed, 0 tests failed out of 90

Total Test time (real) = 172.30 sec
$ python3 tests/lint/lint_one_class.py --root . | tail -1
LintOneClass: 320 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . | tail -1
LintEnginePurity: 277 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py --root . 2>&1 | tail -2
generated exemption games/<title>/iop/stream_irx/iop_recompiled.cpp: 5 getenv arms (frozen tool output; expected 5)
getenv sites: 1 -> PASS
$ git status --short
 M engine/platform/CMakeLists.txt
 M engine/platform/sdl/README.md
 M tests/CMakeLists.txt
?? docs/phases/PHASE7_PLAN.md          (the plan-writer's file, untracked at the branch point; not touched by 7A)
?? engine/platform/sdl/audio_pull_ring.cpp
?? engine/platform/sdl/audio_pull_ring.h
?? engine/platform/sdl/sdl_audio_sink.cpp
?? engine/platform/sdl/sdl_audio_sink.h
?? tests/platform_sdl/
   (+ docs/phases/PHASE7_IMPL.md, this file, written after the listing)
```

Gate (b) greps: `grep -n "clock\|chrono\|MixInto\|engine/sound" engine/platform/sdl/*.{h,cpp}` → only the two comment
words "tick-clocked"/"no clock" in `audio_pull_ring.h:2,12`; no `<chrono>`, no clock call, no `MixInto`, no include of
`engine/sound`. Gate (f) include set of `engine/platform/sdl`: `engine/platform/platform.h` (from `sdl_audio_sink.h`,
for `AudioSpec`) and the two module headers — nothing else.

### §7.1 findings rows (stage 7A)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P7-TESTS-PLATFORM-SDL-DIR | plan → 7A | `tests/` mirrors module paths (`tests/{core,gs,ee,iop}`); the sink lives under `engine/platform/sdl/` and CONSTRUCTION §4.7 names only `tests/{ipu,input}`; a `tests/platform/` dir would collide with the Phase-1 platform stubs' future tests | Landed as `tests/platform_sdl/` (module path with the platform folder flattened); `add_subdirectory(platform_sdl)` in the `# Phase 7` block of `tests/CMakeLists.txt`. Plan deviation recorded |
| P7-SINK-FILE-NAME | 7A | The plan (§2.3, §7 row, D4) names the pair `audio_sink.{h,cpp}` for class `SdlAudioSink`; `LintOneClass` rule 5 (file = `snake_case(PrimaryType)`) rejects it: `audio_sink.h:20: file name 'audio_sink' should be 'sdl_audio_sink'` | Files are `engine/platform/sdl/sdl_audio_sink.{h,cpp}` (class name unchanged; the test stays `test_audio_sink.cpp` / ctest `SdlAudioSinkDummy` as planned). Later plan/README references to `audio_sink.*` mean this pair |
| P7-RING-READ-CAS | 7A | Drop-oldest on overrun requires the PRODUCER to move the read position, so the ring is not a textbook SPSC (read owned by one side). A pull racing a drop could copy overwritten frames | Read position advances by compare-exchange from both sides; `Pull` re-reads when its CAS fails (the copy it made is discarded). Drops land on a quantum boundary of the stream position (a partially consumed oldest quantum loses only its tail — pinned by the `TestOverrun` 10th-push case and the SPSC gap check). Documented in the header + README; TSan probe clean on the run taken |

### HANDOFF (7A → 7B)

**Done:** `engine/platform/sdl/{audio_pull_ring.h,audio_pull_ring.cpp,sdl_audio_sink.h,sdl_audio_sink.cpp,README.md}`,
`tests/platform_sdl/{CMakeLists.txt,test_audio_pull_ring.cpp,test_audio_sink.cpp}`, `# Phase 7` append blocks in
`engine/platform/CMakeLists.txt` (`find_package(SDL2 CONFIG REQUIRED)` + `ps2r_platform_sdl`) and `tests/CMakeLists.txt`
(`add_subdirectory(platform_sdl)`), this record. OLD transplanted: `host_app/sound/mixer.cpp:17-21`, `:36`, `:38-49`, `:54`,
`:62-71`; `mixer.h:1-29`; producer contract `verify/verify_boundary.cpp:22`, `:60-73`, `sound_core.h:114-118`. Flags:
`PS2_SND_TICKCLOCK` → read only, caller's `time.tick_sync` rule (no site); `PS2_SOUND` gate → not written. Tests:
`AudioPullRing`, `SdlAudioSinkDummy` → `100% tests passed, 0 tests failed out of 90`. OLD bounded runs: none (no OLD
binary executed in 7A). Ledger/doc deltas: `engine/platform/sdl/README.md` rewritten; §7.1 rows P7-TESTS-PLATFORM-SDL-DIR
(closed), P7-SINK-FILE-NAME, P7-RING-READ-CAS.  
**Not done (moved to 7B):** nothing from the 7A row. 7B's own scope is untouched: `engine/platform/sdl/input/**`
(`input_backend.h`, `pad_sample`, `input_mapper`, `kbm_backend`, `gamepad_convert`, `gamepad_backend`, `input_system`,
`input_settings.h`, `default_bindings.ini`, `README.md`), the configure-time `input_default_bindings.h` generator appended to the
`ps2r_platform_sdl` block, `tests/input/**` + `add_subdirectory(input)` appended inside the tests block.  
**Module gate status:** (a) – (7C–7E) · (b) ✔ — `AudioPullRing` green on the six §4.4 contracts, `SdlAudioSinkDummy` PASS
(real dummy callbacks: `pulled=1024`), gate-(b) grep quoted above, README carries the NEW-mechanism row · (c) – (7B) ·
(d) ✔ — 0 warnings, 90/90, both lints PASS, `CoreSingleGetenvSite` = 1 (`getenv sites: 1 -> PASS`), `SettingsTableReproducible` /
`FlagPhaseCountsReproducible` untouched and green · (e) ✔ — `git status` = owned paths + 2 append blocks (17 insertions, 0
deletions; `platform.h` untouched) · (f) ✔ for `engine/platform/sdl` (include set = `platform.h` + own headers) · (g) partial —
this section; INTEGRATION CHECKLIST and numeric summary are 7E's.  
**Integration checklist:** §8.2 item 1 (Mixer pump → `AudioPullRing::Push(mixbuf, 800)` AFTER the `AudioHasher` push, same
buffer; `SdlAudioSink::Open` only when `time.tick_sync == false`) — documented only; the sink's `Open` signature is
`Open(const platform::AudioSpec&, AudioPullRing&)` as D1 requires. Note for the savestate owner: no `Lock/Unlock` bracket exists
on the sink (OLD `mixer.cpp:77-82`); add if `core/savestate` needs the device paused during load.  
**Next command:** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect `100% tests passed … out of 90`),
then read `OLD/host_app/input/{input_backend.h,input_mapper.{h,cpp},input_backend_kbm.{h,cpp},input_backend_gamepad.{h,cpp},
input_system.{h,cpp},default_bindings.ini,test_input_mapper.cpp}`, `pad_input.cpp:461-570`, `:779-817`, `host_app/CMakeLists.txt:104-113`;
write `engine/platform/sdl/input/*` per §2.4 and `tests/input/*` per §2.5; expect `ctest -R 'InputMapper|PadlogReplay|BindingsRoundtrip'`
→ 3/3.

---

## Stage 7B — input (2026-09-27)

Scope row: PHASE7_PLAN §7 "7B input"; module rows §2.4; design §3.2; tests §2.5 + §4.1/§4.5; facts INV §C.
`pwd && git branch --show-current` → the worktree path and `phase7` before the first edit. `/tmp/p7/review/7B_findings.md`
did not exist at start. The pre-registered 7B′ split was NOT needed (finish checks at ~17 min).

### Files landed

| Path | Lines | What |
|---|---|---|
| `engine/platform/sdl/input/input_backend.h` | 52 | `InputBackend` interface (`Poll` / `IsActive` / `Name`; MAIN thread after `SDL_PollEvent`), `NormalizedInput` POD (sticks float [-1,+1] y down+, buttons ACTIVE HIGH, mouse dx/dy, 3 mouse buttons), `enum LogicalButton` in DualShock 2 wire-bit order `kBtnSelect = 1<<0 … kBtnSquare = 1<<15`. SDL-free |
| `engine/platform/sdl/input/pad_sample.h` / `.cpp` | 27 / 26 | `PadSample` POD (= OLD `PS2PadState`: `uint16 buttons` active-high, `lx/ly/rx/ry` neutral `0x80`); free functions `ToInputState` (the ONE inversion `wire = ~buttons`, axes copied) and `FromInputState` |
| `engine/platform/sdl/input/input_mapper.h` / `.cpp` | 74 / 296 | `InputMapper` + `Bindings` POD + `StickToByte` / `ScancodeFromName` / `ParseMouseName`; `ParseIni` grammar verbatim (sections `sticks/buttons/gamepad`, `;`/`#` comments, strict floats, deadzone `[0,1)`, sensitivity `[0.01,2]`, `key=value` before a section → reject, WHOLE-FILE rejection with the caller's `Bindings` untouched); `LoadConfig(path)` (absent → false, default stays) and `LoadConfigText(text, origin)` public; `Map(in, dt)` (mouse accumulate `d·sens·0.01`, decay `exp(−decay·dt)`, snap `< 0.5/127.5`, clamp, right-or-left stick, `StickToByte`, `buttons & 0xFFFF`); `ButtonsFromKeys` / `KeyStickFromKeys` (opposites cancel) / `LogicalForScancode` / `SetMouseSensitivity` / `ResetMouseAccum`. `SDL_GetScancodeFromName` is the pair's ONLY SDL call; includes the generated `input_default_bindings.h` |
| `engine/platform/sdl/input/gamepad_convert.h` / `.cpp` | 40 / 76 | `GamepadConvert` (static `Convert(raw, deadzone, out, sensitivity = 1.33f)`, static `Stick(rawx, rawy, deadzone, sensitivity, outx, outy)` = OLD `Pcsx2Stick` verbatim) + `GamepadRaw {int16 axis[6]; uint8 button[21]}` with `static_assert` against `SDL_CONTROLLER_AXIS_MAX` / `SDL_CONTROLLER_BUTTON_MAX`; the 14-row button table, triggers `> 32767/4` → L2/R2. Header SDL-free |
| `engine/platform/sdl/input/keyboard_mouse_backend.h` / `.cpp` | 43 / 74 | `KeyboardMouseBackend : InputBackend` — `SDL_GetKeyboardState` → mapper tables, `SDL_GetRelativeMouseState`, capture on `SDL_MOUSEBUTTONDOWN`+LEFT unless `SetUiCaptured(true)`, release on ESC / `SDL_WINDOWEVENT_FOCUS_LOST`, deltas only while captured, `mouse_buttons = LMASK/MMASK/RMASK`; `InjectKey(scancode)` overlay as MEMBER state (`inject_[512]`, `merged_[512]` — OLD's `static s_merged` removed), consumed per `Poll`; `static_assert(kNumScancodes == SDL_NUM_SCANCODES)`. File name per lint rule 5 (P7-KBM-FILE-NAME) |
| `engine/platform/sdl/input/gamepad_backend.h` / `.cpp` | 91 / 201 | `GamepadBackend : InputBackend` + `PadInfo` POD — ALL state instance-owned (`selection_`, `preferred_guid_`, `deadzone_`, `sensitivity_`, `enabled_`, `pads_`, `snapshot_`; no statics, no atomics, no mutex — main thread only, HACK_LEDGER `:346`); `kSelectionAuto = -1`, `kSelectionNone = -2`; ctor `(deadzone, sensitivity, enabled, gamecontrollerdb_path)` = `SDL_SetHint(JOYSTICK_ALLOW_BACKGROUND_EVENTS)`, `SDL_InitSubSystem(GAMECONTROLLER)`, `SDL_GameControllerAddMappingsFromFile(path)` only when the caller supplies a path, enumerate/open; generic fallback mapping for unmapped joysticks; `SDL_JOYDEVICEADDED/REMOVED`; reconnect-by-GUID; multi-pad merge (buttons OR, sticks strictly-greater magnitude); `Snapshot()`; destructor closes pads and `SDL_QuitSubSystem` |
| `engine/platform/sdl/input/input_system.h` / `.cpp` | 81 / 164 | `InputSystem` — owns `InputMapper` + both backends + `InputSettings`; `Init(settings)` (bindings file over the baked default when `bindings_path` non-empty; gamepad knobs: `InputSettings` < bindings file's `[gamepad]` when it loaded < device-config file; backends constructed; `LoadDeviceConfig(settings.device_config_path)`); `HandleEvent(const SDL_Event&)`; `Frame(dt) → PadSample` (kbm first, gamepad second; kbm polled but DISCARDED when `kbm_enabled_ == false`; merge `buttons |=`, sticks `|cand| > |cur| ? cand : cur`, `mouse_dx/dy +=`, `mouse_buttons ||=`; `Map`; `last_` published; UI capture → `ResetMouseAccum` + neutral); `SetUiCapture` forwards to the kbm backend; `LoadDeviceConfig(path)` / `SaveDeviceConfig(path)` (`pad = auto|none|guid:<GUID>`, `kbm`, `deadzone`, `sensitivity`) through caller-supplied paths. No `getenv`, no `SDL_GetBasePath`, no clock, no selftest grammar, no `PS2_INPUT=0` arm, no latency/ring-log/stick-trace probes |
| `engine/platform/sdl/input/input_settings.h` | 21 | `InputSettings` POD: `bindings_path`, `device_config_path`, `gamecontrollerdb_path`, `kbm_enabled = true`, `gamepad_enabled = true`, `gamepad_deadzone = 0.15f`, `gamepad_sensitivity = 1.33f` |
| `engine/platform/sdl/input/default_bindings.ini` | 38 | values verbatim from OLD `:6-37`; header comment reworded (grammar summary; no title name, no narrative) |
| `engine/platform/sdl/input/README.md` | 56 | purpose / Belongs (9 rows) / precedence rule / Does-not-belong / OLD provenance / rules |
| `engine/platform/sdl/README.md` | ±1 | the `input/` row filled in |
| `engine/platform/CMakeLists.txt` | +32 net (the `# Phase 7` block EXTENDED in place) | block comment widened to §2.4; generator: `set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS <ini>)`, `file(READ)` → `file(WRITE ${PS2R_GENERATED_DIR}/input_default_bindings.h)` with `inline const char* const kDefaultBindingsIni = R"PS2INI(...)PS2INI";` in `ps2r::platform::sdl`; the 6 input `.cpp` added to `ps2r_platform_sdl`; `target_include_directories(PRIVATE ${PS2R_GENERATED_DIR})` |
| `tests/input/CMakeLists.txt` | 17 | `ps2r_input_test(name ctest_name)` helper (links `ps2r_platform_sdl`, `-Wall -Wextra -Werror`, TIMEOUT 120); ctests `InputMapper`, `PadlogReplay`, `BindingsRoundtrip` (argv[1] = the source `default_bindings.ini`) |
| `tests/input/test_input_mapper.cpp` | 562 | all nine OLD cases + a `PadSample`↔`InputState` case (see "Tests added") |
| `tests/input/test_padlog_replay.cpp` | 131 | the §4.5 replay contract over two synthetic specs |
| `tests/input/test_bindings_roundtrip.cpp` | 157 | test-local `SerializeBindings` via `SDL_GetScancodeName`; default == file; fixed point; custom table; rejection |
| `tests/CMakeLists.txt` | +1 (inside the existing `# Phase 7` block) | `add_subdirectory(input)` |

`git -P diff --stat engine/platform/CMakeLists.txt tests/CMakeLists.txt` → `2 files changed, 45 insertions(+)` (0 deletions
against the branch point; the 7A block was extended, not duplicated). `touch default_bindings.ini` + build → CMake re-ran
(`Re-running CMake` / `Configuring done` observed, `/tmp/p7/7B/regen.log`).

### OLD sources transplanted (read-only, `$PS2R_OLD_ROOT/host_app/`)

| NEW | OLD `file:line` |
|---|---|
| `input_backend.h` | `input/input_backend.h:14-22` (`NormalizedInput`), `:27-36` (`LogicalButton`), `:38-46` (interface) |
| `pad_sample` | `input/input_mapper.h:17-23` (`PS2PadState`); `pad_input.cpp:783-785` (`buttons = ~state.buttons`, axes copied) |
| `input_mapper` | `input/input_mapper.h:26-43` (`Bindings`), `:49-59`, `:61-93`; `input_mapper.cpp:21-28` (`StickToByte`), `:30-43`, `:47-53` (aliases), `:61-70` (`ParseMouseName`), `:72-79` (strict float), `:81-93` (button names), `:99-206` (`ParseIni`), `:213-221` (ctor), `:224-238` (`LoadConfig/Text`), `:242-255`, `:257-301` (`Map`); generator `host_app/CMakeLists.txt:104-113` |
| `gamepad_convert` | `input/input_backend_gamepad.h:25-28` (`GamepadRaw`), `:37-38`; `.cpp:16-21` (static_asserts), `:45-73` (`Pcsx2Stick`), `:75-113` (`GamepadConvert`) |
| `keyboard_mouse_backend` | `input/input_backend_kbm.h:7`, `:13-31`; `.cpp:15-17`, `:19-27`, `:29-45` (events), `:47-87` (`Poll`) — `UICaptured()` global → `SetUiCaptured` member; `static Uint8 s_merged` → member `merged_` |
| `gamepad_backend` | `input/input_backend_gamepad.h:42-43`, `:47-52`, `:54-100`; `.cpp:117-192` (statics + mutex → members), `:194-233` (ctor; `SDL_GetBasePath` + cwd lookup `:214-221` → caller path), `:235-242`, `:248-318` (open / close / snapshot), `:333-344`, `:346-358` (events), `:360-416` (`Poll`; `:365-368`, `:396-402`, `:414-415` stick-trace NOT written), `:418-422` |
| `input_system` | `input/input_system.h:22-27`, `:37-54`; `.cpp:37-47` (state), `:85-127` (`LoadDeviceConfig`), `:175-208` (`SaveDeviceConfig`), `:222-243` (bindings + backends), `:305-340` (poll / merge / map / publish / UI-neutral); NOT written: `:77-83` (`DeviceConfigPath` via `SDL_GetBasePath`), `:126-155` + `:287-303` (selftest), `:209-220` (`PS2_INPUT` gate), `:274-284` (steady_clock dt), `:349-389` (INLAT/ring-log/stick-trace probes) |
| `default_bindings.ini` | `input/default_bindings.ini:6-13`, `:15-27`, `:30-37`; `:1-5` (narrative header) dropped |
| tests | `input/test_input_mapper.cpp:44-93` (T1), `:95-182` (T2), `:184-210` (T3), `:212-272` (T5), `:274-359` (gamepad tables), `:361-367` (`StickToByte`), `:377-440` (held stick), `:442-488` (jitter), `:490-630` (stick semantics), `:632-645` (main); `tests/CMakeLists.txt:2150-2170` (link set; `-Wno-error` NOT carried — the port compiles under `-Werror`) |
| read, nothing to port | `input/input_latency.h` (pure DIAG probes) |

### Flags applied

| Flag | Outcome |
|---|---|
| `PS2_INPUT` | NORM → input is always constructed when the composition root constructs `InputSystem`; the OFF arm (`input_system.cpp:214-220`, neutral pad + stderr) is NOT written. Headless legs simply never construct the class (harness CLI, PHASE7_PLAN §3.2) |
| `PS2_INPUT_SELFTEST` | TEST → `ParseSelftestSpec` / the `"300-500:Y"` default / the `g_input_selftest_expect` handshake NOT written; `InputLog` replay (Phase 1) is the ONE pad-script mechanism. `KeyboardMouseBackend::InjectKey` stays as the generic per-frame overlay mechanism (no grammar, no env) |
| `PS2_INPUT_LAT` | DIAG → `input_latency.h` and the `[INLAT-T1]` / ring-log census sites (`input_system.cpp:349-380`) NOT written |
| `PS2_STICKTRACE` | (Phase 4's count) DIAG → `sticktrace::RecordRaw/RecordMap` hooks NOT written |
| `PS2_PAD_SCRIPT` | (Phase 4's count) TEST → not touched here; `test_padlog_replay` exercises the NEW `InputLog` recorder against the mapper output |

Three of §5's 12 P7 rows consumed (`PS2_INPUT`, `PS2_INPUT_SELFTEST`, `PS2_INPUT_LAT`); the count is unchanged.

### Tests added

| ctest | TU | Assertions |
|---|---|---|
| `InputMapper` | `tests/input/test_input_mapper.cpp` | **T1** default key table (9 keys → bits, `LogicalForScancode`, `LShift` alias → `SDL_SCANCODE_LSHIFT` → SQUARE, Mouse2 → CIRCLE via `Map`, Z unbound, Space+Y OR) · **T2** mouse accumulate/decay (monotonic to `0xFF` at dx=200, settle to `0x80/0x80` within 60 idle frames + no drift over 240, linearity `|d2 − 2·d1| ≤ 1`, `100 px @ 0.35 → rx == 172`, `SetMouseSensitivity(0.70) → d ≈ 89`, left stick untouched) · **T3** W/S/A/D → `0x00/0xFF/0x80`, diagonal, neutral · **T5** default parses (10 keys, 1 mouse, right stick, 0.35/12.0, WASD scancodes, gamepad 1/0.15/1.33), override replaces, 9 malformed inputs rejected (incl. NEW: deadzone ≥ 1, sensitivity > 2, unterminated `[buttons`) with the default intact, comments accepted, missing file → default kept · **gamepad tables** 14 buttons, trigger 50 % → L2 / 12.5 % → none, 0.10 snap, full deflection rails, 0.16 pass-through ×1.33, full chain `ly 0xFF` + CROSS · **StickToByte** 5 pins · **held-stick** 3 × 600 frames invariant · **jitter** 5 + 3 rail values ≥ 0.95, monotonic band · **stick semantics** 72 angles `len ≥ 0.995`, 5° drift rails, 45° → (0.92..0.96), angle preserved at 0.6 tilt with len = 0.6·1.33, radial snap, monotonic tilt→speed reaching ≥ 1.3, right stick, bytes `0xF4..0xFA`, sens 1.0 = raw circle · **PadSample wire** (NEW) neutral → `0xFFFF/0x80×4`, `wire == ~buttons`, axes copied, `FromInputState` round trip |
| `PadlogReplay` | `tests/input/test_padlog_replay.cpp` | all 256 axis bytes survive `StickToByte((b−127.5)/127.5)`; spec `10-20:CROSS,15-30:LX=200,25-25:START,35-39:SQUARE,5-8:RY=0` over 40 frames and `0-3:LY=255,4-7:LY=0,2-5:L2,2-5:R2,2-5:SELECT,2-5:L3,6-6:TRIANGLE` over 12: per frame the served (`~wire`) buttons and axes equal the spec's state; the recorder's tokens (after `Flush(frames−1)`) are byte-identical to the spec's tokens (sorted multiset + per-token string equality); the recording re-parses to the same press/axis counts |
| `BindingsRoundtrip` | `tests/input/test_bindings_roundtrip.cpp` | baked default `==` the on-disk `default_bindings.ini`; `SerializeBindings` (test-local, `SDL_GetScancodeName` + inverse `ParseMouseName`, `%.9g` floats) → `LoadConfigText` → identical `Bindings`, and the serialization is a fixed point; a custom table (left-stick mouse, all 16 buttons incl. 3 mouse, `LCtrl`, gamepad 0/0.25/1) round-trips; `cross = ` (empty value) appended to the good text rejects the whole file and leaves the bindings byte-identical; a malformed line AFTER valid lines also rejects; the good text loads afterwards |

Result lines (`/tmp/p7/7B/t1.log`):

```
91: test_input_mapper: 208/208 checks passed / ALL PASS
1/3 Test #91: InputMapper ......................   Passed    0.24 sec
92:   spec   : 10-20:CROSS,15-30:LX=200,25-25:START,35-39:SQUARE,5-8:RY=0
92:   record : 5-8:RY=0,10-20:CROSS,25-25:START,15-30:LX=200,35-39:SQUARE
92:   spec   : 0-3:LY=255,4-7:LY=0,2-5:L2,2-5:R2,2-5:SELECT,2-5:L3,6-6:TRIANGLE
92:   record : 0-3:LY=255,2-5:SELECT,2-5:L3,2-5:L2,2-5:R2,6-6:TRIANGLE,4-7:LY=0
92: test_padlog_replay: PASS (axis round trip, 2 synthetic padlogs byte-identical)
2/3 Test #92: PadlogReplay .....................   Passed    0.21 sec
93: test_bindings_roundtrip: PASS (default == file, serialize/parse fixed point, custom table, rejection)
3/3 Test #93: BindingsRoundtrip ................   Passed    0.21 sec
```

(The recorder emits tokens in range-CLOSE order, so the order differs from the spec; the comparison is on the token
multiset with every token string identical — the grammar equivalence of INV C4.)

### Finish checks (quoted from `/tmp/p7/7B/finish.log`)

```
$ cmake --build --preset macos-dev -j4 2>&1 | grep -c "warning:"
0
$ ctest --preset macos-dev 2>&1 | tail -3
100% tests passed, 0 tests failed out of 93

Total Test time (real) = 185.14 sec
$ python3 tests/lint/lint_one_class.py --root . | tail -1
LintOneClass: 337 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . | tail -1
LintEnginePurity: 292 files scanned, 0 game tokens -> PASS
$ python3 tests/core/check_getenv_sites.py --root . 2>&1 | tail -2
generated exemption games/<title>/iop/stream_irx/iop_recompiled.cpp: 5 getenv arms (frozen tool output; expected 5)
getenv sites: 1 -> PASS
$ git status --short
 M engine/platform/CMakeLists.txt
 M engine/platform/sdl/README.md
 M tests/CMakeLists.txt
?? docs/phases/PHASE7_IMPL.md
?? docs/phases/PHASE7_PLAN.md
?? engine/platform/sdl/audio_pull_ring.cpp
?? engine/platform/sdl/audio_pull_ring.h
?? engine/platform/sdl/input/
?? engine/platform/sdl/sdl_audio_sink.cpp
?? engine/platform/sdl/sdl_audio_sink.h
?? tests/input/
?? tests/platform_sdl/
```

Gate (c)/(f) greps: `grep -c "getenv\|SDL_GetBasePath\|chrono" engine/platform/sdl/input/*.cpp` → 0 in every file; the
input headers include only `<cstdint>/<string>/<vector>/<utility>/<memory>`, their own siblings and (from `pad_sample.h`)
`engine/platform/platform.h`; `SDL_Event` appears in headers only as `union SDL_Event;`. `<SDL.h>` is included by the six
input `.cpp` files and the two SDL-using tests.

### §7.1 findings rows (stage 7B)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P7-KBM-FILE-NAME | 7B | The plan (§2.4, §7 row, D4) names the pair `kbm_backend.{h,cpp}` for class `KeyboardMouseBackend`; `LintOneClass` rule 5 (file = `snake_case(PrimaryType)`) rejects it — the same hazard as P7-SINK-FILE-NAME | Files are `engine/platform/sdl/input/keyboard_mouse_backend.{h,cpp}` (class name unchanged). Later plan/README references to `kbm_backend.*` mean this pair |
| P7-GAMEPAD-CONVERT-CLASS | 7B | OLD's `GamepadConvert` is a free function; the plan names it as a class row and the file-name rule wants a primary type for `gamepad_convert.h` | `GamepadConvert` is a class with two static members: `Convert(...)` (= OLD `GamepadConvert`) and `Stick(...)` (= OLD `Pcsx2Stick`, now callable from tests). Same math, byte-for-byte |
| P7-INPUT-KNOB-PRECEDENCE | 7B | `InputSettings` carries `gamepad_deadzone/sensitivity/enabled` (task/plan §2.4) while the bindings INI also carries a `[gamepad]` section and the device-config file a third copy; OLD had only the latter two (INI → device file) | Precedence fixed and documented in `input_system.h` + README: `InputSettings` (Config) is used when NO bindings file loaded; a loaded bindings file's `[gamepad]` values win over it; the device-config file wins over both. `settings.gamepad_enabled = false` disables the pad regardless. With the shipped defaults all three agree (0.15 / 1.33 / on), so the OLD behaviour is preserved for every existing configuration |
| P7-KBM-CAPTURE-PREDICATE | 7B | Plan §2.4 says UI-capture suppression "via a caller-supplied predicate" | Implemented as a plain member flag `KeyboardMouseBackend::SetUiCaptured(bool)` that `InputSystem::SetUiCapture` forwards — no callback type, no cross-thread read (main thread only). Behaviour identical to OLD's `UICaptured()` check at `input_backend_kbm.cpp:36` |
| P7-GAMEPAD-DB-PATH | 7B | OLD loaded `gamecontrollerdb.txt` from the exe dir (`SDL_GetBasePath`) AND the cwd (`input_backend_gamepad.cpp:214-221`) | Engine loads from `InputSettings::gamecontrollerdb_path` only when non-empty (one file); locating the file is the composition root's (§8.2 integration). No `SDL_GetBasePath` in engine |

### HANDOFF (7B → 7C)

**Done:** `engine/platform/sdl/input/{input_backend.h, pad_sample.h, pad_sample.cpp, input_mapper.h, input_mapper.cpp,
gamepad_convert.h, gamepad_convert.cpp, keyboard_mouse_backend.h, keyboard_mouse_backend.cpp, gamepad_backend.h,
gamepad_backend.cpp, input_system.h, input_system.cpp, input_settings.h, default_bindings.ini, README.md}`;
`tests/input/{CMakeLists.txt, test_input_mapper.cpp, test_padlog_replay.cpp, test_bindings_roundtrip.cpp}`; the
`# Phase 7` block of `engine/platform/CMakeLists.txt` EXTENDED (generator → `${PS2R_GENERATED_DIR}/input_default_bindings.h`
with `CMAKE_CONFIGURE_DEPENDS`, six input sources on `ps2r_platform_sdl`, PRIVATE generated include dir) and of
`tests/CMakeLists.txt` (`add_subdirectory(input)`); `engine/platform/sdl/README.md` `input/` row; this record. OLD
transplanted: `host_app/input/input_backend.h:14-46`, `input_mapper.h:17-93`, `input_mapper.cpp:21-301`,
`input_backend_kbm.{h:7,13-31; cpp:15-87}`, `input_backend_gamepad.{h:25-100; cpp:16-21,45-113,117-422}`,
`input_system.{h:22-54; cpp:37-47,85-127,175-208,222-243,305-340}`, `default_bindings.ini:6-37`, `pad_input.cpp:783-785`,
`host_app/CMakeLists.txt:104-113`, `test_input_mapper.cpp:44-645`. Flags: `PS2_INPUT` → NORM (always on, OFF arm not
written); `PS2_INPUT_SELFTEST` → TEST (not written); `PS2_INPUT_LAT` → DIAG (not written); `PS2_STICKTRACE` → not written.
Tests: `InputMapper` (208/208), `PadlogReplay`, `BindingsRoundtrip` → `100% tests passed, 0 tests failed out of 93`. OLD
bounded runs: none (no OLD binary executed in 7B). Ledger/doc deltas: §7.1 rows P7-KBM-FILE-NAME, P7-GAMEPAD-CONVERT-CLASS,
P7-INPUT-KNOB-PRECEDENCE, P7-KBM-CAPTURE-PREDICATE, P7-GAMEPAD-DB-PATH.  
**Not done (moved to 7C):** nothing from the 7B row; the 7B′ split was not taken. 7C's own scope is untouched:
`engine/ipu/{ipu_device, mpeg_decode}.{h,cpp}`, `ipu_settings.h`, `engine/ipu/README.md`, `engine/ipu/CMakeLists.txt`, the
root `CMakeLists.txt` `# Phase 7` block (`add_subdirectory(engine/ipu)` — the root has NO Phase 7 block yet; 7C creates it),
`tests/ipu/{CMakeLists.txt, test_ipu_device.cpp, test_mpeg_decode.cpp}` + `add_subdirectory(ipu)` appended inside the
existing `# Phase 7` block of `tests/CMakeLists.txt`.  
**Module gate status:** (a) – (7C–7E) · (b) ✔ (7A) · (c) ✔ — `InputMapper` 9 OLD cases + wire case green, `PadlogReplay`
byte-identical tokens on two synthetic padlogs, `BindingsRoundtrip` fixed point + rejection; `LintEnginePurity` PASS over the
`.ini`; `LintOneClass` PASS over `tests/input` · (d) ✔ — 0 warnings, 93/93, both lints PASS, `getenv sites: 1 -> PASS`,
`SettingsTableReproducible` / `FlagPhaseCountsReproducible` untouched and green · (e) ✔ — `git status` = owned paths + the
2 extended blocks (45 insertions, 0 deletions vs the branch point; `platform.h`, `core/**` untouched) · (f) ✔ for
`engine/platform/sdl/input` (include set quoted above; `<SDL.h>` in `.cpp` only) · (g) partial — this section; INTEGRATION
CHECKLIST and numeric summary are 7E's.  
**Integration checklist:** §8.2 input items (documented only): the composition root constructs `InputSystem` only in play
mode, fills `InputSettings` from `Config::input` (bindings/device-config/gamecontrollerdb paths resolved by the root — no
exe-relative lookup in engine), pumps `SDL_PollEvent → InputSystem::HandleEvent`, calls `Frame(dt)` once per main-loop
iteration with ITS `dt` (wall delta in play mode, clamped as it sees fit), converts with `ToInputState` and publishes the
snapshot for `Platform.poll_input`; Phase 4's `PadProtocol::Stamp(InputState)` at `Step::MaintDuties` is the consumer
(§3.2). `SaveDeviceConfig(path)` is the GUI's call. `InputSystem::Init` prints its three `input:` stderr lines — route through
`core/log` at integration if the root prefers.  
**Next command:** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect `100% tests passed … out of 93`),
then read `OLD/host_app/ipu.{h,cpp}` (all), `memory.cpp:618-627`, `:1059-1068`, `libipu.cpp:6-15`, `:67-105`,
`dmac.cpp:1500-1550`, NEW `engine/ee/mem/mmio_handler.h`, `mmio_router.h:16-57`; write `engine/ipu/*` per §2.1 and
`tests/ipu/{test_ipu_device,test_mpeg_decode}.cpp` per §2.5; expect `ctest -R 'IpuDevice|MpegDecode'` → 2/2.

## Stage 7C — ipu device + decoder (2026-09-27)

Scope row: PHASE7_PLAN §7 "7C ipu device + decoder"; module rows §2.1; tests §2.5 (`test_ipu_device`, `test_mpeg_decode`) +
§4.2; facts INV §A6. `pwd && git branch --show-current` → the worktree path and `phase7` before the first edit.
`/tmp/p7/review/7C_findings.md` did not exist at start. No split was pre-registered; none was needed (finish checks at ~18 min).

### Files landed

| Path | Lines | What |
|---|---|---|
| `engine/ipu/ipu_device.h` / `.cpp` | 134 / 345 | `IpuDevice : ::MmioHandler` (namespace `ps2r::ipu`) — CMD/CTRL/BP/TOP at offsets `0x00/0x10/0x20/0x30` decoded from the low six address bits (`kWindowBytes = 0x40`); the class names NO absolute address. `Read/Write(addr, size)` reproduce OLD's bus split: sub-word reads see the byte lanes of the aligned word, sub-word writes store the whole value into the aligned word, 8-byte accesses are two consecutive words (the upper word reads 0 / drops). `WriteCmd` dispatch (ECD/SCD cleared, `cmd = bits 31:28`), `ReadCmd` (FDEC result + BUSY bit 31), `WriteCtrl` (RST bit 30 only: clear both FIFOs, `bp = 0`, `busy = false`, DC predictors zeroed), `ReadCtrl` (IFC/OFC/CBP/ECD/SCD/IDP/BUSY; reports the counts as of the previous recomputation THEN recomputes `min(size/16, 8)` — OLD's order), `ReadBp` (`bp & 0x7F | ifc << 8`), `ReadTop` (peek 32 or 0). FIFO face `FeedInputFifo` / `DrainOutputFifo` (recompute IFC/OFC), `HasOutputData/OutputSize/InputSize/IsBusy`; observers `BitPosition/IntraQuantMatrix/NonIntraQuantMatrix/Threshold0/1/DivergentCommandCount/UnhandledWriteCount`. Commands: BCLR, SETIQ (64 bytes through zigzag; bit 27 → non-intra; no-op when fewer than 64 bytes past the cursor), SETTH (8-bit latches), FDEC (skip FB, peek 32 → result else 0), CSC (RGB32 only; cursor advances by MBC·384·8 regardless), IDEC (skip FB, DC reset 128/256/512 by IDP, loop until start code `0x000001` / bits end / 10000 MBs, `scd` set on start code). BDEC + VDEC/SETVQ/PACK/unknown → `ExecuteDivergent` (counter + `PS2R_LOG(IPU, Warn, …)`); BDEC also clears BUSY as OLD. `enum class IpuCommand` |
| `engine/ipu/mpeg_decode.h` / `.cpp` | 85 / 306 | `MpegDecode` (all static) + method-less PODs `BitCursor {data, size, bit_pos}` and `VlcEntry {code, len, value}`: `Zigzag()`, `DefaultIntraQuantMatrix()`, `DcLumTable()` (B-12), `DcChromTable()` (B-13); `HasBits/PeekBits/ReadBits/SkipBits` (MSB-first, past-end peeks 0); `DecodeDcCoeff` (first table match in table order, sign rule `diff < 1<<(size-1) → diff -= (1<<size)-1`); `DecodeAcCoeffs` (EOB `10`, escape `000001`+run6+level12 signed, `1`+sign ±1, else skip one bit); `InverseDct` (float, `cosf((2x+1)·u·π/16)` with π as double exactly as OLD's `M_PI` promotion; `roundf`, clamp [-256, 255]); `DecodeBlock` (IQ `(coef·iq·qsc) >> 3` stored through int16 THEN clamped [-2048, 2047]; `block[0] = dc_pred << (3 - idp)`); `DecodeIntraMacroblock` (Y0..Y3, Cb, Cr → RGB32 appended to a caller vector); `YCbCrToRgb32` (fixed-point 1436/352/731/1815 >> 10, SGN +128, A = 0x80); `CscPixelRgb32` (the CSC command's float-coefficient truncating form, ABGR word). No globals |
| `engine/ipu/ipu_settings.h` | 30 | `IpuSettings` POD: `decode_ahead_pictures = 64` (P7-LEDGER-CAP comment), `sectors_per_pump = 16`, `max_sector_budget = 4096`; rule stated per field |
| `engine/ipu/CMakeLists.txt` | 11 | `ps2r_ipu STATIC (ipu_device.cpp mpeg_decode.cpp)`, `PUBLIC ${CMAKE_SOURCE_DIR}`, `-Wall -Wextra -Werror`, `PUBLIC ps2r_core`; comment: hle sources come in 7D |
| `engine/ipu/README.md` | 56 | skeleton body replaced: purpose / Belongs (3 rows + `hle/`) / Does-not-belong / the `// diverges:` table (7 rows, every stub enumerated) / OLD provenance rows / not-transplanted list / rules |
| `CMakeLists.txt` (root, SHARED) | +6 appended at EOF | the root's `# Phase 7` block created: `add_subdirectory(engine/ipu)` (append-only; `git -P diff` = additions only) |
| `tests/ipu/CMakeLists.txt` | 13 | `ps2r_ipu_test(name ctest_name)` helper (links `ps2r_ipu`, `-Wall -Wextra -Werror`, TIMEOUT 120); ctests `IpuDevice`, `MpegDecode` |
| `tests/ipu/test_ipu_device.cpp` | 336 | 10 cases (see "Tests added"); free functions + CHECK macro only (lint-clean) |
| `tests/ipu/test_mpeg_decode.cpp` | 300 | 7 cases; test-local `BitsFromString` helper |
| `tests/CMakeLists.txt` (SHARED) | +2 inside the existing `# Phase 7` block | `add_subdirectory(ipu)` |

Total new: 1,616 lines (engine 967, tests 649). `git -P diff CMakeLists.txt tests/CMakeLists.txt` → additions only (quoted in
`/tmp/p7/7C/finish.log` run 2).

### OLD sources transplanted (read-only, `$PS2R_OLD_ROOT/host_app/`)

`ipu.h:10-21` (commands), `:24-37` (CTRL fields), `:41-66` (API), `:96-98` (FIFOs; `:113` `g_ipu` NOT transplanted);
`ipu.cpp:13-56` (tables), `:58-68` (ctor defaults), `:72-99` (bit reader), `:102-123` (dispatch), `:125-130` (CMD read),
`:138-148` (RST), `:150-165` (CTRL read; `:161-162` ifc/ofc), `:167-171` (BP), `:174-178` (TOP), `:189-205` (FIFO face; `:191`,
`:203`), `:208-216` (BCLR), `:220-236` (SETIQ), `:240-245` (SETTH), `:250-261` (FDEC), `:267-270` (BDEC stub), `:275-318` (CSC;
`:294-296` pixel equations → `CscPixelRgb32`, `:307-312` RGB32-only append), `:323-371` (IDEC), `:376-391`, `:394-417`, `:423-461`,
`:465-511`, `:516-548`, `:554-600` (`MpegDecode`). Bus access model: `memory.cpp:1152-1174` (read split), `:4455-4466` (write
split), register map `:618-627`, `:1059-1068`. Read for context only: `libipu.cpp:6-15`, `:67-105` (RST+BCLR init dance; Sync → 0),
`dmac.cpp:1500-1550` (ch3 feeds `FeedInputFIFO`, ch4 drains `DrainOutputFIFO`, both `CompleteChannel` synchronously — the coupling is
`engine/dmac`'s). `ReadCmd64`/`ReadTop64` (`ipu.cpp:132-136`, `:180-184`) NOT transplanted: unreachable in OLD (its 64-bit I/O read
is two 32-bit register reads), which `IpuDevice::Read(…, 8)` reproduces. `g_logFile` narration → `PS2R_LOG(IPU, Narrate|Warn, …)`.
`IpuSettings` values: `mpeg_hle.cpp:1228-1245`, `:1679-1680`.

### Flags applied

| Flag | Outcome |
|---|---|
| `PS2_FMV_DECODE_CAP` | ledgered DELETE; the default-on arm IS the baseline path (P7-LEDGER-CAP, plan §5.5) → written as the constant `IpuSettings::decode_ahead_pictures = 64`; the `cap == 0` eager arm NOT written; no `Config` field |

No other §5 row has its OLD `getenv` site in `ipu.{h,cpp}` (it reads no env). The 12-row count is unchanged.

### Tests added

| ctest | TU | Assertions |
|---|---|---|
| `IpuDevice` | `tests/ipu/test_ipu_device.cpp` | fresh state (CTRL/CMD/BP/TOP read 0, default matrices); RST clears FIFOs/bp/BUSY and non-RST CTRL bits are ignored, IFC recomputes on the next CTRL read (OLD order pinned); BCLR clears the input FIFO, sets bp from bits 7:0, IFC = 0, leaves the output FIFO; BUSY never reads 1 after any of the 11 command codes and `DivergentCommandCount() == 5` (BDEC/VDEC/SETVQ/PACK/0xF); IFC for 0/15/16/47/128/129/4096 bytes = `min(n/16, 8)` in CTRL and BP; OFC read-then-recompute + drain recompute (64 → 8, 40 bytes → 2, drain-to-empty); SETIQ 64 bytes land at `matrix[zigzag[i]]`, bp += 512, bit 27 selects non-intra, 63 bytes → no-op; SETTH latches TH0/TH1; FDEC skip 0 / 4 / 32 (+ TOP and BP agree; exactly 32 bits left peeks the tail word); CSC of 7 constant-colour macroblocks == `ReferenceCsc` written in the test from the BT.601 float equations (256/256 pixels each, alpha 0x80), MBC beyond the FIFO converts what is present and still advances the cursor, OFM = 1 produces 0 bytes; MMIO face: TOP at every width, CMD write issues FDEC and reads back, 64-bit CMD write drops the high word (counted), CTRL RST through the bus, BP/TOP/unused-offset writes dropped and counted, the same device answers at base `0x0` and `0x7FFFFF80` |
| `MpegDecode` | `tests/ipu/test_mpeg_decode.cpp` | zigzag is a bijection of 0..63 with the Figure 7-2 anchors; default intra matrix corners; DC-only IDCT is flat at `round(dc/8)` (±1 float rounding) for 10 values, exact `640 → 80`, a first-row AC term varies along x only; bit reader MSB-first + past-end 0; B-12/B-13 codes at 12 spot rows + prefix-freeness of both tables; `DecodeDcCoeff` +3/−3/size-0/chroma +5/−9/size-8 255/missing-differential/empty; AC: EOB-first, `11` → −1, escape run 5 level 127, escape level 4094 → −2; `YCbCrToRgb32` grey/SGN/chroma 189-55-100/quadrant placement |

```
1/2 Test #94: IpuDevice ........................   Passed    0.01 sec
2/2 Test #95: MpegDecode .......................   Passed    0.24 sec
test_ipu_device: PASS (fresh, rst, bclr, busy, ifc/ofc, setiq, setth, fdec, csc, mmio)
test_mpeg_decode: PASS (zigzag, idct, bits, dc-vlc, dc-coeff, ac-coeff, ycbcr)
```

(First run of `MpegDecode` FAILED on a test-side error — the spot check read `1111 1110` as DC size 8; Table B-12 makes it size 9
(size 8 is the 7-bit `1111 110`). The test was corrected; the table matched OLD/the spec all along.)

### Finish checks (quoted from `/tmp/p7/7C/finish.log`, second run — the first run had the `[Y0]…[Y3]` comment hit below)

```
== warnings
0
== ctest
100% tests passed, 0 tests failed out of 95
== lint_one_class
LintOneClass: 344 files scanned, 0 findings -> PASS
== lint_engine_purity
LintEnginePurity: 298 files scanned, 0 game tokens -> PASS
== getenv
getenv sites: 1 -> PASS
== git status   (owned paths + the shared append blocks only)
 M CMakeLists.txt
 M engine/ipu/README.md
 M tests/CMakeLists.txt
?? engine/ipu/CMakeLists.txt  ipu_device.cpp  ipu_device.h  ipu_settings.h  mpeg_decode.cpp  mpeg_decode.h
?? tests/ipu/
(+ the 7A/7B paths already present at start)
== includes (gate item (f) for engine/ipu)
engine/ipu/ipu_device.cpp:7:#include "engine/core/log/log.h"
engine/ipu/ipu_device.h:24:#include "engine/ee/mem/mmio_handler.h"
```

### §7.1 findings rows (stage 7C)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P7-IPU-NO-BASE | 7C | Plan §2.1 says "register model (`0x10002000/10/20/30`)" and D4 "Register it over [0x10002000, 0x10002040)"; the task forbids the class from taking an absolute address | `IpuDevice` decodes the register from `addr & 0x3F`; the router's `Register(base, 0x40, …)` over a 64-byte-aligned base is the integration item (§8.2). The test proves the same device answers at two unrelated bases. No I/O-range literal exists under `engine/ipu` |
| P7-IPU-CTRL-READ-ORDER | 7C | OLD `ReadCtrl` (`ipu.cpp:150-165`) assembles the word from the stale IFC/OFC and recomputes AFTERWARDS, so the first CTRL read after RST/CSC reports the pre-event count | Kept byte-for-byte (game-visible through `sceIpuSync`'s CTRL poll and any CTRL read the recompiled `_clearOnce/_clearEach` bodies make); pinned by `TestRstClearsState` / `TestIfcOfc`; documented in the header comment as the observable order, not as a hack |
| P7-IPU-SETTH-8BIT | 7C | OLD latches the 9-bit TH0/TH1 fields into `uint8_t` (`ipu.cpp:241-242`), truncating bit 8 | Kept (no reader of the thresholds exists — alpha is the constant 0x80; `// diverges:` row in the README). Root fix (9-bit latches + threshold alpha) belongs with an IDEC/CSC consumer, which the title has none of |
| P7-LINT-BRACKET-COMMENT | 7C | `LintOneClass` rule 4 flags `[Y0]…[Y3]` in a layout comment as session tags (`TAG_RE` accepts `[A-Z]{1,3}\d+`) | Comment reworded ("Y0 Y1 over Y2 Y3"). Note for 7D/7E: no bracketed `[Name0]`-style tokens in comments |
| (process) | 7C | one inline `python3 -` was used to append the two lines to `tests/CMakeLists.txt` (STAGE_RULES 2 says helper scripts are files under `/tmp/p7/<stage>/`) | recorded; every later helper (`/tmp/p7/7C/finish.sh`) is a file. No effect on the tree |

### HANDOFF (7C → 7D)

**Done:** `engine/ipu/{ipu_device.h, ipu_device.cpp, mpeg_decode.h, mpeg_decode.cpp, ipu_settings.h, CMakeLists.txt, README.md}`;
`tests/ipu/{CMakeLists.txt, test_ipu_device.cpp, test_mpeg_decode.cpp}`; the root `CMakeLists.txt` `# Phase 7` block CREATED
(`add_subdirectory(engine/ipu)`, appended at EOF); `tests/CMakeLists.txt` `# Phase 7` block EXTENDED (`add_subdirectory(ipu)`);
this record. OLD transplanted: `ipu.h:10-98`, `ipu.cpp:13-600` (every function except `ReadCmd64`/`ReadTop64` and the `g_ipu`
global), bus split `memory.cpp:1152-1174`/`:4455-4466`, map `:618-627`/`:1059-1068`. Flags: `PS2_FMV_DECODE_CAP` → constant 64
(P7-LEDGER-CAP). Tests: `IpuDevice`, `MpegDecode` → `100% tests passed, 0 tests failed out of 95`. OLD bounded runs: none (no OLD
binary executed in 7C). Ledger/doc deltas: §7.1 rows P7-IPU-NO-BASE, P7-IPU-CTRL-READ-ORDER, P7-IPU-SETTH-8BIT,
P7-LINT-BRACKET-COMMENT (+ one process note).  
**Not done (moved to 7D):** nothing from the 7C row. 7D's own scope is untouched: `engine/ipu/hle/{guest_memory.h, fmv_pcm_sink.h,
fmv_sector_source.h, dmac_channel_face.h, fmv_context_layout.h, pss_demux, video_decoder.h, ffmpeg_video_decoder, frame_writer,
fmv_pacer, fmv_audio_capture, mpeg_context, mpeg_api}` + `hle/README.md` rows + the §8.1 binding-table copy; `cmake/FindFFmpeg.cmake`
(included from `engine/ipu/CMakeLists.txt` — the root block stays `add_subdirectory(engine/ipu)` only); `ps2r_ipu` gains the hle
sources + `FFmpeg::*`; `tests/ipu/{test_pss_demux.cpp, pss_census.py, test_frame_writer.cpp, test_fmv_pacer.cpp,
test_fmv_audio_capture.cpp}` registered through the existing `ps2r_ipu_test()` helper (or a sibling helper for the OLD-gated
`argv[1] = ${PS2R_OG_ROOT}` form). `IpuSettings` (7C) is the POD `MpegContext`/`MpegApi` read their cap/pump constants from.  
**Module gate status:** (a) – (7D–7E; `IpuDevice`, `MpegDecode` green is the 7C share) · (b) ✔ (7A) · (c) ✔ (7B) · (d) ✔ — 0
warnings, 95/95, both lints PASS, `getenv sites: 1 -> PASS`, `SettingsTableReproducible` / `FlagPhaseCountsReproducible` untouched
and green · (e) ✔ — `git status` = owned paths + 3 shared append blocks (root block additions-only at EOF; tests block extended in
place; `engine/platform/CMakeLists.txt` unchanged by 7C) · (f) ✔ for `engine/ipu` — includes quoted above: `engine/core/log/log.h`
and `engine/ee/mem/mmio_handler.h` only (the latter header-only; `ps2r_ipu` links `ps2r_core` alone) · (g) partial — this section;
INTEGRATION CHECKLIST and numeric summary are 7E's.  
**Integration checklist:** §8.2 items touched (documented only): the composition root owns ONE `IpuDevice` and calls
`MmioRouter::Register(<IPU I/O base>, IpuDevice::kWindowBytes, ipu, "IPU")`; `engine/dmac` ch3/ch4 call `FeedInputFifo` /
`DrainOutputFifo` (OLD `dmac.cpp:1512`, `:1543`, both completing synchronously); the IPU FIFO port window stays unmapped
(P7-IPU-FIFO-PORT); `LibipuApi` (7E) drives `WriteCtrl(kCtrlRst)` + `WriteCmd(BCLR)` and polls `ReadCtrl() & kCtrlBusy`.  
**Next command:** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect `100% tests passed … out of 95`), then
read `OLD/host_app/mpeg_hle.{h,cpp}` per the 7D row, `tools/pss_census.py`, `tests/test_fmv_pace.cpp`; create
`cmake/FindFFmpeg.cmake` and the five interface headers first; expect `ctest -R 'PssDemux|FrameWriter|FmvPaceDeadline|FmvAudioCapture'`
→ 4/4.

## Stage 7D — ipu/hle, first half of the pre-registered split (2026-09-27)

Scope row: PHASE7_PLAN §7 "7D ipu/hle" — executed as the **7D′ split's "STOP with" list** by orchestrator instruction (the
remainder is stage 7D′, a separate later stage): the five interface headers, `fmv_context_layout.h`, `pss_demux`,
`frame_writer`, `fmv_pacer`, `fmv_audio_capture`, `cmake/FindFFmpeg.cmake`, and the tests `PssDemux` (synthetic half AND
the OLD-gated census half — both landed), `FrameWriter`, `FmvPaceDeadline`, `FmvAudioCapture`. Module rows §2.2; tests
§2.5/§4.1/§4.2; design §3.3; flags §5 (§5.5). Facts: INV §A1 (a, c, d), A2, A5, A7, A8. `pwd && git branch --show-current`
→ the worktree path and `phase7` before the first edit. `/tmp/p7/review/7D_findings.md` did not exist at start.

### Files landed

| Path | Lines | What |
|---|---|---|
| `engine/ipu/hle/guest_memory.h` | 27 | `GuestMemory` pure interface: `Translate(addr, len) → uint8_t*`, `Read32`, `Write32` |
| `engine/ipu/hle/fmv_pcm_sink.h` | 29 | `FmvPcmSink` pure interface: `Push(const int16_t* stereo, uint32_t frames)`, `GateOpen()`, `Flush()` |
| `engine/ipu/hle/fmv_sector_source.h` | 24 | `FmvSectorSource` pure interface: `PumpSectors(uint8_t* dst, uint32_t max_sectors) → uint32_t` |
| `engine/ipu/hle/dmac_channel_face.h` | 27 | `DmacChannelFace` pure interface: `Read/WriteChcr`, `Read/WriteMadr`, `Read/WriteTadr`, `Read/WriteQwc` per channel (3 = IPU_FROM, 4 = IPU_TO) — the words of the 9-word sceIpuStopDMA snapshot |
| `engine/ipu/hle/decoded_picture.h` | 17 | `DecodedPicture {int width, height; std::vector<uint8_t> rgba}` method-less POD (its own header: a POD is not a primary type, so it does not collide with `VideoDecoder`) |
| `engine/ipu/hle/video_decoder.h` | 34 | `VideoDecoder` interface: `Feed(data, size, std::deque<DecodedPicture>&)`, `Flush(deque&)`, `AuthoredFps() const`, `Shutdown()` |
| `engine/ipu/hle/fmv_context_layout.h` | 51 | `FmvContextLayout` POD of ctx offsets: `ring_base, ring_size, ring_write, ring_read, ring_anchor, inner_status_ptr, total, play_state, mode, plane_select, plane0, plane1, target_w, target_h, abort_flag` — NO defaults, no values; each field's libmpeg meaning in the header comment |
| `engine/ipu/hle/pss_demux_listener.h` | 26 | `PssDemuxListener` interface: `OnVideoEs(const uint8_t*, size_t, bool contains_sequence_end)`, `OnAudioPes(const uint8_t*, size_t)`, `OnProgramEnd()` |
| `engine/ipu/hle/pss_demux.h` / `.cpp` | 77 / 210 | `PssDemux(listener&)`: `Append`, `Process(final_chunk)`, `Reset`, `MarkEnded`, `Ended`, `Buffered`, `Consumed`, `PacksSeen`, `SystemHeadersSeen`; static `FindStartCode`, `ContainsSequenceEnd`, `IsVideoStreamId`, `IsAudioStreamId`, `IsLengthPrefixedHeader` {BB,BC,BE,BF,F0,F1,F2,F8,FF}, `ParsePesPayloadOffset`. The OLD `ProcessBuffer` walk minus the census: pack 12 / `14 + (buf[13] & 7)`, length-prefixed skip, `pkt_len == 0` search from 6, 3-byte tail keep unless final, video → `OnVideoEs(payload, has_end)`, audio → `OnAudioPes(payload)`, `0xB9` → `OnProgramEnd()`; the stream-ended flag is the demuxer's (`ended_`: program end OR a video payload with `00 00 01 B7` OR `MarkEnded`) so both of OLD's `stream_ended` / `end_after_pending` outcomes stop the walk identically — which one the context records is `MpegContext`'s (7D′) |
| `engine/ipu/hle/frame_writer.h` / `.cpp` | 38 / 90 | `FrameWriter` static: `Rgb32Bytes(w, h)`, `Rgb16Bytes(w, h)`, `WriteRgb32Strips(dst, pic)`, `WriteRgb16Strips(dst, target_w, target_h, pic)` — the INV A1.c loops byte-for-byte (16-px column strips × `aligned_h` rows; RGB32 alpha `0x80`, padding `0,0,0,0x80`; PSMCT16 `r5 | g5<<5 | b5<<10 | 0x8000` LE, `0x8000` outside the decoded `fw × fh`). The guest translate is the caller's |
| `engine/ipu/hle/fmv_pacer.h` / `.cpp` | 51 / 33 | `FmvPacer`: `SetPeriod(int64)`, `SetAuthoredRateTicks(double fps)` → `(int64)(60.0 / fps + 0.5)` min 1, `Wait Next(int64 now_tick)` = `TickPace::NextBurst` semantics as the SOLE core (first delivery arms `now + period`, wait 0; `wait < 0` → `late = true`, wait 0, grid unchanged; `next += period`; NO clamp), `Reset()`, `Armed()`, `Period()`, `NextTick()`, static `ShouldPace(frames_served) = frames_served >= 2` |
| `engine/ipu/hle/fmv_audio_capture.h` / `.cpp` | 55 / 111 | `FmvAudioCapture(FmvPcmSink&)`: `OnAudioPes(p, n)`, `Reset()`, `Disabled()`, `HeaderParsed()`, `BodyLeft()`, `StagedBytes()`; OLD `CaptureFmvAudio` state machine: `n < 4` / `p[0] != 0xFF` skip; `SShd` + len `[16, 0x100]` + `{format 1, rate 48000, channels 2, interleave 0x200}` + `SSbd` else `Disabled`; body bound; complete 1024-byte planar pairs → 256 interleaved frames → `Push(fr, 256)` |
| `engine/ipu/hle/README.md` | 45 | skeleton body replaced: landed rows (13) with OLD provenance, the 7D′/7E list, does-not-belong, rules |
| `engine/ipu/CMakeLists.txt` | 18 | `include(${CMAKE_SOURCE_DIR}/cmake/FindFFmpeg.cmake)` + the four hle `.cpp` in `ps2r_ipu` (no `FFmpeg::*` link yet — the decoder is 7D′) |
| `cmake/FindFFmpeg.cmake` | 41 | `find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h HINTS /opt/homebrew/include /usr/local/include)`, `find_library` × avcodec/avutil/swscale, IMPORTED `FFmpeg::avcodec/avutil/swscale` with `INTERFACE_INCLUDE_DIRECTORIES`, `message(FATAL_ERROR …)` when any is absent; idempotent (`if(TARGET …) return()`). Configure line: `-- nativeee: FFmpeg found (include=/opt/homebrew/include, avcodec=/opt/homebrew/lib/libavcodec.dylib)` |
| `tests/ipu/recording_pss_listener.h` / `.cpp` | 33 / 21 | `RecordingPssListener : PssDemuxListener` (+ `PssEvent` POD): ordered events, counts, joined ES |
| `tests/ipu/counting_fmv_pcm_sink.h` / `.cpp` | 21 / 12 | `CountingFmvPcmSink : FmvPcmSink`: samples, push/gate/flush counters |
| `tests/ipu/test_pss_demux.cpp` | 463 | ctest `PssDemux` (65 checks) |
| `tests/ipu/test_frame_writer.cpp` | 201 | ctest `FrameWriter` |
| `tests/ipu/test_fmv_pacer.cpp` | 244 | ctest `FmvPaceDeadline` (OLD's name kept) |
| `tests/ipu/test_fmv_audio_capture.cpp` | 231 | ctest `FmvAudioCapture` |
| `tests/ipu/pss_census.py` | 142 | title-agnostic copy of OLD `tools/pss_census.py` (header reworded, the `mpeg_hle.cpp` reference → `PssDemux::ParsePesPayloadOffset`; the walk is byte-identical) |
| `tests/ipu/fixtures/pss_census_counts.txt` | 14 | the two movies' 7 counters, generated by the script (command in the header) |
| `tests/ipu/CMakeLists.txt` | 36 | the four new executables/ctests; `PssDemux` gets `${PS2R_OG_ROOT}` + the fixture path as argv |

Total new this stage: ≈ 1,885 lines (engine 900 incl. README/CMake, cmake 41, tests 944). Shared files: NONE touched by 7D
(the root and tests blocks from 7C already cover `engine/ipu` / `tests/ipu`).

### OLD sources transplanted (read-only, `$PS2R_OLD_ROOT/`)

`host_app/mpeg_hle.h:56-105` (`PaceDeadline` — read; NOT written, wall domain), `:107-134` (`NextBurst` — the arithmetic
written in ticks), `:146-231` (`TickPace`: `NextBurst :211-231` → `FmvPacer::Next`; the debt-clamp `Next :171-191` NOT written;
`Reset/Armed :193-194`); `mpeg_hle.cpp:82-105` (stream ids), `:584` (`frames_served < 2` → `ShouldPace`), `:636-643` (rate
rounding), `:747-749` (read; `StreamEnded` is `MpegContext`'s), `:754-762` (`FindStartCode`), `:764-773`
(`ContainsSequenceEnd`), `:775-776` (ids), `:778-788` (length-prefixed set), `:790-792` (`ReadBe16`), `:797-823`
(`ParsePesPayloadOffset`), `:825-829` (`ErasePrefix`), `:1302-1312` (ADS rationale), `:1322-1428` (`CaptureFmvAudio`; `:1328`
warp skip, `:1397-1403` PERF probe, `g_logFile` lines NOT written), `:1463-1625` (`ProcessBuffer` walk; `:1431-1461` census,
`:1567-1569` lazy decoder, `:1577-1579` ring mirror, `:1584-1605` deferral, `:1610-1618` audio ctx guard → all `MpegContext`/
`MpegApi` in 7D′), `:1773-1822` (`WriteFrameToGuest`), `:1832-1886` (`WriteFrameToGuestRGB16`); `tests/test_fmv_pace.cpp`
(all 498: cases ported below); `tools/pss_census.py` (copied); `host_app/sound/sound_core.h:155-177` (sink shape);
`host_app/cdvd_stream.h:24-47` (pump shape); `host_app/sce_hle/libipu.cpp:139-172`, `:212-264` (the DMAC words the face
exposes); `host_app/CMakeLists.txt:16-56` (FFmpeg lookup pattern → `FindFFmpeg.cmake`).

### Flags applied

| Flag | Outcome |
|---|---|
| `PS2_MPEG_PACE_AUTHORED` | NORM — the tick-domain pacer is the only path; `FmvPacer` has no wall-clock twin (`PaceDeadline` not written) |
| `PS2_FMV_OPEN_BURST` | ledgered DELETE, default-ON arm written as the SOLE `FmvPacer::Next` (P7-LEDGER-BURST); no debt-clamp arm, no switch; `FmvPaceDeadline` pins "no floor" |
| `PS2_MPEG_PACE_OFF` | DELETE — no early return |
| `PS2_MPEG_SEQCENSUS` | DELETE — the stderr census (`mpeg_hle.cpp:1431-1461`) not written; `tests/ipu/pss_census.py` is the offline answer |
| `PS2_SND_FMV` | NORM — `FmvAudioCapture` is unconditional (no gate, no `warp::Active()` skip) |
| `PS2_MPEG_PDIFF`, `PS2_MPEG_WM`, `PS2_MPEG_DUMP_N` | their OLD sites are in the serve path (`MpegApi`, 7D′) — nothing written here either |
| `PS2_PERF` (outside the count) | the `[FMV-AUD]` stderr self-report `:1397-1403` not written |

### Tests added

| ctest | TU | Assertions |
|---|---|---|
| `PssDemux` | `test_pss_demux.cpp` | classifiers (id ranges, the 9-element prefixed set, `FindStartCode` incl. the 3-byte case, `ContainsSequenceEnd`, `ParsePesPayloadOffset` MPEG-2 `9 + hdl`, MPEG-1 stuffing/STD/PTS = 15, PTS+DTS = 16, `0x0F` = 7, `≤ 6` and clamp); synthetic stream — MPEG-2 pack (3 stuffing, 17 bytes) / system header / video PES with length / MPEG-1 pack (12) / `0xBD` audio PES with an MPEG-1 header / video PES `pkt_len == 0` to the next start code / padding stream `0xBE` / video PES carrying `B7` / `B9` / trailing audio: exactly 4 events with byte-exact payloads, `contains_sequence_end` on the last, `Ended()`, `Buffered() == 0`, `B9` behind the `B7` never reported, `PacksSeen == 2`, `SystemHeadersSeen == 2`, `Consumed()` = offset past the ending packet, later input discarded, `Reset` clears; program end path (+ `MarkEnded`); byte-at-a-time trickle == one-shot (3 identical events); straddling start-code prefix keeps 3 bytes then reassembles; final-chunk drop; short PES waits / truncated on final; 13-of-18-byte pack waits. **OLD-gated census** (argv `${PS2R_OG_ROOT}` + fixture): both movies walked in 2048-byte sectors; pass 1 stops at the sequence end, pass 2 walks the container tail from `Consumed()`; 7/7 counters equal the script on both |
| `FrameWriter` | `test_frame_writer.cpp` | `Rgb32Bytes(20,18) == 4096`, aligned sizes; 20×18 gradient → 32×32 in 2 strips: every in-picture pixel RGB-equal with alpha `0x80`, every padding pixel `0,0,0,0x80`, strip 1 starts at 2048 with pixel (16,0), row 17 vs row 18 of strip 0, x = 20 padding, guard bytes untouched; no-ops (empty / no pixels / null); RGB16 24×20 target from the 20×18 picture: `r5|g5<<5|b5<<10|0x8000` inside, `0x8000` outside the decoded extent incl. target columns 20..23, LE store, pixel (19,17) = `0xB6F0` computed independently, strip 1 at 1024 = `0x980E`; 16×16 target = one 512-byte strip sampled with the picture's 20-pixel stride; zero-target / empty no-ops |
| `FmvPaceDeadline` | `test_fmv_pacer.cpp` | ports (TP = 2, TK0 = 1000): unarmed (0 and −5), arm, steady drift-free ×1000, early jitter absorbed, small-late keeps grid, burst drains a 100-picture backlog in 101 instant serves (100 flagged late) then `next == now + 2·TP`, no-clamp floor (`next == TK0 + 2·TP` after a 1000-period stall), tick twin (50 instant, `next == now + 4`), one-per-tick (100 pictures / 100 ticks), reset re-arms; NEW `SetAuthoredRateTicks`: 29.97 → 2, 30 → 2, 23.976 → 3, 24 → 3, 15 → 4, 60 → 1, 120 → 1 (floor), `ShouldPace(0/1) = false, (2) = true`. The two `long_stall_clamps_debt` cases and `burst_steady_matches_next` are NOT ported (no clamp arm; one core) |
| `FmvAudioCapture` | `test_fmv_audio_capture.cpp` | header accept (`body_left`, nothing staged, no push); rejects: format 2, rate 44100, channels 1, interleave 0x400, missing `SShd`, `SShd` len 8, missing `SSbd` — each `Disabled()` and later body ignored; header split 20 + rest across two packets accepted once complete; `p[0] != 0xFF` skip without disabling, `n < 4` / prefix-only ignored, header still accepted afterwards; planar → interleaved: `fr[2i] = L[i]`, `fr[2i+1] = R[i]` for a 256-frame pair, a pair split 600 + 424 waits then pushes once, body bound truncates a 2-pair + 100-byte packet to the promised 1 pair, trailing padding after the body ignored, `Reset` returns to the pre-header state, the capture never calls `GateOpen`/`Flush` |

```
1/4 Test #96: FrameWriter ......................   Passed    0.01 sec
2/4 Test #97: FmvPaceDeadline ..................   Passed    0.01 sec
3/4 Test #98: FmvAudioCapture ..................   Passed    0.01 sec
4/4 Test #99: PssDemux .........................   Passed    1.80 sec
test_pss_demux: 65/65 checks passed
test_pss_demux: PASS (classifiers, synthetic stream, program end, chunking/tail, census)
test_frame_writer: PASS (sizes, rgb32 strips, rgb16 strips, clamp, no-ops)
test_fmv_pacer: PASS (unarmed, arm, steady, jitter, small-late, burst-drain, no-clamp, tick-twin, one-per-tick, reset, authored-rate)
test_fmv_audio_capture: PASS (header accept, rejects, prefix skip, planar->interleaved)
```

Census evidence (from the `PssDemux` run with `${PS2R_OG_ROOT}`):

```
census game_files/example_title.rep/FMV/VIVENDI.PSS: pass 1 stopped at 0x6c5edc (sequence_end=1): video_pes=1417 audio_pes=317 packs=434 sys=1 program_end=0 pictures=203
census game_files/example_title.rep/FMV/VIVENDI.PSS: tail from 0x6c5edc: audio_pes=3 packs=1 sys=3 program_end=1
census game_files/example_title.rep/FMV/VIVENDI.PSS: totals video_pes=1417 audio_pes=320 packs=435 sys=4 program_end=1 sequence_end=1 total_pictures=203 -> 7/7 MATCH
census game_files/example_title.rep/FMV/TTIDENT.PSS: pass 1 stopped at 0xf30746 (sequence_end=1): video_pes=3324 audio_pes=565 packs=973 sys=1 program_end=0 pictures=361
census game_files/example_title.rep/FMV/TTIDENT.PSS: tail from 0xf30746: audio_pes=2 packs=0 sys=3 program_end=1
census game_files/example_title.rep/FMV/TTIDENT.PSS: totals video_pes=3324 audio_pes=567 packs=973 sys=4 program_end=1 sequence_end=1 total_pictures=361 -> 7/7 MATCH
```

Fixture generation command (bounded; the script reads the movie only):
`verify/run_bounded.sh 120 /tmp/p7/7D/census_<F>.log -- python3 tests/ipu/pss_census.py "$PS2R_OG_ROOT/game_files/example_title.rep/FMV/<F>.PSS"`
for `<F>` in `VIVENDI TTIDENT` → the two "PS layer"/"ES" lines quoted in the fixture header. Run without argv (or with the
movies absent) the test prints `SKIP: census half …` and passes on the synthetic half (verified: 1 SKIP line).
(First run of `PssDemux` FAILED on a test-side error — `FindStartCode(buf, 2)` expected 5, the second start code is at index
6; the test was corrected, the demuxer was right.)

### Finish checks (quoted from `/tmp/p7/7D/finish.log`)

```
== warnings
0
== ctest
100% tests passed, 0 tests failed out of 99
== lint_one_class
LintOneClass: 368 files scanned, 0 findings -> PASS
== lint_engine_purity
LintEnginePurity: 314 files scanned, 0 game tokens -> PASS
== getenv
1/1 Test #19: CoreSingleGetenvSite .............   Passed    0.75 sec
grep -rn getenv engine/ipu | wc -l  ->  0
== git status   (owned paths only; no shared file touched by 7D)
 M engine/ipu/hle/README.md
?? cmake/FindFFmpeg.cmake
?? engine/ipu/CMakeLists.txt  engine/ipu/hle/{decoded_picture,dmac_channel_face,fmv_audio_capture(.h/.cpp),fmv_context_layout,
   fmv_pacer(.h/.cpp),fmv_pcm_sink,fmv_sector_source,frame_writer(.h/.cpp),guest_memory,pss_demux(.h/.cpp),pss_demux_listener,video_decoder}
?? tests/ipu/   (+ the 7A/7B/7C paths already present at start)
== includes (gate item (f) for engine/ipu)
engine/ipu/ipu_device.cpp:7:#include "engine/core/log/log.h"
engine/ipu/ipu_device.h:24:#include "engine/ee/mem/mmio_handler.h"
```

(The hle sources include only `engine/ipu/hle/*` headers and the standard library — no `engine/ee`, `engine/sound`,
`engine/cdvd`, `engine/dmac`, SDL or FFmpeg symbol in any of them.)

### §7.1 findings rows (stage 7D)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P7-7D-SPLIT | 7D | The orchestrator pre-applied the 7D′ split: this stage's scope was the "STOP with" list from the start (the FFmpeg decoder, `MpegContext`, `MpegApi`, the binding-table copy and the `MpegApi` smoke are 7D′) | Both halves of `PssDemux` (synthetic + OLD-gated census) landed in 7D, so 7D′'s remainder shrinks to the decoder chain + API + README table + smoke. `FindFFmpeg.cmake` is included (proves the find) but `ps2r_ipu` does not link `FFmpeg::*` until the decoder TU exists |
| P7-DEMUX-ENDED-OWNER | 7D | OLD's `ProcessBuffer` loop reads `state.stream_ended \|\| state.end_after_pending` (both `PlaybackState` fields) to stop the walk; with the demuxer split from the context the flag needs one owner | `PssDemux::ended_` — set by program end, by a video payload with `B7`, or by `MarkEnded()` (the CD-EOF path) — stops the walk exactly where OLD does in both of OLD's outcomes; WHICH of `stream_ended` / `end_after_pending` the context records (deferral-dependent, `mpeg_hle.cpp:1584-1605`) stays `MpegContext`'s decision on `contains_sequence_end`. Observable bytes unchanged |
| P7-DEMUX-CENSUS-TAIL | 7D | The demuxer stops at the video sequence end (OLD rule) while `pss_census.py` scans the whole file: after the `B7` packet both movies still hold 3 (VIVENDI) / 2 (TTIDENT) audio PES, 1 / 0 packs, 3 / 3 system-class headers and the `B9` — a direct single-pass comparison could never be 7/7 | `PssDemux::Consumed()` (bytes walked, excluding the ended-drop) lets the test walk the container tail in a second pass from the exact stop offset; totals match 7/7 on both movies. `Consumed`, `PacksSeen`, `SystemHeadersSeen` are observers with no effect on the walk |
| P7-DEMUX-B9-CONSUMED | 7D | Where `Consumed()` should point after `OnProgramEnd()` | The 4-byte end code is counted as walked (then the rest dropped uncounted), so `Consumed()` after `B9` = offset past the code — pinned by `TestProgramEnd` |
| P7-PACER-SHOULDPACE | 7D | Plan §2.2 lists `FmvActive()` on `FmvPacer` (the `DecodeActiveRecently` replacement) | Not written in 7D: the signal is a property of the context's recent decode activity, not of the grid; it lands with `MpegContext`/`MpegApi` in 7D′ (`ShouldPace(frames_served)` IS written as the plan's caller rule) |
| P7-RGB16-CLAMP-MEANING | 7D | The task says RGB16 "sampling clamped to decoded size"; OLD (`mpeg_hle.cpp:1869-1877`) does NOT edge-clamp — pixels outside `fw × fh` are `0x8000` | Written as OLD does (outside → `0x8000`); pinned by `TestRgb16Strips` (target columns 20..23 are `0x8000`, not the last picture column) |

### HANDOFF (7D → 7D′)

**Done:** `engine/ipu/hle/{guest_memory.h, fmv_pcm_sink.h, fmv_sector_source.h, dmac_channel_face.h, decoded_picture.h,
video_decoder.h, fmv_context_layout.h, pss_demux_listener.h, pss_demux.h/.cpp, frame_writer.h/.cpp, fmv_pacer.h/.cpp,
fmv_audio_capture.h/.cpp, README.md}`; `engine/ipu/CMakeLists.txt` (FindFFmpeg include + 4 hle TUs); `cmake/FindFFmpeg.cmake`;
`tests/ipu/{CMakeLists.txt, test_pss_demux.cpp, test_frame_writer.cpp, test_fmv_pacer.cpp, test_fmv_audio_capture.cpp,
recording_pss_listener.h/.cpp, counting_fmv_pcm_sink.h/.cpp, pss_census.py, fixtures/pss_census_counts.txt}`; this record.
OLD transplanted: `mpeg_hle.h:107-134`/`:211-231` (burst core), `mpeg_hle.cpp:82-105`, `:584`, `:636-643`, `:754-829`,
`:1322-1428`, `:1463-1625` (walk), `:1773-1886`; `tests/test_fmv_pace.cpp` (burst + tick cases); `tools/pss_census.py`.
Flags: `PS2_MPEG_PACE_AUTHORED` NORM, `PS2_SND_FMV` NORM, `PS2_FMV_OPEN_BURST` → sole core (P7-LEDGER-BURST), `PS2_MPEG_PACE_OFF`
/ `PS2_MPEG_SEQCENSUS` not written. Tests: `PssDemux` (synthetic + census 7/7 × 2), `FrameWriter`, `FmvPaceDeadline`,
`FmvAudioCapture` → `100% tests passed, 0 tests failed out of 99`. OLD bounded runs: `pss_census.py` × 2 through
`verify/run_bounded.sh` (reads the two `.PSS` under OLD; no OLD binary, no side effect). Ledger/doc deltas: §7.1 rows above;
`engine/ipu/hle/README.md` rewritten.  
**Not done (moved to 7D′):** `ffmpeg_video_decoder.h/.cpp` (`FfmpegVideoDecoder : VideoDecoder`, exactly OLD `mpeg_hle.cpp:125-380`,
`thread_count = 1`, `static LibavcodecVersion()`; `ps2r_ipu` then links `FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale`),
`mpeg_context.h/.cpp` (`MpegContext` = OLD `PlaybackState` over `PssDemux` + `FmvPacer` + `FmvAudioCapture` + `IpuSettings`
cap/deferral/`DrainPendingEs`, `StreamEnded()`; it decides `stream_ended` vs `end_after_pending` from `contains_sequence_end`
and calls `demux.MarkEnded()` on CD EOF), `mpeg_api.h/.cpp` (`MpegApi` over `GuestMemory` + `FmvContextLayout`: the ring
mirror, inner status, completion gate, `PumpUntilFrame` over `FmvSectorSource`, serve path → `FrameWriter`, `GateOpen` on the
first serve, `Flush` on Init/Destroy/Reset, `wait_ticks` returned; `FmvActive()`), the §8.1 binding-table copy into
`engine/ipu/hle/README.md` marked "integration — NOT installed by Phase 7", and the `MpegApi` smoke (fake `GuestMemory` over a
1 MiB vector + fake `FmvSectorSource` over a `.PSS` + `CountingFmvPcmSink`, OLD-gated, prints `avcodec_version()`).
`libipu_api` stays 7E.  
**Module gate status:** (a) – (7D′/7E; the 7D share — `PssDemux` census 7/7 on both movies, `FrameWriter`, `FmvPaceDeadline`,
`FmvAudioCapture` green — is done) · (b) ✔ (7A) · (c) ✔ (7B) · (d) ✔ — 0 warnings, 99/99, both lints PASS, `getenv sites: 1`,
`SettingsTableReproducible` / `FlagPhaseCountsReproducible` untouched and green · (e) ✔ — `git status` = owned paths only (7D
touched no shared file) · (f) ✔ for `engine/ipu` — includes quoted above; the hle TUs include only `engine/ipu/hle/*` · (g)
partial — this section; INTEGRATION CHECKLIST and numeric summary are 7E's.  
**Integration checklist:** §8.2 items touched (documented only): the sound module implements `FmvPcmSink` (item 2); the CD
stream reader implements `FmvSectorSource` (item 3); the DMAC implements `DmacChannelFace` for channels 3/4 (item 6); the title
fills `FmvContextLayout` (offsets from `mpeg_hle.cpp:873-877`, `:1013`, `:1175-1177`, `libmpeg.cpp:706-714`) — no value lives
in `engine/`; the composition root adapts EE RAM to `GuestMemory`.  
**Next command:** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect `100% tests passed … out of 99`),
then `ctest --preset macos-dev -R PssDemux -V | grep -c "7/7 MATCH"` → `2`; 7D′ starts with `engine/ipu/hle/ffmpeg_video_decoder.{h,cpp}`
from `OLD/host_app/mpeg_hle.cpp:125-380`, adds `target_link_libraries(ps2r_ipu PUBLIC FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale)`,
and expects the smoke to print `avcodec_version=<62.x.y>` for `VIVENDI.PSS` with `decoded >= 203` pictures (the census's
`total_pictures`).

## Stage 7D′ — ipu/hle, second half of the pre-registered split (2026-09-27)

Scope row: PHASE7_PLAN §7 "7D ipu/hle" remainder as handed off by 7D: `ffmpeg_video_decoder`, `mpeg_context`, `mpeg_api`, the
§8.1 binding-table copy into `engine/ipu/hle/README.md`, the `MpegApi` smoke, plus the two MINOR items of
`/tmp/p7/review/7D_findings.md` (fixed first). Module rows §2.2; design §3.3; flags §5 (§5.5 cap = `IpuSettings::decode_ahead_pictures`);
facts INV §A1 (b, e, f, g), A2, A3, A5. `pwd && git branch --show-current` → the worktree path and `phase7` before the first edit.
`/tmp/p7/review/7D2_findings.md` did not exist at start; loop iteration 1 (review_7d2, NEEDS_CHANGES) delivered six findings, fixed below.

### 7D review items fixed first

| Finding | Edit |
|---|---|
| 7D MINOR 1 (`fmv_context_layout.h` fields) | added `int32_t play_to_eof_total` (the VALUE `HoldFmvCompletionGate` writes/compares at `total`), `uint32_t presented` (the word whose bit 0 selects RAW8 double buffering), `uint32_t mode_raw8` (the VALUE of the mode word meaning the RAW8 movie-player path — so the gate hold names no value in `engine/`); fixed the `plane_select` comment to "alternate-plane flag word"; dropped `play_state` (INFO 4: no OLD reader). Still no numeric default anywhere. README row updated |
| 7D MINOR 2 (`FmvPacer::SetAuthoredRateTicks` UB on `fps <= 0`) | `if (!(fps > 0.0)) fps = kFallbackFps;` (`kFallbackFps = 29.97`, the NTSC fallback OLD latches at `mpeg_hle.cpp:586-590`) at the top of `SetAuthoredRateTicks`; the SAME latch sits where OLD has it, in the serve path (`MpegApi::PaceDelivery`: `fps = decoder ? AuthoredFps() : 0; if (!(fps > 0)) fps = 29.97; pace_rate_fps = fps`). `FmvPaceDeadline` gained `SetAuthoredRateTicks(0.0) → Period() == 2`, `(-3.0) → 2`, `kFallbackFps == 29.97` |
| 7D INFO 3 (unquoted `${PS2R_OG_ROOT}` argv) | the new `MpegApi` ctest quotes it (`"${PS2R_OG_ROOT}"`); `PssDemux`'s line left as 7D wrote it (cosmetic) |

### 7D′ review items fixed (loop iteration 1, `/tmp/p7/review/7D2_findings.md`)

| Finding | Edit | Proof |
|---|---|---|
| 1 MAJOR — `MpegContext::ResetPlayback` re-armed the tick grid (`pacer = FmvPacer{}`) whereas OLD `Reset` (`mpeg_hle.cpp:1908-1942`) resets only the WALL `pace`/`pace_rate_fps` and never touches `tick_pace` (refs `:417`, `:634-637` latch keyed on `ticks_per_pic <= 0`, `:648-649`; `TickPace::Reset` has no caller) | the reassignment (and the `pace_rate_fps = 0.0`) dropped; comment states the rule (grid + period survive sceMpegReset on the same context, only a new context arms fresh; the period is not re-derived because `PaceDelivery` latches only while `Period() <= 0` — OLD `:634`). §7.1 row P7-RESET-KEEPS-GRID | OLD-gated check: after `Reset`, the source is rewound and the second movie demuxed/served on the SAME ctx — priming serves 0/1 instant, serve 2 at `hold + 100` is `late` (a fresh grid would arm), serve 3 at the same tick `late` again (a fresh grid would book `wait 2`). Negative run with the deleted line restored: `80/82` with exactly those two FAIL lines; fix restored: `82/82` |
| 2 MINOR — `GetPicture`/`GetPictureRaw8` wrote wherever `Translate(0, …)` succeeded; OLD `WriteFrameToGuest*` return on `dest_addr == 0`, `width/height <= 0`, `target_w/h == 0` (`:1774`, `:1836`) | guards `image_addr != 0 && f.width > 0 && f.height > 0` / `plane_addr != 0 && target_w != 0 && target_h != 0` around the write; the serve still counts (as OLD) | OLD-gated: `GetPicture(ctx, 0, tick)` → `served`, sentinel word at the image untouched; the next serve to the real address overwrites it |
| 3 MINOR — plan §2.2 `mpeg_api` row's `AlalcInit/AlalcAlloc/ClearRefBuff` had no owner | written: `MpegApi::AlalcInit(alalc, base, size)`, `uint32_t AlalcAlloc(alalc, size, align)`, `ClearRefBuff(mpeg)` over NEW layout fields `alalc_base/capacity/watermark/base_copy`, `ref_slot[kRefSlots = 6]`, `ref_slot_status` (offsets as parameters, no defaults — the SDK struct values live in the test and the title); OLD `libmpeg.cpp:189-206`, `:209-249`, `:403-424` minus `log_call`/`g_logFile`. README rows updated (no longer "NOT yet written") | host-only: 20 checks — init words, first alloc at base, aligned-up second alloc, watermark advance, OUT_OF_MEMORY → 0 leaves the watermark, `align 0` → 0, null allocator → 0, exact fit to `base + capacity`; clear zeroes two allocated slots' status, skips null slots (address 0 + status untouched), leaves neighbour words and the slot pointers |
| 4 INFO — `FramesDecoded()` comment | "since construction (the count survives Shutdown)" | — |
| 5 INFO — P7-GATEOPEN-ONCE reach | sentence added to the row (a sink `Flush` outside `MpegApi` must be followed by a `Reset`) | the second-movie check sees `gate_opens == 2` (once per playback) |
| 6 INFO — `EndFromSource`'s `MarkEnded()` | no change (equivalent, as the reviewer notes) | — |

### Files landed

| Path | Lines | What |
|---|---|---|
| `engine/ipu/hle/ffmpeg_video_decoder.h` / `.cpp` | 68 / 235 | `FfmpegVideoDecoder : VideoDecoder` — EXACTLY OLD's libav sequence: `avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO)`, `av_parser_init`, `avcodec_alloc_context3`, `av_frame_alloc`, `av_packet_alloc`, `thread_count = 1`, `avcodec_open2`; `Feed` = `av_parser_parse2(…, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0)` loop (`used < 0` → stop; `used == 0 && pkt_size == 0` → break) → `SendPacket` (`av_packet_unref`, `av_new_packet`, memcpy, `avcodec_send_packet`; `EAGAIN` → `ReceiveFrames` then retry once; `ret < 0 && != EAGAIN` → stop) → `ReceiveFrames` (`avcodec_receive_frame` until EAGAIN/EOF) → `ConvertFrame` (`sws_getContext(w,h,fmt → w,h,AV_PIX_FMT_RGBA, SWS_BILINEAR)` re-created on any dims/fmt change, `sws_scale(…, 0, h, dst, {w*4,0,0,0})`, `rows <= 0` → fail; authored fps latched once from `codec_ctx->framerate`); `Flush` = NULL parser packet (send if it yields a packet) → `avcodec_send_packet(nullptr)` (EOF tolerated) → drain → `drained_`; `Shutdown` frees sws/frame/packet/ctx/parser, resets dims/fmt/fps/flags (`frames_decoded_` kept, as OLD); `static std::string LibraryVersion()` = `AV_VERSION_MAJOR.MINOR.MICRO(avcodec_version())`, `static unsigned LibavcodecVersion()`. FFmpeg headers in the `.cpp` only (`extern "C"`); the header forward-declares `AVCodecParserContext/AVCodecContext/AVFrame/AVPacket/SwsContext` and keeps the pixel format as `int` (a C enum cannot be forward-declared) |
| `engine/ipu/hle/mpeg_context.h` / `.cpp` | 112 / 128 | `MpegContext : PssDemuxListener` = OLD `PlaybackState` + the context-side halves of `ProcessBuffer`: fields `width/height` (`kDefaultWidth/Height` 512/256), `frames_served`, `saw_input`, `stream_ended`, `end_after_pending`, `ctx_addr`, `deque<DecodedPicture> frames`, `unique_ptr<VideoDecoder> decoder` (lazy via the factory), `pending_es` + `pending_es_off`, `FmvPacer pacer`, `pace_rate_fps`, `FmvAudioCapture audio`, `gate_opened`, `PssDemux demux` (owned; constructed with `*this`), hooks `on_video_payload` (ring mirror) and `audio_allowed` (ring-owning rule). `OnVideoEs`: `saw_input`, `EnsureDecoder`, mirror hook, `defer = frames.size() >= cap \|\| PendingBacklog()` → `pending_es` (+ `end_after_pending` on the sequence end) else `stream_ended` on the sequence end + `Feed`; `OnAudioPes` → capture when allowed; `OnProgramEnd` → `end_after_pending` or `stream_ended` + flush; `Demux(data, size, final)`; `DrainPendingEs` (`kDrainSliceBytes` 64 KiB, promotion + flush when drained, `kCompactBytes` 4 MiB compaction); `EndFromSource` (`demux.MarkEnded()` + end now / after backlog); `FlushIfStreamEnded`; `StreamEnded()`; `ResetPlayback` (OLD Reset body: pictures, demux, capture, backlog; decoder kept but `Shutdown`; the pacer's grid and period are KEPT — P7-RESET-KEEPS-GRID) |
| `engine/ipu/hle/mpeg_api.h` / `.cpp` | 134 / 458 | `MpegApi(GuestMemory&, const FmvContextLayout&, FmvSectorSource&, FmvPcmSink&, DecoderFactory, const IpuSettings&)`; `Init` / `Destroy` / `Reset` / `NotifyCdStreamEof` / `DemuxPss` / `DemuxPssRing` / `PictureResult {served, wait_ticks, late} GetPicture(ctx, image_addr, now_tick)` / `GetPictureRaw8(ctx, plane_addr, w, h, now_tick)` / `IsEnd` / `ShouldHoldAbort` / `Width` / `Height` / `FmvActive()`; private `RingFillVideoPayload` (base/size/write over the layout offsets, wrap, cursor store), `RingDrainToWrite`, `WriteInnerStatus` (inner ptr 0 → nothing; write only on change), `HoldFmvCompletionGate` (saw_input, not ended, ring-owning, `mode == layout.mode_raw8`, `cur < play_to_eof_total` → write), `OwnsRing`, `PumpUntilFrame` (drain first; `stream_ended` → false; `!OwnsRing` → false; `sectors_per_pump` × 2048 scratch member; loop bounded by `max_sector_budget`; `got == 0` → `EndFromSource` (+ `DrainPendingEs` + continue while a backlog remains)), `PrepareServe` (drain → flush-once at end → pump), `PaceDelivery` (`ShouldPace(frames_served)`; rate latch with the 29.97 fallback → `SetAuthoredRateTicks`; `pacer.Next(now_tick)` RETURNED), `FinishServe` (`RingDrainToWrite`, `frames_served++`, first `GateOpen` when `audio.Pushed()`, `WriteInnerStatus(StreamEnded)`), `CopyFromGuest` / `CopyFromGuestRing`; the write is skipped for a null image/plane pointer or a zero geometry (the serve still counts); public guest-word helpers `AlalcInit` / `AlalcAlloc` / `ClearRefBuff` (no context state, no mutex, layout offsets). Table `unordered_map<uint32_t, unique_ptr<MpegContext>>` under one `std::mutex`. NO clock, NO sleep, NO TickSync include, NO diagnostics/stderr/ppm/probes |
| `engine/ipu/hle/fmv_context_layout.h` | 80 (was 51) | +`play_to_eof_total`, +`presented`, +`mode_raw8`, −`play_state`, comment fixes (above); +`alalc_base/capacity/watermark/base_copy`, +`ref_slot[kRefSlots = 6]`, +`ref_slot_status` (the helpers' offsets) |
| `engine/ipu/hle/fmv_pacer.h` / `.cpp` | 55 / 34 | +`kFallbackFps`, the `fps <= 0` guard |
| `engine/ipu/hle/fmv_audio_capture.h` / `.cpp` | 59 / 113 | +`Pushed()` latch (set on the first `Push`, cleared by `Reset`) — the A/V gate rule (`if (aud_pushed) GateOpen`) needs it |
| `engine/ipu/hle/README.md` | 72 | rows for the three new classes, the layout/pacer/capture rows updated, the §8.1 binding table copied and marked "integration — NOT installed by Phase 7" (with the `now_tick`/`PictureResult` return mapping per row; the alalc/clear-ref rows now name the landed helpers and their register mapping), "Lands in 7E" list |
| `engine/ipu/CMakeLists.txt` | 23 | + the three hle TUs; `target_link_libraries(ps2r_ipu PUBLIC ps2r_core PRIVATE FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale)` |
| `tests/ipu/vector_guest_memory.h` / `.cpp` | 27 / 23 | `VectorGuestMemory : GuestMemory` over a zeroed vector (1 MiB default; size is a ctor parameter — the 640×480 image plane needs 4 MiB); out-of-range → nullptr / 0 / no-op |
| `tests/ipu/file_fmv_sector_source.h` / `.cpp` | 30 / 32 | `FileFmvSectorSource : FmvSectorSource`: sequential 2048-byte sectors from a file, short tail zero-padded, `SectorsRead()`, `PumpCalls()` |
| `tests/ipu/test_mpeg_api.cpp` | 401 | ctest `MpegApi` (below); its single struct-with-methods is `TestMpegApi` (the independent first-picture oracle: own `PssDemux` + `FfmpegVideoDecoder`) |
| `tests/ipu/test_fmv_pacer.cpp` | 249 | + the 3 fallback checks |
| `tests/ipu/CMakeLists.txt` | 47 | + `test_mpeg_api` (links the three support TUs) / ctest `MpegApi` with `"${PS2R_OG_ROOT}"`, TIMEOUT 300 |

Total new this stage: ≈ 1,580 lines (engine 1,134 incl. README/CMake, tests 445 + edits). Shared files: NONE touched by 7D′.

### OLD sources transplanted (read-only, `$PS2R_OLD_ROOT/host_app/`)

`mpeg_hle.cpp:104-105` (default dims), `:125-380` (`FfmpegDecoder` → `FfmpegVideoDecoder`; the `[MPEG-DECODE]` count lines
and `g_logFile` error lines → core-log `IPU Warn` on libav failures only), `:387-428` (`PlaybackState` → `MpegContext`),
`:432-437` (table → `contexts_`), `:584`, `:586-590`, `:636-643` (pace rule + rate latch + fallback → `PaceDelivery`; the
`PS2_PERF`/`g_logFile` lines, `g_pace_rate_milli`, `PaceCensusBook`, the wall-clock branch `:705-731` and the tick WAIT
`:655-701` NOT written — the wait is the caller's), `:747-749` (`StreamEnded`), `:831-835` (`FlushIfStreamEnded`), `:881-970`
(`RingFillVideoPayload`; the `[MPEG-RING]` lines and the anchor dims log NOT written), `:972-979` (`RingDrainToWrite`),
`:1013-1035` (`WriteInnerStatus`; `[MPEG-INNER]` NOT written), `:1175-1200` (`HoldFmvCompletionGate`; `[MPEG-FMVGATE]` NOT
written; `kPlayToEofTotal` / `kModeOff == 2` → `layout.play_to_eof_total` / `layout.mode_raw8`), `:1228-1245` (`FmvDecodeCap`
→ `settings.decode_ahead_pictures`; the `cap == 0` arm NOT written), `:1252-1300` (`DrainPendingEs`), `:1488-1496` (program-end
context half), `:1562-1618` (video branch: lazy decoder, mirror, defer/decode; audio branch: ring-owning rule → `audio_allowed`;
`FmvAudioOn()`/`sound::Enabled()` NOT written — NORM), `:1664-1720` (`PumpUntilFrame`; `[MPEG-PUMP]` NOT written; the
function-static scratch → the `pump_buf_` member under the same mutex), `:1729-1766` (guest copies), `:1890-1906`
(`Init`/`Destroy`), `:1908-1942` (`Reset` → `ResetPlayback` + the audio-carried flush rule), `:1944-1968` (`NotifyCdStreamEof`),
`:1970-2032` (`DemuxPss`/`DemuxPssRing`; `PERF_SCOPE`, `IoScope`, `LogFmvGateState` NOT written), `:2043-2424` (`GetPicture`:
kept `:2064-2080` drain/flush/pump, `:2195-2199` pace call, `:2223-2224` pop, `:2310-2313` dims + write, `:2372-2384` drain/served/
gate/inner, `:2386-2393` EOF return, `:2421-2423` no-frame return; NOT written: `:2044-2055` tick pump + `g_last_decode_ms`,
`:2057-2058` perf scopes, `:2083-2146` PDIFF/GCTX/log_ret, `:2202-2222` legacy present-coupled wait, `:2225-2299` WM + DUMP_N,
`:2301-2308` ringlog gauge, `:2314-2371` IMG/SERVE fingerprints + `mpegback_006.ppm`), `:2426-2529` (`GetPictureRAW8`: same
keep/drop; `[R196-SERVE-RAW8]` and `[MPEG-RAW8]` NOT written), `:2531-2557` (`IsEnd`, `ShouldHoldAbort`, `Width`, `Height`);
`sce_hle/libmpeg.cpp:189-206`, `:209-249`, `:403-424` (`_sceMpegAlalcInit` / `_sceMpegAlalcAlloc` / `sceMpegClearRefBuff` bodies →
`MpegApi::AlalcInit/AlalcAlloc/ClearRefBuff`; `log_call` and the OUT_OF_MEMORY `g_logFile` line NOT written; the word offsets
`+0/+4/+8/+C`, the six slot offsets and `+0x28` → `FmvContextLayout` parameters), `:594-628`, `:700-716` (API shape → the
binding-table notes; the plane select stays the binding's); `CMakeLists.txt:16-56` (component set → the `FFmpeg::*` link).

### Flags applied

| Flag | Outcome |
|---|---|
| `PS2_MPEG_PACE_AUTHORED` | NORM — `MpegApi::GetPicture*` always books the tick-domain `FmvPacer`; the legacy present-coupled wait (`:2202-2222`) and the wall-clock branch are not written |
| `PS2_MPEG_PACE_OFF` | DELETE — no early return in `PaceDelivery` |
| `PS2_FMV_DECODE_CAP` | constant `IpuSettings::decode_ahead_pictures = 64` (P7-LEDGER-CAP); deferral + `DrainPendingEs` written; `cap == 0` eager arm not written |
| `PS2_FMV_OPEN_BURST` | the sole `FmvPacer::Next` core (7D); `PaceDelivery` has no switch |
| `PS2_MPEG_PDIFF`, `PS2_MPEG_WM` | DELETE — sites not written |
| `PS2_MPEG_DUMP_N` | DIAG — site not written (the P6 layout is 7E's oracle tool) |
| `PS2_MPEG_SEQCENSUS` | DELETE (7D) |
| `PS2_SND_FMV` | NORM — `OnAudioPes` → capture unconditional for the ring-owning ctx (no `FmvAudioOn()`, no `sound::Enabled()`, no `warp::Active()`) |
| `PS2_PERF`, `PS2_EE_TIM0` (outside the count) | perf scopes / stderr not written; the `eetimers` pump is the binding's (§8.1 row) |

### Tests added

| ctest | TU | Assertions |
|---|---|---|
| `MpegApi` | `test_mpeg_api.cpp` | host-only (33 checks): `Init` flushes once; `Reset`/`Destroy` of an audio-less ctx do not flush; unknown-ctx `Destroy` no-op; `FmvActive` false; `IsEnd`/`ShouldHoldAbort` false; `Width/Height` 512/256; `NotifyCdStreamEof` on nothing; `GetPicture`/`GetPictureRaw8` on an unknown ctx → `served == false`, `wait_ticks == 0`, `!late`, the whole vector memory byte-identical before/after, `PumpCalls() == 0` (no ring → no pump); alalc/clear-ref helpers (20 checks, listed in the review-fix table above). **OLD-gated** (49 more; `argv[1] = ${PS2R_OG_ROOT}`, `VIVENDI.PSS` via `FileFmvSectorSource`, 4 MiB `VectorGuestMemory`, ctx words: ring `0x40000`/`0x40000`, write = anchor = base + 0x40, read = base, inner ptr, `mode = mode_raw8`): first 16 sectors through `DemuxPss` (consumed 32768; `FmvActive`; completion gate held at `play_to_eof_total`; inner status cleared) then `GetPicture` with `now_tick` += 2 until 5 served — exactly 5 calls (the pump blocks until a picture exists); `Width/Height` 640×480 (INV A5); `PumpCalls() > 0`; `ShouldHoldAbort && !IsEnd`; ring write cursor advanced, inside the ring, read drained to write; `00 00 01 B3` at the anchor with header dims 640×480; inner status 0; first served strip bytes == `FrameWriter::WriteRgb32Strips` of the first picture decoded independently (`TestMpegApi` oracle: own demux + decoder); waits: serves 0–2 instant (`ShouldPace` twice, then arming), serves 3–4 `wait 0, !late` on the 2-tick grid; at the last serve's tick held: `wait 2` then `wait 4`; `+100` ticks → instant + `late`; PCM `push_calls > 0`, `frames % 256 == 0`, `gate_opens == 1`, `flushes == 1`; `Reset` → `flushes == 2`, `!FmvActive`, `!ShouldHoldAbort`; **second movie on the same ctx** (source rewound, first window through `DemuxPss`): priming serves instant, serve 2 at `hold + 100` `late` (carried grid, not an arming serve), serve 3 at the same tick `late` (a fresh grid would book `wait 2`), `GetPicture(ctx, 0, …)` served without touching the image sentinel, the next serve overwrites it, 640×480, `gate_opens == 2` (once per playback), `FmvActive`; `Reset` → `flushes == 3`; `Destroy` after `Reset` → no extra flush. Prints `avcodec_version=62.28.101 (0x003e1c65)`. Without argv: `SKIP: OLD-gated half` + 33/33 |
| `FmvPaceDeadline` | `test_fmv_pacer.cpp` | + `SetAuthoredRateTicks(0.0) → 2`, `(-3.0) → 2`, `kFallbackFps == 29.97` |

```
100: avcodec_version=62.28.101 (0x003e1c65)
100: mpeg_api: VIVENDI.PSS served=5 (+3) sectors=80 pcm_frames=19968
100: test_mpeg_api: 82/82 checks passed
100: test_mpeg_api: PASS (lifecycle, unknown ctx, alalc/clear-ref, OLD movie: dims, ring mirror, strips, pacer, audio, reset keeps grid)
1/1 Test #100: MpegApi ..........................   Passed    0.46 sec
$ ./build/tests/ipu/test_mpeg_api            (no argv)
SKIP: OLD-gated half (no OLD root argument)
test_mpeg_api: 33/33 checks passed
test_mpeg_api: PASS (lifecycle, unknown ctx, alalc/clear-ref)
```

(`sectors=80` / `pcm_frames=19968` are the SECOND-movie totals: `FileFmvSectorSource::Open` rewinds its counters and the
sink keeps counting across the flush. Negative proof of the grid check — `pacer = FmvPacer{};` temporarily restored in
`ResetPlayback`: `FAIL: second movie: serve 2 late on the carried grid` + `FAIL: second movie: serve 3 still catching up`,
`80/82`; reverted → `82/82`.)

(First run: 42/45 — two test-side errors: the completion gate is held by the DEMUX entry points only (OLD `:1993`, `:2027`),
not by `GetPicture`, so the test now feeds the first window through `DemuxPss` as the game's load loop does; and the "held
tick" probe used the tick AFTER the last serve — corrected to the last serve's tick. The API was right both times.)

### Finish checks (quoted from `/tmp/p7/7D2/finish2.log`, after the review fixes; the warning count is after `touch engine/ipu/hle/*.cpp tests/ipu/*.cpp`)

```
== warnings (forced recompile of engine/ipu + tests/ipu)
0
== ctest
100% tests passed, 0 tests failed out of 100
== lint_one_class
LintOneClass: 379 files scanned, 0 findings -> PASS
== lint_engine_purity
LintEnginePurity: 320 files scanned, 0 game tokens -> PASS
== getenv
1/1 Test #19: CoreSingleGetenvSite .............   Passed    0.78 sec
grep -rn getenv engine/ipu tests/ipu | wc -l -> 1      (engine/ipu/README.md:24, the "does not belong" bullet; 0 in code)
== clocks/sleeps/TickSync in engine/ipu code (chrono|steady_clock|sleep|condition_variable|TickSync|this_thread, non-comment)
0
== ffmpeg symbols in headers other than the decoder pair
0
== MpegApi verbose
100: avcodec_version=62.28.101 (0x003e1c65)
100: mpeg_api: VIVENDI.PSS served=5 (+3) sectors=80 pcm_frames=19968
100: test_mpeg_api: 82/82 checks passed
== host-only
test_mpeg_api: 33/33 checks passed
== git status   (owned paths only; the M entries on CMakeLists.txt / engine/platform/CMakeLists.txt / tests/CMakeLists.txt are the 7A–7C `# Phase 7` blocks, 53 insertions, untouched by 7D′)
?? engine/ipu/hle/{ffmpeg_video_decoder,mpeg_context,mpeg_api}.{h,cpp}   tests/ipu/{test_mpeg_api.cpp, vector_guest_memory, file_fmv_sector_source}
 M (within untracked) engine/ipu/hle/{fmv_context_layout.h, fmv_pacer.*, fmv_audio_capture.*, README.md}, engine/ipu/CMakeLists.txt, tests/ipu/{CMakeLists.txt, test_fmv_pacer.cpp}
```

Includes of the new engine TUs (gate item (f)): `mpeg_api.cpp`/`mpeg_context.cpp` include only `engine/ipu/hle/*` +
`engine/ipu/ipu_settings.h` + the standard library; `ffmpeg_video_decoder.cpp` adds `libavcodec/avcodec.h`,
`libavutil/{error,frame,imgutils,pixfmt}.h`, `libswscale/swscale.h` (extern "C") and `engine/core/log/log.h`.

### §7.1 findings rows (stage 7D′)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P7-LAYOUT-FIELDS | 7D′ | 7D's `FmvContextLayout` lacked `play_to_eof_total` and the presented-count word, and the RAW8-mode VALUE (`2` at `mpeg_hle.cpp:1176`) would otherwise have to be named in `engine/` | `play_to_eof_total`, `presented`, `mode_raw8` added as parameters (values the TITLE fills, like the offsets); `play_state` dropped (no reader). The layout carries offsets AND the two guest word values — the header says so |
| P7-SERVE-PARK-ORDER | 7D′ | OLD waits on the tick grid BEFORE popping/writing the picture (`:2195` then `:2223`, `:2313`); `MpegApi::GetPicture` writes the picture and RETURNS `wait_ticks` for the caller to park on. While the caller parks, ticks fire (mem lane boundaries among them): with OLD the image buffer still holds the previous picture at those boundaries, with the returned-wait shape it already holds the new one | Recorded for the integration owner (§8.2 item 7): if the `mem` lane on an FMV leg shows the image plane a picture ahead, the binding must obtain the booking first and write after the park — `MpegApi` can be split into `Prepare(ctx, now_tick) → wait` + `Serve(ctx, image_addr)` without touching any other body (the pacer booking already precedes the pop inside `GetPicture`). Not changed here: the stage row fixes the `GetPicture(ctx, image_addr, now_tick) → PictureResult` shape |
| P7-RESET-KEEPS-GRID | 7D′ | OLD `Reset` (`mpeg_hle.cpp:1908-1942`) resets the wall `pace` and `pace_rate_fps` (`:1931-1932`) but never `tick_pace`: its period is latched once (`:634-637`, keyed on `ticks_per_pic <= 0`) and its `next_tick` is carried across sceMpegReset on the same context (`TickPace::Reset` has no caller). The coldboot leg plays two movies on one context (Init once, Resets #2..#4), so the second movie's paced serves catch up with the first movie's grid (late/instant) rather than arming anew | `MpegContext::ResetPlayback` keeps `pacer` (grid AND period); `PaceDelivery` does not re-latch (keyed on `Period() <= 0`, OLD `:634`), so `pace_rate_fps` is kept too (OLD zeroes and re-latches it through the wall-pacer branch NEW does not write; the word has no reader in NEW — unobservable). Rule stated in the comment; the OLD-gated `MpegApi` check serves a second movie on the same ctx and asserts the carried grid (negative run 80/82 with a re-arm). Only a NEW context (or `Init`, which drops the table) starts a fresh grid |
| P7-GATEOPEN-ONCE | 7D′ | OLD calls `sound::FmvGateOpen()` on EVERY serve once audio was pushed (`:2380`, `:2517`); the sink is idempotent. `FmvPcmSink::GateOpen`'s contract says "the first video picture" and the stage row says "GateOpen on the first served frame" | `MpegContext::gate_opened` latch: `GateOpen()` once per playback, at the first serve after audio was pushed (reset by `ResetPlayback`). Observable sink state identical to OLD's idempotent flag; the counting sink sees 1 per playback (2 across the test's two movies). Reach note for the integration owner: OLD's `sound::FmvFlush` closes the gate and the every-serve call reopened it; with the latch, a sink `Flush` issued OUTSIDE `MpegApi` (OLD's only such site is the savestate path, which runs while no FMV is active) must be followed by a `Reset` of the playing context before the gate reopens |
| P7-CTX-PSS-BUFFER | 7D′ | The stage row lists `pss_buffer` among `MpegContext`'s fields; the 7D `PssDemux` already owns the pending program-stream buffer (`buffer_`, `Buffered()`) | No second copy: `MpegContext::Demux(data, size, final)` = `demux.Append` + `demux.Process`; `PssDemux::Buffered()` is `pss_buffer.size()`. `pump_buf_` (the pump scratch) is a `MpegApi` member instead of OLD's function-static |
| P7-CAPTURE-PUSHED | 7D′ | The gate rule needs OLD's `aud_pushed`; 7D's `FmvAudioCapture` exposed `HeaderParsed/Disabled/BodyLeft/StagedBytes` only | `FmvAudioCapture::Pushed()` added (set at the first pair push, cleared by `Reset`); `MpegContext::AudioCarried() = Pushed() \|\| HeaderParsed()` is OLD's `aud_pushed \|\| aud_hdr_parsed` flush rule |
| P7-DECODER-LOG | 7D′ | OLD logs libav failures to `g_logFile`; the stage row says no diagnostics/stderr | libav FAILURES (decoder missing, open/parse/send/receive/sws errors) go to `PS2R_LOG(IPU, Warn, …)` — the engine logger `ipu_device.cpp` already uses, not a probe; the per-frame `[MPEG-DECODE]` count lines are not written |
| P7-VECTOR-MEM-SIZE | 7D′ | The stage row says a 1 MiB vector memory; a 640×480 RGB32 plane is 1,228,800 bytes | `VectorGuestMemory(size)` defaults to 1 MiB (the host-only half uses it); the OLD-gated half constructs 4 MiB |
| P7-UNKNOWN-CTX-STATE | 7D′ | OLD `GetPicture` on an address without state CREATES the state (`GetState`) and writes the inner word only if the ctx's inner pointer is non-zero | Same: `State(mpeg_addr)` creates; `WriteInnerStatus` reads the inner pointer (0 in the test's zeroed memory → no write) and `PumpUntilFrame` is guarded by the ring base → the test's "memory untouched" holds by OLD's own rule, not by an early return |

### HANDOFF (7D′ → 7E)

**Done:** `engine/ipu/hle/{ffmpeg_video_decoder, mpeg_context, mpeg_api}.{h,cpp}`; `fmv_context_layout.h` (+3/−1 fields),
`fmv_pacer.{h,cpp}` (fallback guard), `fmv_audio_capture.{h,cpp}` (`Pushed()`), `README.md` (rows + §8.1 table marked NOT
installed; alalc/clear-ref rows name the landed helpers); `engine/ipu/CMakeLists.txt` (`FFmpeg::avcodec/avutil/swscale` PRIVATE); `tests/ipu/{test_mpeg_api.cpp,
vector_guest_memory.{h,cpp}, file_fmv_sector_source.{h,cpp}, CMakeLists.txt, test_fmv_pacer.cpp}`; this record. OLD transplanted:
`mpeg_hle.cpp:125-380`, `:387-437`, `:584-590`, `:636-643`, `:747-749`, `:831-835`, `:881-979`, `:1013-1035`, `:1175-1200`,
`:1228-1300`, `:1488-1496`, `:1562-1618`, `:1664-1766`, `:1890-2557` (minus every INV A3 diagnostics block); `libmpeg.cpp:189-249`,
`:403-424` (the three guest-word helpers), `:594-628`, `:700-716` (shape). Flags: `PS2_MPEG_PACE_AUTHORED` NORM, `PS2_SND_FMV` NORM, `PS2_FMV_DECODE_CAP` → `IpuSettings` constant,
`PS2_MPEG_PACE_OFF`/`PDIFF`/`WM` not written, `PS2_MPEG_DUMP_N` site not written. Tests: `MpegApi` (33 host + 49 OLD-gated = 82/82),
`FmvPaceDeadline` (+3) → `100% tests passed, 0 tests failed out of 100`. OLD bounded runs: none (the test READS
`OLD/game_files/…/FMV/VIVENDI.PSS`; no OLD binary; no side effect). Ledger/doc deltas: the nine §7.1 rows above (P7-RESET-KEEPS-GRID added by the review loop); README rewritten. Review loop 1: all six
`/tmp/p7/review/7D2_findings.md` items dispositioned (table above).  
**Not done (moved to 7E, as planned):** `libipu_api.{h,cpp}` + `LibipuApi` test; the per-picture golden (`/tmp/p7/old_fmv_dump.py`,
fixture `tests/ipu/fixtures/coldboot_pictures.sha256`, `fmv_picture_oracle.cpp` + ctest `FmvPictureOracle`); the INTEGRATION
CHECKLIST + numeric summary + module-gate table. (`MpegApi::AlalcInit/AlalcAlloc/ClearRefBuff` ARE written — review finding 3;
the bindings' register mapping is in the README table.)  
**Module gate status:** (a) – (7E; the 7D/7D′ share — `PssDemux` census 7/7 × 2, `FrameWriter`, `FmvPaceDeadline`,
`FmvAudioCapture`, `MpegApi` 82/82 with 640×480 and byte-equal strips on VIVENDI.PSS — is done) · (b) ✔ (7A) · (c) ✔ (7B) ·
(d) ✔ — 0 warnings (forced recompile), 100/100, both lints PASS, `CoreSingleGetenvSite` Passed, `SettingsTableReproducible` /
`FlagPhaseCountsReproducible` untouched and green · (e) ✔ — `git status` = owned paths only (7D′ touched no shared file) ·
(f) ✔ — includes quoted above; FFmpeg symbols in `ffmpeg_video_decoder.{h,cpp}` only (`grep` → 0 other headers); `ps2r_ipu`
links `FFmpeg::*` PRIVATE so no consumer sees libav · (g) partial — this section; INTEGRATION CHECKLIST and numeric summary are 7E's.  
**Integration checklist:** §8.2 items touched (documented only): (2) the sound module implements `FmvPcmSink` — `GateOpen` is
issued ONCE per playback at the first serve after audio was pushed, `Flush` on Init/Destroy(carried)/Reset(carried); (3) the
CD stream reader implements `FmvSectorSource` (shared cursor); (5) the title fills `FmvContextLayout` incl. the two VALUES
`play_to_eof_total` and `mode_raw8`, the four alalc word offsets and the six reference-slot offsets + `ref_slot_status`; (7) the bindings map `PictureResult` → `v0` (`served ? 0 : -1` / `served ? mbcount : -1`),
pass `now_tick` from the vblank counter and park `wait_ticks` — see P7-SERVE-PARK-ORDER for the park-before-write option;
(7b) `sceMpegReset` → `MpegApi::Reset` keeps the tick grid (P7-RESET-KEEPS-GRID) — do NOT re-arm the pacer around it, and
issue any sound-side `Flush` outside `MpegApi` only together with a `Reset` (P7-GATEOPEN-ONCE); (8) `MpegApi::FmvActive()` is the signal; (15) `FfmpegVideoDecoder::LibraryVersion()` = `62.28.101` on this host for the meta gate.  
**Next command:** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` (expect `100% tests passed … out of 100`),
then `ctest --preset macos-dev -R MpegApi -V | grep -E "avcodec_version|checks passed"` → `avcodec_version=62.28.101 (0x003e1c65)`
and `82/82 checks passed`; 7E starts with `/tmp/p7/old_fmv_dump.py` (§4.3 step 0: `ls OLD/logs/mpegpic_*.ppm | wc -l` → 0) and
expects the oracle over `VIVENDI.PSS` to decode `>= 203` pictures (the census's `total_pictures`) with the golden a PREFIX.

## Stage 7E — per-picture oracle + LibipuApi + INTEGRATION CHECKLIST (2026-09-28)

Scope row: PHASE7_PLAN §7 "7E per-picture oracle + checklist"; module row §2.2 `libipu_api`; tests §2.5 (`test_libipu_api`,
`fmv_picture_oracle`); the golden procedure §4.3 steps 0–3 (helpers copied verbatim from the plan); gate §6.1; checklist §8.2;
facts INV §A4, A5, D5, D6. `pwd && git branch --show-current` → the worktree path and `phase7` before the first edit.
`/tmp/p7/review/7E_findings.md` did not exist at start. No split was pre-registered; none was needed (finish checks at ≈ 25 min).

### Files landed

| Path | Lines | What |
|---|---|---|
| `engine/ipu/hle/libipu_api.h` / `.cpp` | 74 / 82 | `LibipuApi(IpuDevice&, DmacChannelFace&)` + `LibipuDmaSnapshot` POD (nine `uint32_t` in the SDK save-buffer order: ch4 MADR/TADR/QWC/CHCR, ch3 MADR/QWC/CHCR, IPU_BP, IPU_CTRL; `static_assert`-free, `sizeof == 36` pinned by the test). `Init()` = `WriteCtrl(kCtrlRst)`, `WriteCmd(0)` (BCLR), `WriteCtrl(kCtrlRst)`, `WriteCmd(0)` — the OLD four-write sequence; `// diverges:` the quantiser-table seeding is not performed (as OLD). `Sync(mode)` = mode 0 polls `ReadCtrl() & kCtrlBusy` bounded by `kMaxBusyPolls = 1024` → 0; mode 1 → `ReadCtrl() >> 31`; else 0. `StopDma(out)` = STR clear on ch3 then ch4 (read-modify-write through the face), ch4 words, one `ReadCtrl()` (the drain), ch3 words, `ReadBp()`, `ReadCtrl()`. `RestartDma(snap)` = ch3 restore with `\| kChcrStr` when `madr && qwc`, `ReadCtrl()`, `WriteCmd(ctrl & 0x7F)`, `ReadCtrl()`, ch4 restore rewound by `RewindQuadwords(snap.ipu_ctrl)` (= bits 17:16 + bits 11:8) when the rewound `madr && qwc`. No guest access (the nine-word copy to/from the save buffer is the binding's), no game address |
| `engine/ipu/CMakeLists.txt` | 24 | + `hle/libipu_api.cpp` |
| `engine/ipu/hle/README.md` | 74 | `LibipuApi` row in the landed table; "Lands in 7E" → "(landed)" with the fixture pointer; title line "stages 7D, 7D′, 7E" |
| `engine/ipu/README.md` | ±1 | `hle/` row names the three stages + `LibipuApi` |
| `tests/ipu/recording_dmac_channel_face.h` / `.cpp` | 46 / 51 | `RecordingDmacChannelFace : DmacChannelFace` (+ `DmacChannelRegs` POD): a 10-channel register file with an ordered access log (`R3.CHCR`, `W4.MADR=0x…`) — test support pair (LintOneClass scans `tests/`) |
| `tests/ipu/test_libipu_api.cpp` | 220 | ctest `LibipuApi` (52 checks, below) |
| `tests/ipu/fmv_picture_oracle.cpp` | 434 | tool + ctest `FmvPictureOracle`: `--avcodec-version` (prints `FfmpegVideoDecoder::LibraryVersion()`), `--og-root <dir>`, `--movie <rel>` ×N (relative to `<og-root>/game_files/example_title.rep/`), `--manifest <file>`, `--dump-dir <dir>`; per movie `FileFmvSectorSource` → 16-sector pumps → `PssDemux` → the ONE struct-with-methods `FmvPictureOracle : PssDemuxListener` feeding every video ES payload to a `FfmpegVideoDecoder` in demux order (the `MpegContext` rule), `Flush` at the sequence end / file end; per picture the P6 bytes `P6\n%d %d\n255\n` + RGB triplets (alpha dropped, row-major — OLD `mpeg_hle.cpp:2293-2296`) → SHA-256 (FIPS 180-4 written locally, test-side) and FNV-1a64 of the RGBA bytes; compare mode = the WINDOW assertion of P7-GOLDEN-WINDOW (below) + the `avcodec_version` header check (`META: avcodec <x> != fixture <y>` → FAIL) + contiguous indices + `served_total == rows` |
| `tests/ipu/fixtures/coldboot_pictures.sha256` | 549 (2 header + 547 rows) | the OLD golden manifest (step 3) |
| `tests/ipu/CMakeLists.txt` | 72 | + `test_libipu_api` (links the recording face TU) / ctest `LibipuApi`; + `fmv_picture_oracle` (links `file_fmv_sector_source.cpp`) / ctest `FmvPictureOracle` registered with `--og-root "${PS2R_OG_ROOT}" --movie FMV/VIVENDI.PSS --movie FMV/TTIDENT.PSS --manifest <fixture>` when `PS2R_OG_ROOT/game_files` and the fixture exist, else as `cmake -E echo "SKIP: FmvPictureOracle (…absent)"`; TIMEOUT 600 |

Total new this stage: ≈ 1,456 lines (engine 156 + README rows; tests 751 + fixture 549). Shared files: NONE touched by 7E
(the three `# Phase 7` blocks are unchanged: `git -P diff --stat CMakeLists.txt engine/platform/CMakeLists.txt tests/CMakeLists.txt`
→ `3 files changed, 53 insertions(+)`, as after 7C).

### OLD sources transplanted (read-only, `$PS2R_OLD_ROOT/`)

`host_app/sce_hle/libipu.cpp:67-77` (`sceIpuInit`: the four writes → `LibipuApi::Init`; `log_call` NOT written), `:88-105`
(`sceIpuSync`: OLD returns 0 without polling because BUSY never lingers; NEW polls bounded and returns the same 0 — the SDK's
mode-1 branch is written too), `:139-172` (`sceIpuStopDMA`: the `save_buf == 0` guard and `memory::write` stores → the caller's;
the register order kept), `:212-264` (`sceIpuRestartDMA`: the `memory::read` loads → the caller's; the rewind formula and the two
`madr && qwc` guards kept; see P7-LIBIPU-REWIND-WORD). `host_app/mpeg_hle.cpp:2282-2299` (the `PS2_MPEG_DUMP_N` P6 layout →
`PpmBytes` in the oracle; the emitter itself is the OLD side of the golden and has NO NEW env site), `:2223-2225` (pop) and `:2313`
(`WriteFrameToGuest` — the dump precedes it, so the golden is the decoded picture as served). Read for the procedure only:
`host_app/verify/run_meta.cpp:33-49` (what `env_hash` folds), `verify/run_leg.py:84-146`, `verify/profile_deterministic.py:93-104`,
`verify/diff_legs.py:92-112`, `OLD/.gitignore:8`, `docs/REFERENCE_FREEZE.md` "Accepted side effects"; `OLD/logs/runtime_log.txt:48242-48381`,
`:64547-64591` (the wall-paced coldboot log: the Create/Reset/DemuxPss/GetPicture order that explains the golden windows).

### Flags applied

| Flag | Outcome |
|---|---|
| `PS2_MPEG_DUMP_N` | DIAG — the OLD emitter was ARMED ONCE (value `1000`) on the OLD golden leg through `run_leg.py`'s own env builder (helper `/tmp/p7/old_fmv_dump.py`, `verify/` untouched) to MAKE the golden; the NEW tree has no site (the P6 layout lives in the test tool). Twelve-row count unchanged |

### The OLD golden run — §4.3 steps 0–3, exactly as quoted in the plan

Step 0 (pre-state), `OLD=$PS2R_OLD_ROOT`:

```
$ ls "$OLD"/logs/mpegpic_*.ppm 2>/dev/null | wc -l
       0
$ date +%s > /tmp/p7/run_start_epoch; cat /tmp/p7/run_start_epoch
1790567682
```

(`OLD/logs/` held 6,545 other files; no `mpegpic_*.ppm` existed before the run.)

Step 1 — the helper `/tmp/p7/old_fmv_dump.py` (verbatim from the plan; run from the worktree root, bounded by `run_leg.py`'s own
`verify/run_bounded.sh` with `timeout = max(600, 300 + 1500·2) = 3300 s` and the 300 s progress watchdog):

```python
# Arms PS2_MPEG_DUMP_N on an OLD leg through run_leg's own env builder (monkey-patch; verify/ untouched).
import sys; sys.path.insert(0, "verify")
import profile_deterministic as pd, run_leg
_orig = pd.build_leg_env
def _patched(base_env, out_dir, tree, extra=None):
    extra = dict(extra or {}); extra["PS2_MPEG_DUMP_N"] = "1000"
    return _orig(base_env, out_dir, tree, extra)
pd.build_leg_env = _patched; run_leg.build_leg_env = _patched
sys.argv = ["run_leg.py", "--binary", "$PS2R_OLD_ROOT/build/host_app/host_app",
            "--tree", "legacy", "--anchor", "coldboot", "--frames", "1500", "--out", "/tmp/p7/old_cb"]
sys.exit(run_leg.main())
```

```
$ /usr/bin/time -p python3 /tmp/p7/old_fmv_dump.py          (/tmp/p7/7E/step1.log)
run_leg: OK /tmp/p7/old_cb
real 55.44
user 22.99
sys 3.94
exit=0
```

(OLD binary `build/host_app/host_app`, 189,503,840 B, mtime 2026-09-25 21:26 — the frozen one; `run.meta`:
`build_id=HOSTAPP-BUILD-ID 2026-09-25 21:25:53`, `gl_renderer=Apple M4`, `tree=legacy`, `profile=deterministic`,
`env_hash=0x4d49c11c76a4d7b8`, `frames=1494`, `wall_ms=52949`, `finalized=1`. The leg's `bench_report.json` landed in
`/tmp/p7/old_cb/`; the frozen tree's tracked `OLD/bench_report.json` kept its mtime `2026-09-26 00:22:57` < the run start.)

Step 2 — the dump did not perturb the run (`/tmp/p7/7E/step2.log`, exit 0):

```
$ python3 verify/diff_legs.py verify/baseline/coldboot /tmp/p7/old_cb --cross-engine --lanes frames,audio,mem
NOTE (cross-engine): meta env_hash differs: '0x9a0f815a02d994d4' vs '0x4d49c11c76a4d7b8'
meta: gate PASS (profile=deterministic, both finalized)
frames.fhash: identical (1494 lines)
audio.ahash: identical (162 lines)
mem.mhash: identical (17 lines)
diff_legs: IDENTICAL (lanes=frames,audio,mem)
```

Exactly ONE `NOTE` line, and it is the `env_hash` one; no `build_id` / `gl_renderer` / `tree` NOTE (the same binary, GPU and tree
as the frozen baseline). `--cross-engine` is the meta policy that tolerates the env delta only — both legs are OLD binaries, no
engine claim is made.

Step 2b — the env delta is exactly the dump variable; the helper `/tmp/p7/check_env_delta.py` (verbatim from the plan):

```python
# Asserts: frames == 1494 on both; sorted env_ps2 delta (ignoring PS2_VERIFY_* / PS2_PAD_SCRIPT_FILE, which run_meta.cpp:33-49
# also excludes from env_hash) is exactly {+PS2_MPEG_DUMP_N=1000}. Exit 0 = OK, 1 = mismatch (printed).
import sys
def meta(p):
    d = {}
    for ln in open(p):
        k, _, v = ln.rstrip("\n").partition("=")
        d[k] = v
    return d
def env(d):
    out = set()
    for kv in d.get("env_ps2", "").split(";"):
        if not kv: continue
        k = kv.split("=", 1)[0]
        if k.startswith("PS2_VERIFY_") or k == "PS2_PAD_SCRIPT_FILE": continue
        out.add(kv)
    return out
a, b = meta(sys.argv[1] + "/run.meta"), meta(sys.argv[2] + "/run.meta")
ok = a.get("frames") == "1494" == b.get("frames")
added, removed = sorted(env(b) - env(a)), sorted(env(a) - env(b))
ok &= (added == ["PS2_MPEG_DUMP_N=1000"] and removed == [])
print("frames baseline=%s leg=%s  +%s  -%s  -> %s" % (a.get("frames"), b.get("frames"), added, removed, "OK" if ok else "MISMATCH"))
sys.exit(0 if ok else 1)
```

```
$ python3 /tmp/p7/check_env_delta.py verify/baseline/coldboot /tmp/p7/old_cb
frames baseline=1494 leg=1494  +['PS2_MPEG_DUMP_N=1000']  -[]  -> OK
exit=0
```

Step 3 — collect (mtime-filtered) and manifest; the helper `/tmp/p7/make_manifest.py` (verbatim from the plan):

```
$ START=$(cat /tmp/p7/run_start_epoch)
$ for f in "$OLD"/logs/mpegpic_*.ppm; do [ "$(stat -f %m "$f")" -ge "$START" ] && cp "$f" /tmp/p7/golden/; done
$ ls /tmp/p7/golden | wc -l
     547                                  # mpegpic_000.ppm (P6 640 480) … mpegpic_546.ppm (P6 512 448)
```

```python
# make_manifest.py <golden_dir> <manifest_out> <build_id> <avcodec_version>
# served_total := the number of PPMs in <golden_dir>; ASSERTS their indices are contiguous 000..N-1 (no gaps, no stale extras).
# Header: "# source=OLD coldboot leg PS2_MPEG_DUMP_N=1000 build_id=<b>" / "# avcodec_version=<v>  served_total=<n>";
# rows:   "<index>\t<sha256 of the PPM bytes>\t<w>\t<h>"  (P6 header parsed for w/h; index from mpegpic_NNN.ppm).
import hashlib, os, re, sys
gd, out, build_id, avv = sys.argv[1:5]
files = sorted(f for f in os.listdir(gd) if re.fullmatch(r"mpegpic_\d+\.ppm", f))
idx = [int(re.search(r"(\d+)", f).group(1)) for f in files]
assert idx == list(range(len(files))), "non-contiguous indices: %r" % idx
rows = []
for i, f in zip(idx, files):
    b = open(os.path.join(gd, f), "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", b)
    assert m, "not a P6 PPM: " + f
    rows.append("%d\t%s\t%s\t%s" % (i, hashlib.sha256(b).hexdigest(), m.group(1).decode(), m.group(2).decode()))
with open(out, "w") as o:
    o.write("# source=OLD coldboot leg PS2_MPEG_DUMP_N=1000 build_id=%s\n" % build_id)
    o.write("# avcodec_version=%s  served_total=%d\n" % (avv, len(rows)))
    o.write("\n".join(rows) + "\n")
print("served_total=%d -> %s" % (len(rows), out))
```

```
$ ./build/tests/ipu/fmv_picture_oracle --avcodec-version
62.28.101
$ python3 /tmp/p7/make_manifest.py /tmp/p7/golden tests/ipu/fixtures/coldboot_pictures.sha256 \
      "$(grep '^build_id=' /tmp/p7/old_cb/run.meta | cut -d= -f2-)" "$(./build/tests/ipu/fmv_picture_oracle --avcodec-version)"
served_total=547 -> tests/ipu/fixtures/coldboot_pictures.sha256
$ head -3 tests/ipu/fixtures/coldboot_pictures.sha256
# source=OLD coldboot leg PS2_MPEG_DUMP_N=1000 build_id=HOSTAPP-BUILD-ID 2026-09-25 21:25:53
# avcodec_version=62.28.101  served_total=547
0	84b2b4960f2e88743e913278a521d62e02427cbe7a4810918e0f016126697012	640	480
$ awk -F'\t' 'NR>2{print $3"x"$4}' tests/ipu/fixtures/coldboot_pictures.sha256 | uniq -c
 201 640x480          # movie 1 (VIVENDI.PSS; census total_pictures = 203)
 346 512x448          # movie 2 (TTIDENT.PSS; census total_pictures = 361)
```

Side effects of the OLD run (accepted, REFERENCE_FREEZE "logs/*", gitignored `OLD/.gitignore:8`): `OLD/logs/mpegpic_000..546.ppm`
(547 files) and `OLD/logs/mpegback_006.ppm` (the ungated serve-#6 read-back OLD writes on every run, INV A4). Nothing else under
OLD was written; the OLD tree was never built, edited or git-touched.

Step 4 — the NEW side (`/tmp/p7/7E/oracle2.log`, through `verify/run_bounded.sh 600`, ≈ 20 s wall):

```
$ ./build/tests/ipu/fmv_picture_oracle --og-root "$OLD" --movie FMV/VIVENDI.PSS --movie FMV/TTIDENT.PSS \
      --manifest tests/ipu/fixtures/coldboot_pictures.sha256
avcodec_version=62.28.101
movie1 FMV/VIVENDI.PSS decoded=203 first=640x480
movie2 FMV/TTIDENT.PSS decoded=361 first=512x448
movie1 decoded=203 served=201 (decoded 2..202); movie2 decoded=361 served=346 (decoded 15..360); WINDOW OK
WINDOW OK k1=2 s1=201 k2=15 s2=346 served_total=547 decoded1=203 decoded2=361
fmv_picture_oracle: PASS
```

`s1 + s2 = 201 + 346 = 547 = served_total`; every one of the 547 golden PPM hashes equals the NEW decode of the same picture
(byte-identical P6 files — also confirmed file-by-file with `cmp` on the `--dump-dir` output: 201/201 for movie 1 at offset 2,
346/346 for movie 2 at offset 15). The plan's PREFIX form of the assertion was WRONG for this title — see P7-GOLDEN-WINDOW: the
served sequence is a contiguous window that ends at each movie's LAST decoded picture and starts at decoded index 2 / 15, not 0.
Served counts vs INV A5's wall-paced log (136 + 281): the deterministic leg serves 201 + 346. The A5 log was a wall-paced,
`PS2_LOG=1` run (no `FMV tick pace armed` line), in which the game's own end-of-movie condition fired while fewer pictures had
been served; under the deterministic profile the game asks for every decoded picture from the post-Reset restart point to the
sequence end. The golden's counts are the profile's (INV OPEN Q3 closed: `s1 = 201`, `s2 = 346`).

### Tests added

| ctest | TU | Assertions |
|---|---|---|
| `LibipuApi` | `test_libipu_api.cpp` | **Init** over the REAL `IpuDevice` staged busy (BCLR bp 5, 512 input bytes, one CSC macroblock → 1024 output bytes, counts recomputed to IFC 8 / OFC 8): after `Init` the output FIFO is empty (only RST clears it — BCLR cannot), input empty, `bp == 0`, not busy; the FIRST CTRL read reports IFC 0 (a BCLR ran after the staged recompute) and OFC still 8 (Init made no CTRL read: writes only — P7-LIBIPU-INIT-ORDER), the second read recomputes OFC to 0; `ReadBp() == 0`; no DMAC access; idempotent; `DivergentCommandCount() == 0` · **Sync** mode 0/1/7 → 0 on a fresh unit and after commands; the unit's FIFOs/bp untouched; no DMAC access · **StopDma** on ch3 `{CHCR 0x101, MADR 0x200000, QWC 0x40}` / ch4 `{CHCR 0x100, MADR 0x300100, TADR 0x300000, QWC 0x20}` with the unit at bp 5 + 128 input bytes: STR cleared on both, the nine words as expected (`ipu_bp == 5 \| 8 << 8`, CTRL IFC 8, not busy), access log EXACTLY `R3.CHCR W3.CHCR=0x00000001 R4.CHCR W4.CHCR=0x00000000 R4.MADR R4.TADR R4.QWC R4.CHCR R3.MADR R3.QWC R3.CHCR` · **RestartDma** after scrambling the registers: ch3 restored with STR, ch4 restored (delta 0 from the CTRL word), TADR restored, log `W3.MADR W3.QWC W3.CHCR=0x101 W4.MADR W4.TADR W4.QWC W4.CHCR=0x100`; the re-issued `CTRL & 0x7F` = 8 lands on the unit as BCLR bp 8 with the input FIFO cleared; a snapshot with word[8] = `0x00020300` rewinds ch4 by 5 quadwords (MADR − 80, QWC + 5) and re-issues BCLR bp 0; an idle snapshot (MADR/QWC 0) writes no channel; `RewindQuadwords(0x00020300) == 5`; `kWords == 9`, `sizeof == 36` |
| `FmvPictureOracle` | `fmv_picture_oracle.cpp` | the step-4 run above through ctest (OLD-gated; TIMEOUT 600) |

```
test_libipu_api: 52/52 checks passed
test_libipu_api: PASS (init RST/BCLR effects, sync, stop/restart snapshot round trip)
```

(First run: 42/48 — the test's own expectations were wrong twice: the staged `bp` is `5 + 384·8` after the CSC macroblock
advances the cursor, and the rewind derived from the saved CTRL word is 0 (P7-LIBIPU-REWIND-WORD); the transplant was right. The
nonzero-rewind case is exercised with a hand-built word.)

### Finish checks (quoted from `/tmp/p7/7E/finish.log`; the warning count is after `touch` of the four 7E TUs)

```
== warnings (forced recompile of engine/ipu/hle/libipu_api.cpp + tests/ipu 7E TUs)
0
== ctest (from /tmp/p7/7E/ctest.log, full run)
100% tests passed, 0 tests failed out of 102
101/102 Test #101: LibipuApi ...........................   Passed    0.02 sec
102/102 Test #102: FmvPictureOracle ....................   Passed   20.68 sec
== lint_one_class
LintOneClass: 385 files scanned, 0 findings -> PASS
== lint_engine_purity
LintEnginePurity: 322 files scanned, 0 game tokens -> PASS
== getenv
1/1 Test #19: CoreSingleGetenvSite .............   Passed    0.76 sec
grep -rn getenv engine/ipu tests/ipu (code) ->        0
== git status   (owned paths + the three shared `# Phase 7` blocks only; nothing under engine/core, ee, iop, sound, pad, gs, app, games, verify, docs/filemap, README)
 M CMakeLists.txt   M engine/ipu/README.md   M engine/ipu/hle/README.md   M engine/platform/CMakeLists.txt   M engine/platform/sdl/README.md   M tests/CMakeLists.txt
?? cmake/FindFFmpeg.cmake  docs/phases/PHASE7_IMPL.md  docs/phases/PHASE7_PLAN.md  engine/ipu/**  engine/platform/sdl/**  tests/input/  tests/ipu/  tests/platform_sdl/
== shared blocks
 3 files changed, 53 insertions(+)
== includes (gate f)
engine/ipu/hle/ffmpeg_video_decoder.cpp:19:#include "engine/core/log/log.h"
engine/ipu/ipu_device.cpp:7:#include "engine/core/log/log.h"
engine/ipu/ipu_device.h:24:#include "engine/ee/mem/mmio_handler.h"
engine/platform/sdl/sdl_audio_sink.h:14:#include "engine/platform/platform.h"
engine/platform/sdl/input/pad_sample.h:9:#include "engine/platform/platform.h"
== ffmpeg/SDL symbols in engine headers other than the decoder / sink / SDL-backend pairs
(three input headers name SDL only in comments: input_backend.h:5,43, input_mapper.h:5-6,35-36, input_settings.h:14 — no SDL type or call)
== oracle ctest verbose
102: movie1 decoded=203 served=201 (decoded 2..202); movie2 decoded=361 served=346 (decoded 15..360); WINDOW OK
102: WINDOW OK k1=2 s1=201 k2=15 s2=346 served_total=547 decoded1=203 decoded2=361
102: fmv_picture_oracle: PASS
1/1 Test #102: FmvPictureOracle .................   Passed   19.15 sec
```

### §7.1 findings rows (stage 7E)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P7-GOLDEN-WINDOW | 7E | The plan's §4.3 rule "the golden sequence MUST be a PREFIX of movie-1's decoded pictures followed by a PREFIX of movie-2's" is false for this title: golden[0] equals VIVENDI's decoded picture **2** (not 0) and golden[201] equals TTIDENT's decoded picture **15**; from there every golden picture equals the next decoded one up to and including each movie's LAST picture (201/201 and 346/346 byte-identical PPMs). The OLD wall-paced log (`runtime_log.txt:48286-48338`, `:64547-64566`) shows why: the game calls `sceMpegDemuxPss` with its first 16 KB window, THEN `sceMpegCreate`/`sceMpegReset` (#2 for movie 1; #3/#4 for movie 2, after more windows) — OLD's `Reset` drops the decoded pictures and the pending ES and `Shutdown`s the decoder, while the CD stream cursor stays advanced; the pump resumes at the next sector and the decoder restarts at the next GOP. The first 2 / 15 decoded pictures are therefore never served. NEW's `MpegContext::ResetPlayback` does the same (7D′), so the integration reproduces the windows without any special case | The oracle asserts the WINDOW property instead: `k1 = index of golden[0] in movie 1`, `s1 = run length`, `k1 + s1 == decoded1` (the window reaches the last picture); then `k2/s2` against movie 2 with `k2 + s2 == decoded2`; `s1 + s2 == served_total`; the line printed is `WINDOW OK k1=2 s1=201 k2=15 s2=346 served_total=547 decoded1=203 decoded2=361` (a PREFIX golden would print `k1=0 k2=0 … (PREFIX)`). Every hash is compared — this is different from and more specific than the plan's rule (it also pins the decoder's restart point). The fixture was written because steps 0–2b were exact and the mismatch is a plan-wording error, not a picture difference (no picture differs). Plan §4.3 / §6.1 (a) wording is frozen; this row is the correction |
| P7-LIBIPU-REWIND-WORD | 7E | OLD `sceIpuRestartDMA` (`libipu.cpp:212-264`) documents the SDK as `ctrl = save_buf[7]` (the IPU_BP word: bit position bits 17:16, IFC bits 11:8) but READS `save_buf + 0x20` = word **[8]** (the IPU_CTRL word, whose bits 11:8 are CBP and 17:16 IDP). With CBP = IDP = 0 on this title's path the rewind is 0 either way | Transplanted as OLD behaves (word [8], `RewindQuadwords(snap.ipu_ctrl)`); the test pins delta 0 from a real CTRL snapshot and delta 5 from a hand-built word. The SDK-faithful variant (word [7]) is a one-token change if a later oracle shows a non-zero rewind mattering; no observable difference on the coldboot leg (the DMAC channels never carry IPU work — OLD's own comment) |
| P7-LIBIPU-INIT-ORDER | 7E | The task asks the test to observe "RST, BCLR, RST, BCLR effects in order" over the REAL `IpuDevice`; the four writes are idempotent clears, so the end state cannot distinguish 4 writes from 2, nor RST-then-BCLR from BCLR-then-RST | What IS observable through the device's CTRL-count rule (P7-IPU-CTRL-READ-ORDER) is pinned: after `Init` the output FIFO is empty (only RST clears it), the FIRST CTRL read reports IFC 0 (a BCLR ran after the last recompute) and OFC still at the staged 8 (no CTRL read happened inside `Init` — it is writes only), bp 0, no DMAC access, no divergent command. The four-write body is 4 lines and is reviewed against `libipu.cpp:74-77` by inspection |
| P7-SYNC-POLL | 7E | OLD `sceIpuSync` returns 0 without reading CTRL (`libipu.cpp:102-105`); the stage row asks for a bounded BUSY poll | `Sync(0)` polls `ReadCtrl() & kCtrlBusy` at most `kMaxBusyPolls` times and returns 0 (identical return; the CTRL reads recompute IFC/OFC, which OLD's `sceIpuStopDMA` does anyway on the same path). Mode 1 returns the BUSY bit as the SDK does (always 0 here) |
| P7-MPEGBACK-SIDE-EFFECT | 7E | Besides the 547 armed dumps, the OLD run wrote `OLD/logs/mpegback_006.ppm` (the ungated serve-#6 guest read-back, INV A4) | Accepted OLD side effect (`logs/*`, REFERENCE_FREEZE); not a golden input; recorded so the file count under `OLD/logs/` (6,545 → 7,093) is explained |

### HANDOFF (7E → integration / orchestrator)

**Done:** `engine/ipu/hle/{libipu_api.h, libipu_api.cpp}`; `engine/ipu/CMakeLists.txt` (+ the TU); `engine/ipu/hle/README.md` +
`engine/ipu/README.md` rows; `tests/ipu/{test_libipu_api.cpp, recording_dmac_channel_face.h/.cpp, fmv_picture_oracle.cpp,
fixtures/coldboot_pictures.sha256, CMakeLists.txt}`; the three helpers `/tmp/p7/{old_fmv_dump,check_env_delta,make_manifest}.py`
(quoted above); this record incl. the module-gate table, the INTEGRATION CHECKLIST and the numeric summary below. OLD transplanted:
`libipu.cpp:67-77`, `:88-105`, `:139-172`, `:212-264`; `mpeg_hle.cpp:2282-2299` (layout only). Flags: `PS2_MPEG_DUMP_N` → DIAG
(OLD emitter armed once to make the golden; no NEW site). Tests: `LibipuApi` (52/52), `FmvPictureOracle` (`WINDOW OK … served_total=547`)
→ `100% tests passed, 0 tests failed out of 102`. OLD bounded runs: ONE OLD binary run (`old_fmv_dump.py` → `run_leg.py` →
`verify/run_bounded.sh`, 55.44 s wall, `run_leg: OK`; side effects `OLD/logs/mpegpic_000..546.ppm` + `OLD/logs/mpegback_006.ppm`);
the oracle tool ×3 through `verify/run_bounded.sh 600` (≈ 20 s each; reads the two `.PSS` only). Ledger/doc deltas: the five §7.1
rows above.  
**Not done:** nothing from the 7E row. Out of every Phase-7 stage's scope by contract (integration items, below).  
**Module gate status:** see the table below — (a)–(g) each with evidence.  
**Next command (for the integrator, not a Phase-7 stage):** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`
(expect `100% tests passed … out of 102`); then the INTEGRATION CHECKLIST, starting with items 5–7 (GuestMemory adapter, IpuDevice
registration + DMAC face, the two `*_bindings.cpp`), whose acceptance is the coldboot leg's `frames,audio,mem` IDENTICAL end to end
(CONSTRUCTION §4.7 integration gate) — expect the served window `k1=2 s1=201 k2=15 s2=346` to reappear there as `frames_served`.

---

## Module gate table (PHASE7_PLAN §6.1; evidence lines quoted from the stage sections)

| # | Item | Status | Evidence (where quoted) |
|---|---|---|---|
| (a) | FMV per-picture golden | ✔ | `tests/ipu/fixtures/coldboot_pictures.sha256`: header `# source=OLD coldboot leg PS2_MPEG_DUMP_N=1000 build_id=HOSTAPP-BUILD-ID 2026-09-25 21:25:53` / `# avcodec_version=62.28.101  served_total=547`, 547 rows with contiguous indices 0..546 (asserted by `make_manifest.py` and re-asserted by the oracle); step 0 pre-count `0`; step 2 quoted: exactly ONE `NOTE (cross-engine): meta env_hash differs: '0x9a0f815a02d994d4' vs '0x4d49c11c76a4d7b8'`, `meta: gate PASS (profile=deterministic, both finalized)`, `diff_legs: IDENTICAL (lanes=frames,audio,mem)`, no other NOTE; step 2b `frames baseline=1494 leg=1494  +['PS2_MPEG_DUMP_N=1000']  -[]  -> OK`; `ctest -R FmvPictureOracle` Passed with `movie1 decoded=203 served=201 (decoded 2..202); movie2 decoded=361 served=346 (decoded 15..360); WINDOW OK` and `s1 + s2 = 201 + 346 = 547 = served_total` (the plan's PREFIX wording → WINDOW, P7-GOLDEN-WINDOW: every golden picture compared, byte-identical); `PssDemux` census `7/7 MATCH` on both movies (7D); `FrameWriter`, `FmvPaceDeadline`, `FmvAudioCapture`, `IpuDevice`, `MpegDecode` (+ `MpegApi` 82/82, `LibipuApi` 52/52) green in the 102/102 run |
| (b) | Audio sink module | ✔ (7A) | `test_audio_pull_ring: PASS (arithmetic, order, underrun, overrun, no-drift, spsc)`; `SdlAudioSinkDummy … Passed` with real dummy callbacks (`pulled=1024`); gate-(b) grep: no `<chrono>`/clock/`MixInto`/`engine/sound` in `engine/platform/sdl/*.{h,cpp}` (7A section); `engine/platform/sdl/README.md` carries the NEW-mechanism row |
| (c) | Input module | ✔ (7B) | `test_input_mapper: 208/208 checks passed` (nine OLD cases + wire case); `PadlogReplay … Passed` (recorder tokens byte-identical on two synthetic padlogs); `BindingsRoundtrip … Passed` incl. whole-file rejection; `touch default_bindings.ini` → `Re-running CMake` / `Configuring done` (`/tmp/p7/7B/regen.log`); `LintEnginePurity` PASS over the `.ini`; `gamepad_backend.{h,cpp}`: the only `static` members are `constexpr` constants (grep in 7E finish) |
| (d) | Tree health at every stop | ✔ | every stage: 0 warnings (forced recompiles in 7D′/7E), ctest 100 % (88 → 90 → 93 → 95 → 99 → 100 → 102), `LintOneClass` 0 findings (385 files), `LintEnginePurity` 0 tokens (322 files), `CoreSingleGetenvSite` Passed (1 site), `SettingsTableReproducible` / `FlagPhaseCountsReproducible` untouched and green in every run |
| (e) | Scope discipline | ✔ | `git status --short` (7E finish) = owned paths + the three shared files whose diff is `3 files changed, 53 insertions(+)` (0 deletions; one `# Phase 7` block at EOF each, created 7A/7A/7C, extended in place 7B/7C); `engine/platform/platform.h` untouched; nothing under `engine/core`, `engine/ee`, `engine/iop`, `engine/sound`, `engine/pad`, `engine/gs`, `app/`, `games/`, `verify/`, `docs/filemap/`, `README.md` |
| (f) | Interfaces, not dependencies | ✔ | `grep -rn '#include "engine/' engine/ipu engine/platform/sdl` minus own headers → `engine/core/log/log.h` (×2, the logger), `engine/ee/mem/mmio_handler.h` (from `ipu_device.h`, header-only), `engine/platform/platform.h` (from `sdl_audio_sink.h` and `pad_sample.h`); every other cross-module need is one of the five `engine/ipu/hle/` interface headers or the ring (P7-NO-PHASE4-DEPS); FFmpeg symbols only in `ffmpeg_video_decoder.{h,cpp}` (`ps2r_ipu` links `FFmpeg::*` PRIVATE); SDL types only in the sdl pairs' `.cpp` files + `sdl_audio_sink.h`/backend headers |
| (g) | Records | ✔ | this file: one section per stage (7A, 7B, 7C, 7D, 7D′, 7E) with files, OLD `file:line`, flags, tests + result lines, finish checks, HANDOFF; the INTEGRATION CHECKLIST and the numeric summary below; the three `/tmp/p7/*.py` helpers quoted in full (7E); §7.1 rows filled (8 plan rows + 31 stage rows + 1 process note) |
| DoD | all of (a)–(g) | ✔ | the three modules exist, are unit- and oracle-proven against OLD-derived goldens (the OLD golden leg proven baseline-identical on all three lanes under the `--cross-engine` meta policy with the env delta pinned to `PS2_MPEG_DUMP_N=1000` alone; every one of the 547 served pictures byte-identical), and are wired to nothing; the findings are recorded |

## INTEGRATION CHECKLIST (PHASE7_PLAN §8.2 verbatim + owner + status; the stage HANDOFF items appended)

Status legend: **open** = not started (integration work, out of Phase 7's scope by contract); **doc** = documented in this file / the
READMEs, nothing to code in Phase 7. No item is closed by Phase 7 — the module gate does not wire anything.

| # | Item (§8.2 verbatim) | Owner | Status / Phase-7 notes |
|---|---|---|---|
| 1 | **Mixer → ring** (Phase 4 `Mixer`, `Step::Verify` body): after `AudioHasher::Push(mixbuf, 800)`, call `AudioPullRing::Push(mixbuf, 800)` on the SAME buffer; `SdlAudioSink::Open(spec, ring)` only when `time.tick_sync == false` (§3.1). | Phase-4 sound / composition root (app/host play-mode wiring) | open — `Open(const platform::AudioSpec&, AudioPullRing&)` is the landed signature (7A). No `Lock/Unlock` bracket exists on the sink (OLD `mixer.cpp:77-82`): add one if `core/savestate` needs the device paused during load. `Close` also `SDL_QuitSubSystem(SDL_INIT_AUDIO)` (7A review) |
| 2 | **FmvPcmSink** implemented by Phase 4 `FmvStream` (`Push` → FIFO producer drop-newest, `GateOpen`, `Flush`; consumed in `MixInto` after voices at core-0 BVOLL/BVOLR gain, `sound_core.cpp:780-812`). Lane-critical timing: gate on the first SERVED picture, flush on Init/Destroy/Reset (INV RISK 12). | Phase-4 sound | open — `GateOpen` is issued ONCE per playback at the first serve after audio was pushed (P7-GATEOPEN-ONCE); a sound-side `Flush` outside `MpegApi` must be followed by a `Reset` of the playing context |
| 3 | **FmvSectorSource** implemented over the Phase-4 stream reader with the SHARED cursor semantics of `cdvd_stream::PumpSectors` (advances the game's `sceCdSt*` cursor; INV OPEN Q5) — or a §7.1-style finding if the NEW reader has no shared cursor. | Phase-4 cdvd | open — the shared cursor is load-bearing for the golden WINDOW (P7-GOLDEN-WINDOW: the pump resumes where the game's windows left the cursor) |
| 4 | **Pad stamp**: `Platform.poll_input` filled from `InputSystem::Frame()` → `ToInputState` on the main thread; Phase 4 `PadProtocol::Stamp` at `Step::MaintDuties` (slot 4, after the RPC end-callback duty — OLD `gs_hle.cpp:373-381`). | Phase-4 pad / composition root (app/host play-mode wiring) | open — `Frame(dt)`'s `dt` is the caller's: for play-mode feel parity use OLD's rule `dt = clamp(wall_delta, 0, 0.25 s)` (`input_system.cpp:274-284`; 7B review MINOR 3). `InputSystem` is constructed only in play mode; headless legs never construct it |
| 5 | **GuestMemory** adapter over `MainMemory`/`AddressMap` (`Translate` = `MainMemory::Pointer(Phys(addr), len)`); `FmvContextLayout` filled from title data (`games/<t>/hle/` — the offsets of §1.5); `MpegApi` constructed with both. | composition root (app/host play-mode wiring) + title | open — the layout also carries the two VALUES `play_to_eof_total`, `mode_raw8`, the four alalc word offsets and the six reference-slot offsets + `ref_slot_status` (P7-LAYOUT-FIELDS); no default lives in `engine/` |
| 6 | **IpuDevice registration**: `MmioRouter::Register(0x10002000, 0x40, ipu, "IPU")` in the composition root; the DMAC ch3/ch4 face (`FeedInputFifo`/`DrainOutputFifo`, `CompleteChannel` synchronously as `dmac.cpp:1512-1550`) from Phase 5's dmac. | composition root (app/host play-mode wiring) + Phase-5 dmac | open — `IpuDevice::kWindowBytes` is the size; the device decodes `addr & 0x3F` (P7-IPU-NO-BASE). The dmac also implements `DmacChannelFace` for `LibipuApi::StopDma/RestartDma` (channels 3/4: CHCR/MADR/TADR/QWC) |
| 7 | **Binding rows** of §8.1 as `engine/ipu/hle/{libmpeg,libipu}_bindings.cpp` (`RegisterLibmpegBindings(SdkBindingManifest&)` form, INV D1) — incl. the `EeTimers` pump before every `GetPicture*` and the `TickSync` demand-park for `wait_ticks`. | integration (bindings owner) | open — register mapping per row in `engine/ipu/hle/README.md`; `PictureResult → v0`; P7-SERVE-PARK-ORDER: if the `mem` lane shows the image plane a picture ahead, split `GetPicture` into `Prepare(ctx, now_tick) → wait` + `Serve(ctx, image_addr)` (park before the write); `sceMpegReset → MpegApi::Reset` keeps the tick grid (P7-RESET-KEEPS-GRID) — do not re-arm; `sceIpuStopDMA/RestartDMA` copy the nine `LibipuDmaSnapshot` words to/from `a0` (the binding's guest access; `a0 == 0` → return) |
| 8 | **FmvActive signal** from `FmvPacer` → the frame boundary (present policy / `FrameAnnotator` input; HACK_LEDGER `:307`) replacing `DecodeActiveRecently` sniffs; savestate refusal during FMV consumes the same signal. | frame boundary owner | open — the signal is `MpegApi::FmvActive()` (context-level, P7-PACER-SHOULDPACE), not on the pacer |
| 9 | **Ledger docs**: `FLAG_LEDGER.md:343/:345` → NORM (winning arm = ON) + `FLAG_PHASE_COUNTS.txt` regen (P7 → NORM 5 / DELETE 4); HACK_LEDGER `:310` superseded, `:134` narrowed to the tick core; INDEX.md `mpeg_ipu` → `mpeg_context + mpeg_api`. | orchestrator / user (docs/filemap read-only for the stages) | doc — P7-LEDGER-BURST, P7-LEDGER-CAP, P7-MPEG-IPU-SPLIT |
| 10 | **`fmv_abort_hold`** (P7-ABORT-HOLD): `games/<t>/hle/workarounds/fmv_abort_hold.{h,cpp}` consuming `MpegApi::ShouldHoldAbort`, a HOOK_RECLASSIFICATION row (class: emulation-bug workaround), retirement gate = coldboot leg IDENTICAL with the hook off after the `cdvd` completion-visibility root fix. | title owner | open — `MpegApi::ShouldHoldAbort(ctx)` is landed (7D′) |
| 11 | **IPU FIFO port** decision recorded (P7-IPU-FIFO-PORT): unmapped as OLD; revisit only if `unhandled_mmio_pages` shows `0x10007000` on a gate leg. | composition root (app/host play-mode wiring) | doc |
| 12 | **Diag categories** (`core/diag`, diag builds only): `input_latency` (re-using `ring_tags.h:39-49`), `fmv_dump` (the `PS2_MPEG_DUMP_N` successor, writing where `Config::paths.log_dir` says), `PS2R_PERF_SCOPE` subsystems `kMpeg`/`kIpu`. | core/diag owner | open — the `fmv_dump` category must write the P6 layout of `tests/ipu/fmv_picture_oracle.cpp` (`PpmBytes`) so future goldens stay comparable |
| 13 | **Anchor padlogs** (P7-NO-ANCHOR-PADLOG): record `input.padlog` per anchor once `PadProtocol` + `InputLog` record run; then the OLD-anchor half of `PadlogReplay` becomes writable. | harness owner | open |
| 14 | **Config mapping**: `InputSettings` ← `Config::input.{device, kbm_pad, bindings_file}` (+ a device-config path rule: next to the exe or `paths.game_files`? — decision for the integrator; the engine takes a path); `IpuSettings` stays constant unless a SETTING row is opened; `audio.enabled` gates `SdlAudioSink::Open`. | composition root (app/host play-mode wiring) | open — also `gamecontrollerdb_path` (P7-GAMEPAD-DB-PATH: one file, located by the root); knob precedence documented (P7-INPUT-KNOB-PRECEDENCE); `InputSystem::Init`'s three `input:` stderr lines → `core/log` if the root prefers |
| 15 | **Meta gate**: stamp `avcodec_version()` into `run.meta` / the config fingerprint (CONSTRUCTION §4.7 `:701-703`) via the `LegCloser`/`Config::Fingerprint` owner. | core/verify owner | open — `FfmpegVideoDecoder::LibraryVersion()` = `62.28.101` on this host; the fixture header carries it and `FmvPictureOracle` fails with `META: avcodec <x> != fixture <y>` on a mismatch |
| 16 | **`docs/phases/README.md` row for Phase 7** — orchestrator/user, after the module gate (the file is READ-ONLY for every Phase-7 stage; the row states the module-gate result and points at `PHASE7_IMPL.md`). | orchestrator / user | open — result: module gate PASSED (below) |
| 17 | (7E, P7-GOLDEN-WINDOW) The coldboot FMV serve sequence is a WINDOW of each movie's decoded pictures (`k1=2 s1=201`, `k2=15 s2=346`): the integration must call `MpegApi::Reset` exactly where the game's `sceMpegReset` fires (after its pre-fed `DemuxPss` windows) so the same pictures are dropped; `frames_served` per movie should read 201 / 346 on the integrated coldboot leg. | bindings owner | open |
| 18 | (7E, P7-LIBIPU-REWIND-WORD) `LibipuApi::RestartDma` derives the rewind from save-buffer word [8] as OLD does; switch to word [7] only through the accepted-difference protocol if a leg ever shows a non-zero rewind. | bindings owner | doc |
| 19 | **Config keys with defaults** (closes FINAL_AUDIT item 1(i)). Config-level keys the three modules consume, with the `engine/core/config/config.h` defaults (`:38-47`): `audio.enabled = true` · `audio.volume = 100` (0..100) · `input.device = "auto"` · `input.kbm_pad = true` · `input.bindings_file = "default_bindings.ini"` · `time.tick_sync = false` (`config.h:68`; the deterministic profile pins it **true**, `deterministic_profile.cpp` `Apply`: `c.time.tick_sync = true` — "audio.enabled stays on: the device is suppressed by time.tick_sync", i.e. `SdlAudioSink::Open` is gated by `audio.enabled && !time.tick_sync`; harness legs therefore never open a device). Module-local settings that stay constants and would become SETTING rows ONLY if a ledger row is opened: `InputSettings` (`engine/platform/sdl/input/input_settings.h:15-18`) `gamepad_deadzone = 0.15`, `gamepad_sensitivity = 1.33`, `gamepad_enabled = true`, `kbm_enabled = true` (+ the three path strings `bindings_path` / `device_config_path` / `gamecontrollerdb_path`, empty by default, filled by the root — item 14); `IpuSettings` (`engine/ipu/ipu_settings.h`) `decode_ahead_pictures = 64`, `sectors_per_pump = 16`, `max_sector_budget = 4096`. Proposed new SETTING candidates — **no key opened by Phase 7**: `audio.ring_quanta` (today the constant `AudioPullRing::kCapacityQuanta = 8`, `audio_pull_ring.h:31`, ≈133 ms); fmv: none. | composition root (app/host play-mode wiring) | doc — every default above is the engine's compiled default; the root maps Config → `InputSettings`/`IpuSettings` (item 14) and adds no key |
| 20 | **Sequencer step for the FMV pacer** (closes FINAL_AUDIT item 1(ii)). **NO new `BoundarySequencer::Step` is added** (the enum stays `PresentRendezvous, TickAdvance, EventDrain, MaintDuties, IopDispatch, EeStreamRpc, Savestate, Verify` — `engine/ee/timing/boundary_sequencer.h:46-48`). The pacer's wait is RETURNED by `MpegApi::GetPicture` (`PictureResult.wait_ticks`) to the `sceMpegGetPicture` binding (item 7), which on the EE thread (a) runs the `EeTimers` pump with the live `CpuContext` BEFORE serving — OLD `host_app/sce_hle/libmpeg.cpp:764` `eetimers::PumpAtBoundary(ctx)` at "the movie loop's once-per-frame boundary, exactly where retail's blocking sceMpegGetPicture lets the ISR run" — and (b) parks the EE on `TickSync` for `wait_ticks` boundaries as a NON-consuming tick waiter, signalling one demand per wait entry and looping to the predicate (OLD `host_app/mpeg_hle.cpp:663-678`: `NoteTickDemand()` + `g_vblank_cv.wait_for(..., vblank_count >= target)`), so the boundary stream advances through the EXISTING `Step::TickAdvance` / `Step::Verify` slots while the EE is parked. `MpegApi::FmvActive()` (item 8) is read by the composition root at `Step::Verify` and published to present policy / `FrameAnnotator`; the pacer itself owns no step, thread, clock or sleep (7D: `FmvPacer` is a pure function of `now_tick`). | bindings owner + composition root (app/host play-mode wiring) | doc — no sequencer change is required or permitted; P7-SERVE-PARK-ORDER (item 7) is the only pre-registered variation and it also stays inside the binding |
| 21 | **Exact all-lanes gate commands (integration gate, NOT Phase 7)** (closes FINAL_AUDIT item 1(iii)). Flag names verified against `verify/run_leg.py --help`, `verify/diff_legs.py --help`, `verify/battery.py --help` (2026-09-28). Coldboot: `python3 verify/run_leg.py --binary build/app/host/nativeee --tree new --anchor coldboot --profile deterministic --frames 1500 --out /tmp/p8/cb` then `python3 verify/diff_legs.py verify/baseline/coldboot /tmp/p8/cb --cross-engine --lanes frames,audio,mem` — expected final line `diff_legs: IDENTICAL (lanes=frames,audio,mem)` over the baseline's 1494 `frames.fhash` lines / 162 `audio.ahash` windows / 17 `mem.mhash` checkpoints (exit 0). Each of the 11 anchors (`artdet_f37152`, `ps2state_slot0_f36935`, `_f39807`, `_f40391`, `_f44111`, `_f46203_red_crystal`, `_f46949_yellow_crystal`, `_f57292`, `_f59991`, `slowspot_f37092_s1789428268`, `slowspot_f40298_s1789693059`; 600 frames each): `python3 verify/run_leg.py --binary build/app/host/nativeee --tree new --anchor save_states/<anchor>.ps2st --profile deterministic --frames 600 --out /tmp/p8/<anchor>` then `python3 verify/diff_legs.py verify/baseline/<anchor> /tmp/p8/<anchor> --cross-engine --lanes frames,audio,mem` — same expected final line. Battery: `python3 verify/battery.py --binary build/app/host/nativeee --lanes frames,audio,mem --reps 3` — expected: 12/12 rows print `ADMISSIBLE` (reps column) and `META-OK IDENTICAL(frames,audio,mem)` (cross-engine column) and `FP-OK` (fingerprint), exit 0 (`battery.py` wording: "ADMISSIBLE (reps identical)"; there is no literal `ADMISSIBLE-STABLE` token — the stability verdict IS the `ADMISSIBLE` word, `UNSTABLE` otherwise). Every NEW leg runs bounded (`verify/run_bounded.sh`), mods-off, deterministic profile. | integration gate owner (Phase 8) | open — Phase 7 claims NO lane (§1.3); these commands are the definition of the lane claim the integration phase must produce |

## Numeric summary

| Measure | Value |
|---|---|
| Files / lines — `engine/ipu/` (device + decode + settings + CMake + README) | 7 files, 980 lines |
| Files / lines — `engine/ipu/hle/` (5 interfaces, layout POD, listener, POD, 8 class pairs incl. `LibipuApi`, README) | 25 files, 2,305 lines |
| Files / lines — `engine/platform/sdl/` (ring, sink, README) | 5 files, 407 lines |
| Files / lines — `engine/platform/sdl/input/` (8 pairs/headers, INI, README) | 16 files, 1,360 lines |
| Files / lines — `cmake/FindFFmpeg.cmake` | 1 file, 41 lines |
| Files / lines — `tests/ipu/` (10 test/tool TUs, 5 support pairs, script, 2 fixtures, CMake) | 23 files, 3,908 lines (549 = the golden fixture) |
| Files / lines — `tests/input/` | 4 files, 867 lines |
| Files / lines — `tests/platform_sdl/` | 3 files, 368 lines |
| Shared-file deltas | `CMakeLists.txt`, `engine/platform/CMakeLists.txt`, `tests/CMakeLists.txt`: 53 insertions, 0 deletions (one `# Phase 7` block each) |
| Tests added / ctest total | 14 ctests (`AudioPullRing`, `SdlAudioSinkDummy`, `InputMapper`, `PadlogReplay`, `BindingsRoundtrip`, `IpuDevice`, `MpegDecode`, `FrameWriter`, `FmvPaceDeadline`, `FmvAudioCapture`, `PssDemux`, `MpegApi`, `LibipuApi`, `FmvPictureOracle`; the plan's 13 + the `MpegApi` smoke registered as its own ctest) → 88 → **102**, all green |
| OLD sources transplanted (≈ lines of the cited `file:line` ranges) | 7A ≈ 80 (`mixer.{h,cpp}`) · 7B ≈ 1,900 (`input/*` ≈ 1,250 + `test_input_mapper.cpp` 645) · 7C ≈ 700 (`ipu.{h,cpp}`) · 7D ≈ 1,100 (`mpeg_hle.cpp` demux/writer/capture ≈ 450, `mpeg_hle.h` pacer ≈ 60, `test_fmv_pace.cpp` 498, `pss_census.py` 140) · 7D′ ≈ 950 (`mpeg_hle.cpp` decoder/context/api ≈ 870 net of diagnostics, `libmpeg.cpp` ≈ 80) · 7E ≈ 120 (`libipu.cpp` ≈ 100, PPM layout 18) → **≈ 4,850** |
| Flags (the 12 P7 rows, by outcome) | NORM 3: `PS2_INPUT`, `PS2_MPEG_PACE_AUTHORED`, `PS2_SND_FMV` · DIAG 2: `PS2_INPUT_LAT`, `PS2_MPEG_DUMP_N` (sites not written; the OLD emitter armed once for the golden) · TEST 1: `PS2_INPUT_SELFTEST` · DELETE 6: `PS2_MPEG_PACE_OFF`, `PS2_MPEG_PDIFF`, `PS2_MPEG_WM`, `PS2_MPEG_SEQCENSUS` (not written) + `PS2_FMV_OPEN_BURST`, `PS2_FMV_DECODE_CAP` (default-ON arms written as the sole path / a constant — findings P7-LEDGER-BURST/CAP; ledger correction = checklist 9). Outside the count, read and not written: `PS2_SND_TICKCLOCK`, `PS2_SOUND`, `PS2_STICKTRACE`, `PS2_PAD_SCRIPT`, `PS2_PERF`, `PS2_EE_TIM0` |
| Findings (§7.1 rows) | 8 plan-time + 31 stage rows (7A 2, 7B 5, 7C 4 + 1 process note, 7D 6, 7D′ 9, 7E 5 — P7-TESTS-PLATFORM-SDL-DIR closed in 7A) = **39** (40 counting the 7C process note) |
| Review loops | 7A, 7B, 7C, 7D: findings dispositioned in the following stage's section; 7D′: one NEEDS_CHANGES iteration (6 items) fixed in place |
| OLD bounded runs | OLD binary: **1** (the golden leg, `run_leg: OK`, 55.44 s wall, `wall_ms=52949` inside the binary; side effects `OLD/logs/mpegpic_000..546.ppm` + `mpegback_006.ppm`). OLD data read through `verify/run_bounded.sh`: `pss_census.py` ×2 (7D), `fmv_picture_oracle` ×3 (7E, ≈ 20 s each) |
| Golden | `served_total = 547` (`s1 = 201` of 203 decoded, window 2..202; `s2 = 346` of 361 decoded, window 15..360); `avcodec_version = 62.28.101`; `build_id = HOSTAPP-BUILD-ID 2026-09-25 21:25:53`; OLD leg baseline-identical on `frames` (1494), `audio` (162), `mem` (17) |

**Phase 7 module gate: PASSED** — (a)–(g) each carry the quoted evidence above; no lane is claimed on any leg (§1.3); nothing is
wired (§6.2). The plan's one wording error (PREFIX → WINDOW) is recorded as P7-GOLDEN-WINDOW with the more specific WINDOW assertion in force.

## Final verification (polish)

After the three MINOR 7E review edits (`tests/ipu/CMakeLists.txt` comment → WINDOW rule wording; §7.1 count 30 → 31 stage rows,
total 38 → 39; P7-GOLDEN-WINDOW "STRONGER" → "different from and more specific than"), re-run 2026-09-28 through `verify/run_bounded.sh`:

```
$ cmake --build --preset macos-dev -j4 2>&1 | grep -c warning:
0
$ ctest --preset macos-dev 2>&1 | grep 'tests passed'
100% tests passed, 0 tests failed out of 102
$ python3 tests/lint/lint_one_class.py --root . | tail -1
LintOneClass: 385 files scanned, 0 findings -> PASS
$ python3 tests/lint/lint_engine_purity.py --root . | tail -1
LintEnginePurity: 322 files scanned, 0 game tokens -> PASS
$ git status --short | wc -l
47
```
