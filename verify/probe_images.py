#!/usr/bin/env python3
"""probe_images.py -- the probe images behind the score instrument (DECISIONS_LOG DT-1).

Two modes.

  probe_images.py --old [--out games/<title>/verify/baseline] [--legs all|a,b] [--repo <OLD root>]
                  [--old-binary <OLD root>/build/host_app/host_app]

(Re)produces the FROZEN OLD probe set with pairing proofs: for every anchor leg
`<out>/<leg>/probe/old_b2.ps2st` and `old_b60.ps2st`; for coldboot `old_b60.ps2st`
and `old_b120.ps2st`; each probe dir carries `PROOF.txt` (`--proof-only` rewrites it
from the images on disk without running OLD). OLD's `state_b<N>` is the
memory at boundary N+1 (OLD requests the save at boundary n != 0 and lands it one
boundary later -- I8.5-G's pairing proof), so the OLD legs are
`--frames 4 --state-every 1` -> state_b1 (= boundary 2; the save lands at boundary 2 and the
leg must run one boundary past it, so 4 frames, not 3), `--frames 62 --state-every 59`
-> state_b59 (= 60); coldboot `--frames 121 --state-every 59` -> state_b59 (= 60) and
`--frames 121 --state-every 119` -> state_b119 (= 120). OLD writes no image at boundary
0 or 1; boundary 2 is the first probe. The proof: state_hash.py's chained hash
(RAM0 -> SPAD -> META) of the image equals the frozen baseline `mem.mhash` line of the
same boundary (60 / 120); boundary 2 has no mem.mhash line (the lane writes every 60
boundaries), so its proof is relative to the leg's hash-proved boundary-60 image: OLD
stamps an image with its vsync counter at the boundary the save lands on, so
vsync_frame(old_b2) == vsync_frame(old_b60) - 58.

  probe_images.py --binary <nativeee> --out <dir> [--legs all|a,b] [--set key=val ...]
                  [--repo <OLD root>] [--timeout S] [--baseline games/<title>/verify/baseline]

Produces the NEW probe images for a binary: `<out>/<leg>/new_b2.ps2st`, `new_b60.ps2st`
(anchors: `--frames 3 --state-every 2`, `--frames 61 --state-every 60`); coldboot
`new_b60.ps2st`, `new_b120.ps2st` (`--frames 121 --state-every 60`). NEW labels its
images by the boundary itself (`state_b<N>` = boundary N). `--profile deterministic`
always; `--set` is passed through to the binary (run_leg.py --set).

A leg with a `games/<title>/verify/baseline/<leg>/LEG.tsv` (verify/leg_spec.py; the 13th leg
`coldboot_attract`, DECISIONS_LOG DT-5) takes its probe plan from the file's `probes`
column instead of the built-in plans: every `B[@F]` entry is one probe pair at boundary
B. --old runs ONE OLD leg per probe, `--frames F --state-every B-1` -> `state_b<B-1>`
(= boundary B), and proves it by the baseline `mem.mhash` line B (B must be a multiple of
60; a probe with no line is UNPROVED and fails). NEW runs ONE leg `--frames <frames>
--state-every <state_every>` and keeps the images at the probe boundaries (B must be a
multiple of `state_every`). Both use the spec's `timeout_s` / `stall_s` (run_leg.py
--timeout / --stall-s; a caller's --timeout never lowers the spec's).
With a LEG.tsv `pair_map` (DECISIONS_LOG DT-6; verify/clock_pair.py): the NEW image of probe
B is taken at the MAPPED boundary N = pair_map[B] and written as `new_b<N>.ps2st` (named by
the NEW boundary it holds); mapped boundaries on the `state_every` cadence come from the one
cadence run, every other one from its own run `--frames N+1 --state-every N`.
PER RUN (P8.30-T; verify/scene_pairing.py): with `--pairing run` (the default) the edge is DERIVED from
this binary's own images first -- a census leg `--frames <frozen NEW edge + margin> --state-every 1`
read image by image for the boot-SM word and pruned as it lands -- and every mapped probe is
`B + (run edge - OLD edge)`; the frozen map stays the REFERENCE and the deviation is written to
`<out>/<leg>/PAIRING.tsv` and printed with every mapped image (`scene OK` / `SCENE-MISMATCH` against the
OLD probe's scene words). `--pairing frozen` reproduces the LEG.tsv map as is (S9/S11's semantics).

Exit 0 when every requested image exists (and, in --old mode, every proof holds).
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import state_hash  # noqa: E402
import leg_spec  # noqa: E402
import clock_pair  # noqa: E402
import scene_pairing  # noqa: E402

DEFAULT_REPO = os.environ.get("PS2R_OLD_ROOT", "")  # the frozen OLD tree (read-only); empty = absent
ANCHOR_DIR = "save_states"

# (image label written, OLD run frames, OLD --state-every, OLD state_b label dropped)
OLD_ANCHOR_LEGS = [("old_b2", 4, 1, 1), ("old_b60", 62, 59, 59)]
OLD_COLDBOOT_LEGS = [("old_b60", 121, 59, 59), ("old_b120", 121, 119, 119)]
# (run frames, --state-every, {state_b label: image label written})
NEW_ANCHOR_LEGS = [(3, 2, {2: "new_b2"}), (61, 60, {60: "new_b60"})]
NEW_COLDBOOT_LEGS = [(121, 60, {60: "new_b60", 120: "new_b120"})]


def old_plan(leg, spec):
    """The OLD probe plan of a leg: [(image label, OLD run frames, OLD --state-every, state_b label)].
    A LEG.tsv's `probes` B@F entries give (old_b<B>, F, B-1, B-1); without one the built-in plans."""
    if spec is not None:
        return [("old_b%d" % b, f, b - 1, b - 1) for b, f in spec["probes"]]
    return OLD_COLDBOOT_LEGS if leg == "coldboot" else OLD_ANCHOR_LEGS


