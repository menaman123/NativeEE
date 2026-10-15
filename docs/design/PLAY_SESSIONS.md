# Play sessions — recording the user's two NEW sessions and freezing them as `source=new` legs

Date: 2026-10-07. Status: procedure and exact commands. No session has been recorded yet;
no `source=new` leg exists in `games/<title>/verify/baseline/`.
Running these commands is the user's play, not an automated acceptance result
(PORT_PLAN §8: "No synthetic input run is counted as one of the user's sessions").

## What is recorded

`--record-input <file>` makes the host write the pad sample it actually served to the
game at every logical input tick into `<file>` (PS2_PAD_SCRIPT grammar: a `#` header
line, then held ranges `from-to:buttons[,axes]`). The same sample feeds the game and the
recorder; nothing is re-sampled. Held ranges are flushed at most 60 logical ticks apart
and at orderly exit, so an interrupted session keeps its completed prefix. The file is
created exclusively — an existing file is never overwritten; use a fresh name per attempt.
A reproduction restore (F8) starts a new recording segment (`input-NNNNNN.padlog` next to
the leg's `run.meta`) because the input clock rewinds; the first file is never appended to.

The recording is keyed by the **game frame** (2026-10-11): the frames-lane key of the boundary
the pad was served at (a cold boot 1, 2, 3, …; a restored run `restored_vsync_frame + 1, …`), so a
padlog line `F1-F2:CROSS` and a `frames.fhash` key name the same boundary. The header names the
clock — `# padlog v1 start_frame=1 clock=frame title=…` — and `run.meta` records
`pad_record_clock=frame` beside `pad_record_path`, `pad_record_start`, `pad_record_last` and
`pad_record_lines`. The title reads the pad once per boundary, so this is the title's own input
clock and it is identical in every profile.

It is NOT keyed by the logical input tick (the emulated time domain's field count, the key of
every recording before 2026-10-11 and of a header without `clock=`). In play that count is
WALL-paced: a late boundary consumes the fields that elapsed and a pace park the fields it waited
(s3: tick 9671 for 3068 frames; s4: 8823 / 3993; s5: 7307 / 4968), and the ticks that pass inside
a park are never served — so a tick-keyed recording of a play session named ticks the
deterministic replay reached at other game frames and simply LOST the ranges whose ticks fell in
a park. That is why `play_sessions/s4/session.padlog` replayed onto the load-file menu (its
menu presses landed on the wrong screens; the card it saw was the same as the session's — four
byte-identical `Bank*.bin` = four EMPTY slots) while s3's 26 long holds happened to survive. s5
(428 lines, tick 7307 for 4968 frames) has the same defect: its deterministic replay is STABLE —
two reps identical on frames, audio and mem (`int_work/m_series/input_saves/recorder_clock/s5_det`)
— but stays on the title/menu route (`graphics_shadow_scene published=0`, no level load) where the
session reached the beach (`published=1219`); the play-profile replays of it are the same title
route (`cutover/allon_clock/s5_play`: `published=0`). As a WORKLOAD s5 is therefore a title-route
workload under every profile; the level route of record stays s3 / `root_shadow_level_entry.padlog`
until a session is recorded with the frame clock.
Old recordings (no `clock=`) are still replayed on the tick, exactly as before, so the frozen
fixtures and the s3/s5 workloads are unchanged; they cannot be converted (the session's
frame↔tick map was not recorded). The pin on the real binary: `RootPlayRecordReplay`
(`tests/app/root_play_record_replay.py`) — a play-profile window session replays a frame-clock
script for 600 frames while recording; the recording's per-frame state equals the script's
exactly, and a deterministic replay of the recording is IDENTICAL on frames+audio to a direct
deterministic run of the script (red on dc1a8a3a: three of seven ranges lost).

Every flag of the commands below (`--profile`, `--harness-window`, `--record-input`, `--frames`,
`--game-files`, `--harness-out`, `--set`) is listed by `nativeee --help`; `Coretest_config` pins that
the usage text names every flag the parser accepts, so "`--help` shows the flags" (the cutover's
publication check) cannot silently regress.

The command's recorder path is pinned end to end on the actual binary by the parent-run ctest
`RootRecordInputRoundTrip` (`tests/app/root_record_input_round_trip.py`): a deterministic headless
cold-boot leg replays a small `--inputs` script into an explicit `--record-input <file>`; run.meta
names that file, the recording begins with the header line, replays nothing the script did not hold
and carries every scripted range, and a second leg naming the same file exits 2 with
`an existing file is never overwritten` leaving the first recording's bytes intact. Its judge is
pinned by `RootRecordInputRoundTripSelftest` (no game process).

## Before the first session

Build Release in the integration checkout (the parent's build; the user plays the reported
milestone build):

```sh
cd <repo>
cmake --preset macos-release && cmake --build build-release -j2 --target nativeee
./build-release/app/host/nativeee --version      # record this build id in the session notes
mkdir -p play_sessions/saves
```

`play_sessions/` is ignored by git. Both sessions share one persistent game-save
directory (`play_sessions/saves`) so session 2 can load what session 1 saved. **Before each
session, snapshot that directory** — the freeze replays the session from exactly those saves:

```sh
mkdir -p play_sessions/session-1 && cp -R play_sessions/saves play_sessions/session-1/mc_before
```

## Session 1 — new game, menus, first save, ordinary traversal

Cold boot; start a new game; play through the first hub/level; save at least once from
the in-game menu. Press **F1** for the menu (volume, fullscreen, keyboard/mouse,
savestate slot browser, sound and triage panels), **F9** pause, **F10** one tick
while paused, **F5** savestate (see "Savestates" below), **F8** restore of the selected or
newest slot. Close the window to end; the cap is 36,000 presented frames (about ten minutes
at 60 fps).

### Input devices (automatic)

The input device is automatic: a connected gamepad is used when present, keyboard and mouse are
ALWAYS live as a pad, and the ACTIVE source is whichever produced the last event (typing or
unplugging a pad mid-game takes over instantly; hot-plug both ways). Default keyboard layout:

| PS2 control | Key | PS2 control | Key |
|---|---|---|---|
| left stick | W / A / S / D | right stick (camera) | mouse |
| cross | Space | circle | right mouse button |
| square | LShift | triangle | E |
| L1 / L2 | 1 / 2 | R1 / R2 | 3 / 4 |
| L3 / R3 | 5 / 6 | start | Y |
| select | Tab | dpad up / down / left / right | arrow keys |

run.meta `input_sources=kbm=on|off,gamepad=connected|absent,active=kbm|gamepad|none,switches=N,
hotplug_added=N,hotplug_removed=N` is the census of that contract (pins RootKbmPadOnly, RootPadHotplug).

### Savestates (F5 / F8 / `--load-state`), 2026-10-09

**F5** captures the running composition at the next admissible game boundary into
`<save root>/savestates/slot<N>.ps2st` plus a `.meta` sidecar (`frame=`, `wall_ms=`,
`build_id=`, `composition=` every `hle.*` key and the profile, `sections=` the tags the image
carries). The save root is `paths.save_dir`, else the `--harness-out` directory, else
`paths.log_dir`; the slot number is `--set save.slot=N` (default 0). Every F5/F8 result is
LOUD: the key press is acknowledged on stderr the moment it is queued
(`nativeee: savestate F5 save Queued: ...`), and the terminal result reaches stderr, the SAVE
log channel and the on-screen session notice/menu — written (`... Completed: Savestate written:
<slot> (frame F, sidecar <slot>.meta)`), or REFUSED naming the owner (`... Refused: No admissible
boundary in 120 consecutive boundaries; the last owner to refuse: <owner>: <reason>`). Never a
silent no-op (the s3 session's F5 wrote nothing and said nothing: fixed).

F5 at the title and in play WORKS under the all-on composition (2026-10-10, the Graphics
capture-time settle — `games/<t>/hle/graphics/evidence/INTERFACES.md` §13): at a SyncV boundary the
frame's owned draw groups are recorded behind their predecessors and not yet Linked (host closures
no image can carry), so instead of refusing `graphics: Graphics owned pending=N` the Graphics owner
SETTLES — it proves nothing is in flight (consumer drained, every sealed submission delivered, the
pending set equals the bridge's open recording) and publishes that set as the GXPD digest. The
restored session draws its FIRST field without those groups (the title's own `ResetBuckets` shape),
attributed in run.meta (`graphics_restore_unlinked=…,items=I` and exactly I `ordering` refusals
detail 5 at the Link producer) and one log line — visible for 1/60 s, never silent. Still refused
by name: a capture during a movie (`fmv_active`), an owned fault, an immediate owned slot in
flight, an undelivered sealed submission. Pins on the real binary: `RootPlaySaveRestore` (slot at
boundary 60, `--load-state` 120 frames, 119/119 restored frame hashes equal to the uninterrupted
run's) and `RootPlaySaveAtTitle` (slot at the title boundary 1300 — `savestate=written`,
`graphics_capture_settle=…,unlinked=U,…,items=I` — restored `restored_vsync_frame=1300`, 120
frames, `park=none`).

**F8** restores the browser's selection or, with nothing selected, the newest `slot<N>.ps2st` —
LIVE, in the running session (track 3a, 2026-10-11; `docs/design/F8_LIVE_RESTORE.md`;
`save.restore_mode=live`, the default). The slot is preflighted on the game owner with the session
still open — against the live composition (the slot's `.meta` `composition=` must equal the running
`hle.*` keys + profile — a slot of another profile or toggle set is refused by name: `composition
differs: profile(saved=deterministic,live=default)`; a differing `build_id=` is reported as `F8
restore Warning: build_id differs ...` and restored anyway), against the live registry (every
required section present — a missing one is refused as `required section missing: <TAG>
(<owner>)`), the clock/providers, and the live gate (`LiveRestoreGate`): the MTVU worker must
quiesce (the F5 predicate), no kernel event / native-service RPC / modelled disc delivery may be
pending for a later boundary — each is refused BY NAME with its count (`live restore refused: mtvu
worker holds state: ...` / `ee events pending` / `rpc pending=N` / `disc deliveries pending=N`;
run.meta `live_restore_refusals=`), and the F5 owners' predicates must admit. Then, in THIS process:
the boundary thread unwinds at the admitted SyncV boundary; the supervisor joins the workers and the
capture writer, drains the GS queue and forgets the previous session's last picture (the consumer's
grab, the present policy's held image: the restored session shows nothing until its own first
image), stops the audio device, begins the output segment (`output-NNNNNN/`, its run.meta carrying
`restore_route=live restored_vsync_frame=F restore_mhash=<RAM hash>`), applies the image — the
Graphics owner cancels the open recording's owned batches (`GraphicsOutput::ResetForRestore`;
`graphics_live_restore=resets=,cancelled=`) and attributes the first restored field exactly as a
process restore does (`graphics_restore_unlinked`), the MTVU worker's unit mirrors the restored VU1
memory/registers (`mtvu=...,mirrors=`) — re-arms the presenter, the audio device and the pacer, and
relaunches the restored continuations. The window, the GL context, the device and the user's
fullscreen/scale are kept; the stderr lines are `nativeee: savestate F8 restore Queued`, then
`... Completed: Reproduction restored; approved continuations relaunched.`; run.meta `f8=live`. A
refused F8 leaves the live session exactly as it was.

The RELAUNCH route is kept as the fallback: `--set save.restore_mode=relaunch`, or a composition
that still retains running IOP state (`service_startup=stream=retained,...` — run.meta
`f8=relaunch,fallback=startup_retained`, stderr `nativeee: F8 restore route: relaunch (live
refused: <the retained modules and their consumers>)`). It preflights the same way, then the session
closes and the running executable image (`_NSGetExecutablePath`) re-executes with the same
arguments plus `--load-state <slot>` (`--harness-out` nested under `<out>/restores/<stamp>`, an
explicit `--record-input` renamed `<stem>-r<stamp>.padlog`, `--set paths.save_dir=<root>` pinned);
the stderr line is `nativeee: savestate F8 restore Relaunch: <argv>`.

The harness presses both keys without a keyboard: `--set harness.save_at_frame=N` (the F5 path at
boundary N) and `--set harness.restore_at_frame=N` (the F8 path, play sessions only); the pins
`RootPlaySaveRestore` (deterministic slot round trip), `RootPlaySaveAtTitle` (the slot at a
title boundary through the settle), `RootPlayLiveRestore` (a play-mode F5 slot at 300, F8 at 600
restored LIVE in the same window: one process, the restored segment's provenance and digests, the
clock contract's `frame_waits=0` / `dropped=0,underruns=0` across the restore, `restore_mhash` equal
to a `--load-state` of the same slot) and `RootPlayRelaunchRestore` (`save.restore_mode=relaunch`:
the relaunch, 400 restored frames; then a deterministic-profile slot pressed in a play session,
refused in the LIVE session with the window open) run them on the real binary. A slot
is bound to its profile/composition: the F8 preflight refuses another composition's slot by name
before anything closes (`F8 restore Refused: <slot>: composition differs: profile(saved=
deterministic,live=default) ...`), and a `--load-state` of such a slot refuses at startup
(`Saved clock/provider selection differs ... providers(saved=...profile=deterministic,
live=...profile=default)`).

**`--load-state <slot>`** on the same binary/composition jumps to the saved point (the harness
leg: `--profile deterministic --headless --frames N --load-state <slot>`; a play session: the
usual command plus `--load-state <slot>`). The restored run's `frames.fhash` is keyed by the
restored game frame (`restored_vsync_frame=F` → keys F+1…).

```sh
./build-release/app/host/nativeee --profile default --harness-window \
  --record-input "$PWD/play_sessions/session-1/session-1.padlog" --frames 36000 \
  --game-files $PS2R_OLD_ROOT/game_files \
  --harness-out "$PWD/play_sessions/session-1" \
  --set "paths.log_dir=$PWD/play_sessions/session-1/logs" \
  --set "paths.mc_dir=$PWD/play_sessions/saves"
```

## Session 2 — a different area, combat/effects, pause, save and reload

Snapshot the saves session 1 produced, then play: load the session-1 save, go to a
different area, fight, use pause/menu, save again, reload that save in-game.

```sh
mkdir -p play_sessions/session-2 && cp -R play_sessions/saves play_sessions/session-2/mc_before
./build-release/app/host/nativeee --profile default --harness-window \
  --record-input "$PWD/play_sessions/session-2/session-2.padlog" --frames 36000 \
  --game-files $PS2R_OLD_ROOT/game_files \
  --harness-out "$PWD/play_sessions/session-2" \
  --set "paths.log_dir=$PWD/play_sessions/session-2/logs" \
  --set "paths.mc_dir=$PWD/play_sessions/saves"
```

Each `play_sessions/session-N/` then holds `session-N.padlog`, `run.meta`, `frames.fhash`,
`audio.ahash`, `mem.mhash`, `logs/` and `mc_before/`. Keep the whole directory with the
build id and the settings used; do not edit the padlog.

## First-level map — beach → Mecha-Bandicoot (s9 / s11, 2026-10-13)

What the recorded sessions reach, in order, with the states and padlogs that resume each
stop (every path absolute; the repo's `play_sessions/` is git-ignored scratch):

| Stop | Game frame | Reached by | Resume | Census reference |
| --- | --- | --- | --- | --- |
| Title → menu → new game | 1–1890 | `play_sessions/s9/scripts/v5.padlog` (CROSS at 399/412, START 1350, CROSS 1420, UP 1600/1630, CROSS 1670/1850; autosave-disabled dialog 1760–1860) | cold boot, deterministic or play profile | — |
| Jungle Bungle, the beach | ~1890–4161 | the same padlog; the user's s8/s9 sessions (`play_sessions/s9/session.padlog`, `clock=frame`) | `play_sessions/s9/states/beach_1900.ps2st` (deterministic profile; a windowed restore refuses `bypass`) | `play_sessions/s9/run.meta` (4161 f: `graphics_mesh_materials certified=124005 unowned_consumer=131678`, `graphics_shadow limit_refusals=0`) |
| Jungle Bungle, Mecha-Bandicoot arena (first boss) | 37165 | the user's play (binary baf15a38), saved with F5 INSIDE the pause menu | `play_sessions/s11/states/mecha_slot0.ps2st` (copy of `~/Library/Application Support/NativeEE/logs/savestates/slot0.ps2st`); windowed only, with the state's own `--set video.anisotropy=16 --set video.aspect=21:9 --set video.texture_filter=point --set video.vsync=on --profile default`; TRIANGLE (Back) resumes the fight (START does not) | `play_sessions/s11/lag/pin1/owned_run.meta` (1200 f of the s11 fight padlog `tests/app/root_mecha_bandicoot_meshes.padlog`) |

The arena's loaded material table (576 live `MaterialResource`s at 0x3b0208 in the state's RAM0)
against the certificate: 312 certified / 264 refused — the SAME families as the beach's 540
(291/249): `MaterialEnvelope` 175 (beach 133), `NoMeshConsumer` 110 (105), `TextureIdentityVaries`
67 (27), `SharedUnownedModel` 66 (93), `Sprite` 50 (50), `Skin` 42 (38), `BlendSkin` 8 (4), `Sky` 8,
`Font` 4, `ShadowDefault` 1, `Particle` 1, `FamilyVaries` 1 (new). Refused materials by bucket word:
2: 111 (beach 150), 3: 78 (beach 22), 24: 50, 0: 8, 22: 4, 26: 4, 12: 3, 19: 3 — no bucket or
material type the certificate cannot classify; what the boss adds over the beach is bucket-3
(global opaque) materials of the envelope / texture-identity families. 233 of the arena's material
ids are not loaded on the beach.

A restored image carries no `MaterialResourceRead` receipt for the materials its chunks loaded
before the capture (the record bytes are consumed from the chunk's streamed section, TR
`graphicstables.cpp` `GraphicsResourceReader::Read`): states written before the `MSHR` section
(the user's slot0) run every mesh frame WHOLE on the model path (`graphics_mesh_materials
frames_whole_model`, `unreceipted_live=576`) until those chunks unload; states written with it
restore the receipts (`restored=`) and own again. The frame admission is decided once per frame
(`MeshProducer::FrameAdmits`), so an owned material never shares a frame's material list with the
writer (`mixed_frames=0`; `RootMechaBandicootMeshes`).

## Freezing a session as a `source=new` leg

The interactive session's lanes are **not** the frozen lanes: play runs on the default
profile (realtime pacing, audio device, host RTC). A `source=new` leg is the recording
**replayed headless on the deterministic profile from a cold boot**, three reps, frozen only
when the reps are identical on frames and audio (memory advisory, DT-11). The tool:

```sh
python3 verify/freeze_session_leg.py \
  --binary "$PWD/build-release/app/host/nativeee" \
  --padlog "$PWD/play_sessions/session-1/session-1.padlog" \
  --mc-before "$PWD/play_sessions/session-1/mc_before" \
  --leg play_session_1 --reps 3 --timeout 4000 --stall-s 600 \
  --game-files $PS2R_OLD_ROOT/game_files \
  --notes "user session 1: new game, menus, first save, traversal (build <id>)"

python3 verify/freeze_session_leg.py \
  --binary "$PWD/build-release/app/host/nativeee" \
  --padlog "$PWD/play_sessions/session-2/session-2.padlog" \
  --mc-before "$PWD/play_sessions/session-2/mc_before" \
  --leg play_session_2 --reps 3 --timeout 4000 --stall-s 600 \
  --game-files $PS2R_OLD_ROOT/game_files \
  --notes "user session 2: other area, combat, pause, save and reload (build <id>)"
```

On acceptance it writes, and only then:

| path | content |
|---|---|
| `games/<t>/verify/baseline/<leg>/{frames.fhash,audio.ahash,mem.mhash,run.meta}` | rep 1's lanes |
| `games/<t>/verify/baseline/<leg>/LEG.tsv` | `frames`, bounds, `anchor coldboot`, `pad_script verify/pads/<leg>.padlog`, `mc_dir mc_before` |
| `games/<t>/verify/baseline/<leg>/mc_before/` | the saves the session started from |
| `games/<t>/verify/baseline/<leg>/FREEZE.md` | replay command, binary sha256, build id, per-rep lane digests, padlog digest/header/last tick |
| `games/<t>/verify/pads/<leg>.padlog` | byte copy of the recording |
| `games/<t>/verify/anchors.tsv` | one row: kind `session`, `pad_script <leg>.padlog`, `source new` |

Refusals write nothing (exit 3): an existing leg, a recording with no range line, a leg
length below the recording's last tick, a failed or differing rep. The leg length defaults
to the session `run.meta` `frames=`; pass `--frames` to override (it must cover the last
recorded tick). `--dry-run` prints the rep command.

`battery.py` then runs the leg like the thirteen: `--inputs <pad_script>` and a fresh copy
of `mc_before` per rep as `--set paths.mc_dir=<rep>.mc` (the frozen directory and the other
reps never see a save written by the replay). `anchors_table.py` (ctest `VerifyAnchorsTable`)
checks the row against the LEG.tsv. The leg joins the standing battery only after the user
has actually played it (PORT_PLAN §8); the parent runs the gates.

## Known limits (open, not hidden)

- Replay reproduces the session only if the title is deterministic given the recorded
  input frames and the frozen saves. The play profile differs from the deterministic one
  (`game.rtc_fixed`, pacing, audio device); a session whose logic consumed the host clock
  can diverge on replay. In particular the title's per-frame TIME step is the T0 delta
  (DT-14 rule 3): a late play boundary advanced the title's clock by the elapsed fields,
  the deterministic replay advances it by exactly one — so a time-driven screen (a logo
  timer, the attract countdown) can be reached at a different frame in the replay than in
  the session even though every press lands on its recorded frame. The freeze refuses
  unstable reps; a stable replay that differs from what the user saw is still a valid NEW
  replay oracle and FREEZE.md says so. Recordings from before 2026-10-11 (no `clock=` in the
  header) are tick-keyed and keep the s4 hazard above.
- A session that used F8 (reproduction restore) produces more than one recording segment;
  only the first segment is a cold-boot replay. Record the two sessions without F8, or
  freeze the prefix.
- F8 LIVE (in-process) restore is refused while startup is still retained
  (`harness.live_restore` default off; `title::ServiceStartup` through the root's
  `StartupOwnership`); F8 is composed as the relaunch above instead (the window closes and
  the same binary reopens at the slot). The relaunch starts a new recording segment and a
  new harness-out directory; only the first segment is a cold-boot replay.
- F5 under all-on captures at the title / in play through the Graphics capture-time settle
  (INTERFACES §13; `RootPlaySaveAtTitle` at boundary 1300). The restored session's first field
  omits the captured frame's owned groups (attributed `graphics_restore_unlinked`, detail-5
  ordering refusals) — one field, then identical. A capture during a movie is refused by name
  (`fmv_active`); press F5 again after it.
