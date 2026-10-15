#!/usr/bin/env python3
"""anchors_table.py -- the title's leg index `games/<t>/verify/anchors.tsv` cross-checked against the frozen
baseline tree (P8.36-D). The runners enumerate `<baseline>/<leg>/run.meta` directly; this check keeps the
human index truthful: the leg SET equals the baseline's, each row's `frames` equals the leg's LEG.tsv
`frames` (or its frames.fhash line count without one), its `probes` equal the `probe/old_b<N>.ps2st`
files, its `anchor` equals leg_spec.anchor_for's answer, `source` is `old` or `new`, a `session` leg's `pad_script` exists under pads/ and equals its LEG.tsv -- and the retired
`verify/baseline` path does not exist (no symlink: the new path is the path).

  anchors_table.py [--baseline DIR] [--anchors FILE]   -> `anchors: ... PASS|FAIL`, exit 0/1
  anchors_table.py --selftest
"""
import argparse
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import leg_spec  # noqa: E402

COLUMNS = ("name", "kind", "anchor", "frames", "probes", "probe_dir", "pad_script", "source")
KINDS = ("coldboot", "anchor", "slowspot", "attract", "session")   # session = a NEW-recorded play session (PLAY_SESSIONS.md)


def read_anchors(path):
    rows = []
    with open(path) as f:
        for line in f:
            s = line.rstrip("\n")
            if not s or s.startswith("#"):
                continue
            cols = s.split("\t")
            if cols[0] == "name":
                if tuple(cols) != COLUMNS:
                    raise SystemExit("anchors.tsv header %r != %r" % (cols, COLUMNS))
                continue
            if len(cols) != len(COLUMNS):
                raise SystemExit("anchors.tsv row has %d columns: %r" % (len(cols), cols))
            rows.append(dict(zip(COLUMNS, cols)))
    return rows


def probe_dir(baseline, leg):
    """The leg's probe dir: in-tree when it holds a probe, else the private mirror's, else None."""
    local = os.path.join(baseline, leg, "probe")
    if os.path.isdir(local) and any(x.endswith(".ps2st") for x in os.listdir(local)):
        return local
    private_root = os.environ.get("NATIVEEE_PRIVATE_FIXTURES", "")
    if private_root:
        rel = os.path.relpath(os.path.abspath(local), REPO_ROOT)
        mirrored = os.path.join(private_root, rel)
        if os.path.isdir(mirrored):
            return mirrored
    return local if os.path.isdir(local) else None


def check(baseline, rows, skipped=None):
    fails = []
    skipped = skipped if skipped is not None else []
    # the move's compatibility rule: the old harness path is gone (no directory, no symlink) -- the new path IS the path
    old_path = os.path.join(HERE, "baseline")
    if os.path.lexists(old_path):
        fails.append("the retired path %s still exists (%s); the baselines live under games/<title>/verify/baseline"
                     % (old_path, "symlink" if os.path.islink(old_path) else "directory"))
    legs = sorted(d for d in os.listdir(baseline) if os.path.isfile(os.path.join(baseline, d, "run.meta")))
    names = [r["name"] for r in rows]
    if sorted(names) != legs:
        fails.append("leg set differs: anchors %s vs baseline %s" % (sorted(set(names) - set(legs)), sorted(set(legs) - set(names))))
    if len(set(names)) != len(names):
        fails.append("duplicate leg names")
    for r in rows:
        leg = r["name"]
        d = os.path.join(baseline, leg)
        if not os.path.isdir(d):
            continue
        if r["kind"] not in KINDS:
            fails.append("%s: kind %r not in %s" % (leg, r["kind"], KINDS))
        if r["source"] not in ("old", "new"):
            fails.append("%s: source %r" % (leg, r["source"]))
        spec = leg_spec.read_leg(baseline, leg)
        if spec is not None:
            frames = spec["frames"]
        else:
            with open(os.path.join(d, "frames.fhash")) as f:
                frames = sum(1 for _ in f)
        if str(frames) != r["frames"]:
            fails.append("%s: frames %s != %d (%s)" % (leg, r["frames"], frames, "LEG.tsv" if spec else "frames.fhash lines"))
        # The frozen probes (old_b<N>.ps2st: RAM images, game bytes) are PRIVATE fixtures
        # (cmake/Ps2rPrivateFixtures.cmake): read from $NATIVEEE_PRIVATE_FIXTURES's mirror of this leg
        # dir when the in-tree probe dir carries none. With neither, the probe column is unverifiable
        # and is recorded in `skipped` (the caller reports SKIP, never a failure).
        pdir = probe_dir(baseline, leg)
        probes = sorted(int(m.group(1)) for m in (re.match(r"old_b(\d+)\.ps2st$", x) for x in os.listdir(pdir))
                        if m) if pdir and os.path.isdir(pdir) else []
        want = sorted(int(x) for x in r["probes"].split(",")) if r["probes"] != "-" else []
        if not probes and want:
            skipped.append("%s: probes %s unverifiable (private fixtures absent)" % (leg, want))
        elif probes != want:
            fails.append("%s: probes %s != %s" % (leg, want, probes))
        if r["probe_dir"] != os.path.join("baseline", leg, "probe"):
            fails.append("%s: probe_dir %r" % (leg, r["probe_dir"]))
        anchor = leg_spec.anchor_for(spec, leg, "", "save_states")
        if anchor != r["anchor"]:
            fails.append("%s: anchor %r != leg_spec's %r" % (leg, r["anchor"], anchor))
        if r["pad_script"] != "-" and not os.path.isfile(os.path.join(os.path.dirname(baseline), "pads", r["pad_script"])):
            fails.append("%s: pad_script %r not under pads/" % (leg, r["pad_script"]))
        spec_pad = (spec or {}).get("pad_script")
        row_pad = None if r["pad_script"] == "-" else "verify/pads/" + r["pad_script"]
        if spec_pad != row_pad:
            fails.append("%s: pad_script %r != LEG.tsv's %r" % (leg, r["pad_script"], spec_pad))
        if r["kind"] == "session" and (r["source"] != "new" or spec_pad is None):
            fails.append("%s: a session leg is source=new with a LEG.tsv pad_script" % leg)
    return fails


