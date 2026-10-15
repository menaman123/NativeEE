#!/usr/bin/env python3
"""battery.py -- the 12-leg battery against the frozen OLD baselines
(PHASE1_PLAN section 2.9 / 7; CONSTRUCTION_PLAN section 2 harness contract).

  battery.py --binary build/app/host/nativeee_oracle [--lanes meta] [--reps 1]
             [--out /tmp/battery] [--baseline games/<title>/verify/baseline]
             [--repo <OLD root>] [--legs a,b,...] [--timeout SECONDS]
             [--set key=val ...]

The binary is the ORACLE binary (M12, TRACK 3b D2/D3): the frozen baselines are
the retained IOP arm's lanes (`hle.startup=retained`, the IOP module booted), which
only nativeee_oracle compiles in; the service-only play binary runs the
deterministic profile as `native` (its env_ps2 reads FP-MISMATCH by design).

--set key=val (repeatable) is passed through run_leg.py to the NEW binary for
every leg -- the one-line toggles of DECISIONS_LOG's decision loop. A leg run
with --set carries the override in its run.meta env_ps2, so the FP check
reports FP-MISMATCH by design unless the override equals the default.

--timeout SECONDS is passed through to run_leg.py --timeout for every NEW leg
(the per-leg wall bound; run_leg.py's default max(120, 60 + frames // 10)
applies when absent). The anchors' 600-frame legs need more than the default
since the headroom predicate runs the title's maintenance passes every frame
(DECISIONS_LOG DP-16 (d)); the stall watchdog RB_STALL_S is untouched.

For every leg directory under --baseline (coldboot + 11 anchor states +
coldboot_attract) it runs the NEW binary through run_leg.py --tree new for the
baseline's own frame count (its frames.fhash line count, 1500 for coldboot /
600 otherwise -- or the `frames` column of the leg's LEG.tsv when it has one),
then diff_legs.py --cross-engine over the requested lanes against the frozen
OLD leg. A leg's `games/<title>/verify/baseline/<leg>/LEG.tsv` (verify/leg_spec.py; DECISIONS_LOG
DT-5) also raises its wall bound (`timeout_s`: the larger of it and --timeout is
passed) and its stall watchdog (`stall_s` -> run_leg.py --stall-s); the twelve
legs without one keep today's behaviour. With --reps N > 1 the reps are also compared with each other (same
binary: full meta gate) and the leg is ADMISSIBLE only when identical.

Per leg the table prints: ADMISSIBLE (reps identical), the cross-engine
result per lane (META-OK when the meta gate passes; IDENTICAL(lanes) when
every requested hash lane matches), and the NEW leg's fingerprint check:
every NEW run.meta must carry env_ps2 == the deterministic preset's canonical
fingerprint (CONSTRUCTION_PLAN R6) -- printed as FP-OK.

Exit 0 when every leg is META-OK (and ADMISSIBLE when reps > 1) and FP-OK;
1 otherwise. Phase 1 claims the meta lane only; frames/audio/mem are reported
for the record.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from diff_legs import diff_legs, parse_lanes, read_meta  # noqa: E402
from profile_deterministic import NEW_DETERMINISTIC_FINGERPRINT  # noqa: E402
import leg_spec  # noqa: E402

DEFAULT_REPO = os.environ.get("PS2R_OLD_ROOT", "")  # the frozen OLD tree (read-only); empty = absent
ANCHOR_DIR = "save_states"


def baseline_legs(baseline_dir):
    legs = []
    for name in sorted(os.listdir(baseline_dir)):
        d = os.path.join(baseline_dir, name)
        if os.path.isfile(os.path.join(d, "run.meta")):
            legs.append(name)
    return legs


def leg_frames(baseline_dir, leg):
    spec = leg_spec.read_leg(baseline_dir, leg)
    if spec is not None:
        # the leg declares its length (OLD's presents may fall far short of its vsync
        # frame count once FMVs coalesce them; the frozen lane cannot be trusted for it)
        return spec["frames"]
    with open(os.path.join(baseline_dir, leg, "frames.fhash")) as f:
        lines = [l for l in f if l.strip()]
    keys = [int(l.split(",")[0]) for l in lines]
    if not keys:
        return len(lines)
    # coldboot presents 1494 lines keyed 1..1500 (present-skips): the leg
    # length is the last key. Anchor legs key by the OLD vsync counter, which
    # jumps to the state's frame at load (1..600 -> 37755): there the line
    # count (600) is the length.
    last = max(keys)
    return last if last - len(lines) < max(10, len(lines) // 10) else len(lines)


def anchor_for(leg, repo, baseline_dir=None):
    """`coldboot` or the leg's savestate: by the LEG.tsv `anchor` column when the leg has one
    (coldboot_attract boots), else by the leg's name (leg_spec.anchor_for)."""
    spec = leg_spec.read_leg(baseline_dir, leg) if baseline_dir else None
    return leg_spec.anchor_for(spec, leg, repo, ANCHOR_DIR)


