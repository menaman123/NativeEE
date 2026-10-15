# M12 DELETION LEDGER — every model the play census shows at ZERO (track 3b, 2026-10-11)

Herschel (M3→M5), TRACK 3b preparation. Every number below is read from a file named here; the
evidence root is `int_work/m_series/cutover/allon_w1/` (the integrate_w1 record, `main de48cfb4`,
code HEAD `1bb648b5`, BUILD-ID `2026-10-11 03:08:39`):

- **play** = `s3_play/leg/run.meta` — the s3 padlog replay, play profile, wall-paced, 3000 frames,
  `startup=native iop_boot=0`, `footprint peak_mb=602` (`b60_mb=170`); `s5_play/leg/run.meta` peak 481.
  (The `leg/` dir was removed by the 2026-10-11 09:04 disk-full cleanup; the census survives verbatim as the
  indented `census:` block in `s3_play/run_leg.log` and in `s3_play.summary.txt`.)
- **det** = `det/run.meta` — the deterministic coldboot, 1300 frames, `profile=deterministic`,
  `hle.startup=retained` (the IOP oracle arm), `iop_boot=1`, `footprint peak_mb=429` (`b60_mb=92`).

"ZERO in play" is the DT-13 census criterion (replaced-model counters 0 while the owner's work is
positive). "Keep-list" names the deterministic-profile oracles (Root legs under `--profile
deterministic`) and the component oracles (ctest executables) that still execute the model; a model
with a non-empty keep-list is moved behind a tests-only target, not deleted. "RSS (play)" is the
model's resident contribution when it is NOT executing (what deleting it from the play binary would
return); the method is in §RSS method. LOC are `wc -l` of `.cpp/.h/.inl` in the named directories.

The one fact this ledger has to state up front: **the models at zero in play together hold roughly
8–10 MB of the 602 MB peak** (§RSS attribution). The peak is owned by the GS pipeline — the
pipelined GS queue, texture residency, the VRAM images and `video.scale=2` targets — none of which is
at zero in play. Deleting every zero-census model will not make peak_mb "drop materially"; the
material drop is TRACK 1d (Zeno: scale 1 default, texres trims) and the GS queue depth.

## 1. The ledger

