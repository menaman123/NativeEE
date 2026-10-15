# BUG-047 — DMAC interrupt status/mask: `D_STAT` CIM bits are never set, so DMAC handlers never dispatch and INT1 is never asserted

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-047 |
| **Title** | DMAC interrupt status/mask: `D_STAT` CIM bits are never set, so DMAC handlers never dispatch and INT1 is never asserted |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit); the title's `AddDmacHandler` callbacks (if any were armed) would never run |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` (primary); `ee-kernel` (secondary: `_EnableDmac` syscall) |
| **Root cause** | OLD's `_EnableDmac` returned 1 on every call and never changed `D_STAT` (PHASE5_IMPL `P5-5F-ENABLEDMAC-RETURN`), and OLD never asserted INT1 from the DMAC — `CheckInterrupt` was polled only at `sceSifSetDma` (`P5-5F-INT1-SLOT`). With CIM always 0 the channel-completion CIS bits never meet an enabled mask, so no DMAC handler dispatches. NEW keeps the observable: Phase-3 `SyscallTable::ToggleMask(dmac_mask_)` is NEVER mirrored into `Dmac::globals().stat` (checklist rule `:2041`), and the `InterruptRaise` slot fires from `CompleteChannel` iff `CIS & CIM ≠ 0` — never on a cold boot. |
| **PS2 rule violated** | EE User's Manual `D_STAT`: `_EnableDmac(ch)` sets `CIM<ch>` (writing 1 to a CIM bit toggles it) and returns the previous enable state; when `CIS<ch> ∧ CIM<ch>` the DMAC raises INT1 (COP0 Cause IP3) and the kernel runs the registered DMAC handlers. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field. Integration decides whether to bind the `InterruptRaise` slot (binding changes nothing while the mask is 0). |
| **Regression test** | ctests `DmacRegs` / `DmacChainWalk` (globals + completion observables); the Phase-3 syscall-table pins for `_EnableDmac`'s return |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-DMAC-MASK (+ P5-5F-ENABLEDMAC-RETURN, P5-5F-INT1-SLOT)`; 5F `:1846-1847`, `:2041` |
| **Triage tips** | If a title waits on a DMAC-completion handler (semaphore signalled from an `AddDmacHandler` callback) and hangs — check `D_STAT` CIM in a memory dump (`0x1000E010` high half) and whether `CompleteChannel` ever raised; the fix path is CIM toggling in the syscall + INT1 delivery, both behind an accepted-difference row. |
