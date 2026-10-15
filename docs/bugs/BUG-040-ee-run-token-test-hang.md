# BUG-040 — `EeRunTokenAndEventQueue` (tests/ee/test_ee_sched.cpp) hangs intermittently under ctest

Status: FIXED (4D, 2026-09-27 20:25) — engine fix + rig fix + deterministic regression (`b040_*` cases in `EeRunTokenAndEventQueue`); 10/10 repeat under load avg ≈6 (`/tmp/p4d/flake10.log`). Phase-3 test; first seen in 4C (P4-RUNTOKEN-FLAKE); park site and mechanism identified in 4C‴ (live `lldb -p` attach, 19:53).
(Opened in 4C″ as "BUG-011", a duplicate of the minification-aliasing id; renumbered BUG-040 in 4C‴.)

## Symptom
`ctest --preset macos-dev -R EeRunTokenAndEventQueue` occasionally never finishes and is killed by the
120 s ctest timeout; the binary run directly exits 0 in well under a second. Reproductions:
- 4C (`/tmp/p4c/ctest_final.log`): 1 timeout in a full run; the next `ctest -R` passed in 0.07 s.
- 4C″ (`/tmp/p4c3/ctest_flake.log`): `--repeat until-fail:5` → Passed 0.08 s, Passed 0.07 s, Passed 0.08 s,
  `***Timeout 120.03 sec` on the 4th run — while two harness legs (`run_leg.py`) were executing
  (CPU contention). The full run minutes later, machine otherwise idle: passed (92/92).
- 4C″ review (p4c3r, `/tmp/p4c3r/ctest_flake.log`, `ctest_flake_load.log`): NOT reproduced —
  `--repeat until-fail:5` idle: 5/5 Passed (0.06–0.08 s); `--repeat until-fail:5` concurrent with the two
  harness legs: 5/5 Passed (0.07 s); two full runs 92/92 (`ctest_full1.log`, `ctest_full2.log`).
- 4C‴ (`/tmp/p4c4/ctest_flake10.log`): `--repeat until-fail:10` on an otherwise idle machine — 10/10 Passed
  (0.07–0.08 s), `100% tests passed, 0 tests failed out of 1`.
- **4C‴ review (p4c4r, `/tmp/p4c4r/flake5.log`, 19:42–19:44) — REPRODUCED, with a new symptom.**
  `ctest --preset macos-dev -R '^EeRunTokenAndEventQueue$' --repeat until-fail:5 --timeout 120`, no reviewer
  processes running but `uptime` load avg 7.19/7.56/7.06 on 10 cores from other sessions (the machine was NOT
  idle) → `Passed 0.07 sec`, `Passed 0.08 sec`, `***Timeout 120.03 sec` on run 3. The hung run's output,
  verbatim:
  ```
  PASS single_runner_no_concurrent_holders
  PASS all_quanta_ran
  PASS no_deschedule_without_scheduling_point
  FAIL election_prio_then_fifo_seq
  ```
  i.e. `TestPriorityElection` **completed with the WRONG order** and the hang is in a LATER sub-test (the
  next one, `TestPreemption`, printed nothing). The line `sh: /bin/ps: Operation not permitted` printed at
  the timeout is ctest itself: CMake's kwsys `kwsysProcess_Kill` enumerates the child tree with
  `popen("ps -eo pid,ppid")` on macOS and the sandbox denies `/bin/ps`; the direct child is still killed by
  pid (no lingering `test_ee_sched`: `pgrep -fl test_ee_sched` empty afterwards). Sandbox artefact, not a
  clue about the hang.
