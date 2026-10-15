# BUG-032 — Guest-visible work on wall-clock host loops: maintenance duties and pad slot stamps

| Field | Content |
|---|---|
| **ID** | BUG-032 |
| **Title** | The ~1 kHz CPU maintenance loop delivered deferred-ISO reads (firing cdvd ticks), ran SIF RPC end-callbacks, and resumed DMA on wall cadence; the SDL main loop stamped pad slot phase/seq bytes into guest RAM at wall rate |
| **User-visible symptom** | None in play. Harness-visible: paired-leg tick binning (BUG-031's second input), the boot-window loader-spin exit race (the spin's iteration count to delivery differed per rep, re-timing the 0x2b4738 loader-SM pump cadence), and pad slot seq counters differing by mem checkpoint 0 (measured 386 vs 397 stamps). |
| **Scene / savestate** | coldboot + any anchor; [DETW] watch on `0xBB05D8` named `WriteSlotPhaseBytes ← PadInput_Update ← main` via host backtrace. |
| **Subsystem** | ee-timing (secondary: iop, input) |
| **Root cause** | Three guest-visible duties lived on the CpuThreadFunc maintenance loop's 1 ms wall wait: (1) `ProcessDeferredIsoReads` — each pass decrements the 3-pass `delay_ticks` readiness floor, so readiness elapsed in WALL time racing the EE's own on-demand poll passes, and each delivering pass fired a cdvd-classified vblank tick; (2) swapped SIF RPC end-callback batches executed recompiled `FUN_001e6968` at wall points; (3) suspended-DMA resume. Independently, the SDL main loop called `PadInput_Update` per iteration, writing monotonic seq + phase bytes into the PADMAN client buffers in guest RAM. |
| **PS2 rule violated** | Profile timing contract: IOP/pad completions must arrive at EE-stream-deterministic points (on hardware they arrive via SIF interrupts the EE observes in its own instruction stream). |
| **Fix + hatch** | `PS2_MAINT_EEPACED` (default OFF = legacy verbatim; profile sets 1): the loop never touches the callback queue and skips all three duties; `maint::RunGuestDuties` + the pad stamp run at both EE frame-boundary sites (gs_hle/sif_hle, before the IOPK boundary pump) and the two boot/load park-loop pump sites. Ledger: SETTING. Files: host_app/maint_pump.h, main.cpp, gs_hle.cpp, sif_hle.cpp. |
| **Regression test** | Paired watch legs: pad counters and delta stream identical; audio lane reaches 50/50 windows identical on ~half of 300-vf pairs (was window-0 fork). Neutrality: cold/anchor fps and cuelag drift ON-vs-OFF within 0.2% (PHASE0_IMPL §9.7). |
| **Source record** | PHASE0_IMPL.md §9.3, PROFILE.md round-2 section. |
| **Triage tips** | Any host thread that calls memory::write into guest RAM is a determinism suspect — grep write sites by thread, then [DETW]-watch the addresses with host backtraces. "Every N ms" loops doing guest-visible work should be duty-moved to EE edges behind a pin, not slowed down. |
