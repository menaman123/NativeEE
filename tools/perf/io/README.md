# `tools/perf/io/` -- the I/O roofline microbenchmarks

Portable (no game data, no platform calls beyond the engine's `platform::host`).

| Tool | What it measures |
|---|---|
| `bench_spu_mixer [--pumps N] [--voices V] [--churn K] [--pitch P]` | `SpuCore::MixInto` at the title's pump shape (800 frames per boundary, up to 48 voices on looping ADPCM, KON/KOFF churn, volume and MVOL sweeps): ms per pump and ns per voice-sample for BOTH pump forms (sample-major oracle, voice-major fast), and the FNV-1a64 of every output float of each -- `IDENTICAL` or `*** DIFFERENT OUTPUT ***` (exit 1). The oracle for any mixer change. |

| `stretch_wav IN.wav OUT.wav RATIO [--seek N] [--quantum N] [--ratios CSV]` | `TimeStretch` over a 48 kHz stereo wav in mixer quanta at a fixed ratio (or a lane's per-quantum `ratio` column) and seek range: the output wav and the splice census (segments that were not the exact continuation, their mean match score and offset). Two seek ranges on the same guest audio = the offline arm of the FMV audio lane. |

Numbers are only comparable at a quiet host (`uptime` 1-min load <= 8) and on a performance core (the bench pins itself to the
interactive class like the game thread).
