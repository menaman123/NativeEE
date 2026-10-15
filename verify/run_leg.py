#!/usr/bin/env python3
"""run_leg.py -- run ONE deterministic leg of the Phase-0 verification
harness (EQUIVALENCE_PLAN.md section 2).

  run_leg.py --binary <host_app> --anchor <state.ps2st|coldboot>
             --frames N [--inputs <padlog>] [--profile deterministic]
             --out <leg dir> [--tree legacy|v2|new] [--repo <old tree root>]
             [--timeout SECONDS] [--stall-s SECONDS] [--state-every K] [--set key=val ...]

--tree new runs the NEW nativeee binary (Phase 1+): no environment is set;
the leg is armed on the command line -- `--profile deterministic --headless
--harness-out <leg dir> --frames N` with `--anchor S` -> `--load-state S`,
`--inputs F` -> `--inputs F`, `--state-every K` -> `--state-every K`, and every
`--set key=val` (repeatable; the one-line toggles of DECISIONS_LOG) -> `--set key=val`
in the order given. `--set` is a NEW-tree option only (the OLD trees have no such
knob): a legacy/v2 leg with `--set` is a usage error.

Runs the binary bounded (verify/run_bounded.sh, ours: wall timeout + RSS kill +
progress watchdog), headless, on the deterministic profile, with
PS2_VERIFY_OUT=<leg dir>. Asserts run.meta completeness afterwards. Exit 0 =
leg produced a complete artifact set; nonzero otherwise.

--stall-s SECONDS sets the progress watchdog (run_bounded.sh's RB_STALL_S: the
leg is killed when frames.fhash has not grown for that long). Default: the
inherited RB_STALL_S, else 300 s for OLD trees / 60 s for NEW. A leg whose
phases present nothing for minutes (the FMVs, the Beach load, the attract's
reload -- games/<title>/verify/baseline/<leg>/LEG.tsv `stall_s`) raises it here.

Frozen-tree hygiene (REFERENCE_FREEZE.md "Accepted side effects"): the OLD
binary chdirs to its own project root on startup (host_app/main.cpp:4976-4979)
and would write bench_report.json there -- a TRACKED file -- so every OLD leg
passes `--bench-report <leg dir>/bench_report.json` (absolute; bench::ParseArgs
resolves the path before the chdir). The remaining OLD-tree writes (`logs/*`,
and `bug_reports/<stamp>_crash/` on a crash) are gitignored and listed in the
freeze contract as the accepted side effect of running the oracle.
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from profile_deterministic import PROFILE_NAME, build_leg_env  # noqa: E402

DEFAULT_REPO = os.environ.get("PS2R_OLD_ROOT", "")  # the frozen OLD tree (read-only); empty = absent
REQUIRED_META = ["build_id", "gl_renderer", "tree", "profile", "env_ps2",
                 "env_hash", "frames", "wall_ms", "finalized"]
ARTIFACTS = ["frames.fhash", "audio.ahash", "mem.mhash", "run.meta"]


def parse_meta(path):
    meta = {}
    with open(path, "r") as f:
        for line in f:
            line = line.rstrip("\n")
            if "=" in line:
                k, v = line.split("=", 1)
                meta[k] = v
    return meta


def is_park_leg(meta):
    """A NEW-binary leg that ended at a guest park (PHASE3_PLAN 4.4): the
    park keys are the result; frames=0 and empty lane files are legitimate
    because no frame boundary ran before the park."""
    return meta.get("park", "") not in ("", "none")


def check_meta(leg_dir, frames_expected):
    meta_path = os.path.join(leg_dir, "run.meta")
    if not os.path.isfile(meta_path):
        return "run.meta missing"
    meta = parse_meta(meta_path)
    missing = [k for k in REQUIRED_META if k not in meta or meta[k] == ""]
    if missing:
        return "run.meta incomplete: missing %s" % ",".join(missing)
    if meta.get("finalized") != "1":
        return "run.meta not finalized (leg killed before exit hook?)"
    if is_park_leg(meta):
        for k in ("park_pc", "park_tid", "unhandled_mmio"):
            if k not in meta:
                return "park leg missing key %s" % k
        return None
    # Presents <= vsyncs by design (present-skip paths bypass the scanout
    # block), so the present count may fall slightly short of the bench
    # vsync target; require a sane floor, not equality.
    if int(meta.get("frames", "0")) < max(1, int(frames_expected * 0.5)):
        return "run.meta frames=%s way below requested %d" % (
            meta.get("frames"), frames_expected)
    return None


def run_leg(binary, anchor, frames, out_dir, repo, tree, inputs=None,
            timeout=None, state_every=None, profile=PROFILE_NAME,
            game_files=None, sets=None, stall_s=None, dump_frames=None):
    if profile != PROFILE_NAME:
        raise SystemExit("only the '%s' profile exists in Phase 0"
                         % PROFILE_NAME)
    if sets and tree != "new":
        raise SystemExit("--set is a NEW-tree option (tree=%s has no Config knob)" % tree)
    if dump_frames and tree != "new":
        raise SystemExit("--dump-frames is a NEW-tree harness knob (tree=%s)" % tree)
    binary = os.path.abspath(binary)
    out_dir = os.path.abspath(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    if tree == "new":
        return run_leg_new(binary, anchor, frames, out_dir, repo, inputs,
                           timeout, state_every, profile, game_files, sets,
                           stall_s,
                           dump_frames=dump_frames)
    extra = {}
    if inputs:
        extra["PS2_PAD_SCRIPT_FILE"] = os.path.abspath(inputs)
    if state_every:
        extra["PS2_VERIFY_STATE_EVERY"] = str(state_every)
    env = build_leg_env(os.environ, out_dir, tree, extra)

    args = [binary, "--bench-frames", str(frames)]
    if anchor != "coldboot":
        state = anchor if os.path.isabs(anchor) else os.path.join(repo,
                                                                  anchor)
        if not os.path.isfile(state):
            raise SystemExit("anchor state not found: %s" % state)
        args = [binary, "--load-state", state,
                "--bench-frames", str(frames)]
    # OLD trees only: keep the bench report out of the frozen tree (see the
    # module docstring). The NEW binary (Phase 1+) has no bench report; its
    # harness output is confined to --harness-out.
    if tree in ("legacy", "v2"):
        args += ["--bench-report", os.path.join(out_dir, "bench_report.json")]

    if timeout is None:
        # Hard cap scales with the leg's frame count: FRAME-policy boots run
        # ~1 s/vf (measured: coldboot reaches vf 300 in ~5 min), so budget
        # 2 s/frame + 300 s base. Wedges don't wait for this cap: the progress
        # watchdog below kills after 300 s with NO frames.fhash growth (the
        # hasher flushes per present).
        timeout = max(600, 300 + frames * 2)
    runner = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "run_bounded.sh")
    log = out_dir + ".log"
    cmd = [runner, str(timeout), log, "--"] + args
    env["RB_PROGRESS_FILE"] = os.path.join(out_dir, "frames.fhash")
    env["RB_STALL_S"] = stall_env(env, stall_s, "300")
    # cwd stays the OLD root for OLD trees (relative anchor paths, and the
    # binary chdirs there anyway); the runner itself never changes directory.
    proc = subprocess.run(cmd, cwd=repo, env=env)
    err = check_meta(out_dir, frames)
    meta_path = os.path.join(out_dir, "run.meta")
    park = os.path.isfile(meta_path) and is_park_leg(parse_meta(meta_path))
    for a in ARTIFACTS:
        p = os.path.join(out_dir, a)
        if not os.path.isfile(p):
            err = err or ("artifact missing: " + a)
        elif os.path.getsize(p) == 0 and not park:
            err = err or ("artifact empty: " + a)
    if proc.returncode != 0:
        err = err or ("runner exit=%d (timeout/RSS kill?)" %
                      proc.returncode)
    return err


def new_leg_args(binary, anchor, frames, out_dir, repo, inputs=None,
                 state_every=None, profile=PROFILE_NAME, game_files=None,
                 sets=None, dump_frames=None):
    """The NEW binary's argv for one leg (pure; pinned by `--selftest`).
    dump_frames: the binary's `--dump-frames <dir>` harness knob (every presented
    frame as <dir>/f<key>.ppm, the frames lane's own image) for pixel attribution of a
    divergent key; it adds no Config key, so the env_ps2 fingerprint is unchanged."""
    if not game_files:
        game_files = os.path.join(repo, "game_files")
    args = [binary, "--profile", profile, "--headless",
            "--harness-out", out_dir, "--frames", str(frames),
            "--game-files", os.path.abspath(game_files)]
    if anchor != "coldboot":
        state = anchor if os.path.isabs(anchor) else os.path.join(repo, anchor)
        args += ["--load-state", state]
    if inputs:
        args += ["--inputs", os.path.abspath(inputs)]
    if state_every:
        args += ["--state-every", str(state_every)]
    for kv in sets or ():
        if "=" not in kv:
            raise SystemExit("--set expects key=val, got %r" % kv)
        args += ["--set", kv]
    if dump_frames:
        args += ["--dump-frames", os.path.abspath(dump_frames)]
    return args


def stall_env(env, stall_s, default):
    """The RB_STALL_S value for a leg: an explicit --stall-s wins, else the inherited
    variable, else the tree's default (pure; pinned by --selftest)."""
    if stall_s:
        return str(int(stall_s))
    return env.get("RB_STALL_S", default)