def new_leg_cmd(binary, anchor, frames, out_dir, repo, timeout=None, sets=None, stall_s=None, inputs=None,
                mc_dir=None):
    """The run_leg.py argv for one NEW leg. `timeout` (seconds) is passed through
    as `--timeout` so the per-leg wall bound can exceed run_leg.py's default
    (`max(120, 60 + frames // 10)`); None leaves the default in force. `stall_s`
    (the leg's LEG.tsv `stall_s`) is passed as `--stall-s`; None leaves run_leg.py's
    watchdog default in force. `sets` = the `--set key=val` toggles (DECISIONS_LOG's
    one-line toggles), each passed through to run_leg.py (and by it to the binary)
    in the order given. `inputs` (a `source=new` leg's LEG.tsv `pad_script`) is
    passed as `--inputs`; `mc_dir` (its per-rep copy of the frozen `mc_before`
    game-save directory) is passed as `--set paths.mc_dir=<dir>` AFTER the caller's
    sets so the leg contract wins over a battery-wide override."""
    cmd = [sys.executable, os.path.join(HERE, "run_leg.py"), "--binary", binary,
           "--anchor", anchor, "--frames", str(frames), "--out", out_dir,
           "--tree", "new", "--repo", repo]
    if timeout is not None:
        cmd += ["--timeout", str(int(timeout))]
    if stall_s is not None:
        cmd += ["--stall-s", str(int(stall_s))]
    if inputs:
        cmd += ["--inputs", inputs]
    for kv in sets or ():
        cmd += ["--set", kv]
    if mc_dir:
        cmd += ["--set", "paths.mc_dir=" + mc_dir]
    return cmd


def leg_session(baseline_dir, leg):
    """(pad_script, mc_before) of a `source=new` leg: the absolute replayed padlog (or None) and the absolute
    frozen game-save directory the leg starts from (or None) -- leg_spec's session columns."""
    spec = leg_spec.read_leg(baseline_dir, leg)
    title_dir = os.path.dirname(os.path.dirname(baseline_dir))
    return leg_spec.pad_script_for(spec, title_dir), leg_spec.mc_dir_for(spec, os.path.join(baseline_dir, leg))


def stage_mc_dir(mc_before, out_dir):
    """A fresh copy of the frozen game-save directory for ONE rep, next to its leg output (`<out>.mc`): the
    replay may write saves into it; the frozen directory and the other reps never see them."""
    if not mc_before:
        return None
    staged = out_dir.rstrip("/") + ".mc"
    if os.path.isdir(staged):
        import shutil
        shutil.rmtree(staged)
    import shutil
    shutil.copytree(mc_before, staged)
    return staged


