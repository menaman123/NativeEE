# FIRST_RUN_DRILL — a fresh machine, the zip and a disc image, to the beach (Charter 2 DONE criterion)

Owner: Bernoulli (product). Script: `tools/first_run_drill.py`. Pins: `RootLauncherScreens`
(the hand-off legs), `RootFirstRunExtract`, `DiscVerifyVectors`, `ControlsBindingsPressToBind`.

## The criterion

CUTOVER.md, Standing Charter 2 DONE: *"a fresh machine with only the zip + an ISO reaches the
beach in under 5 minutes with no other installs."* The drill is the measurement of that sentence on
the real release artifact with the user's real disc image, with the clock running from before the
unzip to the game having presented the beach of the first level.

## What the drill does (`tools/first_run_drill.py`)

Inputs: the release zip (the ONLY binary input; nothing of the repository is run), the user's disc
image (read only — the script fails if its mtime or size changes), the scripted player
(`tests/app/fixtures/play_beach_session.padlog`: the s3 play session, title → new game → the
intro movies → the first level; frame 7000 is Crash idle on the beach — the pin frame of
`RootWidescreenBeach`), and an empty `--out` directory.

1. **A clean machine.** `--out/home` becomes `$HOME` for every process. On macOS SDL's
   preference path (`SDL_GetPrefPath`, the user data root) ignores `$HOME`, so the root is pinned
   under `--out/Library/Application Support/NativeEE` with `--set paths.user_data=` — the
   "Application Support override". Nothing of the operator's real `~/Library` is touched.
2. **Unzip** the artifact (python `zipfile`, as Archive Utility would) and, with `--quarantine`,
   mark every file with a browser-style `com.apple.quarantine` attribute — the state a downloaded
   zip is in — then record whether the binary starts from the shell and what
   `spctl --assess --type execute` says.
3. **Choose the ISO → verify → extract → Play**, by what the binary under test supports:
   * `--route drop` (builds from `bc9900da` on): ONE launch —
     `nativeee --launcher --launcher-disc <iso> --launcher-then-play --harness-window --inputs
     <padlog> --frames 7000 --harness-out <dir>`. `--launcher-disc` is the *scripted drop*: the
     launcher opens on the first-run screen and takes the path exactly as a drop on the window
     (`StartExtraction`), shows the stage list and progress bar, and `--launcher-then-play`
     presses Play the moment the Ready screen is up (`launcher: Play (auto)`). A refused image
     leaves with exit 2 (`launcher: scripted drop refused`).
   * `--route cli` (the frozen `d67e84e0` artifact, which predates those flags): the windowless
     `--extract-disc <iso>` is the choose/verify/extract step, then `--launcher
     --launcher-screenshot ready.ppm --launcher-then-play ...` opens the Ready screen (its
     screenshot is kept) and presses Play (`launcher: Play (harness)`).
4. **The beach.** The game runs with a window (`--harness-window`), wall-paced at the title's
   60 Hz, driven by the padlog; the harness writes `run.meta` and the final presented frame
   (`w_final.ppm`, converted to `beach_final_frame.png`). The judge wants exit 0, the Play line,
   `finalized=1 frames=7000 video=gl`, the ELF at `game_files/example_title.rep/SLUS_123.45`,
   the extraction stamp `.nativeee_build_id`, the user's image unchanged, and the total under
   300 s. It prints each stage's wall time and `DRILL: DONE <seconds>`.

## Measured — 2026-10-13 (Apple silicon laptop, APFS SSD, 2,108,489,728-byte .iso)

| run | artifact | route | unzip | verify + extract | launch → Play → beach | **unzip → beach** |
|---|---|---|---|---|---|---|
| 1 | `nativeee-macos-d67e84e0.zip` (the frozen all-on artifact), quarantined | cli | 0.1 s | 2.3 s (`--extract-disc`, disc copied) | 174.3 s | **177.5 s** |
| 2 | `nativeee-macos-bc9900da.zip` (this stage's binary, `build.sh --dist-only`), quarantined | drop (one launch) | 0.1 s | inside the launch | 178.7 s | **179.7 s** |

Both: `run.meta finalized=1 frames=7000 video=gl`, `gs_queue frame_waits=0`, `audio_ring dropped=0
underruns=0`, `pace_timeouts=0`; `beach_final_frame.png` shows Crash on the beach of the first level
(evidence under `int_work/m_series/input_saves/drill/run{1,2}/`). The ~175 s of the game leg is the
player's own time: the s3 session watches the intro movies (the FMV pacer parks the boundary for
~55 s — `fmv parks=1654 park_ticks=3306`), loads the level and walks to the beach; the launcher's
share (window, verify, 2 GB copy, Play) is under 3 s.

Run 1's judge first reported FAILED on its own evidence rule (it expected the headless frames lane's
`p7000.ppm`; a window run writes `w_final.ppm`) — the rule was corrected, the run itself was good.

## Friction found

| # | friction | ours? | disposition |
|---|---|---|---|
| 1 | **Gatekeeper.** The artifact is signed ad hoc (`codesign: Signature=adhoc`), not notarised. A quarantined copy starts from the shell (`--version` exit 0, the dylibs load) but `spctl --assess --type execute` says *rejected*: a double-click in Finder is refused. | no (needs an Apple Developer ID + notarisation) | README "macOS, first start only": right-click → Open, or `xattr -dr com.apple.quarantine nativeee-macos/`; the drill reproduces and records it with `--quarantine`. |
| 2 | **No scripted way to hand the launcher the image.** The first-run screen took a drop or typed path only; a drill (or a launcher shortcut, a helper script, a frontend) had to fall back to `--extract-disc` + a second start. | yes | `--launcher-disc <image>` (the scripted drop) and `--launcher-then-play` without a screenshot — landed in `bc9900da`; `RootLauncherScreens` pins the refused drop and the auto hand-off. |
| 3 | **`$HOME` does not move the user data root on macOS** (SDL_GetPrefPath → the account's real Application Support). | by design (C4: the platform's own rule, no environment read of ours) | the drill pins the root with `--set paths.user_data=`; left as is — an environment override would be a second rule for the same path. |
| 4 | The README still described the launcher as "in progress" with a `--iso --game-files` command line. | yes | README Users 1–3 rewritten (launcher, settings screen, Gatekeeper step, controller remap, the drill's numbers). |
| 5 | The 2 GB disc copy is the only sizeable write of the first run (2.3 s here on APFS; a spinning disk would take a minute or two). | design choice | the first-run screen's "Copy the disc image" tick is on by default; unticking references the image in place (`paths.iso`), no copy. |

## Re-running it

```sh
python3 tools/first_run_drill.py --zip dist/nativeee-macos-<id>.zip \
    --iso "/path/to/the title.iso" --padlog tests/app/fixtures/play_beach_session.padlog \
    --out /tmp/drill --route drop --quarantine
```

`--route cli` for an artifact older than `bc9900da`. Windows and Linux: the script's unzip,
quarantine and `spctl` steps are macOS-shaped; the launch command lines are identical
(`nativeee.exe`), so the per-platform drill is the same two commands with the user data root pinned
to a fresh directory.