| # | Model | Play census (ZERO) | Det census (the oracle arm) | Source (LOC) | Tests | Keep-list (who still needs it) | Owner | RSS (play) |
|---|---|---|---|---|---|---|---|---|
| L1 | **IOP kernel + stock modules** (cooperative R3000 kernel, IOP RAM, sysmem heap, loader, module table, host thread, the kernel-facing HLE modules sysmem/intrman/stdio/sysclib/thbase/thsemap/sifcmd/sifman/timrman/ioman/cdvdman/idle, IOPM savestate section) | `iop_boot=0 iop_modules= iop_hb_beats=0 iop_threads=none iop_semas=none iop_clock=frames=0,us=0 iop_inframe_us=0,0.00 iop_heap_calls=allocs=0 iop_unrouted=0 iop_timer_running=none iop_thread_ops=…=0 iopm_restored=0` | `iop_boot=1 iop_modules=excepman:257,…,STREAM:256 (30 rows) iop_hb_beats=1296 iop_threads=1:Wait/Sleep,2:Wait/Sema iop_inframe_us=22279,9076.44 iop_timer_running=1` | `engine/iop/` 2346 + `engine/iop/hle/*` 1817 + `engine/iop/savestate/` 295 = **4458**; target `ps2r_iop` | tests/iop: IopKernel, IopClock, IopClockRebase, IopLoader, ModuleTable, SifRpcBridge, SifmanDmaStatComplete, KernelFacingModules, SysclibMap, SysmemHeap, FiberGuard, IopShortcuts, IopmBootSymmetricRepark; tests/cdvd: CdvdmanModuleDisc | **Det Root legs** (every `--profile deterministic` leg boots it: `deterministic_profile.cpp:49`), RootCdvdPollVisibility (green; asserts `cdvd_poll_deliveries>0`, `loader_spin_lifts rpc>0` — IOP in-frame work), RootRegistersIopmSection / RootRestoresStreamWorker / RootRestoresStreamHeartbeat / RootAnchorHeartbeat* (in the recorded GCLK red set, `red_names.txt`); component oracles StreamFileSelfOracle, AudioRampSelfOracle, LoaderCompletionModule, EeRpcServer, ServiceTimeline, test_movie_buffers link `ps2r_iop`; **the play degrade DAG**: `--set` of any of 12 keys to off → `hle.startup=retained` → the IOP boots (`config_validator.cpp:68-79`) | Herschel | IOP RAM 2 MB (`IopMemory` zero-filled `std::vector`, resident at construction); kernel/module statics < 0.1 MB; no thread stacks (none created) |
| L2 | **Recompiled IOP code — STREAM.IRX** (the frozen IOP recompiler's TU) | `iop_boot=0` (never entered; `StreamIrxPlugin::Boot` not called: `ee_bootstrap_services.cpp:135-137`) | executes (`stream_rpc_calls=9476/0`, `iop_rpc_tid=1`) | `games/<title>/iop/stream_irx/iop_recompiled.{cpp,h}` **66763** + `abi/` (stub surface 74 externs) ≈ 300; `-O0 -w`; `libps2r_title_stream_irx.a` `__text` 665 KB | tests/iop: StreamIrxStubSurface, StreamIrxLoadsAndParks, StreamIrxServesFirstRpc, IopRecompManifestIntact | the det Root legs (L1), StreamFileSelfOracle (NEW STREAM.IRX vs the native owner, 6408 file bytes), AudioRampSelfOracle (`${PS2R_OG_ROOT}/game_files/STREAM.IRX`), LoaderCompletionModule ("dest holds the file bytes through sif dma") | Herschel | text 0.65 MB file-backed, **not paged in** when never executed; data < 0.05 MB |
| L3 | **STREAM.IRX plugin + LTI protocol + the STREAM RPC server** (hosts L2 under L1; the 0x12345 endpoint in the retained arm) | `sif_bound=STREAM_NATIVE@0x12345 stream_rpc_calls=0/0 stream_rpc_cmds= iop_rpc_tid=0` (the native `StreamTransport` answers: `stream_transport=installed=1,batches=12311,…,server_calls=12311`) | `sif_bound=STREAM@0x12345 stream_rpc_calls=9476/0` | `stream_irx_plugin.{cpp,h}` 311, `stream_rpc_server.{cpp,h}` 216, `lti_protocol.{cpp,h}` 124, `module_layout.h`, `feed_contract.h` = **≈ 760** | the three StreamIrx* tests above | same as L2 | Herschel | < 0.1 MB |
| L4 | **libsd model** (the IOP libsd HLE module over `SpuCore`; voice/block transfer; the SPU2 register window on the IOP bus) | `libsd_calls= libsd_trans=0/0/0 iop_bus_window_hits=0 sdrdrv_calls=0 snd_kon=0 snd_active=0 audio_mix spu_mix_calls=0` (owned: `sound_output=installed=1,mix_ticks=6423`, `sound_effects=installed=1`) | `libsd_calls=0:1,1:596,2:62352,3:21544,4:21544,5:6,6:517056,7:19,11:10776,12:9476,13:6,14:6,15:5` — **but** `libsd_trans=0/0/0 iop_bus_window_hits=0 sdrdrv_calls=0 spu_mix_calls=0` in det too | `engine/iop/hle/libsd/` 242 (in L1's target), `engine/sound/hle/sd_block_trans.{cpp,h}` 112, `engine/sound/spu_register_window.{cpp,h}` 205, `IopMemory::BusWindow` (iop_memory.h/.inl); `SpuCore` itself (`engine/sound/spu_core.*` 534) is the common `Mixer`'s core — still constructed in play, `spu_mix_calls=0` | tests/sound: LibsdModule, SpuRegisterWindow, SpuCoreHammer, SoundVoice, MixerTickPump | the det arm's STREAM.IRX calls libsd (L4 call census); **the register window and the voice/block transfer have no caller in either arm** — see §2 D1 | Herschel | SPU RAM 2 MB (`SpuCore::spu_ram_`) + window/trans < 0.05 MB |
| L5 | **SIF DMA model** (syscall 119 `sceSifSetDma` EE→IOP byte copy + clamp; IOP→EE `EeRamSink` from sifman; DMAC SIF5/6 kick callback) | `sif_dma_bytes=0/0/0 dmac_sif_kicks=0 sif_reg_access=0/0` | `sif_dma_bytes=0/0/0 dmac_sif_kicks=0` (the 9476 RPCs travel the `SifRpcBridge`, not DMA) | `engine/sif/` 123 + `engine/sif/hle/` 810 (of which `SceSif`/`RpcRouter`/`EeRpcServer` are the EE-side SDK rows — `sif_rows=18`, **live in play**: `sceSifCallRpc:12311`); the DMA pieces: `SyscallTable::SetSifDmaCopy/SifDmaBytes` (~40 lines in `engine/ee/hle/syscall_table.*`), `main_memory_ee_ram_sink.*` ≈ 60, `engine/iop/hle/sifman/ee_ram_sink.h` | tests/sif: SceSifDma (the model's own test), SifmanDmaStatComplete; LoaderCompletionModule and StreamFileSelfOracle assert "dest holds the file bytes through sif dma" (the IOP→EE sink in the retained arm) | the IOP→EE sink is the retained arm's file-byte path (keep with L1/L2); the EE→IOP syscall copy has **no caller in either arm** — §2 D2 | Herschel | < 0.05 MB |
| L6 | **cdvd command / delivery model** (`DeliveryMeter` deliveries, `LoaderModel` poll/boundary/wait completions, `DeliveryBudget`, the IOP cdvdman reads, `StreamReader` sceCdSt ring) | `cdvd_deliveries=0/0 cdvd_poll_deliveries=0 cdvd_boundary_deliveries=0 cdvd_wait_deliveries=0 cdvd_meter_pops=0/0 cdvd_forced=0 iop_cdvd=reads=0,sectors=0 loader_polls=pending=0,done=0` — **not zero**: `cdvd_seeks=contig=254,short=32,long=9` (the meter's seek classification still runs for `FileSchedule`'s ReadTiming), `cdvd_rows=25` (the EE-side libcdvd SDK rows: `sceCdInit:2 sceCdDiskReady:2 sceCdMmode:2 sceCdReadClock:11` — live) | same zeros (`cdvd_deliveries=0/0`, `iop_cdvd=reads=0`) with `loader_polls=…,done=4` | `engine/cdvd/` 806 (`iso_fs`, `read_engine`, `stream_reader`, `delivery_meter`, `delivery_budget`) + `engine/cdvd/hle/` 693 (`libcdvd_bindings` live; `loader_model` the model) | tests/cdvd: DeliveryMeter, LoaderModelAndDeliveryBudget, CdvdStreamReader, LoaderCompletion*, LoaderSeams, LoaderSpinLift, LoaderRingCache, IsoFsAndReadEngine (live), LibcdvdBindings (live) | **RootCdvdPollVisibility** (green, deterministic: `cdvd_poll_deliveries>0`, `cdvd_wait_parks>0`, deliveries == async_issued) is a pure oracle of this model; `IsoFs`/`ReadEngine` are the live byte providers (keep); the seek classification is shared with the owned `FileSchedule` (attribute before deleting) | Herschel | < 0.1 MB (no sector buffers held when idle; the loader ring is `LoaderFileIo`'s, live) |
| L7 | **IPU command interface + IPU DMA** (the IPU device: BCLR/IDEC/BDEC/VDEC/FDEC/SETIQ/…; the ch3/ch4 DMAC faces; libipu SDK rows) | `ipu_commands=…all 0 ipu_unsupported=…0 ipu_unmodelled=0 libipu_calls=init=0,sync=0,stop=0,restart=0 ipu_operations=…0 ipu=divergent=0,unhandled_writes=0` (owned: `movie_decoder=…pictures=1667`, `fmv=…pictures=1667`) | all 0 (no movie in 1300 frames) | `engine/ipu/` 1202 (`ipu_device`, `ipu_vlc`, `ipu_color`, `mpeg_decode`) + `engine/ipu/hle/` 3397 (of which the FFmpeg decoder/`decoded_picture`/`movie_*` owners are **live**; the model is `ipu_device`, `dmac_channel_face*`, the libipu/sceMpeg retained bodies) | tests/ipu/* (device, VLC, color, the DMAC face), MovieLifecycleOracle (compares the retained NEW bodies init=2/stop=3/BCLR=4 vs selected 0) | MovieLifecycleOracle is the only oracle that executes the retained IPU bodies (HLE_COVERAGE MOV-1); no Root leg at either arm issues an IPU command | Movie (port-movie) | IPU device buffers: < 1 MB (the `IpuDevice` input FIFO/decoder scratch; measure before claiming) |
| L8 | **DMAC → VIF → GIF parse paths** (chain walker, VIF UNPACK/DIRECT, GIF tag parser/arbiter/register dispatch, the gs_sink fan-out) | **NOT ZERO** in play: `gs_sink=tags=1266676,regs=4548381,packed=54803092,kicks=27937250,image_qw=52906048`, `dmac_tail_chases=3000`, `gs_syncpath_flushes=9216`; zero sub-rows: `dma_spr=…all 0` (scratchpad DMA, HLE_COVERAGE DMA-2), `vif_mmio_gated=r0,w0`, `dmac_sif_kicks=0`, `graphics_kick_lowering_worker raw=0,unsupported=0` (every VU1 kick lowered — track 1a/1b) | `gs_sink=tags=101350,…,kicks=1087173` | `engine/dmac/` 1738 + `engine/dmac/hle/` 347, `engine/vif/` 1437, `engine/gif/` 1669 + `engine/gif/hle/` 328 + `engine/gif/savestate/` 103 | tests/dmac/*, tests/vif/*, tests/gif/* (GIF PARSER EQUIVALENCE, KickLowering 510 checks, LOWERING-PARITY) | the parse still executes in play — track 1c (Zeno) retires it (`gs_sink → 0`); `dma_spr` (scratchpad DMA) is zero in both arms and in the 39-run battery (`dmac_spr_transfer.*`, DMA-2) | Zeno (1c) | the DMAC/VIF/GIF state is small (< 1 MB); the cost is the GS queue the parse feeds (§RSS attribution) |
| L9 | **VRAM mirrors** (`GsState::vram_` the EE-side 4 MB `VramImage`; the pipelined queue's split image `VramOpLog`→consumer `VramImage`; `ReadbackEngine::scratch_` 4 MB; `TransferEngine`) | **NOT ZERO**: `gs_xfer=trxdir=45059,completed=45059 gs_vram_epoch=90118 gs_queue …vram_ops=740434,vram_op_bytes=1799441536,split_image=1`; `gs_xfer local_local=0 local_host=0` (the local→local / local→host copies are zero) | `gs_xfer` positive | `engine/gs/vram/` 888, `engine/gs/xfer/` 493, `engine/gs/hle/targets/readback_engine.*`; `engine/gs/` total 16526 | tests/gs/* | every texture upload in play still lands in the EE-side image and is replayed on the consumer's image (texres decodes from it: `texres=lookups=674744,decodes=398`) — retiring it is the M6 `GraphicsResources` provider cutover (HLE_COVERAGE GS-1) | Zeno / Socrates / Lagrange (graphics) | **3 × 4 MB images = 12 MB resident** + the op-log payload in flight (`vram_op_bytes` 1.8 GB cumulative over 3000 frames; the queue's `max_depth=12994` jobs) |
| L10 | PADMAN/pad protocol model (`padman_calls=0/0/0/0 pad_calls=open=0,read=0,getstate=0,served=0 pad_stamps=0`; owned `input_service=selected=1,samples=3000,reports=4644`) and MCSERV/mc command model (`mcserv_calls=0/0 mc_calls=…0 mc_bound=0`; owned `save_storage=selected=1,calls=4664,committed=10`) | zero in play | zero in det as well (`padman_calls=0/0/0/0 mcserv_calls=0/0`) — the det arm selects `input_service`/`save_storage` too | `engine/pad/hle/` 788, `engine/mc/hle/` 779 + `engine/mc/savestate/` 97 | tests/pad/*, tests/mc/*, RootIopmMcRestore (`mc_bound=1`, in the GCLK red set) | the degrade DAG (`--set hle.input_service=off` / `hle.save_storage=off` → retained) is the only path that executes them; no green oracle | Bernoulli (Input/Save) | < 0.5 MB (the host store maps the memcard files on demand) |

### 1.1 Zero in BOTH arms with no oracle — deletable now

These are the sub-models whose counters are zero in play **and** in the deterministic oracle arm,
whose only tests are their own component tests, and which no `--set` degrade can reach (the
retained arm does not execute them either):

| ID | Sub-model | Evidence (both arms) | Why no `--set` resurrects it |
|---|---|---|---|
| D1 | **SPU2 register window on the IOP bus** (`SpuRegisterWindow`, `IopMemory::BusWindow`, census `iop_bus_window_hits`) | `iop_bus_window_hits=0` play and det | the window's only producer is a raw IOP load/store at `0x1F900000`/`0xBF900000`; the recompiled STREAM.IRX TU contains **no** such address (`grep -c 0xbf90\|0x1f90 iop_recompiled.cpp` = 0); the IOP libsd HLE module drives `SpuCore` directly |
| D2 | **syscall 119 EE→IOP SIF DMA copy** (`SyscallTable::SetSifDmaCopy/SifDmaBytes`, `sif_dma_clamped`) | `sif_dma_bytes=0/0/0` play and det (9476 retained RPCs moved 0 bytes by DMA) | the EE SDK's RPC path is HLE'd (`sceSifCallRpc`/`sceSifBindRpc` rows) onto the `SifRpcBridge`; file bytes return IOP→EE through sifman's sink, not syscall 119. **Deferred** to the L1 move: the model is ~40 lines, has its own component test (SceSifDma), and "zero in 1300 + 3000 frames" is not whole-game absence (HLE_COVERAGE's rule); it goes with the IOP arm it feeds |
| D3 | **libsd voice/block transfer** (`sd_block_trans.*`, `libsd_trans=`) | `libsd_trans=0/0/0` play and det (sound samples are the native `SoundBanks` owner's in both arms) | `--set hle.sound_samples=off` → retained → STREAM.IRX uploads banks through `sceSdVoiceTrans` → **reachable** in the play binary today. Deletable only with L1 (the parent's decision, §3) |

## 2. First deletions in this stage (Herschel's models)

- **D1 deleted** (commit `feat(iop): Delete the SPU2 register window on the IOP bus`): `engine/sound/spu_register_window.{h,cpp}`,
  `IopMemory::BusWindow` + the per-access window check in `iop_memory.inl`, `tests/sound/test_spu_register_window.cpp`
  (ctest `SpuRegisterWindow`), the composition in `ee_bootstrap_services.cpp`, census row `iop_bus_window_hits`.
  The IOP RAM access path loses a branch per access (the det arm's `iop_inframe_us` is the only consumer).
  Acceptance (evidence `int_work/m_series/stream/r7/`): Release 274 steps 0/0/0 (`build-release-d1.log`); component
  suite `ctest -E '^Root' -j8` **630/630** (`ctest-suite-d1.log`, skipset = allon_w1's); deterministic 1300-frame coldboot
  of the frozen main binary `allon_noexc/bin/nativeee_release.1bb648b5` (`det_ref/`) vs this build (`det_d1/`), same cwd
  and out layout: `diff_legs: IDENTICAL` — frames/audio/mem/drawir/gsops byte-identical by cmp
  (`diff_legs_det_ref_vs_d1.txt`); the det arm still boots the IOP (`iop_boot=1`) and the `iop_bus_window_hits` row is gone.
  s3 play replay peak_mb, both binaries back to back under the same machine state (load 4–8, two other game processes
  running — the integrate_w1 figure for the same frozen binary was 602 on a quieter machine, so the run-to-run noise is
  ≈ 30 MB): **before 632 → after 635** (`s3_before.log`, `s3_after_d1.log`) — the window held no memory, as the ledger
  predicted; `iop_boot=0 startup=native frame_waits=0 dropped=0 underruns=0` on both. RootServiceOnlyStartup ALL PASS
  (`root-service-only-startup-d1.log`).
- **D2, D3**: recorded, deferred to the L1–L4 move (below) — their reachability from the play binary is the same
  question as the IOP arm's.

## 3. L1–L4 (IOP kernel, modules, recompiled code, STREAM.IRX plugin, libsd model): the move, and the decision it needs

**DECIDED and DONE (parent decision 2026-10-11, TRACK 3b D2/D3; record `int_work/m_series/stream/TRACK3B_2.md`,
evidence `int_work/m_series/stream/r8/`).** The parent took neither (a) nor the Alternative as written: the oracles
that still boot the IOP are RE-HOMED to a second binary instead of changing the deterministic profile.

- **`ps2r_iop_models`** (`engine/iop/CMakeLists.txt`, INTERFACE): `ps2r_iop` (kernel + stock modules + the libsd
  model) + `ps2r_sif_iop` (new: `MainMemoryEeRamSink`, `EeRpcServer` over the `SifRpcBridge` — the SIF DMA model,
  moved out of `ps2r_sif`) + `ps2r_title_stream_irx` (the recompiled STREAM.IRX, plugin, LTI protocol, RPC server).
  Defines `PS2R_IOP_ORACLE=1`. Linked by `nativeee_oracle` and the component oracles only.
- **`nativeee` (PLAY)** = the same `app/host` sources with `PS2R_IOP_ORACLE=0` and NONE of the above on its link line
  (`ps2r_sif` no longer links `ps2r_iop`; `ps2r_cdvd` no longer links `ps2r_iop` — the `CdvdReader` interface moved to
  `engine/cdvd/cdvd_reader.h`). Proof: `tests/lint/lint_play_binary_no_iop.py` (ctest `LintPlayBinaryNoIop`) — `nm` +
  `c++filt` of the play binary: **0** symbols matching `ps2r::iop::` / `stream_irx::` / `iop_stubs::` / `EeRpcServer` /
  `MainMemoryEeRamSink` / `SifRpcBridge`; the oracle binary: 1346. Census row **`iop_linked=0`** (1 in the oracle).
- **`nativeee_oracle` (ORACLE)** = the same sources + `ps2r_iop_models`; the deterministic profile's `hle.startup=retained`
  pin, the 13-leg battery (`verify/battery.py --binary …/nativeee_oracle`), the frozen-anchor Root pins, the Release lane
  guard and every pin that asks for a retained arm (`PS2R_RETAINED_*_ARM`, `--set hle.stream_files=off`, …) run on it
  (`tests/app/CMakeLists.txt` header names the rule). The seam is `app/host/iop_arm.h` + `#if PS2R_IOP_ORACLE` in the
  composition root (`leg_state.h`, `ee_bootstrap_{services,play,image,sections,census,leg}.cpp`, `service_timeline`,
  `service_trace_log`, `subsystem_census`, `main.cpp`); every IOP census row keeps its never-booted value in the play binary.
- **The twelve degrade edges REFUSE in the play binary** (`ConfigLoader::SetRetainedStartupAvailable(false)` from
  `main.cpp`): `--set hle.{stream_files,files,sound_bank_headers,sound_samples,music,music_buffers,audio_mix,
  movie_lifecycle,movie_start,input_service,save_storage}=off` or `iop.dispatch≠frame` → `hle.startup=native requires
  <key>=on: the retained (IOP-booting) fallback is not compiled into this service-only binary (nativeee); turn the key
  back on, or run the oracle binary nativeee_oracle [degraded first: …]` (exit 2, no leg). An explicit
  `hle.startup=retained` refuses by name. The deterministic profile's retained pin becomes `native` with a `[config]`
  note (the profile stays usable for timing-deterministic legs on the play binary). The oracle binary keeps the degrade.
  Pin: `RootPlayBinaryServiceOnly`.
- **Residue, named:** the cdvd *command* model's EE-side pieces — `DeliveryMeter` / `LoaderModel` / `DeliveryBudget` /
  `StreamReader` — STAY in the play binary: they have live play callers (`loader_spin_lifts rpc=8771` — `LoaderSpinLift`
  holds a `LoaderModel&`; `cdvd_seeks=contig=254,…` — the meter owns the `ReadTiming` the owned `FileSchedule` shares;
  `cdvd_wait_parks=0/90`; the libcdvd sceCdSt rows over `StreamReader`). The IOP-side cdvd model (`CdvdmanModule`
  reads, `iop_cdvd=`) moved with `ps2r_iop`. Likewise the EE-side libsdr face (`SdBlockTrans` D3, `SdrRemote`,
  `LibsdrBindings`, the SDRX/SNDC savestate codec) stays: it is the title's SDK surface over `SpuCore`
  (`libsdr_calls=init=1`), not the IOP libsd model; D3's only IOP reach (`--set hle.sound_samples=off` → STREAM.IRX
  `sceSdVoiceTrans`) is now a refusal. Pad/MC (L10) are Bernoulli's and unchanged.

The original analysis and the proposal it answered follow for the record.

The deterministic profile **does** still boot the IOP for its oracles (`det/run.meta iop_boot=1`), and the
component oracles that compare NEW STREAM.IRX execution against the native owners (StreamFileSelfOracle,
AudioRampSelfOracle, LoaderCompletionModule, tests/iop/*) link `ps2r_title_stream_irx` / `ps2r_iop` as **test
executables already** — that half of "move it to a test-only library" is the status quo. The only production
linker of the IOP arm is `nativeee` itself, for two reasons that are the same decision:

1. **the deterministic profile pins `hle.startup=retained`** (`engine/core/config/deterministic_profile.cpp:49`) so
   that the acceptance lanes stayed identical across M11;
2. **the play degrade DAG** (`config_validator.cpp:68-79`): `--set hle.{stream_files,files,sound_bank_headers,
   sound_samples,music,music_buffers,audio_mix,movie_lifecycle,movie_start,input_service,save_storage}=off` or
   `iop.dispatch≠frame` degrades `hle.startup` to `retained`, i.e. the IOP arm is the ALL-ON charter's "one toggle
   off per run" fallback for twelve keys.

A binary that does not link the IOP cannot do either. The move is therefore gated on the parent's decision:

**Proposed (Herschel):** (a) the deterministic profile selects `hle.startup=native` — one attributed lane change
(the stream/audio timing of the oracle arm becomes the owned transport's; `diff_legs --allow-set-difference
hle.startup` names it; the REFERENCE advances once); (b) `nativeee` stops linking `ps2r_title_stream_irx`,
`ps2r_iop`, `engine/iop/hle/libsd`, `sd_block_trans`, the syscall-119 copy and `MainMemoryEeRamSink` (a CMake
option `PS2R_IOP_ORACLE`, default OFF; the app/host seam is `leg_state.h` + 9 TUs: `ee_bootstrap_{services,play,
image,leg,sections,census}.cpp`, `service_timeline.cpp` (IopClock), `service_trace_log.cpp`, `subsystem_census.cpp`
(thbase), plus `engine/sif/hle/ee_rpc_server.cpp` (SifRpcBridge), `engine/sif/hle/main_memory_ee_ram_sink.h`,
`engine/cdvd/read_engine.h` (the `ioman/cdvd_reader.h` interface header)); (c) the twelve degrade edges REPORT
"`hle.startup=retained` is not compiled into this binary; `<key>` stays on" (report, not refuse — the ALL-ON rule)
instead of booting the IOP; (d) the IOP-arm Root pins (RootRegistersIopmSection, RootRestoresStreamWorker,
RootRestoresStreamHeartbeat, RootAnchorHeartbeat*, RootIopmMcRestore — all already in the recorded GCLK red set —
and the green RootCdvdPollVisibility) are retired with the frozen retained-era anchors they load, their intent
re-pinned on the native arm where one exists (RootServiceOnlyStartup already pins `iop_boot=0`); the component
oracles keep the IOP under a `tests-only` CMake group. **Retired oracles under (a)+(d):** the det arm's
`libsd_calls`, `stream_rpc_calls`, `iop_hb_beats`, `cdvd_poll_deliveries` censuses stop being observable in the
game binary; they remain observable in StreamFileSelfOracle / AudioRampSelfOracle / LoaderCompletionModule.

**Alternative:** keep the IOP arm linked (it does not execute in play: `iop_boot=0`, census-proven), accept the
≈ 4–5 MB it holds, and keep the twelve-key fallback. This is the status quo; the "binary must not link them in
play" criterion is then not met and should be restated as "must not execute".

## 3b. L5–L9 (charter 2 wave 3, Herschel, 2026-10-12; record `int_work/m_series/stream/CHARTER2_C1_LIC_3B.md`)

Census source: the s3 play replay of the frozen all-on `nativeee_release.d67e84e0` (`charter2_c1_lic_3b/s3_before/leg/run.meta`,
3000 frames, play profile, `startup=native iop_linked=0`, `footprint peak_mb=425`). Rule applied: a model moves to the ORACLE
binary when its play census is ZERO and it has a native owner whose work is positive; a hardware path the title may still use
(no owner) or a path with live play callers STAYS, named with the live counter.

| # | Zero in play (quoted) | Still executing in play (quoted) | Moved to `nativeee_oracle` | Stays in `nativeee` — why |
|---|---|---|---|---|
| L5 | `sif_dma_bytes=0/0/0 dmac_sif_kicks=0 sif_reg_access=0/0` | `sif_rpc_calls=12407/0 sif_rows=18 sif_smflag=0x60000` (the sceSif rows over the owned StreamTransport) | the syscall-119 EE→IOP copy (3b-2: it writes IOP RAM), the IOP→EE sink + `EeRpcServer` (3b-2: `ps2r_sif_iop`), **the DMAC SIF0/SIF1 kick callback** (this stage: `#if PS2R_IOP_ORACLE`, the play binary binds nothing on channels 5/6; the row keeps `0`) | `SifState` + its MMIO window (`0x1000F200`): the EE kernel syscalls 121/122 (`sceSifSetReg/GetReg`) store their words through `memory::write` → this window, and the sceSif rows raise `SMFLAG` on it; `sif_reg_access=0/0` because the SDK rows are HLE'd, but the window is the kernel's storage, not an IOP model. Syscall 119's handler stays as the counting stub (`sif_dma_bytes` stays `0/0/0`; no copy function bound) |
| L6 | `cdvd_deliveries=0/0 cdvd_poll_deliveries=0 cdvd_boundary_deliveries=0 cdvd_wait_deliveries=0 cdvd_meter_pops=0/0 cdvd_forced=0 iop_cdvd=reads=0,sectors=0` | `cdvd_seeks=contig=254,short=32,long=22` (the meter's seek classification = the owned `FileSchedule`'s `ReadTiming`), `cdvd_wait_parks=0/97`, `loader_spin_lifts=…,rpc=8871` (`LoaderSpinLift` holds a `LoaderModel&`), `loader_polls=pending=0,done=3`, `cdvd_rows=25` (the live libcdvd rows, `sceCdReadClock:11`), `loader_ring=on,hits=5418` | the IOP-side cdvd model (`CdvdmanModule`, `iop_cdvd=`) — 3b-2 with `ps2r_iop` | `DeliveryMeter` / `LoaderModel` / `DeliveryBudget` / `StreamReader`: live callers above (3b-2's named residue, unchanged). Nothing new is at zero without a live caller on the same object |
| L7 | `ipu_commands=…all 0 ipu_unsupported=…0 ipu_unmodelled=0 ipu_operations=init=0,sync=0,stop=0,restart=0 libipu_calls=init=0,sync=0,stop=0,restart=0 ipu=divergent=0,unhandled_writes=0,dmac_face_oor=0` (owner: `movie_decoder=…pictures=547` / `fmv=…pictures=547,inits=1,retained_init_calls=0`) | — | **the whole IPU command/DMA model** (this stage): `ps2r_ipu_model` = `IpuDevice` (the `0x10002000` window, FIFOs, commands, intra decode: `ipu_vlc`, `ipu_color`, `mpeg_decode`), `DmacChannelFaceAdapter` (DMAC ch3/ch4), `LibipuApi`, the libipu SDK rows (`LibipuBindings`), `LibmpegBindings::RetainedInitialization`. Part of `ps2r_iop_models`. In the play binary the IPU window is the router's idle sink, DMAC ch3/ch4 have no device bound, `libipu_rows=0` (the four `libipu.a` title rows install as trap rows: `sdk_rows_trap` +4, never reached on the owned route), and the census rows keep their never-executed text | the live movie path (`MpegApi`, the sceMpeg rows, `FfmpegVideoDecoder`, `PssDemux`, pacer, audio capture, frame writer, adapters) = `ps2r_ipu` |
| L8 | `dma_spr=…all 0` (scratchpad DMA, ch8/9), `vif_mmio_gated=r0,w0`, `gif_parse path3=0/0`, `gs_sink image_qw=0`, `graphics_kick_lowering_worker raw=0,unsupported=0` | **`gs_sink=tags=4069391,regs=8874099,packed=205635042,kicks=102857121` `gif_parse_executed=1` (`path1=54/62448, path2=344860/34827232`) `dmac_tail_chases=3000`** | nothing | the parse still executes on PATH1/PATH2 (track 1c / M8 slice B, Zeno + Lagrange); `dma_spr` has NO replacement owner — it is EE hardware the title may use on a route the s3/s5/level sessions did not cover (HLE_COVERAGE DMA-2: zero in 3 play routes + the 39-run battery is not whole-game absence) — moving it would make a later SPR DMA kick fault in play with no owner to serve it |
| L9 | `gs_xfer=trxdir=0,completed=0,local_local=0,local_local_over_gpu=0,local_host=0` (owner: `gs_xfer owned=108481`, `texres_owned=binds=108481,…,serves=3327159,misses=0`) | **`gs_vram_epoch=216962` `gs_queue=…vram_ops=3436966,vram_op_bytes=5498105,split_image=1`** (the EE-side VRAM image and the consumer's split image are still written — the owned path's register-level ops, 5.5 MB over 3000 frames, down from 1.8 GB in w1) | nothing | the VRAM mirrors still execute; the `TransferEngine`'s HWREG path is at zero only because `hle.textures*` is on — the `--set hle.<texture key>=off` degrade is the graphics owners' and is not one of the twelve refused keys; moving the engine would mean refusing those keys in the play binary (a parent / graphics-owner decision, named here, not taken). The 3 × 4 MB images stay resident |

Proof and measurement for this stage are in the record: `LintPlayBinaryNoIop` extended with the L7 symbols (`nm` of the play
binary: 0; the oracle: > 0), the deterministic 1300-frame coldboot on BOTH binaries before/after (lanes identical), the s3 play
replay `peak_mb` before/after, the recorded GCLK red set unchanged.

## 4. RSS attribution — where the 602 MB is

Method: `footprint=` is the host physical footprint (`HostPhysFootprintBytes`, `ee_bootstrap_census.cpp:224`) at
each 60-frame boundary; the series is `footprint_series=` in run.meta. Static allocations are read from the
constructors; the deltas are read from the series.

| Component | Resident in play | Evidence |
|---|---|---|
| `b60` baseline (after boot, before the first level/FMV) | **170 MB** (det: 92 MB at `video.scale=1`, `mtvu=off`) | `footprint_series=b0:160,b60:170,…` (play) vs `b0:91,b60:92` (det) → **≈ 78 MB** is `video.scale=2` targets + the MTVU worker + the pipelined queue's standing buffers |
| EE main RAM | 32 MB | `ram` (main memory), zero-filled |
| Recompiled EE text | 68 MB `__TEXT` (`size nativeee`: 67,993,600) — paged in as executed; the 170 MB baseline includes the hot part | `libps2r_title_recomp.a` 170 MB on disk (32 shards, -O0) |
| GS VRAM images | 12 MB (EE-side `GsState::vram_`, the consumer's split image, `ReadbackEngine::scratch_`) | L9 |
| Pipelined GS queue in flight | the growth from 367 (b300) → 489 (b1620–b2220) tracks the first level: `gs_queue max_depth=12994` jobs, `payload_bytes=846 MB` cumulative; `graphics_owned_output pending`, `drawir jobs=90122` | `footprint_series` b1200:423 → b1620:488; `gs_queue=…max_depth=12994` |
| Texture residency (`texres`) | the L1/L2 caches: `lookups=674744,l1_hits=629235,l2_hits=45111,decodes=398,prep_bytes=1.53 GB,view_bytes=1.67 GB` (cumulative) | the standing cache size is not in the census — **add `texres resident_bytes=`** before TRACK 1d trims it |
| **Zero-census models (this ledger)** | **≈ 8–10 MB**: IOP RAM 2 + SPU RAM 2 + IPU < 1 + pad/mc < 0.5 + the IOP/libsd/SIF/cdvd statics < 0.5; (the VRAM 12 MB is NOT zero-census) | L1–L7, L10 |

So: the deletions in this ledger return under 2 % of the peak. The peak is `video.scale=2` (≈ 78 MB at b60
alone), the pipelined GS queue's in-flight payload and the texture caches — TRACK 1d (Zeno) and the GS queue's
depth bound (DP-25) are where "must drop materially" is decided. This ledger asks the parent to re-scope the 3b
peak_mb criterion accordingly: the retirement value of L1–L4 is build time (a 66,763-line -O0 TU), binary size
and the end of the two-arm startup, not RSS.

## 5. RSS method

`HostPhysFootprintBytes` is the process footprint (macOS `phys_footprint`): anonymous resident pages + compressed
+ IOKit, not file-backed text that was never touched. A zero-filled `std::vector<uint8_t>(2 MB)` is resident at
construction (the fill touches every page); `new uint8_t[n]()` likewise. Text of a function that never runs costs
nothing. Hence L2's 66,763 lines cost build time and 0.65 MB of file-backed text, not RSS.
