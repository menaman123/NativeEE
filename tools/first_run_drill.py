#!/usr/bin/env python3
"""tools/first_run_drill.py -- the FIRST-RUN DRILL (CHARTER 2 DONE criterion: "a fresh machine with
only the zip + an ISO reaches the beach in under 5 minutes with no other installs").

Nothing of the repository is used except this script, the s3 beach padlog (the scripted player) and,
optionally, the frozen quarantine step. The clock starts BEFORE the unzip and stops when the game
process has finalized the padlog's last frame (p<frames>, the beach). Everything lands under
--out (a clean temp HOME is created there; on macOS SDL_GetPrefPath ignores $HOME, so the user data
root is pinned under --out with `--set paths.user_data=`: the "Application Support override").

Two routes, by what the binary under test supports:
  --route cli        (frozen artifact d67e84e0 and earlier) the ISO is "chosen" through the
                     windowless `--extract-disc <iso>` (verify + extract), then the launcher opens on
                     the Ready screen, screenshots it and presses Play (`--launcher-then-play`).
  --route drop       (bc9900da+) ONE launch: `--launcher --launcher-disc <iso> --launcher-then-play`
                     -- the scripted drop on the window: verify, extract (progress), Play.
Then the s3 padlog drives the game to the beach with a window (`--harness-window --inputs ...
--frames N`); only the last presented frame is kept (p<N>.ppm -> .png beside it) as the evidence,
with run.meta (with a window the harness keeps the final presented frame as w_final.ppm). Prints every stage's wall time and ends with `DRILL: DONE <seconds>` or
`DRILL: FAILED <why>`; exit 0 / 1.

  --quarantine       put a browser-style com.apple.quarantine xattr on the unzipped files first (what
                     a downloaded zip carries) and record whether the binary still starts from the
                     shell and what `spctl --assess` says (the Gatekeeper step of the README).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import zipfile


class FrameKeeper(threading.Thread):
    """Deletes every dumped frame except the kept keys while the game runs."""

    def __init__(self, directory, keep):
        super().__init__(daemon=True)
        self.directory, self.keep, self.stop = directory, set(keep), threading.Event()

    def sweep(self):
        try:
            names = os.listdir(self.directory)
        except OSError:
            return
        for name in names:
            m = re.fullmatch(r"[fp](\d+)(_drawn_0x[0-9a-fA-F]+)?\.ppm", name)
            if m is None:
                continue
            if m.group(2) or name.startswith("f") or int(m.group(1)) not in self.keep:
                try:
                    os.remove(os.path.join(self.directory, name))
                except OSError:
                    pass

    def run(self):
        while not self.stop.is_set():
            self.sweep()
            time.sleep(0.5)
        self.sweep()


def read_meta(path):
    meta = {}
    if os.path.isfile(path):
        for line in open(path, errors="replace"):
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                meta[k] = v
    return meta


def ppm_to_png(ppm, png):
    try:
        from PIL import Image  # optional: the evidence image is nicer as PNG
        Image.open(ppm).save(png)
        return True
    except Exception:
        return subprocess.run(["sips", "-s", "format", "png", ppm, "--out", png], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", required=True, help="the release artifact zip (the ONLY binary input)")
    ap.add_argument("--iso", required=True, help="the user's disc image (read only)")
    ap.add_argument("--padlog", required=True, help="the scripted player: tests/app/fixtures/play_beach_session.padlog")
    ap.add_argument("--out", required=True, help="a fresh directory: temp HOME, unzip target, user data root, evidence")
    ap.add_argument("--route", choices=("cli", "drop"), default="cli")
    ap.add_argument("--frames", type=int, default=7000, help="the padlog frame that is the beach (p7000 in the s3 session)")
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--quarantine", action="store_true")
    args = ap.parse_args()

    if os.path.exists(args.out) and os.listdir(args.out):
        print("DRILL: FAILED --out must be empty or absent: %s" % args.out)
        return 1
    home = os.path.join(args.out, "home")
    os.makedirs(home)
    env = dict(os.environ)
    env["HOME"] = home
    env.pop("PS2R_PROFILE", None)
    iso_mtime, iso_size = os.path.getmtime(args.iso), os.path.getsize(args.iso)
    log = open(os.path.join(args.out, "drill.log"), "w")

    def say(line):
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    def run(cmd, cwd, timeout, name):
        say("$ " + " ".join("'%s'" % c if " " in c else c for c in cmd))
        t = time.time()
        with open(os.path.join(args.out, name + ".log"), "w") as f:
            p = subprocess.run(cmd, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=timeout)
        say("  -> exit %d in %.1f s" % (p.returncode, time.time() - t))
        return p.returncode, open(os.path.join(args.out, name + ".log"), errors="replace").read()

    stages = []
    t0 = time.time()

    # 1. unzip
    t = time.time()
    unzipped = os.path.join(args.out, "unzipped")
    with zipfile.ZipFile(args.zip) as z:
        z.extractall(unzipped)
    folder = os.path.join(unzipped, "macos")
    binary = os.path.join(folder, "nativeee")
    os.chmod(binary, 0o755)
    for root, _dirs, files in os.walk(folder):
        for f in files:
            if f.endswith(".dylib"):
                os.chmod(os.path.join(root, f), 0o755)
    stages.append(("unzip", time.time() - t))
    say("unzipped %s -> %s (%.1f s); BUILD-ID %s" % (os.path.basename(args.zip), folder, stages[-1][1],
                                                     open(os.path.join(folder, "BUILD-ID")).read().strip()))
    if args.quarantine:
        tag = "0083;%08x;Safari;00000000-0000-0000-0000-000000000000" % int(time.time())
        for root, _dirs, files in os.walk(folder):
            for f in files:
                subprocess.run(["xattr", "-w", "com.apple.quarantine", tag, os.path.join(root, f)])
        p = subprocess.run([binary, "--version"], cwd=folder, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        a = subprocess.run(["spctl", "--assess", "--type", "execute", binary], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        say("quarantine: shell start exit %d (%s); spctl --assess: %s" %
            (p.returncode, p.stdout.decode(errors="replace").strip(), a.stdout.decode(errors="replace").strip()))
        if p.returncode != 0:
            say("quarantine: removing com.apple.quarantine recursively (the README step)")
            subprocess.run(["xattr", "-dr", "com.apple.quarantine", folder])

    user_data = os.path.join(args.out, "Library", "Application Support", "NativeEE")
    os.makedirs(user_data)
    pin = ["--set", "paths.user_data=" + user_data]
    game_out = os.path.join(args.out, "game_out")
    frames_dir = os.path.join(args.out, "frames")
    os.makedirs(frames_dir)
    game_flags = ["--launcher-then-play", "--harness-window", "--inputs", os.path.abspath(args.padlog),
                  "--frames", str(args.frames), "--harness-out", game_out, "--dump-frames", frames_dir]

    # 2. choose the ISO -> verify -> extract (-> Play -> beach)
    keeper = FrameKeeper(frames_dir, (args.frames,))
    keeper.start()
    try:
        if args.route == "cli":
            t = time.time()
            code, text = run([binary, "--extract-disc", args.iso] + pin, folder, args.timeout, "extract")
            stages.append(("verify+extract (--extract-disc)", time.time() - t))
            if code != 0:
                say("DRILL: FAILED extraction exit %d: %s" % (code, text.strip()[-400:]))
                return 1
            t = time.time()
            code, text = run([binary, "--launcher", "--launcher-screenshot", os.path.join(args.out, "ready.ppm")] + game_flags + pin,
                             folder, args.timeout, "launch")
            stages.append(("launch -> Ready -> Play -> beach", time.time() - t))
            play_line = "launcher: Play (harness)"
        else:
            t = time.time()
            code, text = run([binary, "--launcher", "--launcher-disc", args.iso] + game_flags + pin, folder, args.timeout, "launch")
            stages.append(("launch -> drop -> verify -> extract -> Play -> beach", time.time() - t))
            play_line = "launcher: Play (auto)"
    finally:
        keeper.stop.set()
        keeper.join()
    total = time.time() - t0

    # 3. judge
    meta = read_meta(os.path.join(game_out, "run.meta"))
    # With a window the harness writes the final PRESENTED frame as w_final.ppm (the per-key
    # f<key>/p<key> dump is the headless frames lane); either is the beach evidence.
    kept = os.path.join(frames_dir, "w_final.ppm")
    if not os.path.isfile(kept):
        kept = os.path.join(frames_dir, "p%d.ppm" % args.frames)
    fails = []
    if code != 0:
        fails.append("game exit %d" % code)
    if play_line not in text:
        fails.append("no '%s' line" % play_line)
    if meta.get("finalized") != "1" or meta.get("frames") != str(args.frames):
        fails.append("run.meta finalized=%s frames=%s" % (meta.get("finalized"), meta.get("frames")))
    if meta.get("video") != "gl":
        fails.append("run.meta video=%s" % meta.get("video"))
    if not os.path.isfile(kept):
        fails.append("the final presented frame (w_final.ppm / p%d.ppm) was not dumped" % args.frames)
    else:
        ppm_to_png(kept, os.path.join(args.out, "beach_final_frame.png"))
        say("beach evidence: %s -> beach_final_frame.png" % os.path.basename(kept))
    gf = os.path.join(user_data, "game_files")
    elf = os.path.join(gf, "example_title.rep", "SLUS_123.45")
    stamp = os.path.join(gf, ".nativeee_build_id")
    if not os.path.isfile(elf):
        fails.append("ELF not extracted at %s" % elf)
    if not os.path.isfile(stamp):
        fails.append("no extraction stamp at %s" % stamp)
    else:
        say("stamp: %s" % open(stamp).read().strip())
    if os.path.getmtime(args.iso) != iso_mtime or os.path.getsize(args.iso) != iso_size:
        fails.append("the user's ISO changed (mtime/size) -- it must be read only")
    ud_entries = sorted(os.listdir(user_data))
    say("user data root %s: %s" % (user_data, ", ".join(ud_entries)))
    say("run.meta: finalized=%s frames=%s wall_ms=%s video=%s game_hz=%s" %
        (meta.get("finalized"), meta.get("frames"), meta.get("wall_ms"), meta.get("video"), meta.get("game_hz", "")[:60]))
    for name, secs in stages:
        say("stage %-52s %7.1f s" % (name, secs))
    say("unzip -> beach: %.1f s (%s; budget 300 s)" % (total, "within budget" if total < 300 else "OVER BUDGET"))
    if total >= 300:
        fails.append("unzip -> beach %.1f s >= 300 s" % total)
    if fails:
        say("DRILL: FAILED " + "; ".join(fails))
        return 1
    say("DRILL: DONE %.1f" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
