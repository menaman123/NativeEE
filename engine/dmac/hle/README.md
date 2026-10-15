# `engine/dmac/hle/`

**Phase:** 5 (the four title rows `memclr`/`sceDmaGetChan`/`sceDmaReset`/`sceDmaPutEnv` were pulled forward in Phase 4D'' as a trap release -- `libdma_bindings.{h,cpp}`); **integrated at I5.1-B** (the ELF-verified bodies over Phase 5's `Dmac`, the descriptor arena removed, the manifest `Bind`).  
**Purpose:** sce libdma SdkBinding rows + the DMA-idle sync seams (`sceDmaSync`, `sceGifSyncDma`).

## Status (integration, I5.1-B)

`libdma_bindings.h/.cpp` (`LibdmaBindings`, one instance over `Dmac&` + `MainMemory&` + `Scratchpad&`) carries the
bodies as `void (CpuContext&)` members and **`Bind(SdkBindingManifest&, LibdmaBindings*)`** registers the four
`libdma.a` rows (`memclr`, `sceDmaGetChan`, `sceDmaReset`, `sceDmaPutEnv`) through file-static thunks over the bound
instance (the manifest handler is a plain function pointer). The composition root calls `Bind` once (I5.1-C); until it
does, the title rows (`games/<title>/hle/sdk_bindings.tsv`) still resolve as manifest trap rows.
`sceDmaSync` / `sceGifSyncDma` stay libgraph-band seams (`games/…/hle/glue/sce_dma_glue.cpp`) whose rebind to
`SceDmaSync` / `SceGifSyncDma` is plan item 5.5.

**Register page contract.** Every register access goes through the Dmac's MMIO face (`Dmac::Read/Write`, 4 bytes), so
the observable is exactly a guest store's. The bodies issue every store the SDK does; **the Dmac page decides what
lands**: `D_PCR` and `D_STAT` are writable (status W1C, mask toggles) — `D_CTRL` / `D_SQWC` / `D_RBSR` / `D_RBOR` / `D_STADR` writes
are dropped, and so are the IPU TO (`0x1000B400`), SIF1 and SIF2 channel pages; SPR TO (`0x1000D400`) is writable
(`Dmac::ChannelForWrite`). Consequences recorded as findings (INT-I51B-*): `sceDmaReset` cannot latch `D_CTRL.DMAE`
(its return is the page's bit 0 read at entry, 0 on a cold page), its IPU TO register clears do not land; SPR TO and D_STAT clears now land, and
`sceDmaPutEnv`'s `D_CTRL` field encoding is computed but never observable through the page.

| Member | Body (the retail library, instruction for instruction) | Reference |
|---|---|---|
| `SceDmaGetChan` | `$a0` id 0..9 → `$v0` = the channel's **register block** (`0x10008000` VIF0, `0x10009000` VIF1, `0x1000A000` GIF, `0x1000B000` fromIPU, `0x1000B400` toIPU, `0x1000C000` SIF0, `0x1000C400` SIF1, `0x1000C800` SIF2, `0x1000D000` fromSPR, `0x1000D400` toSPR); 0 otherwise. Writes nothing. The SDK's `sceDmaChan*` table IS the register page (PHASE4_IMPL P4-LIBDMA-PARK evidence) | ELF `sceDmaGetChan` (`FUN_002c1ef0`), `sceDmaChan*` table `0x2e8010` |
| `SceDmaReset` | `$a0` mode: for every non-SIF channel (the reset-flag table excludes SIF0/1/2) SADR, CHCR, TADR, MADR, ASR1, ASR0 ← 0 in that order (QWC untouched); `D_STAT ← 0xFF1F`; `D_STAT ← D_STAT & 0xFF1F0000`; an all-zero env applied (`ApplyEnv`); mode 1 sets `D_CTRL.DMAE`; `$v0` = the DMAE bit read at entry | ELF `sceDmaReset` (`FUN_002c1f18`), reset-flag table `0x307b00` |
| `SceDmaPutEnv` | `$a0` = `sceDmaEnv*` (20 bytes: `u8 sts, std, mfd, rcyc; u16 pcr_hi, pcr_lo, sqwc_lo, sqwc_hi; u32 rbor, rbsr`): `D_CTRL.STS` (SIF0=1, fromSPR=2, fromIPU=3), `.STD` (VIF1=1, GIF=2, SIF1=3), `.MFD` (VIF1=2, GIF=3), rcyc 0 clears RELE / 1..6 sets RELE with RCYC = rcyc−1; `D_PCR`, `D_SQWC`, `D_RBOR`, `D_RBSR`; `$v0` = 0, or −1/−2/−3 (sts/std/mfd ≥ 10) / −4 (rcyc ≥ 7) with nothing written | ELF `sceDmaPutEnv` (`FUN_002c1ff8`), D_CTRL field tables `0x307b28/38/48` |
| `Memclr` | `$a0` dst, `$a1` n: n zero bytes through guest memory — RAM (KUSEG/KSEG0/KSEG1 mirrors, `MainMemory::Pointer(Phys)`) and the 16 KB scratchpad (`0x7000xxxx`, `Scratchpad::Pointer(Offset)`); a byte outside both is dropped and counted (`DroppedStores()`); `$v0` = 0 | ELF `memclr` (`FUN_002c1eb8`); OLD `sce_hle/libdma.cpp:66-79` (its `if (dst)` guard is not the ELF's — INT-I51B-MEMCLR-NULL) |
| `SceDmaSync`, `SceGifSyncDma` | `Dmac::FlushDeferredVif1()` (the deferred kick's observation point), `$v0` = 0. The reference's MTVU barrier + `IsRunning`-gated drain are the 5G worker's rows | OLD `gs_hle.cpp:936-957, :965-986` |
| `Bind`, `RowNames`, `ResetCalls` / `PutEnvCalls` / `GetChanCalls` | the four manifest rows as thunks over the bound instance (returns 4); the row-name table; run.meta evidence counters | pattern: `engine/pad/hle/libpad_bindings.cpp` |

Retired: the Phase 5 descriptor arena (`kChannelArenaBase = 0x01CFF000`, `kChannelDescriptorBytes`,
`DescriptorInitialised`) — OLD's invented RAM mirror page; accepted-difference row **P4-CKPT0-LIBDMA-TABLE**
(`docs/phases/PHASE4_IMPL.md:937, :1107, :1115`; the mask ranges `0x323288-0x3232f8` / `0x1cff000-0x1cff300` in
`verify/mask/coldboot_elf_text.ranges` mask OLD, not NEW). HACK_LEDGER libdma row: RETIRED.

Suite: `tests/dmac/test_dmac_libdma.cpp` → ctest `DmacLibdmaRows`: the ten bases + RAM/SPAD byte-identical, the reset
through the page (channels 0-4/8/9 zeroed, SIF 5-7 untouched, globals but PCR/STAT
unchanged, the DMAE-at-entry return), the env decode + the four rejections with nothing written, `Memclr` to RAM / SPAD /
outside, `Bind` (4 rows, thunks dispatch to the instance), the sync seams.

## Belongs here

- `libdma_bindings.h/.cpp` (`LibdmaBindings`): bodies, the manifest thunks (`Bind`), the DMA sync surface.

## Does not belong here

- The `Bind` call (composition root, `app/host`); title addresses (`sdk_bindings.tsv`); the title glue that
  forwards the generated TU's `hle_sceDmaSync` / `hle_sceGifSyncDma` names to the `sceDmaSync` / `sceGifSyncDma`
  rows (`games/<t>/hle/glue/sce_dma_glue.cpp`, plan item 5.5).

## Transplanted from (OLD, read-only)

- `sce_hle/libdma.cpp` (structure only; the bodies follow the ELF's libdma as the recompiled
  `FUN_002c1eb8/1ef0/1f18/1ff8` show them), `gs_hle.cpp hle_sceDmaSync`.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4; `docs/phases/INTEGRATION_PLAN.md` item 5.1.
