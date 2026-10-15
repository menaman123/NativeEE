# HOOK_RECLASSIFICATION — every address-bound override, reclassified

**DT-9 supersession (user, 2026-10-07):** the classifications below are a
historical audit, not a prohibition on native game-platform replacements.
The requirements to repair a defect at the hardware/OS layer (§1), bind only
SDK functions (§3.3), or retain a hardware path until its model is completed
are superseded for DMAC/VIF/GIF, IOP kernel/STREAM.IRX and IPU command
scaffolding. Implement the specified game intent at a verified native boundary
and retire the old path, as tracked in
[HLE_COVERAGE.md](HLE_COVERAGE.md#scaffolding-retirement).

Title function identities, layouts and semantics remain in `games/`; reusable
mechanisms remain in `engine/`. Verify NTSC function identity and calling
convention before binding. Native replacements implement the full stated
game operation and its observable effects, with regression and retirement
checks; they are not constant-return patches that conceal an unhandled operation.

[REV 2026-09-22 D1/D2/D3] New document implementing user decision D2: **there is
no "game hooks" concept and no `hle/game/` directory.** Every current
address-bound override in the tree is reclassified into exactly one of five
classes (§1). This document is the source of truth; ARCHITECTURE.md §4.9 (the
SDK-library binding manifest) and MIGRATION_PLAN.md (Phase 2 retirement + row
destinations) are kept consistent with it.

Populations covered (A4 §0.1 finding 1, re-verified against source 2026-09-22):
the `install_dual_hook` layer (A4 §4.3's 191-row classified snapshot; 200 live
call sites today per the MIGRATION §1.3 count reconciliation), the 235
`manual_switch_dispatchers.cpp` installs (222 `fun_*_body.inc` + 3 hand-written
dispatchers + 10 sibling raw installs), the 9 anim-pose native mirrors
([REV-2 2026-09-23 ledger-fix] anim_pose_hle.cpp installs nine — its own log
line is "installed (ok/9)" — of which only 0x294480 overlaps the manual 235),
the `sce_hle` registry (291 signature-matched bindings), the libc installs (4),
the sif_hle 0x181490 piracy bypass ([REV-2] sif_hle.cpp's sceSif handler
*bodies* are invoked through the 291 registry rows — e.g. sceSifCallRpc
0x2d29a8, libkernl.cpp:112 delegation — so its only *independent* address-bound
override is the piracy check), the recompiler-injected sce seams (9 in the
gs-block emission: 8 SDK-band, 5 of whose addresses the 291 registry also owns,
+ 1 game address), S8-ARGV, and the [KEY-REDIRECT] allocator repair.

---

## 1. The five classes (definitions, binding)

| Class | Definition | Where it lives (D3 tree) |
|---|---|---|
| **SDK/OS HLE** | Replaces a function of the SDK 3.0.1 library drop (sce libs, toolchain libc) or the kernel/boot environment. Bound by **library signature** via `tools/sce_lib_finder` matching against the SDK 3.0.1 drop — **never by hand-pinned game address**; the concrete EE address is resolved at build time from the signature match. | the subsystem it emulates: `pad/hle`, `mc/hle`, `cdvd/hle`, `ipu/hle`, `sound/hle`, `gs/hle`, `dmac/hle`, `sif/hle`, `ee/hle` (kernel, boot env, libc) |
| **HARDWARE/TIMING HLE** | Models what the game observes of hardware in the time dimension (counters, budgets, completion visibility, latency). Not bound to game code identity — bound to the hardware surface the game reads. | `ee/timing` (clocks, budget anchor) and the owning device subsystem (`cdvd/hle` completion model) |
| **EMULATION-BUG-WORKAROUND** | The patch masks a defect of **our runtime** (a divergence from retail that our own layers introduced). The refactor must fix the defect at the hardware/OS layer and then delete the patch. Each row below names the suspected defect and its record. | quarantined beside the suspected defect's layer until the root fix lands; deletion gated by the equivalence harness (§4) |
| **RETAIL-BEHAVIOUR** | A game bug that retail hardware also exhibits. **Not patched** — reproduced bit-faithfully. | nowhere (it is the absence of a patch); pinned by an invariant test |
| **DIAGNOSTIC** | Read-only probe. Deleted from the tree; only the probe **mechanism** survives, in `core/diag` (PS2R_DIAGNOSTICS builds only). | `core/diag` (mechanism only) |

## 2. Headline counts

[REV-2 2026-09-23 ledger-fix] Counting basis: one row per **distinct binding**
(distinct EE address, or a non-address mechanism like the boot-env writer /
write-gauntlet mutator). Redundant install *mechanisms* at an address the
registry already owns do not double-count — 5 of the 8 SDK-band gs-block seam
addresses are registry-owned (§3.5), and the generator emits 4 further seams at
registry-owned addresses (sceSifInitCmd 0x2d1a50, sceSifBindRpc 0x2d27c8,
sceSifCallRpc 0x2d29a8, InitTLB32MB 0x2d6618); `register_sce_hle_overrides()`
installs last and wins, so those addresses are counted once, in the 291.
Exception, stated where used: the DIAGNOSTIC bucket keeps A4's per-call-site
basis (191 snapshot rows span 156 distinct addresses; 200 live sites today)
because each probe site is retired individually.

| Class | Count | Composition |
|---|---|---|
| SDK/OS HLE | **299** bindings | 291 sce registry + 4 libc + 3 seam-only gs seams (sceDmaSync 0x2c1030, sceGsResetPath 0x2c1ab0, sceGsSyncPath 0x2c1b50 — the other 5 seam addresses are already registry rows, §3.5) + 1 S8-ARGV boot environment |
| HARDWARE/TIMING HLE | **2** | budget-predicate emulation @0x191bd0; CDVD/loader completion-visibility model (incl. the hold branch currently inside the 0x2ad110 hook) |
| HARDWARE/TIMING HLE (landed, D3 tree) | **+1 seam** | **`LoaderSpinLift`** (`games/<title>/hle/loader_sync/loader_spin_lift.{h,cpp}`, I4.10-C): the sync-wait lift over `LoaderModel::ShouldYieldPollSlice` at the title's headroom query `FUN_00191bd0`, engaged at the load-wait loop's two load-poll sites (lap-1 spin ra `0x17d724`, `$s0 != 0`; lap ≥ 2 gate ra `0x17d75c`, `$s1 != 0 && $s0 == 0` — DP-1 option (a)); guards: the noted poll was HeldInflight AND the read is still in flight (boundary-only delivery, re-verified by the model); answer v0 = 0 only; counter `run.meta loader_spin_lifts=lap1=,lap2=,declines=`; title data in `Layout` (never in engine/); pin `LoaderSpinLift`. Rule: T0 advances in real time on hardware and the loop exits on quantum exhaustion; NEW's T0 is slice-frozen (BUG-014/031/032) so this seam supplies the exit the clock would. Replaces OLD's [LDR-HLE] hook (`loader_sync_hle.h`) and the budget half of S96 at this site; BUG-041. |
| EMULATION-BUG-WORKAROUND | **250** | 4 behavioral dual-hooks (0x181560, 0x1b36b8, 0x268440, 0x2ad110 dest-preservation) + [KEY-REDIRECT] + 235 manual-body installs + 8 anim-pose mirror addresses beyond the 235 ([REV-2] 9 mirrors installed; 0x294480 overlays a manual body — 243 distinct manual/anim addresses) + the 0x0019aff0 hle_SetupDisplayRegisters game-address seam ([REV-2] not hle_InitGraphics, which is never installed — §3.5) + the 0x181490 piracy-check bypass ([REV-2] reassigned from the former "2 sif_hle" SDK rows) |
| EMULATION-BUG-WORKAROUND (landed, D3 tree, present-time, non-address) | **+1** | **Stale-display present stand-in** (`ScanoutComposer::PickSource` + `VramLayout::main_fb_fbp`, Phase 6 6D‴; §3.10; row L1 of PHASE6_IMPL "Ledger deltas owed at integration", carried by INTEGRATION CHECKLIST item 7; docs/bugs BUG-063). Not an EE address binding — outside the 747 total [IDOC-A 2026-10-01] |
| RETAIL-BEHAVIOUR | **1** | the oversized-read stream-buffer overflow semantics (reproduced, not patched, once the 0x2ad110 root fix lands) |
| DIAGNOSTIC | **195** | 200 live dual-hook sites − the 5 behavioral/timing rows. Re-bucketed A4 snapshot: 52 DIAG (incl. the second, chained-probe 0x191bd0 row) + 129 DIAG? (130 minus the gated budget row that reclassifies as timing HLE) + 4 DISABLED + the mislabeled 0x1a98b8 "FIX upstream" probe = 186; + 9 post-snapshot sites, all probes per the MIGRATION §1.3 reconciliation = 195 |
| **Total address-bound rows** | **747** | [REV-2 recomputed: 299+2+250+1+195. The former 306/243 split double-counted 5 seam addresses inside the 291 and miscounted sif (2→1) and anim (2→8 beyond the 235); the shifts cancel: SDK −7, workaround +7] |

## 3. Population-by-population disposition

### 3.1 `install_dual_hook` layer (main.cpp; 200 live sites, A4 §4.3 191-row classified snapshot)

**195 → DIAGNOSTIC.** All DIAG/DIAG?/DISABLED rows (composition in §2's
DIAGNOSTIC row: 186 of A4's 191 after re-bucketing, plus the 9 sites added
after A4's snapshot — all probes per the MIGRATION §1.3 reconciliation; the
0x1a98b8 row is included here per the A4 §4.3 caveat: labeled FIX only because
its comment says "FIX upstream" — it is a capped probe). Deleted outright;
the two probes relocated into generator emission ([ADAF0-DIAG] @0x2adaf0,
[DESER] @0x205df0, A4 §4.3 rows 91/130) die at the generator with the ARCH §5.2
"no diagnostics in generated code" rule; the declared `GenProbe` channel is
their diagnostics-build replacement.

**4 → EMULATION-BUG-WORKAROUND** (the confirmed behavioral set, A4 §4.3; + the FMV abort hold row landed by INTEGRATION_PLAN row 7.7):

| Patch | What it does | Suspected runtime defect (to fix at root) | Evidence |
|---|---|---|---|
| `0x181560` [META-ARENA] | Routes the game allocator's small (≤0x20) metadata allocations from pool-allocator call sites into a host-invented 2 MB arena at EE phys 0x02000000, isolating them from the shared slab; "eliminates the free-list corruption that put region-1 (key-buffer) pointers into pool 0x31b138 and overwrote the key" | Host-induced allocator/free-list corruption: the corruption does not occur on retail/PCSX2, so an upstream host divergence (the boot double-init leak family below, and/or the loader delivery divergence at 0x2ad110) poisons the free list. Root fix = eliminate the upstream divergences; the meta-arena and this reroute then delete together (the arena is itself a non-hardware invention — A4 §2.2, §0.8) | main.cpp:3095-3108 comment; A4 §4.3 row 46; hle_meta_arena.{cpp,h}; [R144-LISTLEAK] |
| `0x1b36b8` [RENDINIT] | Before the *second* boot-time run of the render/DMA-packet init FUN_001b36b8 re-allocates, frees the first run's ~2 MB of DMA-tag arrays via the game's own free (FUN_00181590) | Host boot flow runs **both** init paths of FUN_00178388 where "retail rb-mode boot runs only one" (the in-source root-cause note attributes this to the host's CRT0-argv-era boot handling; PCSX2 ground truth: FUN_001b36b8 runs once, in the boot baseline — host ran it six times, leaking ~3.9 MB → heap exhaustion → mesh ALLOC-FAILs). Root fix = make boot init-path selection retail-faithful at the `ee/hle` boot-environment layer (one path, as retail), resolving jointly with the 0x268440 row below | main.cpp:4184-4200 comment; A4 §4.3 row 70 |
| `0x268440` [SESSION-14] | Appends a call to the game's own type-registry populate (FUN_0025c6f8 with the "Particle" prefix) after the ELSE branch of FUN_00268440, because on host the ELSE branch (bit 0x10000 / rb-mode) runs and none of the 4 populate call sites fire — yet the scene parser later looks up the 4 type hashes | On PCSX2/retail the same rb-mode boot leaves the registry populated (the game works); on host the populate path never fires — an unlocalized host boot-flow divergence (flag state / boot ordering). Root fix = root-cause why the populate that fires under PCSX2 does not fire here, at the boot/loader layer — NOT by editing the branch. Note the patch interplay: the appended FUN_0025c6f8 call is one of the two RENDINIT invocations the 0x1b36b8 row frees — these two patches must be root-fixed **together** | main.cpp:7989-8020 comment (Sessions 13/14 enumeration); A4 §4.3 row 106 |
| `0x2ad110` [SESSION-15 Option B] dest-preservation | For oversized async reads (e.g. the 330 KB bullseye atlas through the 64 KB stream buffer), saves/restores the destination across the original completion body, because the host already delivered correct data to dest directly and the game's completion memcpy from the (stale) stream buffer would corrupt it | The host CDVD/loader HLE delivers read data **directly to dest**, diverging from retail's through-the-stream-buffer flow; the completion-time copy then re-copies stale bytes. Root fix = make the `cdvd/` delivery model faithful (deliver through the stream buffer, chunked as retail does — including the retail overflow semantics, §3.8); the preservation hack then deletes. The *hold branch* co-resident in this hook (completion-visibility gating, SOUND_P6 FIX-1b / LOADTIME NotePoll) is **not** part of this row — it reclassifies as HARDWARE/TIMING HLE (§3.6) | main.cpp:10063-10078 comment; A4 §4.3 row 136; SOUND_P6_IMPL.md; LOADTIME_IMPL.md |
| `0x2af0d8` FMV abort hold (`FmvAbortHold`, `games/<title>/hle/workarounds/fmv_abort_hold.{h,cpp}`; INTEGRATION_PLAN row 7.7, P7-ABORT-HOLD) | Wraps the title's scene updater FUN_002af0d8; each tick, when the player handle word `0x30a700` names a main-RAM SceMpeg context, its mode word is the RAW8 movie path (`FmvContextLayout::mode == mode_raw8`), its abort word (`abort_flag`, +0x15c) is set AND `MpegApi::ShouldHoldAbort(gctx)` (a started movie that has not genuinely finished), writes the abort word back to 0 before the updater reads it (FUN_002b0f68). Counter `run.meta fmv_abort_holds=` (coldboot: 2, one per intro movie). Inert once the movie genuinely ends (the predicate is false, the real teardown proceeds) and on every non-movie context | The title's load-loop pump FUN_002b0ae0 front-loads the movie file under the engine's INSTANT CD delivery: the refill callback reaches end of file while the player is still priming (two serves before the playing state), returns <0 and the pump latches `gctx+0x15c = 1`; the updater then tears the movie down after 2 pictures (`fmv_served_per_playback=2,2+`, BUG-045). On retail the refill is paced by the drive and the ring the demux fills, so end of file arrives with the last picture. The frozen OLD baseline ran with instant delivery AND this hold (main.cpp:12768-12796, unconditional), so the hold is part of the observable the baselines carry. RETIREMENT GATE: a paced CD-delivery model (DeliveryMeter Modelled) that keeps the refill callback from reaching end of file before the movie ends (INT-I73C-ROOT-FIX-OPTION: `sceMpegDemuxPss` consuming only what the stream callback's ring accepts — a design decision, with the title's `v0 == 0` retry ladder on `+0x14c` to model); retire when the coldboot leg serves 201/346 with the hold inert (`fmv_abort_holds=0`) | OLD main.cpp:12768-12796 `[FMV-ABORT-HOLD]`; OLD mpeg_hle.cpp:2537-2547 `ShouldHoldAbort`; title FUN_002b0ae0 @0x2b0bc4/0x2b0bd4 (the latch); INTEGRATION_IMPL I7.3-C (attribution), I7.7-A (landed); pins ctest `FmvAbortHold`, `RootHoldsFmvAbort` (coldboot `--frames 1500` → `fmv_served_per_playback=201,346+`, `fmv_pictures_served=547`, `fmv_abort_holds=2`); docs/bugs/BUG-045 |

**1 → HARDWARE/TIMING HLE:** the gated budget hook at `0x191bd0`
([S96-BUDGETEMU], main.cpp:3254-3611 + budget_math.h): emulates the frame-budget
predicate the game derives from hardware timing (synthetic base 8800 +
[S112-BOOTPHASE] phase classifier). A4 §4.3 carries two 0x191bd0 rows ([REV-2
2026-09-23 citation-fix] its rows 50 and 120 — rows 49/121 are 0x203358 and
0x2ad1a0); only the gated budget install reclassifies here — the companion
row is a chained read-only probe and is counted with the diagnostics. Already
modeled as the `budget_anchor` row of ARCH §4.7 DerivedClocks; home =
`ee/timing`. Records: CAVERN_BUDGET_IMPL.md, BLACKBOOT_IMPL.md (A2 registry row
PS2_BUDGET_EMU).
**Status after decision DP-8 (1) (I8.1-A, 2026-10-02): the wait seam at `0x191bd0` is RETIRED on this title.** The frozen
recompiler emitted `hle_TimerWaitMicroseconds` at the query's address (`recompiled.cpp` dispatch install), which modelled the
budget QUERY as a 1–1.7 ms WAIT answered by one `TickSync::Cause::Demand` tick = one T0 field per call (DP-8 class (1): 638
Demand ticks on the 130-frame coldboot leg, 4 per anchor frame). The composition root now re-installs the title's RECOMPILED
predicate `FUN_00191bd0` through `HeadroomQueryGlue` (`games/<title>/hle/glue/headroom_query_glue.{h,cpp}`)
BEFORE `LoaderSpinLift` captures/wraps the address, so the predicate runs over the slice-frozen T0 (`DerivedClocks::TimerCounts`,
a pure function of the field count by design — BUG-014/031/032): it answers 1 while the frame's quantum has headroom and never
advances a clock; the load-wait exits are the lift's (row above). `KernlTimer::TimerWaitMicroseconds` (row 18's seam provider)
stays in the tree and is unreached on this title (`run.meta headroom_query=recompiled`, `ticks=…demand=0`). Pins: ctest
`HeadroomQueryRecompiled` (a call through the dispatch at `0x191bd0` leaves the Demand count and `KernlTimer::LastDeadline()`
unchanged; v0 = 1 with headroom, 0 at one quantum), `RootHeadroomQueryRecompiled` (the process leg). Record: INTEGRATION_IMPL I8.1-A.

### 3.2 Manual-body runtime layer → EMULATION-BUG-WORKAROUND (retire; 243 distinct addresses)

`manual_switch_dispatchers.cpp` installs **235** addresses (222 machine-restitched
`fun_*_body.inc` bodies + 3 fully hand-written dispatchers — FUN_00203358
particle SM, FUN_002a13e0 bullseye SM, FUN_00255f30 text/decal render+tick with
VU0-macro sin/cos folded to std::sin/std::cos — + 10 sibling raw installs);
`anim_pose_hle.cpp` installs **9** native mirrors ([REV-2 2026-09-23
ledger-fix] 0x293ec0, 0x294300, 0x294480, 0x294b18, 0x297ba8, 0x297b40,
0x297d88, 0x297e68, 0x297cc0 — statement-for-statement mirrors of the
recompiled pose subtree with direct-memory + direct-call swaps, [R129-FMVHLE];
the install log prints "installed (ok/9)"). Only 0x294480 overlaps the manual
235 (main.cpp's install order runs the mirrors after the dispatchers so "the
native FUN_00294480 supersedes the manual body the same way the manual
superseded the generated one"), so the two layers cover **235 + 8 = 243
distinct addresses**.

The masked defects — both fixed at root already or by planned emission work:

1. **Legacy-recompiler translation defect** (the reason the bodies exist): the
   legacy engine could not general-emit these jump-table dispatchers. **v2 fixed
   this at the root**: the 143-function manual enumeration
   (switch_manual_enumeration.json) is a strict subset of the 235 install
   addresses (verified 2026-09-22: 143 ∩ 235 = 143), and v2 general emission
   covers all of them plus FUN_00177168's 1,490-line hand emitter
   (REFACTOR_COMPLETE.md: "The 143 hand-written manual dispatcher bodies are no
   longer required by the v2 path"; RECOMP_P3A_IMPL.md: FUN_00177168
   general-path body with its 23-target table @0x1772c0, oracle spot-sweep over
   13 fixtures + 50-sample + all 18 legacy-divergence findings; RECOMP_P4:
   corpus audit GAPS=0 over 1,642 site→target pairs).
2. **Emission-quality (performance) defect** (the reason the anim-pose mirrors
   and the fast-path swaps exist): std::map + std::function dispatch and the
   memory-probe gauntlet made hot recompiled clusters slow. Root fix = the ARCH
   §5.2 emission changes (flat dispatch tables, fastmem fast path) — after which
   a hand mirror is pure duplication.

Disposition: **retired per-address in MIGRATION Phase 2** (the D1 proof-gated
phase). Today the install layer is engine-independent — main.cpp:5177 calls
`install_manual_switch_dispatchers()` unconditionally, so even a v2 build runs
the hand bodies on the indirect-dispatch tier while direct jal calls run v2
emitted bodies. The retirement gate adjudicates each of the 243 distinct
addresses on the harness (v2 emitted body vs the winning hand body; at
0x294480 the mirror is the installed winner). A divergence is **never** a
reason to keep the body: it is either a v2 emission bug (fix the emitter — the
oracle family exists for exactly this) or a hidden behavioral fix smuggled into
a "re-implementation" (reclassify it explicitly into one of the other four
classes). Expected end state: **0 manual bodies**; `recomp_overrides.ini`, the
pin/pin-closure machinery, and switch_manual_enumeration.json (whose only
remaining consumer is ALL_MANUAL pin expansion, A4 §1.1) delete with them.

### 3.3 sce_hle registry → SDK/OS HLE (291 bindings)

`sce_hle_bindings.inc` header: "Unambiguous matches: 291 (skipped 38 ambiguous,
2 out-of-band); SDK band 0x002b6fd8-0x002d6880" — generated by
generate_bindings.py from crash_matches_full.json, itself produced by
`tools/sce_lib_finder` signature-matching the SDK 3.0.1 drop. **This population
is already signature-bound**, exactly the D2 rule; the out-of-band rejection
policy in the same file ("these landed in game code… binding one would…") is
the precedent that game addresses are never bound. Disposition: the bindings
become rows of the SDK-library binding manifest (ARCH §4.9), distributed to the
owning subsystems' `hle/` directories: libpad→`pad/hle`, libmc→`mc/hle`,
libcdvd→`cdvd/hle`, libmpeg+libipu→`ipu/hle`, libsdr→`sound/hle`,
libgraph→`gs/hle`, libdma→`dmac/hle`, libkernl/libscf/libdev→`ee/hle` ([REV-2
2026-09-23] the libkernl sceSif* rows' handler *bodies* home in `sif/hle` —
§3.5), netcnfif→`sif/hle`.

### 3.4 libc installs → SDK/OS HLE (4 bindings)

libc_hle.cpp: memcpy 0x2c6ce0, memmove 0x2c6d8c, memset 0x2c6e88, strlen
0x2c72d4 — all inside the SDK band, i.e. toolchain-library code, matchable by
signature like any sce lib (memcpy 0x2c6ce0 was never emitted by the recompiler
and its HLE is the shipping contract — libc_hle.cpp:37/52 comments). The
guarded-fallback semantics (any argument set the guard cannot prove safe falls
back to the captured original, [R138-LIBCHLE]) survive verbatim. Home:
`ee/hle/libc.*`.

### 3.5 sif_hle 0x181490 bypass (1 → workaround) and recompiler-injected sce seams (9 → 3 SDK rows + 5 registry-owned + 1 workaround)

[REV-2 2026-09-23 ledger-fix] This section previously counted "2 sif_hle
installs (sceSif RPC surface)" as SDK/OS HLE and conflated the two gs_hle init
handlers. Corrected against source:

**sif_hle.cpp → 1 workaround row (0x181490), 0 SDK rows.** The sceSif RPC
surface (hle_sceSifCallRpc, hle_sceSifBindRpc, hle_sceSifInitRpc, …) is *not*
an independent address-bound population: those handler bodies are invoked
through the 291 signature-bound registry rows (sceSifCallRpc 0x2d29a8 →
libkernl.cpp:112 `::hle_sceSifCallRpc(ctx)`, sceSifBindRpc 0x2d27c8,
sceSifInitCmd 0x2d1a50 — all in sce_hle_bindings.inc) and are already counted
in §3.3; the bodies home in `sif/hle` while their binding rows ride libkernl.
sif_hle.cpp's only independent address-bound override is the **0x181490
piracy-check bypass**:

| Patch | What it does | Suspected runtime defect (to fix at root) | Evidence |
|---|---|---|---|
| `0x181490` piracy-check bypass | Replaces FUN_00181490 (GAME code — 0x181490 is far below the SDK band 0x2b6fd8, so the D2 out-of-band rule forbids an SdkBind row) with "assume the check always passes": writes 0 to the "Piracy Check Failed" flag @0x0030a458. **Load-bearing and dual-engine**: under legacy the generated tree skips emitting 0x181490 (W16R1 HLE-replaced) and the host owns the `FUN_00181490` symbol; under v2 the engine emits a real body (nothing-lost rule) and `recomp_install_engine_hle_hooks()` (main.cpp:5154, [RECOMP-P3b]) re-installs the bypass through `update_function_impl` — 0x181490 is in the generator's k_gen_hooks frozen-contract list, and a failed reinstall aborts boot | On retail hardware the check runs and passes (retail discs boot); the bypass presumes the recompiled check would fail against our HLE'd CDVD/SIF surface. It predates any root-cause — nobody has observed the v2-emitted body actually run. Root fix = adjudicate under the harness: let the v2 body execute; if it passes against the current `cdvd/`+`sif/` emulation the bypass simply deletes; if it fails, root-cause what the probe reads and make the `cdvd/` surface satisfy it. End state either way: flag @0x0030a458 reads 0 with no patch | sif_hle.cpp "HLE Bypass for the 0x181490 piracy check" comment + `hle_piracy_skip_body`; main.cpp:5154; RECOMP_P3B_IMPL.md (engine-hook channel); W16 record (HLE-replaced) |

**Recompiler-injected sce seams → 3 SDK rows + 5 registry-owned + 1
workaround.** The gs-block emission (recompiler.cpp:1904-1998) binds 9
addresses. The 8 SDK-band ones, with their *actual* handlers:

| Seam addr | Handler emitted | Also a §3.3 registry row? |
|---|---|---|
| 0x002c1030 | hle_sceDmaSync | no — **seam-only SdkBind row**, home `dmac/hle` |
| 0x002c1190 | hle_sceGsSendImage | yes — sceIpuRestartDMA (libipu) |
| 0x002c12e0 | hle_sceGifSyncDma | yes — sceIpuSync (libipu) |
| 0x002c1ab0 | hle_sceGsResetPath | no — **seam-only SdkBind row**, home `gs/hle` |
| 0x002c1b50 | hle_sceGsSyncPath | no — **seam-only SdkBind row**, home `gs/hle` |
| 0x002c1bd8 | hle_sceGsResetGraph | yes — sceGsResetGraph (libgraph, delegates to the same body) |
| 0x002c1d78 | hle_sceGsSyncV | yes — sceGsSyncV (libgraph, delegates to the same body) |
| 0x002d20d0 | inline FlushCache/SyncDCache NOP (emitted lambda, "no data cache in recompiler") | yes — sceSifWriteBackDCache (libkernl) |

Where an address is registry-owned, `register_sce_hle_overrides()` runs last
and installs unconditionally, so the registry row is the live binding and the
seam is a superseded install mechanism — counted once, in the 291 (basis, §2).
Only the 3 seam-only addresses add SdkBind rows; all 8 collapse into ordinary
signature-keyed manifest rows in Phase 7/8 and the generation-time seam
emission dies with them (ARCH §5.2 "No override lanes" — the runtime
`update_function_impl` manifest install becomes the only function-replacement
mechanism). The same collapse swallows the generator's other redundant seams
at registry-owned addresses (0x2d1a50, 0x2d27c8, 0x2d29a8, 0x2d6618).

The 9th injected address, **0x0019aff0 → hle_SetupDisplayRegisters** ([REV-2]
previously misnamed hle_InitGraphics), is GAME code (outside the SDK band):
it replaces the game's own display-register setup routine FUN_0019aff0
(gs_crtc_class.h:13), reading the game's tile/display config struct
@0x3D4AE0 and falling back to hardcoded Crash-NTSC defaults when the struct
looks uninitialized (gs_hle.cpp:1000-1060) → **EMULATION-BUG-WORKAROUND**: it
exists because early hosts lacked working GS privileged-register/CRTC emulation
at boot. Suspected defect: incomplete boot-time GS init emulation (`gs/`
privileged regs + present). Root fix = let the recompiled FUN_0019aff0 run
against `gs/state/privileged` + `gs/hle/present`; the NTSC fallback then
deletes. Harness-adjudicated in Phase 2.

**hle_InitGraphics is dead code — its own disposition: DELETE (not a ledger
row).** Defined at gs_hle.cpp:128 targeting the graphics-init fn 0x0019fb18
(hardcoded game-struct writes: game_system+0x1C := 0x002f6498, globals
0x0030a788/0x0030a78a/0x0030a792, window-resize request), but no live
recompiler emission and no runtime install binds it — main.cpp references it
only in comments. It deletes in the Phase 1 dead-code purge; the window-creation
concern it embodied moves to `platform/` regardless.

### 3.6 Loader/completion-visibility HLE → HARDWARE/TIMING HLE (1 model)

The custom-loader HLE (HandleCustomLoaderRecompiled in sif_rpc_handlers.cpp,
LOADER_HLE_IMPL.md) plus the completion-visibility hold branch inside the
0x2ad110 hook (SOUND_P6 FIX-1b module-mirror hold; LOADTIME NotePoll) model
**when the game observes CDVD/IOP read completion** — the timing surface of the
IO hardware, per the project's HLE strategy for hardware polling/sync
functions. Home: `cdvd/hle` (completion + latency model, consuming `ee/timing`
clocks). The DVD seek/transfer latency model already in the CDVD engine
(hle_heap.cpp per A4 §4.1) is part of the same model.

**Wait-seam clause (I4.10-I / I4.10-K, DP-6 orchestrator decision (a), BUG-043).** Not every read record on
a loader fs object is host-served: the title stages STREAM records itself (`FUN_002ab160 → FUN_002ad9a8`,
type 0x7e; `FUN_002ad0b0 → FUN_002ada68`, type 0x7f) and they are served by the recompiled STREAM.IRX. On
hardware the IOP progresses concurrently and the SIF interrupt lands the client's RPC end-callback (which
ages the slot byte) while the EE spins in the wait; NEW's cooperative IOP runs at boundaries by design, so
the wait seam (`LoaderFileIo::WaitReadComplete`, already the replacement of `FUN_002ad1a0` — no new hook)
yields to it: guards = the pending flag up ∧ `Deps::pump_module` bound ∧ no host-served read in flight
(`LoaderModel::InFlight`) ∧ the slot byte module-owned (`Layout::module_slot_states` = {1, 2, 5}); each lap =
`pump_module` (the root binds `IopModuleTable::PumpFromParkLoop` → `EeRpcServer::Service` →
`PumpAfterEeService` → `RpcRouter::CompletePending`; frame-neutral — no frame counted — though the bridge's
drive may advance the IOP clock past one quantum) then the client's REAL `Ship(0)` (retail's per-lap status
call) then the title's OWN poll (`FUN_002adaf0`); bounded by `Layout::max_module_laps` (8); the flag is
lowered only when that poll answers done — no status or memory is written by the seam on this path.
Counter: `run.meta loader_waits_module=<waits>/<laps>`. Pins: `SifmanDmaStatComplete` (the kernel-facing
constant the module's worker polls, −1 = complete), `LoaderCompletionModule` (the seam over the real module;
DISABLED until the rig opens the slot's ring stream). Revert = unbind `Deps::pump_module`.

### 3.7 S8-ARGV → SDK/OS HLE (kernel boot environment, retail-faithful)

ps2_argv.h [S8-ARGV]: ground truth captured live from retail-mode PCSX2 (PINE,
investigate17): the OSD launches SLUS-12345 with argc=1,
argv[0]="cdrom0:\SLUS_123.45;1"; the game's parser FUN_0017d130 sees argc<2 and
falls back to its built-in cmdline "rb batch=Data\Crash". This is the
**kernel/OSD boot environment** our EE kernel HLE must populate — OS surface,
not a game hook. (History: the pre-fix host argv {"host:SLUS_123.45","rb"} was
itself an emulation bug — it dropped the batch token and booted "a hybrid mode
retail never executes", SHADOW2_IMPL.md loop-15 — the current value is the root
fix of that bug.) Home: `ee/hle/boot_env.*`; the PS2_ARGV legacy hatch dies in
the gate purge (A2 row, delete-losing-arm-keep-winner). Pinned by
tests/test_ps2_argv.cpp.

### 3.8 [KEY-REDIRECT] → EMULATION-BUG-WORKAROUND (1 mutator)

The every-256-writes pointer repair in memory.cpp's write gauntlet
(~L3149-3221, deliberately outside PS2_MEMWATCH — "repairs a game-progression-
critical pointer and stays unconditionally live", memory.h:150-160; A4 §0.8):
restores the level-load object's name-key (obj+0x500, "Levels\Earth\Hub\Beach")
that host-side corruption clobbers ("reads as 'd' by the time the manager list
populates, so the lookup fails forever" — [KEY500-WATCH]/[CASE6-LOOKUP],
memory.cpp; W10_GOAL.md/W10_IMPL.md location census). Suspected defect: the same
host-induced allocator/overflow corruption family as 0x181560/0x2ad110 —
PCSX2 keeps the key stable with no repair. Root fix: eliminate the corrupting
writes (boot double-init leaks + loader delivery divergence); interim quarantine
as a named module beside `ee/mem` with a workaround ledger entry; deletion
harness-gated.

### 3.9 RETAIL-BEHAVIOUR (1 row)

The oversized-read overflow: retail streams a 330 KB read through a 64 KB
stream buffer — buffer overflow semantics the retail game exhibits and survives
(the 0x2ad110 comment documents the host-side skip of "the overflowing
CB-STREAM-FIX mirror"). When the §3.1 root fix makes delivery faithful, this
overflow is **reproduced bit-faithfully, not patched**; an invariant test pins
the "checking memory card" string surviving to its lookup. No code row exists
for this class — that is the point.

### 3.10 Stale-display present stand-in → EMULATION-BUG-WORKAROUND (present-time; 1 row, carried from Phase 6 row L1) [IDOC-A 2026-10-01]

Row text carried verbatim from `docs/phases/PHASE6_IMPL.md` "Ledger deltas owed at integration" L1 (6D‴, Review 6D″ fix 2):

**Stale-display present stand-in.** OLD `host_app/graphics/opengl/opengl_backend.cpp:24339-24385` `[SCANOUT-STALE-OVERRIDE]`. NEW `engine/gs/hle/present/scanout_composer.cpp` `ScanoutComposer::PickSource` + `VramLayout::main_fb_fbp` (profile data, `games/<title>/renderer_profile.cpp`) + the named ages `kStaleDisplayFrames` (30) / `kFreshWorkFrames` (16). PS2 rule: the CRTC displays exactly DISPFB — the buffer at the DISPFB1/2 address in the DISPFB format, nothing else. Suspected defect (OLD's own comment `:24344-24350`): the game's per-frame `0x70000 → 0x0` display composite (one full-screen textured draw / local→local transfer per picture) is not reproduced on the host — "the drawn render-graph has zero draws to FRAME=0x0 in-scene" — so the true display buffer keeps a stale pre-scene image while the main buffer is composited fresh every frame; the stand-in presents the main buffer instead (with the DISPFB-format round trip through the mirror when the classes differ). Root-fix plan: locate the dropped composite when 6E/6F integrate the replay — a draw classification that discards the FRAME=0x0 draw, or a local→local transfer the transfer engine does not execute — and remove the stand-in once the composite reaches 0x0. Expiry gate: the 12-leg frames lane (the stand-in must be inert on every leg once the composite lands; it also moves the mirror — the stand-in deposit lands at the scanout base — so the mem lane is a secondary witness). Pinned today by `Present_DrawScanoutSource` (stale-display cases + probe P13).

Companion rows: HACK_LEDGER §9 L2 (`ScanoutFromVRAM` FMV hold — dormant, not written; reached only through this stand-in's round trip); `docs/bugs/BUG-063-stale-display-pick.md`.

## 4. Adjudication protocol

Every row of class EMULATION-BUG-WORKAROUND follows the same ladder, run under
the EQUIVALENCE_PLAN harness:

1. **Baseline**: OLD binary (patch live) hash streams on the anchor set + the
   scenes that motivated the patch (boot, bullseye/MC, island load).
2. **Root fix lands** in the owning subsystem; the patch is deleted in the same
   commit.
3. **Gate**: NEW (root-fixed, patch-free) vs OLD (patched) must be
   hash-identical on all legs. Identical ⇒ the patch was a faithful mask and the
   root fix is complete. Different ⇒ either the root fix is wrong (stop), or the
   patch itself introduced off-retail behavior — then the difference goes
   through the EQUIVALENCE §7 accepted-difference protocol with hardware-truth
   evidence (PCSX2/retail capture), and the corrected behavior gets an
   invariant test.
4. The workaround ledger (this file's §3 tables) is updated row-by-row; Phase 2
   (manual bodies) and Phase 7 (remaining behavioral patches) of
   MIGRATION_PLAN.md carry the ledger to zero.

## 5. The SDK-library binding manifest (replaces HleHookTable)

Specified in ARCH §4.9 as the **SdkBindingManifest**. One declarative manifest
whose rows are keyed by **(library, function signature id)** from the
sce_lib_finder match set — the address column is *derived at build time*, never
authored. Kinds: `SdkBind` (replace), `SdkWrap` (wrapper receives the original —
the guarded-libc shape), `GenProbe` (diagnostics builds only, a separate
diagnostics manifest). The former `GameFix` and `ManualBody` kinds do not exist;
there is nothing left for them to name.

## 6. [REV2 2026-09-23 D4-D8] Workarounds are NOT mods — the mod surface is disjoint from this ledger

User decision D6 introduces intentional modding hooks (ARCH §15:
`games/<title>/mods/manifest.toml` + the `engine/ee/mods/` ModHost over the v2
`update_function_impl` seam). This section states the boundary explicitly, in
both directions:

1. **The 250 EMULATION-BUG-WORKAROUND rows of §2 are NOT mods.** They are
   masks over defects of OUR runtime, and their one legitimate future is
   retirement at their root fixes under the §4 adjudication ladder (Phase 2
   for the 243 manual/anim addresses, Phase 7 for the behavioral remainder).
   None of them may be re-homed into a mods manifest, "shipped as an optional
   mod", or grandfathered as an enhancement — a workaround that survives its
   root fix is a root fix that failed, and the ledger stays open.
2. **Mods are never workarounds.** A mod exists only for modding/enhancement
   (widescreen, fps unlock, debug camera, asset replacement); anything a mod
   would need to make the game WORK is, by definition, an emulation defect —
   it enters THIS ledger as a new row with a suspected-defect column, not the
   manifest. The ModHost cannot express a workaround even structurally:
   detour targets resolve only against the per-title symbol map (adjudicated
   exports of `games/<title>/symbols/`), so the raw-address hook pattern D2
   retired cannot be resurrected through the mod channel.
3. **Correctness is certified mods-off only.** Engine correctness tests and
   the equivalence harness run with the ModHost empty; CI asserts the
   mods-off build is byte-identical with the mod host compiled in
   (EQUIVALENCE §1/§8, MIGRATION Phase 11 gate). A difference that appears
   only with a mod loaded is the mod's business; a difference that appears
   mods-off is ours.

Cross-references: ARCH §15 (manifest schema, ModHost duties, examples),
MIGRATION Phase 11 (delivery + proof gate), EQUIVALENCE §1 (contract
amendment), R17 (the erosion risk this section exists to prevent).
