# Installed wrapper compatibility audit

Audit: 2026-10-07. Product proof code at `c0d2224`; current source adapters
read from the integration worktree at `c8e70c3`. Per-file SHA-256 evidence is
in `int_work/m_wave/product-ui/wrapper-source-audit.tsv`. No adapter, root,
shared CMake or generated body is changed by this audit.

## Finding and acceptance

The compatibility risk is real in source. `ObserveInstall` cannot recover
metadata from an arbitrary lambda, even when that lambda calls its captured
original unchanged. `InvokeAt` now checks that proof, so a formerly reachable
interior entry can fail after an otherwise behavior-preserving installation.
An OFF selection is insufficient: several adapters still install counting
wrappers. This is a potential historical-restore regression, not evidence
that the old continuation was invalid. No failing title leg has been observed
by this worker; the parent owns those checks.

Do not weaken the exact-entry guard, copy proof onto every wrapper, or use
`TraceBody` as a general exemption. A captured body may itself already be
opaque. Every layer between the installed callable and the generated body
must preserve the particular entry being resumed.

The existing restore/root legs remain acceptance. If an approved leg fails,
retain its thread, logical level, function, entry and active adapter selection.
Then prove that installed source path, pin its preserved outputs, and repeat
that leg. Fresh-capture refusal and successful metadata tests do not close
that regression. Check the complete chain before old-owner teardown; a late
`InvokeAt` failure otherwise reaches `resume_no_function` in the relauncher.

## Source-backed candidates

Paths below are relative to the integration repository. These are audited
control-flow candidates, not installed continuation permissions.

| Source | Interior behavior | Proof boundary |
|---|---|---|
| `engine/ee/hle/sdk_binding_manifest.cpp`, `Kind::Recompiled` | Forwards the same context and entry once; its hit counter changes only at the full address. | Preserve exact captured-body support, never infer support from the manifest name. |
| Same file, `Kind::SdkWrap` | Non-address entries bypass the handler and PC write, then delegate if the original exists. | Interior-only proof; replacement entry and wait replay remain separate. Zero has different routing from the full address in this wrapper. |
| `games/<title>/hle/math/{math_library,collision_math,trig_math,animation_math}.cpp`, `Install` | Interior route delegates through a captured-body counter; OFF and inactive/rollback paths also run that counter. | Audit both the inner counter callable and outer selection callable. MathLibrary's point transform stays replaced even with helper selection OFF. Retain lifetime/rollback guarantees. |
| `games/<title>/hle/fmv/movie_decoder.cpp`, lifecycle/audio/stream/input install methods | Nonzero, non-address entries increment continuation counts and delegate. Audio/stream/input originals have an additional `CountOriginal` layer. | Interior-only; counter layers must retain identity. Movie ownership/quiescence/reset still apply and are not implied by dispatch proof. |
| `games/<title>/hle/stream/stream_library.cpp`, `Install` | Service replacements run only at entry; interior route increments the fallback counter and delegates, even when disabled. | Interior-only across the original's complete wrapper stack; no inference about entry replay. |
| `games/<title>/hle/game_libraries.cpp`, sound-bank binding | Interior branch delegates directly; full entry invokes the reader. | Interior-only; preserve generated stack-frame completion. |
| `games/<title>/hle/graphics/graphics_service.cpp`, Strip/Link | Valid, nonfaulted interior route increments counters and delegates. | Original may already be a FrameEnvironment wrapper, which has additional state effects. Inspect composition and quiescence. |
| `games/<title>/hle/graphics/sky_service.cpp`, `Run` | Valid, nonfaulted interior route counts and delegates; replacement is full-entry only. | Inspect the captured FrameEnvironment/SkyPass layers too. |

## Cases that are not transparent delegation

