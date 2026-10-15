# M_CUTOVER — the default flips (CUTOVER.md, 2026-10-08 →)

One stage per flip on `main` (single writer). Acceptance per flip: the one-line default + settings
table; explicit old-value pins; Release 0/0; the full suite ALONE; `RootPlayModeSmoke` +
`RootMovieLifecyclePlay` (-j1); the 4600-frame deterministic coldboot leg frames/audio IDENTICAL to
`int_work/m_series/cutover/REFERENCE`; the replaced model's census counter ZERO on the leg and both
smokes with the owned counters positive; OLD-bug pins green; the slowspot bench. A failing criterion
reverts the default in the same stage. Reference lanes: `ref0` = pre-flip `main` 726a5e26
(`frames.fhash 4600,0xcdb1ff109c38971c`, `audio.ahash 766,0xfd605d29418b3536`,
`mem.mhash 4560,0xfcc35c8f67bdd7b0`); after flip 4 `REFERENCE -> flip4/leg` (frames/audio identical to
ref0; `mem.mhash 4560,0x6b1820af6773930c`); after flip 5 `REFERENCE -> flip5/leg` (frames/audio identical to
ref0; `mem.mhash 4560,0x7ceb2e39c0fb6a71`).

| # | Key | Verdict | Commits | Census (leg / smokes) | Leg vs previous reference | Slowspot ms/frame |
|---|---|---|---|---|---|---|
| 1 | textures-at-load = `hle.graphics_sprites` | **REVERTED** — census not zero | flip 1cd2a13e; revert (next commit on `main`) | `Graphics.sprites.model_calls` = 21928 / 425 (play smoke) / 547 (movie smoke); owned `graphics_sprites.replaced=0`, `graphics_textures=loaded=0,destroyed=0,model_calls=287,failed=0,uploaded=0,restores=0`; `graphics_refusals_textures=total=40,missing_resource=0,unsupported_state=40,ordering=0,faults=0,first=frame:60/source_frame:0/n:1/producer:0x1bc920/detail:3/reason:unsupported_state,last=frame:60/…/n:40/…`; `graphics_refusals_sprites=total=0`; census summary `counters=97 nonzero=78 zero=19 absent=0` (ref0: 73/19/5 — the 5 GraphicsRuntime rows appear) | `diff_legs: IDENTICAL (lanes=frames,audio, masked 0 B, coalesced 0)`; `mem.mhash: identical (77 lines)` (`--allow-build-diff`: build ids 11:30:07 vs 12:34:18); first keys `1,` … last `4600,0xcdb1ff109c38971c` / `766,0xfd605d29418b3536` | 54.58 (total 56.73, p50 54.31, p95 57.50) on the reverted binary at load_1m 5.11 — above the BASELINE rule (<4.0 at launch), so NOT a baseline; ref `verify/bench/BASELINE.tsv` 50.95; ratio 1.071 (threshold 1.10, report only) |
| 2 | Graphics bridge = `hle.graphics_frame` | **REVERTED** — acceptance leg aborts (frame 63); smoke aborts; census not zero | flip e57a3281; revert d48f7e90 | leg: none (aborted before the census; `--frames 62` probe: `Graphics.frame.send.model_calls` = 1, setup/clear/layout/interrupt = 0, `graphics_frame_output=completed=0,setups=1,layouts=0`, `graphics_refusals_frame=total=1,missing_resource=0,unsupported_state=0,ordering=1,faults=0,first=frame:61/source_frame:0/n:1/producer:0x181968/detail:7/reason:ordering,…,first_fault=none`); play smoke: `Graphics.frame.send.model_calls` = 1 (same refusal), others 0, owned `graphics_frame_output=completed=58,setups=59,layouts=58`; movie smoke: aborted (no run.meta) | no 4600 comparison (abort at frame 63: `Graphics ordered delivery failed`); the 63 frames / 10 audio windows written are identical to ref0's prefix (last `63,0xa741b33a5c037124`); mem advisory: first divergent key 0 — `0,0x2cfcc4f064d14998` vs ref0 `0,0x03018fa3110d34b5` (also key 60; same key-0 value on the `--frames 62/63` probes and the play smoke — deterministic) | 52.74 (reverted binary, load_1m 5.86 — report only; flip1 54.58 @5.11; BASELINE 50.95 @3.06) |
| 3 | native Strip primitives = `hle.graphics_strips=all` | **REVERTED** — acceptance leg aborts (frame 118: a selected strip draw's delivery faults); both smokes abort | flip e3435827; revert 509d58b1 | leg: none (aborted before the census); `--frames 117` probe: `Graphics.strip.{plain,tinted,placed,placed_tinted}.model_calls` = 0, owned `graphics_strip_output=captured=8,completed=0,failed=0`, `graphics_service=strip_calls=8,replaced=8,empty=0,owned_strips=8,model_producers=0,fallback=0,interior=0,linked=0,unsubmitted=8,…`, `graphics_refusals_strips=total=0,…,first=none,last=none,first_fault=none`; diagnostic census of the faulted `--frames 118` run (uncommitted local build): `graphics_strip_output=captured=16,completed=0,failed=1`, `graphics_owned_output=queued=16,completed=0,pending=15,failed=1`, `graphics_service=…,linked=1,…,queued_groups=1,completed=0,recorded_work=16,completed_work=0,ordering_errors=0,failed_groups=1`; smokes: aborted (no run.meta) | no 4600 comparison (abort at frame 118: `Graphics ordered delivery failed`; first fault `Graphics output delivery failed` = `GraphicsService::EndWalk` Poll `DeliveryFailed`); the 118 frames / 19 audio windows written are identical to ref0's prefix (last `118,0x241c366e84655d6c`); mem keys 0 (`0x03018fa3110d34b5`) and 60 (`0x1929f3f8e1ad3402`) identical to ref0 | 51.57 (reverted binary, load_1m 6.74 — report only; first run 57.17 @6.47 kept as `bench-underload-1`; flip2 52.74 @5.86; BASELINE 50.95 @3.06) |
| 4 | MovieDecoder lifecycle = `hle.movie_lifecycle` | **ON (DEFAULT)** — all criteria held | flip e62dfa68 | leg: `ipu_commands=bclr=0,…` (ref0 bclr=4), `ipu_operations=init=0,sync=0,stop=0,restart=0` (ref0 2/0/3/0), `libipu_calls=init=0,…,stop=0` (ref0 1/3), `movie_audio … model_pops=0` (ref0 32200), `movie_decoder … audio/stream/input/audio_playback_original_calls=0`, `movie_stream … prepare/open/close_original=0` (ref0 3/3/3), `fmv … retained_init_calls=0` (ref0 1); owned `movie_decoder initialized=1,started=3,closed=3,audio_feeds=8566,stream_reads=6246,demux_windows=6246,audio_started=3,audio_stopped=3`, `movie_stream prepared=3,opened=3,closed=3,reads=6302`, `movie_audio selected=1,gain_changes=6,peak=32767,pops=32200`; play smoke: the same zeros (`movie_audio pops=1547 model_pops=0`, no movie); movie smoke: zeros + `started=2,closed=2,pictures=547,audio_feeds=1953`, `movie_audio pops=9842 audible_frames=865600`; catalog (97 rows) identical to ref0 `nonzero=73 zero=19 absent=5` | `diff_legs: IDENTICAL (lanes=frames,audio, masked 0 B, coalesced 0)` (4600 keys paired, 767 audio lines; `--allow-build-diff`: build ids 11:30:07 vs 16:21:28); last `4600,0xcdb1ff109c38971c` / `766,0xfd605d29418b3536`; mem advisory FIRST DIVERGENT key 180 `0xd3593dcced9e5827` vs ref0 `0x99d6c871e93314f8` (keys 0/60/120 identical; the retired libmpeg/IPU bodies' guest-RAM writes at the first movie's Initialise) | 50.47 (total 52.03, p50 50.31, p95 53.35) on the FLIP binary at load_1m 3.65 (< the BASELINE launch rule 4.0); ref BASELINE 50.95 → ratio 0.991; flip 3 revert 51.57 @6.74 |
| 5 | MathLibrary helpers = `hle.math_helpers` | **ON (DEFAULT)** — all criteria held; RootAnchor* 6/6 (the round-2 revert class) | flip 0fdde3fb | leg: all 61 math catalog rows `*.generated.*` = 0 (REFERENCE: 11,636,915 retired executions — `rotate_vector=1778261`, `transform_vector=1421080`, `multiply_matrices=950895`, `rotation_matrix=191301`, `transpose=343667`, `transpose_rotation=339825`, `triangle_plane=77715`, `start_edges=343686`, `finish_edges=112224`, `cos_sin16=657733`, `angle_from=727273`, `turn_toward=66606`, `inverse_length4=19736`, `rotation_xyz=18613`, `rotation_yz=18572`, `tangent=10608`, `angle_pairs=2156844`, `triangle=84798`, `points=7080`, `transformed_points=571944`, `transform=828478`, `animate=718948`, …); owned = retired entry by entry (`math_library rotate_vector=1778261,transform_vector=1421080,multiply_matrices=950895,identity_matrix=101479,…`, `trig_math cos_sin16=657733,…,angle_from=727273`, `collision_math start_edges=343686,finish_edges=112224,…`, `animation_math angle_pairs=2156844`, `bounds_math triangle=84798,points=7080,transformed_points=571944`, `joint_library transforms=828478,matrices_written=828478,done_returns=0,animations=718948,blended_animations=1821,angle_reads=2156844,rotations=0`); `identity_matrix` −5 (101484→101479) = the zero-angle axis-matrix branch's host reuse (no nested dispatch); `generated_continuations/unsupported_inputs/generated_interiors/unsupported_entries/refused_*/refusal_*/faults` all 0; unexercised both arms: clipping, rotate, trig pitch/roll/matrix_x/*_axis*/snap_yaw, sixteen_groups_range; play smoke `cos_sin16=6604,angle_from=3313,matrix_y=48,matrix_z=62,transform_point=4396,identity_matrix=97`, movie smoke `cos_sin16=44020,angle_from=32929,matrix_z=182`, all `generated_body_*=0`; restored anchor f57292 (300 frames): `transforms=38008,matrices_written=33808,done_returns=4200,animations=37916,refused_*=0,faults=0`, `generated_body_*=0`; catalog summary leg `nonzero=25 zero=67 absent=5` (36 non-math rows = flip 4) | `diff_legs: IDENTICAL (lanes=frames,audio, masked 0 B, coalesced 0)` (4600 keys paired, 767 audio lines; `--allow-build-diff`: build ids 16:21:28 vs 17:36:51); last `4600,0xcdb1ff109c38971c` / `766,0xfd605d29418b3536`; mem advisory FIRST DIVERGENT key 1080 `0x3d47b409b9189dde` vs flip4 `0xd27d7e407152c179` (keys 0…1020 identical; same first key as the round-2 same-binary pair — dead stack bytes of the retired bodies vs the owned adapters) | 50.07 (total 51.54, p50 49.80, p95 51.82) on the FLIP binary at load_1m 2.18; ref BASELINE 50.95 → ratio 0.983; flip 4 50.47 → 0.992 |
| 6 | StreamLibrary file jobs = `hle.stream_files` | **REVERTED** — the frozen legacy anchors refuse to load (`selected service deadlines require GCLK`); census not provable | flip 16c409bd; revert e91e5daa | leg: `stream_library=file_opens=4,sample_opens=2,reads=170,deliveries=170,polls=636,waits=24,audio_reads=120,audio_registrations=1,cancelled=0,stale_deliveries=0,errors=0,fallbacks=2645` (REFERENCE `file_opens=0,…,reads=0,deliveries=0,polls=0,waits=0,audio_reads=0,…,fallbacks=3421`), owned `file_schedule=issued=170,delivered=170,consumed=170,cancelled=0,deferred=0,poll_delivered=150,boundary_delivered=0,wait_delivered=20,wait_parks=91,overruns=0,rejected=0`, `loader_slot_status=0/0` (REF 170/170), `stream_rpc_calls=13199/0` (REF 13332), `sdk_rows_hit=472536` (REF 473203: `sceSifCallRpc 13334→13201`, `sceSifCheckStatRpc 13332→13199`, memcpy/memset/strlen −58/−85/−258); play smoke `stream_library=…,reads=23,deliveries=23,polls=42,waits=22,audio_reads=42,…,fallbacks=253`, `file_schedule=issued=23,delivered=23,consumed=23`; movie smoke `reads=24,deliveries=23,polls=51,waits=22,audio_reads=42,fallbacks=253`; `fallbacks` ≠ 0 by construction (bank-start entry `0x2ada68` always runs the original after `PrepareAudioRead`; IsReading/WaitRead on bank requests fall back; no per-entry split) | `diff_legs: IDENTICAL (lanes=frames,audio, masked 0 B, coalesced 0)` (4600 keys paired, 767 audio lines; `--allow-build-diff`: build ids 17:36:51 vs 18:25:18); last `4600,0xcdb1ff109c38971c` / `766,0xfd605d29418b3536`; mem advisory FIRST DIVERGENT key 60 `0xa9b29855e0b3341e` vs flip5 `0x1929f3f8e1ad3402` (key 0 identical; the owned FileSchedule's guest-RAM delivery footprint from the first loads) | 50.50 (total 52.04, p50 50.35, p95 53.34) on the REVERTED binary at load_1m 2.88; ref BASELINE 50.95 → 0.991; flip 5 50.07 → 1.009; the flip binary cannot load the slowspot anchor (same GCLK refusal) |
| 7 | textures-at-load re-attempt = `hle.graphics_sprites` (after Stage 15's scratchpad-stack TextureRead repair 1dc1b80c) | **REVERTED** — census not zero / owned sprite work zero; frozen anchors refuse (`required section missing: Graphics selection`); every `--state-every` checkpoint refused | flip ee57c253 (`-x` of 1cd2a13e); revert 09db3b74 | leg: the repair holds — `graphics_refusals_textures=total=0,missing_resource=0,unsupported_state=0,ordering=0,faults=0,first=none,last=none,first_fault=none` (flip 1: total=40), `graphics_textures=loaded=40,destroyed=0,model_calls=287,failed=0,uploaded=0,restores=0` (flip 1: loaded=0) — but `Graphics.sprites.model_calls` = **21928** (exactly flip 1's count) / 425 (play smoke) / 517 (movie smoke), owned `graphics_sprites=replaced=0,…,refused=0`, `graphics_refusals_sprites=total=0,…,first=none`: no Sprite::Draw of the leg or the smokes references an owned startup icon (`TextureSelection::Sprite → Material → Probe` finds no Hud owner, silently); census summary `counters=97 nonzero=30 zero=67 absent=0` (REFERENCE 25/67/5 — the 5 GraphicsRuntime rows appear); game group `80% tests passed, 15 tests failed out of 76` (13 anchor restores `required section missing: Graphics selection` ×14 in LastTest.log; `RootCdvdPollVisibility` 0/25 state images — `.refused`: `Graphics shared surfaces: pending retained writeback dependency` ×22, `incoming TextureTable has an unclassified owner` ×2, `Graphics unsubmitted=8` ×1; `RootMetaArenaOff` RAM descriptors 345 vs console 385) | `diff_legs: IDENTICAL (lanes=frames,audio, masked 0 B, coalesced 0)` vs flip5/leg (4600 keys paired, 767 audio lines; `--allow-build-diff`: build ids 17:36:51 vs 19:58:20); last `4600,0xcdb1ff109c38971c` / `766,0xfd605d29418b3536` (the original drew every sprite: identity is not evidence); mem advisory FIRST DIVERGENT key 60 `0x8366f459eee4573d` vs flip5 `0x1929f3f8e1ad3402` (key 0 identical; the owned conversion of the 40 icons at frame 60 replaces the original upload path's guest-RAM writes) | 49.52 (total 51.13, p50 49.06, p95 52.64) on the REVERTED binary at load_1m 2.80; ref BASELINE 50.95 → 0.972; flip 6 revert 50.50 → 0.981; the flip binary cannot load the slowspot anchor (same GXSL refusal) |

| 8 | MovieDecoder pictures = `hle.graphics_movie` (+ `hle.movie_step`, not attempted) | **REVERTED** — frames NOT identical to REFERENCE (keys 171, 184, 185, 4026, 4027; the round-2 gate-3 presentation-seam divergence, attributed by the owner "by mechanism, not accepted", not as the retired model's defect); frozen anchors refuse (`required section missing: Graphics selection`); `--state-every` checkpoints refused from b120 | flip 6c1aa22a; revert 2a4e8785 | leg: `movie_graphics=decoded=2326,upload_intents=2323,view_intents=2317,draw_intents=2317,presents=2323,decode_original=0,queue_original=0,draw_original=0,errors=0` (REFERENCE `decoded=0,…,decode_original=2326,queue_original=2323,draw_original=2323`), `graphics_movie_transport=interrupt_model_calls=2323,present_model_calls=4540,failed=0` (retained transport, positive by design), `movie_step=selected=0,original_calls=2323` (other key, as REFERENCE), `graphics_selection=sprites=0,text=0,sky=0,movie=1`; catalog `Graphics.movie.queue_original = 0`, `Graphics.movie.draw_original = 0` (REFERENCE 2323/2323), summary `counters=97 nonzero=28 zero=69 absent=0` (REFERENCE 25/67/5: the five GraphicsRuntime rows appear with the model counts `sprites 21928`, `text 19987`, `sky start/model/finish 3547/28376/3547`); movie smoke `decoded=547,upload_intents=545,view_intents=541,draw_intents=541,presents=545,decode/queue/draw_original=0`, `graphics_movie_transport=interrupt_model_calls=545,present_model_calls=140`; play smoke `movie_graphics` all 0 (no movie in 120 frames), `present_model_calls=60`; game group `82% tests passed, 14 tests failed out of 76` (13 anchor restores + `RootCdvdPollVisibility`: 2/25 images, `.refused` `pending retained writeback dependency` ×22, `Graphics unsubmitted=8` ×1) | `diff_legs: DIVERGENT (lanes=frames,audio, masked 0 B, coalesced 0)` vs flip5/leg — `frames.fhash: FIRST DIVERGENT frame at key 171 … A: 171,0xaa40d7823a22a254 B: 171,0x012901761a28705b`, 184 `0x13e0a671915cf57c`→`0x3fe62f99e26f764e`, 185 `0x13e0a671915cf57c`→`0xdbe3d2ec470d7ccc`, 4026 `0x60387ab39df567a4`→`0xd1a1af69e1f18f18`, 4027 `0x60387ab39df567a4`→`0x3a5e5b74412935ac`; `audio.ahash: identical (767 lines)`; last `4600,0xcdb1ff109c38971c` / `766,0xfd605d29418b3536`; mem advisory FIRST DIVERGENT key 180 `0xc76b0ac7c739099b` vs flip5 `0xd3593dcced9e5827` (keys 0/60/120 identical); 200-frame same-binary `--dump-frames` pair: `f171.ppm` 100 % pixels differ, `f184.ppm` 99.9 %, `f185.ppm` 9.1 % (rows 54..434), all other 269 images byte-identical | 49.69 (total 51.19, p50 49.53, p95 52.19) on the REVERTED binary at load_1m 3.99; ref BASELINE 50.95 → 0.975; flip 1b revert 49.52 → 1.003; the flip binary cannot load the slowspot anchor (`required section missing: Graphics selection`, `frames 0/600 … FAIL`) |
| 9 | Audio: `hle.music` (MusicLibrary; `hle.sound_samples` not attempted) | **REVERTED** — frames AND audio NOT identical to REFERENCE (frames from key 212: the stage11 service-time lead, 6 presented frames ahead from the title scene; audio from key 177: the owned arm silent over windows 177-201 and 674-699 — MusicLibrary's complete-window read before readiness, MUSIC.md "Timing limit"); frozen anchors refuse (`selected service deadlines require GCLK`) | flip d74126cc; revert 28a9a079 | leg: `music_library=windows=4,plays=4,children=4,interleaves=4,ends=4,prepares=4,starts=4,stops=2,reads=4,delivered=4,bytes=16777216,stale=0,errors=0,fallback=0,statuses=6792` (REFERENCE: not installed, all 0), `sound_samples_binding=on music_binding=on stream_files_binding=on` (the key composes the sample/bank/control owner and the owned files), `sound_banks=issued=120,delivered=120,…,acknowledged=120,errors=0,fallbacks=0`, `sound_controls=staged=566,applied=566,…,batches=12913,replies=12913,shared=8,fallback=0,errors=0`, `sound_output=installed=1,mix_ticks=4600,owned_channel_samples=11329600`, `stream_library=file_opens=4,sample_opens=2,reads=294,deliveries=294,polls=3026,waits=182,…,fallbacks=0` (REF `fallbacks=3421`), `file_schedule=issued=294,delivered=294,consumed=294,…,wait_parks=89,overruns=39`, retained side `libsd_trans=0/0/0` (REF `5256576/640/0`), `iop_cdvd=reads=0,sectors=0` (REF 369/3069), `snd_kon=0` (REF 16), `libsd_calls` no `9:` uploads (REF `9:640`), `stream_rpc_calls=12913/0` (REF 13332), `sdk_rows_hit=470864` (REF 473203); play smoke `music_library` all 0 (no music), `sound_banks 42/42 fallbacks=0`, `stream_library reads=65 fallbacks=0`, `libsd_trans=0/0/0`; movie smoke the same with `reads=66`; catalog `counters=97 nonzero=25 zero=67 absent=5` — NO catalog row for the music/sample/file models (the only diffs vs flip5/census.txt are the 6-frame lead's model-row counts) | `diff_legs: DIVERGENT (lanes=frames,audio, masked 0 B, coalesced 0)` vs flip5/leg — `frames.fhash: FIRST DIVERGENT frame at key 212 … A: 212,0xd7d799fae96b2f14 B: 212,0xebad29d49e48a8bc` (4376 of 4600 keys differ; keys 1000-2000 `selected[k]==ref[k+6]` for 822 keys), `audio.ahash: FIRST DIVERGENT audio window at line 178 (key 177): A: 177,0xdaa2946b6b2bcd32 B: 177,0x0a6fb8af536edb25` (581 of 767 windows; the owned arm is silence `0x0a6fb8af536edb25` over 177-201 and 674-699; `audio_first_nonsilent_window=177 → 202`); last `4600,0xb4bcd30149fdbd14` / `766,0x3755dd2aa6789cc3` (REF `0xcdb1ff109c38971c` / `0xfd605d29418b3536`); `fmv_window=170-4019` (REF 170-4025), `fmv_served_per_playback=201,346,1779` (=); mem advisory FIRST DIVERGENT key 0 `0x9bcc7cc66ed869dd` vs `0x03018fa3110d34b5` | 49.68 (total 51.12, p50 49.52, p95 52.72) on the REVERTED binary at load_1m 3.32; ref BASELINE 50.95 → 0.975; flip 7 revert 49.69 → 1.000; the flip binary cannot load the slowspot anchor (`selected service deadlines require GCLK`, `frames 0/600 … FAIL`) |
| 10 | Graphics bridge re-attempt = `hle.graphics_frame` (flip 2b, after Stage 16's fixes 2/3/4) | **REVERTED** — census not zero (the frame family stops delivering after the frame-62 retained clear); frames NOT identical (key 84); `RootMovieLifecyclePlay` aborts; frozen anchors refuse `required section missing: Graphics selection` | flip 0d0f7b9e (re-expressed; e57a3281 no longer applies); revert e55988bb | leg: `Graphics.frame.setup.model_calls` = 0, **send 4534, interrupt 2323, clear 3307, layout 6860** (REFERENCE 1/4536/2323/3310/6861); owned `graphics_frame_output=completed=0,setups=1,layouts=1,retained_clears=2`, `graphics_owned_output=queued=5,completed=4,pending=0,failed=1,retained=2`, `graphics_output_faulted=1`, `graphics_frame_other=foreign_sends=0,interior_calls=0,failed=0,refused=17024,empty_sends=1`, `graphics_refusals_frame=total=17026,…,ordering=17026,…,first=frame:62/source_frame:2/n:1/producer:0x19f1c8/detail:514/reason:ordering,last=frame:4599/source_frame:6861/n:17026/producer:0x19aff0/detail:3/reason:ordering,first_fault=none`; summary `counters=104 nonzero=29 zero=75 absent=0`; play smoke (EXACTLY the owner's expected values): `graphics_frame_send=calls=59,replaced=59,model_calls=0`, `interrupt 0/0/0`, `clear 60/60/0`, `layout 58/58/0`, `graphics_frame_other=…,refused=0,empty_sends=1`, `graphics_refusals_frame=total=0,…,first=none,…,first_fault=none`, `graphics_frame_output=completed=58,setups=59,layouts=58,retained_clears=0`, `graphics_output_faulted=0`; movie smoke (aborted, census published by fix 4): `graphics_frame_send=calls=113,replaced=112,model_calls=1`, `graphics_refusals_frame=total=1,…,first=frame:174/source_frame:313/n:1/producer:0x181968/detail:7/reason:ordering`, `graphics_frame_output=completed=311,setups=312,layouts=312,retained_clears=0`, `graphics_output_faulted=1`, `graphics_service … ordering_errors=1`, `park=exception`; probes `--frames 63`: `retained_clears=1`, `graphics_owned_output failed=1`, `first=frame:62/…/detail:514`; `--frames 64`: `send model_calls=1`, `retained_clears=2`, `refused=3`; game group `78% tests passed, 17 tests failed out of 76` (12 × `required section missing: Graphics selection`; `RootCdvdPollVisibility` 24/25 `.refused` `graphics: Graphics owned pending=0,failed=1`) | `diff_legs: DIVERGENT (lanes=frames,audio, masked 0 B, coalesced 0)` vs flip5/leg — `frames.fhash: FIRST DIVERGENT frame at key 84 (paired by key): (83 keys paired before it) A: 84,0xa741b33a5c037124 B: 84,0x32b7c8a196897e0f`, 4457 of 4600 keys (segments 84-157, 171-172, 184-185, 210-4020, 4026-4027, 4035-4600; no shift alignment; timing rows = REFERENCE), `audio.ahash: identical (767 lines)`; mem advisory first key 0 `0x2cfcc4f064d14998` vs `0x03018fa3110d34b5` — ATTRIBUTED by the boundary-0 pair: `IDENTICAL-UNDER-MASK (lanes=mem, masked RAM0 1376 B, SPAD 0 B, META 0 B, coalesced 0, declared selection: hle.graphics_frame)` (`P7-FRAME-CHAIN`, EQUIVALENCE_PLAN.md §7.1) | 50.05 (total 51.48, p50 49.90, p95 52.35) on the REVERTED binary at load_1m 3.76; ref BASELINE 50.95 → 0.982; flip 8 revert 49.68 → 1.007; the flip binary cannot load the slowspot anchor (`required section missing: Graphics selection`, `frames 0/600 … FAIL`) |
| 10b | native Strip primitives re-attempt = `hle.graphics_strips=all` (flip 3b) | **NOT ATTEMPTED** (flip 2b did not hold); owner's §4 re-test run with `--set` on the reverted binary | none | probes `--frames 118/120` + both smokes COMPLETE (`graphics_output_faulted=0`, `runner exit=0`): `graphics_strip_output=captured=32,completed=16,failed=0,retained=8,first_refusal=514` (120 frames), play smoke `captured=40,completed=24,retained=8`, movie smoke `captured=160,completed=152,retained=8` (176 calls − 160 = 16 empty strips); `Graphics.strip.{plain,tinted,placed,placed_tinted}.model_calls=0`; `graphics_refusals_strips=total=8,…,unsupported_state=8,…,faults=0,first=frame:117/source_frame:57/n:1/producer:0x1a9120/detail:514/reason:unsupported_state` (**F = 0x200\|UnclosedDependency**, the title's first owned strip group); anchor probe `--set hle.graphics_strips=all --anchor artdet_f37152.ps2st` → `required section missing: Graphics selection` | not run (no flip) | not run (no flip) |
| 11 | MovieDecoder pictures re-run under DT-13 = `hle.graphics_movie` (flip 8b; `hle.movie_step` not attempted) | **REVERTED** — DT-13 blocking (1): the frozen legacy anchors refuse `required section missing: Graphics selection` (game group `82% tests passed, 14 tests failed out of 76`, the same 14 as flip 7; slowspot anchor refuses) — nothing since Stage 16 changed the GXSL class; (2) and (3) HELD; frames/audio deltas RECORDED | flip a51e18b4 (6c1aa22a re-expressed on 349bb028); revert 6632f958 | leg: `movie_graphics=decoded=2326,upload_intents=2323,view_intents=2317,draw_intents=2317,presents=2323,decode_original=0,queue_original=0,draw_original=0,errors=0` (REFERENCE `decoded=0,…,decode_original=2326,queue_original=2323,draw_original=2323`), `graphics_movie_transport=interrupt_model_calls=2323,present_model_calls=4540,failed=0` (retained transport), `movie_step=selected=0,original_calls=2323` (= REFERENCE), `graphics_selection=sprites=0,text=0,sky=0,movie=1`; catalog `Graphics.movie.queue_original = 0`, `Graphics.movie.draw_original = 0` (REFERENCE 2323/2323), summary `counters=104 nonzero=28 zero=76 absent=0` (REFERENCE 97 rows: 25/67/5 — the 5 GraphicsRuntime model rows `sprites 21928`, `text 19987`, `sky 3547/28376/3547` appear, plus the 7 Stage-16 `StreamLibrary.original.*` rows = 0); play smoke `movie_graphics` all 0 (no movie in 120 frames), `present_model_calls=60`, census 104: 7/97/0; movie smoke `decoded=547,upload_intents=545,view_intents=541,draw_intents=541,presents=545,decode/queue/draw_original=0`, `interrupt_model_calls=545,present_model_calls=140`, `fmv_pictures_served=547`, census 104: 13/91/0; game group: `RootCdvdPollVisibility` 2/25 images, `.refused` `pending retained writeback dependency` ×22, `Graphics unsubmitted=8` ×1 | `diff_legs: DIVERGENT (lanes=frames,audio, masked 0 B, coalesced 0)` vs flip5/leg — `frames.fhash: FIRST DIVERGENT frame at key 171 … A: 171,0xaa40d7823a22a254 B: 171,0x012901761a28705b`; 5 of 4600 keys: 171, 184 `0x13e0a671915cf57c`→`0x3fe62f99e26f764e`, 185 →`0xdbe3d2ec470d7ccc`, 4026 `0x60387ab39df567a4`→`0xd1a1af69e1f18f18`, 4027 →`0x3a5e5b74412935ac` (flip 7's set and hashes exactly); `audio.ahash: identical (767 lines)` (0 windows differ; first non-silent window 177 both); last `4600,0xcdb1ff109c38971c` / `766,0xfd605d29418b3536`; mem advisory FIRST DIVERGENT key 180; magnitudes (`tools/frame_diff.py`, same-binary 200-frame `--dump-frames` pair, fhash = the lanes'): f171 229376/229376 px (100.00 %), mean max\|Δ\| 7.15, max 26, 99.8 % sub-step (≤ 16), chan mean B−A R +5.93 G +1.39 B +6.92; f184 229089/229376 (99.87 %), mean 7.66, max 40, 96.9 % sub-step; f185 20820/229376 (9.08 %, rows 54..434), mean 9.46 over differing px (0.86 over all), max 32, 100 % multiples of 8; 4026/4027 not dumped (≈ 3.7 GB per arm) | 51.30 (total 53.02, p50 51.35, p95 54.70) on the REVERTED binary at load_1m 5.13 (> the 4.0 launch rule — report only); ref BASELINE 50.95 → 1.007; flip 2b revert 50.05 → 1.025; a first run at load 15.81 read 92.24 (`bench-underload-1`); the flip binary cannot load the slowspot anchor (`required section missing: Graphics selection`, `frames 0/600 … FAIL`) |
| A1 | ALL-ON (21 on / 6 off; `allon/SECTION.md`) | **SHIPPED** `b65138a0…`; title screen reached (b1260 case 7) | 6eb4fa41 | 104-row catalog: `nonzero=14 zero=90` (the 6 off keys' model rows + `Graphics.sky.*` STILL EXECUTING; `music` REFUSED by `music_buffers`) | frames key 120 / audio key 177 vs flip5 (recorded) | 51.15 (retained anchor arm) |
| A2 | ALL-ON, NO EXCEPTIONS (27 on; `allon_noexc/SECTION.md`) | **SHIPPED** `8ff27204…` — Release 0/0/0, component suite 599/599, game group 57/77 (19 anchor refusals + `RootPlayModeSmoke` = KNOWN-CRASH #1 at frame 63 `Graphics Movie presentation release refused`, triage bundle written); triage bundle on fault, `--set hle.<key>=off` degrades, census at exit | 48a388e9, 25049c05, 2d4c2818, 0e49db9f + publication | smoke (63 frames): `nonzero=3 zero=101` (`Graphics.frame.{layout,send}.model_calls=1/1`, `Graphics.screen.effects=3`); the PER-TOGGLE CENSUS EXPECTATIONS table below replaces a leg census (no leg required) | none required (no leg) | not run (no leg) |
| A3 | ALL-ON fixes 2 (27 on; shared-cause + music; `allon_fixed/`) | **SHIPPED** `973606bc…` (`bin/nativeee_release.966187d1`, `PS2RECOMP-BUILD-ID 2026-10-09 17:18:06`) — Release 0/0/0, component suite 600/600 (22 fixture skips), game group 61/80 = the SAME 19 frozen-anchor refusals, **`RootPlayModeSmoke` PASSED** (KNOWN-CRASH #1 closed), `RootGraphicsBridgeAllOn` + `RootTitleMusicPlays` passed | `41dae2c3..f91e8db5` (Zeno ×12, `a2974164` skipped), `d1ab9bf1` (3 stale pins re-expressed), `17b7ffca 8fddb7bc 966187d1` (Herschel) | 1800-frame leg: `nonzero=6 zero=98` (`Graphics.frame.send.model_calls=4` at the movie transitions, `Graphics.screen.effects` (no toggle), `Graphics.sky.*` and `Graphics.sprites` STILL EXECUTING); `graphics_output_faulted=0`, every lifecycle row `failed=0 refused=0`, `music_library starts=2 delivered=2 bytes=8388608 errors=0 refused_buffers=0`, `audio_first_nonsilent_window=237`; title b1260 case 7, clock advancing | vs `allon/leg` (21-on): frames FIRST DIVERGENT key 86 (the owned frame producer now draws); audio identical (217) — RECORDED (DT-13) | not run (DT-13/all-on) |

## Flip 1 — detail (`int_work/m_series/cutover/flip1/SECTION.md`)

"textures-at-load" is not one key: `TextureConsumers::Selected(HleConfig)` maps `hle.graphics_sprites`
→ Sprite (0x0D|0x0E; the 40 startup icons), `hle.graphics_text` → Font (0x0D; 3 Crash pages),
`hle.graphics_sky` → Sky (0x0A; owns 0 alone), `hle.graphics_shadows` → ShadowDefault (0 textures);
the other 4,004 IDs have no composed owner (M8 placed/rigid/skin/blend, particle, cloth, borrowed HUD).
The stage flipped `hle.graphics_sprites` and records the rest as the remaining gap.

What held with the flip: Release `FAILED=0 warning:=0 ld_warning=0 dup=0`; the component suite ALONE
`100% tests passed, 0 tests failed out of 597` (22 fixture skips + `Vu1CollectSweep (Disabled)`,
the Stage-14 set; the first run read `FAIL texture consumers line 143: none.families == 0` — the
old-default pin in `TextureConsumers::SelectionMap`, re-expressed as an explicit control arm);
`RootPlayModeSmoke Passed 6.20 sec`, `RootMovieLifecyclePlay Passed 24.41 sec`; the acceptance leg
`run_leg: OK` with every lane identical to ref0 (the owner refused every texture, so the original
drew every sprite: identity is expected, not evidence of a working replacement). The deterministic
fingerprint did not change (`env_hash=0x4b170af3afd2b4c9` on both legs: `Config::Fingerprint()` lists
non-default keys only).

What failed: criterion (c). With sprites selected, the first 40 `TextureRead` (`0x1bc920`) calls of the
title sequence (frame 60, `source_frame:0`) return `TextureBindings::Result::InvalidMemory`
(`detail:3`: `Bytes(object, 0x6c)` null or `object & 3` — the object address outside the owner's
readable-RAM check or misaligned) and REFUSE to the original (Zeno a66a8e3b, round 3). No asset is
converted (`loaded=0`), every later Sprite draw declines (`graphics_textures.model_calls=287`
declined reads) and the model draws (`graphics_sprites.model_calls` 21928). Exactly the outcome
Stage 14 §Next item 1 predicted from Zeno's handoff ("census (c) will not read zero until that
refusal is retired").

Owner and gap: **Zeno (M6/M7, `hle/graphics/texture_bindings.cpp`)** — retire the 40
`InvalidMemory` refusals at `0x1bc920` frame 60 (read the startup icons' object addresses the title
passes; the `source_frame:0` says they are the pre-first-frame startup loads), then `graphics_textures.
loaded=40`, `graphics_sprites.replaced>0`, `model_calls=0` on the leg and smokes. The flip commit
1cd2a13e carries the complete default + pins and can be re-applied as-is once the refusals are gone.

Revert: `config.h`, `tools/gen_settings_table.py`, `settings_table.tsv`, `tests/core/test_config.cpp`
restored byte-identical to 726a5e26; kept (default-independent, explicit control-arm statements):
`tests/title/textures/test_texture_consumers.cpp` (control arm states `graphics_sprites=false`; pins the
compiled default off and selecting nothing) and `tests/app/root_anchor_{frames,pad_ready,heartbeat}.py`
(`--set video.backend=soft` legs also pass `--set hle.graphics_sprites=off`). Reverted binary (BUILD-ID
2026-10-08 12:50:33): Release 0/0, suite `100% tests passed, 0 tests failed out of 597`,
`RootPlayModeSmoke Passed 6.27 sec`, `RootMovieLifecyclePlay Passed 24.42 sec`, `RootAnchorPadReady /
RootAnchorHeartbeat / RootAnchorHeartbeatAtRestore` 3/3. `REFERENCE` stays `ref0/leg`.

Also found: `nativeee --help` omits `--game-files <dir>` (parsed at `engine/core/config/config_loader.cpp:267`;
every Root test passes it) — the published recorder command's one flag the help text does not show.

## Flip 2 — detail (`int_work/m_series/cutover/flip2/SECTION.md`)

`hle.graphics_frame` selects the five source frame boundaries at once (SetUpFrame clear `0x19f1c8`,
WriteFrameHead `0x19aaa8`, display Sends `0x181968`/`0x1819e8`, SetupPCRTC layout); the catalog carries one
`Graphics.frame.<boundary>.model_calls` row per boundary (`frame_service.cpp` increments, `graphics_report.cpp`
publishes); owned work is `graphics_frame_output=completed,setups,layouts`. On ref0 the five model counters read
1 / 4536 / 2323 / 3310 / 6861. This was the first live leg ever run with the key on (the round-2/3 gates ran
sprites/text/sky/strips/movie pairs only).

What held with the flip: Release `FAILED=0 warning:=0 ld_warning=0 dup=0` (BUILD-ID 2026-10-08 13:52:41); the
component suite ALONE `100% tests passed, 0 tests failed out of 597` (same 22 skips); config/schema/table/lints
16/16; `RootPlayModeSmoke Passed 6.25 sec` with clear/layout/setup replaced on every call
(`graphics_frame_clear=calls=60,replaced=60,model_calls=0`, `graphics_frame_layout=calls=58,replaced=58,model_calls=0`,
`graphics_frame_setup=calls=1,replaced=1,model_calls=0`, `graphics_frame_output=completed=58,setups=59,layouts=58`).

What failed: (1) the acceptance leg (deterministic coldboot, 4600) aborted after 63 presented frames —
`libc++abi: terminating due to uncaught exception of type std::runtime_error: Graphics ordered delivery failed`
from `GraphicsOutput::Record` (`graphics_output.cpp:374`, thrown when `GraphicsService` has `ordering_errors`
or the bridge `failed_groups`), `run_leg: FAIL: run.meta not finalized`; reproducible at `--frames 63` and `64`,
while `--frames 62` completes. The frame-62 census shows the first display Send of the title (frame 61,
`source_frame:0`) REFUSED with `detail:7` = `FrameService::kSendFrame` (no plan / no submitted scene frame /
size mismatch) and ran the model (`graphics_frame_send.model_calls=1`), leaving `graphics_owned_output
pending=1` / `graphics_service unsubmitted=1`; the Link walk of frame 63 then faults (one of
`GraphicsService::Fault`'s ordering messages — the two read in play mode: `Graphics walk did not deliver every
accepted predecessor`, `Graphics output delivery failed`). (2) `RootMovieLifecyclePlay` aborts the game process
the same way (`runner exit=134`, frames 128 then 63 on re-run) and `root_play_fmv_presents.py` reports it as
Skipped because the crashed run has no `video=` row. (3) Census not zero on the completing smoke:
`Graphics.frame.send.model_calls = 1`. Frames/audio: the 63 hashes and 10 audio windows written before the abort
equal ref0's prefix; no 4600-frame comparison exists. Mem advisory (not a criterion): the mem lane diverges from its first key — `verify/diff_legs.py … --lanes mem`: `FIRST DIVERGENT mem checkpoint at line 1 (key 0): A: 0,0x03018fa3110d34b5 B: 0,0x2cfcc4f064d14998` (key 60: `0x1929f3f8e1ad3402` vs `0x9844d33c39185567`); deterministic on every flipped run read (`--frames 62/63` probes, play smoke), while flip1's leg with the frame key off read ref0's value — the owned frame environment changes guest RAM at boundary 0, before the first present and 61 frames before the frame-61 refusal. Passed to Zeno.

Owner and gap: **Zeno (M6/M7)** — `frame_service.cpp` (`kSendFrame` at frame 61 must be taken, or refusing
must leave no pending owned work), `graphics_service.cpp`/`graphics_bridge` (the following walk must refuse,
not fault — INTERFACES.md §8a), and `app/host/graphics_output.cpp` (`Record` must publish the census of a
faulted run; today the `graphics_refusals_*.first_fault` ledger built for exactly this attribution is thrown
away with it). Flip commit e57a3281 carries the complete default + pins and is re-applicable as-is.

Revert: `config.h`, `tools/gen_settings_table.py`, `settings_table.tsv`, `tests/core/test_config.cpp` restored
byte-identical to ea664a27; kept (default-independent): the three root anchor scripts (`--set hle.graphics_frame=off`
beside `video.backend=soft`) and `test_texture_consumers.cpp` (compiled default pinned off for the frame key and
selecting nothing). Reverted binary (BUILD-ID 2026-10-08 14:28:00): Release 0/0, suite `100% tests passed, 0 tests
failed out of 597`, `RootPlayModeSmoke Passed 6.17 sec`, `RootMovieLifecyclePlay Passed 24.35 sec`, `RootAnchor*`
6/6, lints 13/13; slowspot `ms_frame 52.74` at load_1m 5.86 (report only). `REFERENCE` stays `ref0/leg`.

Also found: `root_play_fmv_presents.py` turns an aborted game process into a ctest Skip (owner Bernoulli);
`GraphicsOutput::Record` discards the census of a faulted run (owner Zeno).

## Flip 3 — detail (`int_work/m_series/cutover/flip3/SECTION.md`)

`hle.graphics_strips=all` routes the four complete flat `Strip::Draw` producers (`0x1a9120` plain, `0x1a92a0`
tinted, `0x1a9448` placed, `0x1a9660` placed_tinted) through `GraphicsService::TryStrip` → `StripIntent::Capture`
→ `GraphicsStrip::Submit` (scene-frame capture for bucket 24, `GraphicsScene::CanDrawStrip`) →
`GraphicsOutput::RecordOwned` (the owned callback inserted at the source bucket predecessor) → at the Link walk
`GraphicsBridge::AfterTag` delivers the group → `DeliverOwned` → `GraphicsScene::DrawStrip` (STRIP_SCENE.md).
Census: `Graphics.strip.<variant>.model_calls` (`graphics_service.cpp` `++counts.model_calls`, published by
`graphics_report.cpp`); owned work `graphics_strip_output.completed` (STRIP_SCENE.md: "Parent acceptance must use
`graphics_strip_output.completed > 0` together with zero actual per-variant model calls"). On ref0 (Plain) the
leg reads `graphics_strip_plain=calls=168,replaced=152,model_calls=16` with the 16 refusals at frames 135/136
(`detail:259` = capture result `StripIntent::Result` 3).

Round 3 check: Zeno's handoff 2 (`int_work/m_series/graphics/HANDOFF_2026-10-08_2.md` §2) recorded the round-2
gate-4 abort `strips-all (118)` = `Graphics ordered delivery failed` and repaired "All refuses exactly like
Plain (environment, capture result, interior, record refused)". That repair is in main (`db16dc1f`,
`graphics_service.cpp:160-184`). It does not reach this abort: the frame-118 fault is raised AFTER the owned
arm accepted the capture (`record_strip` returned true, `owned_strips=8`), when the Link walk delivers the
first owned group and `GraphicsScene::DrawStrip` returns non-Ok (`graphics_strip_output.failed=1`) — a
committed-arm fault that §8a keeps thrown.

What held with the flip: Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-08 15:24:59); the
component suite ALONE `100% tests passed, 0 tests failed out of 597` (same 22 skips as flips 1/2);
config/schema/table/lints 16/16 (`StripSelfOracle Passed 14.60 sec`, `GraphicsStripGL`, `GraphicsStrip`,
`GraphicsService`, `GraphicsRefusals` green — the OLD-bug pins for the subsystem).

What failed: (1) the acceptance leg aborts after 118 presented frames (`run_leg: FAIL: run.meta not finalized`;
`leg.log`: `Graphics ordered delivery failed` from `GraphicsOutput::Record` in the park census, `failed_groups`);
`--frames 117` completes, `--frames 118` aborts (reproducible). (2) `RootPlayModeSmoke` and `RootMovieLifecyclePlay`
both abort the game process (`runner exit=134`, `libc++abi: terminating due to uncaught exception of type
std::runtime_error: Graphics output delivery failed` inside `SessionRuntime::Run`, 116 presented frames) and
both scripts report the crash as Skipped (`SKIP: no window (video=<absent>)`). (3) No census exists for the leg
or the smokes (the writer throws); the `--frames 117` probe has `Graphics.strip.*.model_calls = 0` and owned
`graphics_strip_output.completed = 0` (criterion (c)'s "replacement's owned-work counters positive" fails too).
Frames/audio: the 118 hashes / 19 audio windows equal ref0's prefix; no 4600-frame comparison exists. Mem
advisory: keys 0 and 60 identical to ref0.

Attribution evidence for the owner: the control arm on the flip binary (`--set hle.graphics_strips=plain
--frames 118`) completes with `graphics_service=strip_calls=16,replaced=16,…,linked=1,unsubmitted=8,
queued_groups=1,completed=8` — the same first group of 8 plain strips is delivered by the retained DrawIR path
at frame 118. With All, a one-off uncommitted local build that publishes the census before `Record` throws
(`flip3/diag/`, BUILD-ID 15:37:55, the file restored byte-identical afterwards) read at `--frames 118`:
`graphics_strip_output=captured=16,completed=0,failed=1`, `graphics_owned_output=queued=16,completed=0,
pending=15,failed=1`, `graphics_service=…,owned_strips=16,…,linked=1,unsubmitted=8,pending=1,queued_groups=1,
completed=0,recorded_work=16,completed_work=0,ordering_errors=0,failed_groups=1`, `graphics_refusals_strips=
total=0,…,first_fault=none` (the `Fault` itself fires at the next walk's `Poll`, frame 119+). So the first
owned `GraphicsScene::DrawStrip` of the title fails (one of `InvalidFrame`, `UnclosedDependency`,
`ResourceFailure`, `MissingScene`, `StaleScene`, `DrawFailure`; `CanDrawStrip` had passed at capture).

Owner and gap: **Zeno (M6/M7)** — `graphics_scene.cpp` `DrawStrip` / `app/host/graphics_strip.cpp`: the
first owned strip delivery of frame 118 must complete (or the condition must be refused at capture time so the
original draws it — §8a), and `GraphicsOutput::Record` must publish the census of a faulted run (carried from
flip 2; the `graphics_strip_output.failed` / `first_fault` members are exactly the attribution the owner needs).
Flip commit e3435827 carries the complete default + pins and is re-applicable as-is.

Revert: `config.h`, `tools/gen_settings_table.py`, `settings_table.tsv`, `tests/core/test_config.cpp` restored
byte-identical to 9b70755c; kept (default-independent control-arm statements): the three root anchor scripts
(`--set hle.graphics_strips=plain` beside `video.backend=soft`) and `test_texture_consumers.cpp` (compiled default
pinned Plain and selecting nothing). Reverted binary (BUILD-ID 2026-10-08 15:40:00): Release 0/0, suite `100% tests
passed, 0 tests failed out of 597`, `RootPlayModeSmoke Passed 6.16 sec`, `RootMovieLifecyclePlay Passed 24.40 sec`,
`RootAnchor*` 6/6, lints 13/13; slowspot `ms_frame 51.57` at load_1m 6.74 (report only; a first run at load 6.47
read 57.17 and was flagged `REGRESSION-CANDIDATE` by the tool — the binary is code-identical to flip 2's reverted
binary, so that reading is load). `REFERENCE` stays `ref0/leg`.

Also found: both play smokes turn an aborted game process into a ctest Skip (flip 2 named
`root_play_fmv_presents.py`; `root_play_mode_smoke.py` does the same — owner Bernoulli).

## Flip 4 — detail (`int_work/m_series/cutover/flip4/SECTION.md`)

`hle.movie_lifecycle` (`engine/core/config/config.h`, `BOOL_FIELD` `config.cpp:183`) selects the owned
`MovieDecoder` lifecycle services (Initialise/Start/Finished/Close, FeedAudio, SetVolume, stream seek/read,
demux, sound ring start/stop — `GameLibraries` installs 7 services instead of the control arm's 3), the owned
`MovieStream` file cursor (`movie_stream.Install(…, cfg.hle.movie_lifecycle)`), the `MovieSound`/`MovieAudio`
PCM provider on the single sink seat (replacing the retained `FmvPcmInput` + BVOL register gains), and
retires the `FmvAbortHold` wrapper (installed only when the key is off). The replaced model's counters are
run.meta rows (not catalog rows): `ipu_commands.*` (IpuDevice command dispatch; the lifecycle executes BCLR),
`ipu_operations.init/stop` and `libipu_calls.init/stop` (LibipuApi), `movie_audio.model_pops` (retained PCM
input pops), `movie_decoder.{audio,stream,input,audio_playback}_original_calls`,
`movie_stream.{prepare,open,close}_original`, `fmv.retained_init_calls` (the retained libmpeg initialization
callback) — the set `MOVIE_ACCEPTANCE.md` §"Actual-operation and positive-work evidence" names. Round-2 gate 3
read the pair PASS (`bclr 4→0`, `init/stop 2→0`, `model_pops 10500→0` on 1500 frames).

What held: the one-line default + table (`reproducible (96 rows) -> PASS`); explicit pins — `test_config.cpp`
(default on, `Set off → off`, the validator chain starts from the retired arm stated explicitly),
`test_texture_consumers.cpp` (control arm states `movie_lifecycle=false`; compiled default on selects no
texture family), and the four Root tests that measure the RETAINED arm (`RootBindsFmvRows` sceIpuInit tail,
`RootHoldsFmvAbort` FmvAbortHold, `RootMixesFmvPcm` BVOL, `RootPlayFmvPresents` BVOL/retained PCM) now pass
`--set hle.movie_lifecycle=off` (the three scripts gained a `--set` passthrough) — on the flipped default they
would read `fmv_abort_holds=0,nohook`, `libipu_calls init=0`, `bvol_peak=0x0`. Release `FAILED=0 warning:=0
ld_warning=0` (BUILD-ID 2026-10-08 16:21:28); config/lints 16/16; component suite ALONE `100% tests passed,
0 tests failed out of 597` (22 skips, identical set to flip 3; `MovieLifecycleOracle`, `MovieDecoderServices`,
`MovieBoot`, `MovieServiceCheckpoint`, `MovieCheckpoint`, `MovieAudio`, `MovieBuffers`, `MovieStream`,
`GraphicsMovie`, `FmvAbortHold`, `FmvPcmInput`, `IpuDevice` … Passed — the OLD-bug pins BUG-045/BUG-079/
SOUND_P5_IMPL/FMV2_IMPL live there); the game-process group at -j1: `RootPlayModeSmoke Passed 8.58 sec`,
`RootMovieLifecyclePlay Passed 31.37 sec` (both `runner exit=0`, `finalized=1`), RootAnchor* 6/6 (legacy
anchors restore under the owned lifecycle through `MovieBoot`), Render_AttractReplay 5/5, RootAttractCutsceneFrames,
RootColdbootProbeResumes, RootRecordInputRoundTrip Passed; `RootCdvdPollVisibility Passed` (ref0's pre-existing
failure: with the owned lifecycle the `--state-every 60` images from b120 on are no longer refused
`movie_software: … still require Close`). `RootReleaseLanesIdentical` first FAILED (`guard=DIVERGENT
coldboot/mem.mhash line 4: 180,0x99d6c871e93314f8 vs 180,0xd3593dcced9e5827`) because its Debug side is the
`build/` binary, which was stale (BUILD-ID 2026-10-07 11:42:40, pre-flip default off) — the divergence is the
flip's own mem advisory; after `cmake --build --preset macos-dev --target nativeee` (BUILD-ID 2026-10-08
16:54:50) it read `guard=IDENTICAL`, `ALL PASS`. Acceptance leg `run_leg: OK`, 4600 frames, `env_hash=
0x4b170af3afd2b4c9` (= ref0; the default is not in the fingerprint), `diff_legs: IDENTICAL (lanes=frames,audio)`
vs `REFERENCE` (ref0). Census zero on the leg and both smokes with owned work positive (table). The audio lane
identity is the same basis as round 2's PASS: in the deterministic freerun leg the Movie PCM is inaudible in
both arms (`movie_audio frames=0 audible_frames=0`, ref0 `fmv_pcm mixed_audible=0`); the audible evidence is
`RootMovieLifecyclePlay` (`audible_frames=865600`, `mixed_audible=865600`, `peak=32767`) and `MovieLifecycleOracle`.

Mem advisory (not a criterion): first divergent key 180 — `180,0xd3593dcced9e5827` vs ref0
`180,0x99d6c871e93314f8`; keys 0, 60, 120 identical. Attribution by mechanism: the FMV window opens at frame 170
(`fmv_window=170-4025`) and the leg's `sdk_rows_hit_list` loses exactly `sceMpegInit@0x2b7b50`,
`sceMpegAddCallback@0x2b7d48`, `sceMpegDemuxPss@0x2bbe90`, `sceMpegAddStrCallback@0x2bbeb0`,
`sceIpuStopDMA@0x2c10a8`, `sceIpuInit@0x2c13c0` (and `libsdr_calls remote 2338→0`, `sdrdrv_calls 2338→0`,
`syscalls_total 25287→25284`): the retired libmpeg/IPU bodies no longer write their MPEG contexts, callback
tables and demux buffers into guest RAM from the first movie's Initialise on. Frames and audio are unaffected.

Bench on the FLIP binary (the first accepted flip, so the first flipped-binary bench): `[run_bench] 16:53:20 leg
slowspot_f37092_s1789428268 load=3.65 other_run_legs=0`, `frames 600/600 ms_frame 50.47 total 52.03 p50 50.31
p95 53.35 wall 32.1s OK`; `--compare verify/bench/BASELINE.tsv`: `50.95 50.47 0.991 52.15 52.03 0.998 -`,
`compare: worst ms_frame ratio 0.991 (threshold 1.10) -- report only, no verdict`. Load 3.65 satisfies the
BASELINE launch rule (< 4.0) — the first cutover bench that does.

`REFERENCE -> flip4/leg` (the next flip compares against this leg). Owner Bacon (M2).

## Flip 5 — detail (`int_work/m_series/cutover/flip5/SECTION.md`)

`hle.math_helpers` (`engine/core/config/config.h`, `BOOL_FIELD` `config.cpp:181`) is the one selection
`GameLibraries::Install` (`game_libraries.cpp:44-50`) passes to seven installers — `MathLibrary` (13 macro
operations), `CollisionMath` (8), `TrigMath` (31), `AnimationMath` (ReadJointAngle), `BoundsMath` (3),
`ClippingMath` (2 bodies / 3 entries) and `JointLibrary` (RotateJoint 297b40, CreateJointTransform 294300,
AnimateJoint 293ec0, one atomic batch). `hle.culling` is separate and stays off. The replaced model is the
captured generated (VU0-macro) body per entry, counted at the DispatchTable binding immediately before the
captured execution (BINDINGS.md): the 61 catalog rows `{MathLibrary,CollisionMath,TrigMath,AnimationMath,
BoundsMath,ClippingMath,JointLibrary}.generated.<entry>` → run.meta `<field>.generated_body_<entry>`; owned work
is the same-named selected count (`joint_library`: `matrices_written`, `animations`, `rotations`).
`transform_point` was already the accepted control replacement.

Round 2 (`PARENT_REVIEW_2026-10-07_r2.md` §5): pair clean (frames/audio identical, mem 1080 advisory), census
zero with `matrices_written=110514`, but 11/13 RESTORED legs aborted at 0 frames in `JointLibrary: invalid
source joint transform objects/frame` — the title's stack (`$sp=0x70004220`) straddles the scratchpad mirror
seam and the adapters validated objects with one host translation. Socrates' round 3 (port-math
`cd2301ff…c1113b04`, in `main` as `30677403` + `e0eeb855`, Stage 14 §6/§7): `GuestSpan` quad-wise spans and
refuse-never-abort (`refused_rotate/transform/animate`, `refusal_state/frame/objects/channels`, `faults` in the
`joint_library` census; the captured original runs on the unchanged context so a refusal moves
`generated_body_*`). CUTOVER.md's rule for this stage: the `RootAnchor*` tests restore the frozen anchors and
must not abort.

What held: the one-line default + table (`reproducible (96 rows) -> PASS`); explicit pins — `test_config.cpp`
(default on, `Set off → off`, `sound_bank_headers` pin kept), `test_texture_consumers.cpp` (control arm states
`math_helpers=false`; compiled default on selects no texture family); no Root test measures the retained math
arm and none of the anchor scripts sets the key (they ran the compiled default). Release `FAILED=0 warning:=0
ld_warning=0` (frozen BUILD-ID 2026-10-08 17:36:51; the build id regenerates on every ninja run, so the binary
was `cmp`-checked against the frozen copy before every game run); config/lints 16/16; component suite ALONE
`100% tests passed, 0 tests failed out of 597` (22 skips, identical set to flip 4; all 23 `MathLibrary*`
tests Passed — `MathLibraryTrigOracle 35.83 sec`, `MathLibraryJointAdapterOracle`, `MathLibraryJointTransformOracle`,
`MathLibraryJointAnimateOracle`, `MathLibraryBoundsOracle`, `MathLibraryClippingOracle`, `MathLibraryBindings`, …
— where the subsystem's OLD-bug pins live); the game-process group at -j1 `100% tests passed, 0 tests failed
out of 73`: `RootAnchorGsRestored 2.31`, `RootAnchorPadReady 2.26`, `RootAnchorF57292 18.70`, `RootAnchorF59991
18.75`, `RootAnchorHeartbeat 3.35`, `RootAnchorHeartbeatAtRestore 3.34` (the restored frozen anchors run under the
owned joint adapters), `RootPlayModeSmoke 6.24`, `RootMovieLifecyclePlay 24.59`, Render_AttractReplay 5/5,
`RootCdvdPollVisibility 58.34`, `RootReleaseLanesIdentical 71.13` (Debug target rebuilt from the flipped source
first — flip 4's procedural note — BUILD-ID 2026-10-08 17:39:55). Smokes -V: both `runner exit=0`,
`finalized=1`, real passes. Acceptance leg `run_leg: OK`, 4600 frames, `env_hash=0x4b170af3afd2b4c9` (= flip 4 =
ref0; the default is not in the fingerprint), `math_helpers_binding=on`, `diff_legs: IDENTICAL (lanes=frames,
audio)` vs `REFERENCE` (flip4/leg).

Census: every one of the 61 replaced-model rows is 0 on the leg, both smokes and the restored anchor, and the
owned counts equal REFERENCE's retired counts entry by entry (table in the row above and in SECTION §6;
11,636,915 retired executions → 0, 11,636,910 owned + `transform_point`); `angle_reads=2156844 = 3 ×
animations = animation_math.angle_pairs`. The one delta, `identity_matrix` 101484 → 101479 (round 2: 21742 →
21739), is attributed: the seven axis-matrix trig bodies (`FUN_0018bae8/0018bbc0/0018bc98/00182ad0/00182d40/
00182fb0/0018cea8`) are the only replaced bodies whose generated code calls `IdentityMatrix` 18e570 (zero-angle
branch); the control arm counted that nested dispatch as a `generated_body_identity_matrix` execution, the
selected `TrigMath::AxisMatrix` reuses the host identity operation directly (`trig_math.cpp:310-315`). Zero on
every supporting row: `generated_continuations`, `unsupported_inputs`, `generated_interiors`,
`unsupported_entries`, `refused_*`, `refusal_*`, `faults`. Unexercised in both arms on the leg (0/0, so not
evidenced by it): `clipping_math`, `joint_library.rotate` (RotateJoint), trig pitch/roll/matrix_x/
rotation_axis(_sine)/matrix_axis(_sine)/snap_yaw, `collision_math.sixteen_groups_range` — the round-2 set.
Restored anchor `ps2state_slot0_f57292` (one of the 11 that aborted in round 2), 300 frames on the flip binary:
`run_leg: OK`, `joint_library … transforms=38008,matrices_written=33808,done_returns=4200,animations=37916,
blended_animations=6824,angle_reads=113748,refused_rotate=0,refused_transform=0,refused_animate=0,…,faults=0`,
every `generated_body_*=0` — the seam-straddling stack objects are admitted, the refuse path was not needed.

Mem advisory (not a criterion): first divergent key 1080 (`1080,0x3d47b409b9189dde` vs flip 4
`1080,0xd27d7e407152c179`; checkpoints 0…1020 identical) — the same first key the round-2 same-binary pair
read for this key on a different binary (`GATES_LOG.md` 2a): the retired generated bodies and the owned
adapters leave different dead bytes below `$sp` (saved-register slots / reserved frames that the oracles exclude
as non-source-visible). `sdk_rows_hit=473203` and `syscalls_total=25284` equal REFERENCE's. Frames and audio
unaffected.

Bench on the FLIP binary: `[run_bench] 18:06:21 leg slowspot_f37092_s1789428268 load=2.18 other_run_legs=0`,
`frames 600/600 ms_frame 50.07 total 51.54 p50 49.80 p95 51.82 wall 32.1s OK`; `--compare verify/bench/BASELINE.tsv`:
`50.95 50.07 0.983 52.15 51.54 0.988 -`; vs flip 4: `50.47 50.07 0.992`. Load 2.18 < the BASELINE launch rule 4.0.

`REFERENCE -> flip5/leg`. Owner Socrates (M1).

## Flip 6 — detail (`int_work/m_series/cutover/flip6/SECTION.md`)

`hle.stream_files` (`engine/core/config/config.h`, `BOOL_FIELD` `config.cpp:189`) selects the owned StreamLibrary
file jobs at the four verified NTSC FileStream entries (`stream_library.cpp:70`: StartRead `0x2ad9a8`, IsReading
`0x2adaf0`, WaitRead `0x2ada30`, StartSoundBankRead `0x2ada68`; TR `game/filestream.cpp`) — ordinary reads enter
`FileSchedule` (owned bytes, destination, deadline, completion phase; SCHEDULING.md) instead of a STREAM.IRX read
record / `LoaderModel::Enqueue`. The root composes the key as `stream_files || sound_samples || music` into
`GameLibraries` AND into `SessionServices` (`ee_bootstrap_image.cpp:279`, `ee_bootstrap_sections.cpp:130`): with the
key on, the REQUIRED `SVCS` section is registered and `ServiceRestore::ValidateOwners` demands a `GCLK` clock in
every restored image (`service_restore.cpp:31`, SERVICE_CHECKPOINT.md §6 "Keep legacy selected restores refused").
The replaced model's counter is `stream_library.fallbacks` (the captured original body executed at the binding,
`stream_library.cpp:87`); owned work `stream_library.{file_opens,reads,deliveries,polls,waits}` and
`file_schedule.{issued,delivered,consumed}`. This was the first live leg with the key on.

What held: the one-line default + table (`reproducible (96 rows) -> PASS`); explicit pins (`test_config.cpp` default
on / `Set off → off` / the service_time chain states `stream_files=false`; `test_texture_consumers.cpp` control arm
states `stream_files=false`, compiled default pinned); Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-08
18:25:18, frozen `bin/`); config/lints 16/16; component suite ALONE `100% tests passed, 0 tests failed out of 597`
(22 skips, identical set to flip 5; `StreamLibrary`, `StreamFileSelfOracle`, `FileScheduleSelfOracle`,
`FilesSelfOracle`, `FileServices`, `ServiceRestore`, `SessionFiles`, `LoaderCompletion*`, `ServiceStartup`,
`StartupOwnership` Passed — the subsystem's OLD-bug pins BUG-026/032/033/038/040/041/042/043/066/068/069/070/071/086
live there); `RootPlayModeSmoke Passed 6.18 sec` and `RootMovieLifecyclePlay Passed 24.53 sec` with `runner exit=0`
(real passes); the acceptance leg `run_leg: OK`, 4600 frames, `env_hash=0x4b170af3afd2b4c9` (= flip 5), `stream_files_
binding=on`, `diff_legs: IDENTICAL (lanes=frames,audio)` vs `REFERENCE` (flip5/leg) — the owned file delivery
reproduces every frame and audio window of the attract leg while removing the 133 STREAM file-read RPCs (the
`31`/`32` open/read commands vanish from `stream_rpc_cmds`).

What failed: (1) criterion (c) — the game-process group `73% tests passed, 20 tests failed out of 73`: every test that
restores a frozen legacy anchor or the b120 probe image is refused before frame 0, `nativeee: --load-state <anchor>:
selected service deadlines require GCLK` (`RootAnchorGsRestored`, `RootAnchorPadReady`, `RootAnchorF57292`,
`RootAnchorF59991`, `RootAnchorHeartbeat`, `RootAnchorHeartbeatAtRestore`, `RootRegistersVuVifGifSections`,
`RootBindsDmacSyncSeams`, `RootRegistersIopmSection`, `RootRestoresStreamWorker`, `RootRestoresStreamHeartbeat`,
`RootRoutesGsPageAndDrawIr`, `RootVideoStackPresents`, `RootVu1LibraryNone`, `RootFramesKeyConvention`,
`RootColdbootProbeResumes`, the anchor halves of `RootReleaseLanesIdentical` / `RootGsQueueThreadIdentical`); the
same refusal stops the slowspot bench anchor. `RootCdvdPollVisibility` (`cdvd_poll_deliveries > 0` reads 0: the
owned schedule delivers at its own poll, not the CDVD poll; `async_issued 5362` delivered 0) and
`RootHeadroomQueryRecompiled` (`ticks demand=91`) measure the retained CDVD/headroom arm and would need explicit
`--set hle.stream_files=off` pins if the flip were otherwise viable. The CUTOVER.md rule stated for `hle.culling`
applies verbatim: a flip that makes the frozen anchors un-loadable is a regression. (2) Census: `fallbacks=2645` on
the leg (REF 3421), 253 on both smokes — the bank-start entry runs the original by design after `PrepareAudioRead`
(`audio_reads=120`) and the title's IsReading/WaitRead polls on bank requests fall back; the single counter cannot
show zero for the file intents the key owns. Mem advisory first key 60 (key 0 identical).

Owner and gap: **Herschel (M3)**, with **M0** for the root restore — (a) the legacy import the handoff names as its
first boundary ("Composition of the pending import in the root restore — M0's legacy path does not reach M3 yet;
legacy anchors refuse at the clock"): a frozen anchor without GCLK/SVCS must restore under the selected composition
through `StreamState::MigrateFiles(..., pending)` + a clock policy, so the six RootAnchor* tests and the b120 probe
resume; (b) a per-entry `stream_library` fallback census (`fallback_start_read/is_reading/wait/bank_start`) so the
replaced file-job counter is provable at zero while the bank path stays with `hle.sound_samples`; (c) explicit
`--set hle.stream_files=off` on the Root tests that measure the retained CDVD poll/headroom arm. Flip commit
`16c409bd` carries the complete default + pins and is re-applicable as-is.

`hle.files` (CUTOVER.md 5, second key of this stage) was NOT flipped because the first did not hold; a 4-frame probe
on the flip-5 binary reads the same class of refusal for the frozen anchors: `nativeee: --load-state …/artdet_f37152.ps2st:
selected disc files require FILS` (`file_services.cpp:39`; FILES.md "this stage does not import descriptors from
legacy states") — it needs the same legacy import before it can pass criterion (c). `hle.service_time` depends on
one of these.

Revert `e91e5daa`: `config.h`, `tools/gen_settings_table.py`, `settings_table.tsv`, `tests/core/test_config.cpp`
restored byte-identical to cb62180d (the validator chain additionally states `stream_files=false` explicitly —
default-independent); `test_texture_consumers.cpp` keeps the explicit control-arm statement and pins the compiled
default off. Reverted binary (BUILD-ID 2026-10-08 18:49:03): Release 0/0/0, config/lints 16/16, suite `100% tests
passed, 0 tests failed out of 597`, game group `100% tests passed, 0 tests failed out of 73` (RootAnchor* 6/6,
`RootReleaseLanesIdentical Passed 71.10 sec` with the Debug target rebuilt), smokes `runner exit=0` ×2; slowspot
`ms_frame 50.50` at load 2.88 (BASELINE 50.95 → 0.991; flip 5 50.07 → 1.009; report only). `REFERENCE` stays
`flip5/leg`.

## Flip 1b — detail (`int_work/m_series/cutover/flip1b/SECTION.md`)

Re-attempt of flip 1 after Stage 15 integrated Zeno's `1dc1b80c fix(graphics): Read TextureRead streams on the scratchpad
stack` (the 40 `InvalidMemory` refusals at `0x1bc920` frame 60 were the Stream argument on the scratchpad stack, not the
object addresses). `git cherry-pick -x 1cd2a13e` → `ee57c253` (four `UU` conflicts: the root anchor scripts kept `main`'s
three `--set` pins; `test_texture_consumers.cpp` kept `main`'s explicit control arm with the compiled default re-pinned to
select exactly `Family::Sprite` 0x0D|0x0E); settings table regenerated `reproducible (96 rows) -> PASS`.

What held: Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-08 19:58:20); config/lints/TextureConsumers 18/18;
component suite ALONE `100% tests passed, 0 tests failed out of 597` (22 skips = flip 5's set); `RootPlayModeSmoke Passed
6.26 sec` / `RootMovieLifecyclePlay Passed 24.61 sec`, both `runner exit=0`; acceptance leg `run_leg: OK` 4600 frames,
frames/audio IDENTICAL to flip5/leg; **the repair itself**: `graphics_refusals_textures=total=0` and
`graphics_textures=loaded=40` on the leg and both smokes (flip 1: `total=40`, `loaded=0`).

What failed (three independent regressions): **(a)** criterion (c) — `graphics_sprites=replaced=0,model_calls=21928` on
the leg, exactly flip 1's count, 425 / 517 on the smokes, `graphics_refusals_sprites=total=0`: with `refused=0` the only
path to `model_calls` (`sprite_service.cpp:46`) is `TextureSelection::Sprite()` returning false without a `Declined()`, i.e.
every drawn sprite's material textures lie outside the 40-icon Hud owner set (`Probe()` `expected=false` → nullptr). The
icons are converted at load and never consumed on the coldboot→attract leg or the smokes (they are gameplay HUD sprites);
the catalog row is per entry, not per texture ID (TEXTURE_OWNERSHIP.md rule 4's per-ID `graphics_texture_reads_by_id` does
not exist), so it cannot read zero here, and the owned draw count — the replacement work — is zero. Zeno's §3c caveat
(first owned sprite through the `GraphicsScene` pre-draw guards) stays untested. **(b)** the frozen legacy anchors refuse
to restore with a Graphics producer selected: `GraphicsRuntime::RegisterState` registers the `Graphics selection` (GXSL)
section and the restore requires it — `nativeee: --load-state …/artdet_f37152.ps2st: required section missing: Graphics
selection` (×13), `…/old_b120.ps2st` (×1), `…/ps2state_slot0_f57292.ps2st` (300-frame probe, `frames=0`) → game group
`80% tests passed, 15 tests failed out of 76` (RootRegistersVuVifGifSections, RootBindsDmacSyncSeams, RootRegistersIopmSection,
RootRestoresStreamWorker, RootRestoresStreamHeartbeat, RootRoutesGsPageAndDrawIr, RootVideoStackPresents, RootVu1LibraryNone,
RootFramesKeyConvention, RootAnchorGsRestored, RootColdbootProbeResumes, the anchor halves of RootReleaseLanesIdentical /
RootGsQueueThreadIdentical; the six RootAnchor* pass only because they carry `--set hle.graphics_sprites=off` since flip 1).
**(c)** save-state capture refused at every boundary of the coldboot leg: `RootCdvdPollVisibility` 0/25 `--state-every`
images (`.refused`: `graphics_resources: Graphics shared surfaces: pending retained writeback dependency` ×22, `graphics_resources:
Texture selection: incoming TextureTable has an unclassified owner` ×2 at b0/b60, `graphics: Graphics unsubmitted=8,pending=0,
ordering_errors=0` ×1 at b120); `RootMetaArenaOff` `descriptors: NEW RAM 345 / META 0 == console RAM 385 / META 0` (the 40
owned icons' original allocations no longer made).

Owner: **Zeno (M6/M7)**, with Herschel for (1): (1) GXSL/TXAS legacy import so the frozen anchors restore with a Graphics
producer selected (compose with the GCLK/SVCS/FILS import for the same anchors); (2) a provable census — a per-ID split of
the sprite model counter (owned-texture vs retained-texture sprites) or an acceptance leg that draws the icons (gameplay
anchors, themselves blocked by (1)); (3) checkpoint capture with sprites selected (`pending retained writeback dependency`,
`incoming TextureTable has an unclassified owner`); (4) an explicit old-value pin or attributed re-pin of `RootMetaArenaOff`.
`ee57c253` is re-applicable as-is.

Revert `09db3b74`: the five files restored byte-identical to `ea3d7217` (`git diff ea3d7217 --stat` empty; `--check` PASS).
Reverted binary (BUILD-ID 2026-10-08 20:37:23): Release 0/0/0, config 18/18, suite `100% tests passed, 0 tests failed out of
597`, game group `100% tests passed, 0 tests failed out of 76` (RootAnchor* 6/6, `RootCdvdPollVisibility Passed 62.78 sec`,
`RootReleaseLanesIdentical Passed 71.12 sec`, `RootMetaArenaOff Passed 5.22 sec`), smokes `runner exit=0` ×2; slowspot
`ms_frame 49.52` at load 2.80 (BASELINE 50.95 → 0.972; flip 6 revert 50.50 → 0.981; report only). `REFERENCE` stays `flip5/leg`.


## Flip 7 — detail (`int_work/m_series/cutover/flip7/SECTION.md`)

`hle.graphics_movie` (`engine/core/config/config.h`, `BOOL_FIELD` `config.cpp:204`; requires `hle.movie_lifecycle`
and `video.backend=gl` — `config_validator.cpp:22,33`) selects the owned Movie picture producers of
`hle/fmv/movie_graphics.cpp` (DecodeAndUpload `0x2b0e50`, QueuePicture `0x2af968`, Draw `0x2aeb30`) through `GraphicsMovie` (`app/host/graphics_movie.cpp`,
`GraphicsApplication::Install` `graphics_application.cpp:53`), and — because a Graphics producer is selected — creates
`GraphicsRuntime` (`graphics_application.cpp:28-41`), which registers the `Graphics selection` (GXSL) section and the
`graphics_resources` checkpoint condition. The replaced model's counters are the catalog rows
`Graphics.movie.queue_original` / `Graphics.movie.draw_original` (`movie_graphics.{queue,draw}_original`) plus the
run.meta member `movie_graphics.decode_original`; owned work `movie_graphics.{decoded,upload_intents,view_intents,
draw_intents,presents}`. The presentation transport (`graphics_movie_transport.{interrupt,present}_model_calls`) is
retained by design (MOVIE_ACCEPTANCE.md: "positive calls mean presentation transport is not retired").

Round 2 (`PARENT_REVIEW_2026-10-07_r2.md` §5, `GATES_LOG.md` L241/L252/L275): the pictures pair read
`FIRST DIVERGENT frame key 171; differing keys 171, 184, 185 only (3 of 1500)`, audio identical, census zero —
"not accepted (unattributed)". Bacon's HANDOFF 39 §3 / MOVIE_ACCEPTANCE.md §Round-2 located it "at the presentation
seam … attributed-by-mechanism, not accepted" and requested a `--dump-frames` pair from the parent.

What held: the one-line default + table (`reproducible (96 rows) -> PASS`); explicit pins (`test_config.cpp` default on /
`Set off → off`, the Step-without-pictures refusal and the validator chain state `graphics_movie=false`, the Soft
compiled default refuses, the Plain/Soft case states the picture arm; `test_texture_consumers.cpp` control arm +
compiled default pinned on; the four retained-arm Root tests and the three root anchor scripts pass
`--set hle.graphics_movie=off` explicitly — the validator requires it beside `movie_lifecycle=off` / `video.backend=soft`);
Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-08 21:17:20); config/lints/TextureConsumers 18/18;
component suite ALONE `100% tests passed, 0 tests failed out of 597` (22 skips = flip 5's set; `MovieGraphicsSelfOracle`,
`GraphicsMovie`, `MovieStepOracle`, `MovieLifecycleOracle` Passed); `RootPlayModeSmoke Passed 6.27 sec`,
`RootMovieLifecyclePlay Passed 24.78 sec` (`runner exit=0`); leg `run_leg: OK` 4600 frames, `env_hash=0x4b170af3afd2b4c9`;
**criterion (c)**: `decode/queue/draw_original` 2326/2323/2323 → 0 with `decoded=2326, presents=2323` on the leg and
547/545/545 → 0 with `decoded=547, presents=545` on the movie smoke.

What failed: **(1)** frames NOT identical to REFERENCE — 5 of 4600 keys (171, 184, 185, 4026, 4027; `fmv_window=170-4025`),
audio identical: the round-2 divergence, unchanged. A replaced model's output differing is a regression unless attributed
as the retired model's own defect with evidence; the owner's record attributes it to the owned arm's `SelectMovieFrame`/
`ClearMovieFrame` timing versus the model's scanout and calls it "not accepted". The requested pixel evidence was
collected (`flip7/pair/`, 200 frames, same binary, `--set hle.graphics_movie=off` vs default, `diff_legs … --allow-set-difference
hle.graphics_movie`): 272 `.ppm` per arm, 4 differ — `f171.ppm` (100 % of pixels: the owned arm presents a picture the model
arm never scans out at 171, mean RGB 245.6/248.3/254.5 vs 239.7/247.0/247.6; the model's `f171` is byte-identical to both
arms' `f172`, i.e. the model shows the FIRST picture at 171, not the pre-movie black of `f170`), `f184.ppm` (99.9 %: the
owned arm shows its drawn buffer — `f184.ppm` == its own `f184_drawn_0x70000.ppm` — while the model holds the last picture,
its `f184` == `f185`), `f185.ppm` (9.1 %, rows 54..434: the owned arm's presented frame differs from the model's held
picture inside the picture rows), `f184_drawn_0x70000.ppm` (13.5 %). **(2)** the frozen legacy anchors refuse to restore
(`required section missing: Graphics selection` — artdet ×13, `old_b120`, the slowspot anchor) → game group `82% tests
passed, 14 tests failed out of 76`; the six `RootAnchor*` pass only through their explicit `--set hle.graphics_movie=off`.
**(3)** `RootCdvdPollVisibility` FAIL: 2 of 25 `--state-every` images (b0, b60), 23 `.refused` (`Graphics shared surfaces:
pending retained writeback dependency` ×22, `Graphics unsubmitted=8` ×1 at b120). (2) and (3) are the flip-1b class
(any selected Graphics producer); `RootMetaArenaOff` passed here (no owned texture allocations).

`hle.movie_step` was NOT attempted (CUTOVER.md: it requires `graphics_movie`). Owners: **Bacon (M2) with Zeno (M7)** —
the presentation seam (`GraphicsMovie::Hooks::select/clear`, `graphics_application.cpp:56-64`; Zeno's
`GraphicsApplication::Present(const Frame&)` seam per INTERFACES.md) must make keys 171/184/185/4026/4027 identical to the
model's scanout, or the model's picture-late / last-picture-held behaviour must be attributed as the retired model's own
defect against the source (TR) with evidence; **Zeno (+Herschel)** — the GXSL legacy import and the checkpoint predicate
(flip-1b gaps 1 and 3). Flip commit `6c1aa22a` is re-applicable as-is.

Revert `2a4e8785`: `config.h`, `tools/gen_settings_table.py`, `settings_table.tsv` byte-identical to `1a60ebe4`
(`--check` → `reproducible (96 rows) -> PASS`); kept, default-independent: the four retained-arm Root tests' and the
three anchor scripts' `--set hle.graphics_movie=off`, `test_config.cpp`'s explicit picture-arm statements,
`test_texture_consumers.cpp`'s control arm + `CHECK(!compiled_defaults.hle.graphics_movie)`. Reverted binary (BUILD-ID
2026-10-08 21:46:39): Release 0/0/0 (a first attempt failed on `errno=28` — the system temp dir held 13 GB of earlier
Root-test artifacts, removed), config 18/18, suite `100% tests passed, 0 tests failed out of 597`, game group `100% tests
passed, 0 tests failed out of 76` (RootAnchor* 6/6, `RootCdvdPollVisibility Passed 56.16 sec`, `RootReleaseLanesIdentical
Passed 71.19 sec`), smokes `runner exit=0` ×2; slowspot `ms_frame 49.69` at load 3.99 (BASELINE 50.95 → 0.975; flip 1b
revert 49.52 → 1.003; report only). `REFERENCE` stays `flip5/leg`.

## Flip 8 — detail (`int_work/m_series/cutover/flip8/SECTION.md`)

`hle.music` (`engine/core/config/config.h:132`, `BOOL_FIELD` `config.cpp:192`) selects the owned MusicLibrary service
(`hle/audio/music_library.cpp`; MUSIC.md's ABI table — MsReadFile `1e2ee0`, MsPlayStream `1e1500`, MsInterleaveStream
`1e2fd0`, MsAddStreamChannel `1e30d0`, MsSetMibEndOffset `1e7800`, MsDisableKeyOn `1e7990`, MsAllowKeyOn `1e78f0`,
MsStopStream `1e6c28` owned; MsGetStreamStatus/MsReadReply shared). The root composes it through
`SoundOutput(samples, music, output)` whose `selected_ = samples || music || output` (`app/host/sound_output.cpp:23-24`): the
key ALONE also composes the sample/bank/control owner (`run.meta sound_samples_binding=on`), and through
`ee_bootstrap_image.cpp:279` / `ee_bootstrap_sections.cpp:130` (`stream_files || sound_samples || music`) the owned file
delivery and the REQUIRED `SVCS` section. Replaced-model count of the owned entries: `music_library.fallback` (the captured
original executed at the binding, `music_library.cpp:234`); retained-side observables `libsd_trans`, `iop_cdvd.reads`,
`snd_kon`, `stream_library.fallbacks`, `sound_banks.fallbacks`, `sound_controls.fallback`. The counters catalog has no row for
any of these models (criterion (c) read from run.meta, not through `retirement_gate.py`).

Pre-flip probe on the base binary: `--anchor artdet_f37152.ps2st --set hle.music=on` → `selected service deadlines require
GCLK`, `run.meta frames=0` (flip 6's refusal; Herschel HANDOFF #3 §8 predicted exactly this for `music`).

What held: the one-line default + table (`reproducible (96 rows) -> PASS`); explicit pins (`test_config.cpp` default on /
`Set off → off` / `Set on → on`, the validator chain states `c.hle.music = false`; `test_texture_consumers.cpp` control arm +
compiled default pinned on); Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-08 22:28:37); config/lints/
TextureConsumers 18/18; component suite ALONE `100% tests passed, 0 tests failed out of 597` (22 skips = flip 5's set;
`StreamFileSelfOracle` (MusicSelfOracle), `MusicAsset`, `SoundOutput`, `AudioMixer`, `SoundControls`, `SessionAudio`,
`ServiceRestore` Passed); `RootPlayModeSmoke Passed 6.20 sec`, `RootMovieLifecyclePlay Passed 24.63 sec` (`runner exit=0`);
leg `run_leg: OK` 4600 frames, `env_hash=0x4b170af3afd2b4c9`; **the owned entries' census**: `music_library fallback=0` with
`plays=4,children=4,starts=4,reads=4,delivered=4,bytes=16777216`, `sound_banks 120/120 fallbacks=0`, `sound_controls
566/566 fallback=0`, `stream_library reads=294 fallbacks=0`, `libsd_trans 5256576/640/0 → 0/0/0`, `iop_cdvd reads 369 → 0`,
`snd_kon 16 → 0`; smokes `fallback=0` everywhere with positive bank/control/file work (no music in 120/200 play frames).

What failed: **(1)** frames NOT identical — `FIRST DIVERGENT frame at key 212`, 4376 of 4600 keys; the identical prefix
1..211 is boot/loading/the first logo movie; from the title scene the owned arm is 6 presented frames AHEAD
(`selected[k]==ref[k+6]` for 822 of keys 1000-2000). The loader request stream has the same 5596 requests in the same order
with earlier ticks (`131→130`, `204→202`, `1775→1770`, `1984→1978`, `1998→1986`, `2208→2200`), `loader_waits_module 42/170 →
0/0`, `iop_inframe_us max 240324 → 16966`, `edge lifts 174 → 167`, `file_schedule overruns=39` — the stage11 `sound_samples`
divergence reproduced number for number (`hle/audio/REGRESSION_ATTRIBUTION.md` §2-3): the owned bank wait publishes bytes
without settling its read deadline into the loader headroom clock; fix = `hle.service_time` (flip order 5c), one-frame residual
= the retired STREAM round-trip charge. **(2)** audio NOT identical — `FIRST DIVERGENT audio window at key 177`; the owned
arm is SILENT over windows 177-201 (25) and 674-699 (26) where REFERENCE plays (`audio_first_nonsilent_window 177 → 202`);
the title's music starts are ISSUED earlier (MsPlayStream `0x1e1500 args=5,2,30000,…` at domain 2203/2236 vs 2211/2244) but
SOUND ~2.5 s later: `LoaderFileIo::StartMusicRead` (`loader_sync/loader_file_io.cpp:543-568`) submits each complete 4 MiB
track window as one `Destination::Music` read charged at its full sector extent before readiness — MUSIC.md "**Timing limit**
… Resolve this difference before default acceptance." Both are owned-arm timing properties, not retired-model defects →
REVERT under CUTOVER.md's identical rule. **(3)** every frozen legacy image refuses `selected service deadlines require GCLK`
→ game group `74% tests passed, 20 tests failed out of 76` (flip 6's 20 names: the six `RootAnchor*`, nine anchor-restoring
Root tests, `RootColdbootProbeResumes`, the anchor halves of `RootReleaseLanesIdentical`/`RootGsQueueThreadIdentical`, and
the retained-arm measurements `RootHeadroomQueryRecompiled` / `RootCdvdPollVisibility` — the latter also `FAIL [poll] snd_kon
> 0 … (the title music keyed)` because the owned arm keys through `Audio`; its `--state-every` images WERE written, no
checkpoint refusal with this key). The slowspot bench anchor refuses the same way.

`hle.sound_samples` was NOT attempted (the stage flips it only if `music` holds; the leg already ran the sample owner).
Owner **Herschel (M5/M4/M3)**: (1) music initial readiness — preload/refill scheduling instead of the complete-window
charge; (2) `hle.service_time` settling the owned bank-wait deadline into the loader headroom (REGRESSION_ATTRIBUTION.md §6)
and the parent's decision on the one-frame round-trip residual; (3) the Audio-composed legacy import so the frozen anchors
restore with `SessionServices` enabled (HANDOFF #3 §3.1/§10); (4) catalog rows for the music/sample/file replaced models.
Flip commit `d74126cc` is re-applicable as-is.

Revert `28a9a079`: `config.h`, `tools/gen_settings_table.py`, `settings_table.tsv` byte-identical to `1a60ebe4` (`--check`
→ `reproducible (96 rows) -> PASS`); kept, default-independent: `test_config.cpp`'s explicit statements of both values and
`c.hle.music = false` in the validator chain, `test_texture_consumers.cpp`'s control arm + `CHECK(!compiled_defaults.hle.music)`.
Reverted binary (BUILD-ID 2026-10-08 22:59:38): Release 0/0/0, config 18/18, suite `100% tests passed, 0 tests failed out of
597`, game group `100% tests passed, 0 tests failed out of 76` (RootAnchor* 6/6, `RootCdvdPollVisibility Passed 58.13 sec`,
`RootReleaseLanesIdentical Passed 71.19 sec`, `RootHeadroomQueryRecompiled Passed 2.20 sec`), smokes `runner exit=0` ×2;
slowspot `ms_frame 49.68` at load 3.32 (BASELINE 50.95 → 0.975; flip 7 revert 49.69 → 1.000; report only). `REFERENCE`
stays `flip5/leg`.

## Flip 2b — detail (`int_work/m_series/cutover/flip2b/SECTION.md`)

Re-attempt of flip 2 after Stage 16 (main 10a62733) integrated Zeno's fix 2 (`FrameEnvironment::Linked()`; a Send with a plan
and no submitted scene before the first Link COMPLETES as the owned display of no scene — `empty_sends`), fix 3 (`GraphicsStrip::
Work` Retained route), the §8a retained clear (`retained_clears`, `graphics_refusals_frame` detail `0x200|Result`), fix 4
(`graphics_output_faulted`, `park=exception`, census published on a faulted run) and Herschel's legacy ServiceImport (GCLK/SVCS/
FILS — NOT GXSL). `e57a3281` no longer applies (`git apply --check` fails on the three root anchor scripts, which already pass
`--set hle.graphics_frame=off`, and on the two moved pin files); the default + pins were re-expressed as `0d0f7b9e`.

What held: table `reproducible (96 rows) -> PASS`; explicit pins (`test_config.cpp` default on / `Set off → off` / `Set on → on`,
Soft cases state `graphics_frame=false`, `frame_soft` default+Soft refused; `test_texture_consumers.cpp` control arm + compiled
default pinned on); Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-09 00:31:46, 624 steps); config/lints/
TextureConsumers 18/18; component suite ALONE `100% tests passed, 0 tests failed out of 598` (22 skips = the r4 set); **`RootPlayModeSmoke
Passed 6.21 sec` with EXACTLY the owner's HANDOFF 4 §4 values** (`send … model_calls=0`, `empty_sends=1`, `graphics_refusals_frame=total=0`,
`retained_clears=0`, `graphics_output_faulted=0`); the leg and the 63/64 probes COMPLETE (fix 4: no abort, census published);
the boundary-0 mem advisory ATTRIBUTED — `IDENTICAL-UNDER-MASK (masked RAM0 1376 B)` under `mem_boundary0_mask.txt`
(`P7-FRAME-CHAIN` row added to EQUIVALENCE_PLAN.md §7.1; the pair needs `--allow-set-difference hle.graphics_frame`).

What failed: **(1)** the DETERMINISTIC coldboot's second Link: `--frames 63` reads `graphics_refusals_frame=total=1,…,first=
frame:62/source_frame:2/n:1/producer:0x19f1c8/detail:514/reason:ordering` = `kClearRetained|UnclosedDependency` on the owned
`StartFrame`, `retained_clears=1`, `graphics_owned_output … failed=1`, `graphics_output_faulted=1` — the frame-61 Send IS an
`empty_send`, so fix 2's hypothesised cause (residue of a refused Send's original copy) is not it; after the fault the frame
family delivers nothing (`graphics_frame_output=completed=0` over 4600 frames; `Graphics.frame.send.model_calls=4534`,
`interrupt 2323`, `clear 3307`, `layout 6860`; `refused=17024`, last `producer:0x19aff0/detail:3` kLayoutRefused) — census NOT
zero. **(2)** frames NOT identical: `FIRST DIVERGENT frame at key 84` (4457/4600; audio identical; timing rows = REFERENCE):
the refused Sends copy the unwritten `g_FrameChain` (the owner's Honesty note), so the display is not the drawn buffer —
an owned-arm consequence, not a retired-model defect. **(3)** `RootMovieLifecyclePlay` / `RootPlayWallLock` abort (`runner
exit=134`, `park_detail=Graphics walk did not deliver every accepted predecessor`): a second `kSendFrame` refusal
(`detail:7`) at the first display Send after the first movie starts (`frame:174/source_frame:313`, `graphics_movie=off`) then
`ordering_errors=1` (`first_fault … producer:0x17ed50`). **(4)** every frozen anchor refuses `required section missing: Graphics
selection` (GXSL `required=true` once a Graphics producer is selected): game group `78% tests passed, 17 tests failed out of 76`
(12 anchor restores, the two aborts, `RootPlayPolicyInert` frames, `RootCdvdPollVisibility` 1/25 images — 24 `.refused`
`graphics: Graphics owned pending=0,failed=1`); the bench anchor refuses too.

Owner **Zeno (M6/M7)** + **Herschel** (GXSL import): the `UnclosedDependency` on the first owned scene (frame 62 StartFrame;
also the first strip group under `strips=all`, flip 3b `F=514`); the frame family must keep delivering after a retained clear
(`failed=1` ends the owned display); the movie-start Send refusal must not fault the walk; the GXSL legacy import is the
precondition for any Graphics-family flip. Handed back: `int_work/m_series/graphics/CUTOVER_FLIP2B_FLIP3B_TO_ZENO_2026-10-09.md`.

Revert `e55988bb`: `config.h`, `tools/gen_settings_table.py`, `settings_table.tsv`, `test_config.cpp`,
`test_texture_consumers.cpp` byte-identical to `10a62733` (`--check` → `reproducible (96 rows) -> PASS`). Reverted binary
(BUILD-ID 2026-10-09 00:58:48): Release 0/0/0 (474 steps), config 18/18, suite `100% tests passed, 0 tests failed out of 598`,
game group `100% tests passed, 0 tests failed out of 76` (RootAnchor* 6/6, `RootCdvdPollVisibility Passed 62.56 sec`,
`RootReleaseLanesIdentical Passed 71.16 sec`), smokes `runner exit=0` ×2; slowspot `ms_frame 50.05` at load 3.76 (BASELINE
50.95 → 0.982; flip 8 revert 49.68 → 1.007; report only). `REFERENCE` stays `flip5/leg`.

## Flip 3b — detail (`int_work/m_series/cutover/flip3b/SECTION.md`)

NOT attempted as a flip (the stage brief: only if flip 2b holds). The owner's HANDOFF 4 §4 flip-3 re-test was run as
`--set hle.graphics_strips=all` on the reverted flip-2b binary (same code as main for every non-flip file): `--frames 118/120`
probes and both smokes COMPLETE (`finalized=1`, `graphics_output_faulted=0`, `runner exit=0`) — flip 3's frame-118 abort is
gone; `graphics_strip_output=captured=32,completed=16,failed=0,retained=8,first_refusal=514` (120 frames; play smoke
`captured=40,completed=24,retained=8`; movie smoke `captured=160,completed=152,retained=8`, the 16 uncaptured calls are the
empty strips), `Graphics.strip.{plain,tinted,placed,placed_tinted}.model_calls=0`, `graphics_refusals_strips=total=8,
unsupported_state=8,faults=0,first=frame:117/source_frame:57/n:1/producer:0x1a9120/detail:514` — **F = 0x200|UnclosedDependency**
on the title's first owned strip group (the same Result the frame family reads at frame 62). A flip would still revert on
the anchors (`--set hle.graphics_strips=all --anchor artdet_f37152.ps2st` → `required section missing: Graphics selection`).
No leg, no bench, no commit.

## Flip 8b — detail (`int_work/m_series/cutover/flip8b/SECTION.md`) — DT-13 re-run of flip 7

`hle.graphics_movie` re-flipped on `main` 349bb028 under DT-13 (blocking = build + ctest incl. the frozen anchors loading;
leg + both smokes complete; replaced-model census 0; frames/audio deltas recorded, never judged). Flip a51e18b4 = 6c1aa22a
re-expressed (`git revert --no-commit 2a4e8785`, one conflict in `test_texture_consumers.cpp` resolved beside the flip-8
music pins); `--check` → `reproducible (96 rows) -> PASS`; Release `FAILED=0 warning:=0 ld_warning=0` (BUILD-ID 2026-10-09
08:51:45, sha256 `84ce6d17…bc0e`); config/lints 18/18; component suite ALONE `100% tests passed, 0 tests failed out of 598`
(22 skips = flip 2b's set). Game group -j1 **`82% tests passed, 14 tests failed out of 76`** — the direct probe first:
`run_leg … --anchor artdet_f37152.ps2st --frames 4` → `nativeee: --load-state …/artdet_f37152.ps2st: required section
missing: Graphics selection`, `run_leg: FAIL: run.meta frames=0 way below requested 4`. The 14 are flip 7's exactly (12
anchor restores + `RootAnchorGsRestored` + `RootCdvdPollVisibility` 2/25 images). Source check: `section_registry.h:26`
`required = true` default; `graphics_runtime.cpp:245-280` registers `Graphics selection` without a legacy-absent policy
(`session_services.cpp:130` / `file_services.cpp:61` use `required = !legacy_absent; capture_required = true` — the
pattern the GXSL owner has not applied). **DT-13 (1) FAILS → REVERT** (6632f958; default/pin files byte-identical to
349bb028). (2) HELD: leg `run_leg: OK` 4600 `finalized=1 park=none`, smokes `runner exit=0` ×2, `errors=0`, `failed=0`
(no refusal storm). (3) HELD: `Graphics.movie.{queue,draw}_original = 0`, `decode_original=0`, owned `decoded=2326
presents=2323` (leg), `547/545` (movie smoke). Recorded deltas: frames 5 keys (171/184/185/4026/4027), `frame_diff.py`
f171 100.00 % px mean |Δ| 7.15, f184 99.87 % / 7.66, f185 9.08 % / 9.46 (differing px); audio identical. Bacon's stage-40
repairs for exactly these keys (`port-movie-r3` b527569e view pow2 / b5820a0a present seam, HANDOFF 40 §1.3 "owned wrong
twice") are NOT in `main` — the parent's integration, not this stage's. Reverted binary (BUILD-ID 09:20:34): 18/18,
598/598, game group `100% tests passed, 0 tests failed out of 76`, smokes `runner exit=0` ×2; slowspot 51.30 at load 5.13
(report only). `hle.movie_step` not attempted. Owner: Zeno (+Herschel) — GXSL legacy import; `REFERENCE` stays `flip5/leg`.


## All-on build (2026-10-09) — detail (`int_work/m_series/cutover/allon/SECTION.md`)

ALL-ON MODE (CUTOVER.md, user 2026-10-09 10:12) supersedes the one-flip-per-stage rows above: ONE Release with every `hle.*`
default on except the keys proven to abort the title coldboot. Base `75bacba1` + flip 9b's uncommitted `hle.music=on` edits
(kept; `allon/flip9b-working-tree.patch`). Acceptance (ONLY): Release 0/0/0; ctest green with old-default pins stated explicitly
and the frozen-anchor refusals recorded; a deterministic coldboot to the TITLE SCREEN without abort; the census.

**Keys: 21 on, 6 off** (`docs/filemap/settings_table.tsv`, `gen_settings_table: … reproducible (96 rows) -> PASS`). Off:
`graphics_frame` (flips 2/2b), `graphics_strips=plain` (flips 3/3b), `graphics_sprites` (flips 1/1b), `stream_files` (flip 6
GCLK), and the two the all-on bisect found — `graphics_shadows` (frame 113 `Shadow model load could not establish owned geometry`,
`shadow_loads.cpp:68`) and `graphics_text` (frame 119 `Graphics Movie presentation release refused` after the title's first owned
text group fails delivery: `graphics_service … recorded_work=6,completed_work=0,failed_groups=1`, `graphics_owned_output …
failed=1`, `graphics_output_faulted=1`; reproduced with text alone). The bisect: all-23-on → abort 113 (shadows); shadows off →
abort 119 (text); shadows+text off → `run_leg: OK` 1300 (`allon/leg-attempt1-allon-abort113`, `probe2-shadows_off`,
`probe3-shadows_text_off`, `probe4-text_alone`).

**Binary:** `allon/bin/nativeee_release`, `PS2RECOMP-BUILD-ID 2026-10-09 10:30:39`, sha256
`b65138a024176abcb8757667d89c51a62f4dd7547bce038d6e8763d72007d57b`; Release `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0`
(`allon/build-release.log`); Debug `nativeee` rebuilt for the Release-lane guard (BUILD-ID 10:43:46, 0/0/0).

**Suite:** config/lints/TextureConsumers 18/18; component suite ALONE `100% tests passed, 0 tests failed out of 598` (22 skips =
flip 8b's set); game group -j1 `75% tests passed, 19 tests failed out of 76` — all 19 are frozen-anchor restore/capture refusals
under the composition (`nativeee: --load-state …: Restore needs owned logical clock state.` ×17 — `hle.input_service`/
`hle.save_storage`; the chain behind it `selected disc files require FILS` — `hle.files`; `selected service deadlines require GCLK`
— `hle.music`/`hle.sound_samples`; `required section missing: Graphics selection` — `hle.graphics_movie`/`sky`/`distortion`;
`RootCdvdPollVisibility` 2/25 images, 23 `.refused` by the Graphics checkpoint predicate). Pins stated explicitly (nothing
weakened): `test_config.cpp` (every new default + every retired arm by `Set`/assignment in the validator chains),
`test_texture_consumers.cpp` (control arm states all 21 keys off; the compiled default selects exactly `TC::Sky`),
`tests/app/CMakeLists.txt` `PS2R_RETAINED_MOVIE_ARM` / `PS2R_RETAINED_FILE_ARM` / `PS2R_RETAINED_BOOT_TIMING_ARM`, the three
Soft-backend anchor scripts, and a `--set` passthrough for `root_play_policy_inert.py`, `root_title_screen_effects.py`,
`root_attract_cutscene_frames.py`. The seven boot-timing pins pass with the retained Input/Save/libmc arm stated
(`100% tests passed, 0 tests failed out of 7`): under the owned arm the title performs its memory-card check at boot
(frames 151-366, "Checking memory card (PS2) in memory card slot 1"; +216 frames before the first movie — the retained arm
skipped it through BUG-089's stack read of `*format`; both `save_storage` and `libmc_format_in_t0` must be off to skip it).

**Title screen:** `run_leg: OK`, `frames=1300 finalized=1 park=none`, boot-SM case `b1200:6 → b1260:7`, title clock
`354531 → 363471`; 1800-frame confirmation: case 7 on b1260…b1740 with the clock `363471 … 437039` advancing on every image.
Frames vs REFERENCE: first divergent key 120 (the Input/Save arm), `fmv_window=401-414` (REFERENCE 170). Smokes:
`RootPlayModeSmoke Passed` (compiled defaults, `runner exit=0 finalized=1`); `RootMovieLifecyclePlay Passed` (boot-timing arm
stated); the all-on composition movie smoke at 440 frames (census-only): `PASS fmv served_total=547 >= 500`.

**Known at publication:** (1) title music SILENT — `music_library … starts=0,errors=2,refused_buffers=2` (`MusicBuffers::CanPlay`,
`music_library.cpp:103`); plays with `hle.music_buffers=off` (+ `movie_start/wait/stop=off`) — Herschel; (2) the memory-card
check (+216 frames; hardware behaviour under the owned Input/Save + libmc t0 arms); (3) `graphics_sky` refuses every title sky start
`missing_resource` (54/54) and the model draws — Zeno; (4) the 19 anchor-restore reds (legacy clock/FILS/GCLK/GXSL import —
M9/Herschel/Zeno/M0).

**Slowspot:** the anchor refuses under the compiled defaults (`Restore needs owned logical clock state.`); with the retained anchor
arm stated via `--set`: `frames 600/600 ms_frame 51.15 total 52.81 p50 51.21 p95 54.07` at load 5.89 (report only; BASELINE 50.95
→ 1.004; flip 8b 51.30 → 0.997).

### CENSUS TABLE (leg 1300 / play smoke 120 / movie smoke all-on 440; `allon/census_table.py`)

| key | owned counters (leg / play smoke / movie smoke) | replaced-model counters (leg / play / movie) | verdict |
|---|---|---|---|
| `hle.math_helpers` | `math_library.rotate_vector`=8518/0/0; `trig_math.cos_sin16`=271453/6084/189180; `joint_library.transforms`=11346/0/0; `joint_library.animations`=6611/0/0 | `sum math_library.generated_body_*`=0/0/0; `sum trig_math.generated_body_*`=0/0/0; `sum collision_math.generated_body_*`=0/0/0; `sum animation_math.generated_body_*`=0/0/0; `sum bounds_math.generated_body_*`=0/0/0; `sum clipping_math.generated_body_*`=0/0/0; `sum joint_library.generated_body_*`=0/0/0; `joint_library.refused_transform`=0/0/0; `joint_library.faults`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.culling` | `culling_library.setup_chunk`=270/0/0; `culling_library.load_model`=1602/0/0; `culling_library.test_cell`=3992/0/0; `culling_library.test_box_at`=3482/0/0 | `sum culling_library.generated_body_*`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.movie_lifecycle` | `movie_decoder.initialized`=1/0/1; `movie_decoder.started`=2/0/2; `movie_decoder.closed`=2/0/2; `movie_decoder.audio_feeds`=1953/0/1953; `movie_stream.opened`=2/0/2 | `ipu_commands.bclr`=0/0/0; `ipu_operations.init`=0/0/0; `libipu_calls.init`=0/0/0; `movie_audio.model_pops`=0/0/0; `movie_decoder.audio_original_calls`=0/0/0; `movie_stream.open_original`=0/0/0; `fmv.retained_init_calls`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.graphics_movie` | `movie_graphics.decoded`=547/0/547; `movie_graphics.presents`=545/0/545 | `movie_graphics.decode_original`=0/0/0; `movie_graphics.queue_original`=0/0/0; `movie_graphics.draw_original`=0/0/0; `movie_graphics.errors`=0/0/0; `graphics_movie_transport.present_model_calls (retained transport, by design)`=1240/60/380 | ZERO (model 0, owned > 0) |
| `hle.movie_step` | `movie_step.steps`=545/0/545; `movie_step.advanced`=543/0/543 | `movie_step.original_calls`=0/0/0; `movie_step.failed`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.movie_start` | `movie_start.starts`=2/0/2; `movie_start.primes`=4/0/4 | `movie_start.original_calls`=0/0/0; `movie_start.failed`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.movie_wait` | `movie_wait.waits`=545/0/545; `movie_wait.presents`=545/0/545 | `movie_wait.original_calls`=0/0/0; `movie_wait.failed`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.movie_stop` | `movie_stop.stops`=2/0/2; `movie_stop.recoveries`=2/0/2 | `movie_stop.original_calls`=0/0/0; `movie_stop.failed`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.music` | `music_library.plays`=2/0/0; `music_library.starts`=0/0/0; `music_library.delivered`=0/0/0; `music_library.bytes`=0/0/0 | `music_library.fallback`=0/0/0; `music_library.refused_buffers`=2/0/0; `music_library.errors`=2/0/0; `libsd_trans (retained uploads)`=0/0/0/0/0/0/0/0/0; `iop_cdvd.reads`=0/0/0; `snd_kon (retained key-on)`=0/0/0 | **REFUSED** — model 0 (`fallback=0`, `libsd_trans=0`, `iop_cdvd.reads=0`, `snd_kon=0`) but the owned plays are refused by `MusicBuffers::CanPlay` (`refused_buffers=2`, `starts=0`): TITLE MUSIC SILENT (plays with `hle.music_buffers=off`: `starts=2,delivered=2,bytes=8388608`, `audio_first_nonsilent_window=237`) |
| `hle.music_buffers` | `music_buffers.allocations`=5/5/5; `music_buffers.sound_loans`=2/0/2; `music_buffers.disc_loans`=2/0/2 | `music_buffers.fallback`=0/0/0; `music_buffers.errors`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.sound_samples` | `sound_banks.delivered`=120/42/42; `sound_controls.applied`=40/17/19; `sound_output.owned_channel_samples`=58400/0/0; `stream_library.reads (owned file delivery)`=290/65/66; `file_schedule.delivered`=290/65/66 | `sound_banks.fallbacks`=0/0/0; `sound_controls.fallback`=0/0/0; `stream_library.fallbacks`=0/0/0; `libsd_trans`=0/0/0/0/0/0/0/0/0; `loader_slot_status`=0/0/0/0/0/0 | ZERO (model 0, owned > 0) |
| `hle.audio_mix` | `audio_mix.ticks`=1300/217/1619; `audio_mix.owned_channel_samples`=58400/0/0 | `audio_mix.spu_mix_calls`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.service_time` | `service_timeline.installed`=1/1/1; `service_timeline.sequence`=42/42/42 | `loader_waits_module (retained module waits)`=0/0/0/0/0/0 | EXECUTING (owned timeline installed, 42 deadlines; the retained module-wait model `loader_waits_module=0/0`; no catalog row) |
| `hle.files` | `files.selected`=1/1/1; `files.opens`=0/0/0; `files.reads`=0/0/0 | `files.original_calls`=0/0/0 | NOT EXERCISED (0/0) |
| `hle.sound_bank_headers` | `sound_bank_headers.loaded`=2/2/2 | `sound_bank_headers.missing`=0/0/0; `sound_bank_headers.failures`=0/0/0 | ZERO (model 0, owned > 0) |
| `hle.input_service` | `input_service.samples`=1300/120/440; `input_service.reports`=1829/114/971 | `pad_calls.read (retained libpad model)`=0/0/0; `pad_calls.served`=0/0/0; `padman_calls`=0/0/0/0/0/0/0/0/0/0/0/0 | ZERO (model 0, owned > 0) |
| `hle.save_storage` | `save_storage.calls`=1779/62/921; `save_storage.published`=1/0/1 | `mc_calls.getinfo (retained libmc model)`=0/0/0; `mc_calls.sync`=0/0/0; `mcserv_calls`=0/0/0/0/0/0 | ZERO (model 0, owned > 0) |
| `hle.libmc_format_in_t0` | `libmc_format_in_t0`=on/on/on; `libmc_rows`=14/14/14 | - | TOGGLE ON (no replaced-model counter; the hardware arm) |
| `hle.meta_arena` | `meta_arena`=on,hooks=2,allocs=10434,frees=8602,live=1832,bump=2106/on,hooks=2,allocs=719,frees=336,live=383,bump=646/on,hooks=2,allocs=2040,frees=1639,live=401,bump=776 | - | TOGGLE ON (no replaced-model counter; the hardware arm) |
| `hle.graphics_sky` | `graphics_sky_start.replaced`=0/0/0; `graphics_sky_model.replaced`=0/0/0 | `graphics_sky_start.model_calls`=54/0/0; `graphics_sky_model.model_calls`=432/0/0; `graphics_sky_finish.model_calls`=54/0/0; `graphics_refusals_sky.total`=54/0/0 | **STILL EXECUTING** — every title-screen sky start refused `missing_resource` (`graphics_refusals_sky=total=54,missing_resource=54,first=frame:1246/source_frame:1731/n:1/producer:0x1b9cf8/detail:4`), the model draws (54/432/54) |
| `hle.graphics_distortion` | `graphics_distortion.draws`=0/0/0; `graphics_distortion_blocks.event_turns`=54/0/0; `graphics_distortion_settings.captured`=1/1/1 | `graphics_distortion_draw.model_calls`=0/0/0; `graphics_distortion_draw.calls`=0/0/0; `graphics_distortion.refused`=0/0/0 | NOT EXERCISED (no distortion draw before the title: `graphics_distortion_draw=calls=0`; the owned block/settings capture executes) |

Catalog (`allon/census.txt`, 104 rows): `summary: counters=104 nonzero=14 zero=90 absent=0` — the non-zero rows are the OFF keys'
model rows (`Graphics.frame.*`, `Graphics.screen.effects`, `Graphics.shadow.*`, `Graphics.sprites`, `Graphics.strip.plain`,
`Graphics.text`) and `Graphics.sky.*` (STILL EXECUTING); all 61 math rows, 10 culling rows, 2 Movie picture rows, 4 Movie player
rows and 7 StreamLibrary rows are 0.

## All-on build, no exceptions (2026-10-09) — detail (`int_work/m_series/cutover/allon_noexc/SECTION.md`)

ALL-ON, NO EXCEPTIONS (CUTOVER.md, user 2026-10-09 ~11:30) supersedes the six-off list above: EVERY `hle.*` toggle is the compiled
default — 27 keys, `graphics_strips=all`, the former exceptions `graphics_frame` / `graphics_sprites` / `graphics_shadows` /
`graphics_text` / `stream_files` included. Acceptance = Release 0/0/0 + ctest green; nothing else gates it (no title-screen leg);
the user plays it, the first crash reported becomes the graphics-bridge shared-cause stage. Base `6eb4fa41` (the 21-on all-on).

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release`, `PS2RECOMP-BUILD-ID 2026-10-09 12:36:09`, sha256
`8ff272041f1ad0eaa75123cf70824b8165a3ae0a937c6a8d0e12f86a966fb43f`. Release `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0`
(full build, 482 steps) and 0/0/0 on the incremental relink. `gen_settings_table --check` → `reproducible (97 rows) -> PASS`.

**Commits** (`main`): `48a388e9` cutover: all 27 hle.* defaults on (no exceptions) · `25049c05` config: --set hle.<key>=off degrades
dependents; inject_fault_frame · `2d4c2818` triage: crash-path bundle, bounded drawlist ring, census at exit · `0e49db9f` tests(root):
retained Graphics arm pins; session-path fault pc · the publication commit (this section).

**Mechanisms:**
- `ConfigValidator::DegradeDependents` (`engine/core/config/config_validator.cpp`): one rule per hle.* dependency of `Validate`
  (`graphics_movie→movie_lifecycle`, `movie_step→lifecycle+graphics_movie`, `movie_start→lifecycle+graphics_movie+music_buffers`,
  `movie_wait→movie_start`, `movie_stop→movie_wait+movie_step`, `music_buffers→music+lifecycle`, `audio_mix→music+lifecycle+
  iop.dispatch=frame`, `service_time→(stream_files|sound_samples|music)+frame`, every Graphics producer→`video.backend=gl`); the loader
  runs it before `Validate` and the root prints `[config] <key> disabled: requires <prerequisite>` per degraded key. Pins:
  `Coretest_config` `TestDegradeDependents` (per rule: the dependent off, the exact note, the count of degraded keys, the degraded
  composition validates; `--set hle.<key>=off` accepted for all 27 keys in a play session; the compiled defaults degrade nothing).
- `TriageBundle` (`engine/core/triage/triage_bundle.{h,cpp}`) + `DrawlistRing` (`engine/gs/hle/ir/drawlist_ring.{h,cpp}`: fixed
  200-frame / 32768-draw ring allocated once, in the DrawIR chain right before the consumer) + `LegCloser::Close`'s fault sink
  (`app/host/leg_closer.cpp`, `app/host/ee_bootstrap_leg.cpp`): on any park but `none`, `<harness.out_dir | paths.log_dir>/triage/
  <UTC>/` {`census.txt` = the run.meta rows, `drawlist_last200.txt`, `fault.txt` = park/pc/tid + error text + the faulting draw + the
  Graphics ledger rows (producer entry addresses) + the noted throw site, `backtrace.txt` = `backtrace_symbols_fd` of the frames noted
  on the throwing thread (`NoteThrowSite` at cold_boot's catch / the park sink / the `std::terminate` hook), `run.meta` finalized with
  `park=exception`}; run.meta `triage_bundle=<dir>`. Both routes verified on the frozen binary: `--harness-out` (`smoke/`) and a plain
  play session without it (`probe-logdir-route.log`: `triage bundle written: …/probe-logdir-route/logs/triage/20261009T171131Z`).
- The census at exit: `LegCloser::Close` prints `census:` + every run.meta row to stdout on every close (normal: `probe-normal-exit.log`;
  crash: the smoke's runner log line 6).
- `harness.inject_fault_frame=N` (`--set` only, requires `--harness-out`; files refuse it): the Verify step throws at boundary N —
  `RootTriageBundleOnFault` (TIMEOUT 120) proves the bundle on a real process: `runner exit=134`, `park=exception park_detail=injected
  fault at frame boundary 100 (harness.inject_fault_frame)`, `frames=100`, the five files non-empty, `fault.txt` with a faulting draw,
  the `census:` block, the `[config] hle.movie_step disabled: requires hle.graphics_movie` line → `Passed 2.41 sec`.

**Suite:** component suite ALONE `100% tests passed, 0 tests failed out of 599` (22 fixture skips, unchanged set); game group -j1
`74% tests passed, 20 tests failed out of 77` = the 19 frozen-anchor refusals of the previous all-on (`Restore needs owned logical
clock state.` ×17 lines + `RootCdvdPollVisibility`'s Graphics checkpoint predicate; keys `hle.input_service`/`save_storage` → FILS/GCLK/
GXSL chain) + `RootPlayModeSmoke` = KNOWN-CRASH #1. The 19 coldboot Root tests recorded under the retained Graphics arms state them
explicitly (`PS2R_RETAINED_GRAPHICS_ARM`); without them the first run read `53% tests passed, 36 tests failed out of 77` (23 aborts at
frame 63 / 113).

**KNOWN-CRASH #1** (`smoke/play_mode_smoke/`, the all-on play-mode smoke on the frozen binary): `frames=63 park=exception park_pc=0x1a01cc
park_tid=1 park_detail=Graphics Movie presentation release refused`, bundle `smoke/play_mode_smoke/triage/20261009T171056Z/` — `fault.txt`:
faulting draw `seq=8 frame=62 … prim=6 ctx=1 verts=192 fb=0x2f0000/512 psm=2 z=0x1c0000 tex=0x1c0000/50`; producer rows
`graphics_output_faulted=1`, `graphics_owned_output=queued=7,completed=5,pending=0,failed=1,retained=1`, `graphics_movie_transport=…,
present_model_calls=3,failed=1`, `graphics_refusals_frame=total=3,…,ordering=3,…,first=frame:62/source_frame:2/n:1/producer:0x19f1c8/
detail:514/reason:ordering,last=frame:63/…/producer:0x19aff0/detail:3/reason:ordering`. Bisect (`bisect/summary.txt`): `graphics_frame=off`
→ abort 113 (shadows); `+graphics_shadows=off` → abort 119 (text); `+graphics_text=off` → 300 frames clean with sprites and strips=all on.

### PER-TOGGLE CENSUS EXPECTATIONS (27 rows) — how to read the `census:` block / `run.meta` / a bundle's `census.txt`

Rows are `field.member` of run.meta (the `census:` block prints the same rows). "Owned > 0 when exercised" = the native path
executed; "replaced-model = 0" = the retired model did not. A key whose owned counters stay 0 on a session that never reached
its stage is NOT EXERCISED, not failing. The catalog names in parentheses are `games/<t>/verify/retirement_counters.json` rows
(`int_work/m_series/cutover/census_lines.py <run.meta> <catalog>` prints every one).

| key | owned counters expected > 0 when exercised | replaced-model counters expected 0 | where it is exercised |
|---|---|---|---|
| `hle.math_helpers` | `math_library.rotate_vector`, `trig_math.cos_sin16`, `joint_library.transforms`, `joint_library.animations` | `math_library.generated_body_*`, `trig_math.generated_body_*`, `collision_math.generated_body_*`, `animation_math.generated_body_*`, `bounds_math.generated_body_*`, `clipping_math.generated_body_*`, `joint_library.generated_body_*` (61 catalog rows), `joint_library.refused_transform`, `joint_library.faults` | boot, title, FMV, level (every frame) |
| `hle.culling` | `culling_library.setup_chunk`, `.load_model`, `.test_cell`, `.test_box_at` | `culling_library.generated_body_*` (10 `CullingLibrary.generated.*` rows) | title scene (from ~f420), level |
| `hle.movie_lifecycle` | `movie_decoder.initialized`, `.started`, `.closed`, `.audio_feeds`, `movie_stream.opened` | `ipu_commands.bclr`, `ipu_operations.init`, `libipu_calls.init`, `movie_audio.model_pops`, `movie_decoder.audio_original_calls`, `movie_stream.open_original`, `fmv.retained_init_calls` | FMV (the two boot movies; attract cutscene) |
| `hle.graphics_movie` | `movie_graphics.decoded`, `.upload_intents`, `.view_intents`, `.draw_intents`, `.presents` | `movie_graphics.decode_original`, `.queue_original`, `.draw_original`, `.errors` (`Graphics.movie.{queue,draw}_original`); `graphics_movie_transport.present_model_calls` stays > 0 BY DESIGN (retained transport) | FMV |
| `hle.movie_step` | `movie_step.steps`, `.advanced` | `movie_step.original_calls` (`Movie.player.step`), `.failed` | FMV |
| `hle.movie_start` | `movie_start.starts`, `.primes` | `movie_start.original_calls` (`Movie.player.start`), `.failed` | FMV start |
| `hle.movie_wait` | `movie_wait.waits`, `.presents` | `movie_wait.original_calls` (`Movie.player.wait`), `.failed` | FMV |
| `hle.movie_stop` | `movie_stop.stops`, `.recoveries` | `movie_stop.original_calls` (`Movie.player.stop`), `.failed` | FMV end |
| `hle.sound_bank_headers` | `sound_bank_headers.loaded` | `sound_bank_headers.missing`, `.failures` | boot |
| `hle.stream_files` | `stream_library.file_opens`, `.reads`, `.deliveries`, `.polls`, `.waits` | `stream_library_fallbacks.{start_read_declined,is_reading_declined,wait_declined,start_read_unowned,is_reading_unowned,wait_unowned,bank_start}` (7 `StreamLibrary.original.*` rows), `stream_library.fallbacks` | boot (file jobs), level loads |
| `hle.files` | `files.opens`, `.reads` (`files.selected=1` always) | `files.original_calls` | level loads (NOT EXERCISED before the title) |
| `hle.sound_samples` | `sound_banks.delivered`, `sound_controls.applied`, `sound_output.owned_channel_samples`, `file_schedule.delivered` | `sound_banks.fallbacks`, `sound_controls.fallback`, `stream_library.fallbacks`, `libsd_trans` (=0/0/0), `loader_slot_status` (=0/0) | boot, title, level (SFX) |
| `hle.music` | `music_library.plays`, `.starts`, `.delivered`, `.bytes` (title leg 1800: `plays=2,children=2,starts=2,delivered=2,bytes=8388608`), `audio_first_nonsilent_window=<n>` (237) | `music_library.fallback`, `.errors`, `.refused_window`, `.refused_buffers`, `.refused_voice` (all 0 since fixes 2), `libsd_trans`, `iop_cdvd.reads`, `snd_kon` | title (music starts ≈ frame 1420: the complete-window charge, timing only); FMV; level |
| `hle.music_buffers` | `music_buffers.allocations` (5), `.sound_loans`, `.disc_loans`, `.returns` = loans, `.active_loans=0` — AND `music_library.starts>0` with `.refused_buffers=0` (fixes 2: a paired child reserves through its PARENT's buffer; `RootTitleMusicPlays`) | `music_buffers.fallback`, `.errors` | title, FMV |
| `hle.input_service` | `input_service.samples`, `.reports` | `pad_calls.read`, `pad_calls.served`, `padman_calls` (=0/0/0/0) | boot onward (every frame) |
| `hle.save_storage` | `save_storage.calls`, `.published` | `mc_calls.getinfo`, `mc_calls.sync`, `mcserv_calls` (=0/0) | boot (the memory-card check, frames 151-366), save/load |
| `hle.audio_mix` | `audio_mix.ticks`, `.owned_channel_samples` | `audio_mix.spu_mix_calls` | every frame |
| `hle.service_time` | `service_timeline.installed`, `.sequence` | `loader_waits_module` (=0/0) | boot, level loads |
| `hle.graphics_strips` (=all) | `graphics_strip_{plain,tinted,placed,placed_tinted}.replaced`, `graphics_strip_output.completed` | `graphics_strip_{plain,tinted,placed,placed_tinted}.model_calls` (`Graphics.strip.*`), `graphics_refusals_strips.total` + `graphics_service.refused_work`, `.failed_groups`, `.ordering_errors` (0); `.discarded_groups`/`.discarded_walks` = the title's own `ResetBuckets` at the movie transitions (2/2 on the 1800 leg, attributed `graphics_refusals_strips ordering detail:4`) | title/UI strips (fixes 2: f118 passes; 1800 leg `captured=2216,completed=2216,failed=0`) |
| `hle.graphics_shadows` | `graphics_shadow_{begin,mesh,end}.replaced`, `graphics_shadow.completed`, `graphics_shadow_assets.converted` | `graphics_shadow_{begin,mesh,end}.model_calls` (`Graphics.shadow.*`), `graphics_shadow.faults`, `.refused` + `graphics_shadow_assets.refused`, `.rejected`, `graphics_refusals_shadows.total` (a model whose DiskManager node cannot be read is REFUSED, never a throw) | title scene (fixes 2: f113 passes — the node lives in the META arena, read through `AddressMap`; 1800 leg `converted=16 refused=0 rejected=0`), level |
| `hle.graphics_distortion` | `graphics_distortion.draws`, `graphics_distortion_blocks.event_turns`, `graphics_distortion_settings.captured` | `graphics_distortion_draw.model_calls` (`Graphics.distortion.draw`), `graphics_distortion.refused` | level (bucket-23 distortion; NOT EXERCISED before the title) |
| `hle.graphics_sprites` | `graphics_sprites.replaced` | `graphics_sprites.model_calls` (`Graphics.sprites`), `.failed`, `.refused`, `graphics_refusals_sprites.total` + `graphics_producers.refused_slots` | boot (startup HUD icons), menus — f60 passes since flip 1b; `graphics_sprites.model_calls` STAYS > 0 (`TextureSelection::Sprite` owns only the startup owner set — selection scope, RECORDED) |
| `hle.graphics_text` | `graphics_text.replaced` | `graphics_text.model_calls` (`Graphics.text`), `.failed`, `.refused`, `graphics_refusals_text.total` + `graphics_producers.refused_slots` | title text group (fixes 2: f119 passes; 1800 leg `replaced=1139,model_calls=0,refused=0`), menus |
| `hle.graphics_sky` | `graphics_sky_start.replaced`, `graphics_sky_model.replaced`, `graphics_sky_finish.replaced` | `graphics_sky_{start,model,finish}.model_calls` (`Graphics.sky.*`), `graphics_refusals_sky.total` | title scene — KNOWN: every sky start refused `missing_resource` (model STILL EXECUTING) |
| `hle.graphics_frame` | `graphics_frame_{setup,send,interrupt,clear,layout}.replaced`, `graphics_frame_output.completed` | `graphics_frame_{setup,send,interrupt,clear,layout}.model_calls` (`Graphics.frame.*`), `graphics_frame_other.failed`, `graphics_refusals_frame.total`, `graphics_output_faulted` + `graphics_owned_output.failed`, `.refused`, `.cancelled` (a refused owned group releases its slot and never latches; only FAILED latches), `graphics_movie_transport.failed`, `.release_refused` | every frame from the first frame setup (f62) — fixes 2: KNOWN-CRASH #1 CLOSED (`GraphicsScene::ForeignPages`: the target's own scanout writeback is not a dependency); 1800 leg `graphics_frame_send=calls=1237,replaced=1233,model_calls=4` (the 4 = the movie-transition Sends, `graphics_refusals_frame … detail:7`, RECORDED) |
| `hle.meta_arena` | `meta_arena=on,hooks=2,allocs>0` | (no replaced-model counter: the hook arm) | boot onward |
| `hle.libmc_format_in_t0` | `libmc_format_in_t0=on`, `libmc_rows` | (no replaced-model counter: the hardware arm) | boot (the memory-card check) |

Bisect form: turn ONE key back off per run with `--set hle.<key>=off` (`--set hle.graphics_strips=plain` for the enum); the
dependents it carries degrade with one `[config] hle.<dependent> disabled: requires hle.<key>` line on stderr instead of a
refusal (`graphics_movie` → `movie_step/start/wait/stop`; `music` → `music_buffers`, `movie_start/wait/stop`, `audio_mix`;
`movie_lifecycle` → `graphics_movie` + all of those; `video.backend=soft` → every Graphics producer).

## All-on fixes 2 (2026-10-09 17:18) — detail (`int_work/m_series/cutover/allon_fixed/`)

The follow-up CUTOVER.md "ALL-ON MODE" named: the shared cause of the aborting Graphics-bridge keys attributed and fixed once
(`int_work/m_series/graphics/SHARED_CAUSE_2026-10-09.md`, reviewed `SHARED_CAUSE_REVIEW.md` VERDICT APPROVED) and the silent title
music under `hle.music_buffers` (`int_work/m_series/stream/HANDOFF_2026-10-09_4.md`, reviewed `MUSIC_BUFFERS_REVIEW.md` APPROVED),
integrated into `main` (`bfaa6cb2` → `966187d1`, 16 commits, ff-only from `integration-20261009-fixes2`). Keys unchanged: 27 on.

**Attribution (the owner's, verified by the review):** four of the five all-on/flip failures (frame 63 movie release, flip-2b 62/63,
119 text, 118 strips) are ONE ownership/ordering defect in two parts: (a) `GraphicsScene::Ready` read the target's OWN scanout
writeback (`ScanoutComposer::SyncDrawn` → `AliasPageSet` cpu_dirty+aliased over the footprint) as a foreign dependency; (b) the bridge
turned a slot refusal into a run-wide failure (`EnqueueOwned` false ⇒ Failed ⇒ `owned_failed_` latch ⇒ every later `MakeOwned` refuses
⇒ the Movie release refusal was fatal). Frame 113 (shadows) is DISTINCT: `ShadowAssets` read the DiskManager node through a RAM-only
reader while `hle.meta_arena` places it at `0x2001a8c`, and the load THREW. Frame 60 (sprites) was fixed by flip 1b; the remaining
sprite model census is `TextureSelection::Sprite`'s scope. A sixth shape behind them (405/418, the title's `ResetBuckets` dropping
appended buckets → "walk did not deliver every accepted predecessor") is the same lifecycle family.

**Fix:** `GraphicsScene::ForeignPages` (pages whose `VramImage` author is this very target are not a dependency; EE / other-target
authors are) + the ordered-group lifecycle `captured → completed | retained | refused (slot released, attributed, never latches) |
failed (throw after delivery started) | discarded (an accepted group the title's walk never visits: cancel hooks run, attributed
`kDiscarded`)` across `GraphicsBridge/Output/Display/Producers/Movie/Service`; `ShadowAssets`/`ShadowApplication` read through
`AddressMap::Translate` (RAM + META arena) and `ShadowLoads` REFUSES (`graphics_refusals_shadows`), never throws. New census members:
`graphics_owned_output.refused/.cancelled`, `graphics_service.refused_work/.discarded_groups/.discarded_walks`,
`graphics_producers.refused_slots`, `graphics_movie_transport.release_refused`, `graphics_shadow_assets.refused`, row
`graphics_refusals_shadows`. Pins: `GraphicsFrameClear` (ForeignPages), `GraphicsBridge` (Completes/Refuses/Throws), `GraphicsMovie`
(refused release counted), `GraphicsService` (discard), `ShadowOwnership::ArenaNodeAndRefusal`, `RootGraphicsBridgeAllOn` (five 300-frame
all-on coldboots). No hook; the only `engine/` change is the `PendingRtSyncLedger::Reason::Save` enumerator.
Music: `MusicLibrary::Reserve(stream, buffer_stream)` — a paired child (TR `sound.cpp:594-609`: the second stream is opened with buffer
size 0 and never allocates) is reserved against its PARENT's buffer; `ServiceState::JoinMusic` joins on the parent's slot; the oracle
fixture drops the invented zero-size child allocate. Pins: `StreamFileSelfOracle` (title-shaped play/child pair, red before:
`failures=72`), `RootTitleMusicPlays` (1600-frame title coldboot: `starts>0 refused_buffers=0`, first non-silent window numeric).

**Integration notes:** `a2974164` (Zeno's independent copy of the music fix, same clause) SKIPPED in favour of Herschel's approved
commit (`git cherry` on the result: every other owner commit `-`). Three test binaries were STALE in the owner's `build-graphics`
(linked 2026-10-08 — the owner's and the reviewer's `105/105` ran them stale): rebuilt on `main`'s `build-release` they pinned the
pre-fix behaviour (`EffectsScene` 3 failures, `GraphicsCheckpoint` 1, `GraphicsProducers` abort). `d1ab9bf1` re-expresses them
stricter, exactly as the owner re-expressed `test_graphics_frame_clear.cpp`: a ledger entry owing THIS buffer is settled before the
owned draw / capture (`PreDraw` / `Save` flush counted, another buffer's entry stays), a foreign author on the same pages still refuses;
a renderer failure at the ordered slot throws (`failures=1`); a declined owned sky is `refused_slots=1`, `failures=0`, attributed
`Sky/Ordering/kDrawModel/0x200`. Checks 67→75, 161→162, 320→321.

**Acceptance (every line read; logs in `allon_fixed/`):** Release `-j4` steps 1/2: `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0` ×2;
component suite ALONE `-j8`: `100% tests passed, 0 tests failed out of 600` (22 skips = the previous set); game group `-j1` (Debug
`nativeee` rebuilt first): `76% tests passed, 19 tests failed out of 80` — the 19 = the previous all-on's 19 frozen-anchor refusals
(`Restore needs owned logical clock state.` ×17 lines; `RootCdvdPollVisibility`'s image-refusal class now only `Graphics unsubmitted=8`
×10 — the 18 `pending retained writeback dependency` refusals are gone); `RootPlayModeSmoke` **Passed** (previously KNOWN-CRASH #1),
`RootGraphicsBridgeAllOn` Passed, `RootTitleMusicPlays` Passed, `RootMovieLifecyclePlay` Passed. Re-test on the frozen binary: smoke
`frames=120 finalized=1 park=none graphics_output_faulted=0`, all lifecycle rows `failed=0 refused=0`, every `graphics_refusals_*=total=0`;
coldboot 1300 `run_leg: OK frames=1300 finalized=1 park=none`; coldboot 1800 `--state-every 60`: `b1260: boot_case=7`, clock advancing
on every later image (b1320…b1740), `music_library starts=2 delivered=2 bytes=8388608 errors=0 refused_*=0`, `audio_first_nonsilent_window=237`.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.966187d1`, `PS2RECOMP-BUILD-ID 2026-10-09 17:18:06`,
sha256 `973606bc68985d92f8d5a3c7f71653826c00a6f81f9aee53ccd6d5e8217f87fe` (`BIN_FROZEN_OK`; `nativeee_release` = `8ff27204…` kept).
Play command: INTEGRATION_STATUS.md "All-on build, no exceptions" with this binary; no `--set … =off` needed for the title or the music.

**Recorded, not fixed:** the 4 `kSendFrame` detail-7 Sends and the 2 discarded groups at the movie transitions (405–420); the sprite
selection scope (`graphics_sprites.model_calls` > 0); `graphics_refusals_sky missing_resource` on the title screen (sky model still
executes); the music complete-window charge (first audible window 237 ≈ frame 1420) and the `service_time` bank-wait lead (timing);
the 19 frozen-anchor refusals (GCLK owned-clock state; `hle.input_service`/`save_storage`).

## All-on s3 fixes (2026-10-09 23:35) — detail (`int_work/m_series/cutover/allon_s3/`)

CUTOVER.md "S3 PLAY BUGS" (user, 20:27, on the s2 binary `7e5f0abb` 19:47:57/20:25:51): three stages, each independently reviewed;
the two APPROVED ones integrated into `main` (`7e5f0abb` → **`dd9b20b0`**, 11 commits, ff-only from `integration-20261010-s3`;
every owner commit `git cherry` `-` after `cherry-pick -x`; no shared root file in either diff). Keys unchanged: 27 on.

| stage | owner / worktree | review | commits on `main` | result |
|---|---|---|---|---|
| 1 shadows (no meshes + white streak) | Lagrange, `port-textures` 3888daec | `graphics/LAGRANGE_SHADOWS_S3_REVIEW.md` APPROVED | `fe5f4b1e 8ae5c693 65bdd600 13f108ae` | FIXED — `RootShadowLevelEntry: ALL PASS` 26/26; play 4400: `refused_meshes 25512 → 0`, `instances 0 → 25428`, `published=1521` |
| 2 F5/F8/`--load-state` | Bernoulli, `port-product` a94d1131 | `input_saves/SAVESTATES_S3_REVIEW.md` **NEEDS_CHANGES** | — (not integrated) | OPEN — 3 review items (F8 preflight reads `.meta composition=`; `execv(argv[0])` → resolved image; retained Root artefacts) |
| 3 performance | Zeno, `port-graphics-r2` b5709082 | `graphics/PERF_S3_REVIEW.md` APPROVED | `675ff5e4 469b025e 55495d07 c086e5ff 3318516f 7ae51965 dd9b20b0` | IMPROVED — level 58 → 35 ms/frame, `pace_late 2919 → 2604`/4400, `underruns 3712 → 892`; gate (0 late) NOT met |

**Shadows — what was wrong (owner's attribution, re-derived by the review from the diff, TR, the NTSC ELF and the legs):** (1) check 4 /
result 2 = `MeshCheck::Shape` / `UnsupportedSource` on `model[5] & 0xffff` = needsEye | billboard<<8 — every default shadow mesh has
needsEye=1 (type-0B shader `NeedsEye` returns 1: TR `renderer/resources.cpp:138`, `shaderclasses.cpp:461`; NTSC `0x1d9330` `jr ra; li v0,1`);
needsEye only sizes the placed instance (`PlacedEyeSize` 11 vs 10 quadwords; the block closes at `size + 14 >= 100` → the EIGHTH eye-sized
instance, TR `models.cpp:30-34,431-459,471`); the eye quadword is never read by the 0B program. (2) The shader vtable constant `0x2fa8f0` was
slot 5 of the real table; the factory (`0x1bb948`) stores shader+0x6c = `0x2fa8c8` → `ShadowAssets::kShaderVtable`, pinned by
`audit_shadow_boundaries.py` (slots 0/1/5/6/8/13). (3) check 9 / result 1 = View / InvalidMemory after 1–2: `DrawShadowEntry` passes LOCAL
`screen`/`camera` matrices (TR `game/shadows.cpp:716-730`) and the main thread's stack is on the 16 KB scratchpad (TR `entry.cpp:61-62,83`),
so a1/a2 are `0x7000xxxx` → `ShadowSource::Value` serves them through the same reader, bounded to the window. (4) The STREAK: the s2 rule
"a refused mesh runs the model body" inside an OWNED bracket — the begin had bypassed `StartShadows`' half-size target set-up, so
`SetDefaultMeshDMA`'s bucket-21 additive strips (colour 8/248, `ALPHA=0201`) drew into the SCENE at `FRAME=070000`. Fix: inside an owned
bracket a declined mesh is drawn by NEITHER arm (attributed `refused_meshes`, `last_mesh_refusal` with `screen:/camera:`, the refusal ledger
with the bracket's source frame); `EffectsScene::Shadows` with no volume publishes NOTHING (no lease, clear, copy, composite; the scene
clear is not consumed); census `graphics_shadow=…,empty=N`, new row `graphics_shadow_scene=published=N,empty=N`. Pins red-before by
construction: `EffectsScene::EmptyPass` (the old `ClearBoundary` asserted a clear DID happen on `{}`), `ShadowPass` RefusedMesh
(`model_calls 1 → 0`), `BlockCapacity` (8 eye-sized → 1 block, 9th → 2), `ShadowAssetsSelfOracle` stride-11 eye-poisoned cases identical
(`86194 checks, 104 cases`), `RootShadowLevelEntry` (7600-frame deterministic replay of the s3 route with an ISOLATED memory card; asserts
`model_calls=0`, `replaced==calls`, `instances/blocks/groups>0`, `refused_meshes=0`, `published>0`, `published+empty==completed`, pass
empty ≈ scene empty, refusals `total=0`, AND per dumped frame f7000/f7500 a dark footprint under Crash (box mean ≤ ring − 22) and no
≥ 40-px near-white vertical run; the owner's `red2_7e5f0abb` leg of the frozen s2 binary on the identical route FAILS both pixel pins:
footprint `247.3`, streak `165 px (column 260)`).

**Performance — the round (owner's PERF_S3_1.md; review verified the lanes, profiles and tests):** workload = `play_sessions/s3/session.padlog`
replayed (`verify/bench/run_s3.py`: deterministic ms/frame per 300-frame window; `--play` wall-paced pace_late/underruns); profile =
`sample(1)` 20 s in the level (`verify/bench/sample_summary.py`). Deterministic baseline: level 71–96 ms/frame, `texres prep_bytes=693 GB`
over 8000 frames (87 MB/frame of per-draw key re-hash), gsops lane hash ~10 % of the EE thread, play's `vu1worker` 94 % busy in the cached
INTERPRETER (the title's VU1 library was never consulted under MTVU: `identity_stamped=0`, `classes raw-other=7007107`). Fixes, one at a
time, each lane-identical on the deterministic replay (8000 frames for 1+2; 4400 for all; review's own 3600-frame reproduction identical on
frames/drawir/gsops/audio/mem): `469b025e` texres content-key reuse while the pages' `view_gen` is unchanged (`prep_bytes −96 %`, `sig_hits`);
`675ff5e4` gsops recorder attached under `time.tick_deterministic` only (`gsops_recorder=on|off`); `55495d07` PSM descriptors by a constexpr
64-entry index + one draw flush per IMAGE span; `dd9b20b0` `PsmLayout::RowAddresser` for the host→local pixel walk (`1327104 addresses
compared, 1327104 equal`); `c086e5ff` the title's VU1 library on the MTVU worker's unit (`KickLowering` + `Vu1HleRuntime` per unit;
`--vu1-library none` = control) — the one BEHAVIOUR change in play (raw → library route, the deterministic legs' route; worker 94 % → 33 %
busy), accepted as attributed; identity pin for the worker route still owed (review item 1). `3318516f` the bench scripts, `7ae51965`
PERFORMANCE.md §7.

**Acceptance (every line read; logs in `allon_s3/`):** Release `-j4` step 1 `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0` (241 steps), step 2
same (448 steps); focused sets `100% … out of 28` (shadows + lints) and `100% … out of 110` (GS/texres + lints); component suite ALONE `-j8`
**`100% tests passed, 0 tests failed out of 601`** (157.12 s; 22 skips = the previous set, `SKIPSET_SAME`); game group `-j1` (Debug `nativeee`
rebuilt first, 0/0/0) **`77% tests passed, 19 tests failed out of 84`** (1187.56 s) — the 19 = the frozen-anchor refusal set, name-for-name
(`RED_SET_SAME_AS_PREVIOUS_19`; `anchor_refusals.txt` the same 17 `Restore needs owned logical clock state.` lines; `RootCdvdPollVisibility`
`FAIL [poll] boot case 0 >= 6 at the final image (b60)`); **`RootShadowLevelEntry` Passed 463.21 s** (red on `7e5f0abb`), `RootPlayModeSmoke`
6.18, `RootGraphicsBridgeAllOn` 20.93, `RootTitleMusicPlays` 32.65, `RootMenuSfxAudible` 38.65, `RootMovieLifecyclePlay` 24.59, `RootMtvuPlaySmoke`
6.21 passed; re-test `-j1` on the frozen binary `100% tests passed, 0 tests failed out of 4`.

**The s3 workload, before → after (play profile, wall-paced, `--harness-window`, one game process; `play1500-*`, `play4400-*`):**

| frames | pace_late | pace_parks | underruns / dropped | title+FMV ms/f | level ms/f | overall | prep_bytes | shadows |
|---|---|---|---|---|---|---|---|---|
| 1500 before | 377 | 1123 | 273 / 49888 | 17.16–24.41 | — | 31.78 | 4.52 GB | — |
| 1500 after | 291 | 1209 | 121 / 0 | 16.96–17.88 | — | 30.35 | 0.48 GB | — |
| 4400 before | 2919 | 1481 | 3712 / 123136 | 17.07–24.90 | 56.9–59.3 | 45.89 | 236 GB | `refused_meshes=25512 check:4/result:2` |
| 4400 after | 2604 | 1796 | 892 / 240352 | 17.16–22.03 | 35.0–35.7 | 36.98 | 8.7 GB | `instances=25428 refused_meshes=0 published=1521` |

load_1m at launch 5.21 / 5.89 / 6.64 / 5.02 (not idle — the idle table is still owed); both after-legs `park=none graphics_output_faulted=0`.
The level at 35 ms/frame is still every frame late: the GS pipeline runs inline on the EE thread (~72 % of the level frame in the owner's
`play-fix3` profile — `WriteImage` 16.7 %, `TextureResolver` 14.9 %, `RefreshCt16` 12.5 %, class renderers + GL 11.5 %); the remaining
path to 16.7 ms is DP-25 (`GsQueue::Pipelined` off the EE thread with the VRAM-image barrier) — the parent's fidelity-contract decision with
its own identity gate; smaller items in PERFORMANCE.md §7.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.dd9b20b0`, `PS2RECOMP-BUILD-ID 2026-10-09 22:55:06`,
sha256 `5605757753f9cbc63f121baf5c9770058d03a9afb2c13a2cd94d4b49ceec81e0` (`BIN_FROZEN_OK`; `.7e5f0abb`, `.966187d1`, `nativeee_release`
kept). Play command: INTEGRATION_STATUS.md "All-on build, no exceptions" with this binary.

**F5/F8/`--load-state` — the user's bug #2 stays OPEN in this binary:** the savestates stage's review found two defects on the F8 path (an
accepted F8 can still END the live session: the preflight never reads the slot's `.meta composition=` it writes, and `execv(argv[0])` fails
for a PATH-resolved launch after the session is closed) and no retained Root artefacts; its F5 loud-refusal path (`nativeee: savestate F5
save <Phase>: … the last owner to refuse: <owner>: <reason>` on stderr + the notice overlay) and the slot round trip (119/119 frame-hash
identity at boundary 60 on the all-on binary) passed review and will come with the fixed stage. Until then: `--record-input` + `--inputs`
replay is the bug-report path (it is how every s3 leg here was produced).

## All-on savestates (2026-10-10 01:5x) — detail (`int_work/m_series/cutover/allon_saves/`)

CUTOVER.md "S3 PLAY BUGS" #2 (F5 silent in play; F8; `--load-state` on the all-on binary): two owner stages, one re-review
(`input_saves/SAVESTATES_S3_REVIEW2.md` **APPROVED**), integrated into `main` (`1dd99548` → **`96c8525c`**, 18 commits, ff-only from
`integration-20261010-saves`; every owner commit `git cherry` `-` after `cherry-pick -x` except the one CMake-conflict pick, resolved by
keeping both owners' test blocks). Keys unchanged: 27 on.

| stage | owner / worktree | commits on `main` | result |
|---|---|---|---|
| Graphics capture-time settle (INTERFACES §13) | Zeno, `port-graphics-r2` bc7fa8eb | `01ce10b3 5fe00058 32baf92f c5d33ea8 da0ca041 2a7eb93b c3a903c1` | F5 admissible at the title / in play: `RootPlaySaveAtTitle: PASS (trigger save_at_frame, boundary 1300, unlinked items 4)`; `RootCdvdPollVisibility` green again |
| F5 slots / loud refusals / F8 relaunch / `--load-state` + review items 1–3 | Bernoulli, `port-product` 22ab19d8 | `2341329f e56bbb9a 6789b695 cd0968b8 6f523d0a 8e9a5310 184681b4 f7faf671 50b54dce d8bdf67e` | `RootPlaySaveRestore: PASS` (119/119 d=1 identity), `RootPlayRelaunchRestore: PASS` (relaunch + the composition refusal with the window open) |
| integrator | — | `96c8525c` | `RootPlaySaveRefusal` retired (red on purpose once combined: boundary 120 now WRITES); §12→§13 comment nit |

**What the settle is (Zeno; the review verified "a model of the boundary, no hook"):** at the title's SyncV boundary (TR `renderer.cpp:436-440`:
`Present` = WaitIdle → sceGsSyncV → SendToGif → SetUpDisplay, then `StartNextFrame` → `LinkRenderBuckets`) the CURRENT frame's owned draw groups
are recorded behind their predecessors and not yet Linked — host closures no image can carry; before, `GraphicsOutput::CanCheckpoint` refused
every such boundary `graphics: Graphics owned pending=N,failed=0` (the red retained at `graphics/settle/red_b1300/state_b1300.refused`).
`GraphicsOutput::SettleOwnedForCapture` INSPECTS: consumer drained, no owned fault, no immediate owned slot in flight, every sealed submission
delivered (`GraphicsService::CanCheckpointUnlinked`), and the pending owned batches = exactly the bridge's open recording
(`GraphicsBridge::OpenRecording()`); then it publishes the set as the **GXPD** digest `{source_frame, groups, items, owned_items,
bucket_items[28]}` (optional section both ways). The restored session's first Link draws that one frame WITHOUT those groups (the title's own
`ResetBuckets`-after-`WaitIdle` shape), attributed once per item at the Link producer (`ordering`, detail `kRestoredUnlinked=5`) + a log line.
`GSCN v3` carries the title's PSMCT16 display target (`render_psm`, `sync_keep_mask16`, `was_16bit`; v2 images load); shadow/distortion passes
admit a CLOSED pass at a settled boundary; the Present transport under `hle.graphics_movie` is `DispatchTable::TraceBody` (the original runs
first at every entry; an interior resume forwards to the original, `graphics_movie_transport … interior_resumes=`) so the live thread
capture's continuation proof holds at `0x1a0150`. Strict `GraphicsService::CanCheckpoint` unchanged; restore requires a closed recording.
Unit pin `TestCaptureSettleAdmitsTheOpenRecording` keeps the pre-settle refusal as the strict predicate's behaviour (`GraphicsService: 543
checks`); `GraphicsCheckpoint: 170`, `GraphicsMovie: 78`.

**What Bernoulli's review items became:** (1) `SavestateSlots::ReadMeta` / `CompositionDifference` / `PreflightComposition` — F8 compares the
slot's `.meta composition=` with `SavestateSlots::Composition(cfg)` BEFORE `Working`, refuses by name in the LIVE session (`composition differs:
profile(saved=deterministic,live=default) -- … so the session stays open`), `build_id` mismatch = Warning, missing sidecar refuses; (2)
`SavestateSlots::RunningImage` (`_NSGetExecutablePath` + canonical / `/proc/self/exe`, `access(X_OK)`) resolved in the preflight, `main.cpp`
`execv(image)` → `execvp(argv[0])` fallback → the `Restart by hand: <argv>` line (a bare-name launch relaunched and restored to a finalized
session: `root_pins/RootPlayRelaunchRestore/bare_name_argv0/`); (3) `int_work/m_series/input_saves/root_pins/` with the raw `run.meta` /
`frames.fhash` / `.meta` / `judge.log` artefacts — the reviewer re-derived 119/119 (62 distinct hashes; 62 mismatches at d=0 and d=2) himself.

**Acceptance (every line read; logs in `allon_saves/`):** Release `-j4` step 1 `FAILED=0 warning:=0 ld_warning=0 BUILD_RC=0` (517 steps), step 2
same (574), step 3 same (64); focused sets `100% … out of 81` (graphics), `100% … out of 36` (product), `100% … out of 29` (lints after the
retirement); `SavestateSlots: 72 checks, 0 failures`; `gen_settings_table … reproducible (100 rows) -> PASS`; component suite ALONE `-j8`
**`100% tests passed, 0 tests failed out of 602`** (136.98 s; 22 skips = the previous set, `SKIPSET_SAME`); game group `-j1` (Debug `nativeee`
rebuilt first, 0/0/0, 225 steps) **`80% tests passed, 18 tests failed out of 90`** (1064.28 s) — the 18 = the frozen-anchor refusal set minus
`RootCdvdPollVisibility` (`anchor_refusals.txt`: 19 `Restore needs owned logical clock state.` lines: artdet ×14, slowspot ×2, f57292, f59991,
old_b120); `RootCdvdPollVisibility ALL PASS` (its retained-arm `--state-every` images were refused `unsubmitted=8 (recording open before Link)`
— the settle admits them); `RootPlaySaveAtTitle 20.65 s`, `RootPlaySaveRestore 5.27`, `RootPlayRelaunchRestore 40.29`, `RootPlayModeSmoke 6.17`,
`RootGraphicsBridgeAllOn 15.64`, `RootShadowLevelEntry 371.22`, `RootTitleMusicPlays 22.37`, `RootMenuSfxAudible 30.51`, `RootMovieLifecyclePlay
24.59` passed; re-test `-j1` on `main` after the merge (binary `cmp`-identical to the frozen copy) **`100% tests passed, 0 tests failed out of
6`** (RootPlayModeSmoke, RootGraphicsBridgeAllOn, RootShadowLevelEntry, RootPlaySaveRestore, RootPlayRelaunchRestore, RootPlaySaveAtTitle).

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.96c8525c`, `PS2RECOMP-BUILD-ID 2026-10-10 01:16:32`, sha256
`c1fe47f1d30560ccc4818d8eb2ff9ce240004b7c41848a4c44e416ad9af884a5` (`BIN_FROZEN_OK`; `.dd9b20b0` kept; the 12:36:09 `nativeee_release`
deleted for disk, `bin/DELETED.txt`). Play command and the F5/F8/`--load-state` usage + refusal texts: INTEGRATION_STATUS.md "All-on build,
no exceptions" → "2026-10-10 — play-mode savestates". Slots: `<paths.save_dir | --harness-out | paths.log_dir>/savestates/slot<N>.ps2st` + `.meta`
(= `play_sessions/saves/savestates/slot0.ps2st` with the published play command).

**Census rows added for the user's exit `census:` block:** `graphics_capture_settle=settles=S,unlinked=U,source_frame=,groups=,items=,owned=,
buckets=` (a capture settled the open recording; `settles=` counts predicate calls — the effects' `unlinked_open` lambda calls it too),
`graphics_restore_unlinked=<the same digest>` on a restored session (expect `items=I` and `graphics_refusals_strips … ordering=I … detail:5`),
`savestate=written,path=…,frame=F,boundary=B` / `savestate=refused,<owner text>`, `savestate_refused_fmv=N` (the F5 presses refused by a playing
movie), `restored_vsync_frame=F`, `graphics_movie_transport=… interior_resumes=1` after a restore inside Present. Known: the first restored field
omits the captured frame's owned groups (one field; per-family intent serialisation would be the fidelity item — §13).

## All-on s4 (2026-10-10 15:5x) — detail (`int_work/m_series/cutover/allon_s4/`)

CUTOVER.md "S4 PLAY BUGS" #1 (no shadow in the PRESENTED frame), #2 (perf: the pipelined GS stage), #3 (the 18 % `unsupported` kick class):
three owner stages, three reviews APPROVED, integrated into `main` (`9948796b` → **`ff12dca8`**, 16 commits, ff-only from
`integration-20261010-s4`; every owner commit `git cherry` `-` after `cherry-pick -x`, 15 × `-`). Keys unchanged: 27 `hle.*` on; **`gs.queue`
default `inline` → `pipelined`** (DP-25 (b), `--set gs.queue=inline` the override).

| stage | owner / worktree | commits on `main` | result |
|---|---|---|---|
| Kick census by producing program | Hypatia, `port-math` 811d169e | `332f8bbe` | `graphics_kick_lowering_unsupported_by_program=…` (7 rows, sum = `unsupported`); with/without identity on the 4000-frame replay; attribution: 99.9 % = the material's A+D state packet, no missing family |
| Pipelined GS: split VRAM image (DP-25 (b)) + default flip | Zeno, `port-graphics-r2` e2a456ab | `80c45308 8cc9da9a f276398d f506119c 303d310a dc706476` | `RootGsQueuePipelinedIdentical` identical (200 lines, 4 lanes); s4 4000 / level 7600 `IDENTICAL`; det ms/frame 28.6→25.2, play 31.7→29.0 (owner idle table) |
| Shadows: composite at the player's scale; presented-frame pin | Lagrange, `port-textures` 49b6a581 | `3d0af48c cfb89205 b8f7a454 9f8ffb1c 0f4e3491 d357b49f 334086fb 4d53681f` | `RootShadowLevelEntry` judges `p7000/p7500` (the presented image): RED on `96c8525c`, `ALL PASS` here; `graphics_shadow_empty` names the 1710 empty passes |
| integrator | — | `ff12dca8` | the pipelined-GS relaunch deadlock: off-main window resizes DEFERRED per the VideoStack contract; `VideoStackDefersOffMainResize` + `RootPlayRelaunchRestore` green |

**What the pipelined GS is (Zeno; the review read it as "an ordering model — no test-only hook, no per-frame constant, no fidelity change"):**
`DrawIrThreadQueue` ran the consumer a frame ahead since P8.12-A but 29/30 frames differed because the EE and the pipeline shared ONE `VramImage`
(DP-25's named cause). Now `VramImage` carries a `VramMirror*`: the EE's five mutators (`WriteStream`, `NoteWriteNoBytes`, `NoteStreamWrite`,
`SettleWrites`, `NoteCpuWrite`) log `VramOp`s into the producer-owned `VramOpLog`; `FlushVramOps()` turns the log into one `Kind::VramOps` record
before every push (`AttachPendingPayload`) and at `Sync()`/`WaitFrame()`; the consumer replays it on its own image in record order, so each draw
sees the EE's writes up to its push and not later ones (`TestSplitImage`, `Gs_VramOpLogReplay`). `UploadEpoch()` agrees on both images by
construction (bumped only in `NoteStreamWrite`/`SettleWrites`). The EE's two reads of pipeline-side state (`TransferEngine::LocalToLocal` source,
`GsSections::SerializeVram`) go through selectors the host wires to `Sync(); return pipeline_vram`. `inline`/`thread` keep one image
(`split_image=0`). A consumer-thread fault is re-thrown on the producer thread (`DrawIrThreadQueueConsumerFault`).

**What the deadlock was (integrator):** under `pipelined`, `RootPlayRelaunchRestore` leg C (a WINDOWED restore: `--profile default --harness-window
… harness.restore_at_frame=100`) hung — `allon_s4/relaunch_bisect/pipelined_hang.sample.txt`: main `MovieBoot::Restore → SectionRegistry::Restore →
GraphicsOutput::SettleOwnedForCapture → DrawIrThreadQueue::Sync()` (waiting for the consumer to drain) ↔ `ps2r-gs` `DrawIrQueueConsumer::OnJob →
VideoStack::OnWindowResizedOffMain → SessionRuntime::OnMain → Park::Wait` (waiting for main to run the window resize). `video_stack.h`'s contract
for the off-main entry is DEFER to `ApplyPendingWindowResize()` at the frame boundary; `1c1a64d1` had added a synchronous `main_dispatch_` round
trip to it, latent under lockstep (the window job never arrived while main sat in `Sync()`), fatal under pipelined. `ff12dca8`: the off-main entry
records the pending geometry (counted in `deferred_resizes=`, coalescing) and never calls into the main thread; the boundary (slot 0, now in every
`gs.queue` mode — `ConfigureDisplay` and the checkpoint restore use the off-main entry without a consumer thread too) dispatches it to main from
the game's host thread (main is pumping there; inline when the boundary IS main). No sleeps, no timeouts. Pin `VideoStackDefersOffMainResize`
(tests/app, a visible window: the off-main post completes with zero dispatch attempts while the test thread blocks on it as `Sync()` does; the
boundary dispatches once; 10 checks, 9 red on the old function).

**What the shadow fix is (Lagrange; the review verified the TR citations line by line):** the composite's registers were already TR's
(`ApplyShadows`: `FRAME=070000` the scene target, whole-frame scissor, `ALPHA=2021 fix=20` = `QuarterTakenAway`, `psm=10` + bilinear the half-size
16S texture, `DepthAlways`, after the world draws and before the HUD — `draw_routes.txt` line 3223/3233, the only `owner=none` publication). The
pass's private half-size images were allocated at `frame/2 × scale` while `GlDrawExecutor::DrawStripCount` rasterises the volumes at the logical
half size (`s.scale = 1`, viewport `render_w × render_h`) — at `video.scale=2` the volumes covered the top-left quarter and the composite sampled
nothing where Crash stood. `shadow_renderer.cpp`: `Allocate(frame)` → `frame/2` at scale 1, only `Step::Composite` sets `params.scale = scale`.
The presented-frame lane: armed (`--dump-frames`), `GlDevice::Present` draws the clear + display quad into an owned window-sized RGBA8 FBO, reads it
back (`Backend::ReadPresented`, `p<key>.ppm`), blits to framebuffer 0 and swaps; unarmed, the player's path is untouched (`Render_Present` 240 checks).
The `empty` census: per `ShadowPass::Close()` a frame with begin/end and no accepted mesh is `no_mesh` (vs `redirected`), ranges appended per frame;
the level route's 1710 = 1731–3436 (title/menu/load, no caster) + 4600–4603 (the entry cut).

**Acceptance (every line read; logs in `allon_s4/` and `allon_s4/fix/`):** Release step 1/2/3 `FAILED=0 warning:=0` (711 / 626 / 440 steps), the fix
0/0/0; focused sets 23/23 (math), 35/35 (perf), 31/31 (shadows), 155/155 (app/gs after the fix); component suite ALONE `-j8` **`100% tests passed, 0
tests failed out of 606`** (253.20 s; 22 skips = the previous set, `SKIPSET_SAME`); game group `-j1` (Debug `nativeee` rebuilt first, 0/0/0)
**`80% tests passed, 18 tests failed out of 91`** (1985.59 s) — the 18 = the frozen-anchor set of `96c8525c` BY NAME (`RED_SET_SAME_AS_96c8525c`;
`fix/root_fail_logs/anchor_refusals.txt`: each class `Restore needs owned logical clock state.`); the first group run before the fix was
`79% … 19 tests failed` with `RootPlayRelaunchRestore` the one extra red (the hang). Passed: `RootShadowLevelEntry 1089.27 s` (`ALL PASS`, presented
`p7000 105.9 <= 122.5`, `p7500 106.5`), `RootGsQueuePipelinedIdentical 10.57`, `RootPlayRelaunchRestore 46.79`, `RootPlaySaveAtTitle 35.79`,
`RootPlaySaveRestore 8.67`, `RootPlayModeSmoke 6.28`, `RootGraphicsBridgeAllOn 15.98`, `RootCdvdPollVisibility 86.12`, `RootTitleMusicPlays 20.47`,
`RootMenuSfxAudible 51.28`, `RootMovieLifecyclePlay 24.88`, `RootPlayFmvPresents 24.68`, `RootPlayWallLock 8.39`, `RootMtvuPlaySmoke 6.31`. Re-test
`-j1` on `main` after the merge (`fix/ctest-retest.log`; binary `cmp`-identical to the frozen copy): RootGraphicsBridgeAllOn, RootPlayModeSmoke,
RootShadowLevelEntry (1113.65 s), RootGsQueuePipelinedIdentical, RootPlaySaveAtTitle, RootPlayRelaunchRestore → **`100% tests passed, 0 tests failed
out of 6`** (1241.05 s); lints/settings/Verify*/Retirement* 40/40 with the docs edits.

**The s3 workload, 2000 frames wall-paced (`fix/play2000-*`; before = frozen `96c8525c` ×2, after = `ff12dca8`):**

| binary | load_1m | pace_late | pace_parks | underruns / dropped | 600–1200 ms/f | 1200–1800 ms/f | overall | wall |
|---|---|---|---|---|---|---|---|---|
| 96c8525c (inline) | 7.75 | 1800 | 200 | 2063 / 5184 | 21.90 / 23.64 | 29.23 / 29.90 | 34.65 | 69.9 s |
| 96c8525c r2 | 3.75 | 1822 | 178 | 2038 / — | — | — | 34.80 | 70.2 s |
| **ff12dca8 (pipelined)** | 4.76 | **1652** | 348 | **1155** / 51456 | 19.07 / 18.71 | 26.96 / 27.30 | **32.23** | 65.1 s |

`pace_late → 0` NOT reached (the level is still every frame late at ~27 ms); the next lever named: the kick state-packet lowering (18 % of kicks on the
raw parser path on the MTVU worker) and the owner's mains-power idle table. Audio `dropped` rises under pipelined (pacing; the deterministic audio
lane is identical) — the audio-pacing owner's measurement.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.ff12dca8`, `PS2RECOMP-BUILD-ID 2026-10-10 14:53:26`, sha256
`409dbfdaa608ce3f009cc025014b34b591750e965bffbfa4770ed19318603d25` (`BIN_FROZEN_OK`; `.96c8525c` kept; `.dd9b20b0` deleted for disk, `bin/DELETED.txt`).
Play command: INTEGRATION_STATUS.md "All-on build, no exceptions" with this binary. Hazard: the user's s4 padlog does not replay deterministically
(input-clock indices; the copied card reads EMPTY) — INTEGRATION_STATUS.md "2026-10-10 15:5x" names it for Bernoulli.

**Census rows added for the user's exit `census:` block:** `graphics_kick_lowering_unsupported_by_program=<hash>:<program>@<entry>:<shape>:<n>,…`
(and the `_worker_` twin; `none` when empty), `graphics_shadow_empty=frames=,one_bracket=,many_brackets=,no_mesh=,redirected=,empty_frames=,
last_published_frame=,ranges=`, `gs_queue=… vram_ops=,vram_op_bytes=,split_image=1,deferred_resizes=` (expect `deferred_resizes=1` per windowed
session: the first DISPLAY), `graphics_output … draw_routes` lines now carry the owned pass's publication (`owner=none`).

## All-on clock (2026-10-10 23:3x) — detail (`int_work/m_series/cutover/allon_clock/`)

CUTOVER.md "DT-14 CLOCK CONTRACT" (game speed constant real time; the renderer never blocks the game thread; audio on the wall clock) and its
"Also" (F5 does nothing in `--harness-window`): two owner stages, two reviews APPROVED, integrated into `main` (`8bd76e91` → **`dc1a8a3a`**,
21 commits, ff-only from `integration-20261011-clock`; `cherry-pick -x`, 18 × `-` by patch-id + 1 context-only pick). Keys unchanged: 27 `hle.*`
on, `graphics_strips=all`; `gs.queue=pipelined`. No Config default changed; one new `--set`-only key `harness.inject_key=<F1..F12>@<frame>`.

| stage | owner / worktree | commits on `main` (subjects) | result |
|---|---|---|---|
| Clock contract rules 1–5 + the renderer decoupled | Zeno, `port-graphics-r2` 1210aef2 | `feat(timing): rule 1 — the 60 Hz wall edge, no surplus ticks` · `test(timing): rule 3 — elapsed vsyncs reach the title's T0` · `feat(gs): rule 2 — one pending frame, drop the older` · `feat(audio): rule 4 — wall-clocked device, pad with the tail` · `feat(host): rule 5 — game_hz/field_hz/render_fps census, F1 readout` · `fix(host): TickThread edges at exactly 1/60 s; census the ordered waits` · `feat(graphics): Owned deliveries never wait in play; RootClockContract pin` · `fix(graphics): Rule 2 — the bridge fence is the stream position in play` · `feat(gs): Rule 2 — local->local VRAM source served EE-side in play` · `fix(host): The EE's own frame time — late trace, mem lane off in play, pinned pacer` · `test(app): RootClockContract tightened to the DONE band` · `fix(host): Census windows count the edges answered, not the answer latencies` · `chore(host): mem_lane= published in both profiles` · `test(app): Rename the clock-contract fixture` | s3/s5 3000-frame play replays: `frame_waits=0 syncs=0 ordered_waits=0 sync_wait_ms=0`, bridge `waited=0`, audio `dropped=0 underruns=0`, game_hz in-loop 59/60 (s3, 42/44 at 60) and 58/60 (s5, 41/43 at 60), field_hz 60 everywhere; deterministic 1300 f IDENTICAL to main |
| F5/F8/F1 reach SessionControls from the game window | Bernoulli, `port-product` 5c2de09b | `feat(config): harness.inject_key=F5@N synthetic press knob` · `fix(host): Hotkeys reach SessionControls from the game window` · `test(root): RootPlayHotkeyF5 pins F5 through the real SDL key route` · `docs(test): RootPlayHotkeyF5 red-before is focus-dependent; unit is the deterministic red` · `docs(product): Record the F5 hotkey routing fix and its review loop 1` | `SessionHotkeyRoute` 15 checks (deterministic red on the old router), `RootPlayHotkeyF5` `Passed 28.33` (slot0.ps2st written through SDL's queue, `hotkeys=…discarded=0`) |
| integrator | — | `6a3a3bb4 fix(host): SetClockReadout body out of the header (LintOneClass)` · `dc1a8a3a fix(host): SDL event trace armed from dev.log_level, not a getenv` | `LintOneClass` and `CoreSingleGetenvSite` were red on the raw picks (neither review ran them); both green; trace via `--set dev.log_level=narrate` |

**What the clock contract is (Zeno; the review read every `Sync()` caller left):** the game boundary is a 60 Hz WALL event — `TickThread` edges
`anchor + k·1e9/60 ns` (rational; the truncated `16,666,666 ns` was 40 ns/period short), the wall-locked `BoundarySequencer` parks the EE to the
next edge and fires every noted tick 1:1 (`FirePending`), `EmuDomain::OnBoundary` tops nothing up, so `domain tick == tick_thread ticks` and
`absorbed=0`. The renderer is decoupled: `DrawIrThreadQueue::SetFrameSkip` (play only) keeps ONE pending frame and drops the older at the next
push (`frames_dropped=`; pushes never wait for ring room); the boundary no longer calls `WaitFrame`; the bridge's `hooks.delivery.drain` returns
without `Sync()` because every consumer input is an owned copy at the push (the group's fence IS its stream position), counted
`graphics_bridge_drain=stream_ordered=,waited=`; owned deliveries run on the consumer (`RunOrderedAsync` under `DrawIrQueueConsumer::RunOrdered`
for `Enter()`/`DrainPendingUpload` parity, `graphics_owned_async=`); the EE's local→local VRAM source is served from the EE image plus the
consumer's last completed frame snapshot for GPU-target pages (`ServeLocalToLocalSource`, staleness in `gs_queue_l2l=`; `local_local=0` on this
title — pinned, not exercised). Remaining `Sync()` callers: F5 capture (`SettleOwnedForCapture`, `GsSections`), `StopAfterQuiescence`, the
deterministic-profile drain, the uncounted leg-end `Quiesce()` — none on the 60 Hz path. A late boundary delivers its elapsed fields to the
title's T0 (`DerivedClocks::T0Counts` +9600 per field; TR `platform/ps2/time.cpp`, `game/clock.cpp`) — a bigger step, never slow motion.
Audio: `AudioPullRing` pad mode — a short pull repeats the previous quantum's tail (phase-continuous), re-latches at the next push, a late
catch-up burst is trimmed to the 50 ms lead (`padded=,relatches=,trimmed=,pad_with_tail=1`) — never a pitch shift, never a drop. Census: the
`game_hz=`/`field_hz=`/`render_fps=` series per wall second (a boundary belongs to the window of its first field's wall EDGE — `game_hz ≤ 60` by
construction, 60 = every edge answered), `pace_late_fields=`, `pace_slept_ms=`, `pace_late_trace=ordinal:work_us:fields` (the EE's own long
frames; `pace_surplus` retired), `mem_lane=off` in play (the harness's 34 MB guest-RAM FNV every 60 boundaries was a 38 ms frame on the game
thread; `on` under deterministic, `diff_legs` unchanged). F1: `game N Hz | render M fps`.

**What the F5 fix is (Bernoulli; the review verified the red arm on disk):** without a keyboard-focus window SDL stamps a press `windowID=0`;
`SessionGui::ProcessEvent` discarded it before `HandleFunctionKey`. Now ours unless SDL names a window that is not ours; `SessionWindow::Open`
raises + requests focus; a lost hotkey prints `nativeee: F<n> discarded: SDL stamped window id U (session window W)` and counts
`hotkeys=…discarded=`; `InjectFunctionKey(n)` pushes a real `SDL_KEYDOWN/KEYUP` into SDL's own queue (`harness.inject_key=F5@N`) — the
production route from `SDL_PollEvent` on. The `[sdl-trace]` per-event attribution trace is armed by `--set dev.log_level=narrate` (integrator).

**Acceptance (every line read; logs in `allon_clock/`):** Release per step 865 / 29 / 503 / 29 steps and the full rebuild 1833 steps, all
`FAILED=0 warning:=0 ld_warning=0`; focused 98 (1 red → fixed, 18/18) · 45 + RootPlayHotkeyF5 · 33 + RootPlayHotkeyF5; `gen_settings_table
--check` `reproducible (101 rows) -> PASS`; component suite ALONE `-j8` **`100% tests passed, 0 tests failed out of 610`** (121.43 s; 22 skips
`SKIPSET_SAME`); game group `-j1` (Debug `nativeee` rebuilt first, 0/0/0) **`81% tests passed, 18 tests failed out of 95`** (1355.80 s) — the 18 =
the frozen-anchor GCLK set of `ff12dca8` BY NAME (`RED_SET_SAME_AS_ff12dca8`; `root_fail_logs/anchor_refusals.txt`: `Restore needs owned logical
clock state.` ×4 classes), 0 other reds; passed `RootClockContract 47.11`, `RootPlayHotkeyF5 28.33`, `RootShadowLevelEntry 581.25`,
`RootGsQueuePipelinedIdentical 10.40`, `RootPlayRelaunchRestore 39.94`, `RootPlaySaveAtTitle 18.83`, `RootPlayModeSmoke 6.22`,
`RootGraphicsBridgeAllOn 15.94`, `RootPlayWallLock 8.22`, `RootMovieLifecyclePlay 24.72`. Re-test `-j1` on `main` after the merge and again on the
frozen bytes: RootClockContract, RootPlayHotkeyF5, RootPlayModeSmoke, RootGraphicsBridgeAllOn, RootPlaySaveAtTitle, RootPlayRelaunchRestore,
RootGsQueuePipelinedIdentical → **`100% tests passed, 0 tests failed out of 7`** ×2.

**DT-14 on the integrated Release (idle machine, one game process; 3000-frame play replays, `--harness-window`; `s{3,5}_play/leg/run.meta`):**

| replay | frame_waits | sync_wait_ms | ordered_waits | bridge waited | frames_dropped | audio dropped | underruns | game_hz ALL windows | in-loop game_hz | field_hz in-loop | render_fps in-loop | pace_late / trace |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| s3 | **0** | **0** | 0 | 0 | 3 | **0** | **0** | 0 / 60 (106 w; FMV + load) | **59 / 60** (44 w; 60→42, 59→2) | 60 / 60 | 55–61 | 31 — `1248:29972:2, 1885:43965:2` |
| s5 | **0** | **0** | 0 | 0 | 2 | **0** | **0** | 0 / 60 (69 w) | **58 / 60** (43 w; 60→41, 59→1, 58→1) | 60 / 60 | 57–60 | 10 — `1248:30298:2, 1352:56026:3` |
| s5 on `ff12dca8` | 4963 | — | — | — | 0 | 344896 | 1060 | — | — | — | — | 2366 |

Deterministic 1300-frame coldboot, integrated vs `ff12dca8` (same cwd): `diff_legs: IDENTICAL` (frames 1300 keys, audio 217 lines, mem 22 lines).
The in-loop misses (2 per replay) are the EE's own frame time with every renderer-wait row 0 — boundary **1248** (systematic, ~30 ms) and one
sporadic 44–56 ms frame; rule 3 delivered their fields (field_hz 60). DT-14 in DECISIONS_LOG records the carve-out for the user's decision.

**Binary (frozen):** `int_work/m_series/cutover/allon_noexc/bin/nativeee_release.dc1a8a3a`, `PS2RECOMP-BUILD-ID 2026-10-10 23:21:02`, sha256
`ee57499e0dd80e608ab1b891eb56171b9170a3258a317e278ac02984a15acc4a` (`BIN_FROZEN_OK`; `.ff12dca8` kept; `.96c8525c` deleted for disk, `bin/DELETED.txt`).
Play command: INTEGRATION_STATUS.md "All-on build, no exceptions" with this binary.

**Census rows added for the user's exit `census:` block:** `game_hz=<v>,<v>,…` / `field_hz=` / `render_fps=` (one value per wall second),
`frames_dropped=`, `pace_late_fields=`, `pace_slept_ms=`, `pace_work_max_ms=`, `pace_late_trace=ordinal:work_us:fields,…` (first 64),
`mem_lane=off|on`, `graphics_bridge_drain=stream_ordered=,waited=`, `graphics_owned_async=on=,delivered=,ran=,not_completed=,failed=`,
`gs_queue … frame_skip=,ordered_waits=,ordered_wait_ms=,sync_wait_ms=`, `gs_queue_l2l=ee_served=,snapshot_served=,stale_frames=,stale_max=,unserved=,snapshots=`,
`audio_ring … padded=,relatches=,trimmed=,pad_with_tail=`, `hotkeys=key_events=,function_keys=,discarded=`. `pace_surplus` is gone. Expected in
play: `frame_skip=1`, `frame_waits=0`, `sync_wait_ms=0`, `waited=0`, `dropped=0`, `underruns=0`, `pad_with_tail=1`, `mem_lane=off`, `discarded=0`.

## Standing charter wave 1 (2026-10-11 03:5x) — detail (`int_work/m_series/cutover/allon_w1/`)

CUTOVER.md "STANDING CHARTER": three tracks in parallel, each stage reviewed, the approved ones integrated under the DT-14 invariant (Release
0/0/0, ctest green, the clock pins green, deterministic lanes identical). Five stages reviewed; **four APPROVED and three integrated**
(`main 16da6dd1` → code HEAD **`1bb648b5`**, 12 commits, ff-only from `integration-20261011-w1`; `cherry-pick -x` 12 × `-` by patch-id); one
APPROVED stage **REVERTED** by the invariant (2c); one **NEEDS_CHANGES** stage not picked (1c). Keys: **28 `hle.*` on** incl. the new
`hle.startup=native` (`enum(retained|native)`, deterministic profile pins `retained`), `graphics_strips=all`; `gen_settings_table --check` →
`reproducible (102 rows) -> PASS`.

| stage | owner / worktree | commits on `main` (subjects) | result |
|---|---|---|---|
| 3a M11 service-only startup | Herschel, `port-services-r3` 07f44f9f | `feat(startup): M11 service-only startup -- the IOP never boots in play` · `docs(startup): Record the service-only startup owners and the retired IOP model counters` · `test(startup): Lint-clean the StreamTransport pin; two engine IOP counters` | play: `startup=native iop_boot=0 iop_modules= iop_hb_beats=0 stream_rpc_calls=0/0 libsd_calls=`, `stream_transport … unowned=0 malformed=0 server_calls==batches` (12311 s3 / 12310 s5), `service_startup=stream=native,sound=native,retained=none,consumers=0`; `RootServiceOnlyStartup: ALL PASS` (music + SFX audible under native); s5 **peak_mb 609 → 481** |
| Bernoulli product: recorder clock + F8-live design | Bernoulli, `port-product` 330cbc75 | `fix(recorder): Key the input recording by the game frame, not the tick` · `test(root): RootPlayRecordReplay pins play-recorded replay determinism` · `docs(product): F8 live-restore design; s5 replay re-check recorded` · `docs(verify): The padlog's clock is the game frame (clock=frame header)` · `docs(product): Record the recorder-clock fix (round 9) and its pins` | `RootPlayRecordReplay: ALL PASS` (a play `--harness-window` recording replays deterministically: frames 600 keys / audio 100 lines identical to the direct script run); `pad_record_clock=frame`; earlier tick-clock padlogs unchanged |
| 1a + 1b native VU1 kick lowering | Socrates, `port-math` 7441b7c0 | `feat(vu): Lower the material's A+D register block direct` · `docs(graphics): MESH.md and OLD-bug pins for the register-block lowering` · `feat(vu): Lower every single-tag PACKED kick direct (track 1b)` · `docs(graphics): MESH.md records the track 1b census (unsupported=0)` | `KickLowering: 510 checks, 0 failures`, `LOWERING-PARITY … unsupported=0 parity_failures=0`, `GIF PARSER EQUIVALENCE PASS`; play `graphics_kick_lowering_worker … raw=0,unsupported=0` on s3 (was 209833) and s5 (was 215674), `_unsupported_by_program=none`; 5 OLD-bug pins (`kick_lowering_old_bug_pins.tsv`) |
| 1c retire the DMAC→VIF→GIF parse in play | Zeno, `port-graphics-r2` 65efedee | — (not picked) | review NEEDS_CHANGES: PATH1 byte parse retired (`gif_parse path1` 211668 → 92) but `gif_parse_executed=1`, `gs_sink` 1.36 M tags / 30 M kicks on s5 (PATH2 943.6 MB per 3000 f). Parent to re-scope or hold |
| 2c owned screen effects | Lagrange, `port-textures` 0dfdb622 | — (picked, then REVERTED: `backup/integration-20261011-w1-with-2c` 90192a88) | `RootPlaySaveAtTitle` red with 2c in: `savestate=refused,No admissible boundary in 120 consecutive boundaries; the last owner to refuse: graphics_screen: screen effects boundary is executing or a reserved frame is undelivered` — `ScreenPass::CanCheckpoint()` has no open-recording arm (`ShadowPass`/`DistortionPass::CanCheckpoint(bool unlinked_recording_open)` do); `graphics_owned_output … pending=5` at every title boundary under the pipelined queue. Also `CoreSingleGetenvSite` red (`test_screen_pixels.cpp:154: getenv`; integrator fix in the backup branch) and a 3-way `test_config.cpp`/settings-table collision with 3a (resolved there: `count_on` 29, soft-backend notes 14, `notes[8]`) |

**Acceptance (every line read; logs in `allon_w1/`):** Release per step 526 / 369 / 148 steps `FAILED=0 warning:=0 ld_warning=0` and the full
`--clean-first` rebuild of the merged set **1889 steps, 0/0/0**; focused ctests per step 50/50 · 56/56 · 52/52 (3 pre-existing OLD-tool skips);
component suite ALONE `-j8` **`100% tests passed, 0 tests failed out of 611`** (127.44 s; 22 skips `SKIPSET_SAME`); game group `-j1` (Debug
`nativeee` rebuilt first, 0/0/0) **`82% tests passed, 18 tests failed out of 99`** (1463.36 s) — the 18 = the frozen-anchor GCLK set of
`dc1a8a3a` BY NAME (`RED_SET_SAME_AS_dc1a8a3a`; `Restore needs owned logical clock state.`), **0 other reds**; passed `RootClockContract 47.08`
(`ALL PASS`, `EE_OWN_TIME windows = 2 <= 3`), `RootGraphicsBridgeAllOn 15.79`, `RootShadowLevelEntry 585.86`, `RootPlaySaveAtTitle 19.84`,
`RootPlayRelaunchRestore 40.04`, `RootPlayHotkeyF5 28.37`, `RootGsQueuePipelinedIdentical 10.40`, `RootServiceOnlyStartup 28.54`,
`RootPlayRecordReplay 43.16`, `RootPlayModeSmoke 6.24`, `RootMovieLifecyclePlay 24.77`, `RootPlayWallLock 6.19`.

**DT-14 on the integrated Release (idle machine, one game process; 3000-frame play replays, `--harness-window`; `s{3,5}_play/leg/run.meta`):**

| replay | frame_waits | sync_wait_ms | ordered_waits | bridge waited | frames_dropped | audio dropped | underruns | in-loop game_hz | field_hz | render_fps p50 / p95 | pace_late (trace) | unsupported | peak_mb | gs_sink tags / kicks |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| s3 | **0** | **0** | 0 | 0 | 3 | **0** | **0** | **57 / 60** (44 w; 60→43, 57→1 own-time) | 60 / 60 | **60.0 / 60.2** | 38 — `1178:53289:3, 1179:37121:2` | **0** (was 209833) | 602 (597) | 1266676 / 27937250 |
| s5 | **0** | **0** | 0 | 0 | 3 | **0** | **0** | **57 / 60** (44 w; 60→42, 58→1, 57→1 own-time) | 60 / 60 | **60.0 / 60.0** | 14 — `1178:45510:2, 1179:39846:3, 1423:55355:3` | **0** (was 215674) | **481** (609) | 1551221 / 34519218 |

Deterministic 1300-frame coldboot, integrated vs `dc1a8a3a` (same cwd): **`diff_legs: IDENTICAL`** (frames 1300 keys, audio 217, mem 22;
`drawir.ohash`/`gsops.ohash` byte-identical; env_hash differs by the declared `hle.startup` pin only). **Frozen:**
`allon_noexc/bin/nativeee_release.1bb648b5`, BUILD-ID `2026-10-11 03:08:39`, sha256 `0b1681b36f7e9cfd314a193c05edfee1bd3c026133a071face122c74df002828`
(`.dc1a8a3a` kept, `.ff12dca8` deleted). Expected new census rows in play: `startup=native`, `iop_boot=0`, `service_startup=stream=native,sound=native`,
`graphics_kick_lowering_worker … raw=0,unsupported=0`, `pad_record_clock=frame`; `--set hle.startup=retained` restores the IOP boot (oracle arm).

## Standing charter wave 2 (2026-10-11 13:2x) — detail (`int_work/m_series/cutover/allon_w2/`)

Five APPROVED stages (3b, F8 live, 2b, 1c+1d, 2c fix + 2a) integrated via `integration-20261011-w2` from `main de48cfb4` under the DT-14
invariant; `git merge --ff-only` → code HEAD **`3c47bd96`** (49 commits; `git cherry main <owner>` 4/8/4/18/8 × `-`). **2a REVERTED** by the
invariant (a play-mode memory runaway on the first level); 2c stays. Keys: **29 `hle.*` rows**, new `hle.graphics_screen=on`,
`save.restore_mode=live`, `video.scale=1`; `gen_settings_table --check` → `reproducible (104 rows) -> PASS`.

| stage | owner / worktree | commits on `main` (subjects) | result |
|---|---|---|---|
| 3b M12 D1 | Herschel, `port-services-r3` 2093c350 | `docs(retirement): M12 deletion ledger …` · `feat(iop): Delete the SPU2 register window on the IOP bus (M12 ledger D1)` · `docs(coverage): …` · `docs(retirement): Ledger D1 acceptance …` | the window is out of the link (`nm` 0 symbols); lanes identical; `iop_bus_window_hits` row gone; ledger §3 asks the IOP-move decision |
| 3a part 2: F8 LIVE restore | Bernoulli, `port-product` ce8bbcd7 | `feat(config): save.restore_mode=live|relaunch …` · `feat(product): F8 restores LIVE in the running session` · `test(product): LiveRestoreGate …` · `feat(product): Forget the old session's picture at the live F8; RootPlayLiveRestore` · `test(product): RootPlayRelaunchRestore pins the relaunch route by setting` · `style(gs): PresentPolicy::ForgetImage out of line` · 2 docs | `RootPlayLiveRestore 110.95 s` (`restore_route=live`, counters continuous across the restore, one window bind), `RootPlayRelaunchRestore 41.00`, `RootPlaySaveAtTitle 13.55`, `RootPlayHotkeyF5 28.43`; `f8=live` in run.meta on an F8 press |
| 2b owned sprites | Zeno, `port-math` b9d4a2db | `feat(textures): Own every sprite-only UI texture at load (track 2b)` · `fix(host): Draw the untextured flat Sprite at its slot; name slot refusals` · `test(root): RootSpriteTitleLogo …` · docs | play `graphics_sprites … model_calls=0` on s3/s5/level (11597 / 12247 / 12171 replaced), `graphics_textures loaded=61`; `RootSpriteTitleLogo: ALL PASS` (grab + PRESENTED frame); peak_mb s5 481 → 549 (the owner's A/B: +76) |
| 1c census (rebased) + 1d | Zeno, `port-graphics-r2` c68dbfa7 | `feat(gif): Census of the byte-form GIF parse per path` + 3 docs · `feat(config): video.scale defaults to 1 in play` · `perf(gs): Stage the IMAGE upload store per 64-byte column` · `perf(host): The play boundary reads the grab's hash, not its pixels` · `perf(texsrc): A decode reads its footprint, not the 4 MiB view` · `perf(gs): The flushed draw moves into the queue record` · `feat(census): ee_cpu_ms` · `perf(gs): The DrawIR queue signals only a sleeper` · `perf(host): The game thread runs at the interactive QoS` · `perf(gs): Row-segment walk for the IMAGE words inside a transfer` · `perf(gs): The mirror import walks its band once, not the image` · `fix(gs): The mirror band read and its de-swizzle walk share one stride (1d review F1)` · docs | **DONE-1: the level route (RootShadowLevelEntry's padlog, 7600 f, play, scale 1) beach render_fps p50 60.0 / p95 60.3 / min 59, game_hz 60, EE 14.3 ms/frame**; s3/s5 own-time windows 0 (the 1178/1179 boundaries gone); 1c's `gs_sink → 0` OPEN: `gif_parse path1=92` receives (retired), `path2=123335/535273488` (s3), `gs_sink tags 1177310 kicks 27151398` |
| 2c owned screen effects + checkpoint arm | Lagrange, `port-textures` 996c6bc6 (2c part) | the seven (`feat(render): Depth-byte palette serve …` · `feat(screen): Owned blur/filter/wave pass …` · `feat(screen): Selected arm of the DrawScreenEffects boundary …` · `feat(config): hle.graphics_screen …` · docs · `test(root): RootScreenFilterLevel …` · docs) · `fix(test): ScreenPixels debug dump armed by --debug` · `fix(screen): Admit the reserved frame at a settled boundary (open-recording arm)` · docs · **`fix(screen): Screen reservation guard built in place (screen half of 5d5297b1)`** (integrator partial pick) | `graphics_screen_selection=owned`, s3 `graphics_screen_effects=calls=2926,…,model_calls=0,replaced=2926,filter_off=1107,refused=0,faults=0`, `graphics_screen_scene=passes=1816,…,failures=0`; `RootScreenFilterLevel 529.90 s` ALL PASS (scale 2, stated by the pin since `53d36a49`); `RootPlaySaveAtTitle` written at 1300 with bucket 4 among the unlinked items (the wave-1 revert's refusal gone); det frames differ at keys 1251–1300 only (the attract shot under the owned filter, 0.4/channel) |
| 2a the sky owned | Lagrange, `port-textures` (`dcf6fd3e 861eb5ec 18699cda 5d5297b1(sky) 996c6bc6`) | — (picked, then REVERTED: `backup/integration-20261011-w2-with-2a` 33b09065) | level route in PLAY: `peak_mb=1654` (`b3600:503 → b4200:921 → b7560:1600`, `graphics_owned_output pending=317`) vs 1d's 672 flat; `hle.graphics_sky=off` → 550 flat; `hle.graphics_screen=off` / `graphics_sprites=off` → `park=exception "Shadow pass limit"` at 5094 / 5391 (3.2 / 3.5 GB). Pins all green with 2a (incl. `RootSkyAttractFrame`); the owner's deterministic beach leg (492) does not show it |

**Acceptance (every line read; logs in `allon_w2/`):** Release per step 567 / 510 / 222 / 801 / 627 steps `FAILED=0 warning:=0 ld_warning=0`
and the full `--clean-first` rebuild **1905 steps, 0/0/0**; focused ctests per step 69/69 · 78/78 · 50/50 · 144/144 · 90/90 (after the
`RootScreenFilterLevelSelftest` scale fix); component suite ALONE `-j8` **`100% tests passed, 0 tests failed out of 644`** (142 s; 24 skips);
game group `-j1` (Debug `nativeee` rebuilt first) **`76% tests passed, 19 tests failed out of 78`** (2209 s) — the 18 GCLK frozen-anchor reds BY
NAME + `RootSpriteTitleLogo` killed from outside at frame 1856 (exit 137, no runner line, no jetsam) and **green alone (67.64 s, ALL PASS)**;
0 other reds. Passed `RootClockContract 46.67` (`ALL PASS`, `EE_OWN_TIME windows = 0 <= 3`, `game_hz min/max=60/60`), `RootGraphicsBridgeAllOn
10.42`, `RootShadowLevelEntry 331.20` (scale 1) + `Scale2 529.96`, `RootScreenFilterLevel 529.90`, `RootSpriteTitleLogo 67.64`, `RootPlaySaveAtTitle
13.55`, `RootPlayRelaunchRestore 41.00`, `RootPlayLiveRestore 110.95`, `RootPlayHotkeyF5 28.43`, `RootGsQueuePipelinedIdentical 10.39`,
`RootServiceOnlyStartup 20.40`, `RootPlayRecordReplay 38.80`, `RootTriageBundleOnFault 9.39` (fault at 1300 since `3c47bd96`), `RootPlayModeSmoke
6.14`, `RootMovieLifecyclePlay 24.66`.

**DT-14 on the integrated Release (idle machine, one game process; `--harness-window`; `s{3,5}_play/leg/run.meta`, `level_play/leg/run.meta`):**

| replay | frame_waits | sync_wait_ms | ordered_waits | bridge waited | frames_dropped | audio dropped | underruns | in-loop game_hz | field_hz | render_fps p50 / p95 | pace_late | unsupported | peak_mb | gs_sink tags / kicks |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| s3 (3000 f) | **0** | **0** | 0 | 0 | 0 | **0** | **0** | **60 / 60** (44 w, own-time 0) | 60 / 60 | **60.0 / 60.0** | 12 | **0** | 564 (602) | 1177310 / 27151398 |
| s5 (3000 f) | **0** | **0** | 0 | 0 | 0 | **0** | **0** | **60 / 60** (44 w) | 60 / 60 | **60.0 / 60.0** | 7 | **0** | 549 (481) | 1480017 / 34088509 |
| level (7600 f, scale 1) | **0** | **0** | 0 | 0 | 0 | **0** | **0** | 59 / 60 (119 w; one own-time at the load) | 60 / 60 | **beach 60.0 / 60.3**, min 59 | 117 | **0** | 553 | 21.3 M / 470 M |

Deterministic 1300-frame coldboot vs wave 1: `audio.ahash: identical`; `frames.fhash` first divergent key 1251 (50 keys: the owned colour
filter on the attract shot, 2c-attributed); `mem.mhash` from key 60, `drawir`/`gsops.ohash` from key 62 (owned sprites/screen effects leave the
DrawIR stream; 2b/2c-attributed). **Frozen:** `allon_noexc/bin/nativeee_release.3c47bd96`, BUILD-ID `2026-10-11 12:02:23`, sha256
`9e1a7a446afa21b4b2aa4689ec5fcf57d83f820ab3393d516fe9bd54b3244964` (`.1bb648b5` kept, `.dc1a8a3a` deleted). Expected new census rows in play:
`video_scale=1`, `graphics_screen_selection=owned`, `graphics_screen_effects`/`graphics_screen_scene`, `graphics_sprites … model_calls=0`,
`ee_cpu_ms`, `gif_parse`/`gs_sink_by_path`, `f8=live`; sky rows still on the model (`graphics_sky_start … model_calls>0`, `graphics_refusals_sky
missing_resource`).

## Standing charter wave 3 (2026-10-12 02:3x) — detail (`int_work/m_series/cutover/allon_w3/`)

Three stages (3b D2/D3, 1c-2, 2a fix + 2d) integrated via `integration-20261012-w3` from `main af82bb9d` under the DT-14 invariant;
`git merge --ff-only` → code HEAD **`e1247812`** (22 commits; `git cherry HEAD <owner>` 5/10/2 × `-`). **2a REVERTED** by the invariant (its own
pin `RootSkyAttractFrame` red on the integrated composition: the owned sky reads the DiskManager handle at object+0x60 that textures-at-load
replaces with the ownership word); the sky-independent 2d parts stay as picks. Keys: **30 `hle.*` rows**, new `hle.graphics_textures=on`;
`gen_settings_table --check` → `reproducible (105 rows) -> PASS`.

| stage | owner / worktree | commits on `main` (subjects) | result |
|---|---|---|---|
| 3b M12 D2/D3 | Herschel, `port-services-r3` 60f7bbf7 (review NEEDS_CHANGES on one test-only item → integrator pin) | `feat(iop): Split the IOP arm into ps2r_iop_models; nativeee links none of it` · `test(app): Re-home the oracle-arm Root pins to nativeee_oracle; pin the play binary` · `fix(verify): battery.py usage text passes the engine purity lint` · `test(app): RootTriageBundleOnFault pins the degrade line with hle.movie_wait` · `docs(ledger): Review nits …` · **`test(config): Pin the service-only startup refusals per consumer key`** (integrator `53e99d9e`) | two binaries from one source: `nativeee` (PLAY, `iop_linked=0`, `LintPlayBinaryNoIop` 0 IOP symbols) and `nativeee_oracle` (the retained arm; 33 ctest rows re-pointed); the 12 native-startup consumer keys REFUSE (exit 2, key named) on play instead of a silent IOP boot; `--profile deterministic` on play → `startup=native` + `[config]` note; oracle lanes IDENTICAL to the frozen `3c47bd96` (frames/audio); peak_mb s3 564 → 532, s5 549 → 514 |
| 1c-2 textures at load | Zeno, `port-graphics-r2` c0ebf0c5 | `feat(texsrc): Owned texture slots …` · `feat(textures): Textures at load -- every record converts, the slot upload binds it (hle.graphics_textures)` · `feat(census): The parse's remainder named -- PATH2 by class, PATH1 by bucket, the library's declines by program` · `fix(host): The texture snapshot preflight sees the textures service` · `fix(config): hle.graphics_textures degrades with the Soft backend` · `fix(host): GraphicsProducers install under textures-at-load with a UI producer off` · `fix(mtvu): A flushed staged batch carries only its bytes` · `fix(census): The bucket hint travels in stream order` · `perf(vram): NoteWriteNoBytes marks per page run, not per byte` · `docs(graphics): Track 1c-2 -- the M8 bucket handoff …` · `test(root): The retained Graphics arm states hle.graphics_textures=off` | PATH2 IMAGE transport retired: s3 `gif_parse path2 53471/7169120` (was 123335/535273488), `gs_sink image_qw=0`, `gs_xfer trxdir=0 owned=35280`, `texres decodes=0 prep_bytes=0 owned_misses=0`; `graphics_textures=loaded=319,destroyed=8,…,current=311` (lifetime = the title's TextureRead/TextureDestroy); level route EE 14.3 → 12.68 ms/frame, peak_mb 553 → 509; `gs_sink → 0` still OPEN (remainder per class / per bucket, INTERFACES.md §15) |
| 2d (sky-independent part of Lagrange's stage) | Lagrange, `port-textures` 61bea756 | `fix(shadows): A bracket past 64 undelivered reservations is refused, not faulted` · `feat(census): The VU1 library's misses attributed by (variant hash, entry pc)` · `test(root): RootShadowLevelEntry pins the route's peak_mb` (partial: shadow hunk) · `test(root): RootShadowLevelEntry pins Crash (skin + blend-shape) in the presented frame` · `docs(shadows): Record the shadow limit refusal` (partial: OWNERSHIP.md hunk) | `RootShadowLevelEntry 313.90 s` ALL PASS: `peak_mb=450` ≤ 1000, `misses=0`, `classes skinned=2205830 indexed-displace=2846759`, Crash's fur `(124.8, 45.4, 4.1)` over the footprint in grab + presented frame; `graphics_shadow … limit_refusals=0,faults=0`; `vu1_hle_worker_misses_by_variant=0xb4c16758d3c6564c:0x1ac8:113` on the level route (one variant) |
| 2a the sky owned (+ its VU1 residency fix) | Lagrange, `port-textures` (`fd915433 c4a3314c 30a11388 3751f69f 6026f5dd d6a8ff7e` + sky halves of `adf38155` `c47d1c6f`) | — (picked, then REVERTED: `backup/integration-20261012-w3-with-2a` 7ca727b5) | `RootSkyAttractFrame` FAIL (6): `graphics_sky_start replaced=0,model_calls=121`, `graphics_refusals_sky total=121,missing_resource=121 … detail:6`, `graphics_sky_assets models_loaded=296,textures_loaded=0,declines_memory=319,…,programs_uploaded=0` — `SkyAssets::TextureLoaded` reads object+0x60 as the DiskManager handle; `TextureBindings::Read` (1c-2) writes `0xffffffff` there for every record. The model arm draws the sky (frame checks PASS). Fix = consume `TextureAssets::Find` (§15 bucket 0) |

**Acceptance (every line read; logs in `allon_w3/`):** Release per step 948 / 8 / 726 / 328 / 325 steps `FAILED=0 warning:=0 ld_warning=0` and
the full `--clean-first` rebuild **1978 steps, 0/0/0**; focused ctests per step 97/97 · 11/11 · 194/194 · 159/159 · 86/86; component suite
ALONE `-j8` **`100% tests passed, 0 tests failed out of 652`** (146 s; 24 skips = wave 2's); game group `-j1` (Debug `nativeee` +
`nativeee_oracle` rebuilt first) **`78% tests passed, 18 tests failed out of 80`** (2160 s) — EXACTLY the 18 GCLK frozen-anchor reds BY NAME;
0 other reds. Passed `RootClockContract 47.39` (`ALL PASS`, `EE_OWN_TIME windows = 1 <= 3`, `frame_waits=0 dropped=0 underruns=0`),
`RootGraphicsBridgeAllOn 10.82`, `RootShadowLevelEntry 313.90` + `Scale2 517.07`, `RootSpriteTitleLogo 67.41`, `RootScreenFilterLevel 519.12`,
`RootPlaySaveAtTitle 13.69`, `RootPlayRelaunchRestore 40.60`, `RootPlayLiveRestore 110.11`, `RootPlayHotkeyF5 27.63`, `RootGsQueuePipelinedIdentical
10.48`, `RootServiceOnlyStartup 16.38`, `RootPlayBinaryServiceOnly 0.43`, `RootPlayRecordReplay 39.32`, `RootTriageBundleOnFault 8.47`,
`RootPlayModeSmoke 4.22`, `RootMovieLifecyclePlay 24.80`.

**DT-14 on the integrated Release (idle machine, one game process; `--harness-window`; `s{3,5}_play/leg/run.meta`, `level_play/leg/run.meta`):**

| replay | frame_waits | sync_wait_ms | ordered_waits | bridge waited | frames_dropped | audio dropped | underruns | in-loop game_hz | field_hz | render_fps p50 / p95 | pace_late | unsupported | peak_mb (w2) | gs_sink tags / kicks / image_qw | iop_linked |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| s3 (3000 f) | **0** | **0** | 0 | 0 | 0 | **0** | **0** | **59 / 60** (43 w; one own-time) | 60 / 60 | **60.0 / 60.3** | 25 | **0** | **532** (564) | 1078030 / 27294491 / 0 | 0 |
| s5 (3000 f) | **0** | **0** | 0 | 0 | 0 | **0** | **0** | **59 / 60** (43 w; one own-time) | 60 / 60 | **60.0 / 60.4** | 18 | **0** | **514** (549) | 1347974 / 34089704 / 0 | 0 |
| level (7600 f, scale 1) | **0** | **0** | 0 | 0 | 1 | **0** | **0** | **60 / 60** (119 w) | 60 / 60 | **beach 60.0 / 60.3**, min 59 | 21 | **0** | **509** (553) | 21.1 M / 494.9 M / 0 | 0 |

Deterministic 1300-frame coldboot: the ORACLE binary vs wave 2's frozen main `frames.fhash`/`audio.ahash` IDENTICAL, `mem.mhash` from key 120 and
`drawir`/`gsops.ohash` from line 1248 (1c-2-attributed); the PLAY binary diverges at key 443 / 199 / 0 by the declared `hle.startup` selection
(3b D2/D3-recorded). **Frozen:** `allon_noexc/bin/nativeee_release.e1247812`, BUILD-ID `2026-10-12 01:14:46`, sha256
`10a74019a9998f01f17f715297932f2162b10b3e0c201646e2eab1ca604726d2` (`.3c47bd96` kept, `.1bb648b5` deleted). Expected new census rows in play:
`iop_linked=0`, `graphics_textures=…model_calls=0`, `gif_path2_by_class=image:0/0,…`, `gs_sink … image_qw=0`, `gs_xfer trxdir=0 … owned=`,
`texres … decodes=0,prep_bytes=0`, `texres_owned`, `gs_sink_path1_by_bucket=exact=1,…`, `vu1_hle(_worker)_misses_by_variant`,
`graphics_shadow … limit_refusals=`; sky rows still on the model (`graphics_sky_start … model_calls>0`, `graphics_refusals_sky missing_resource`).

## Standing charter 2 wave 1 (2026-10-12 06:5x) — detail (`int_work/m_series/cutover/allon_c2w1/`)

Five stages (B1 Linux, B2/C3 Windows groundwork + build scripts, A menu + C4, A1 widescreen, A4 texture packs + 2a sky) integrated via
`integration-20261012-c2w1` from `main 636c797f` under the DT-14 + all-on invariant; `git merge --ff-only` → code HEAD **`729a321e`**
(35 commits; `git cherry HEAD <owner> 636c797f` 25 × `-`, 7 × `+` = conflict-resolved picks). **Nothing reverted**: every stage's own pin and
the charter-1 Root set are green on the integrated composition, incl. `RootSkyAttractFrame` (2a's third attempt, re-expressed on
textures-at-load — the two prior reverts' cause closed). Keys: **110 rows** (`gen_settings_table --check` PASS); 30 `hle.*` unchanged.

| stage | owner / worktree | commits on `main` (subjects) | result |
|---|---|---|---|
| B1 Linux + the ONE host header | Herschel, `port-linux` 5f983d76 (review NEEDS_CHANGES on GCC-lane items → integrator `a78c226a`) | `feat(platform): ONE host header engine/platform/host/platform.h` · `build(linux): linux-release/linux-dev presets, SDL2 found-or-fetched, GL one way` · `fix(build): GCC 13 -Wall -Wextra -Werror clean; PS2R_FENV_ACCESS_ON` · `test(verify): platform_identity.py + the macOS s3 reference lanes` · `ci(linux): Dockerfile + build script + CI stub; the Linux lane's findings` · integrator `fix(tests): The three oracle generators emit the ONE FENV spelling` | macOS unchanged (646/646 in the review's run; the s3 lanes byte-identical to `e1247812` — the review's own replay); here 853 steps 0/0/0, focused 122/122 |
| B2 Windows groundwork + C3 | Socrates, `port-windows` 50d6976f | `build(recomp): Shard split as a generator knob; PCH + object lib for the shards` · `feat(platform): Host shim header with the Windows arm; windows presets; CI stub` · `feat(dist): build.sh / build.ps1 developer path; README for users and developers` · `feat(platform): Windows target folder -- PlatformWindows over the host arm` · integrator `build(platform): The two host-header arms merged; the Windows target in the ONE CMake site` | the ONE header carries both arms (section A `ps2r::platform::host`, section B `ps2r::host`); `PlatformHostShims` Passed; 337 steps 0/0/0, focused 63/63 |
| A menu framework + C4 | Bernoulli, `port-product` 60208f4d | `feat(paths): C4 user data root; paths.* under it; BUILD-ID stamp` · `feat(gui): ui.perf_overlay + SettingsFile writer; F1 is the dev panel` · `feat(menu): MenuExtension -- Display/Controls pages in the title's menu` · `feat(menu): Compose MenuExtension in play; RootMenuPerfOverlay pin` · `refactor(host): MenuComposition owns the menu install; budget row` | `RootMenuPerfOverlay 157.70` ALL PASS; 582 steps 0/0/0, focused 59/59, table 109 rows |
| A1 widescreen | Zeno, `port-graphics-r2` 371143cd | `feat(graphics): video.aspect -- the title's screen shape owned (A1)` · `test(root): RootWidescreenBeach pins the 16:9 presented frame; the Display-page hook` · `refactor(graphics): AspectSelection::Shape -- the GS present layer includes no Config` · `test(root): RootWidescreenBeach judges the save dialog's widget fit (p2900)` | `RootWidescreenBeach 574.49` ALL PASS; 585 steps 0/0/0, focused 95/95 |
| A4 texture packs + 2a sky | Lagrange, `port-textures` 9e47eb76 | the 8 sky commits (`fix(sky): The producers read the META arena` … `fix(sky): The sky takes a converted texture from the owned record`) · `feat(core): Png -- the host's one PNG codec` · `feat(config): video.texture_pack, paths.data_dir and --dump-textures` · `feat(textures): A4 texture packs by content hash and --dump-textures` · `test(root): RootTexturePackTitle pins a texture pack in the presented frame` · `docs(textures): TEXTURE_PACKS.md …` · integrator `refactor(paths): The texture pack dir joins the C4 layout (paths.texture_packs)` | `RootSkyAttractFrame 28.96` + `RootTexturePackTitle 43.43` ALL PASS; 714 steps 0/0/0, focused 88/88, table 110 rows |

**Acceptance (every line read; logs in `allon_c2w1/`):** the full `--clean-first` rebuild **2020 steps, 0/0/0**; component suite ALONE `-j8`
**`100% tests passed, 0 tests failed out of 661`** (25 skips = wave 3's 24 + `VerifyPlatformIdentity`); game group `-j1` **`80% tests passed,
18 tests failed out of 88`** (2981 s) — EXACTLY the 18 GCLK frozen-anchor reds BY NAME; 0 other reds. `RootClockContract 47.26` `ALL PASS`
(`frame_waits=0 dropped=0 underruns=0`, every in-loop window 60).

**DT-14 on the integrated Release (one game process; `--harness-window`; isolated `paths.user_data`; `s3_play/leg/run.meta`, `level_play/leg/run.meta`):**

| replay | frame_waits | sync_wait_ms | ordered_waits | bridge waited | frames_dropped | audio dropped | underruns | in-loop game_hz | field_hz | render_fps p50 / p95 | pace_late | unsupported | peak_mb (w3) | sky owned | iop_linked |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| s3 (3000 f) | **0** | **0** | 0 | 0 | 0 | **0** | **0** | **60 / 60** (44 w) | 60 / 60 | **60.0 / 60.3** | 25 | **0** | **565** (532) | `replaced=1778,model_calls=0`, refusals 0 | 0 |
| level (7600 f, scale 1) | **0** | **0** | 0 | 0 | 1 | **0** | **0** | **59 / 60** (118 w; one own-time) | 60 / 60 | **beach 60.0 / 61.0**, min 59 | 42 | **0** | **531** (509) | `replaced=6379,model_calls=0` | 0 |

**Frozen:** `allon_noexc/bin/nativeee_release.729a321e`, BUILD-ID `2026-10-12 05:43:02`, sha256
`79a7ee3e994ed8290b9ad7adbbdff75a546ad5fe7e6a3646e017a2cb440514ed` (`.e1247812` kept, `.3c47bd96` deleted). **macOS artifact:**
`allon_noexc/dist/nativeee-macos-729a321e.zip`, sha256 `735a4b0e21a1168d7ab1fec4a69463394d3b975b39f6a929e3609835dc23359d` (`./build.sh
--dist-only`; asset scan clean: format constants only, 0 ELF byte-window hits). The user data root is now `~/Library/Application
Support/NativeEE` (settings `ps2r.ini`, `memcards/`, `logs/`, `textures/`). New census rows: `user_data_root=`, `game_files_stamp=`, `menu_ext=`,
`menu_ext_pages=`, `video_aspect=`, `graphics_texture_pack=`, `graphics_texture_dump=`; the sky rows read `model_calls=0` /
`graphics_refusals_sky total=0` in play.

## Standing charter 2 wave 2 (2026-10-12 20:4x) — detail (`int_work/m_series/cutover/allon_c2w2/`)

Five stages (C1 Linux artifact + the GCC lane, B3 CI matrix, C2 launcher + A5 QoL, A2 Display rows + the A3 scaffold, A2 texture filtering +
the 2d cloth census) integrated via `integration-20261012-c2w2` from `main f8cb78cd` under the DT-14 + all-on invariant; `git merge --ff-only`
→ code HEAD **`d67e84e0`** (40 commits; `git cherry HEAD <owner> f8cb78cd` 31 × `-`, 5 × `+` = conflict-resolved picks + one skipped-as-empty).
**Nothing reverted**: every stage's own pin and the charter-1 Root set are green on the integrated composition; the one non-GCLK red of the
game group (`RootMenuPerfOverlay`) was the pin's six-row items box under the eight-row page — widened, re-run Passed. Keys: **122 rows**
(`gen_settings_table --check` PASS); 30 `hle.*` unchanged.

| stage | owner / worktree | commits on `main` (subjects) | result |
|---|---|---|---|
| C1 Linux artifact + GCC lane | Herschel, `port-linux` 3ae0759b (review APPROVED) | `build(recomp): Compact init table -- the GCC lane compiles the common TU` · `test(render): Render_Present bilinear pin per renderer precision class` · `fix(build): GCC 13 lane -- link group, nice() result, VR_reg init, parentheses` · `feat(dist): C1 Linux artifact -- linux-dist preset, asset scan, title tables` · `fix(dist): SDL2 licence text …` · `fix(build): GCC 13 lane -- the generated oracle TUs and three test findings` · `fix(vu0): ONE host sin/cos -- the libvu0 substitution no longer calls libm` · `test(verify): platform_ref/s3 re-based to the all-on source; Linux IDENTICAL` · `fix(dist): Linux artifact links libGL.so.1; --no-tests builds the binary only` | 996 steps 0/0/0, focused 171/171; the Release binary 138 → **53 MB** (one `std::function` instantiation for the 8,807 bodies instead of 17,600 closure types); identity leg IDENTICAL on audio/drawir/gsops/mem (review §3) |
| B3 CI matrix | Bernoulli, `port-windows` aecc4432 (review APPROVED) | `ci(guard): asset_guard.py -- game files NEVER in CI, three classes + pins` · `build(windows): clang-cl flag shim as the compiler launcher + its contract test` · `test(identity): Math oracles --digest mode -> the component identity lane for CI` · `ci: The B3 matrix (ci.yml), the tag release (release.yml), the asset guard in every job` · `docs(platforms): CI section …` · `docs(ci): The GCC lane's blocker is Herschel's compact init table` · integrator `ci: The C1 Linux artifact job joins the B3 matrix; dist/<dist> per platform` + `fix(ci): asset_guard --mode local scans the tracked set of a work tree` | 77 steps 0/0/0, focused 57/57 (`PlatformClangClShimContract`, `CiAssetGuardTree`, `CiAssetGuardSelftest`) |
| C2 launcher + A5 QoL | Bernoulli, `port-product` cefcfa59 (review APPROVED) | `feat(cdvd): Sha256 primitive; IsoFs reads raw 2352 (.bin) images` · `feat(launcher): DiscVerify + FirstRun extraction; --extract-disc; pins` · `feat(launcher): First-run launcher GUI + settings screen in the binary` · `feat(qol): START/CROSS skips a movie -- the owned MovieDecoder stop` · `feat(fixes): RetailFixes -- two RETAIL_BUGS toggles as owned constructor bodies` · `feat(qol): Cutscene fast-forward -- N fields per 60 Hz boundary while R2 held` · `fix(launcher): A failed/cancelled extraction never reads as ready` · `fix(clock): The census and the judge subtract fast-forward fields` · `feat(launcher): Save-state delete asks for confirmation` | 720 steps 0/0/0, focused 73/73, table 120 rows; `RootLauncherScreens 5.99` + `RootFirstRunExtract 0.51` Passed |
| A2 Display rows + A3 scaffold | Zeno, `port-graphics-r2` fea35cb7 (integrator review: APPROVED FOR INTEGRATION, A3 pixels OPEN) | `feat(video): Display-page rows -- scale, vsync, fullscreen, aspect, render rate (A2/A3)` · `test(root): RootHighFrameRate, RootClockContractDisplayRate, the scale-3 arm; CameraTrack unit` · `test(root): RootDisplayRowsLive -- scale 2x + vsync from the Display page live` · `docs(graphics): M6_M7_PHASE -- the charter 2 A2/A3 entry (A3 interpolation OPEN)` | 726 steps 0/0/0, focused 158/158, table 120 rows; `RootDisplayRowsLive 83.61`, `RootHighFrameRate 177.58`, `RootClockContractDisplayRate 47.09`, `RootShadowLevelEntryScale3 929.47` Passed |
| A2 texture filtering + 2d | Lagrange, `port-textures` a99a6875 (review APPROVED) | `feat(config): video.texture_filter and video.anisotropy (A2)` · `feat(render): Texture filtering + anisotropy over the owned textures (A2)` · `test(root): RootTextureFilterTitle / Beach pin the filtering in the presented frame` · `feat(effects): Cloth producer census + the particle-system blend census (2d)` · `docs(textures): TEXTURE_FILTERING.md; the 2d route search record` · `test(root): RootTextureFilterBeach bounds match RootWidescreenBeach (2400/5000)` · `docs(effects): 2d cloth = the owned vs_indexed_displace family, on the route` (`a99a6875` skipped: empty after the integrated pin) · integrator `fix(integration): A2 filter rows merged beside the A2/A3 Display rows`, `test(root): RootDisplayRowsLive expects the integrated page (items=9, texts=35)`, `test(root): RootMenuPerfOverlay's items box covers the eight-row Display page` | 578 steps 0/0/0, focused 153/153, table **122 rows**; `RootTextureFilterTitle 61.57` ALL PASS, `RootTextureFilterBeach 642.73` Passed |

**Acceptance (every line read; logs in `allon_c2w2/`):** component suite ALONE `-j8` **`100% tests passed, 0 tests failed out of 671`** (142 s;
25 skips = wave 1's skipset); game group `-j1` **`81% tests passed, 19 tests failed out of 102`** (4948 s) = the 18 GCLK frozen-anchor reds BY
NAME + `RootMenuPerfOverlay` (pin box; re-run alone `Passed 157.06 sec`); 0 other reds. `RootClockContract 47.13` `ALL PASS` (`frame_waits=0
dropped=0 underruns=0`, `DONE band … (out of band: none)`, one EE-own-time window ≤ 3).

**DT-14 on the integrated Release (one game process; `--harness-window`; isolated `paths.user_data`; `s3_play/leg/run.meta`, `level_play/leg/run.meta`):**

| replay | frame_waits | sync_wait_ms | ordered_waits | bridge waited | frames_dropped | audio dropped | underruns | in-loop game_hz | render_fps p50 / p95 | pace_late | unsupported | peak_mb (c2w1) | sky owned | iop_linked |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| s3 (3000 f) | **0** | **0** | 0 | 0 | 0 | **0** | **0** | **60 / 60** (44 w) | **60.0 / 60.0** | 15 | **0** | **518** (565) | `replaced=1779,model_calls=0` | 0 |
| level (7600 f, scale 1) | **0** | **0** | 0 | 0 | 0 | **0** | **0** | **58 / 60** (119 w; two own-time, out-of-band 0) | **beach 60.0 / 61.0**, min 58; EE cpu 12.87 ms/frame | 111 | **0** | **493** (531) | `replaced=6379,model_calls=0` | 0 |

**Frozen:** `allon_noexc/bin/nativeee_release.d67e84e0`, BUILD-ID `2026-10-12 18:46:39`, sha256
`b03df788dd0f41dc866d2b90076a6c0915a307138dae7f5cbbcdae1d99fd3d6d` (53,144,472 B; `.729a321e` kept, `.e1247812` deleted). **macOS artifact:**
`allon_noexc/dist/nativeee-macos-d67e84e0.zip` (28.1 MB), sha256 `7b4246bd99763951384646bf2d4a9e0407f013f7201c4be237079161ea153985`
(`./build.sh --dist-only`; `asset_scan: CLEAN -- 24 files, 85988121 bytes, 0 findings` — names, magic, 123 NTSC-ELF byte windows). New
census rows: `display_rows=`, `hfr_camera=`, `video_texture_filter=`, `retail_fixes=`, `fast_forward=`, `graphics_cloth_producer=`.
Charter 2 status: A1 A2 A4 B1 B3 C2 C3 C4 DONE; A3 scaffold (pixels OPEN); A5 DONE except the remap page; B2 groundwork (the Windows compile =
CI's first run); C1 macOS + Linux (Windows OPEN).

## Standing charter 2 wave 3 (2026-10-13 05:1x) — detail (`int_work/m_series/cutover/allon_c2w3/`)

Five stages (C1 licence attestation + GCC `SnapToYaw` + ledger L5/L7, B2 Windows cross-compile verification, A5 remap page + the first-run
drill, A3 display-rate re-view mechanism, M8 slice B owned meshes) integrated via `integration-20261013-c2w3` from `main 8d52ece4` under the
DT-14 + all-on invariant; `git merge --ff-only` → code HEAD **`91dfdbe6`** (37 commits; `git cherry HEAD <owner> 8d52ece4` 34 × `-`, 1 × `+` =
Bernoulli's README re-contextualised over Herschel's Licence section). **Nothing reverted**: every stage's own pin and the charter-1 Root set
are green on the integrated composition; the game group's one non-GCLK red is `RootHighFrameRate`'s pixel tier, which the A3 review approved
as OPEN (reported, not weakened). Keys: **123 rows** (`gen_settings_table --check` PASS); `hle.graphics_meshes=on` is the 31st `hle.*` key.

| stage | owner / worktree | commits on `main` (subjects) | result |
|---|---|---|---|
| C1 licences + GCC SnapToYaw + L5/L7 | Herschel, `port-linux` 5e39b7b1 (review loop 2 APPROVED) | `feat(dist): complete licence set -- committed texts, static LGPL FFmpeg on macOS, LintArtifactLicenses` · `fix(build): GNU lane -- -ffp-contract=off -frounding-math tree-wide` · `feat(m12): ledger L5/L7 -- the IPU model and the DMAC SIF face move to the oracle binary` · `fix(m12): libipu row binder out of InstallDispatch (size budget 410)` · `docs(build): GNU flags -- the SnapToYaw attribution verified on GCC 13` · `fix(dist): ffmpeg_static.sh caches on tag + configure args, not .a existence` · `fix(dist): FFmpeg attestation from what was linked, not the recipe` · `test(lint): LintArtifactLicenses --dist reads the linked FFmpeg's string` · `docs(readme): FFmpeg-BUILD-CONFIGURATION.txt quotes what was linked` | 1003 steps 0/0/0; focused 287 (2 GCLK reds, 0 other); `LintArtifactLicenses` + `Selftest`, `LintPlayBinaryNoIop`, `MathLibraryTrigOracle 28.63` Passed; the Linux artifact `ea460209…` (review §4) |
| B2 Windows local verification | Socrates, `port-windows` 2dda6a50 (review APPROVED) | `build(windows): Cross-compile smoke to COFF; the Windows arm compiles` · `build(windows): MtvuWorker on the host thread API; the smoke's --prefix sweep` · `test(platform): PlatformWindowsCrossSmoke + the Windows arm link check` · `build(windows): build.ps1 reviewed against build.sh; parsed and dry-run` · `docs(windows): CI has never run (no remote); the Windows developer path` | 581 steps 0/0/0; focused 91/91; `PlatformWindowsCrossSmoke` **`36 / 36 TUs compiled; PASS`** with the xwin splat (SKIP 77 without) |
| A5 remap + first-run drill | Bernoulli, `port-product` 18f39e79 (review APPROVED) | `feat(input): Gamepad-button remap page (bindings.ini [pad], press-to-bind)` · `docs(drill): First-run drill -- zip + ISO to the beach in 177.5 / 179.7 s` · `docs(readme): Users section -- launcher, Gatekeeper step, remap, drill` | 413 steps 0/0/0; focused 84 (1 GCLK red); `ControlsBindingsPressToBind 32/32`, `InputMapper 226/226`, `RootLauncherScreens 11.42`, `RootPlayHotkeyF5 27.59`, `RootFirstRunExtract` Passed |
| A3 re-view mechanism | Zeno, `port-graphics-r2` 9bc4daf9 (review APPROVED: mechanism in, `render_rate=60` default, PIXELS OPEN) | `feat(video): A3 display-rate re-view -- FrameReview re-renders the tick's frame at the interpolated camera` · `feat(video): re-view jobs ahead of the backlog, the backlog budget, the live-draw census; RootHighFrameRate judges the PIXELS` · `docs(graphics): A3 pixels …` · `test(root): RootHighFrameRate tolerates the shutdown's presents in camera.trace` · `feat(video): re-view budget reads the consumer's idleness; present-time census` · `feat(video): the re-view yields to the tick's frames while its cameras are stale` · `test(root): RootHighFrameRate pins the re-viewed pan's pixels; the busiest pan's budget is a REPORT` · `feat(video): async PBO window readback under the re-view; edge-timed phase; RAII replay_pending_` · `fix(video): the re-view's readback hand-over is not run at shutdown; no glFlush per transfer` · integrator `fix(video): FrameReview::Count out of the header; the replay reset is a unique_ptr deleter` (LintOneClass) | 573 + 57 steps 0/0/0; focused 183 (3 GCLK + `RootHighFrameRate` + the lint → fixed); `RootClockContractDisplayRate 45.27`, `RootDisplayRowsLive 81.93`, `RootMenuPerfOverlay 157.61`, `Render_FrameReview`, `CameraTrack` Passed; `RootHighFrameRate` 28 PASS / 2 FAIL (pixel CV 0.507 vs 0.50) |
| M8 slice B owned meshes | Lagrange, `port-textures` 72da4077 (review APPROVED) | `feat(config): hle.graphics_meshes -- the owned mesh bucket key (M8 slice B)` · `feat(graphics): FrameEnvironment::CaptureMesh -- the material buckets' environment` · `feat(graphics): The owned mesh bucket -- MeshProducer behind hle.graphics_meshes (M8 slice B)` · `test(root): RootMeshLevelEntry -- the owned mesh bucket in the presented beach frame` · `fix(textures): TextureAssets::Upload finishes the mip chain at the last level, not the count` · `feat(meshes): Receipts carry the shader digest; MESH.md / INTERFACES.md section 15 …` · `test(root): RootMeshLevelEntry's kick bound is the measured residue (0.95; measured 0.925)` · `test(textures): TextureState counts mip finishes per chain beyond the base` · `fix(meshes): The receipt's shader digest covers the identity bytes, not the animated block` | 737 steps 0/0/0; focused 137/137; table **123 rows**; `RootMeshLevelEntry 649.51` Passed; the level route's `gs_sink kicks` −6.1 % vs the model arm |
| integrator | — | `fix(build): build.sh runs under macOS bash 3.2; the dist suite links each archive once` | 57 steps 0/0/0; `./build.sh` end to end: `build OK -- warning: 0, ld: warning 0`, 592/592, lint PASS, asset scan CLEAN |

**Acceptance (every line read; logs in `allon_c2w3/`):** component suite ALONE `-j8` **`100% tests passed, 0 tests failed out of 677`** (135 s;
26 skips = wave 2's 25 + the env-gated cross-smoke); game group `-j1` **`82% tests passed, 19 tests failed out of 104`** (5763 s) = the 18 GCLK
frozen-anchor reds BY NAME + `RootHighFrameRate` (A3 pixel tier OPEN); 0 other reds. `RootClockContract 47.32` `ALL PASS` (`frame_waits=0
dropped=0 underruns=0`, `DONE band … (out of band: none)`); the 17-pin invariant set re-run on the FROZEN bytes 17/17.

**DT-14 on the frozen Release (one game process; `--harness-window`; isolated `paths.user_data`; `s3_play/leg/run.meta`, `level_play/leg/run.meta`):**

| replay | frame_waits | sync_wait_ms | ordered_waits | bridge waited | frames_dropped | audio dropped | underruns | in-loop game_hz | render_fps p50 / p95 | pace_late | unsupported | peak_mb (c2w2) | gs_sink kicks | meshes owned | iop_linked |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| s3 (3000 f) | **0** | **0** | 0 | 0 | 1 | **0** | **0** | **59 / 60** (43 w; one own-time, out-of-band 0) | **60.0 / 60.0** | 26 | **0** | **523** (518) | 21,602,742 | `placed_replaced=1963 flush_replaced=700` | 0 |
| level (7600 f, scale 1) | **0** | **0** | 0 | 0 | 0 | **0** | **0** | **60 / 60** (119 w; own-time 0) | **beach 60.0 / 61.0**, min 59; EE cpu 13.00 ms/frame | 29 | **0** | **523** (493) | **445,796,413** (model arm 474,809,595) | `placed_replaced=319162/1349974 flush_replaced=115625 draws=1028736 faults=0`; refusals `unowned_consumer=584841` | 0 |

**Frozen:** `allon_noexc/bin/nativeee_release.91dfdbe6`, BUILD-ID `2026-10-13 05:00:47`, sha256
`5f609ab82a43f637a61c9a05b1840b5b43efc7db2197db7406c0c223c7743bad` (53,728,872 B; `.d67e84e0` kept, `.729a321e` deleted). **macOS artifact
(licence-clean, `macos-dist` preset: static SDL2 2.30.9 + static LGPL FFmpeg n6.1.2, no `lib/`):** `allon_noexc/dist/nativeee-macos-91dfdbe6.zip`
(15.3 MB), sha256 `61cab83b5cda603d088dea029c7b34e2aeaed1e95aceaae1b014014167f5be51`; `ffmpeg: the linked libavcodec's configuration == the recipe
(23 arguments; --disable-gpl --disable-nonfree --disable-version3)`, `lint_artifact_licenses: PASS (0 failures)`, `asset_scan: CLEAN -- 14 files,
56365387 bytes, 0 findings`; deterministic lanes identical to the acceptance binary (1300 f). The wave-2 zip `nativeee-macos-d67e84e0.zip`
(Homebrew GPL FFmpeg) FAILS the lint and was deleted, never published. New census rows: `hfr_review=`, `graphics_meshes=`,
`graphics_mesh_assets=`, `graphics_mesh_materials=`, `graphics_refusals_meshes=`, `gif_parse=`.
Charter 2 status: A1 A2 A4 A5 B1 B3 C2 C3 C4 DONE; A3 mechanism (pixels OPEN); B2 cross-compiled (the run = CI's first push); C1 macOS + Linux
(Windows OPEN). Charter 1 leftovers: 3b L5 + L7 DONE (L6/L8/L9 OPEN); 1c reduced by M8 slice B (residue named); 2d OPEN.
