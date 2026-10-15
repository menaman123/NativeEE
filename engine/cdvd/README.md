# `engine/cdvd/`

**Phase:** 4  
**Purpose:** ISO9660 walker, deferred async read engine, DVD latency model, stream reader, delivery meter/budget; hle/ = libcdvd + the completion-visibility model.

## Belongs here

- IsoFs, ReadEngine, LatencyModel, StreamReader, DeliveryMeter, DeliveryBudget

## Does not belong here

- allocator re-entry sites (workaround quarantine), file table (games/<title>/data)

## Transplanted from (OLD, read-only)

- `hle_heap.{h,cpp}, cdvd_stream.{h,cpp}, ps2_vblank.h delivery meter, budget_lift.h`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

## Delivery visibility (Config `cdvd.delivery_visibility`, DP-17 (a); I8.13-A)

When a completed host-served disc read becomes visible to the EE (`DeliveryMeter`, `hle/LoaderModel`):

- `boundary` (default = today's behaviour): every completion scheduled during tick k is delivered at boundary slot 4 of the
  due tick (`cdvd.latency_model` picks it, never earlier than k + 1). `RootPlayPolicyInert` pins this bit for bit.
- `poll`: the virtual drive has a timeline in ms — a read issued during tick k starts at `max(drive free, k × 16.667)` and ends
  at start + seek + transfer (the Modelled cost scaled by `cdvd.poll_latency_scale`, > 0, default 1.0 — never by
  `cdvd.latency_scale`, whose 0 under the deterministic profile is `boundary` mode's "no drive time" pin). A read that ends
  before `(k + 1) × 16.667` is complete inside the frame: the next `LoaderModel::Poll` (any loader poll; the owner binds
  `SetNowTick`) runs `DeliveryMeter::CompleteNow(k)` and delivers it there and then — the EE observes a finished DVD read
  mid-frame, as on the console; a read ending later is due at the boundary of the tick the drive finishes in (slot 4).
  The title's BLOCKING wait on a read (`LoaderModel::CompleteNow(fs)`): the EE is blocked until the drive is done. While
  the drive's end lies at or past the current frame's edge the wait PARKS whole frames (`LoaderModel::SetParkFrame`: the
  owner's one-frame park — the boundary protocol's wait, as the FMV pace park and the kernel timer wait: T0 advances one
  field, the vblank is raised, the domain tick moves by one; never a sub-tick) and re-checks, until the end is inside the
  frame; then the read is delivered at the drive's end and its meter entry is SETTLED (`DeliveryMeter::Settle`: no phantom
  pop later). Parked frames are counted (`WaitParks()`); nothing is forced (`Forced()` is `boundary` mode's pull-forward, 0
  under `poll`). Only without a park (unbound, or one that cannot advance time) is a wait past the edge an OVERRUN
  (`WaitOverruns()`); the drive timeline keeps its time either way — nothing the drive did not do is charged, nothing it
  did is dropped. A synchronous burst of waited reads therefore spans as many frames as its drive time (the hardware's
  blocked EE). A poll delivery is data + status only (dest bytes, ring copy + cursor, slot byte): no guest callback runs
  inside a poll (RPC end-callbacks stay slot-4 / wait-pump duties).
  The `DeliveryBudget` is NOT consulted for deliveries under `poll` (the drive model IS the throughput: one contiguous
  2 KB sector costs 1/1.08 ms, so ≈ 18 single-sector reads fit a frame; the title's loader windows average 2–3 sectors
  → ≈ 6–9 reads per frame); it keeps its role under `boundary`.
  ONE drive (`DeliveryMeter::ChargeReadAtMs`, `poll` only): the recompiled STREAM module's own `sceCdRead`s are charged on
  the same timeline (seek from the head the EE's reads left, then the transfer, at the poll scale) and complete at that
  end on the IOP clock (`CdvdmanModule::SetCompletionDue`, bound by the composition root; the issue instant is the
  domain's frame start plus the IOP's offset inside its frame) instead of a fixed latency after the issue; under
  `boundary` the meter charges nothing and the module's `kCompletionUs` applies as before. The IOP's half of the
  in-frame rule lives at the title's load-wait lift (`LoaderSpinLift`, BUG-070): a lap with no host-served read in flight
  runs the IOP's work due inside the frame at the poll (`IopModuleTable::PumpWithinFrame`) and yields when nothing is due.
  Census (the MODEL's split by the site that landed the bytes — a meter pop whose closure found the read already delivered
  is not a delivery): `run.meta cdvd_visibility=<mode>`, `cdvd_poll_deliveries=<n>` (at a Poll), `cdvd_boundary_deliveries=<n>`
  (slot 4), `cdvd_wait_deliveries=<n>` (inside the blocking wait, poll mode; a wait first lands every read queued ahead of the
  waited one, in issue order -- `cdvd_wait_order_deliveries=<n>`, trace `dlv src=wait-order`), `cdvd_wait_overruns=<n>`,
  `cdvd_wait_parks=<frames>/<protocol waits>` (whole frames blocking waits parked through / the waits those parks issued),
  `cdvd_forced=<n>` (boundary mode's pull-forward), `cdvd_seeks=contig=,short=,long=` (the drive timeline's seek classes),
  `cdvd_meter_pops=<poll>/<boundary>` (the meter's raw pops), `cdvd_poll_scale=`. A `poll` harness leg also writes
  `<out>/loader_trace.log` (`<tick> <seq> enq|prk|dlv|rel|cns|yld …`; `enq … lba=<n> sec=<n> end=<ms>` — the drive timeline
  is auditable per read; `prk … end=<ms> edge=<ms>` one per parked frame; `dlv … src=poll|wait end=<ms> [overrun]`).
  Measured (DECISIONS_LOG DP-17 attempts 2 and 3): the title's loader issues AND waits its bulk load as a synchronous burst
  inside one loader-SM poll; under attempt 2 that burst ran inside one 16.7 ms frame (`cdvd_wait_overruns=904`); under
  attempt 3 the waits park 117 frames across the burst (`cdvd_wait_parks=117/117 cdvd_wait_overruns=0`), the frames the
  console's EE spends blocked. The wait seam also runs the title's own completion step (the callback stream's copy and the
  staging clear, `games/…/loader_sync`), the omission of which was BUG-068's root cause.
