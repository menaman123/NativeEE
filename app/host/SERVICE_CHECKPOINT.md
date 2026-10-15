# Application service checkpoint owner

`SessionServices` is registered in the application when files, samples or
music are selected. It emits REQUIRED `SVCS` version 1: an explicit version
and file-only/composed-Audio selector followed by the corresponding owned
payload. Missing, duplicated, optional, differently selected or malformed
state is rejected. The original file format's 64 MiB per-section limit is
checked before a capture can be accepted; this stage does not enlarge it.

`SoundOutput::StateOwner()` exposes only its successfully installed Audio
owner. File-only snapshots reject Sound/Music jobs; the composed arm preserves
pending bank tickets, queued sample leases, music readiness and decoder state,
paired voices, buffers and settled service time. Capture and preflight neither
send nor pump work. Restore uses fresh runtime generations without new reads.

The isolated application now composes this owner with M0's clock through
`ServiceRestore`. Selected files and Audio can capture and load their own
complete new images. No legacy image is silently imported by this owner.
Active Movie state is explicitly refused: the complete Movie consumer
references and its reconstruction must join the pool before that condition
can be broadened.

## M0 integration contract

Depends on M0's committed `SessionClock` v2, including pending ticks
(`09931c9`), and original-image registry validation (`48a6594`). These
providers are consumed unchanged; see `SERVICE_CLOCK_DEPENDENCY.md`. Parent
should retain its single M0 clock and the broader product supervisor when
merging the following isolated root wiring.

1. Register `SessionClock::Section()` before `SessionServices::Section()`.
   Restore RAM first, then GCLK, then dependent services. SVCS and FILS stay
   REQUIRED of every capture (`capture_required`); the only absent callback
   is the explicit legacy import of item 6 (`ServiceImport`), composed by the
   root and reached only for an image it admits. No other legacy-owner
   exception is valid.
2. Construct `ServiceRestore(session_clock, service_checkpoint, file_services)`.
   Register its `RegisterValidation(registry)` for capture and owner
   reconstruction, and bind `StateRestorer::SetImageValidation` to
   `service_restore.ValidateImage(meta, original_blobs, clockless_import,
   error)`. This runs **before filtering, teardown or RAM writes**; registration
   in the section registry alone is insufficient for unselected optional
   service records. The join requires a validated GCLK tick equal to SVCS,
   validates FILS descriptor ownership, rejects incoming disabled owners, and
   joins the clock boundary count to file metadata.
3. SessionCapture and pre-restore now use
   `service_restore.CanCheckpoint(error)` instead of the broad selected-mode
   guards. Keep the independent Movie/graphics/thread conditions. The capture
   predicate is observational: it does not pump, send, charge, deliver or reset.
4. On the joined supervisor, release old active Movie views before services
   restore. The current owner accepts only incoming states with no Movie
   loans; a newly restored service state must not subsequently be reset.
   Existing `GameLibraries::ResetAfterRestore` currently resets Movie only;
   do not extend it to clear restored file/Audio ownership.
5. Stop and join tick producers before restoring GCLK. The root boot path
   stops TickThread and restarts it after reconstruction; the product
   supervisor must do the same for interactive restore. Re-anchor only before
   GCLK is restored. For an image with GCLK, reset the host mixer's accounted
   tick to the restored domain tick so no artificial historical PCM backlog
   is rendered. Preserve M0's stopped-output disposal and worker lifecycle.
