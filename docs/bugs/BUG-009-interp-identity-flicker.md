# BUG-009 — Interp flicker: frame-interpolation paired draws by submission order, breaking phase under timing churn

| Field | Content |
|---|---|
| **ID** | BUG-009 |
| **Title** | [FL5-COHERENT] The interpolator's DrawKey included order-sensitive `kick_ord`, so instance pairing churned with submission timing — phase-broken lerps rendered as temporal banding/flicker |
| **User-visible symptom** | With `PS2_GS_INTERP=1`, periodic banded flicker fires (fire+8 frame cadence, green census); 7 of the user's 9 reported fires were scale-dependent false alarms of the detector itself (fixed-count threshold). |
| **Scene / savestate** | Fire states around vf 27232 (mined from fl4 legs); `[R199-DET]` deterministic re-fire on both binaries. |
| **Subsystem** | gs-interp |
| **Root cause** | The frame interpolator matched current-frame draws to previous-frame draws with a DrawKey that included `kick_ord` (submission order) and a FIFO within each bucket. Any submission-timing churn (load, MTVU, queue depth) re-ordered kicks, so instances paired with the WRONG previous draw — the lerp interpolated between different objects → phase-broken intermediate frames = flicker. Secondary: the green/weird fire census used fixed counts, firing at 1/4 the content fraction at SCALE=2. |
| **PS2 rule violated** | None on the PS2 side — interp is a host enhancement. The violated contract is ours: enhancement layers must key on CONTENT identity (handler/image/kick_pc/prim/nverts/fbp + geometry), never on submission order, because order is not deterministic across runs. |
| **Fix + hatch** | `[FL5-COHERENT]` in `gs_interp.h`, hatch `PS2_INTERP_COHERENT` (default ON; =0 legacy byte-for-byte): (1) drop `kick_ord` from DrawKey; (2) NEAREST-CENTROID one-to-one pairing within a key bucket (geometric disambiguation); (3) phase-coherence gate — if ≥8 draws AND >2 % of lerp-eligible draws are still phase-broken, skip the intermediate for that plan (retail-cadence degradation, no fixed-band deficit). Plus `[FL5]` scale-normalized census thresholds (fires mean the same content fraction at any scale). Post-refactor home: gs/hle/interp (identity-stamp matched, ARCH tree). |
| **Regression test** | `tests/test_gs_interp.cpp` (GsInterpCore — new-identity pop-in scenario drives the ≥8 floor); 17/17 frozen corpus + 1 new capture; fps_min improved ~23→~41 on the fire state in both duplicates. |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_FLICKER_IMPL.md` (§ [FL4]/[FL5], [FL5-REVIEW]) |
| **Triage tips** | Flicker only with interp ON = pairing bug, not a renderer bug: dump the interpolator's matched pairs at the fire frame and look for cross-object pairs. Any cache/matcher keyed on an ordinal is nondeterminism waiting to fire. Detector thresholds must be resolution-normalized or they lie at scale. |
