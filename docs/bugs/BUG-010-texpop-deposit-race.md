# BUG-010 — Single-frame garbage pop on the crystal: GFX deposits clobbered EE uploads mid-encode

| Field | Content |
|---|---|
| **ID** | BUG-010 |
| **Title** | [TEXPOP-FIX] SyncRTToVRAM's page-epoch snapshot was taken once at encode start; an EE upload landing during the multi-ms encode+readback was overwritten by rendered bytes — a two-writer byte race on `gs_vram` |
| **User-visible symptom** | One-frame garbage flashes ("pops") on the crystal and wall (crystal ROI pair-diff spikes >5× median, up to diff 20); ~5× worse at SCALE=1 (continuous shimmer). |
| **Scene / savestate** | `save_states/ps2state_slot0_f39807.ps2st`, PPM oracle over vf 40100-40399. |
| **Subsystem** | gs-vram |
| **Root cause** | The game streams tile-texture uploads into 0x2c0000+ (inside target 0x280000's footprint) every frame, draws with them, and the shadow-shuffle pass renders over the same band. Our GFX-thread `SyncRTToVRAM` deposits write rendered bytes into `gs_vram` with the `[VRAM-EPOCH]` last-writer page skip — but the `page_newer[]` epoch snapshot is taken ONCE at encode start, so an EE upload that lands during the multi-ms encode+readback is CLOBBERED. `CaptureSharedSnapshot` re-copying the window when hit widened, not closed, the race. |
| **PS2 rule violated** | GS local memory has ONE writer in stream order — last writer in stream order wins. Readbacks on real flow are synchronous in stream order: rendered bytes can never asynchronously destroy stream-newer CPU writes (PCSX2's rule, paraphrased in the record). |
| **Fix + hatch** | `[TEXPOP-FIX]` single-writer mirror (`gs_deposit_overlay.h`, hatch `PS2_GS_DEPOSIT_EEAPPLY` default ON; =0 legacy byte-identical): all GFX-side deposit encoders (EncodeRTToVRAM, SyncZToVRAM, DepositAlt16LaneToVRAM) write a 4 MiB SHADOW (per-8KB-page copy-on-first-touch), never `gs_vram`; the EE thread applies committed pages with per-page last-writer arbitration (`upload_epoch ≤ deposit_epoch` ⇒ commit, else drop). Post-refactor: this IS the VramImage design (ARCH §4.3 StageDeposit/CommitDeposits). |
| **Regression test** | Pop oracle 0 pops/300 frames on the anchor; later hardened by SNAPVERIFY mismatches=0 (BUG-026); page-state machine invariants as named tests (A3 invariants). |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_TEXPOP_IMPL.md` (§ [TP4]/[TP5]), `CAVERN_WALLTEX_RESEARCH.md` |
| **Triage tips** | Single-frame pops whose timing shifts when you attach a capture tool = a cross-thread write race (the readback stall perturbs the window). Any "snapshot the epochs, then write later" pattern is racy by construction — either re-check at write time per page, or route all writes through one owner thread. |