| Source | Why proof cannot be inherited |
|---|---|
| `sdk_binding_manifest.cpp`, `Kind::SdkBind` / trap | Ignores the requested continuation and invokes a replacement handler or terminal trap. |
| `graphics/frame_environment.cpp`, `Run` | Interior entry invalidates rendering evidence/state before and after delegation, with active-operation bookkeeping. It needs an explicit state-owner contract. |
| `graphics/sky_pass.cpp`, `Run` | Active passes can reject interior producers; exception handling can change fault state. Full-entry destruction also forgets assets. |
| `graphics/texture_bindings.cpp`, installed lambda | Explicitly throws on an interior entry. |
| `fmv/movie_stream.cpp`, installed lambda (stage8 addition) | Selected interior fallback first resets cursor/session ownership, then counts and delegates. OFF still installs the counting layer. These two selections need different source contracts. |
| `workarounds/fmv_abort_hold.cpp`, `Hook` | Calls `Tick` for every entry; it can clear a guest movie abort flag before delegation. |
| `pad_readiness_hook.cpp`, installed lambda | Calls `Observe` for every entry; it can issue a readiness event. |
| `loader_sync/loader_spin_lift.cpp`, installed lambda | Can consume delivery state and short-circuit before delegation, without an entry guard. |
| `workarounds/free_list_0x181560.cpp`, allocation/free wrappers | Can allocate/free or publish a return at any requested entry. |

The shortened game-source paths in this table are under
`games/<title>/hle/`. The two byte-hashed diagnostic
cases already in `continuation_audits.json` cover only their exact generated
wrapper/prologue text; they grant no permission to these installed adapters.

## Dispatch-owned primitives; source adoption remains gated

The following primitives are implemented and component-tested. No SDK or
title adapter adopts them in this slice. Parent reports both initial historical
root checks passing and a same-binary movie pair with zero boundary-return
rejections; no failing historical chain has been supplied.

```cpp
DispatchTable::Body DispatchTable::DelegateInterior(
    uint32_t fn, Body original, Body entry_body);

enum class DispatchTable::CountMode { EveryCall, FunctionAddress };
DispatchTable::Body DispatchTable::CountCalls(
    uint32_t fn, Body original,
    std::shared_ptr<std::atomic<uint64_t>> counter,
    CountMode mode = CountMode::EveryCall,
    std::shared_ptr<const std::atomic<bool>> enabled = {});
```

`DelegateInterior` constructs the callable that structurally routes interior
entries to its proven original and routes only full entry to a supplied
replacement. This avoids attaching metadata to an arbitrary supplied wrapper.
Zero is routed to the supplied entry body unchanged. `CanResumeAt` accepts
only the original's exact interior entries; zero/function entry and safe wait
restart are not granted. Ordinary `Invoke` still executes the replacement.
Capture remains refused while that unrepresented full-entry frame is active,
even if its fallback calls the original.

`CountCalls` updates only a reviewed diagnostic atomic and delegates once
with the same context/entry. It accepts no arbitrary observer callback.
An aliasing shared pointer keeps an array cell and its owner alive. The
optional owned enable flag preserves inactive-install/rollback accounting.
`FunctionAddress` counts `entry == fn`, not zero, matching SDK accounting.
It preserves exact entry support and any interior-only restriction, but
conservatively drops wait-restart proof. Plain MovieDecoder counters need a
separate lifetime/storage adaptation by their source owner before adoption.

Neither primitive recovers proof from an opaque captured body or one belonging
to a different function. Unknown outer installation revokes proof; an exact
captured original can restore it. Generated tracing preserves the restriction
instead of widening it. Missing original/entry body/counter returns an empty
callable, not a usable adapter.

Pins cover interior caller-tail outputs, no replay of entry side
effects, exact non-entry rejection, nested original/wrapper identity,
rollback/destruction, and revocation on replacement. Dynamic omission tracking
refuses capture while an unrepresented replacement frame is active.
A wrapper may be admissible for historical interior restore while unsafe to
capture during a full-entry replacement. All three generated wait-thunk types
stay unproven through these adapters. The actual ThreadRelauncher/provider
fixture now installs the counted interior adapter after saving, restores the
real thread image, finishes the saved caller tail, and recaptures its full
deferred chain without entering the replacement.

Adoption still requires a concrete failing leg and review of its source
adapter's counters, full-entry behavior, wrapper composition and state owners.
These primitives do not authorize bypassing the nontransparent cases above.

OLD evidence retained: `E2_SAVE_IMPL.md` 220-248 requires the innermost
post-sync arena-flip tail, 355-405 requires owner-local/complete chains,
418-431 documents the menu chain; `E2_SAVE_REVIEW.md` treats restore-chain
behavior separately from schema validation. Local BUG-028 preserves caller
and delay-slot semantics, BUG-036 incarnation ownership, BUG-085 the complete
state-owner inventory. No OLD behavior is made the implementation target.
