# `engine/ipu/`

**Phase:** 7 (stage 7C: the register model + decode arithmetic; stage 7D: `hle/`)  
**Purpose:** the EE Image Processing Unit as the game sees it — the four memory-mapped
registers, the two DMAC-facing FIFOs, and the MPEG-2 intra decode / colour-space
arithmetic the IDEC and CSC commands run. `hle/` holds the sceMpeg / libipu FMV
pipeline (PHASE7_PLAN §2.2).

## Belongs here

| File | Class | Role |
|---|---|---|
| `ipu_device.h/.cpp` | `IpuDevice` | CMD / CTRL / BP / TOP in a caller-registered 64-byte window. Writable CTRL fields latch; RST immediately clears FIFO/status state. IFC excludes consumed quadwords, OFC updates on commands and drains. Synchronous BCLR, FDEC, VDEC, SETIQ, SETVQ, SETTH, CSC and PACK; BDEC remains a counted stub, IDEC a counted partial decoder. `CommandCount`, `UnsupportedCommandCount`, `LastUnsupportedCommand`, `UnsupportedVariantMask` expose lifetime census stats preserved across RST/BCLR. |
| `ipu_color.h/.cpp` | `IpuColor` | EE User's Manual 8.6 fixed-point CSC, nine-bit transparency thresholds, ordered dither, RGB16 translucency bit and closest-colour INDX4 quantization. |
| `ipu_vlc.h/.cpp` | `IpuVlc` | EE User's Manual 8.5.3 VDEC tables: address increment (including MPEG1 stuffing), I/P/B/D macroblock type, signed motion code and dual-prime vector. |
| `mpeg_decode.h/.cpp` | `MpegDecode` (+ PODs `BitCursor`, `VlcEntry`) | pure arithmetic over a caller-owned bit cursor: the MPEG-2 default intra quantiser matrix, zigzag scan, Table B-12 / B-13 DC size codes, `DecodeDcCoeff`, `DecodeAcCoeffs` (EOB / escape / ±1 form), inverse quantisation, the separable float reference IDCT, `DecodeBlock`, `DecodeIntraMacroblock`, `YCbCrToRgb32` (fixed-point BT.601, A = 0x80) and `CscPixelRgb32` (the CSC command's float-coefficient form). No globals |
| `ipu_settings.h` | `IpuSettings` POD | module-local knobs with the proven defaults: `decode_ahead_pictures = 64` (finding P7-LEDGER-CAP: the ledgered-DELETE flag's default-on arm is the baseline path, so the constant is kept), `sectors_per_pump = 16`, `max_sector_budget = 4096`. No `Config` field, no environment read |
| `hle/` | (stages 7D, 7D′, 7E) | sceMpeg / libipu FMV pipeline incl. `LibipuApi` — see `hle/README.md` |

## Does not belong here

- The IPU's absolute I/O address and its `MmioRouter` registration (composition root).
- The DMAC ch3 / ch4 channel logic (`engine/dmac`); only the FIFO face is here.
- The libipu / libmpeg SDK binding rows (`games/<title>/hle/`, integration) — `hle/` provides
  the handlers, not the installs.
- `Config` keys, `getenv`, diagnostics probe sites.

## Remaining hardware gaps

The title reachability evidence and acceptance tests are in
[`subsystem_completion_ipu.md`](../../docs/design/subsystem_completion_ipu.md).
HLE bypass is a title-specific completion strategy, not proof of complete IPU hardware.
OLD-memory identity is retired; the historical transplant references below are provenance only.

| Site | What is not modelled | Evidence / visibility |
|---|---|---|
| BDEC | No block decode / RAW16 output | Every issue increments opcode 2's unsupported count and the aggregate. Retail libmpeg decoder is bypassed by HLE entry points. |
| IDEC | Incomplete MBT/MBAI/DTD, AC VLC, scan/quantization/MPEG mode handling and approximate decode arithmetic; SGN+dither hardware clipping quirk remains unverified | Every issue increments opcode 1's unsupported count, even with default flags. Output now honours RGB16/32, thresholds and dither, but decoded pixels are not certified. The reusable driver has a direct IDEC branch; the inspected player selects MPEG2/RGB32. |
| FIFO scheduling | No asynchronous resume, eight-qword backpressure, FP prefetch buffer, or command interrupt | Short input is counted per opcode; CSC/PACK consume only complete macroblocks. BUSY still never persists after a call. Counts are a buffered model, not cycle-accurate hardware occupancy. |
| CPU FIFO ports / DMAC coupling | CPU FIFO window, input backpressure and source chains remain open | FROM=3 / TO=4 and channel 4 MMIO are corrected; output-ready callbacks retry partial drains. Real DMAC regressions cover direction and buffered stop/resume. |
| libipu init | Quantizer/CLUT seeding remains omitted by the HLE init body | The actual hardware commands now exist. Initialization calls are visible through `LibipuBindings::InitCalls`; no claim that skipped commands were executed. |
| RAW8 HLE | Writes final RGB16 planes instead of raw YCbCr macroblocks | `LibmpegBindings::Census` reports `raw8`; a regression pins the HLE boundary. The retail caller's subsequent CSC/DMA path still needs an integration decision. |

## Transplanted from (OLD, read-only)

| NEW | OLD `host_app/` |
|---|---|
| `IpuDevice` | `ipu.h:10-21` (commands), `:24-37` (CTRL fields), `:41-66` (API), `:96-98` (FIFOs); `ipu.cpp:102-123` (dispatch), `:125-136` (CMD read), `:138-148` (RST), `:150-165` (CTRL read + `ifc/ofc` recompute `:161-162`), `:167-171` (BP), `:173-183` (TOP), `:188-205` (FIFO face; `:191`, `:203`), `:208-216` (BCLR), `:220-236` (SETIQ), `:240-245` (SETTH), `:250-261` (FDEC), `:267-270` (BDEC stub), `:275-318` (CSC), `:323-371` (IDEC); access-width split `memory.cpp:1152-1174`, `:4455-4466`; register map `memory.cpp:618-627`, `:1059-1068` |
| `MpegDecode` | `ipu.cpp:13-56` (tables), `:71-99` (bit reader), `:376-391` (macroblock), `:394-417` (block), `:423-461` (DC), `:465-511` (AC), `:516-548` (IDCT), `:554-600` (YCbCr → RGB32); `:294-296` (the CSC pixel equations) |
| `IpuSettings` | `mpeg_hle.cpp:1228-1245` (decode-ahead cap default 64), `:1679-1680` (`kSectorsPerPump`, `kMaxSectorBudget`) |

Not transplanted: the global `g_ipu` (`ipu.h:113`, `ipu.cpp:10`) — the device is owned by the
composition root; the `g_logFile` narration — `PS2R_LOG(IPU, …)`; `ReadCmd64` / `ReadTop64`
(`ipu.cpp:131-136`, `:179-183`) — never reached in OLD (its 64-bit I/O reads are two 32-bit
register reads, `memory.cpp:1170-1172`), which `IpuDevice::Read` reproduces.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.7; `docs/phases/PHASE7_PLAN.md` §2.1; record `docs/phases/PHASE7_IMPL.md` §7C.