def selftest():
    ok = True

    def c(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)

    tmp = tempfile.mkdtemp(prefix="anchors_table_")
    base = os.path.join(tmp, "baseline")
    for leg, n in (("coldboot", 3), ("artdet_f1", 2)):
        os.makedirs(os.path.join(base, leg, "probe"))
        open(os.path.join(base, leg, "run.meta"), "w").write("frames=%d\n" % n)
        open(os.path.join(base, leg, "frames.fhash"), "w").write("".join("%d,0x0\n" % i for i in range(1, n + 1)))
        for b in (2, 60):
            open(os.path.join(base, leg, "probe", "old_b%d.ps2st" % b), "w").write("x")
    hdr = "\t".join(COLUMNS) + "\n"
    good = hdr + "coldboot\tcoldboot\tcoldboot\t3\t2,60\tbaseline/coldboot/probe\t-\told\n" \
        "artdet_f1\tanchor\tsave_states/artdet_f1.ps2st\t2\t2,60\tbaseline/artdet_f1/probe\t-\told\n"
    p = os.path.join(tmp, "a.tsv")
    open(p, "w").write(good)
    c(check(base, read_anchors(p)) == [], "a truthful table passes")
    open(p, "w").write(good.replace("\t3\t", "\t4\t"))
    c(any("frames 4 != 3" in x for x in check(base, read_anchors(p))), "a wrong frame count is named")
    open(p, "w").write(good.replace("2,60\tbaseline/artdet", "2\tbaseline/artdet"))
    c(any("probes [2] != [2, 60]" in x for x in check(base, read_anchors(p))), "a missing probe is named")
    # the public tree: no probe file at all -> the column is unverifiable, recorded as skipped, not a failure
    for b in (2, 60):
        os.remove(os.path.join(base, "artdet_f1", "probe", "old_b%d.ps2st" % b))
    skipped = []
    open(p, "w").write(good)
    f = check(base, read_anchors(p), skipped)
    c(f == [] and len(skipped) == 1 and "artdet_f1" in skipped[0], "absent probes are skipped (private fixtures), never a failure")
    for b in (2, 60):
        open(os.path.join(base, "artdet_f1", "probe", "old_b%d.ps2st" % b), "w").write("x")
    open(p, "w").write(hdr + good.split("\n")[1] + "\n")
    c(any("leg set differs" in x for x in check(base, read_anchors(p))), "a missing leg is named")
    open(p, "w").write(good.replace("\told\n", "\tnew\n", 1).replace("artdet_f1\tanchor", "artdet_f1\tweird"))
    f = check(base, read_anchors(p))
    c(any("kind 'weird'" in x for x in f) and not any("source" in x for x in f), "kind checked; source=new accepted")
    print("anchors_table selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", default=leg_spec.BASELINE_DIR)
    ap.add_argument("--anchors", default=os.path.join(os.path.dirname(leg_spec.BASELINE_DIR), "anchors.tsv"))
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    rows = read_anchors(a.anchors)
    skipped = []
    fails = check(a.baseline, rows, skipped)
    for f in fails:
        print("  " + f)
    for s_ in skipped:
        print("  SKIP " + s_)
    verdict = "FAIL" if fails else ("SKIP (probe column: set NATIVEEE_PRIVATE_FIXTURES)" if skipped else "PASS")
    print("anchors: %d rows vs %s -> %s" % (len(rows), a.baseline, verdict))
    return 1 if fails else (77 if skipped else 0)


if __name__ == "__main__":
    sys.exit(main())
