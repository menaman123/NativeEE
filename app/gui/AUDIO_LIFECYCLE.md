# Session audio lifecycle

The product pause handler stops the ticker and uses `SessionRuntime::OnMain`
to pause the actual SDL device on its owner. Single-step resumes it for the
one-boundary interval and pauses it at the following completed boundary.
Pause keeps the output ring intact; resuming does not rewind game audio.
The in-game pause menu and generated game callers are unchanged.

Restoring a reproduction must instead discard output from the old timeline.
`AudioPullRing::DiscardStopped()` requires both the game producer to have
joined and the SDL callback to have stopped (`SdlAudioSink::Close()` joins
that callback). It keeps monotonic process positions and counters, accounts
discarded frames, and cannot restore Audio/Music/mixer state. Root must
install the approved logical audio state before reopening the device and
relaunching its producer. The F8 root lifecycle is still pending.

OLD `E2_SAVE_REVIEW.md` records pre-teardown validation and stale state after
load; `U4_SAVESTATE_REVIEW.md` requires bounded admission and truthful queued
results. These require keeping device shutdown/discard out of preflight.
`PAUSE_IMPL.md` concerns the game's own pause-menu caller semantics, which
this product pause must not replace. No old implementation was copied.

`SessionAudio` uses the real SDL sink and output ring with SDL's dummy
device. It pins pause retaining samples without callbacks, bounded resume,
idempotent open preserving pause, callback join before discard, exact stale
sample removal, unchanged process counters, repeated teardown, resumed
sample identity, and invalid device format refusal. It neither executes the
game nor proves service playback, restore success, or model retirement.