def new_plan(leg, spec, offset=None):
    """The NEW probe plan of a leg: [(run frames, --state-every, {state_b label: image label})]. A LEG.tsv
    gives ONE run of the leg's own length at its `state_every`, keeping the images at the probe
    boundaries (NEW labels an image by its boundary); without one the built-in plans. With a `pair_map`
    the kept boundaries are the MAPPED ones (`new_b<N>`): those on the cadence from the cadence run,
    each other one from a dedicated `--frames N+1 --state-every N` run (one image at N). `offset` = the
    RUN's edge offset (scene_pairing.derive): every mapped probe is B + offset instead of the frozen map's."""
    if spec is not None:
        every = spec["state_every"]
        on_cadence, off_cadence = {}, []
        for b, _ in spec["probes"]:
            n = leg_spec.mapped_boundary(spec, b) if offset is None or b not in spec.get("pair_map", {}) else b + offset
            if every and n % every == 0 and n < spec["frames"]:
                on_cadence[n] = "new_b%d" % n
            else:
                off_cadence.append(n)
        plan = [(spec["frames"], every, on_cadence)] if on_cadence else []
        plan += [(n + 1, n, {n: "new_b%d" % n}) for n in sorted(set(off_cadence))]
        return plan
    return NEW_COLDBOOT_LEGS if leg == "coldboot" else NEW_ANCHOR_LEGS


def baseline_legs(baseline_dir):
    legs = []
    for name in sorted(os.listdir(baseline_dir)):
        if os.path.isfile(os.path.join(baseline_dir, name, "run.meta")):
            legs.append(name)
    return legs


def select_legs(all_legs, spec):
    if not spec or spec == "all":
        return list(all_legs)
    want = [x.strip() for x in spec.split(",") if x.strip()]
    missing = [w for w in want if w not in all_legs]
    if missing:
        raise SystemExit("unknown legs: %s (baseline legs: %s)" % (",".join(missing), ",".join(all_legs)))
    return [l for l in all_legs if l in want]


def anchor_for(leg, repo, spec=None):
    """`coldboot` or the leg's savestate: the LEG.tsv `anchor` column when the leg has one, else the name."""
    return leg_spec.anchor_for(spec, leg, repo, ANCHOR_DIR)


def baseline_mem_lines(baseline_dir, leg):
    out = {}
    with open(os.path.join(baseline_dir, leg, "mem.mhash")) as f:
        for line in f:
            line = line.strip()
            if line and "," in line:
                k, h = line.split(",", 1)
                out[int(k)] = h.strip()
    return out


