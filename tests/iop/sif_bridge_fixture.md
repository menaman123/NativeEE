# SifRpcBridge fixture ownership

The stage10 full suite failed five assertions in `TestRoundTripServiceCharge`.
They were preceded by fourteen FRAME-drive stalls. This was not evidence of a
changed service charge: its RPC thread never entered the service-loop park.

The original fixture order places the REALTIME lifecycle immediately before
the charge test. A bounded reproduction with the unchanged Release libraries
failed with both `-O0` and `-O3` test compilation. Moving the charge case first
made every assertion pass, identifying an order dependency, not a fix.

A public-context diagnostic observed:

```
BEFORE_RT host=0x103526660
AFTER_RT host=0x103526260
CHARGE_SETUP tid=1 state=2 wait=0 registered=1 shutdown=0 frame=1 service=0
```

The main thread's `FiberMacos::Current` handle changed across the REALTIME
fixture. The next fixture's RPC thread remained `Run/None`, rather than
`Wait/Sleep`; wakeups were banked and no RPC body ran. Consequently the charge,
completion-count and event assertions all failed downstream. The migration
from the booting host to `IopHostThread` crosses macOS ucontext/TLS bookkeeping;
this test does not claim to repair that generic platform limitation.

The correction gives the entire REALTIME fixture a separate host execution
owner, joins it, and checks that the FRAME caller retains its host context.
The charge test now checks thread creation and the initial service-loop park
before measuring time. Its assertion order, zero/160-us charge checks, ticket
counts and event firing conditions are unchanged. All preexisting bridge
assertions remain. The main thread never changes its dispatch policy.

This is a harness lifecycle correction. No bridge, IOP kernel, clock, platform
fiber implementation or game path was changed. In particular it is unrelated
to the stage9 bank frame/audio divergence. The selected services' contract is
still logical time and callbacks on the game execution owner; REALTIME model
behavior is not a requirement for their replacement.

Before touching this test, OLD's `SOUND_P6_IMPL.md:61–77` and
`SOUND_P7_REPORT.md:158–160` were checked for reply/wait regressions. Those
describe completion visibility and deferred callback progress, not this host
context contamination. No old wire governor or new device timing was added.

Evidence under `int_work/m_wave/services`:

- `sif-fixture-diagnostic1.log`: original fails with both test optimization
  levels; charge-first diagnostic passes; the unparked charge thread is shown.
- `sif-fixture-context.log`: caller context changes and all five original
  charge failures reproduce.
- `sif-fixture-fixed.log`: actual Debug libraries with Debug fixture, and
  Release libraries with Release fixture, three runs each; all assertions pass.
  One additional ASan/UBSan fixture run against the unchanged Release libraries
  passes. The libraries themselves were not rebuilt with sanitizers.

The focused runs used the existing parent archives read-only and wrote their
executables/logs in the services evidence directory. No worker game run or
PCSX2 comparison was used.
