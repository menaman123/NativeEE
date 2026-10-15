# `engine/platform/sdl/input/`

**Phase:** 7 (stage 7B)  
**Purpose:** The host half of pad input: SDL keyboard/mouse and game-controller state → one
DualShock 2 sample per main-loop iteration. Everything here runs on the MAIN thread (SDL's
rule); the EE boundary receives a `platform::InputState` snapshot that the pad protocol
(`engine/pad`, Phase 4) stamps into the PADMAN slots on the tick domain. No clock is read
here: the mouse-decay `dt` is a `Frame`/`Map` parameter. Headless legs never construct these
classes — the harness selects `InputLog` replay or the neutral pad by CLI.

## Belongs here

| File | Type | What it is |
|---|---|---|
| `input_backend.h` | `InputBackend`, `NormalizedInput`, `LogicalButton` | The device-facing interface (`Poll` / `IsActive` / `Name`), the device-agnostic control POD (sticks float [-1,+1] with y down+, buttons ACTIVE HIGH, mouse deltas + 3 buttons) and the button bits in DualShock 2 WIRE order (`kBtnSelect = 1<<0 … kBtnSquare = 1<<15`). SDL-free |
| `pad_sample.h/.cpp` | `PadSample` + `ToInputState` / `FromInputState` | The output sample: `uint16 buttons` active-high, `lx/ly/rx/ry` bytes (neutral `0x80`). `ToInputState` performs the ONE active-low inversion (`wire = ~buttons`); neutral `PadSample{}` ↔ `InputState{0xFFFF, 0x80×4}` |
| `input_mapper.h/.cpp` | `InputMapper`, `Bindings`, `StickToByte`, `ScancodeFromName`, `ParseMouseName` | Bindings owner + `NormalizedInput → PadSample`. INI grammar (`[sticks]/[buttons]/[gamepad]/[pad]`, `;`/`#` comments, strict floats, `key = value` only inside a section): ANY malformed line or unresolvable name rejects the WHOLE file and the current bindings stay. Baked default = the configure-time generated `${PS2R_GENERATED_DIR}/input_default_bindings.h` (`kDefaultBindingsIni`, from `default_bindings.ini`), parsed by the same parser at construction. `Map`: mouse accumulate (`d · sensitivity · 0.01`) → decay `exp(−decay·dt)` → snap below half a byte → clamp → add to the right (or left) stick → `StickToByte` (`lroundf(v·127.5 + 127.5)`, clamped). `SDL_GetScancodeFromName` is this pair's ONLY SDL call |
| `gamepad_convert.h/.cpp` | `GamepadConvert`, `GamepadRaw` | PURE controller mapping (no device): SDL logical layout = DualShock 2 layout (A→Cross, B→Circle, X→Square, Y→Triangle, Back→Select, Start, sticks→L3/R3, shoulders→L1/R1, dpad), triggers > 25 % → L2/R2, `Stick()` = normalise /32767, clamp, radial deadzone SNAP (`|v| ≤ deadzone` → 0, no rescale), per-axis scale by `sensitivity` and clamp |
| `keyboard_mouse_backend.h/.cpp` | `KeyboardMouseBackend` | `SDL_GetKeyboardState` → mapper tables; `SDL_GetRelativeMouseState`; relative-mouse capture on left click (unless the UI owns the mouse), release on ESC / focus loss; deltas only while captured; `InjectKey` overlay for integration tests (member state, consumed per `Poll`). Class name kept, file named per the lint (finding P7-KBM-FILE-NAME) |
| `gamepad_backend.h/.cpp` | `GamepadBackend`, `PadInfo` | SDL_GameController devices: every pad opened (generic fallback mapping for unmapped joysticks), hotplug via `SDL_JOYDEVICEADDED/REMOVED`, selection Auto / None / instance id with reconnect-by-GUID, deadzone/sensitivity clamps, `Snapshot()`. Multi-pad merge: buttons OR, sticks by strictly larger magnitude. ALL state instance-owned (no statics, no atomics: main thread only) |
| `input_system.h/.cpp` | `InputSystem` | Owns the mapper + both backends. `Init(InputSettings)`: bindings file over the baked default, backends from the effective deadzone/sensitivity/enabled, device-config file. `HandleEvent(const SDL_Event&)`. `Frame(dt)`: poll kbm FIRST then gamepad; merge = buttons OR, each stick axis by STRICTLY greater magnitude (kbm wins ties), mouse deltas summed, mouse buttons OR; `Map`; UI capture → mouse accumulator reset + neutral sample; kbm result polled but DISCARDED when kbm is disabled. `LoadDeviceConfig(path)` / `SaveDeviceConfig(path)` (`pad = auto|none|guid:<GUID>`, `kbm`, `deadzone`, `sensitivity`) through caller-supplied paths only |
| `input_settings.h` | `InputSettings` | POD the composition root fills from `Config`: `bindings_path`, `device_config_path`, `gamecontrollerdb_path`, `kbm_enabled`, `gamepad_enabled`, `gamepad_deadzone = 0.15`, `gamepad_sensitivity = 1.33` |
| `default_bindings.ini` | data | The baked default: mouse → right stick (0.35 / 12.0), W/S/A/D → left stick, `cross=Space square=LShift circle=Mouse2 triangle=E l1=1 l2=2 r1=3 r2=4 l3=5 r3=6 start=Y`, `[gamepad] enabled=1 deadzone=0.15 sensitivity=1.33`, `[pad]` = SDL's standard layout (`cross=a circle=b square=x triangle=y ... l2=lefttrigger r2=righttrigger`) |
| `pad_button_map.h/.cpp` | `PadControl` (POD), `PadButtonMap` | The gamepad-button remap (CHARTER 2 A5): one SDL control (button index or trigger axis past 25 %) per LogicalButton bit; default = SDL's standard layout == the DualShock 2 layout; `ControlFromName`/`ControlName` use SDL's control-name tables (no SDL_Init). `Bindings.pad_map` is parsed from `[pad] <ps2 button> = <control|none>`; `InputSystem::Init` hands it to `GamepadBackend::SetButtonMap`, `GamepadConvert::Convert` applies it. The launcher's Controls page writes it by press-to-bind (`app/host/controls_bindings`) |