def run_leg_new(binary, anchor, frames, out_dir, repo, inputs, timeout,
                state_every, profile, game_files=None, sets=None,
                stall_s=None, dump_frames=None):
    """NEW-binary leg: CLI-armed harness, no environment variables. The
    title files (ELF) come from --game-files, defaulting to the frozen
    reference tree's game_files/ (the harness oracle's copy)."""
    if dump_frames:
        os.makedirs(dump_frames, exist_ok=True)
    args = new_leg_args(binary, anchor, frames, out_dir, repo, inputs,
                        state_every, profile, game_files, sets, dump_frames)
    if anchor != "coldboot":
        state = anchor if os.path.isabs(anchor) else os.path.join(repo, anchor)
        if not os.path.isfile(state):
            raise SystemExit("anchor state not found: %s" % state)
    if timeout is None:
        timeout = max(120, 60 + frames // 10)
    runner = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "run_bounded.sh")
    log = out_dir + ".log"
    env = dict(os.environ)
    env["RB_PROGRESS_FILE"] = os.path.join(out_dir, "frames.fhash")
    env["RB_STALL_S"] = stall_env(env, stall_s, "60")
    proc = subprocess.run([runner, str(timeout), log, "--"] + args, env=env)
    err = check_meta(out_dir, frames)
    meta_path = os.path.join(out_dir, "run.meta")
    park = os.path.isfile(meta_path) and is_park_leg(parse_meta(meta_path))
    for a in ARTIFACTS:
        p = os.path.join(out_dir, a)
        if not os.path.isfile(p):
            err = err or ("artifact missing: " + a)
        elif os.path.getsize(p) == 0 and not park:
            err = err or ("artifact empty: " + a)
    if proc.returncode != 0:
        err = err or ("runner exit=%d (timeout/RSS kill?)" % proc.returncode)
    return err