def image_hash(path):
    """(chained FNV hash as `0x…`, header vsync_frame) of a .ps2st image (state_hash's rule)."""
    _, vsync_frame, sections = state_hash.parse(path)
    h = state_hash.FNV_OFFSET
    for t in (state_hash.TAG_RAM, state_hash.TAG_SPAD, state_hash.TAG_META):
        h = state_hash.fnv1a64(sections[t][2], h)
    return "0x%016x" % h, vsync_frame


def run_leg_cmd(binary, anchor, frames, out_dir, repo, tree, state_every, timeout=None, sets=(), stall_s=None):
    cmd = [sys.executable, os.path.join(HERE, "run_leg.py"), "--binary", binary, "--anchor", anchor,
           "--frames", str(frames), "--out", out_dir, "--tree", tree, "--repo", repo,
           "--state-every", str(state_every)]
    if timeout is not None:
        cmd += ["--timeout", str(int(timeout))]
    if stall_s is not None:
        cmd += ["--stall-s", str(int(stall_s))]
    for kv in sets:
        cmd += ["--set", kv]
    return cmd


def run_leg(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return proc.returncode, proc.stdout


def move_image(leg_dir, label_n, dest):
    src = os.path.join(leg_dir, "state_b%d.ps2st" % label_n)
    if not os.path.isfile(src):
        return False
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.move(src, dest)
    return True


def drop_leg_dir(leg_dir):
    """Delete a transient leg directory and its runner log (images are large)."""
    shutil.rmtree(leg_dir, ignore_errors=True)
    try:
        os.remove(leg_dir + ".log")
    except OSError:
        pass


def prove(label, h, vf, mem, sibling_vf):
    """(ok, text) -- the pairing proof of one OLD probe image. A boundary with a baseline
    mem.mhash line (60, 120) is proved by hash equality. Boundary 2 has no line (the lane
    writes every 60 boundaries): it is proved against the hash-proved boundary-60 image of
    the same leg -- OLD stamps an image with the vsync counter at the boundary it lands on
    (boundary + 1 past the anchor's own frame), so vf(b2) == vf(b60) - 58."""
    boundary = int(label.split("_b")[1])
    if boundary in mem:
        ok = mem[boundary] == h
        return ok, "baseline mem.mhash line `%d,%s` -> %s" % (boundary, mem[boundary],
                                                                "PROOF-OK" if ok else "PROOF-FAIL")
    if sibling_vf is not None:
        ok = vf == sibling_vf - (60 - boundary)
        return ok, ("no baseline mem.mhash line at boundary %d (lane writes every 60); header vsync_frame %d "
                    "== hash-proved old_b60 vsync_frame %d - %d -> %s"
                    % (boundary, vf, sibling_vf, 60 - boundary, "PROOF-OK" if ok else "PROOF-FAIL"))
    return False, "no baseline line and no proved sibling image -> UNPROVED"


def produce_old(a, legs):
    rc = 0
    for leg in legs:
        probe_dir = os.path.join(a.out, leg, "probe")
        os.makedirs(probe_dir, exist_ok=True)
        spec = leg_spec.read_leg(a.baseline, leg)
        plan = old_plan(leg, spec)
        timeout, stall = leg_spec.effective_timeout(spec, None), leg_spec.stall_s(spec)
        anchor = anchor_for(leg, a.repo, spec)
        mem = baseline_mem_lines(a.baseline, leg)
        anchor_vf = None if anchor == "coldboot" else image_hash(anchor)[1]
        proof = ["# PROOF for the frozen OLD probe images of leg %s (verify/probe_images.py --old)" % leg,
                 "# OLD state_b<N> = boundary N+1 (OLD requests the save at boundary n != 0, lands it one boundary later;",
                 "#   OLD host_app/verify/verify_boundary.cpp OnEeBoundary, proved I8.5-G). NEW state_b<N> = boundary N.",
                 "# hash = verify/state_hash.py chained FNV-1a64 over RAM0 -> SPAD -> META (= the mem.mhash lane's hash).",
                 "# anchor: %s%s" % (anchor, "" if anchor_vf is None else " (header vsync_frame %d)" % anchor_vf)]
        if spec is not None:
            proof.append("# plan from %s: probes %s (B@F: OLD --frames F --state-every B-1 -> state_b<B-1> = boundary B); "
                         "timeout_s %s stall_s %s" % (leg_spec.FILE, ",".join("%d@%d" % p for p in spec["probes"]), timeout, stall))
        # the hash-proved boundary-60 image first: boundary 2's proof is relative to it
        ordered = sorted(plan, key=lambda e: 0 if e[0] == "old_b60" else 1)
        sibling_vf = None
        for label, frames, every, state_n in ordered:
            dest = os.path.join(probe_dir, label + ".ps2st")
            if a.proof_only:
                if not os.path.isfile(dest):
                    proof.append("%s: MISSING (no image on disk; run without --proof-only)" % label)
                    rc = 1
                    continue
            else:
                work = os.path.join(a.work, "old_%s_%s" % (leg, label))
                drop_leg_dir(work)
                code, log = run_leg(run_leg_cmd(a.old_binary, anchor, frames, work, a.repo, "legacy", every,
                                                timeout, (), stall))
                tail = log.strip().splitlines()[-1] if log.strip() else ""
                moved = move_image(work, state_n, dest)
                drop_leg_dir(work)
                if code != 0 or not moved:
                    proof.append("%s: MISSING (OLD leg --frames %d --state-every %d exit=%d: %s)"
                                 % (label, frames, every, code, tail))
                    print("%s %s: MISSING (exit=%d %s)" % (leg, label, code, tail))
                    rc = 1
                    continue
            h, vf = image_hash(dest)
            ok, why = prove(label, h, vf, mem, sibling_vf)
            if label == "old_b60" and ok:
                sibling_vf = vf
            line = "%s: OLD --frames %d --state-every %d -> state_b%d; hash %s vsync_frame %d; %s" % (
                label, frames, every, state_n, h, vf, why)
            if not ok:
                rc = 1
            proof.append(line)
            print("%s %s" % (leg, line))
        with open(os.path.join(probe_dir, "PROOF.txt"), "w") as f:
            f.write("\n".join(proof) + "\n")
    return rc


def scene_keys_for(baseline_dir, leg):
    path = os.path.join(baseline_dir, leg, "SCENE_KEYS.tsv")
    return clock_pair.read_keys(path) if os.path.isfile(path) else None


def one_image_case(a, leg, anchor, timeout, stall, keys):
    """image_case(N): a one-image leg `--frames N+1 --state-every N`, the SM case of its image (None when missing)."""
    def image_case(n):
        work = os.path.join(a.work, "edge2_%s_b%d" % (leg, n))
        drop_leg_dir(work)
        run_leg(run_leg_cmd(a.binary, anchor, n + 1, work, a.repo, "new", n, timeout, a.set or (), stall))
        import memory_capture
        try:
            case = clock_pair.words(memory_capture.path(work, n), keys)["case"]
        except (FileNotFoundError, ValueError):
            case = None
        drop_leg_dir(work)
        return case
    return image_case


def produce_new(a, legs):
    rc = 0
    for leg in legs:
        dest_dir = os.path.join(a.out, leg)
        os.makedirs(dest_dir, exist_ok=True)
        spec = leg_spec.read_leg(a.baseline, leg)
        timeout, stall = leg_spec.effective_timeout(spec, a.timeout), leg_spec.stall_s(spec)
        anchor = anchor_for(leg, a.repo, spec)
        offset, pairing, keys = None, None, None
        if spec is not None and spec.get("pair_edge") is not None and a.pairing == "run":
            # DT-6 PER RUN: the edge from this binary's own images, the frozen map kept as the reference
            keys = scene_keys_for(a.baseline, leg)
            if keys is None:
                raise SystemExit("%s: a pair_map needs %s/SCENE_KEYS.tsv for the per-run edge" % (leg, leg))

            def argv(frames, every, work, _anchor=anchor, _timeout=timeout, _stall=stall):
                return run_leg_cmd(a.binary, _anchor, frames, work, a.repo, "new", every, _timeout, a.set or (), _stall)
            pairing = scene_pairing.derive(spec, keys, argv, a.work, margin=a.edge_margin, log=print,
                                           image_case=one_image_case(a, leg, anchor, timeout, stall, keys), verify_edge2=a.verify_edge2)
            scene_pairing.write(dest_dir, pairing)
            print("%s pairing: %s" % (leg, scene_pairing.note(spec, pairing, spec["probes"][0][0]) if spec["probes"] else pairing["status"]))
            if pairing["status"] != "EDGE-FOUND":
                print("%s: EDGE-NOT-FOUND -> no mapped image produced (the pairs count as MISSING)" % leg)
                rc = 1
                continue
            offset = pairing["offset"]
        plan = new_plan(leg, spec, offset)
        for frames, every, labels in plan:
            work = os.path.join(a.work, "new_%s_f%d" % (leg, frames))
            drop_leg_dir(work)
            code, log = run_leg(run_leg_cmd(a.binary, anchor, frames, work, a.repo, "new", every,
                                            timeout, a.set or (), stall))
            tail = log.strip().splitlines()[-1] if log.strip() else ""
            for state_n, label in sorted(labels.items()):
                dest = os.path.join(dest_dir, label + ".ps2st")
                if move_image(work, state_n, dest):
                    h, vf = image_hash(dest)
                    scene = ""
                    if offset is not None and keys is not None:
                        # the mapped image's scene words against the OLD probe's (clock_pair's check), REPORTED
                        old_b = state_n - offset
                        old_img = os.path.join(a.baseline, leg, "probe", "old_b%d.ps2st" % old_b)
                        if os.path.isfile(old_img):
                            wo, wn = clock_pair.words(old_img, keys), clock_pair.words(dest, keys)
                            same = (wo["case"], wo["bit63"]) == (wn["case"], wn["bit63"])
                            scene = "; old_b%d %s vs new %s -> %s" % (old_b, clock_pair.scene_text(wo), clock_pair.scene_text(wn),
                                                                     "scene OK" if same else "SCENE-MISMATCH")
                    print("%s %s: NEW --frames %d --state-every %d -> state_b%d; hash %s vsync_frame %d%s"
                          % (leg, label, frames, every, state_n, h, vf, scene))
                else:
                    print("%s %s: MISSING (NEW leg --frames %d --state-every %d exit=%d: %s)"
                          % (leg, label, frames, every, code, tail))
                    rc = 1
            # keep the leg's small artifacts (run.meta, lanes) next to the images for the record
            for name in ("run.meta", "frames.fhash", "audio.ahash", "mem.mhash"):
                src = os.path.join(work, name)
                if os.path.isfile(src):
                    shutil.copyfile(src, os.path.join(dest_dir, "f%d_%s" % (frames, name)))
            drop_leg_dir(work)
    return rc


def selftest():
    """The probe plans: built-in for a leg without LEG.tsv (anchors b2/b60, coldboot b60/b120); from the
    spec's `probes` for a leg with one (OLD one run per probe at --state-every B-1, NEW one run at the leg's
    cadence); the argv carries --timeout / --stall-s only when given."""
    ok = True
    ok &= old_plan("artdet_f37152", None) == OLD_ANCHOR_LEGS and old_plan("coldboot", None) == OLD_COLDBOOT_LEGS
    ok &= new_plan("artdet_f37152", None) == NEW_ANCHOR_LEGS and new_plan("coldboot", None) == NEW_COLDBOOT_LEGS
    spec = leg_spec.parse("\t".join(leg_spec.COLUMNS) + "\n4800\t60\t3000\t600\tcoldboot\t900@1500,3780@4400\tx\n")
    ok &= old_plan("coldboot_attract", spec) == [("old_b900", 1500, 899, 899), ("old_b3780", 4400, 3779, 3779)]
    ok &= anchor_for("coldboot_attract", "/repo", spec) == "coldboot" and anchor_for("coldboot", "/repo") == "coldboot"
    ok &= anchor_for("artdet_f37152", "/repo") == "/repo/save_states/artdet_f37152.ps2st"
    ok &= new_plan("coldboot_attract", spec) == [(4800, 60, {900: "new_b900", 3780: "new_b3780"})]
    # a pair_map (DT-6): the mapped boundaries are kept -- on-cadence ones from the cadence run, the others each
    # from a dedicated one-image run; the image is named by the NEW boundary it holds
    spec_m = leg_spec.parse("\t".join(leg_spec.COLUMNS + leg_spec.OPTIONAL_COLUMNS)
                            + "\n4800\t60\t3000\t600\tcoldboot\t900@1500,3780@4400\tx\told_b850=new_b1133,old_b900=new_b1183,old_b3780=new_b4080\t530\n")
    ok &= new_plan("coldboot_attract", spec_m) == [(4800, 60, {4080: "new_b4080"}), (1184, 1183, {1183: "new_b1183"})]
    # the RUN's offset (scene_pairing.derive) replaces the frozen map's on every mapped probe: edge 1049 -> offset +199
    ok &= new_plan("coldboot_attract", spec_m, offset=199) == [(1100, 1099, {1099: "new_b1099"}), (3980, 3979, {3979: "new_b3979"})]
    ok &= new_plan("coldboot_attract", spec_m, offset=0) == [(4800, 60, {900: "new_b900", 3780: "new_b3780"})]
    ok &= new_plan("coldboot", None, offset=199) == NEW_COLDBOOT_LEGS   # a leg without a map ignores the offset
    ok &= old_plan("coldboot_attract", spec_m) == [("old_b900", 1500, 899, 899), ("old_b3780", 4400, 3779, 3779)]
    cmd = run_leg_cmd("bin", "coldboot", 1500, "/w", "/repo", "legacy", 899, 3000, (), 600)
    ok &= cmd[cmd.index("--state-every") + 1] == "899" and cmd[cmd.index("--timeout") + 1] == "3000" \
        and cmd[cmd.index("--stall-s") + 1] == "600" and "--set" not in cmd
    cmd = run_leg_cmd("bin", "coldboot", 121, "/w", "/repo", "legacy", 59)
    ok &= "--timeout" not in cmd and "--stall-s" not in cmd
    mem = {900: "0xaa", 3780: "0xbb"}
    ok &= prove("old_b900", "0xaa", 1431, mem, None)[0] and not prove("old_b3780", "0xcc", 0, mem, None)[0]
    ok &= not prove("old_b3810", "0xcc", 0, mem, None)[0]  # no mem line, no sibling -> UNPROVED
    print("probe_images selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", action="store_true", help="(re)produce the frozen OLD probe set with proofs")
    ap.add_argument("--binary", help="NEW nativeee binary (NEW mode)")
    ap.add_argument("--old-binary", default=os.path.join(DEFAULT_REPO, "build", "host_app", "host_app"))
    ap.add_argument("--out", help="NEW mode: image dir; --old: baseline root (default games/<title>/verify/baseline)")
    ap.add_argument("--legs", default="all")
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--baseline", default=leg_spec.BASELINE_DIR)
    ap.add_argument("--timeout", type=int, help="NEW legs: run_leg.py --timeout (a leg's LEG.tsv timeout_s raises it)")
    ap.add_argument("--set", action="append", help="NEW legs: key=val passed to the binary (repeatable)")
    ap.add_argument("--work", help="transient leg dir (default <out>/.legs)")
    ap.add_argument("--proof-only", action="store_true",
                    help="--old: rewrite PROOF.txt from the images on disk without running OLD")
    ap.add_argument("--pairing", choices=("run", "frozen"), default="run",
                    help="NEW legs with a LEG.tsv pair_map: `run` (default) derives the scene edge from this run's own "
                         "images (scene_pairing.py; PAIRING.tsv written next to the images), `frozen` uses the LEG.tsv map as is")
    ap.add_argument("--edge-margin", type=int, default=scene_pairing.DEFAULT_MARGIN,
                    help="--pairing run: the census leg runs to the frozen NEW edge + this many boundaries (one retry at 4x)")
    ap.add_argument("--verify-edge2", action="store_true",
                    help="--pairing run: check the predicted pass-2 edge with two one-image legs (case 6 / case 7)")
    a = ap.parse_args()
    if a.old == bool(a.binary):
        ap.error("exactly one of --old / --binary")
    if a.old:
        a.out = a.out or a.baseline
    elif not a.out:
        ap.error("--out is required with --binary")
    a.work = a.work or os.path.join(a.out, ".legs")
    os.makedirs(a.work, exist_ok=True)
    legs = select_legs(baseline_legs(a.baseline), a.legs)
    rc = produce_old(a, legs) if a.old else produce_new(a, legs)
    for stale in glob.glob(os.path.join(a.work, "*")):
        if os.path.isdir(stale):
            drop_leg_dir(stale)
    print("probe_images: %s" % ("OK" if rc == 0 else "INCOMPLETE"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