def leg_bounds(baseline_dir, leg, requested_timeout):
    """(timeout, stall_s) for one leg: the LEG.tsv's bounds merged with the caller's
    --timeout (leg_spec.effective_timeout); (requested, None) for a leg without one."""
    spec = leg_spec.read_leg(baseline_dir, leg)
    return leg_spec.effective_timeout(spec, requested_timeout), leg_spec.stall_s(spec)


def run_new_leg(binary, anchor, frames, out_dir, repo, timeout=None, sets=None, stall_s=None, inputs=None,
                mc_before=None):
    cmd = new_leg_cmd(binary, anchor, frames, out_dir, repo, timeout, sets, stall_s, inputs,
                      stage_mc_dir(mc_before, out_dir))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return proc.returncode, proc.stdout


def rebased_note(rep):
    """` rebased=<lane> <window>(<rows>);...` from a diff_legs report that compared a re-based window against
    the effective reference (DECISIONS_LOG DT-4: the leg's REBASELINE.tsv) -- the table row carries it, never
    silently; '' when the report has no such line."""
    found = []
    for l in rep:
        if ": rebased " in l:
            head = l.split(": rebased ", 1)[1]
            found.append(head.split(" in ", 1)[0].replace(" (", "(").replace(", ", ","))
    return (" rebased=" + ";".join(found)) if found else ""


