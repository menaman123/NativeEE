#!/usr/bin/env python3
"""freeze_session_leg.py -- freeze a NEW-recorded play session as a `source=new` regression leg.

The two user play sessions (docs/design/PLAY_SESSIONS.md) are recorded on the interactive
Release build with `--record-input <file>`. A recording is not a leg: the frozen lanes of a
`source=new` leg are produced by REPLAYING the recorded padlog headless on the deterministic
profile from a cold boot, N reps, and freezing rep 1 only when every rep is identical on the
frames and audio lanes (memory is advisory under DT-11). The leg then joins the battery like
the thirteen: `battery.py` passes its `pad_script` as `--inputs` and stages a fresh copy of its
`mc_before` game-save directory for every rep (verify/leg_spec.py session columns).

  freeze_session_leg.py --binary <Release nativeee> --padlog <session .padlog> --leg <name>
                        [--mc-before <dir>] [--frames N] [--reps 3] [--work <dir>]
                        [--timeout S] [--stall-s S] [--notes TEXT] [--repo OLD] [--game-files DIR]
                        [--dry-run]
  freeze_session_leg.py --selftest

What it writes on acceptance (nothing on refusal; existing frozen files are never edited):
  <baseline>/<leg>/{frames.fhash,audio.ahash,mem.mhash,run.meta}   rep 1's lanes
  <baseline>/<leg>/LEG.tsv        frames / bounds / anchor coldboot / pad_script / mc_dir
  <baseline>/<leg>/mc_before/     the game saves the session started from (when --mc-before)
  <baseline>/<leg>/FREEZE.md      command, binary digest, build id, rep digests, provenance
  <title>/verify/pads/<leg>.padlog   the replayed script (byte copy of the recording)
  <title>/verify/anchors.tsv      one appended row: kind `session`, pad_script, source `new`

Refusals (exit 3): leg already frozen; padlog missing/unreadable or with no range line; any rep
fails or differs from rep 1 on frames/audio; the recording's last tick exceeds the leg length.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import leg_spec  # noqa: E402
from diff_legs import diff_legs, read_meta  # noqa: E402
from battery import DEFAULT_REPO, new_leg_cmd, stage_mc_dir  # noqa: E402

KIND = "session"


class FreezeRefusal(RuntimeError):
    """A condition under which nothing is written."""


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_padlog(path):
    """(header_line, range_lines, last_tick): the recorder's `#` header, its range tokens and the
    largest tick named by a range (`<from>-<to>:<buttons>...` / `<tick>:...`). Comment-only or
    empty files are refused -- a session without one served input is not a play session."""
    if not os.path.isfile(path):
        raise FreezeRefusal("padlog %s does not exist" % path)
    header, ranges, last = None, [], 0
    with open(path) as f:
        for raw in f:
            line = raw.rstrip("\n")
            if not line.strip():
                continue
            if line.lstrip().startswith("#"):
                if header is None:
                    header = line
                continue
            ranges.append(line)
            head = line.split(":", 1)[0].strip()
            for tok in head.replace("-", ",").split(","):
                tok = tok.strip()
                if tok.isdigit():
                    last = max(last, int(tok))
    if not ranges:
        raise FreezeRefusal("padlog %s has no input range line" % path)
    return header, ranges, last


def leg_length(requested, session_meta, last_tick):
    """The leg's `--frames`: the caller's, else the session run.meta `frames=`; it must cover the
    recording's last tick (every recorded sample is replayed) -- a shorter length is a refusal."""
    frames = int(requested) if requested else int((session_meta or {}).get("frames", "0") or 0)
    if frames <= 0:
        raise FreezeRefusal("no leg length: pass --frames or a session run.meta with frames=")
    if last_tick > frames:
        raise FreezeRefusal("recording's last tick %d exceeds the leg length %d; pass --frames >= %d"
                            % (last_tick, frames, last_tick))
    return frames