- 4C‴ iteration 2 (`/tmp/p4c4/flake5_iter2.log`, 19:51:39–19:53:39): `--repeat until-fail:5 --timeout 120`
  under external load avg 5.32/5.68/6.25 (other sessions, no local build) → **Passed 0.08, 0.07, 0.08, 0.07 s,
  `***Timeout 120.04 sec` on run 5** — REPRODUCED again, and this time **attached live** (see below). The hung
  run's output, verbatim (stdout is unbuffered, `:276`, so this is exact):
  ```
  PASS single_runner_no_concurrent_holders
  PASS all_quanta_ran
  PASS no_deschedule_without_scheduling_point
  PASS election_prio_then_fifo_seq
  PASS preempt_point_hands_over_to_higher_priority
  FAIL change_priority_moves_to_tail
  PASS queue_initially_empty … PASS b035a_nothing_applied_off_token   (9 event-queue checks, all PASS)
  ```
  then nothing: `b035a_holder_drained_queue` never printed → parked inside `TestNonHolderDrainCountedNoop`.
  **`lldb -p 71405 --batch -o "thread backtrace all"` at 19:53:30 (`/tmp/p4c4/lldb_hang_iter2.log`), two
  threads only:**
  ```
  thread #1 (main): __ulock_wait ← _pthread_join ← std::thread::join()
      ← TestNonHolderDrainCountedNoop() at test_ee_sched.cpp:171:12 ← main at test_ee_sched.cpp:285:5
  thread #2: __psynch_cvwait ← std::condition_variable::wait<EeRunToken::AcquireSlow(int)::$_0>
      ← EeRunToken::AcquireSlow(this=…, tid=60) at ee_run_token.cpp:79:9
      ← EeRunToken::GuestEnter(tid=60, prio=10) at ee_run_token.cpp:96:5
      ← TestNonHolderDrainCountedNoop()::$_0::operator() at test_ee_sched.cpp:170:36
  ```
  No third thread exists: nobody holds or will ever release the token; tid 60 waits for `owner_ == 60`.

