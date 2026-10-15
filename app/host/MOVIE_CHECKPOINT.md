# Movie checkpoint owner

M2 is implementing, not accepted. This closes **inactive** Movie persistence;
active playback remains the named refusal allowed by PORT_PLAN. It does not
serialize partially decoded video, live PCM, picture jobs or buffer loans.

`MovieCheckpoint` owns `MOVI`, version 2, REQUIRED in every new capture. The
record contains installed decoder/file service selections, stopped decoder
identity, closed file-cursor ownership and software MPEG context dimensions,
authored rate and relative delivery deadline. With the selected PCM provider,
it also owns the player's quantized stereo gain (0–32767). It carries no process pointers,
callbacks, renderer resources, IPU registers or IOP addresses.

Version 2 uses decoder-service bit 128 for the owned PCM provider and the
previously reserved header word at byte 44 for its gain. The header remains
48 bytes; all fields are little endian. Version 1 remains readable only for
a composition without that provider, with the original reserved word zero.
Missing selected gain ownership is a provider mismatch, not permission to
invent state. Approved frozen OLD imports record the closed, muted gain zero.

The pacer persists its armed bit separately from the deadline: rebasing a late
closed movie at tick zero can legitimately produce a zero or negative deadline.
Close keeps that clock for the next movie. Initialize/legacy import clears it.
The serialized signed values and IEEE binary64 rate are explicitly little endian.

## Root and M0 contract

Prerequisites are Movie `189aaf6` and Bernoulli's `63e029d` then `48a6594`
(the movie worktree imports of the latter are `1d2ea67`/`af7c05a`; merge only
one copy). `capture_required` and named whole-image validation are required.

The prepared root patch:

1. Constructs the owner over the actual `GameLibraries.Movie()`, MovieStream,
   MpegApi and the same `mpeg_hooks.now_tick` used for picture pacing.
2. Registers after RAM and the logical clock. M0's GCLK owner must precede it
   when composed; Movie deadlines are relative to that restored clock.
3. Adds the same `movie_software` refusal to automatic and interactive capture,
   and checks live Movie ownership before restore teardown.
4. Removes the old unconditional Movie resets **after** `RestoreFile`. Those
   resets would erase the newly restored delivery grid and cursor.
5. Emits `movie_checkpoint=restored=N,legacy_migrated=N`.

New reproduction images must contain MOVI with matching installed selections.
Whole-image preflight reads the **incoming RAM0 blob**, not live memory, and
matches the stopped decoder and context ranges before any restore or reset.
Movie's direct restore also validates the applied RAM identity before replacing
the software table. The MPEG provider prepares allocations before swapping.
PCM gain reconstruction neither increments an executed-volume counter nor
adds a flush. Queued PCM or even an empty but open first-picture gate refuses
capture and restore before teardown; only source Close releases that owner.

`RegisterBoot` permits only a bounded historical import: the requested boot
filename must exist in the title's frozen `restore_mhash_expected.tsv`, incoming
RAM0+SPAD+META must match that entry, and every presented owner must belong to
the frozen schema. A new GCLK/TXAS/service owner with missing MOVI rejects.
This preserves the previously documented empty host-decoder/PCM import policy,
with a separate migration counter. Unknown missing Movie state rejects.
Interactive restart must require MOVI and use no historical import permission.
No missing historical file blocks implementing or testing this owner.

M0's joined-owner reset callback must **not** call
`GameLibraries.ResetAfterRestore()` or `MovieStream.ResetAfterRestore()` after
MOVI restore. Those remain explicit session teardown/legacy-import operations.
The root still has independent Graphics and live-service guards. `ServiceClock`
joins required SVCS to saved GCLK, while MOVI validates its own incoming RAM
and provider selections. No provider reset may erase their restored state.

M0 `4143524`, with the original-image validation API from `808a9a5`, provides
`StateRestorer::PrepareImage` / `RestorePrepared`. The Movie worktree's local
dependency import is `082bc91`; merge the original prerequisites once, not
that copy. M0 `96d2d05` moves the restore setters out of the header; the local
exact-file dependency import is `ee99e68`. Merge the original M0 stage once.
For interactive restart, `Sources.validate_image` stays pure.
Bind paired `prepare_image` / `discard_prepared` hooks to StateRestorer's
preparation/discard and bind `apply_image` to `RestorePrepared` over those
same bytes. The full Graphics allocation preflight runs before unwind.
Changed bytes, changed owner policy or duplicate preparation refuse before
teardown. Applying the preparation consumes it once and must not repeat
resource staging or reset the resulting Movie/Audio/Graphics state.

The root additionally calls `MovieCheckpoint::RegisterPreparation(restorer)`
after registering MOVI and GCLK. This allocates the incoming closed MPEG
contexts at the saved GCLK origin during `PrepareImage`, without publishing
them. MOVI apply consumes precisely that prepared record and context table;
it performs no second decoder-context allocation and no PCM operation.
An application with this registration refuses direct unprepared MOVI apply.
Component-only `Restore` remains available for isolated owner reconstruction.