def selftest():
    """The --timeout / --stall-s / --set pass-throughs reach run_leg.py's argv (and only when given); the
    rebased note; a LEG.tsv drives the leg's frames and bounds while a leg without one keeps the lane-derived
    frames and the caller's timeout."""
    import shutil
    import tempfile
    ok = True
    ok &= rebased_note(["mem.mhash: identical (3 lines)"]) == ""
    ok &= rebased_note(["mem.mhash: rebased mem b120 (P9-A, P9-B) in /x: frozen '120,0xo' -> effective '120,0xn' (commit c, proof p)"]) == " rebased=mem b120(P9-A,P9-B)"
    cmd = new_leg_cmd("bin", "coldboot", 600, "/x/out", "/repo", timeout=900)
    ok &= cmd[-2:] == ["--timeout", "900"] and cmd[1].endswith("run_leg.py")
    cmd = new_leg_cmd("bin", "coldboot", 600, "/x/out", "/repo")
    ok &= "--timeout" not in cmd and "--set" not in cmd
    cmd = new_leg_cmd("bin", "coldboot", 600, "/x/out", "/repo", timeout=900,
                      sets=["cdvd.delivery_visibility=poll", "iop.restored_timer_due=at_restore"])
    ok &= cmd[-6:] == ["--timeout", "900", "--set", "cdvd.delivery_visibility=poll",
                       "--set", "iop.restored_timer_due=at_restore"]
    ok &= cmd.count("--set") == 2
    cmd = new_leg_cmd("bin", "coldboot", 4800, "/x/out", "/repo", timeout=3000, stall_s=600)
    ok &= cmd[cmd.index("--timeout") + 1] == "3000" and cmd[cmd.index("--stall-s") + 1] == "600"
    ok &= "--inputs" not in cmd and "paths.mc_dir" not in " ".join(cmd)
    # a `source=new` session leg: --inputs and the staged save directory, the latter after the caller's sets
    cmd = new_leg_cmd("bin", "coldboot", 36000, "/x/out", "/repo", sets=["paths.mc_dir=/elsewhere"],
                      inputs="/t/verify/pads/play_session_1.padlog", mc_dir="/x/out.mc")
    ok &= cmd[cmd.index("--inputs") + 1] == "/t/verify/pads/play_session_1.padlog"
    ok &= cmd[-2:] == ["--set", "paths.mc_dir=/x/out.mc"] and cmd.count("--set") == 2
    # LEG.tsv: frames / timeout / stall come from the spec; absent spec = lane-derived frames, caller's bounds
    tmp = tempfile.mkdtemp(prefix="battery_selftest_")
    for leg in ("plain", "specd"):
        os.makedirs(os.path.join(tmp, leg))
        with open(os.path.join(tmp, leg, "frames.fhash"), "w") as f:
            f.write("".join("%d,0x%016x\n" % (k, k) for k in range(1, 601)))
        with open(os.path.join(tmp, leg, "run.meta"), "w") as f:
            f.write("frames=600\n")
    with open(os.path.join(tmp, "specd", leg_spec.FILE), "w") as f:
        f.write(leg_spec.format_spec(4800, 60, 3000, 600, "coldboot", [(900, 1500)], "test"))
    ok &= leg_frames(tmp, "plain") == 600 and leg_frames(tmp, "specd") == 4800
    ok &= anchor_for("specd", "/repo", tmp) == "coldboot" and anchor_for("plain", "/repo", tmp) == "/repo/save_states/plain.ps2st"
    ok &= anchor_for("coldboot", "/repo", tmp) == "coldboot" and anchor_for("plain", "/repo") == "/repo/save_states/plain.ps2st"
    ok &= leg_bounds(tmp, "plain", 900) == (900, None) and leg_bounds(tmp, "plain", None) == (None, None)
    ok &= leg_bounds(tmp, "specd", 900) == (3000, 600) and leg_bounds(tmp, "specd", 4000) == (4000, 600)
    ok &= baseline_legs(tmp) == ["plain", "specd"]
    ok &= leg_session(tmp, "plain") == (None, None) and leg_session(tmp, "specd") == (None, None)
    # a session leg resolves its padlog against the title dir and its mc_before against the leg dir;
    # stage_mc_dir copies the frozen saves next to the rep output and never touches the original
    os.makedirs(os.path.join(tmp, "session", "mc_before", "mc0"))
    with open(os.path.join(tmp, "session", "mc_before", "mc0", "save.bin"), "w") as f:
        f.write("frozen")
    with open(os.path.join(tmp, "session", "run.meta"), "w") as f:
        f.write("frames=36000\n")
    with open(os.path.join(tmp, "session", leg_spec.FILE), "w") as f:
        f.write(leg_spec.format_spec(36000, 0, 4000, 600, "coldboot", [], "s", pad_script="verify/pads/session.padlog",
                                     mc_dir="mc_before"))
    pad, mc = leg_session(tmp, "session")
    ok &= pad == os.path.join(os.path.dirname(os.path.dirname(tmp)), "verify", "pads", "session.padlog")
    ok &= mc == os.path.join(tmp, "session", "mc_before")
    staged = stage_mc_dir(mc, os.path.join(tmp, "out", "session.rep1"))
    ok &= staged == os.path.join(tmp, "out", "session.rep1.mc") and os.path.isfile(os.path.join(staged, "mc0", "save.bin"))
    with open(os.path.join(staged, "mc0", "save.bin"), "w") as f:
        f.write("replay wrote")
    ok &= open(os.path.join(mc, "mc0", "save.bin")).read() == "frozen"
    ok &= stage_mc_dir(None, "/x") is None
    shutil.rmtree(tmp, ignore_errors=True)
    print("battery selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required="--selftest" not in sys.argv)
    ap.add_argument("--lanes", default="meta")
    ap.add_argument("--reps", type=int, default=1)
    ap.add_argument("--advisory-mem", action="store_true",
                    help="DT-11 retirement: memory differences between reps are reported, not blocking")
    ap.add_argument("--out", default="/tmp/ps2r_battery")
    ap.add_argument("--baseline", default=leg_spec.BASELINE_DIR)
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--legs", help="comma list to restrict (default: every baseline leg)")
    ap.add_argument("--timeout", type=int,
                    help="per-leg wall bound in seconds passed to run_leg.py --timeout for the NEW "
                         "legs (default: run_leg.py's own max(120, 60 + frames/10)); a leg's LEG.tsv "
                         "timeout_s raises it (never lowers); the stall watchdog is run_leg.py's "
                         "unless the leg's LEG.tsv names stall_s")
    ap.add_argument("--set", action="append", metavar="KEY=VAL",
                    help="Config override passed through run_leg.py to the binary as `--set KEY=VAL` "
                         "for every NEW leg (repeatable, order kept; the toggles of DECISIONS_LOG)")
    ap.add_argument("--selftest", action="store_true", help="unit checks of the argv construction")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    lanes = parse_lanes(a.lanes)
    legs = baseline_legs(a.baseline)
    if a.legs:
        want = [x.strip() for x in a.legs.split(",") if x.strip()]
        legs = [l for l in legs if l in want]
    os.makedirs(a.out, exist_ok=True)

    rows = []
    all_ok = True
    for leg in legs:
        frames = leg_frames(a.baseline, leg)
        anchor = anchor_for(leg, a.repo, a.baseline)
        timeout, stall = leg_bounds(a.baseline, leg, a.timeout)
        pad_script, mc_before = leg_session(a.baseline, leg)
        rep_dirs = []
        run_err = None
        for r in range(1, a.reps + 1):
            out_dir = os.path.join(a.out, "%s.rep%d" % (leg, r))
            code, log = run_new_leg(a.binary, anchor, frames, out_dir, a.repo, timeout, a.set, stall,
                                    pad_script, mc_before)
            if code != 0:
                run_err = "run_leg exit=%d: %s" % (code, log.strip().splitlines()[-1] if log.strip() else "")
                break
            rep_dirs.append(out_dir)
        if run_err:
            rows.append((leg, frames, "RUN-FAIL", "-", "-", run_err))
            all_ok = False
            continue
        admissible = "n/a"
        if a.reps > 1:
            admissible = "ADMISSIBLE"
            for d in rep_dirs[1:]:
                code, _ = diff_legs(rep_dirs[0], d, allow_build_diff=False,
                                   lanes=["frames", "audio"] if a.advisory_mem else None)
                if code != 0:
                    admissible = "UNSTABLE"
                    all_ok = False
                if a.advisory_mem:
                    mem_code, _ = diff_legs(rep_dirs[0], d, allow_build_diff=False, lanes=["mem"])
                    if mem_code != 0:
                        print("%s: memory repetition difference (DT-11 advisory)" % leg)
        code, rep = diff_legs(os.path.join(a.baseline, leg), rep_dirs[0],
                              cross_engine=True, lanes=lanes)
        if code == 2:
            cross = "META-FAIL"
            all_ok = False
        elif code == 0:
            cross = "META-OK" if lanes == ["meta"] else "META-OK IDENTICAL(%s)" % ",".join(
                l for l in lanes if l != "meta")
        else:
            div = [l for l in rep if "FIRST DIVERGENT" in l]
            cross = "META-OK DIVERGENT(%s)" % ",".join(l.split(":")[0].split(".")[0] for l in div)
        meta = read_meta(rep_dirs[0])
        fp = "FP-OK" if meta.get("env_ps2") == NEW_DETERMINISTIC_FINGERPRINT else "FP-MISMATCH"
        if fp != "FP-OK":
            all_ok = False
        rows.append((leg, frames, admissible, cross, fp, "frames=%s%s" % (meta.get("frames"), rebased_note(rep))))

    w = max(len(r[0]) for r in rows) if rows else 10
    print("%-*s %6s %-11s %-40s %-11s %s" % (w, "leg", "frames", "reps", "cross-engine (lanes=%s)" % ",".join(lanes), "fingerprint", "note"))
    for r in rows:
        print("%-*s %6d %-11s %-40s %-11s %s" % (w, r[0], r[1], r[2], r[3], r[4], r[5]))
    n_ok = sum(1 for r in rows if r[3].startswith("META-OK"))
    print("battery: META-OK %d/%d legs; %s" % (n_ok, len(rows), "PASS" if all_ok else "FAIL"))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
