# `engine/sound/`

**Phase:** 4 (stage 4E′: `VagDecoder`; 4E″: `SpuCore`, `Voice`, `Mixer`, `CommandRing`, the libsd / libsdr bodies; the SNDC/SDRX savestate sections are Phase 6)  
**Purpose:** The SPU2 half -- ADPCM decode, voices, the tick-clocked mixer (no host device here).

## Belongs here

- vag_decoder (pure), then sound_core / voice / mixer and hle/ (libsd, libsdr)
- fmv_pcm_input: the mixer's FMV PCM source (the sound module's `ps2r::ipu::FmvPcmSink`; Phase 7 integration row 7.2). `SpuCore::MixInto` pops it per 128-frame chunk and adds the frames AFTER the voice mix at the core-0 BVOLL/BVOLR gain (`SpuCore::ExternalInputGain`: bit 15 → unity, else `(v & 0x7FFF) / 32767`), before the master gain and the clamp; consumption runs whenever the gate is open (a zero gain still drains the queue). Queue: `1 << 22` stereo frames, drop-NEWEST + counter, `GateOpen` enables consumption, `Flush` closes the gate and discards the queue.

## Does not belong here

- host audio devices (app/), title bank layouts (games/), probes (core/diag)

## Transplanted from (OLD, read-only)

- `sound/vag_decoder.{h,cpp}` (4E′); `sound/voice.{h,cpp}` → `voice`, `sound/sound_core.{h,cpp}` → `spu_core` (file = snake_case(`SpuCore`) per the one-class lint), `sound/ring.h` → `command_ring`, `sound/mixer.{h,cpp}` engine half → `mixer` (4E″); `sce_hle/libsdr.cpp:94-137, :170-260, :271, :311-395` → `hle/{sd_block_trans, sdr_remote, libsdr_bindings}` and `iop_stubs.cpp:1678-2003, :2362-2445` → `engine/iop/hle/libsd/libsd_module` (4E″ iteration 2); the SNDC/SDRX codec half (`sound_core.cpp:1000-1178`, `libsdr.cpp:400-440`) and the FMV half of `sound_core.cpp` (4E‴)

## Known OLD quirks kept for equivalence

- **Eager ENVX step.** `SpuCore::EagerEnvx0` (OLD `sound_core.cpp:304-312`) publishes the first attack step of a just-keyed voice as `2^(ar/2)`-scaled, while `Voice::AttackStep` (OLD `voice.cpp`) ramps with `4·2^(rate/4)`; OLD's comment claims the eager value "mirrors" AttackStep, but the exponents differ. The eager value is only the readback a poll sees between the KON and the next pump (the ring applies the true step at the pump), so the audible output is unaffected; the readback is kept OLD-exact for the harness. A Phase-6/7 equivalence record may replace it with the true first step once a leg observes the difference.
- **`SdBlockTrans` is virtual.** OLD paced the FMV block transfer on the wall clock (`std::chrono`, `PS2_SDR_BLOCK_MS`) with an FMV-consumption "real mode"; NEW paces it on the mixer's frame count (`audio.sdr_block_ms` × 48 frames) -- same ramp, same completion, deterministic. The real mode (OLD `FmvPacerLive` = gate open and frames queued) is not wired; `FmvPcmInput` exposes `GateIsOpen()`/`Queued()` should a leg need it.
- **FMV PCM at the core-0 BVOL gain.** The SPU2 mixes core 0's external audio input (the IOP-side PCM path) under BVOLL/BVOLR; OLD's sound core (`sound/sound_core.cpp:772-812, :906-947`) fed the movie's muxed PCM through that gain after the voices, FIFO `1 << 22` frames drop-newest, gate opened at the first served picture and closed by the lifecycle flush. `FmvPcmInput` + `SpuCore::MixInto` keep the rule; the gain is a per-pump read of the shadow (OLD read it per `MixInto` call too). The coldboot leg's title leaves BVOL at 0 through its first 400 frames, so the FMV PCM mixes to silence there (census `fmv_pcm=…,bvol=0x0/0x0`).
- **The SPU2 runs per field, not per EE frame (BUG-079, DP-29).** The title programs core-0 BVOLL/BVOLR = 0x7FFF at movie start (its EE code: `sceSdRemote(1, 0x8010, 0xF80/0x1080, 0x7FFF)`, measured on the console) and back to 0 at movie end; both reach `SpuCore::SetParam`. A mixer pumped only at EE frame boundaries mixes the movie's queued PCM AFTER that zero (no boundary passes during a movie), so the composition root pumps once per display field in play mode (device only; the hash lane keeps 800 frames per boundary). The census `BvolSets()/BvolSetsNonZero()/BvolPeak()` and `FmvFramesMixedAudible()` (frames mixed while a BVOL gain was non-zero) name the set-then-clear a final-shadow read hides.
- **`SdBlockTrans`/`SdBlockTransStatus` (libsd ordinals 10/12) answer 0** as OLD did ("phase 2"): no IOP-side autodma model; STREAM.IRX polls them (2 each in the coldboot leg) and proceeds.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

## 4E‴ provenance

| NEW | OLD | Rule kept |
|---|---|---|
| `command_ring` over `core/threads` `CmdRing<SoundCommand, 4096>` | `sound/ring.h` | MPSC, producer-only mutex, lock-free consumer, drop-oldest + count: the SPU2 register file keeps the latest write. |
| `spu_core` atomics + the seq_cst eager-hold protocol | `sound_core.cpp:355-390, :842-848` | A poll between a KON and the next pump sees the voice started; the pump's republish never clobbers a fresh eager NAX/ENVX (the hold is re-asserted). |
| `hle/sdr_remote` `Dispatch` (0x8010 / 0x80E0 / 0x8100 only) vs `Invoke` (full model) | `libsdr.cpp:311-395` | The EE `sceSdRemote` row keeps the reference scope: rSdInit from the EE does not silence the module's voices; status queries read 0. |
| `sound_state_codec` (SNDC/SDRX) | `sound_core.cpp:1000-1178`, `libsdr.cpp:400-440` | Capture drains the ring first; restore zeroes the eager holds before republishing from the imported voices. |
| (deleted) `spu_register_window` | — | The SPU2 register window on the IOP bus (0x1F900000) was deleted at zero hits in every arm: the title reaches the SPU2 only through libsd imports (`docs/design/M12_DELETION_LEDGER.md` D1). |

