# `engine/gs/hle/capture/`

**Purpose:** The `.gsstream` capture of EVERYTHING the DrawIR consumer thread receives, in stream order, so the GS
pipeline can be replayed ALONE (no EE, no VU, no game pace) with the inputs the live consumer saw: the throughput
harness of the renderer (`tools/gs_replay stream`, the title's `judge_render_throughput.py`).

The `.gsdl` recording (`core/diag/drawlist_recorder`) is the per-draw STATE oracle: it carries texture hashes, no
upload pixels, no local-memory writes and no CRTC snapshot, so a replay of it decodes zero VRAM. This capture is the
COST oracle: the upload payloads, the EE image's `VramOpLog` ops between records (the pipelined queue's split-image
records), the CRTC snapshot per frame and the local-memory image at open all travel, so the texture resolver, the
target refresher and the scanout composer do in the replay exactly the work they did live.

## Files

| File | Type | What |
|---|---|---|
| `gs_stream_format.h` | `GsStreamFormat` + PODs | Header / record layout, kinds (`kVramInit`, `kVramOps`, `kDraw`, `kJob`, `kFrame`). The `DrawRec` / `VertexRec` / `JobRec` layouts are the `.gsdl` ones (`gs/hle/ir`). |
| `gs_stream_recorder.{h,cpp}` | `GsStreamRecorder : DrawIrSink, VramMirror` | Writes each record and forwards; the upload job is held and written WITH its pixels at the next event (the queue's rule); the EE image's mirror calls are logged and written before the next event, then forwarded to the mirror the host had installed. |
| `gs_stream_reader.{h,cpp}` | `GsStreamReader` | Streaming reader: one record at a time (`Next`), draws lifted to `DrawIr`, frames to `JobRec` + `CrtcClassifier::Snapshot`. |

## Does not belong here

- The replay driver and its timing (`tools/gs_replay`); the queue itself (`gs/hle/queue`); title data.