Precedence of the gamepad knobs in `InputSystem::Init`: `InputSettings` (Config) < the bindings
file's `[gamepad]` section when that file loads < the device-config file.

## Does not belong here

- Stamping into the PADMAN slots, the pad protocol, `platform.poll_input` wiring (`engine/pad`,
  `app/host` — integration).
- Any clock (`dt` is the caller's), any environment variable, any exe-relative path lookup
  (`SDL_GetBasePath`): paths arrive in `InputSettings`.
- Scripted key injection specs / selftest grammar (`InputLog` replay in `core/triage` is the ONE
  pad-script mechanism), latency probes, stick traces (`core/diag` categories, not written).
- A `Bindings` serializer (test-local in `tests/input/test_bindings_roundtrip.cpp`).

## OLD provenance (read-only, `$PS2R_OLD_ROOT/host_app/input/`)

| NEW | OLD |
|---|---|
| `input_backend.h` | `input_backend.h:14-46` |
| `pad_sample` | `input_mapper.h:17-23`; the inversion `pad_input.cpp:783-785` |
| `input_mapper` | `input_mapper.h:26-93`, `input_mapper.cpp:21-301`; generator `host_app/CMakeLists.txt:104-113` |
| `gamepad_convert` | `input_backend_gamepad.h:25-38`, `.cpp:16-21`, `:45-113` |
| `keyboard_mouse_backend` | `input_backend_kbm.h:13-31`, `.cpp:15-87` |
| `gamepad_backend` | `input_backend_gamepad.h:42-100`, `.cpp:200-416` (statics/mutex → instance state; stick-trace hook not written) |
| `input_system` | `input_system.h:22-54`, `.cpp:85-127`, `:175-208`, `:222-243`, `:305-340` (env gate, selftest, clock, latency probes not written) |
| `default_bindings.ini` | `default_bindings.ini:6-37` (header reworded) |

## Rules

- MAIN thread only; the boundary consumes a snapshot (PHASE7_PLAN §3.2).
- Only this folder's `.cpp` files and the SDL-backed tests include `<SDL.h>`; the headers name
  `SDL_Event` by forward declaration only.
- `LintEnginePurity` scans `default_bindings.ini`: no title tokens in the data file.
