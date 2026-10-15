# tools/gs_replay

The corpus replay oracle: replays a `.gsdl` draw-IR capture headlessly through the NEW
GS HLE layers (`engine/gs/hle/*`: DrawPipeline, class renderers, targets, texture
resolver, scanout grab) over the GL backend (`engine/render/backends/gl`) and emits
NUMERIC artifacts only -- per-frame framebuffer hashes, the chained `REPLAY_HASH`, the
`.gsrep` downsample artifact, the census -- in exactly the grammar of the frozen OLD
tool, so `gs_corpus/baseline/*.replay.txt` / `*.gsrep` are the oracle.

```
gs_replay replay    <in.gsdl> [--out f.gsrep] [--ds N] [--frames a-b] [--quiet] [--skip SPEC]
                    [--dump-ppm DIR] [--dump-frames a-b]                       (exit 77 = no GL)
gs_replay census    <in.gsdl> [--frames a-b] [--configs]                        (CPU-only)
gs_replay diff      <a.gsrep> <b.gsrep> [--tol N] [--pct P] [--max N]           (exit 3 = DIFF)
gs_replay classdiff <a.gsdl> <b.gsdl> [--frames a-b] [--seq]                    (exit 3 = DIFF)
```

Belongs: the replay driver (frame loop, hash chain, job map), the census grammar, the
`.gsrep` format, the diff verdicts, the `--skip` ablation filter. Every knob is a CLI
flag -- the OLD tool's `PS2_REPLAY_*` test variables became `--dump-ppm` /
`--dump-frames` / `--skip`; this tool reads no environment.

Does not belong: any GS semantics (they live in `engine/gs`), the recording reader
(`tests/lib/gsdl_reader`), title data (the renderer profile is linked, never defined here).

OLD source: `tests/gs_replay.cpp` (frozen, read-only) -- the loop order, the FNV chain
constants, the downsample rounding, the `.gsrep` layout and every printed line are
transplanted from it; the replay world (zero VRAM mirror never committed, texture
snapshots absent, upload payloads re-uploaded) is the same fidelity contract.

Note on the frame end: the OLD tool's present was the pending-writeback flush + swap;
it never ran the scanout composite. The driver uses `DrawPipeline::OnFrameEndGrabOnly`
for the same reason -- the display quad's mirror maintenance would deposit rows the
reference never deposited.

Note on the window: the pipeline's render-height fallback is seeded from the hidden
64x64 window right after construction (OLD `Init` read `SDL_GetWindowSize` into
`m_width/m_height`); a capture without a `SetWindow` job therefore renders its
non-scanout targets 64 rows tall, as the reference did (6F': five captures depended on it).