6. Legacy selected restores are admitted ONLY through the explicit import
   `ServiceImport` (`app/host/service_import.{h,cpp}`), whose every value is
   derived from the image's own sections: files from the title's
   StreamSystem/FileStream objects in RAM0 (`StreamState::MigrateFiles` at
   the manager pointer `0x30a748`; open ordinary/archive handles and the
   staged ring-fill pending class, re-issued at the restore tick from the
   mounted medium); disc descriptors as the empty set with the medium's
   identity (the title holds them only in stack-local `File` objects for one
   load step, OLD persisted none; a saved THRD chain inside the synchronous
   family -- `Files::kStepFunctions` -- refuses "step in flight"); the clock
   as M0's clockless boot policy (the retained arm's restore of the same
   anchors). Admission = the frozen OLD schema (no GCLK/SVCS/FILS, no other
   new owner's section) AND the inventoried memory signature of
   `verify/restore_mhash_expected.tsv` (M0's GCLK policy). The import
   prepares in the preflight (no mutation; the medium is read there) and
   publishes at the two owners' absent positions; a refusal names what the
   image lacks (`service_import=` in run.meta: `last_refusal`). Nothing is
   inferred from an empty live queue and no OLD hardware record (IOPM, STRM,
   FILE) is consumed. The Audio-composed arm (sound_samples/music) has no
   derivable state and refuses by name -- its import is M5's.
   `clockless_import=true` retains the historical boot policy when
   asynchronous service ownership is off OR the import admits the image; it
   never exempts a NEW image's selected SVCS from GCLK. Interactive
   new-image preflight passes `false` (the import is a boot operation).

SVCS and FILS are emitted only for their selected owners; M0's GCLK belongs to
all new captures. Sound/control defaults remain off, and the
frame-212/audio-window-176 game regression remains unresolved. Original-image
validation cannot be bypassed by changing a service record to OPTIONAL.

## Pins and evidence

OLD records were read before implementation:

| Prior defect / source obligation | Replacement pin |
|---|---|
| `SOUND_P5_IMPL.md`: restored playback went silent; stale queued commands survived loads | Application registry restore resumes 25,600 audible stereo frames exactly, including queued/read/prepared/playing states; old generations are invalidated |
| `SOUND_P5_IMPL.md`: reject malformed data before replacing live sound | Missing selected owner, duplicate tag, optional flag, wrong Audio selection and mismatched game tick all reject with byte-identical recapture and unchanged read/delivery counts |
| `SOUND_P7_IMPL.md`: private ramp/voice progress must survive restore | Existing production sample/music/control state and common-Mixer checks remain in the same fixture |
| `SOUND_P8_IMPL.md`: stop/reopen completion reached a reused stream | Composed restore renews bank/music generations; restored reads and voices retain their exact joined identities |
| `LOADER_HLE_IMPL.md`: pending versus delivered-but-held work and stale completion | Application file-only restore retains the typed pending ring request and original deadline, delivers saved bytes once, and discards later-generation work |
| Both source Movie buffer consumers must own their ranges | Application capture refuses active loans while the component oracle validates and renews both complete references |

`session-services1.log` passes under ASan/UBSan for changed code with retained
dependency archives: 480 composed application-state checks, 64 loan checks,
the full file-owner fixture, 614,400 music frames and 614,400 shared-control
frames. The SFX oracle still uses both arms through the common production
Mixer. `root-session-services1.log` compiles the actual root and new owner
with production Release flags. Structural lint and settings checks pass.

These are owner and integration preparation checks. No worker game run,
root restore leg, zero-model phase acceptance or performance result is claimed.

The application join adds `ServiceRestore` (59 checks): actual StateRestorer
parse/preflight/RAM/GCLK/SVCS/FILS reconstruction over an owned pending read
and an open disc descriptor. Advancing into a discarded future before load
does not recharge the saved deadline or deliver its later bytes. The saved
file cursor, two pending clock ticks and original payload resume, with zero
LoaderModel enqueues and zero captured file bodies. Missing owners, optional
service state, duplicate tags, metadata/tick mismatch and disabled incoming
owners reject before the pre-restore callback, retaining live RAM, queues,
clock and cursor. Changes use ASan/UBSan, including the parser and registry;
retained dependency archives remain uninstrumented.

`service-restore2.log` also passes FilesSelfOracle (71,625 equal bytes,
6,757 checks) and FileServices (4,120 checks). The first isolated attempt mixed
an instrumented vector consumer with an uninstrumented parser; compiling the
parser under the same sanitizers corrected that harness inconsistency without
disabling container checking.
