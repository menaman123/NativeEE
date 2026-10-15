# `engine/ee/mem/`

**Phase:** 3  
**Purpose:** 32 MB RAM + SPAD model, address translation, fastmem, MMIO router (per-range handler registration), ELF loader driven by TitleProfile.elf.

## Files (stage 3A)

| Pair | Class | Rule implemented |
|---|---|---|
| `quad_word.h` | `QuadWord` (POD) | the 16-byte LQ/SQ unit with the element views the generated code uses |
| `main_memory.{h,cpp}` | `MainMemory` | 32 MB; KUSEG/KSEG0/KSEG1 -> `addr & 0x1FFFFFFF` |
| `scratchpad.{h,cpp}` | `Scratchpad` | 16 KB; `(addr & 0x70000000) == 0x70000000`, offset `addr & 0x3FFF` |
| `address_map.{h,cpp}` | `AddressMap` | translate order scratchpad -> HW window (registered byte windows: IO image, BIOS built in; VU/IOP registered by their phases) -> RAM+mirrors -> registered extra regions (physical; a title's arena) -> BIOS mirror -> unmapped; straddle = unmapped |
| `fast_mem.{h,inl,cpp}` | `FastMem` | inline RAM fast path (`phys + width <= 32 MB` -> memcpy), quad ops mask `addr & ~0xF` first, slow path = `MmioRouter` for IO registers else `AddressMap` (unmapped reads 0, writes drop) |
| `mmio_handler.h` | `MmioHandler` (interface) | one Read/Write pair per register range |
| `mmio_router.{h,cpp}` | `MmioRouter` | `is_io_register` window `0x10000000-0x1000FFFF` + GS priv `0x12000000-0x12001FFF`; last registration wins; unclaimed pages counted for `run.meta unhandled_mmio=` |
| `mmio_sink.{h,cpp}` | `MmioSink` | the default handler: idle read values lifted from the original read path (12 rows, OLD lines cited per row; GS_CSR word = 0x8, everything else 0); writes accepted and dropped |
| `elf_loader.{h,cpp}` | `ElfLoader` | ELF32 LE EM_MIPS program headers; every PT_LOAD copied (`p_filesz`) and zero-filled (`p_memsz`); entry + ranges reported; nothing title-specific |
| `data_map_applier.{h,cpp}` | `DataMapApplier` | the title data-map grammar (`addr:0xVALUE`, big-endian digits stored little-endian, `#` comments); also the post-load patch list |

Flags consumed (PHASE3_PLAN section 5): `PS2_FASTMEM` NORM (fast path always on; no slow-only arm),
`PS2_MEMWATCH` DIAG (the probe gauntlet is not transplanted; a `core/diag` observer hook is the Phase-9 form),
`PS2_S15_WATCH` / `PS2_S8_MTXWATCH` DELETE, `GAME_FILES_DIR` SETTING (`Config.paths.game_files`, read by the
title composition in 3F).

## Belongs here

- AddressMap, FastMem, MmioRouter, MmioSink, ElfLoader, DataMapApplier

## Does not belong here

- the 3.5k write_slow switch (dissolved into registered handlers), hardcoded ELF path/section table, the data map itself (`games/<title>/data/`), the meta arena (`games/<title>/hle/workarounds/meta_arena`), workarounds (games/<title>/hle/workarounds/)

## Transplanted from (OLD, read-only)

- `memory.{h,cpp}`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