Tally (2026-09-27): **4 timeouts** (4C full run; 4C″ repeat under two legs; p4c4r repeat under load avg ≈7;
4C‴-iter2 repeat under load avg ≈5.5) vs **10/10 + 5/5 + 5/5 passes** (4C‴ idle; p4c3r idle; p4c3r
concurrent) plus every full run since 4C. Every hang was preceded by a wrong-order `FAIL` in an
election sub-test (`election_prio_then_fifo_seq` in p4c4r's run, `change_priority_moves_to_tail` here).

## What is known
- The test exercises the EE run-token / event-queue handoff (thread wake ordering); nothing in
  Phase 4 touches it (it passed 3/3 immediately before the hang in the same repeat run).
- Load-sensitive: all three hangs occurred with other CPU-heavy work running (two legs; load avg ≈7 from
  other sessions). A lost-wakeup race (condition checked before the notifier's store becomes visible, or a
  notify before the wait) was the leading hypothesis — the 4B′ "blocking race" family.
- **Lead from the p4c4r reproduction (4C‴ review):** the hung run printed `FAIL election_prio_then_fifo_seq`
  before parking. `TestPriorityElection` (`tests/ee/test_ee_sched.cpp:91-110`) increments `entered`
  immediately after each `std::thread` construction (`:102`, `:104`, `:106`) — i.e. BEFORE that worker has
  executed its `GuestEnter` — and orders the workers only by 2 ms sleeps; `boss` leaves on
  `entered >= 3`. The reviewer's reading (boss exits before w32 entered → FIFO among w33/w31) does not by
  itself yield the wrong order: `ElectLocked` (`ee_run_token.cpp:60-66`) elects over the `ready_` slots,
  and `MakeReady(32, 20)` has already made 32 the best slot whether or not its host thread has arrived, so a
  late w32 is still first. What DOES yield it is the other side of the same rig race — a worker entering
  before `boss` holds the token (Mechanism, below). The pointer at the rig's start ordering was right.
  `TestPreemption` (`:112-119`) has the same MakeReady-before-GuestEnter shape (`lo`: `MakeReady(41, 10);
  PreemptPoint()` vs `hi` calling `GuestEnter(41, 10)` only after a 5 ms sleep on the main thread), and
  `TestChangePriorityTail` (`:121-134`, `entered.fetch_add(1)` at `:130-131`) repeats the election pattern.
- **Mechanism (read from the code after the live attach; owner_ not yet dumped — 4D confirms):**
  1. *Rig start race.* Every election sub-test launches `boss` and its workers as concurrent host threads
     with no synchronisation on "boss holds the token first" (`TestPriorityElection` `:93` vs the `starter`
     at `:101-107`; `TestChangePriorityTail` `:123` vs `w51/w52` at `:130-131`; `entered` is bumped by the
     launcher right after `std::thread` construction, not by the worker after `GuestEnter`). Under load the
     boss's host thread is late, a worker's `GuestEnter` finds `owner_ == kNone` and is elected first
     (`ee_run_token.cpp:93` → `ElectLocked`) → the wrong-order `FAIL` (33 before 32; 51 before 52).
  2. *Engine wedge.* That worker runs and `GuestExit`s (its slot `valid = false`, `:104`). The late `boss`
     then `MakeReady(tid)`s it (`:140-149`): `FindReady` misses (invalid), `SlotFor` allocates a FRESH valid
     slot for a tid whose host thread has already left and will never `GuestEnter` again. When `boss` exits,
     `ElectLocked` (`:60-66`) elects it (it is valid and ready), so `owner_` = a tid with no host thread. The
     next sub-test's `GuestEnter(60, 10)` only elects when `owner_ == kNone` (`:93`), so it parks in
     `AcquireSlow` (`:79`) forever — exactly the lldb picture. The same shape explains p4c4r's run
     (wrong order in `TestPriorityElection`, park in `TestPreemption`'s `GuestEnter(40, 60)`).
  Conclusion: the wrong-order FAIL and the hang are ONE bug, in two parts — a test-rig start race that
  makes a stale `MakeReady` possible, and an engine invariant gap: **a READY tid that never attaches wedges
  the token, and nothing can re-elect past it.** Whether the second part is reachable in-game (a thread
  made READY by `StartThread`/`WakeupThread` and then deleted/terminated before it ever runs, with
  `ElectLocked` having already picked it) is the 4D question — it is the BUG-035 family.

## Next step (owner: 4D opening)
1. Confirm the wedge value: reproduce under load (bounded) — `ctest --preset macos-dev
   -R '^EeRunTokenAndEventQueue$' --repeat until-fail:10 --timeout 120` with a deliberate CPU load (the two
   harness legs, or `yes > /dev/null` × cores); on the hang `lldb -p $(pgrep test_ee_sched) --batch
   -o "thread backtrace all" -o "thread select 2" -o "frame select 4" -o "p this->owner_" -o "p this->ready_"`
   and record `owner_` here (expected: the tid `MakeReady`'d after its host thread had exited — 51 or 33).
2. Engine fix in `engine/ee/sched/ee_run_token.cpp` (the test stays the oracle, order assertions untouched):
   a READY tid without an attached host thread must not be able to wedge the token. Candidates, pick after
   step 1: (i) `GuestExit` retires the tid so a later `MakeReady` of an exited tid is a counted no-op
   (needs a "retired" mark distinct from `valid=false`, since StartThread legitimately pre-enqueues);
   (ii) `GuestEnter` re-elects when `owner_` is a tid no host thread holds (needs an attached flag per slot).
3. Rig fix in `tests/ee/test_ee_sched.cpp` so the start race cannot happen: `boss` must hold the token
   before any worker is launched (launch workers from inside `boss` after `GuestEnter`, or have workers
   bump `entered` after their own `GuestEnter`) — in `TestPriorityElection`, `TestPreemption`,
   `TestChangePriorityTail`. Both fixes; the rig fix alone would hide the engine gap.
4. Then answer the in-game question (Terminate/Delete of a never-run READY thread) and, if reachable, add
   the runtime oracle to the census (`run_token_wedge=`).

## Root cause (confirmed in 4D by deterministic reproduction)
Two parts, one bug (the 4C‴ reading was right):
1. **Engine invariant gap** (`engine/ee/sched/ee_run_token.cpp`): `GuestExit` only invalidated the tid's ready slot. A
   later `MakeReady(tid)` — for a tid whose host thread had EXITED and would never `GuestEnter` again — re-allocated a
   fresh valid slot (`SlotFor`), `ElectLocked` elected it (best priority), and every later `GuestEnter`/`BlockEnd` only
   elects when `owner_ == kNone`, so the token stayed with a tid no host thread holds. Permanent wedge.
2. **Rig start race** (`tests/ee/test_ee_sched.cpp`): the election sub-tests launched `boss` and its workers
   unsynchronised; under load a worker entered before `boss` held the token, ran and exited first (the wrong-order
   `FAIL`), and the late `boss`'s `MakeReady` of that exited tid was exactly the part-1 shape.

## Fix (4D)
- **Engine:** a tid is RETIRED by `GuestExit` (`retired_[tid]`, guest tids 0..255; the HostGate pseudo-tid is never
  retired). `MakeReady` of a retired tid with no slot is a **counted no-op** (`RetiredReadyNoops()`), so a READY slot no
  host thread will attach can never exist and never be elected. The next `GuestEnter` of that tid (StartThread of a
  reused thread id spawns a fresh host thread) attaches and un-retires it. Header comment on `MakeReady` documents
  the rule. `ResetForTest` clears both.
  Known cost, recorded for the owner of the ThreadManager contract: for a REUSED tid, StartThread's pre-enqueue
  `MakeReady` (before the worker's `GuestEnter`) is now the no-op, so that thread's FIFO seq is assigned at host
  arrival rather than at the kernel event — a same-priority ordering window that exists only for restarted tids
  (first-start tids, i.e. every boot-time thread, keep the kernel-event seq). Closing it needs the spawn signal from
  `ThreadManager::StartThread` (a `Revive(tid)` before `MakeReady`), a Phase-3 file edit (D-SEQ-2 note) — 4D′/4E.
- **Rig:** `boss`/`lo` publish `boss_holds`/`lo_holds` after `GuestEnter`; the launcher waits for it before creating any
  worker; workers count themselves in (`entered`) BEFORE their `GuestEnter` (they block inside it while boss holds).
  Order assertions untouched (`election_prio_then_fifo_seq`, `preempt_point_hands_over_to_higher_priority`,
  `change_priority_moves_to_tail`).
- **Regression test** `TestRetiredTidCannotWedge` (`b040_makeready_of_exited_tid_is_counted_noop`,
  `b040_exited_tid_never_elected`, `b040_next_guest_enter_runs_after_exited_tid_made_ready`,
  `b040_reused_tid_reattaches_on_guest_enter`): tid 70 enters and exits; a holder MakeReady's 70 and exits; a fresh
  GuestEnter(72) must run within 5 s (a wedge is reported via `Shutdown()`, never a hang). **Proof it reproduces:**
  with the retired check disabled (`/tmp/p4d/nofix_run.log`) the three first cases FAIL deterministically
  (`owner_` = 70, tid 72 parked until the deadline); with the fix `ALL PASS` (`/tmp/p4d/fix_run2.log`).
- **Repeat:** `ctest -R '^EeRunTokenAndEventQueue$' --repeat until-fail:10 --timeout 120` (bounded,
  `/tmp/p4d/flake10.log`, load avg 5.73/6.07/6.62 — not idle): **10/10 Passed (0.06–0.27 s),
  `100% tests passed, 0 tests failed out of 1`, 0.86 s.**

## In-game reachability (the step-4 question)
Not reachable through the ThreadManager today: every `MakeReady` site is guarded by the target's status
(WakeupThread/SignalSema/ResumeThread refuse a DORMANT thread; TerminateThread of the running target throws; StartThread
spawns before it readies), and a READY-never-run thread that is Terminated/Deleted goes through `Drop` (re-elects). The
engine rule now holds regardless, so no runtime oracle (`run_token_wedge=`) is needed; the counted no-op is the diagnostic.
