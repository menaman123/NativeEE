# M11 startup and remaining service consumers

This is a static source audit and M10 startup correction, not M11 acceptance.
The hardware-free bootstrap and obsolete dependency deletion are unfinished.
No game process or hardware-reference comparison was used.

## Preserve game initialization

TR `src/main.cpp:37` runs constructors, initializes system/media, parses
arguments, starts services, constructs stream/music/controllers, then starts
the game context. NTSC Main is `0x17ce50` (PAL `0x17d560`); InitDisk is
`0x17cd68` (PAL `0x17d478`). Both are high-confidence map entries, and their
actual generated calls were inspected. Main is not a replaceable startup shim.

TR `src/platform/ps2/system.cpp` requests eight drivers in this order:
SIO2MAN, PADMAN, MCMAN, MCSERV, LIBSD, SDRDRV, STREAM, CDVDSTM. Its restart image
is IOPRP300. NTSC Main loops eight pointers at `0x2e6e10` and calls the loader
at `0x2d4ac0`, return `0x17cf34`. The source-level platform helpers are a
specification of those intents; their names alone do not prove that each is
a separate NTSC function.

| Direct caller | Request | Current replacement/dependency |
|---|---|---|
| InitDisk `0x17cd68` | InitRpc, RebootIop, SyncIop, then InitRpc again | M11 must request selected service/media readiness while preserving remaining disk/game setup. Existing SIF success bodies are not proof of service readiness. |
| Main `0x17ce50` | Eight module loads | Only remove after every endpoint/heap/buffer consumer has an owner. Root still constructs and boots STREAM independently of the generated loader. |
| Main, return `0x17cf50` | Memory-card startup `0x2c5080` | Selected SaveBindings now calls actual host storage preparation. It does not create SDK semaphores, bind/call MCSERV, write fake client state or bypass Main. |
| Main, return `0x17cf58` | Pad startup `0x2c2428` | Selected InputBindings replaces this and Init2; future pad command-handler registration dies in that arm. Imported handler records still need explicit migration. |
| Main, return `0x17cf60` | InitIopHeap `0x2d4558` | Heap cutover still blocked by Movie buffer consumers below. Do not manufacture allocation success. |
| Main, return `0x17cf68` | Sound remote startup `0x2c4c30` | M5 owns the controls, effects, music and Movie handoff that must replace this libsdr client. |
| `0x2ae1c8`, `0x2ae860` | AllocIopHeap `0x2d45e0` | Movie disk buffer and audio ring ownership, M2/M5. Source: `movie/player.cpp:320,370`. |
| `0x2ae658`, `0x2af5d0` | FreeIopHeap `0x2d46c8` | Paired Movie close/release; the SDK wrapper calls FreeSysMemory `0x2d4650`. Source: `movie/player.cpp:338,696`. |
| Multistream `0x1e11e8`, `0x1e2958`, `0x1e7f80` | Bind/CallRpc and EE server registration/loop | M3–M5 ordered requests and completion own these intents. A missing direct call after installation does not dispose of saved callbacks. |

Read-only generated-body scanning found 75 direct call records for the
selected startup/heap/RPC target set. Evidence is
`int_work/m_wave/product-ui/startup-direct-callers.json` and
`startup-consumers-source.txt`. It includes retained SDK implementation
calls and is not a reachability or retirement census. SIF DMA/syscalls,
indirect registrations and saved continuation chains require their own
closure. Existing `hle/saves/LEGACY_AUDIT.md` records pad and MC callback
issuers and their exact registrations.

## M10 startup defect and pins

Removing MCSERV registration while leaving Main's direct mcInit intact
would leave its bind loop without a server. SaveBindings previously replaced
only the save-manager caller; it now owns the complete startup interface too.
`SaveStorage::Start` prepares configured host roots, returns failure for an
invalid backing path, preserves existing files and does not set region/product
or reset logical requests. The later game save-manager Initialise remains the
format owner. Restore does not invoke startup or create missing directories.

The component regression executes the actual installed startup body before
format initialization, verifies all configured roots, repeats startup without
altering progress, and proves an occupied-file backing path returns failure.
The existing SaveStorage file/store self-oracle is rerun. Parent must still
run actual cold boot, save-select/save-load, the selected zero-census checks
and frame/audio/13×3 gates before enabling it by default.

OLD records searched before this correction include E2_SAVE_IMPL/REVIEW,
U4/U5, BLACKBOOT_IMPL and memory-card references in the loop-state records.
Carry these risks:

- BUG-089: output pointers and four-slot setup must not corrupt the game
  registry. The service writes the explicit 12-byte StorageInfo output.
- E2_SAVE: old private service state or undelivered writes must not be
  silently reset or replayed after reproduction restore.
- U4/U5: queued requests and provider startup acknowledgments are not
  successful saves; actual failures remain observable.
- BLACKBOOT: a progressing counter can coexist with a black boot caused by
  file-admission starvation. Require positive startup/game work and frame
  output, not merely a clean exit or no unknown calls.

## Required owner composition

Services' proposed SVCS v1 joins file deadlines/payloads, Audio assets/leases,
queued controls, Music and buffer loans. Restore follows saved RAM/GCLK and
returns renewed Movie loans; it must not be followed by a blanket provider
reset. The implementation remains with the services owner.

The product worktree now imports selected SVCS/FILS clock joins, closed MOVI,
TXAS, shared scene/frame/presentation persistence and restored viewport state.
Graphics stages a fresh implementation behind stable runtime wrappers before
publication. These contracts close those selected reconstruction dependencies;
they do not retire unselected callers or allow active Movie ownership to be
discarded. Do not destroy an owner while installed bodies retain its references.

Services `50a39b1` explicitly retains shared send/reply/status, heartbeat,
startup, effects and remaining-clock observation. These are genuine M11
dependencies. Root STREAM boot therefore remains required, and F8 explicitly
refuses live rewind of that graph. No selector combination is sufficient to
prove all those source consumers closed. Parent-visible contract requests
remain in `int_work/m_wave/product-ui/WHOLE_PHASE_CONTRACT_REQUEST.md`.

M10 no longer installs the frontend readiness wrapper. The unchanged NEW
controller caller fixture verifies complete SDK setup, the game's own
sixty-tick reconnect delay and coherent post-restore behavior without a pad
module. See `hle/input/READINESS.md`; full Input/Save game acceptance remains
parent work, distinct from reproduction-checkpoint component acceptance.

The existing IOPM path explicitly preserves a later system-time clock and
reconciles against booted coroutine state. It is not a live rewind contract.
The root cannot claim F8 completion by applying those records to an already
running IOP runtime. Retire those consumers through service ownership; do not
add model clock or coroutine fidelity to make the interim graph rewind.
