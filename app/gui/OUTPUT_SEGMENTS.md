# Restore output epochs

`VerifyHost::BeginOutputSegment(restored_frame, restored_tick, error)` is the
joined-supervisor operation for an accepted restore. It reserves a new
`output-NNNNNN` directory under the original harness output, finishes the
old partial audio and held-input tails, finalizes its metadata, then switches
to new frame/audio/memory/input files. Existing directories are skipped,
with a bounded 64-name search. Earlier files are never reused or truncated.

Each new directory carries the same build/configuration/renderer provenance,
its predecessor, restored frame/tick and process boundary. Its initial state
is `awaiting_resume`; only an actual present changes that to `active`.
Creating a segment is not evidence that state restoration succeeded.
No persistent game-save path is involved.

`Boundaries()` remains the process-lifetime count used by the frame cap.
`SegmentStartBoundary()` identifies the epoch's start. Memory hash keys and
audio windows restart at zero in the new files. The actual root calls
`PresentKey(boot_restore_frame, boundary)` to supply `OnPresent` keys.
The first present of a restore segment uses saved frame + 1; subsequent
keys use its local boundary offset. An older segment's boundary or key
overflow refuses rather than wrapping or appending old-timeline keys.
The historical CLI boot convention (first key 1, then anchor + boundary)
is preserved before any interactive output segment. A new input file accepts the restored logical tick,
even when it is earlier than the previous segment. `InputPath()` and
`LegDir()` now identify the current segment. Do not retain `Meta()` pointers
across this supervisor transition.

Root restart ordering:

1. Purely validate the retained image, selected owners, frame/tick grammar,
   continuations, pending-work contracts and recording policy.
2. Unwind and join old game workers and writers, then drain approved graphics
   work. SDL/main continues processing the restore deadline.
3. Begin the output segment once. Do not also call `BeginInputSegment`:
   output segmentation already changes the input writer.
4. Install clocks and owner state, reattach resources, reset input edges,
   then relaunch. Report the current `InputPath()` in the restore completion.
5. Any failure after step 2 closes the session. A filesystem failure after
   old tails were finished cannot resume the old epoch.

`OutputFailed()` and `OutputError()` retain any failure once old-tail
completion starts. Further frame/audio/memory/input writes and segment
requests refuse; `Finalize()` remains safe and does not retry half-written
tails. The retained metadata records `output_phase=failed` and its reason
when the destination is writable. Earlier reservation failures leave the
old recording usable. Neither failure reports a successful restore.

The actual root uses `SessionFiles::Select(host.LegDir(), host.OutputSegment())`
before its frame output, so graphics diagnostic handles and frame dumps
follow the selected epoch. Existing process-wide diagnostic hash values and
boundary counters are not reset. Initial output names/keys retain their
previous convention. New epochs exclusively create `gsops.ohash`,
`drawir.ohash` and, when dumps are configured, `output-NNNNNN` under the
original dump directory. A foreign file or directory refuses; earlier
artifacts are never reused. Output failure is terminal and the file owner
closes its handles on destruction.

The supervisor must also call `SessionFiles::Select` immediately after
successful `BeginOutputSegment`, before relaunch. That attributes filesystem
errors to the restore transaction instead of its first resumed frame.
The actual F8 hook remains pending; the existing frame-owner call is
idempotent. Root must still flush/reopen device audio on its owner and
reconstruct every selected provider.
This component does not turn on F8 or permit absent-owner resets.

`test_verify_units` exercises a real partial audio tail, a rewind of input,
unchanged previous files, preserved metadata provenance, a full reservation
collision budget, repeated sibling segments, frame-cap count continuity,
overflow refusal, finalized refusal and disarmed behavior. Existing frame,
audio and input recorder pins remain in the same test. ASan/UBSan evidence:
`int_work/m_wave/product-ui/output-segment-{build,tests}.log`.
An additional real filesystem-error pin replaces the preceding metadata
file with a directory, verifies transition refusal after the old tails,
checks all later output is rejected, and checks safe finalization preserves
the recorded prefixes (`output-failure-{build,tests}.log`).
`SessionFiles` pins actual diagnostic file bytes, stable path references,
idempotent frame selection, separate dump destinations, backward epoch
refusal and cleanup without removing a colliding foreign recording.
It does not execute any graphics model or game code.
