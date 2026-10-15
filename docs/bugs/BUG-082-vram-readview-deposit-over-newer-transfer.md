# BUG-082 — VramImage::ReadView overlaid a pending GPU deposit over a NEWER EE transfer (FIXED)

| Field | Content |
|---|---|
| **ID** | BUG-082 |
| **Title** | `VramImage::ReadView` substituted the pending shadow of every non-Clean page unconditionally, while `CommitDeposits` applies a deposit only when `upload_epoch <= shadow_epoch` and drops it when a newer EE write stamped the page. Between a staged deposit and the next observation boundary, a host→local transfer into a covered page was invisible to readers (texture decodes, snapshots): they read the older frame-buffer writeback instead of the transferred texels |
| **User-visible symptom** | none measured: the title frame hash is unchanged by the fix (f2519 `71aa69de…` before and after), so this is not the title's mechanism (BUG-081); the class is a texture streamed into pages a render target's writeback also covers (the title's 0x2f0000 / 0x280000 CT16 copies carry SCISSOR 1024 rows over the slots 0x300000–0x3effff) |
| **Scene / savestate** | unit: `Gs_Inv39_ViewHonoursNewerWriter` (RED before: `a reader after the newer transfer sees the transferred bytes, not the deposit` FAIL) |
| **Subsystem** | `gs-hle` (`engine/gs/vram/vram_image.cpp`) |
| **Root cause** | the reader rule and the commit rule disagreed; the header promised "a snapshot sees exactly the content a later commit would produce" |
| **PS2 rule violated** | GS local memory: the last writer in stream order wins (GS User's Manual 4.2) — a transfer after the draw that produced the writeback is what a later texture read sees |
| **Fix + hatch** | `ReadView` overlays the shadow only when `upload_epoch <= shadow_epoch` (the commit's predicate); no toggle (construction gap: the code now matches its own contract); frames lane unchanged (f2519 hash identical, `RootPlayPolicyInert` / corpus green) |
| **Regression test** | `Gs_Inv39_ViewHonoursNewerWriter` |
| **Source record** | `docs/phases/PHASE8_IMPL.md` §P8.21-A |
| **Triage tips** | compare `ReadView` against `Bytes()` after `CommitDeposits()` — they must agree page by page |
