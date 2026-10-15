# IOP shortcut completion audit

Date: 2026-10-06. Scope: `engine/iop/**`, `tests/iop/**`, and this report.
No title/generated source, app, EE, DMAC, IPU, shared CMake, or unrelated dirty
files were edited by this work. No commit, game run, shared build, or full
ctest run was performed. The parent owns integration and live validation.
`docs/phases/STAGE_RULES.md` and `docs/REFERENCE_FREEZE.md` were read.
DT-8 retires OLD-memory identity; old dispatch order is not a reason to keep
an imported function ineffective.

Integration decision, 2026-10-06: **defer the additional clock-conversion
changes**. The parent owns restoring only those source/test edits and running
full validation. Retain the priority, lifecycle, cooperative delay, semaphore,
intrman, observer-counter, and IOPM fixes below. In particular, the retained
`DelayThread` minimum and removal of its upper clamp are separate from the
deferred `USec2SysClock` / `TimrmanModule::PeriodUsec` changes. The clock-only
control experiment and actual Sony ABI investigation are recorded below;
neither timer completion nor a fully passing integration suite is claimed.

**Current direction (DT-9, user, 2026-10-07):** this is a historical implementation
and limitations record. Further kernel, module, interrupt or timer fidelity
work is superseded by native Stream/Audio services using the decomp's RPC
meanings. Retire STREAM.IRX and IOP scheduling paths as those services take
ownership; track this in [HLE_COVERAGE.md](HLE_COVERAGE.md#scaffolding-retirement).

## Parent integration API

`engine/iop/hle/thbase/thbase_module.h` exposes:

```cpp
struct Counters {
    uint64_t priority_calls;
    uint64_t priority_changes;
    uint64_t priority_errors;
    uint64_t delay_kernel;
    uint64_t delay_host;
    uint64_t injected_wake_errors;
    uint64_t injected_signal_errors;
};
Counters Count() const;
```

Use the parent-owned `StreamIrxPlugin::Thbase()` accessor. These counters are
atomic, observer-only lifetime totals and excluded from savestates.
`priority_changes` counts successful numeric priority changes; an effective
same-priority queue rotation increments only `priority_calls`. `Count()` loads
individual atomics without taking the dispatcher lock; fields can straddle
an in-progress call, so this is not a transactional kernel-state capture.
Host delay calls are counted even though they now return `KE_ILLEGAL_CONTEXT`.
Any positive `delay_host` requires investigation; rejection is not an
implementation of a host continuation.
The two `injected_*_errors` fields read the kernel's atomic delivery counters,
so the parent can reject live coverage with an invalid queued request.

Additional race-free counters for optional census wiring:

- `IopKernel::InjectedWakeErrors()` and `InjectedSignalErrors()`: errors when
  queued external-host requests are actually delivered.
- `ThsemapModule::HostWaitCount()` and `ErrorCount()`: host polling and returned
  errors; asynchronous delivery failures belong to the kernel counters above.
- `IntrmanModule::SuspendCount()`, `ResumeCount()`, `NestedSuspendCount()`.

Host wake/signal return 0 means **queued**, not a completed operation. They
never wait for the dispatch mutex: `Dispatch()` can remain active in a
yielding poll loop until another host delivers such a request. Guest calls
return their actual result. Queued requests validate registration/state/count
when applied. Overflow is a legitimate SDK error, not itself proof of a
missing subsystem; interpret delivery errors against the caller.

## Reachability evidence

The installed generated `iop_recompiled.cpp` and `INVOCATION.md` were read
without modification. Import presence and a direct call path prove potential
reachability, not execution on every title leg.

| Import | Generated call sites | Evidence |
|---|---:|---|
| thbase 14 `ChangeThreadPriority` | 1 | Function `0x11144`, JAL `0x1115c`, generated line 60482. Reads the target tid from module RAM `0x12f14`, forwards the supplied priority when nonzero tid exists. Main RPC handler `0x8d28` calls this wrapper at `0x9ac4`, line 34304. |
| thbase 33 `DelayThread` | 6 | JALs `0x8bfc` (40 us), `0x8c8c` (20 us), `0xd4c0` and `0xd544` (20 us), `0x101e0` (400 us), `0x11694` (80 us). Generated lines 29881, 30003, 46942, 47071, 57102, 61594. |
| intrman 17/18 suspend/resume | 30 / 31 | Imports at `0x12830` / `0x12838`; calls in libsd/stream critical sections. Branches account for unequal static counts; this is not evidence of an unbalanced runtime path. |
| thbase create/start/delete/terminate | 2 / 2 / 1 / 1 | Imports 4/6/5/10; ordinary module and streaming-worker lifecycle. |
| thbase sleep/wakeup | 1 / 1 | Imports 24/25; cooperative module thread control. |
| thsemap create/delete/signal/iSignal/wait | 1 / 1 / 3 / 3 / 1 | Exactly the five installed thsemap imports 4 through 8. Worker wait/handoff and timer/completion notifications are in the generated artifact. |
| timrman setup | 1 | `0x12c0` helper, JAL `0x132c`, line 3957. Main RPC handler reaches it through `0x29c`. Still an abstraction, described below. |
| thbase `USec2SysClock` | 1 | Generated line 3878, timer setup helper. Reachable conversion discrepancy investigated; correction deferred after the controlled live experiment below. |

Direct calls from the main RPC handler reach the delay helpers `0x8bf4`,
`0x8c14`, and `0x11604`. The module entry `0x13c` has no direct function-call
path to a delay; it creates/starts a guest thread. This does **not** establish
that host delay is unreachable through every indirect callback or future glue
path. The `delay_host` census is the required live check.

All six static delay arguments are at most 400 us. The former 100 ms upper cap
does not affect these constant arguments. It was removed as an unjustified
kernel restriction and is covered by a 250 ms regression. The SDK's 100 us
alarm minimum does affect the 20/40/80 us calls; the change can legitimately
alter frame timing and must be covered by the parent's live regressions.

Timer call-site follow-up:
the `0x12c0` setup helper calls `USec2SysClock(0x411a)` = 16666 us, then
`AllocHardTimer(1, 32, 1)`, `SetTimerHandler(id, converted_ticks, 0x136c, arg)`,
and `SetupHardTimer(id, 1, 0, 1)`. The generated assignments are at
`0x12cc`, `0x12e0`–`0x12ec`, and `0x1324`–`0x1330`.
PS2SDK's timrman header identifies source 1 as the 36.864 MHz system clock;
the title's prescale is 1 and width is 32. The periodic callback model reaches
this setup, but does not establish exact SDK timer or scheduling behavior.

The read-only `<projects>/the title-reversed` corroborates
normal use of the IOP streaming service: `src/platform/ps2/multistream/transport.cpp`
sets the IOP thread-priority parameter to 40; `commands.cpp::MsInitStreamData`
serializes that parameter into the stream-init command. Its `multistream.h`
documents the RPC protocol and thread-priority field. This is corroborating EE
client evidence, not a claim that every command-switch branch is exercised.
The generated IOP call sites above are the actual runtime artifact.

## Deferred clock correction and causal experiment

The retained compatibility behavior is `uint64(usec) * 37`, followed by
floor conversion from timer ticks to microseconds with the existing
1 ms minimum and 100 ms maximum. The title argument produces 616642 ticks
and a modeled 16727 us period, 61 us longer than its requested 16666 us.
This is an explicit deferred approximation, not SDK-exact conversion.

The candidate change used `floor(uint64(usec) * 36864000 / 1000000)`,
equivalent to `floor(uint64(usec) * 4608 / 125)`. It produced 614375 ticks
for the title argument, about 16665.988 us. Its companion `PeriodUsec`
change rounded deadlines up to whole microseconds and removed the 100 ms
timer-period cap, yielding a 16666 us heartbeat. A saved legacy compare
of 616642 ticks also changed from a 16727 us period to 16728 us.
All these additional clock source/test changes are deferred together.

The parent authorized an isolated Release diagnostic binary built from
temporary copies of only the thbase/timrman sources. Those copies restored
x37 conversion and the former floor/min/max period calculation; all other
current IOP, IPU, and DMAC fixes remained. The existing Release link command
was reused with override objects before the IOP archive. Link-map and hash
checks confirmed the overrides and unchanged shared sources, objects, and
original host. No gameplay was launched by this worker.

The parent ran the controlled live comparisons:

| Observation | Clock candidate | Clock-only legacy control |
|---|---|---|
| Boundary 60 memory | Matches prechange baseline | Matches prechange baseline |
| Boundary 120 memory hash | `0x460fc7fc90bf4cb1` | `0x96dacb45cd7e529d`, exact prechange baseline |
| `RootTitleScreenEffects`, judged frame 2519 | FAIL; valid in-engine cutscene instead of the expected title phase | PASS |

The parent's before/candidate memory probe found 42 changed words at
boundary 120, attributed to timing/counters, including `0x323534` increasing
by 300 and RPC status counts differing by one. Their first 121 frame hashes
matched. The clock-only control
restoring the exact memory baseline isolates the clock changes as the cause
of that delta. Its title-screen PASS also isolates the fixed-frame gate
regression to the clock changes. These experiments restore both clock
functions together: they do not isolate each arithmetic edit individually,
prove the exact title transition boundary, or adjudicate every full-suite
failure. They do not implicate the retained IPU or core IOP fixes.

Artifacts are under
`<work>/int_work/subsystem_completion/`:
`memory-proof.json`, `probe-legacy-clock.log`, `title-legacy-clock.log`,
`build_legacy_clock.py`, and `legacy-clock-vjpb_4qd/provenance.json`.
The diagnostic binary `nativeee-legacy-clock` has SHA256
`5b9956dc2bf94577e85a65f231fe6cd6678bed29ef714ec7017aead5192fc0d3`.
It reuses the Release build-ID object, so path and SHA256 identify the control.

The integration decision preserves the existing gates while the surrounding
IOP-clock/frame-budget/loading interaction remains unresolved. No fixture
was changed by this work. Arithmetic evidence alone does not authorize moving
frozen title checks. Closure requires the selected target clock mode,
deadline/restore behavior, and loader/RPC scheduling effects to be established
and validated on the parent's live leg matrix.

## Actual Sony THBASE ABI evidence

Read-only inspection used the title's
`OLD/game_files/example_title.rep/DATA/SYS/IOPRP300.IMG`, size 275345,
SHA256 `02d314464c17c715d7e809297b0aba184173a98616b1fafeac27fe33fc480c82`.
ROMDIR locates `THREADMAN` at image offset `0x84a0`, length `0x99ed`;
its SHA256 is
`d9b5c8c30173338554e637a269680c4d13c8972f6406666e386ad524ca8593d6`.
The retail EE ELF contains `IOPRP300.IMG`; the reversed title source also
constructs this image path for `SifIopReboot`.

All following addresses are module-relative before relocation. The actual
`thbase` v1.2 export table at `0x67b0` maps export 39 to `0x5d44`.
Initialization at `0x5840` sets multiplier 4608 and divisor 125. Export 39
uses unsigned multiplication/division and drops the remainder. Thus the
candidate's normal-mode conversion matches the target Sony binary's default
ABI, independently of open PS2SDK.

There is a conditional alternative: `QueryBootMode(7)` (the image's
`loadcore[12]` import) returning a non-null record with low 16 bits `0x00c8`
selects multiplier 25 and divisor 1. Neither branch uses x37. LOADCORE
export 12 at `0x6e0` reads a registered boot-mode table through IOP RAM
`0x3f0`; export 13 at `0x684` appends records. Static inspection did not
capture a physical console's selected entry or all ROM registrants.
The default arithmetic is verified; the actual supplied boot-mode value
is not. The compatibility deferral does not assert x37 is Sony's ABI.

Export 33 (`DelayThread`, `0x2bcc`) calls export 39 and then export 35
(`SetAlarm`, `0x5f38`). Initialization converts 100 us; SetAlarm clamps
short intervals to that value, 3686 ticks in normal mode (about 99.989 us).
This supports the retained whole-microsecond HLE minimum of 100 us.
There is no 100 ms upper delay clamp in this path.

The minimum interval is not an exact delivery guarantee. The timer-compare
programming path at `0x5cc4` through `0x5d2c` also uses a guard of twice
that minimum, 7372 ticks (about 199.978 us), and can postpone delivery.
The cooperative HLE does not implement this physical alarm policy.
Delay tests establish modeled ordering and deadlines, not exact Sony
wake timing. This remains a documented abstraction; implementation scope
was not expanded.

Reproduction helper and detailed evidence are in the same external work
directory: `inspect_target_iop.py`, `target-thbase-spec.md`,
`target-iop-spec.log`, `target-iop-clock-detail.log`, and
`target-bootmode-origin.log`. The static investigation copied no firmware
code into the runtime.

## Implemented behavior

- `ChangeThreadPriority` now changes effective priority. Ready targets enter
  the tail of their new priority group, including a same-priority request.
  A promoted ready peer runs before the caller continues. A caller lowering
  itself to a ready peer's priority goes behind equal peers. Ordinary
  preemption preserves the caller's position ahead of its equal peers.
- Thread id 0 resolves to the running guest for that syscall. Priority 0
  borrows the caller's current priority. Unknown/dormant targets and invalid
  priorities return the corresponding IOP errors. A host has neither a self
  thread nor a priority to borrow.
- Priority changes preserve sleeping/delay waits and their deadlines.
  Semaphore priority queues reinsert changed waiters after equal priorities;
  FIFO queues keep arrival order. Restored token reconciliation also respects
  the semaphore's live queue policy.
- `DelayThread` parks on the kernel clock for at least 100 us, without the old
  100 ms cap. Host/callback contexts without a running guest return
  `KE_ILLEGAL_CONTEXT`; no host sleep or implicit clock advance is introduced.
  `SleepThread` likewise rejects a host context rather than claiming success.
- Thread create/start/delete/terminate/wake now reject the covered invalid
  argument/state cases. Unresolved native entries no longer report successful
  start. Deleted thread slots can be reused. Restart discards the terminated
  continuation and restores creation priority. The callable stays owned by
  the thread record so destroying a parked stack does not leak that callable.
- `OnKernelThread()` checks the calling host's dispatcher ownership. A foreign
  EE host cannot mistake an IOP coroutine running elsewhere for its own stack.
- Imported semaphore operations honor FIFO/priority attributes, reject unknown
  IDs and overflow, and return `KE_WAIT_DELETE` to deleted waiters even if a
  new semaphore reuses the slot before they resume. A host poll on an empty
  semaphore returns `KE_CAN_NOT_WAIT`.
- Intrman saves/restores an enabled/disabled word. Nested suspend returns
  `KE_CPUDI` with a saved zero; restoring that zero does not enable interrupts.
  This corrects the ABI-visible state/error behavior.
- The existing IOPM priority word mirrors effective priority and restores it
  before releasing live waits. The file format is unchanged. Diagnostic
  counters are neither serialized nor used in reconciliation.

## Historical hardware limitations — not an implementation backlog

The completion suggestions below predate DT-9. Native Stream/Audio ownership
and retirement checks now replace the proposed kernel, timer and module work.
The described limitations remain relevant when identifying current callers.

| Area | Status and completion evidence still needed |
|---|---|
| Host delay | Explicit rejection, not implemented guest scheduling. Require `delay_host == 0` on boot, restore, gameplay, loading, save, and movie legs. A hit needs call-site/context attribution and routing through a resumable guest continuation. |
| Interrupt delivery | Saved-state ABI implemented; physical interrupt lines, per-thread interrupt-mask context, and gating of timer/deferred callbacks remain unmodeled. The current cooperative dispatcher does not interrupt a non-yielding guest region. Do not equate the nesting tests with hardware interrupt completion. A reached critical section crossing a scheduling point needs a kernel-mask design and execution-order regression. |
| Other scheduling calls | Start/wake/signal make targets ready; only `ChangeThreadPriority` gained syscall-time rescheduling here. Full PS2 preemption at every kernel API boundary is not claimed. Expand only with caller/context evidence and ordering tests; interrupt-form calls must remain deferred until callback return. |
| Semaphore host/context model | External hosts enqueue signals; a host wait is a polling extension under the dispatch lock. Interrupt-vs-thread restrictions for every ordinary/i-form import are not comprehensively modeled. The title's timer/completion callbacks intentionally enter through the host/dispatcher injection path. `SA_IHTHPRI` remains an accepted but otherwise unmodeled attribute. |
| Thread parameters | Priority, entry alignment, stack minimum, object existence/state and resolver failure are covered. Attribute validation, requested guest-stack allocation/fill modes, and exact platform resource exhaustion are not a complete PS2 thread manager. Host lifecycle/priority administration assumes a quiescent dispatcher; it is not a general asynchronous thread-management API. |
| Savestate metadata | The legacy IOPM layout has one priority word and no independent creation-priority word, semaphore attributes, or wait-queue sequence. Live threads retain creation priority; a freshly reconstructed thread must use the saved effective priority as its initial value. Exact restart semantics after such a reconstruction need a versioned metadata extension. No diagnostic counters should enter that extension. |
| Timer manager | `SetupHardTimer` remains success-only; general allocation/setup/source/prescale/error handling are incomplete. The reached source=1, width=32, prescale=1 heartbeat retains the approximate 16727 us period. Exact conversion, ceiling rounding, and timer-period cap removal are deferred after the controlled regression. Reconcile clock/frame-budget/loading effects and restore behavior before claiming closure. Other sources, prescalers, allocation errors, and hardware timers remain backlog. |
| Clock mode and alarm delivery | Sony's default 4608/125 and conditional 25/1 conversion branches are verified, but the supplied boot-mode entry is not captured. The HLE retains x37 compatibility behavior. The 100 us cooperative delay minimum does not model the hardware alarm compare guard or exact wake timing. Establish the target mode and delivery requirements before expanding this scope. |
| Module table/loader | Registration rows and idle modules are composition metadata. STREAM.IRX does not import sio2man/padman/mcman/mcserv/sdrdrv/cdvdstm functions; their EE service bridges own those APIs. A registered name alone does not prove hardware implementation. Generic loadcore/modload relocation, library linking, and arbitrary module errors remain outside this specialized loader. |
| SIF/device faces | Immediate fake-server bind and missing-handler success behavior still exist in sifcmd; specialized SIF/CD/SPU providers implement the title routes. Full firmware/RPC compatibility is not claimed. Audit endpoint hits before treating a stub-surface census as behavioral coverage. |

Primary semantic references, inspected from PS2SDK on this date (no PCSX2 or
OpenGOAL code copied):

- `https://github.com/ps2dev/ps2sdk/blob/master/iop/system/threadman/src/thbase.c`
  — priority-zero/self rules, ready requeue, errors, delay via SetAlarm.
- `https://github.com/ps2dev/ps2sdk/blob/master/iop/system/threadman/src/thcommon.c`
  — thread-start priority reset, preemption/FIFO rules, 100 us alarm minimum.
- `https://github.com/ps2dev/ps2sdk/blob/master/iop/system/threadman/src/thsemap.c`
  and `include/thsemap.h` in that directory — attributes, delete/overflow errors.
- `https://github.com/ps2dev/ps2sdk/blob/master/iop/system/intrman/src/intrman.c`
  — saved interrupt state and nested `KE_CPUDI`.
- `https://github.com/ps2dev/ps2sdk/blob/master/iop/system/timrman/include/timrman.h`
  — source 1 is the 36.864 MHz system clock, counter width and prescale meanings.
- `https://github.com/ps2dev/ps2sdk/blob/master/iop/kernel/include/kerr.h`
  — numeric error ABI.

These sources describe the reference behavior; implementation here uses the
project's existing cooperative coroutines and data structures. No external
implementation or license text was copied.

## Validation and handoff

New target `test_iop_shortcuts`, ctest name `IopShortcutSemantics`, exercises
ready FIFO rotation, self/peer rescheduling, current-priority inheritance,
FIFO/priority waiter behavior, sleep wake order, long/zero delays, invalid
calls, semaphore deletion/reuse, thread termination/restart, nested intrman,
concurrent observer reads, and host injections while `Dispatch()` remains
active in a yielding loop. Tests check effect sequences and completion, not
just metadata getters.

`test_iopm_section` adds an effective-priority serialize/parse/reconcile test
that checks wake order. `test_kernel_facing_modules` and `test_sysclib_map`
replace their explicit old-no-op expectations.
Deferred candidate tests covered clock conversion at zero, fractional
boundaries, 16666 us, and uint32 maximum; ceiling deadlines; exact heartbeat
cadence; maximum compares; and changed legacy-compare restore timing.
Those tests are deferred with the clock source edits. They are not
requirements of the retained compatibility implementation; the parent owns
restoring only those test changes while preserving core shortcut regressions.

Focused independent compile/run artifacts are under
`/tmp/ps2r-iop-completion/`. No helper modifies a shared build directory.
Completed focused results for the core IOP fixes:

- PASS: `IopShortcutSemantics`, including the final seven-field counter API,
  host injection while dispatch remains active, and restart priority ordering.
- PASS: `IopKernelUnits`, `IopKernelFacingModules`, `IopSysclibMap`,
  `IopModuleTable`, and `SifRpcBridge`, compiled independently from IOP/platform
  source with C++17, `-Wall -Wextra -Werror`, then bounded to 30 seconds.
- PASS: isolated `TestRestoredPriorityOrder` extracted from the new
  `test_iopm_section` case into a `/tmp` driver, linked against the modified
  IOPM implementation. The full title/anchor IOPM test remains for the parent.
- PASS: scoped `LintOneClass`, `LintEnginePurity`, and `LintSizeBudget`.
  Size lint reports only existing warnings; it reports no failures.
- PASS: syntax check of the full modified `test_iopm_section.cpp` and
  `iopm_section.cpp`; `git diff --check` on the owned paths.

Final logs are in `/tmp/ps2r-iop-completion/final/`; the final observer/API check
is `test_iop_shortcuts_final.log`, and the isolated restore case is
`test_iopm_priority.log`. Shared build artifacts were never mutated (a copy of
the existing core archive was used only for linking the isolated tests).

Historical candidate check: after the additional clock changes, independent
bounded runs passed for `IopKernelFacingModules`, `IopShortcutSemantics`,
`IopClockRebase`, and `IopModuleTable`; logs end in `_timer.log`.
Those focused passes did not establish live integration compatibility.
The parent's full suite subsequently had five failures, and the clock-only
control established the memory/title regressions above. The parent now owns
the source/test restoration and validation of the retained change set;
no post-restoration full-suite PASS is asserted here. This deferral update
edited only this report and launched no build or game run.

Parent validation targets:

```text
IopShortcutSemantics
IopKernelUnits
IopKernelFacingModules
IopClockRebase
IopSysclibMap
IopmSection
IopModuleTable
SifRpcBridge
StreamIrxLoadsAndParks
IopmBootSymmetricRepark
StreamIrxServesFirstRpc
```

After those pass, the parent owns the full suite and the live leg matrix.
Record before/after thread-operation counters and frame/audio outcomes.
Static potential reachability and isolated kernel tests cannot close sustained
title behavior, remaining timer abstractions, or physical interrupt semantics.
