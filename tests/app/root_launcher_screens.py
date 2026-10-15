#!/usr/bin/env python3
"""RootLauncherScreens (CHARTER 2 C2, Bernoulli): the launcher GUI of the actual play binary opens a
window and PRESENTS its two screens -- no game process, no disc of the user's.

  first run : `--launcher --launcher-screenshot <ppm> --set paths.user_data=<empty tmp>` -> stderr
              `launcher: first run: no game files`, the PPM written, non-trivial (>= 400x300), NOT a
              blank frame (>= 0.5 % of the pixels are light text/widget pixels), exit 0.
  ready     : the same over a user data root the synthetic extraction filled (make_synthetic_iso +
              `--extract-disc` with the injected hash) -> `launcher: settings screen`, PPM as above.
  controls  : the same with `--launcher-page controls` (the Controls page: gamepad-button remap table +
              keyboard table, CHARTER 2 A5) -> PPM as above.
  drop-refused : `--launcher-disc <noise> --launcher-then-play` over an empty root -> `launcher: scripted
              drop`, `extraction refused`, `scripted drop refused`, exit 2, nothing written.
  bypass    : a harness command line (`--game-files <tmp> --headless --frames 1`) never opens the
              launcher: no `launcher:` line on stderr.
  hand-off  : with the REAL game files (--game-files; SKIPPED silently when absent): `--launcher
              --launcher-screenshot --launcher-then-play --harness-window --frames 120` -> `launcher:
              Play (harness)`, then the game runs: run.meta finalized=1 frames=120 video=gl (one game
              process; RUN_SERIAL); and the same WITHOUT a screenshot -> `launcher: Play (auto)` (the
              first-run drill's hand-off), run.meta as above.
SKIP 77 when the window cannot open (no display). Selftest pins the PPM judge on canned images.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile


def run(cmd, timeout):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def judge_ppm(path):
    """Returns (ok, detail) for a P6 PPM: size >= 400x300 and >= 0.5 % light pixels."""
    if not os.path.isfile(path):
        return False, "missing"
    data = open(path, "rb").read()
    parts = data.split(b"\n", 3)
    if len(parts) < 4 or parts[0] != b"P6":
        return False, "not P6"
    w, h = [int(x) for x in parts[1].split()]
    px = parts[3]
    if w < 400 or h < 300 or len(px) < w * h * 3:
        return False, "size %dx%d bytes %d" % (w, h, len(px))
    light = 0
    total = w * h
    step = 7  # sample every 7th pixel
    for i in range(0, total, step):
        r, g, b = px[3 * i], px[3 * i + 1], px[3 * i + 2]
        if r + g + b > 300:
            light += 1
    share = light * step / float(total)
    return share >= 0.005, "%dx%d light share %.3f" % (w, h, share)


def selftest():
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, "a.ppm")
        w, h = 400, 300
        # 5 % light pixels.
        px = bytearray(b"\x10\x10\x10" * (w * h))
        for i in range(0, w * h, 20):
            px[3 * i:3 * i + 3] = b"\xff\xff\xff"
        open(p, "wb").write(b"P6\n%d %d\n255\n" % (w, h) + bytes(px))
        ok &= judge_ppm(p)[0]
        open(p, "wb").write(b"P6\n%d %d\n255\n" % (w, h) + b"\x10\x10\x10" * (w * h))
        ok &= not judge_ppm(p)[0]
        open(p, "wb").write(b"P6\n100 100\n255\n" + b"\xff" * 30000)
        ok &= not judge_ppm(p)[0]
        ok &= not judge_ppm(os.path.join(tmp, "none.ppm"))[0]
    print("root_launcher_screens selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary")
    ap.add_argument("--make-iso")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--game-files", default="")
    ap.add_argument("--runner", default="")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.binary or not os.path.isfile(args.binary) or not args.make_iso or not os.path.isfile(args.make_iso):
        print("SKIP: binary or make_synthetic_iso missing")
        return 77
    tmp = tempfile.mkdtemp(prefix="root_launcher_")
    fails = []
    try:
        empty = os.path.join(tmp, "empty")
        shot1 = os.path.join(tmp, "first_run.ppm")
        code, out, err = run([args.binary, "--launcher", "--launcher-screenshot", shot1, "--set", "paths.user_data=" + empty], args.timeout)
        print("--- first run exit=%d\n%s" % (code, err.strip()))
        if "cannot open the window" in err:
            print("SKIP: no display for the launcher window")
            return 77
        ok, detail = judge_ppm(shot1)
        print("first-run frame: %s" % detail)
        if code != 0 or "launcher: first run: no game files" not in err or not ok:
            fails.append("first-run screen: exit %d, frame %s" % (code, detail))
        # Ready: synthetic extraction first.
        elf = os.urandom(50000)
        elf_path = os.path.join(tmp, "fake.elf")
        open(elf_path, "wb").write(elf)
        iso = os.path.join(tmp, "fake.iso")
        run([args.make_iso, iso, "plain", "SLUS_123.45=" + elf_path], 60)
        data = os.path.join(tmp, "data")
        code, out, err = run([args.binary, "--extract-disc", iso, "--set", "paths.user_data=" + data,
                              "--set", "launcher.elf_sha256=" + hashlib.sha256(elf).hexdigest()], args.timeout)
        if code != 0:
            fails.append("extraction for the ready screen failed: %s" % err.strip()[-300:])
        shot2 = os.path.join(tmp, "ready.ppm")
        code, out, err = run([args.binary, "--launcher", "--launcher-screenshot", shot2, "--set", "paths.user_data=" + data], args.timeout)
        print("--- ready exit=%d\n%s" % (code, err.strip()))
        ok, detail = judge_ppm(shot2)
        print("ready frame: %s" % detail)
        if code != 0 or "launcher: settings screen" not in err or not ok:
            fails.append("ready screen: exit %d, frame %s" % (code, detail))
        # Controls page (the gamepad-button remap + keyboard tables).
        shot3 = os.path.join(tmp, "controls.ppm")
        code, out, err = run([args.binary, "--launcher", "--launcher-screenshot", shot3, "--launcher-page", "controls",
                              "--set", "paths.user_data=" + data], args.timeout)
        ok, detail = judge_ppm(shot3)
        print("--- controls exit=%d frame: %s" % (code, detail))
        if code != 0 or "launcher: settings screen" not in err or not ok or "unknown --launcher-page" in err:
            fails.append("controls page: exit %d, frame %s" % (code, detail))
        # Scripted drop of a refused image + auto-play: refused, leaves, writes nothing.
        noise = os.path.join(tmp, "noise.iso")
        open(noise, "wb").write(os.urandom(70000))
        empty2 = os.path.join(tmp, "empty2")
        code, out, err = run([args.binary, "--launcher", "--launcher-disc", noise, "--launcher-then-play",
                              "--set", "paths.user_data=" + empty2], args.timeout)
        print("--- drop-refused exit=%d\n%s" % (code, err.strip()[-400:]))
        written = os.path.isdir(os.path.join(empty2, "game_files")) and os.listdir(os.path.join(empty2, "game_files"))
        if (code != 2 or "launcher: scripted drop " + noise not in err or "extraction refused" not in err or
                "scripted drop refused" not in err or written):
            fails.append("drop-refused: exit %d, lines %s/%s/%s, game_files written=%s" %
                         (code, "scripted drop" in err, "extraction refused" in err, "scripted drop refused" in err, bool(written)))
        # Bypass: a harness command line.
        code, out, err = run([args.binary, "--game-files", os.path.join(tmp, "nowhere"), "--headless", "--frames", "1",
                              "--set", "paths.user_data=" + empty], args.timeout)
        if "launcher:" in err:
            fails.append("harness command line opened the launcher")
        # Hand-off: launcher -> Play -> the game (real files only).
        elf = os.path.join(args.game_files, "example_title.rep", "SLUS_123.45") if args.game_files else ""
        if elf and os.path.isfile(elf) and args.runner and os.path.isfile(args.runner):
            out_dir = os.path.join(tmp, "handoff")
            log = os.path.join(tmp, "handoff.log")
            cmd = [args.runner, str(args.timeout), log, "--", args.binary, "--launcher", "--launcher-screenshot",
                   os.path.join(tmp, "h.ppm"), "--launcher-then-play", "--set", "paths.user_data=" + os.path.join(tmp, "udata"),
                   "--game-files", args.game_files, "--set", "paths.mc_dir=" + os.path.join(tmp, "mc"), "--harness-window",
                   "--harness-out", out_dir, "--frames", "120"]
            code, out, err = run(cmd, args.timeout + 30)
            text = open(log).read() if os.path.isfile(log) else err
            meta = {}
            mp = os.path.join(out_dir, "run.meta")
            if os.path.isfile(mp):
                for line in open(mp):
                    if "=" in line:
                        k, v = line.strip().split("=", 1)
                        meta[k] = v
            print("--- hand-off exit=%d finalized=%s frames=%s video=%s" % (code, meta.get("finalized"), meta.get("frames"), meta.get("video")))
            if "launcher: Play (harness)" not in text or meta.get("finalized") != "1" or meta.get("frames") != "120" or meta.get("video") != "gl":
                fails.append("hand-off: Play line %s, run.meta finalized=%s frames=%s video=%s" %
                             ("present" if "launcher: Play (harness)" in text else "MISSING", meta.get("finalized"), meta.get("frames"), meta.get("video")))
            # Auto-play (no screenshot): the first-run drill's hand-off.
            out_dir2 = os.path.join(tmp, "handoff_auto")
            log2 = os.path.join(tmp, "handoff_auto.log")
            cmd = [args.runner, str(args.timeout), log2, "--", args.binary, "--launcher", "--launcher-then-play",
                   "--set", "paths.user_data=" + os.path.join(tmp, "udata2"), "--game-files", args.game_files,
                   "--set", "paths.mc_dir=" + os.path.join(tmp, "mc2"), "--harness-window", "--harness-out", out_dir2, "--frames", "120"]
            code, out, err = run(cmd, args.timeout + 30)
            text = open(log2).read() if os.path.isfile(log2) else err
            meta = {}
            mp = os.path.join(out_dir2, "run.meta")
            if os.path.isfile(mp):
                for line in open(mp):
                    if "=" in line:
                        k, v = line.strip().split("=", 1)
                        meta[k] = v
            print("--- hand-off (auto) exit=%d finalized=%s frames=%s video=%s" % (code, meta.get("finalized"), meta.get("frames"), meta.get("video")))
            if "launcher: Play (auto)" not in text or meta.get("finalized") != "1" or meta.get("frames") != "120" or meta.get("video") != "gl":
                fails.append("hand-off (auto): Play line %s, run.meta finalized=%s frames=%s video=%s" %
                             ("present" if "launcher: Play (auto)" in text else "MISSING", meta.get("finalized"), meta.get("frames"), meta.get("video")))
        else:
            print("hand-off leg skipped (no game files / runner)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("FAIL root_launcher_screens:\n  " + "\n  ".join(fails))
        return 1
    print("PASS root_launcher_screens: first-run, settings and controls screens presented; refused scripted drop leaves; harness CLI bypasses")
    return 0


if __name__ == "__main__":
    sys.exit(main())