def run_reps(binary, frames, padlog, mc_before, reps, work, repo, timeout, stall_s, game_files, runner=None):
    """Run the replay reps through run_leg.py (headless, deterministic profile, coldboot). Returns the rep
    directories; a failed run refuses. `runner` is the subprocess entry point (replaced by the selftest)."""
    rep_dirs = []
    for r in range(1, reps + 1):
        out_dir = os.path.join(work, "rep%d" % r)
        cmd = new_leg_cmd(binary, "coldboot", frames, out_dir, repo, timeout, None, stall_s, os.path.abspath(padlog),
                          stage_mc_dir(mc_before, out_dir))
        if game_files:
            cmd += ["--game-files", game_files]
        proc = (runner or subprocess.run)(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if proc.returncode != 0:
            tail = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else ""
            raise FreezeRefusal("rep %d failed: run_leg exit=%d: %s" % (r, proc.returncode, tail))
        rep_dirs.append(out_dir)
    return rep_dirs


def check_stable(rep_dirs):
    """Every rep identical to rep 1 on frames and audio (exit 0 of diff_legs); memory differences are
    reported as advisory. Returns the advisory notes."""
    notes = []
    for d in rep_dirs[1:]:
        code, rep = diff_legs(rep_dirs[0], d, allow_build_diff=False, lanes=["frames", "audio"])
        if code != 0:
            first = [l for l in rep if "FIRST DIVERGENT" in l]
            raise FreezeRefusal("reps differ (%s vs %s): %s" % (rep_dirs[0], d, "; ".join(first) or "see diff_legs"))
        mem_code, _ = diff_legs(rep_dirs[0], d, allow_build_diff=False, lanes=["mem"])
        if mem_code != 0:
            notes.append("memory lane differs between %s and %s (DT-11 advisory)" % (os.path.basename(rep_dirs[0]), os.path.basename(d)))
    for d in rep_dirs:
        meta = read_meta(d)
        if meta.get("finalized") != "1":
            raise FreezeRefusal("%s did not finalize" % d)
    return notes


def freeze_text(leg, frames, reps, rep_dirs, binary, binary_sha, padlog, padlog_sha, header, ranges, last_tick,
                mc_before, notes, advisory, cmd_text):
    meta = read_meta(rep_dirs[0])
    lines = ["# FREEZE -- `%s` (a `source=new` session leg; docs/design/PLAY_SESSIONS.md)" % leg, "",
             "Recorded on NEW by the user with `--record-input`; frozen by replaying that recording headless on the",
             "deterministic profile from a cold boot, %d reps, rep 1's lanes kept. The interactive session's own lanes" % reps,
             "are NOT the frozen lanes (the play profile is not the deterministic profile).", "",
             "## Replay command (each rep; `verify/freeze_session_leg.py` ran it through `run_leg.py --tree new`)", "",
             "```", cmd_text, "```", "",
             "| item | value |", "|---|---|",
             "| binary | `%s` |" % binary, "| binary sha256 | `%s` |" % binary_sha,
             "| build_id | `%s` |" % meta.get("build_id", "?"), "| env_hash | `%s` |" % meta.get("env_hash", "?"),
             "| profile | `%s` |" % meta.get("profile", "?"),
             "| frames | %d (`--frames`; run.meta frames=%s) |" % (frames, meta.get("frames", "?")),
             "| padlog | `verify/pads/%s.padlog` (copied from `%s`) |" % (leg, padlog),
             "| padlog sha256 | `%s` |" % padlog_sha,
             "| padlog header | `%s` |" % (header or "-"),
             "| padlog range lines / last tick | %d / %d |" % (len(ranges), last_tick),
             "| mc_before | %s |" % ("`mc_before/` (copied from `%s`)" % mc_before if mc_before else "none (default save directory)"),
             "", "## The reps", "", "| rep | frames.fhash | audio.ahash | mem.mhash |", "|---|---|---|---|"]
    for i, d in enumerate(rep_dirs, 1):
        lines.append("| %d | `%s` | `%s` | `%s` |" % (i, sha256_file(os.path.join(d, "frames.fhash"))[:16],
                                                      sha256_file(os.path.join(d, "audio.ahash"))[:16],
                                                      sha256_file(os.path.join(d, "mem.mhash"))[:16]))
    lines += ["", "frames and audio: IDENTICAL across the %d reps (`diff_legs.py`, the freeze refuses otherwise)." % reps]
    for n in advisory:
        lines.append("- " + n)
    lines += ["", "## Notes", "", notes or "-", "",
              "This leg is a replay oracle for NEW. It is not a comparison against the original hardware or PCSX2.",
              "The user's actual play session is the provenance; its reproduction here depends on the title being",
              "deterministic given the recorded input ticks and the frozen save directory."]
    return "\n".join(lines) + "\n"


def write_leg(baseline, title_dir, leg, frames, timeout, stall_s, rep_dirs, padlog, mc_before, freeze_md, notes):
    leg_dir = os.path.join(baseline, leg)
    pads_dir = os.path.join(title_dir, "verify", "pads")
    os.makedirs(leg_dir)
    os.makedirs(pads_dir, exist_ok=True)
    for name in ("frames.fhash", "audio.ahash", "mem.mhash", "run.meta"):
        shutil.copyfile(os.path.join(rep_dirs[0], name), os.path.join(leg_dir, name))
    pad_rel = "verify/pads/%s.padlog" % leg
    shutil.copyfile(padlog, os.path.join(title_dir, pad_rel))
    mc_rel = None
    if mc_before:
        shutil.copytree(mc_before, os.path.join(leg_dir, "mc_before"))
        mc_rel = "mc_before"
    with open(os.path.join(leg_dir, leg_spec.FILE), "w") as f:
        f.write(leg_spec.format_spec(frames, 0, timeout or 0, stall_s or 0, "coldboot", [], notes or "user play session",
                                     pad_script=pad_rel, mc_dir=mc_rel))
    with open(os.path.join(leg_dir, "FREEZE.md"), "w") as f:
        f.write(freeze_md)
    anchors = os.path.join(title_dir, "verify", "anchors.tsv")
    with open(anchors, "a") as f:
        f.write("%s\t%s\tcoldboot\t%d\t-\tbaseline/%s/probe\t%s.padlog\tnew\n" % (leg, KIND, frames, leg, leg))
    return leg_dir


def freeze(a, runner=None):
    baseline = a.baseline
    title_dir = os.path.dirname(os.path.dirname(baseline))
    if os.path.exists(os.path.join(baseline, a.leg)):
        raise FreezeRefusal("leg %s is already frozen under %s" % (a.leg, baseline))
    if not a.leg.replace("_", "").isalnum():
        raise FreezeRefusal("leg name %r: letters, digits and underscores only" % a.leg)
    header, ranges, last_tick = read_padlog(a.padlog)
    session_dir = os.path.dirname(os.path.abspath(a.padlog))
    session_meta = read_meta(session_dir) if os.path.isfile(os.path.join(session_dir, "run.meta")) else {}
    frames = leg_length(a.frames, session_meta, last_tick)
    if a.mc_before and not os.path.isdir(a.mc_before):
        raise FreezeRefusal("--mc-before %s is not a directory" % a.mc_before)
    work = a.work or tempfile.mkdtemp(prefix="freeze_%s_" % a.leg)
    os.makedirs(work, exist_ok=True)
    if a.dry_run:
        cmd = new_leg_cmd(a.binary, "coldboot", frames, os.path.join(work, "rep1"), a.repo, a.timeout, None, a.stall_s,
                          os.path.abspath(a.padlog), (os.path.join(work, "rep1.mc") if a.mc_before else None))
        print("dry run: would run %d reps of: %s" % (a.reps, " ".join(cmd)))
        print("dry run: leg %s, frames %d, last tick %d, %d range lines" % (a.leg, frames, last_tick, len(ranges)))
        return 0
    rep_dirs = run_reps(a.binary, frames, a.padlog, a.mc_before, a.reps, work, a.repo, a.timeout, a.stall_s,
                        a.game_files, runner)
    advisory = check_stable(rep_dirs)
    cmd_text = " ".join(new_leg_cmd(a.binary, "coldboot", frames, "<rep>", a.repo, a.timeout, None, a.stall_s,
                                    os.path.abspath(a.padlog), ("<rep>.mc" if a.mc_before else None)))
    freeze_md = freeze_text(a.leg, frames, a.reps, rep_dirs, a.binary, sha256_file(a.binary) if os.path.isfile(a.binary) else "-",
                            a.padlog, sha256_file(a.padlog), header, ranges, last_tick, a.mc_before, a.notes, advisory, cmd_text)
    leg_dir = write_leg(baseline, title_dir, a.leg, frames, a.timeout, a.stall_s, rep_dirs, a.padlog, a.mc_before,
                        freeze_md, a.notes)
    print("frozen %s: %d frames, %d reps identical (frames, audio); work %s" % (leg_dir, frames, a.reps, work))
    for n in advisory:
        print("  " + n)
    return 0


def selftest():
    """Pure checks plus a stubbed freeze: the runner writes synthetic lanes; the freeze refuses a differing rep,
    an overlong recording and an existing leg, and writes exactly the leg contract on acceptance."""
    ok = True

    def c(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    tmp = tempfile.mkdtemp(prefix="freeze_selftest_")
    title = os.path.join(tmp, "games", "t")
    baseline = os.path.join(title, "verify", "baseline")
    os.makedirs(baseline)
    with open(os.path.join(title, "verify", "anchors.tsv"), "w") as f:
        f.write("name\tkind\tanchor\tframes\tprobes\tprobe_dir\tpad_script\tsource\n")
    session = os.path.join(tmp, "session-1")
    os.makedirs(session)
    padlog = os.path.join(session, "session-1.padlog")
    with open(padlog, "w") as f:
        f.write("# ps2 pad script tick=12 title=T\n12-40:X\n41-77:\n78:O,lx=200\n")
    with open(os.path.join(session, "run.meta"), "w") as f:
        f.write("frames=100\nfinalized=1\n")
    h, r, last = read_padlog(padlog)
    c(h.startswith("#") and len(r) == 3 and last == 78, "padlog header, range lines and last tick read -> %d" % last)
    c(leg_length(None, {"frames": "100"}, 78) == 100 and leg_length(200, {}, 78) == 200, "leg length from --frames or run.meta")
    for args in ((None, {}, 78), (50, {}, 78)):
        try:
            leg_length(*args)
            c(False, "refusal expected for %r" % (args,))
        except FreezeRefusal:
            c(True, "no length / length below the last tick -> refusal")
    try:
        read_padlog(os.path.join(session, "empty.padlog"))
        c(False, "missing padlog accepted")
    except FreezeRefusal:
        c(True, "missing padlog -> refusal")
    mc_before = os.path.join(tmp, "saves_before")
    os.makedirs(os.path.join(mc_before, "mc0"))
    with open(os.path.join(mc_before, "mc0", "slot0.bin"), "w") as f:
        f.write("save")

    def fake_runner_factory(diverge_rep=None):
        def run(cmd, **_):
            out = cmd[cmd.index("--out") + 1]
            os.makedirs(out, exist_ok=True)
            frames = int(cmd[cmd.index("--frames") + 1])
            rep_n = int(os.path.basename(out)[3:])
            c("--inputs" in cmd and cmd[cmd.index("--inputs") + 1] == os.path.abspath(padlog), "rep %d replays the padlog" % rep_n)
            mc = cmd[cmd.index("--set") + 1] if "--set" in cmd else ""
            c(mc == "paths.mc_dir=" + out + ".mc" and os.path.isfile(os.path.join(out + ".mc", "mc0", "slot0.bin")),
              "rep %d gets its own staged save directory" % rep_n)
            with open(os.path.join(out, "frames.fhash"), "w") as f:
                for k in range(1, frames + 1):
                    f.write("%d,0x%016x\n" % (k, k * 7 + (1 if (diverge_rep == rep_n and k == 50) else 0)))
            with open(os.path.join(out, "audio.ahash"), "w") as f:
                for w in range(frames // 6):
                    f.write("%d,0x%016x\n" % (w, w * 11))
            with open(os.path.join(out, "mem.mhash"), "w") as f:
                for b in range(0, frames, 60):
                    f.write("%d,0x%016x\n" % (b, b * 13 + rep_n))   # mem differs per rep: advisory only
            with open(os.path.join(out, "run.meta"), "w") as f:
                f.write("build_id=B\ngl_renderer=none\ntree=new\nprofile=deterministic\nenv_ps2=E\nenv_hash=0x1\n"
                        "frames=%d\nwall_ms=1\nfinalized=1\n" % frames)

            class P:
                returncode = 0
                stdout = ""
            return P()
        return run

    class Args:
        pass
    a = Args()
    a.baseline, a.binary, a.padlog, a.leg, a.mc_before = baseline, os.path.join(tmp, "nobinary"), padlog, "play_session_1", mc_before
    a.frames, a.reps, a.work, a.timeout, a.stall_s, a.notes, a.repo, a.game_files, a.dry_run = None, 2, os.path.join(tmp, "w1"), 900, 120, "first session", "/repo", None, False
    try:
        freeze(a, runner=fake_runner_factory(diverge_rep=2))
        c(False, "a differing rep was accepted")
    except FreezeRefusal as e:
        c("reps differ" in str(e) and not os.path.exists(os.path.join(baseline, a.leg)), "a differing rep refuses and writes nothing")
    a.work = os.path.join(tmp, "w2")
    c(freeze(a, runner=fake_runner_factory()) == 0, "identical reps freeze")
    leg_dir = os.path.join(baseline, a.leg)
    spec = leg_spec.read_leg(baseline, a.leg)
    c(spec["frames"] == 100 and spec["anchor"] == "coldboot" and spec["pad_script"] == "verify/pads/play_session_1.padlog"
      and spec["mc_dir"] == "mc_before" and spec["timeout_s"] == 900 and spec["stall_s"] == 120,
      "LEG.tsv carries frames, coldboot, pad_script, mc_dir and the bounds")
    c(all(os.path.isfile(os.path.join(leg_dir, n)) for n in ("frames.fhash", "audio.ahash", "mem.mhash", "run.meta", "FREEZE.md"))
      and os.path.isfile(os.path.join(leg_dir, "mc_before", "mc0", "slot0.bin")), "lanes, FREEZE.md and mc_before written")
    c(open(os.path.join(title, "verify", "pads", "play_session_1.padlog")).read() == open(padlog).read(), "padlog copied byte for byte")
    rows = [l for l in open(os.path.join(title, "verify", "anchors.tsv")) if not l.startswith("name")]
    c(rows == ["play_session_1\tsession\tcoldboot\t100\t-\tbaseline/play_session_1/probe\tplay_session_1.padlog\tnew\n"],
      "anchors.tsv row appended -> %r" % rows)
    md = open(os.path.join(leg_dir, "FREEZE.md")).read()
    c("advisory" in md and "IDENTICAL across the 2 reps" in md and "last tick | 3 / 78" in md, "FREEZE.md records stability, advisory memory and the recording")
    import anchors_table
    fails = anchors_table.check(baseline, anchors_table.read_anchors(os.path.join(title, "verify", "anchors.tsv")))
    c(fails == [], "anchors_table accepts the frozen session row -> %r" % fails)
    try:
        freeze(a, runner=fake_runner_factory())
        c(False, "refreezing an existing leg was accepted")
    except FreezeRefusal:
        c(True, "an existing leg refuses")
    shutil.rmtree(tmp, ignore_errors=True)
    print("freeze_session_leg selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", help="the Release nativeee that replays the recording")
    ap.add_argument("--padlog", help="the session's recording (`--record-input <file>`)")
    ap.add_argument("--leg", help="the new leg's name (letters, digits, underscores)")
    ap.add_argument("--mc-before", help="the game-save directory as it was when the session STARTED")
    ap.add_argument("--frames", type=int, help="leg length (default: the session run.meta frames=)")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--work", help="rep output directory (default: a temporary directory)")
    ap.add_argument("--timeout", type=int, help="run_leg.py --timeout for each rep")
    ap.add_argument("--stall-s", type=int, help="run_leg.py --stall-s for each rep")
    ap.add_argument("--notes", default="", help="LEG.tsv notes / FREEZE.md provenance")
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--game-files", help="title files directory passed to run_leg.py")
    ap.add_argument("--baseline", default=leg_spec.BASELINE_DIR)
    ap.add_argument("--dry-run", action="store_true", help="print the rep command; write nothing")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    for req in ("binary", "padlog", "leg"):
        if not getattr(a, req):
            ap.error("--%s is required" % req)
    try:
        return freeze(a)
    except FreezeRefusal as e:
        print("freeze_session_leg: REFUSED: %s" % e, file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
