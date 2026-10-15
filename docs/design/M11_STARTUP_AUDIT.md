# M11 startup audit — remaining IOP module / heap / RPC users (static)

Date: 2026-10-07. Source: `python3 tools/sif_startup_callers.py` over the generated NTSC
shards and `hle/sdk_bindings.tsv` (direct `recomp_call` edges only; thread entries,
vtables and SIF callbacks are not proven absent by this table). This is the
"audit all remaining module/heap/RPC users first" step of PORT_PLAN M11; it
authorizes no no-op, fabricated success or new IOP fidelity.

## Title-code roots that still reach the SIF SDK

| Title root (NTSC) | What it is (TR specification) | SDK entries reached | Owner / disposition |
|---|---|---|---|
| `0x100008 → 0x17cd68` | pre-reboot stage (`sceSifInitRpc`, `sceSifRebootIop` with `IOPRP300.IMG`, `sceSifSyncIop`, `sceSifCheckStatRpc`) — TR `src/platform/ps2/system.cpp` System startup | InitRpc, RebootIop, SyncIop, CheckStatRpc, AddCmdHandler, InitCmd, GetSreg | **M11 (Bernoulli)**: with every provider native this stage has no consumer; retire as an explicit startup ownership change in `0x17ce50`'s caller chain, not a stubbed success. Blocked until the four rows below have owners. |
| `0x17ce50` | main startup: `sceSifInitIopHeap`, eight `sceSifLoadModule` from the table `0x2e6e10` (return PC `0x17cf34`), `mcInit 0x2c5080`, `padInit 0x2c2428`, sound remote init `0x2c4c30`; then game constructors/settings/main loop | InitIopHeap, LoadModule, InitRpc, BindRpc, CallRpc, AddCmdHandler | **M10 owns** `mcInit`/`padInit` when selected (SaveStorage::Start / InputBindings). **M11** owns the load loop and `InitIopHeap`; `0x2c4c30` is the sound remote init (Herschel M5). The rest of the body stays. |
| `0x2ad560 → 0x1e11e8` | STREAM client bind (`sceSifInitRpc`, `sceSifBindRpc` server id `0x12345`, the custom `lti_streamer` server — TR `src/platform/ps2/stream.cpp`) | InitRpc, BindRpc | **Herschel M3** (StreamLibrary): native file intents stop binding STREAM. |
| `0x1e2958` (via `0x1e13b0 ← 0x1e1500 ← 0x1e0968 ← 0x1dd640/0x1dd860/0x1ddcf8`, `0x1e69a8 ← 0x2ad9a8 ← 0x2aac80`) | STREAM RPC call wrapper under the file-stream ring consumer and the loader (`0x2aac80` ring rule, `0x2a9040`/`0x2ab858` loaders) | CallRpc, CheckStatRpc | **Herschel M3/M4**: file completions through Scheduler; `hle.stream_files` arm. |
| `0x1e7f80` (no direct caller: a thread entry) | EE-side RPC **server** (`sceSifRegisterRpc`, `sceSifSetRpcQueue`, `sceSifRpcLoop`, `sceSifGetNextRequest`) — STREAM→EE callback server | RegisterRpc, SetRpcQueue, RpcLoop, GetNextRequest | **Herschel M3/M4**: the callback context that native completion delivers on the game owner instead. Its CreateThread site is the startup owner's to remove once no IOP callback remains. |
| `0x2ae318 → 0x2ae1c8 / 0x2ae860` | sound bank `.mb` residency into IOP memory (`sceSifAllocIopHeap`, `sceFsSifCallRpc` loads), `0x2ae658`/`0x2af5d0` free (`sceSifFreeSysMemory`) — TR `src/game/filestream.cpp:634` bank load / Audio | AllocIopHeap, FreeSysMemory, CallRpc | **Herschel M3/M5** (`hle.sound_bank_headers` on; `.mb` residency and voices pending). |
| `0x2ae018`, `0x2af458`, `0x2af688`, `0x2af6b0`, `0x2b4230`, `0x2ab2c8` | libsd/sdr remote calls (bank/voice/music controls) and file RPC | CallRpc, FsSifCallRpc | **Herschel M5**. |
| `0x2a2970 … 0x2a6b68`, `0x2a8668` | memory-card manager helpers under original Update `0x2a6b68` / Initialise `0x2a8668` | CallRpc | **M10**: unreachable when `hle.save_storage` selects the nine complete entries (LEGACY_AUDIT "Reachability"). |
| `0x2b2558`, `0x2b3858`, `0x2b3890` | pad UpdatePad/Destroy/Create | CallRpc (through libpad) | **M10**: served by the 17 InputBindings entries when `hle.input_service` is on. |
| `0x17d130`, `0x17d6b8`, `0x191c80`, `0x1cc000`, `0x1cc1b0`, `0x169348` | file-system RPC (`sceFsSifCallRpc`: open/read/lseek/close through `0x2d36b0…0x2d4238`), CD/DVD and misc service users | FsSifCallRpc, InitRpc, BindRpc, AddCmdHandler | **Herschel M3** (host file services already back `file_services.cpp`; these SDK bodies remain the retained arm until the parent flips `hle.files`). |

## Consequences for M11

1. Service-only startup = `0x17ce50` keeps constructors, settings, media paths and
   the game start order, while `InitIopHeap`, the eight-module load loop and the
   `0x17cd68` reboot stage are owned by the startup owner and executed **only when
   a retained consumer still needs them**. Today every row above except M10's has a
   retained consumer, so `irx_boot.entry_ran` stays true and live F8 refuses.
