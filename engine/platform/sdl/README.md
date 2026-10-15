# `engine/platform/sdl/`

**Phase:** 6-7  
**Purpose:** The SDL2 host half composed by the desktop targets: the audio device sink and the
PCM pull ring that feeds it (Phase 7 stage 7A), the input backends + mapper (Phase 7 stage 7B,
under `input/`), the window/GL surface (Phase 6). Only the targets in this folder and the
SDL-backed tests see SDL symbols; no engine header outside `platform/sdl` includes `<SDL.h>`.

## Contents (stage 6C; lib `ps2r_platform_sdl`, `find_package(SDL2 CONFIG)`)

- `window.h/.cpp` -- `platform::Window` (hidden 64x64 headless or visible resizable; requests the GL attributes between video init and window creation).
- `gl_context.h/.cpp` -- `platform::GlContext` (4.1 core forward-compatible, depth 32, swap interval 0).
- Phase 7 adds the input backends and the audio sink here under their own target.

## Belongs here

| File | Class | What it is |
|---|---|---|
| `audio_pull_ring.h/.cpp` | `AudioPullRing` | **NEW mechanism.** Single-producer / single-consumer stereo float ring between the tick-clocked mixer and the wall-clocked device thread. `kQuantumFrames = 800` (one vsync at 48 kHz), `kCapacityQuanta = 8` (6400 frames, ~133 ms). `Push(interleaved, frames)` is the producer's (EE boundary thread) call; a full ring discards the OLDEST audio up to the next quantum boundary of the stream position (`overruns` +1, `dropped` += frames) and never blocks. `Pull(out, frames)` is the consumer's (SDL callback) call; an empty ring zero-fills the remainder (`underruns` +1). Counters `pushed / pulled / dropped / underruns / overruns`; `Available()`. Positions are monotonic 64-bit frame counts published with release/acquire atomics; the read position moves by compare-exchange from both sides so a pull that raced a drop discards its copy and reads again. Nothing flows back to the engine |
| `sdl_audio_sink.h/.cpp` | `SdlAudioSink` | The transplanted device half. `Open(const platform::AudioSpec&, AudioPullRing&)`: `SDL_InitSubSystem(SDL_INIT_AUDIO)`, `SDL_AudioSpec{freq = spec.sample_rate (48000), AUDIO_F32SYS, 2 ch, 512 samples}`, `SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0)` (`allowed_changes = 0` so the callback format is exactly the request), `SDL_PauseAudioDevice(dev, 0)`; the callback is `ring.Pull(out, len / (2 * sizeof(float)))` and nothing else. `Close()`: `SDL_CloseAudioDevice` (waits for an in-flight callback) then `SDL_QuitSubSystem(SDL_INIT_AUDIO)`; idempotent. `Running()`. A second `Open` while running is a no-op returning true. Whether to open at all is the CALLER's rule: under the deterministic profile (`time.tick_sync = true`) the composition root never calls `Open`, so the harness runs with no device |
| `input/` | `InputSystem`, `InputMapper`, `KeyboardMouseBackend`, `GamepadBackend`, `GamepadConvert`, `PadSample`, `default_bindings.ini` | stage 7B: the pad host half (main-thread poll → `PadSample` → `InputState` snapshot); see `input/README.md` |

## The NEW-mechanism note (PHASE7_PLAN §3.1; DECISIONS D2; BUG-002 / BUG-014)

OLD had no PCM output ring: in play mode the SDL callback mixed directly into the device buffer
at device rate (device-clocked mixing), and under the tick-clocked profile the device was never
opened and the EE boundary mixed 800 frames per vsync into a stack buffer that the audio hasher
consumed. NEW keeps the tick-clocked producer in BOTH modes and inserts `AudioPullRing` between
the mixer and the device. Consequences (play mode only; not harness-visible, since the harness
never opens the device → no accepted-difference record):

- the game-visible sound state (NAX/ENDX/VOLX readbacks) is a pure function of the EE frame
  counter in play mode too;
- audible latency is the ring depth (≤ 133 ms) rather than the device-buffer latency;
- sustained producer lead is bounded by drop-oldest (audible skip, `overruns`/`dropped` count);
  sustained producer lag by zero-fill (audible gap, `underruns` count);
- NOTHING crosses the ring right-to-left: no counter, no clock, no back-pressure reaches the
  engine. `PcmSource` (D1) IS the ring — the sink takes an `AudioPullRing&`, nothing else.

## Does not belong here

- The PS2 pad protocol (`pad/hle`), the SPU2 voice model / mixer (`engine/sound`), the FMV
  PCM FIFO (`engine/ipu/hle`). The sink calls no `MixInto`-equivalent and includes nothing
  from `engine/sound`.
- Device-clocked mixing (the OLD play-mode path): the callback only ever pulls.
- `SDL_LockAudioDevice`/`Unlock` brackets for savestate load (OLD `mixer.cpp:77-82`): no
  consumer in this phase; integration adds them if `core/savestate` needs the device paused.
- `sound.ini` next to the executable via `SDL_GetBasePath` (OLD `mixer.cpp:86-92`): superseded
  by `Config`; not written.
- Any `getenv`: the OLD `PS2_SOUND` / `PS2_SND_TICKCLOCK` gates are the caller's `Config`
  (`time.tick_sync`; NORM in Phase 4).

## Transplanted from (OLD, read-only)

| NEW | OLD source (`file:line`) |
|---|---|
| `SdlAudioSink::Open` | `host_app/sound/mixer.cpp:38-43` (spec), `:45-49` (`allowed_changes = 0`), `:54` (`SDL_PauseAudioDevice`), `:36` (idempotence); `mixer.h:1-29` (doc block) |
| `SdlAudioSink::Callback` | `mixer.cpp:17-21` (`len / (2 * sizeof(float))` frames), with `MixInto` replaced by `ring.Pull` |
| `SdlAudioSink::Close` / `Running` | `mixer.cpp:62-69`, `:71` |
| `AudioPullRing` quantum | `host_app/verify/verify_boundary.cpp:22` (`kFramesPerVsync = 48000 / 60`), `:60-73` (`MixInto(mixbuf, 800)` once per boundary, then the hasher push — the producer contract the ring receives) |
| (not written) | `mixer.cpp:26-35` (`Enabled()`/`TickClocked()` gates: caller's rule), `:77-82` (lock brackets), `:86-92` (`DefaultConfigPathImpl`) |

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Tests: `tests/platform_sdl/` (ctest `AudioPullRing`, `SdlAudioSinkDummy`).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.7; `docs/phases/PHASE7_PLAN.md` §2.3 / §3.1 / §4.4; record `docs/phases/PHASE7_IMPL.md`.
