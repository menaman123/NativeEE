#!/usr/bin/env python3
"""launcher_folders_judge.py -- the memory-card and save-state FOLDERS through the launcher's own path.

The frozen launcher_settings_judge.py names `paths.mc_dir` and `paths.save_dir` "not judged" (its leg pins the
memory card itself). This judge drives the two rows the setup window asks for -- "Memory cards" and "Save states"
-- through the launcher's headless Build (`ps2r_launcher --prepare --subfolder memcards=DIR --subfolder saves=DIR`,
the Build button's code path, D-L14) and checks that:

  A. write.   After the pipeline rewrote <folder>/nativeee.ini, the launcher put the player's choices back (D-L13):
              the engine's own reader (`nativeee_settings get`) returns the chosen absolute paths for `paths.mc_dir`
              and `paths.save_dir`, and `nativeee_settings path --exe-dir <folder>` resolves THAT file for a game
              started from the folder (`source=executable folder`).
  B. read.    The prepared game is started FROM the folder (the binary beside the file, no --config, no --set for the
              paths), in a harness window, `harness.inject_key=F5@<n>` (the F5 press through the real key route): run.meta says `settings_file=<folder>/nativeee.ini:
              source=executable folder`, and the slot the game wrote is `<saves>/savestates/slot<N>.ps2st` -- the
              save states folder the launcher's Save States page lists (`${saves}/savestates`), NOT the harness out dir
              and NOT <folder>/cache/logs/savestates (where an empty `paths.save_dir` would have sent it).
              The memory cards folder is read the same way (A); the game only creates `<mc_dir>/mc0` when it first
              writes a card, which a 60-frame boot does not reach, so B reports mc_dir by the resolver only.

  launcher_folders_judge.py --launcher <ps2r_launcher exe> --tool <nativeee_settings> --fixture <dir> --iso <image>
        --out <dir> [--memcards <dir>] [--saves <dir>] [--jobs N] [--frames 60] [--save-at 30] [--release TAG]
        [--state <.ps2st>] [--skip-build]
      --memcards / --saves default to folders OUTSIDE the game folder (<out>/cards, <out>/snapshots), the case the
      portable note talks about. Exit 0 when every check is GREEN. `uptime` is printed with every timing number.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time


def uptime():
    try:
        return subprocess.run(["uptime"], capture_output=True, text=True).stdout.strip().split("load")[-1]
    except Exception:
        return "?"


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def read_meta(path):
    rows = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if "=" in line:
                    k, v = line.rstrip("\n").split("=", 1)
                    rows[k.strip()] = v.strip()
    except OSError:
        pass
    return rows


def tool(args, exe):
    p = subprocess.run([exe] + args, capture_output=True, text=True)
    return p.returncode, p.stdout.strip(), p.stderr.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--launcher", required=True)
    ap.add_argument("--tool", required=True, help="nativeee_settings")
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--iso", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--memcards")
    ap.add_argument("--saves")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--save-at", type=int, default=30)
    ap.add_argument("--release", default="v0.0.0-judge")
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--state", help="a .ps2st to start leg B from (a cold boot may not reach a logic frame in --frames)")
    ap.add_argument("--leg-timeout", type=int, default=240)
    ap.add_argument("--skip-build", action="store_true", help="reuse <out>/game from an earlier run (leg B only)")
    a = ap.parse_args()
    if a.state:
        a.state = os.path.abspath(os.path.expanduser(a.state))
    for k in ("launcher", "tool", "fixture", "iso", "out"):
        setattr(a, k, os.path.abspath(os.path.expanduser(getattr(a, k))))
    os.makedirs(a.out, exist_ok=True)
    folder = os.path.join(a.out, "game")
    memcards = os.path.abspath(a.memcards or os.path.join(a.out, "cards"))
    saves = os.path.abspath(a.saves or os.path.join(a.out, "snapshots"))
    if not a.skip_build:
        shutil.rmtree(folder, ignore_errors=True)
        for d in (memcards, saves):
            shutil.rmtree(d, ignore_errors=True)
    shutil.rmtree(os.path.join(saves, "savestates"), ignore_errors=True)   # leg B's slot must be this run's
    engine = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    results = []

    # the offline release (the cold judge's model): releases.json over the fixture, served on 127.0.0.1
    port = free_port()
    base = "http://127.0.0.1:%d" % port
    mri = os.path.join(engine, "tools", "ci", "make_release_index.py")
    idx = subprocess.run([sys.executable, mri, "--dir", a.fixture, "--tag", a.release, "--base-url", base], capture_output=True, text=True)
    if idx.returncode != 0:
        sys.exit("launcher_folders_judge: make_release_index failed: %s" % idx.stderr)
    server = subprocess.Popen([sys.executable, "-m", "http.server", str(port), "--bind", "127.0.0.1"], cwd=a.fixture,
                              stdout=open(os.path.join(a.out, "fixture_server.log"), "w"), stderr=subprocess.STDOUT)
    time.sleep(0.8)
    home = tempfile.mkdtemp(prefix="folders_home_")
    env = dict(os.environ, HOME=home)
    for k in list(env):
        if k.startswith("NATIVEEE_") or k.startswith("PS2R_"):
            del env[k]
    print("== launcher_folders_judge %s load%s folder=%s memcards=%s saves=%s" % (time.strftime("%H:%M:%S"), uptime(), folder, memcards, saves))
    try:
        # ---- the launcher's Build, headless, with the two folder rows chosen -----------------------------
        report = os.path.join(a.out, "prepare_report.json")
        if a.skip_build:
            rep = json.load(open(report))
            binary = rep.get("binary", "") or json.load(open(os.path.join(folder, "prepared.json")))["binary"]
            print("== Build reused from %s: binary=%s" % (report, binary))
            results.append(("build", "prepared", os.path.exists(binary), "reused; binary=%r" % binary))
        else:
            cmd = [a.launcher, "--prepare", "--index", base + "/releases.json", "--iso", a.iso, "--folder", folder, "--jobs", str(a.jobs),
                   "--json", report, "--subfolder", "memcards=" + memcards, "--subfolder", "saves=" + saves]
            t0 = time.time()
            with open(os.path.join(a.out, "prepare_stderr.log"), "w") as lf:
                p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=lf, text=True, env=env, timeout=a.timeout)
            wall = time.time() - t0
            binary = p.stdout.strip().splitlines()[-1] if p.stdout.strip() else ""
            rep = {}
            try:
                rep = json.load(open(report))
            except Exception:
                pass
            print("== Build rc=%d wall=%.1fs load%s binary=%s ok=%s" % (p.returncode, wall, uptime(), binary, rep.get("ok")))
            results.append(("build", "prepared", p.returncode == 0 and bool(binary) and os.path.exists(binary) and rep.get("ok") is True,
                            "rc=%d binary=%r" % (p.returncode, binary)))
        ini = os.path.join(folder, "nativeee.ini")

        # ---- A: the file says what the player chose, read by the engine's own reader ---------------------
        for key, want in (("paths.mc_dir", memcards), ("paths.save_dir", saves), ("paths.user_data", os.path.join(folder, "cache"))):
            rc, out, err = tool(["get", ini, key], a.tool)
            results.append(("A.write", key, rc == 0 and os.path.realpath(out) == os.path.realpath(want), "file says %r" % out))
        rc, out, err = tool(["path", "--exe-dir", folder, "--no-migrate"], a.tool)
        resolved = dict(kv.split("=", 1) for kv in out.splitlines() if "=" in kv) if rc == 0 else {}
        results.append(("A.resolve", "settings_file", rc == 0 and os.path.realpath(resolved.get("path", "")) == os.path.realpath(ini)
                        and resolved.get("source", "") == "executable folder", "nativeee_settings path: %s" % " ".join(out.split())))
        results.append(("A.folders", "made", os.path.isdir(memcards) and os.path.isdir(saves), "memcards %s saves %s" % (os.path.isdir(memcards), os.path.isdir(saves))))

        # ---- B: the game started FROM the folder reads the file and writes its slot where the file says ----
        exe = binary
        if binary.endswith(".app"):
            inside = os.path.join(binary, "Contents", "MacOS")
            names = os.listdir(inside) if os.path.isdir(inside) else []
            exe = os.path.join(inside, names[0]) if names else binary
        out_b = os.path.join(a.out, "leg_from_folder")
        shutil.rmtree(out_b, ignore_errors=True)
        env_b = dict(env, NATIVEEE_WATCHDOG_S="55", NATIVEEE_MTVU="1", NATIVEEE_VU1_MODE="aot", NATIVEEE_UNPACED="1", NATIVEEE_PAD_SCRIPT="")
        # a key press needs a window: --harness-window (the settings judge's leg D does the same for F1)
        args = [exe, "--harness-window", "--frames", str(a.frames), "--iso", a.iso, "--harness-out", out_b, "--set", "harness.inject_key=F5@%d" % a.save_at]
        if a.state:
            args += ["--load-state", a.state]
        env_b["NATIVEEE_WATCHDOG_S"] = str(a.leg_timeout)
        t0 = time.time()
        with open(os.path.join(a.out, "leg_from_folder.log"), "w") as lf:
            try:
                rc = subprocess.run(args, stdout=lf, stderr=subprocess.STDOUT, env=env_b, cwd=folder, timeout=a.leg_timeout + 30).returncode
            except subprocess.TimeoutExpired:
                rc = -9
        wall = time.time() - t0
        meta = read_meta(os.path.join(out_b, "run.meta"))
        sf = meta.get("settings_file", "(absent)")
        print("== B (from the folder) rc=%d wall=%.1fs load%s settings_file=%s" % (rc, wall, uptime(), sf))
        results.append(("B.run", "rc", rc == 0, "rc=%d" % rc))
        results.append(("B.run", "settings_file", os.path.realpath(sf.split(":source=")[0]) == os.path.realpath(ini) and sf.endswith(":source=executable folder"),
                        "run.meta settings_file=%s" % sf))
        rc_s, slot_s, _ = tool(["get", ini, "save.slot"], a.tool)
        slot = slot_s if rc_s == 0 and slot_s else "0"
        slot_file = os.path.join(saves, "savestates", "slot%s.ps2st" % slot)
        wrong_out = os.path.join(out_b, "savestates", "slot%s.ps2st" % slot)
        wrong_cache = os.path.join(folder, "cache", "logs", "savestates", "slot%s.ps2st" % slot)
        wrong_folder = os.path.join(folder, "saves", "savestates", "slot%s.ps2st" % slot)
        results.append(("B.game", "paths.save_dir", os.path.isfile(slot_file) and not os.path.exists(wrong_out) and not os.path.exists(wrong_cache)
                        and not os.path.exists(wrong_folder),
                        "slot at %s: %s; harness out: %s; cache/logs: %s; <folder>/saves: %s" % (slot_file, os.path.isfile(slot_file), os.path.exists(wrong_out),
                                                                                             os.path.exists(wrong_cache), os.path.exists(wrong_folder))))
        results.append(("B.game", "paths.mc_dir", True, "by the resolver (A): the game creates <mc_dir>/mc0 only at its first card write; mc0 present: %s"
                        % os.path.isdir(os.path.join(memcards, "mc0"))))
    finally:
        server.terminate()
        shutil.rmtree(home, ignore_errors=True)

    red = 0
    for leg, key, ok, why in results:
        print("%s  %-10s %-18s %s" % ("GREEN" if ok else "RED  ", leg, key, why))
        red += 0 if ok else 1
    verdict = {"ok": red == 0, "folder": folder, "memcards": memcards, "saves": saves,
               "results": [{"leg": l, "key": k, "ok": o, "why": w} for l, k, o, w in results]}
    json.dump(verdict, open(os.path.join(a.out, "verdict.json"), "w"), indent=2)
    print("== %s: %d red of %d checks (verdict.json in %s)" % ("GREEN" if red == 0 else "RED", red, len(results), a.out))
    return 0 if red == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