2. No universal "LoadModule succeeds" shim: each of the eight modules maps to an
   owner row above; `STREAM.IRX`/`CDVDSTM.IRX` stay executing until Herschel's
   M3/M4 cutover, `LIBSD.IRX`/`SDRDRV.IRX` until M5, `SIO2MAN/PADMAN/MCMAN/MCSERV`
   until the parent accepts the M10 selections.
3. The already-saved callback registrations (`_pad_intr_hdr` 0x80000019, libmc
   read/info/chdir appendices) need an import disposition for historical images;
   `SaveLegacyImport`/`InputLegacyImport` refuse pending work they cannot own.
4. Re-run `tools/sif_startup_callers.py --json` after each owner cutover; the M11
   gate is the table's title-root column emptying for every row except the
   startup owner itself, plus zero `padman_calls`/`mcserv_calls`/STREAM pumps on
   the 13×3 battery and the two play-session legs.

## The services owner's contract, as the root reads it (2026-10-08)

Herschel published the startup contract as `title::ServiceStartup`
(`hle/scheduler/service_startup.h`; specification table in
`hle/stream/INTENTS_SCOPE.md` §"STREAM / libsd / IOP-heap startup ownership
contract"): `Inspect(Current(root_inputs))` answers, per retained module
(`STREAM.IRX`, `CDVDSTM.IRX`, `LIBSD.IRX`, `SDRDRV.IRX`) and for the IOP heap,
whether a consumer still needs it under the composed selection, derived from
the INSTALLED owners (`StreamLibrary/SoundBanks/SoundControls/MusicLibrary/
MusicBuffers::Enabled()`) plus the root's own owners. This supersedes the
`OwnsStartup()`/`OwnsLibsdStartup()` signatures this owner had requested.

The root consumes it in `EeBootstrap::InstallDispatch`
(`app/host/ee_bootstrap_image.cpp`) through `ps2r::app::StartupOwnership`
(`app/host/startup_ownership.{h,cpp}`): the root fills `disc_files =
FileServices::Enabled()`, `output = SoundOutput::OutputOwner() != nullptr`,
`movie_pcm = MovieSound::StateOwner() != nullptr`, `movie_cursor =
MovieStream::OwnsCursor()`; `ServiceStartup::Current` fills the library
fields. Consequences today:

- `run.meta` carries `service_startup=stream=<owned|retained>,sound=<owned|
  retained>,retained=<names>,consumers=<n>` on every leg (next to `irx_boot`),
  so the parent's pairs show the startup state the selection composed.
- The live-restore refusal (`restore_lifecycle.inspect`) is the contract's
  named list — "Live restore requires service-only startup; retained:
  STREAM.IRX+CDVDSTM.IRX+LIBSD.IRX+SDRDRV.IRX+IOP heap. STREAM.IRX: file
  stream ring consumer and loader (0x2aac80 -> 0x1e2958; …); …" — instead of
  the generic sentence; F8 stays refused and visible.
- Nothing is skipped: with the default composition every module and the heap
  are retained (`M4 transport`, `M5 effects`, `M5 .mb residency` have no
  owner), `STREAM.IRX` boots and executes as before, `irx_boot.entry_ran`
  stays true. The load loop at `0x17ce50`/`InitIopHeap` gating and the
  `0x17cd68` pre-reboot retirement land when `StreamStartupOwned()` /
  `SoundStartupOwned()` first become true (Herschel's order of release: M4
  transport → M5 effects → M5 residency), each as one `Inputs` field flipping
  in the root; `StartupOwnership` pins the three partial states.

Full output with chains: `int_work/m_series/input_saves/bernoulli/sif_startup_callers.{md,json}`.

## Service-only startup landed (Herschel 3a, 2026-10-11)

`hle.startup=native` (compiled all-on default; the deterministic profile pins
`retained`). The root (`app/host/native_startup.{h,cpp}`, composed in
`ee_bootstrap_services/image/play/census.cpp`) never boots the IOP: the eight
`sceSifLoadModule` of `0x17ce50` are answered by the owner's handle
(`SIO2MAN/PADMAN -> InputBindings, MCMAN/MCSERV -> SaveStorage, LIBSD ->
Audio+SoundEffects, SDRDRV -> SdrRemote+SoundBanks, STREAM -> StreamTransport,
CDVDSTM -> FileServices+MovieStream`; `startup_loads=8/0`), `InitIopHeap`
stays a no-op and `AllocIopHeap` refuses and counts (`iop_heap_native=
refused=0`), the STREAM server 0x12345 is `title::StreamTransport`
(`hle/stream/TRANSPORT_SCOPE.md`), effects/global controls are
`title::SoundEffects`, the `0x17cd68` pre-reboot stage keeps its accepted
`sceSifRebootIop`/`sceSifSyncIop` answers (nothing to reboot), and the
`0x1e7f80` EE RPC server thread is created and sleeps in `sceSifRpcLoop` as
before (no fast-load call ever reaches it: the owned file service delivers
into EE memory itself). Census: `iop_boot=0 iop_modules= iop_hb_beats=0
stream_rpc_calls=0/0 libsd_calls= service_startup=stream=native,sound=native,
retained=none,consumers=0`. The M11 gate of consequence 4 holds on the Root
pin `RootServiceOnlyStartup`: zero STREAM pumps, zero padman/mcserv calls, the
title to its menu with music and SFX. M12 (Bernoulli) may now delete the IOP
kernel, the recompiled STREAM.IRX, the libsd/sdr IOP modules and the IOP RAM
from the play composition, keeping them under `hle.startup=retained` for the
deterministic oracles only.