def selftest():
    """The NEW argv: `--set key=val` appears once per value, in order, after the
    harness arming; without --set no `--set` token exists."""
    ok = True
    args = new_leg_args("bin", "coldboot", 7, "/x/out", "/repo", state_every=2,
                        sets=["a.b=1", "c.d=two"])
    ok &= args[:8] == ["bin", "--profile", PROFILE_NAME, "--headless",
                       "--harness-out", "/x/out", "--frames", "7"]
    ok &= args[args.index("--state-every") + 1] == "2"
    ok &= args[-4:] == ["--set", "a.b=1", "--set", "c.d=two"]
    ok &= args.count("--set") == 2
    # --dump-frames is appended last, absolute, and absent when not requested
    dumped = new_leg_args("bin", "coldboot", 7, "/x/out", "/repo", sets=["a.b=1"], dump_frames="/x/frames")
    ok &= dumped[-2:] == ["--dump-frames", "/x/frames"] and dumped[-4:-2] == ["--set", "a.b=1"]
    ok &= "--dump-frames" not in args
    plain = new_leg_args("bin", "/s.ps2st", 7, "/x/out", "/repo")
    ok &= "--set" not in plain and plain[plain.index("--load-state") + 1] == "/s.ps2st"
    # the stall watchdog: --stall-s wins over the inherited RB_STALL_S, which wins over the default
    ok &= stall_env({}, None, "60") == "60" and stall_env({"RB_STALL_S": "600"}, None, "60") == "600"
    ok &= stall_env({"RB_STALL_S": "600"}, 900, "60") == "900" and stall_env({}, 0, "300") == "300"
    print("run_leg selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    req = "--selftest" not in sys.argv
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=req)
    ap.add_argument("--anchor", required=req,
                    help="path to .ps2st (repo-relative ok) or 'coldboot'")
    ap.add_argument("--frames", type=int, required=req)
    ap.add_argument("--inputs", help="padlog file (PS2_PAD_SCRIPT grammar)")
    ap.add_argument("--profile", default=PROFILE_NAME)
    ap.add_argument("--out", required=req)
    ap.add_argument("--tree", default="legacy", choices=["legacy", "v2", "new"])
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--timeout", type=int)
    ap.add_argument("--stall-s", type=int,
                    help="progress watchdog seconds (RB_STALL_S) for this leg; default: "
                         "inherited RB_STALL_S, else 300 (OLD trees) / 60 (NEW)")
    ap.add_argument("--state-every", type=int)
    ap.add_argument("--game-files",
                    help="NEW tree: title files dir (default <repo>/game_files)")
    ap.add_argument("--set", action="append", metavar="KEY=VAL",
                    help="NEW tree: Config override passed to the binary as "
                         "`--set KEY=VAL` (repeatable, order kept)")
    ap.add_argument("--dump-frames", metavar="DIR",
                    help="NEW tree: write every presented frame as DIR/f<key>.ppm (the "
                         "binary's --dump-frames harness knob; no Config key, same fingerprint) "
                         "for verify/frame_pair_attribute.py on a divergent key")
    ap.add_argument("--selftest", action="store_true",
                    help="unit checks of the NEW argv construction (--set pass-through)")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    err = run_leg(a.binary, a.anchor, a.frames, a.out, a.repo, a.tree,
                  a.inputs, a.timeout, a.state_every, a.profile,
                  a.game_files, a.set, a.stall_s, a.dump_frames)
    if err:
        print("run_leg: FAIL: %s" % err)
        return 1
    print("run_leg: OK %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
