# Music and Movie storage composition

`hle.music_buffers=on` selects one host storage pool for music and both Movie
loans. It requires `hle.music=on` and `hle.movie_lifecycle=on`; ConfigValidator
rejects incomplete selections before boot. It is off by default.

Prerequisites: Bacon's Movie commits 4f38b6a and 227f73b (services equivalents
685b3aa and 0d23dad). Do not replay both versions.

Install order:

1. Existing GameLibraries file and Movie software consumers.
2. Existing MovieStream shared cursor.
3. SoundOutput bank/control/music owners.
4. MovieStorage installs both Movie buffer consumers and lookup against the
   same IsoFs used by that cursor.
5. Only after those consumers succeed, MusicBuffers can allocate and publish
   opaque storage handles.

Borrow uses MusicBuffers' finite control flush, preserving queued Stop before
Movie reuse. Both Prepare and OpenStream receive distinct real loan tokens.
Each closure returns its exact token. A failed view resolution still returns
the acquired token. No handle is treated as an IOP or SPU address.

MovieStorage's lifetime ends after game workers join and before Movie/Audio.
It closes software playback, returns both views, cancels control/music
producers, then detaches the empty pool. The pool has a fixed 2 MiB host
capacity; free capacity is actual remaining allocated storage, not a fabricated
readiness answer. This is not an emulated IOP heap and has no module reserve.

The comparison arm observes actual retained Movie file dispatch. Selected
metadata adds `music_buffers_binding` and allocation/resize/close/query,
sound/disc loan, return, error, fallback, used/free capacity and live-loan
counts. Existing Movie census distinguishes Break/Ready/Search/Sync,
heap-free and player producers from retained gameplay reclaim helpers.

## Parent validation

Changed root, GameLibraries and Config units compile with production Release
flags. Bacon's actual original-body Movie fixture passes 1,526 checks under
ASan/UBSan. The config/default/dependency test passes, and the generated
settings table is reproducible. Existing composed-service evidence remains
`services/service-persistence3.log`; new evidence is
`services/root-movie-storage1.log` and `services/root-movie-tests4.log`.
Dependency archives are retained uninstrumented inputs. No worker game or
shared build was run.

After the service-time trace pair, parent can compare this separate selection
with the same binary and source manifest:

```sh
python3 verify/run_leg.py --tree new --binary build-release/app/host/nativeee \
  --anchor coldboot --frames 1500 --state-every 60 \
  --out <work>/int_work/m_wave/music-buffer-pair/model \
  --repo $PS2R_OLD_ROOT --timeout 900 --stall-s 300 \
  --set hle.stream_files=on --set hle.movie_lifecycle=on --set hle.music=on \
  --set hle.music_buffers=off
python3 verify/run_leg.py --tree new --binary build-release/app/host/nativeee \
  --anchor coldboot --frames 1500 --state-every 60 \
  --out <work>/int_work/m_wave/music-buffer-pair/selected \
  --repo $PS2R_OLD_ROOT --timeout 900 --stall-s 300 \
  --set hle.stream_files=on --set hle.movie_lifecycle=on --set hle.music=on \
  --set hle.music_buffers=on
```

Run serially. Require full frame/audio keys and attributed differences, real
positive sound/disc loans, returns equal to both loan counts after Close,
zero active loans at the completed run, zero buffer/lookup errors, and zero
selected Movie Break/Ready/Search/Sync/heap-free producer execution. A game
reclaimer call is expected gameplay, not a retired model counter.

Checkpoint guards remain until M0 restores the saved logical clock and
installs the required ServiceState section, and M2 composes active Movie
state/loan references. Data-stream buffers still have retained consumers.
Effects and remaining STREAM controls are also open. This patch is production
composition ready for the parent gate; it is not accepted M3/M4/M5 retirement.
