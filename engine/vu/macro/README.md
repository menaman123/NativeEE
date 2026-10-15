# engine/vu/macro — the EE COP2 ↔ VuUnit hand-over

`VuMacroBridge` (`vu_macro_bridge.{h,cpp}`) is the provider of row 27 of the generated code's seam
roster: it DEFINES `Vu0Macro::Execute`, `Vu0Macro::ResumeIfWaiting` and `Vu1Window::Execute`
(declared in `engine/ee/recomp_abi/host_app/vu.h`; `g_vu0` / `g_vu1` live in `recomp_abi.cpp`), the
way `engine/gs/hle/sce/gs_sync.cpp` provides row 15. Any binary that links `ps2r_title_recomp` must
therefore link `ps2r_vu`.

PS2 rule: in macro mode the EE's COP2 registers ARE VU0's register file. `VCALLMS imm15` /
`VCALLMSR` (CMSAR0) start the VU0 microprogram at `imm15 * 8` synchronously with the EE; it runs to
its E bit — or parks in a CTC2-gated self-spin until the EE releases it (`ResumeIfWaiting`) — and
the EE then sees the results in its COP2 registers. The unit owns what the register block does not
carry across calls (flag pipeline, clock, Q/P latches, both memories), so the bridge is a register-
file hand-over and nothing more:

| Seam | Body |
|---|---|
| `Vu0Macro::Execute(ctx, start)` | `memcpy(unit.regs, ctx.vuRegs, 608)` → `vu0.Execute(start)` (the tier ladder; the SDK sin/cos fingerprint substitution lives in the tiers) → `memcpy` back |
| `Vu0Macro::ResumeIfWaiting(ctx)` | nothing when the unit is not suspended (the COP2 block stays the truth); else copy in → `vu0.ResumeIfWaiting()` → copy out |
| `Vu1Window::Execute(ctx, start)` | `vu1.Execute(start)` — VU1 owns its own register file; no copy |

Binding is process-wide (`Bind(&vu0, &vu1[, &vu0_registry])` from the composition root; `Unbind()`
for tests). Unbound, the bodies park exactly as the Phase-3 seams did —
`GuestPark::Forever("vu_micro_absent", pc, "vcallms micro pc 0x… (VU micro memory + interpreter
absent)")` — so an unwired binary still attributes its leg from `run.meta`.

Census: `Stats()` = `vu0_calls` / `vu0_park_resumes` / `vu1_kicks` (run.meta keys of the same
names); with a `Vu0Registry` bound, every VU0 entry is `NoteEntry`'d and the FirstKnown /
FirstUnknown / Periodic / fingerprint-mismatch notes go to `PS2R_LOG(VU, …)`.

Test: `tests/vu/test_vu_macro_bridge.cpp` (ctest `VuMacroBridge`).