`StateRestorer::AddImagePreflight(check, discard)` adds an optional paired,
nonthrowing release operation, preserving existing one-argument callers.
Releases run in reverse registration order after failed preparation, explicit
cancellation and completed/failed application. Movie uses the pair to return
unpublished contexts even when a later Graphics preflight refuses. An
immutable-image/policy mismatch keeps its preparation until explicit discard,
as before. This is an isolated shared-file patch over M0 `96d2d05`; preserve
M0's newer clock, ServiceClock and SessionRestore root wiring when merging.
Graphics owners may use the same pair for their staged images/resources;
that provider wiring remains with M7.

Clockless legacy boot migration is limited to an empty context table; normal
saved contexts require GCLK. The original-image classifier and MovieBoot
conversion run first. M0's interactive path requires the original complete
clock and Movie record and has no legacy absence permission.

## Buffer, audio and picture relationships

MovieStream refuses capture during Prepare, an open file, either outstanding
loan, deferred source reclaim, failed preparation or unresolved return. Both
loan closures must already have succeeded. Therefore M5's BufferState must
have **zero Movie loans** when accepting the same image; it must not renew an
unlisted active Movie token. Its ordinary music/file ownership remains M5 state.

MovieDecoder refuses from Start through Close, including refill EOF and terminal
decoder error. MpegApi separately refuses compressed input, demux/audio state,
queued pictures and frames served without Reset/Close. This prevents a quiet
instant before priming, or EOF with accepted backlog, from masquerading as a
complete save point. Close flushes the source's PCM as before; no muted-tail
metadata workaround is introduced.

M7's Movie/Graphics guard must also reject active presentation, failed output
and pending immutable image leases. Resource completion and reconstruction
belong to M7. A closed M2 record does not approve discarding pending render work.

## Pins and validation

Frozen OLD records were reread before replacement:

- `docs/loop_state/SOUND_P5_IMPL.md:35–134`: reject corrupt state before teardown,
  stale PCM cut at load, no save during a movie, first-picture gate, muted
  consumption, no EOF flush, skip/reopen flush.
- `docs/loop_state/E2_SAVE_REVIEW.md`: CRC-valid semantic/truncation defects must
  reject before destructive restore; a valid outer checksum is insufficient.
- `host_app/mpeg_hle.cpp:1908`: the software reset keeps the tick pacer while
  clearing pictures/audio. Its old wall-clock pacer is a separate policy.
- `docs/loop_state/FMV2_IMPL.md`: accepted backlog outlives file EOF.

Registered `MovieCheckpoint` composes the actual MemorySections, StateRestorer,
Movie owners and software context provider. It checks portable round trip,
new-origin deadlines, closed-cursor refusal/reopen, every truncation, semantic
corruption with valid envelope CRCs, incoming/live RAM disagreement, missing
selection, explicit legacy migration and active-owner refusal before teardown.
`MovieDecoderServices` now round-trips the real decoder **and file/PCM owners**
through this record: the next 24 picture/clock keys and actual PCM bytes match
continuous playback at origins 0, 100 and 10000. `MovieAudio` compares actual
retained and selected mixer output through mute, overflow, chunk boundaries,
abort and reopen, and pins the original voice→Movie→master/clamp order.
`MovieCheckpoint` tests version-1 compatibility, version-2 gain continuation
and rejection of unclosed PCM without a flush. Default three-binding tests
and the actual retained lifecycle oracle remain registered.

`MovieServiceCheckpoint` also composes the actual GSCN/GPRS owners over an
in-memory render backend and an explicit source-frame fixture. It stages a
held Movie image together with audible Audio, file state, GCLK and MOVI gain.
A real Graphics allocation failure leaves the live clock, gain and held pixels
unchanged before teardown. Successful prepared apply restores exact pixels,
audible Audio continuation and the Movie relative deadline without a new gain
intent or PCM flush. Applying prepared resources performs no second allocation.
This test exposed an unconditional flush in `MpegApi::RestoreIdle`; closed
software reconstruction now leaves the sink alone. Source Close/abort remains
the PCM cut, and the application still refuses queued PCM or an open gate.
The real-provider reopen test pins the same no-flush contract at three clock
origins. This is a persistence composition test; it does not
replace GPU raster, root F8, provider-clock play or phase acceptance.

Evidence: `int_work/m_series/movie/whole-m2-stage21/`, including isolated
ASan/UBSan logs and a production-flags syntax check of the root. One rerun had
four elapsed-time timeouts (including source lints); the unchanged-limit rerun
passed all four. No game/shared build was run. Full root restoration, same-binary
paired outputs, 13×3 stability and actual replaced-model zero counters remain
parent integration/acceptance work for this M2 owner to follow through.
The prepared Movie/Audio/Graphics join and no-flush correction are recorded
separately under `int_work/m_series/movie/whole-m2-stage32/`.
`whole-m2-stage33/` adds the real MPEG context-construction failure, balanced
cancellation, later-provider refusal, changed live PCM admission and reuse of
prepared contexts. The existing boot/decoder/oracle tests retain their scope.
