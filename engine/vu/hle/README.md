# `engine/vu/hle/`

**Phase:** 5 (stage 5D)  
**Purpose:** the VU1 HLE MECHANISM ONLY — the program contract, the registry parameterized by a title's
`Vu1ProgramLibrary`, the image analyzer parameterized by a `Vu1FamilySignature`, the runtime tier
(`TryExecute` behind `VuTierDispatch::SetHleHook`), the kick lowering seam and the generic PACKED GIF
packet builder. Zero title constants: every family value (variant base, dispatcher op, exit stubs, exit
TPC, hashes, entry pcs, program names) arrives as data from `games/<title>/vu1_library/`.

## Files

| File | Class / types | Contract |
|---|---|---|
| `vu1_hle_contract.h` | `Vu1HleInput`, `Vu1HleOutput` (+`DataWrite`), `Vu1HleFn`, `Vu1ImageInfo`, `Vu1LoadedAt`, `kVu1HleMicroSize/DataSize` (PODs) | what a native program receives (memories, VIF1 TOP/TOPS/ITOP/ITOPS, entry regs, image hash, the image analysis incl. the loaded-program layout -- null when the caller has none) and produces (raw kicks in order, final data writes, optional full register file); `false` = DECLINE |
| `vu1_family_signature.h` | `Vu1FamilySignature` (POD) | `{variant_base, dispatcher_op0, exit_stubs[2], family_exit_tpc}` — fields only |
| `vu1_library_entry.h` | `Vu1LibraryEntry`, `Vu1LoadedProgram`, `Vu1ProgramLibrary`, `kVu1AnyEntry` (PODs) | one entry `{image_hash, variant_hash, name, start_pc, fn, oracle_case_ids, count, layout_hash}` (the trailing `layout_hash` = a layout-keyed row); the library = signature + entries in REGISTRATION ORDER + the loaded-program catalog `{code_hash, size_bytes, name}` (the title's loadable microprograms, hashes only) |
| `vu1_registry.h/.cpp` | `Vu1Registry` | `Lookup(image, variant, layout, pc)` / `Lookup(info, pc)`: pc filter → first exact image → first variant → first layout → wildcard; `Append` (the harness reference); `Entries`, `DistinctVariantHashes` (variant keys only), `LayoutEntries` |
| `vu1_image_analyzer.h/.cpp` | `Vu1ImageAnalyzer` | image hash (`VuImageHash::Fast`), extent (last non-zero bundle), prefix/variant hashes at `variant_base`, the family prefix LEARNED from the first dispatcher image per instance, the dispatch chain (≤64 nodes, ends at an exit stub); with the library's catalog the LAYOUT walk: from `variant_base`, the catalog program whose code hash matches at the position, advance by its size, until none matches → `layout` (pc, program index), `loaded_end`, `layout_hash` = `Fast(micro[variant_base .. loaded_end))` — the key that ignores the stale bytes earlier uploads leave past the last loaded program (the variant hash does not: 40 new variant hashes for 14 known layouts at the Mecha-Bandicoot boss); `ShortHash` |
| `vu1_hle_runtime.h/.cpp` | `Vu1HleRuntime` | `Hook`/`TryExecute`: image-hash memo on (micro generation, external epoch), analyze memo per image (`kMemoLimit` 512), lookup (image → variant → layout), run with `Vu1HleInput::info` = the analysis (a program keys its sub bodies on the layout, not on whole-image pins); `SetMissCapture` (the `--vu1-miss-cases` knob: the first N kicks of every missed kind as `Vu1Case` files); data writes applied BEFORE kicks; kicks → `KickLowering` one slot each; regs installed wholesale or `TPC = family_exit_tpc`; `SetVifTops` (the unit carries TOP/ITOP only); `SetIdentityObserver` — before each kick's submit the observer receives `KickIdentity{handler_name, image_hash, kick_pc, kick_ord}` and null after the run's last kick (the DrawIR identity stamp's source, invariant 28; observation only); `census()` runs/misses/declines |
| `kick_lowering.h/.cpp` | `KickLowering` | one kick → one arbiter slot in program order; direct delivery (`submitDirectOwned`) for the single-tag `{ST,RGBAQ,XYZ2/XYZF2}` geometry packet and for the single-tag A+D register block (`DispatchGifRegister`, the parser's own dispatcher); raw PATH1 `submitOwned` for every other shape, censused per producing program (`Origin`, `unsupported_by_program`) |
| `gif_packet_builder.h/.cpp` | `GifPacketBuilder` | PACKED GIF packet emission (tag with NLOOP patched at `EndTag`; PRIM/RGBAQ/ST/UV/XYZ2/XYZF2/A+D layouts); consumers = tests and tools (the title library lifts its packets from data memory) |

## Belongs here

The contract, the lookup rule, the analysis rule, the tier ladder step, the lowering seam, generic GIF emission.

## Does not belong here

Programs, registration rows, image/variant hashes, entry pcs, the family constants (`games/<title>/vu1_library/`);
the GIF packet decoder / differ (`tools/vu1_replay/gs_packet_diff`); the typed-op projection (`tools/vu1_replay/gs_direct_ops_check`).

## Transplanted from (OLD, read-only)

- `host_app/hle/vu1_hle.h:28-108` (contract), `host_app/hle/vu1_hle.cpp:9-91` (registry + analysis),
  `host_app/hle/vu1_hle_runtime.cpp:160-423` (`TryExecute`; the raw tier of its kick ladder),
  `host_app/hle/gs_packet.h:351-403` (`Builder`).
- Flags applied: `PS2_VU1_HLE` → `VuSettings::hle_enabled`; `PS2_VU1_HLE_HASHEPOCH` → memo unconditional;
  `PS2_VU1_HLE_DIRECT` / `PS2_GS_DRAWIR` → the `TryLower` seam (raw tier written); `PS2_VU1_HLE_DEBUG` / the
  stderr censuses → not written.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Zero game constants (`tests/lint/lint_engine_purity.py` + the stage G8 grep).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4, `docs/phases/PHASE5_PLAN.md` §2.4 / §3.3.
