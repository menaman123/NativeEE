# engine/vu/vu0 — VU0 (macro-mode) helpers

**Purpose:** engine-side VU0 pieces that are not title-bound: the SDK libvu0 sin/cos fingerprint substitution.

## Belongs here
- `lib_vu0_sin_cos.h/.cpp` — `LibVu0SinCos`: matches Sony's libvu0 dual-lane sin/cos microprogram by its first three
  instruction bundles at VU0 micro 0xF0 and substitutes libm (`VF31 = {sin, cos}(VF20.x), {sin, cos}(VF20.z)`, `TPC = 0x1C0`).
  Keyed by SDK instruction words only (no game address) — lint-clean, engine-side (PHASE5_PLAN §7.1 P5-LIBVU0-ENGINE).
- `vu0_registry.h/.cpp` — `Vu0Registry`: the VU0 macro-program CENSUS mechanism (descriptor kinds HLE / INTERP /
  UNIMPL, per-(unit, pc) lookup of an injected table, the first-call / sparse-periodic call marks, the 16-byte
  fingerprint check warned once per entry). Mechanism only: the rows are title data (`games/<title>/vu1_library/vu0_programs`);
  nothing dispatches on a `Kind` — the libsin/cos substitution is `LibVu0SinCos`'s fingerprint match. No NEW caller yet
  (the interpreter tier's entry hook wires `NoteEntry` → `PS2R_LOG(VU, …)` when the census is wanted; PHASE5_IMPL 5D part 3 HANDOFF).
- `vu0_trace.h/.cpp` — `Vu0Trace`: the VU0 macro-mode OPERAND census (diagnostics): for every VCALLMS of an armed
  micro pc (`--vu0-trace 0xPC[,0xPC...]`, a CLI knob, never a Config key) the COP2 block the program sees on entry
  (VF01..VF31, ACC, Q, I, the status/MAC/clip flags) and the block it leaves (VF01..VF31, ACC, Q, flags), raw bits + decimal,
  the EE's a0..a3/t0..t3/sp at the call (so LQC2/QMTC2 operands can be found by address), plus the program's bundles
  (up to 64, through the one after the E bit) on the first call / on a re-upload of the slot. `VuMacroBridge::SetTrace` binds it around the unit run
  (the composition root does so only under `PS2R_DIAGNOSTICS`; `<leg>/vu0_trace.log`, run.meta `vu0_trace=`).
  Mechanism only: the pcs are handed in. OLD's equivalent is the interp-arm-only `[VCALLMS-PROBE]` (`host_app/vu.cpp:5870`).
- (later) VU0 macro-mode interlock helpers if the EE integration needs them.

## Does not belong here
- Anything keyed by a title address, hash or string (`games/<title>/`).
- The VU0 register/memory record (`Vu1State` with `mem_mask 0x0FFF`, `engine/vu/vu1_state.h`) or the interpreter (`engine/vu/tiers/`).

## Transplanted from (OLD, read-only)
`host_app/vu.cpp:5540-5607` (interp arm) ≡ `:2570-2600` (cached arm, identical minus the log).
`host_app/vu_function_registry.{h,cpp}` (mechanism `:39-80` / `:66-88`; the 16 data rows → games) and the census block
`host_app/vu.cpp:5395-5520` (`[VUn-FN-FIRST]` / `[VUn-FN-CALLS]` / `[VUn-FN-UNKNOWN]` / `[VUn-FN-FINGERPRINT-MISMATCH]`).
