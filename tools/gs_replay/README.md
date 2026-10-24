# tools/gs_replay

The GS replay tool over the NEW GS HLE layers (`engine/gs/hle/*`) and the GL backend
(`engine/render/backends/gl`). On this tree -- no renderer profile -- it is the renderer
THROUGHPUT harness:

```
gs_replay stream <in.gsstream> [--scale N] [--warmup N] [--max-frames N] [--csv f] [--json f]
                 [--dump-ppm DIR] [--dump-frames a-b] [--quiet] [--sync-capture] [--swap]
                 [--cd-barrier] [--no-grab] [--sync-deposit] [--phash] [--gate-trace]   (exit 77 = no GL)
```

`stream` replays a `.gsstream` consumer-stream capture (`engine/gs/hle/capture`: everything the
live consumer thread received -- draws with vertices, jobs with upload pixels, frames with the
CRTC snapshot, the EE image's local-memory writes between records, the image at open) through
the SAME objects the play composition runs on its consumer thread -- `DrawIrQueueConsumer` over
the generic `DrawPipeline` (no class registry, empty `VramLayout`, live snapshot) over `GlDevice`
(present capture armed and read after every present, the hidden window flushed) -- with no
producer, no pacing and no frame skip. Per frame it records the consumer's wall time from the
previous frame boundary's return to this one's, the draw count, the frames-lane hash
(`LastFrame().hash` at the replay's scale) and the backend census deltas; the summary gives
mean / p50 fps, p50 / p99 / max ms, the frames over the 12 ms bar and the chained `STREAM_HASH`.
The file is read by a prefetch thread so the measured thread does only the consumer's work.
The title's `verify/judge_render_throughput.py` judges the CSV (fps gates, identity of two
replays' hash sequences).

`--sync-capture` / `--swap` / `--cd-barrier` select the pre-lever forms (the synchronous
present readback, the hidden window's swap) and the NV_texture_barrier destination read (off by
default: not tile-coherent on Apple's GL-on-Metal) for A/B runs.

`--no-grab` is the play composition with no frames-lane reader (`grab_wanted` false: no display
grab, no lane hash -- the `hash` column reads 0, so it is a TIMING run); `--sync-deposit` turns
the deferred display deposit (the GPU read one frame late behind a fence) back into the
synchronous one for the A/B; `--phash` adds a `phash` column -- the hash of the PRESENTED window
image read after each present -- which is the identity oracle of a `--no-grab` run (compare it
against a grabbing run's; it costs ~3 ms a frame of harness time, hence opt-in); `--gate-trace`
prints the first deferred-deposit completions forced by a reader / writer of the owed span. The
`[GS-PIPELINE]` lines at the end carry the pipeline census (alpha merges, the deferred deposit).

Belongs: `gs_replay.cpp` (main), `stream_replay_driver.{h,cpp}` (StreamReplayDriver). The first
title's corpus-replay sources stay for that tree's driver (`census`, `gsrep_file`, `replay_diff`,
`skip_filter`; the `replay` / `census` / `diff` / `classdiff` grammar documented in their headers)
and are not built here.

Does not belong: any GS semantics (`engine/gs`), the capture format (`engine/gs/hle/capture`),
title data. Every knob is a CLI flag -- this tool reads no environment.
