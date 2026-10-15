# DMA, IPU, and IOP transition record

**Superseded work direction (user, 2026-10-07; DT-9):** hardware models are
scaffolding to retire. No further DMAC/VIF/GIF, IOP kernel/STREAM.IRX or IPU
command fidelity work is authorized. New fixes implement the game's intent
at a native boundary and remove the affected hardware path. The active
worklist is [HLE_COVERAGE.md](HLE_COVERAGE.md#scaffolding-retirement).

This document records the preceding bounded subsystem pass and its retained
transitional code. It does not claim complete PS2 coverage, scaffolding
retirement, or a passing standing memory-regression gate.

## Acceptance rules

- Existing transitional implementations retain their regression tests.
- A reached unsupported operation identifies a game-intent replacement task,
  not a hardware implementation task. Count the old path until it is retired.
- Static call-site evidence and dynamic observations are separate. Zero hits
  in boot, restored anchors, or the attract sequence do not prove that a later
  gameplay scene cannot reach an operation.
- Observer counters do not alter guest state or the savestate wire format.
- The existing frame/audio regression gates and PCSX2 replay tests remain in
  force. The frozen OLD tree and frozen reference files are unchanged.

## Workstreams

| Area | Scope | Detailed evidence |
|---|---|---|
| Scratchpad DMA | Transfer data in both directions; register progression, wrap, completion, supported modes, and unsupported-request reporting | [DMA report](subsystem_completion_dma.md) |
| IPU | Command reachability through the HLE movie path; required command behavior and explicit remaining decoder work | [IPU report](subsystem_completion_ipu.md) |
| IOP | Thread-priority scheduling, delay-context behavior, and classification of remaining module shortcuts | [IOP report](subsystem_completion_iop.md) |

## Implemented behavior

- Scratchpad DMA now copies RAM in both directions, wraps the scratchpad,
  advances transfer registers, and supports the title's RAM-to-SPR source
  chains. D_STAT acknowledgement and interrupt masks are functional.
- IPU VDEC, SETVQ, PACK and RGB16 CSC have command tests. DMA directions,
  channel 4 MMIO, pending output drains, and buffered input stop/restart
  are covered by a real DMAC/IPU/libipu coupling test.
- IOP priority changes now reorder runnable threads and priority semaphore
  waiters. Thread lifecycle, semaphore errors/deletion, interrupt suspend
  state, host injection ownership, and saved effective priority have tests.
  Kernel DelayThread parks for its requested duration (with the minimum
  delay); unsupported host-context calls return an error and are counted.

## Native replacement tasks still open

| Task | Completion evidence required |
|---|---|
| Movie decode and lifecycle | Own open/reset/decode/close at the game boundary with native demux/decode, frame and PCM queues; remove IPU initialization, CTRL writes and stop/restart snapshots. BDEC/IDEC completion is not required. |
| Alternative movie formats and presentation | Audit required assets/callers, then implement native pixel conversion, texture upload and presentation. Retire the entire RAW8/CSC/DMA continuation if reached. The current census still rejects RAW8 until that replacement is validated. |
| Scratchpad and graphics transfers | Identify the buffer-copy, resource-upload or bucket-submit intent and implement it natively. Do not add missing DMA modes, arbitration, MFIFO or stall controls. |
| STREAM and audio services | Replace the required RPC command meanings and file/audio state transitions with native services. Remove their calls into STREAM.IRX and the IOP kernel; retain the module inventory as migration evidence. |
| IOP timer-dependent work | Give native stream/audio jobs explicit game-clock deadlines and completion events. The deferred clock-precision experiment is closed as a model-fidelity work item; its regression evidence remains useful for the replacement. |
| Whole-game reachability | Extend the census to longer gameplay, transitions and save/load scenarios. Zero hits in the standing battery only describe that battery. |

## Repeatable runtime observation

`run.meta` from the instrumented host includes `subsystem_census=1`,
`dma_spr`, `ipu_commands`, `ipu_unsupported`, `ipu_unmodelled`,
`ipu_dma_snapshot_errors`, and `iop_thread_ops`.
These are informational fields, outside the deterministic configuration
fingerprint and guest memory.

After running a battery, inspect its completed runs with:

```sh
python3 verify/subsystem_reachability.py <battery-directory> \
    --expect-runs 39 --json <report-path>.json
```

The expected count is 13 legs times 3 repetitions. For a smaller check, supply
the actual expected number of runs. Missing instrumentation, unfinished runs,
guest parks, and observed unsupported operations are errors. A passing report
states only that the supplied runs completed without those unsupported
operations; it is not a full-game coverage certificate.
The check also rejects unhandled MMIO/IPU writes and invalid IPU DMA face accesses.
Host-context `DelayThread` calls fail coverage because they return an error
without delaying; valid kernel delays are counted separately.
Invalid DMA snapshots, failed queued IOP signals/wakes, and served RAW8 frames
also fail this acceptance check. RAW8 requires a native replacement for the
complete retail continuation before that row can be retired; direct hardware
CSC/DMA completion is no longer the task.

## Validation record

Debug and Release builds pass with zero compiler warnings. The preceding full
CTest run has **472 passing tests, four skipped and two disabled, with zero
failures**. The unchanged memory, title, attract and CD/DVD timing checks all
pass after deferring the clock experiment. Debug/Release identity also passes
on coldboot (200 frames) and a restored anchor (60 frames), including frame,
audio, memory, draw and GS-operation hashes.

The first 13-scenario × 3-repetition battery completed all 39 runs with admissible
repetitions and clean subsystem counters. Its standing memory-regression gate
failed by five raw words: the MP1 bit in the saved IPU control word at five
attract checkpoints. The 6,000 frame hashes and 1,000 audio hashes match the
standing candidate exactly. The native MPEG path omitted the stream-mode
update performed by the retail sequence parser. Under DT-9 the unfinished
decoder-to-IPU handoff was withdrawn. The next fix must retire the movie
lifecycle's dependency on the IPU snapshot; the unchanged regression gate
remains open. The abandoned experiment is preserved outside the source tree
under `int_work/subsystem_completion/withdrawn_ipu_bridge/`.

After withdrawing that unfinished experiment, Debug and Release rebuilds
again completed without warnings, and all 21 selected IPU/HLE, picture-oracle,
reachability-selftest and lint checks passed (`withdrawal-build-debug.log`,
`withdrawal-build-release.log`, `withdrawal-checks.log` in the same work
directory). The full-suite and battery results above belong to the retained
pre-bridge candidate; this documentation/withdrawal step did not run another
full battery or change its failed verdict.
