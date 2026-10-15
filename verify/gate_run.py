#!/usr/bin/env python3
"""gate_run.py -- DT-12 retirement, or the historical score/probe pipeline.

--retirement defaults to DT-12: a fresh 13x3 frames/audio battery, followed by
artifact verification using --retirement-evidence and --ctest-inventory. It
does not run PCSX2, the 29 memory probes, or console attribution. --evaluate-only
uses an existing battery. The parent supplies measured scene pairing, self-
oracles, OLD-bug pins, and a completed CTest invocation in the selected scope.
--retirement-policy dt11 explicitly selects the historical policy below.

Historical pipeline: guard -> battery (Release, N reps) -> NEW probe legs ->
score.json + SUMMARY.md (DECISIONS_LOG DT-1 / DT-2).

  gate_run.py --release <bin> --debug <bin> --out <dir> [--set key=val ...] [--reps 3]
              [--skip-guard] [--lanes frames,audio,mem] [--timeout 900] [--legs a,b]
              [--mask verify/mask/coldboot_elf_text.ranges] [--ctest-log <log>] [--keep]
              [--repo <OLD root>] [--baseline games/<title>/verify/baseline]

1. The identity guard (tests/app/root_release_lanes_identical.py run as a leg pair,
   not through ctest): coldboot 200 + artdet 60 frames on both binaries; a DIVERGENT
   guard voids the battery (the run stops, SUMMARY.md records the guard line) unless
   --skip-guard.
2. battery.py on the Release binary: `--lanes frames,audio,mem --reps N --timeout 900`
   plus every --set; its table goes to <dir>/battery/battery.log; per leg the
   cross-engine report of rep 1 is kept as <dir>/battery/<leg>.diff.out (the
   first-divergent keys score.py reports).
3. The NEW probe legs (probe_images.py --binary <release> --set ...), leg by leg: as
   soon as a leg's images exist, its probe pairs are scored (score.pair_words against
   the frozen OLD images under the mask) and the NEW images are deleted (unless --keep),
   so the ~1 GB NEW probe set never has to coexist on disk. The per-pair counts go to
   <dir>/probe_words.json.
4. score.py <dir>/battery --probe-words <dir>/probe_words.json [--ctest-log] ->
   <dir>/score.json, and <dir>/SUMMARY.md: the guard line, the battery table, the
   per-leg first-divergent keys, the per-pair word counts, the tuple.
5. DT-8 (P8.35-C): when `<baseline>/REGRESSION_REF.tsv` exists (or --regression-ref names one),
   `score.py --regression <ref> <dir>/score.json` runs and SUMMARY.md's `## Regression` section
   carries its `regression: PASS|FAIL vs REGRESSION_REF.tsv` line + per-leg table (<dir>/regression.log);
   without a reference the section says NOT RUN. The exit code is still the pipeline's, not the verdict.

The effective reference (DECISIONS_LOG DT-4, proposed) passes through: a leg's
`REBASELINE.tsv` is read by diff_legs.py (lane lines) and score.py (probe pairs); every
re-based window is REPORTED here too -- the `rebased` note on the scored pair, the
`rebased_pairs` line of the score, and a `## Re-based windows` section in SUMMARY.md
listing each manifest row (lane, window, rows, commit, proof) -- never silently.

PER-RUN SCENE PAIRING (P8.30-T; verify/scene_pairing.py): a leg with a LEG.tsv `pair_map` has its
edge derived by probe_images.py from THIS binary's images (`PAIRING.tsv` next to the probe images);
its probe pairs are scored at `B + the run's offset`, its lane diff (`<leg>.diff.out`) is computed
AFTER the probe stage with that record (`diff_legs(..., pairing=)`), and SUMMARY.md carries a
`## Scene pairing (per run)` section (edge, offset, frozen reference, deviation, pass-2 status).
DT-7 (DECISIONS_LOG): every scored pair also carries `words_booked_console` (attribute_probe.
booked_console_only over the pair's images while they exist: the words a console-proved FILED row
books where the console == NEW != OLD) and `words_unbooked`; score.py prints both tuples.

Every leg is bounded (run_leg.py -> run_bounded.sh). A leg with a `LEG.tsv`
(verify/leg_spec.py; coldboot_attract) runs for its own frame count and bounds: battery.py
and probe_images.py raise --timeout / --probe-timeout to the spec's `timeout_s` and pass
its `stall_s` -- nothing to set here. Exit 0 when the pipeline ran to the score (the tuple
is the result, not a pass/fail); 2 when the guard voided the run; 1 on a tool failure.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import score as score_mod  # noqa: E402
import rebaseline  # noqa: E402
import scene_pairing  # noqa: E402
import leg_spec  # noqa: E402
import attribute_probe  # noqa: E402
import regression_ref  # noqa: E402
from diff_legs import diff_legs, parse_lanes  # noqa: E402
from probe_images import baseline_legs, select_legs  # noqa: E402

DEFAULT_REPO = os.environ.get("PS2R_OLD_ROOT", "")  # the frozen OLD tree (read-only); empty = absent
GUARD = os.path.join(REPO_ROOT, "tests", "app", "root_release_lanes_identical.py")


def stamp():
    return time.strftime("%H:%M:%S")


def run(cmd, log_path):
    """Run a tool, tee its output to log_path; returns (exit code, output text)."""
    print("[%s] run: %s" % (stamp(), " ".join(cmd)))
    sys.stdout.flush()
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    with open(log_path, "w") as f:
        f.write(proc.stdout)
    return proc.returncode, proc.stdout


def rebased_windows(baseline, legs):
    """One line per manifest row of the selected legs (the SUMMARY's report; [] when none)."""
    out = []
    for leg in legs:
        for e in rebaseline.read_manifest(os.path.join(baseline, leg)):
            out.append("- %s: %s %s rows %s commit %s proof %s" % (leg, e["lane"], e["window"], ",".join(e["rows"]), e["commit"], e["proof"]))
    return out


def regression_section(ref, out_dir, score_code):
    """Step 5 (DT-8): SUMMARY.md's `## Regression` block. Runs `score.py --regression <ref> <out>/score.json`
    (tee'd to <out>/regression.log) when score.py succeeded AND the reference exists; otherwise the NOT RUN line.
    Returns the SUMMARY lines; the verdict line is score.py's `regression: PASS|FAIL vs REGRESSION_REF.tsv`."""
    lines = ["## Regression (DT-8; the standing gate: score.py --regression)"]
    if score_code == 0 and os.path.isfile(ref):
        rcode, rout = run([sys.executable, os.path.join(HERE, "score.py"), "--regression", ref, os.path.join(out_dir, "score.json")],
                          os.path.join(out_dir, "regression.log"))
        print(rout)
        lines += ["```"] + rout.splitlines() + ["```", ""]
    else:
        line = "regression: NOT RUN (no reference at %s)" % ref if score_code == 0 else "regression: NOT RUN (score.py failed)"
        print(line)
        lines += [line, ""]
    return lines


def _candidate_from_ref(ref):
    """A synthetic score.json dict that reproduces the reference's rows exactly (selftest only)."""
    def key(v):
        return regression_ref.IDENTICAL if v == regression_ref.IDENTICAL else int(v)
    legs = {leg: (key(r["frames_first_div"]), key(r["audio_first_div"]), key(r["mem_first_div"]), int(r["raw_words"]), int(r["booked"]))
            for leg, r in ref["legs"].items()}
    return regression_ref._score(legs, int(ref["all"]["raw_words"]), int(ref["all"]["booked"]),
                                 li=int(ref["header"]["legs_identical"]), pins=int(ref["header"]["pins_green"]))


def selftest():
    import tempfile
    import shutil
    import json
    tmp = tempfile.mkdtemp(prefix="gate_run_")
    os.makedirs(os.path.join(tmp, "coldboot", "probe"))
    os.makedirs(os.path.join(tmp, "artdet", "probe"))
    ok = rebased_windows(tmp, ["coldboot", "artdet"]) == []
    with open(os.path.join(tmp, "coldboot", rebaseline.MANIFEST), "w") as f:
        f.write(rebaseline.header() + "mem\tb120\tP9-TEST-ROW\tabc1234\trebased/mem_b120.ledger.json\t120,0xo\t120,0xn\trebased/new_b120.ps2st\t\n")
    lines = rebased_windows(tmp, ["coldboot", "artdet"])
    ok &= lines == ["- coldboot: mem b120 rows P9-TEST-ROW commit abc1234 proof rebased/mem_b120.ledger.json"]
    # DT-8: the regression reference path next to the frozen legs
    ok &= regression_ref.default_path(tmp) == os.path.join(tmp, "REGRESSION_REF.tsv")
    ok &= not os.path.isfile(regression_ref.default_path(tmp))
    # DT-8 step 5 driven end to end against the REAL reference (the one next to the frozen legs) with a stub score.json
    real_ref = regression_ref.default_path(leg_spec.BASELINE_DIR)
    ok &= os.path.isfile(real_ref)
    print("  real reference: %s %s" % (real_ref, "present" if os.path.isfile(real_ref) else "MISSING"))
    out = os.path.join(tmp, "gate")
    os.makedirs(out)
    ref = regression_ref.read_ref(real_ref)
    cand = _candidate_from_ref(ref)
    with open(os.path.join(out, "score.json"), "w") as f:
        json.dump(cand, f)
    sec = regression_section(real_ref, out, 0)
    ok &= sec[0].startswith("## Regression") and any(l.startswith("regression: PASS vs ") for l in sec)
    ok &= os.path.isfile(os.path.join(out, "regression.log"))
    # one leg's frames key one lower -> FAIL (the block carries score.py's FAIL line; gate exit code is unaffected by design)
    leg = sorted(ref["legs"])[0]
    fd = cand["per_leg"][leg]["first_divergent"]
    if "frames" in fd:
        fd["frames"] -= 1
        with open(os.path.join(out, "score.json"), "w") as f:
            json.dump(cand, f)
        sec = regression_section(real_ref, out, 0)
        ok &= any(l.startswith("regression: FAIL vs ") for l in sec) and any(("%s: frames first-divergent key" % leg) in l for l in sec)
    # no reference -> NOT RUN (reported, never a silent PASS); score.py failed -> NOT RUN (score.py failed)
    sec = regression_section(os.path.join(tmp, "REGRESSION_REF.tsv"), out, 0)
    ok &= sec[1].startswith("regression: NOT RUN (no reference at ")
    sec = regression_section(real_ref, out, 1)
    ok &= sec[1] == "regression: NOT RUN (score.py failed)"
    shutil.rmtree(tmp, ignore_errors=True)
    print("gate_run selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def dt12_run(a, parser):
    """A separate path: no accidental entry into historical console/probe code."""
    from pathlib import Path
    import retirement_dt12 as dt12
    if (a.reps != 3 or a.legs != "all" or not a.ctest_log or not a.ctest_inventory or
            not a.retirement_evidence or a.lanes not in ("frames,audio", "frames,audio,mem")):
        parser.error("DT-12 requires all legs, 3 reps, frames/audio, --ctest-log, "
                     "--ctest-inventory and --retirement-evidence")
    if a.set:
        parser.error("DT-12 acceptance uses native defaults; record model toggles in self-oracle evidence")
    out_dir = Path(a.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    bdir = out_dir / "battery"
    report = out_dir / "retirement.json"
    # A failed/preflight-interrupted run must not leave an earlier PASS behind.
    report.write_text(json.dumps({"policy": "DT-12", "verdict": "FAIL",
                                 "failures": ["collection/validation not completed"]}, indent=2) + "\n")
    summary = ["# DT-12 retirement", "", "PCSX2 excluded. Memory diagnostic only.", ""]
    ref = a.regression_ref or regression_ref.default_path(a.baseline)
    try:
        dt12.require(Path(ref).parent.resolve() == Path(a.baseline).resolve(),
                     "--regression-ref must belong to --baseline")
        evidence = dt12.Evidence(a.retirement_evidence)
        if not a.evaluate_only:
            dt12.require(a.release, "collection requires --release")
            dt12.require(Path(a.release).resolve() == evidence.binary_path.resolve(),
                         "--release differs from evidence build")
            dt12.require(not bdir.exists(), "DT-12 requires a fresh battery directory (or --evaluate-only)")
            # Refuse missing baseline data before starting any game.
            reference = regression_ref.read_ref(ref)
            legs = baseline_legs(a.baseline)
            dt12.require(len(legs) == 13 and set(legs) == set(reference["legs"]),
                         "missing/incomplete 13-leg OLD baseline")
            for leg in legs:
                dt12.history.run_contract(a.baseline, leg)
            fingerprint = dt12.build_fingerprint(a.retirement_evidence)
            bdir.mkdir()
            cmd = [sys.executable, os.path.join(HERE, "battery.py"), "--binary", a.release,
                   "--lanes", "frames,audio", "--reps", "3", "--advisory-mem",
                   "--out", str(bdir), "--baseline", a.baseline, "--repo", a.repo,
                   "--timeout", str(a.timeout), "--legs", ",".join(legs)]
            code, output = run(cmd, str(bdir / "battery.log"))
            summary += ["## Battery", "```", output, "```", ""]
            dt12.require(code == 0, "battery runner failed; no run receipts issued")
            dt12.require(dt12.build_fingerprint(a.retirement_evidence) == fingerprint,
                         "build changed during battery")
            dt12.seal_battery(bdir, legs, fingerprint)
        cmd = [sys.executable, os.path.join(HERE, "retirement_gate.py"), "--policy", "dt12",
               "--evidence", a.retirement_evidence, "--battery", str(bdir), "--ref", ref,
               "--ctest-log", a.ctest_log, "--ctest-inventory", a.ctest_inventory, "--json", str(report)]
        code, output = run(cmd, str(out_dir / "retirement.log"))
        print(output)
        summary += ["## Acceptance", "```", output, "```", "",
                    "DT-11 retained via --retirement-policy dt11; not evaluated as DT-12 acceptance.",
                    "Historical memory/console reports are not recomputed in this wave.", ""]
    except (OSError, ValueError, TypeError, KeyError, IndexError, AttributeError) as error:
        code = 1
        failure = {"policy": "DT-12", "verdict": "FAIL", "failures": [str(error)]}
        report.write_text(json.dumps(failure, indent=2) + "\n")
        summary += ["retirement: FAIL (DT-12)", str(error), ""]
        print("retirement: FAIL (DT-12): " + str(error))
    (out_dir / "SUMMARY.md").write_text("\n".join(summary) + "\n")
    return 0 if code == 0 else 1


def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser()
    ap.add_argument("--release")
    ap.add_argument("--debug")
    ap.add_argument("--out", required=True)
    ap.add_argument("--set", action="append", metavar="KEY=VAL")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--skip-guard", action="store_true")
    ap.add_argument("--lanes", default="frames,audio,mem")
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--legs", default="all")
    ap.add_argument("--mask", default=os.path.join(HERE, "mask", "coldboot_elf_text.ranges"))
    ap.add_argument("--ctest-log")
    ap.add_argument("--keep", action="store_true", help="keep the NEW probe images and the guard legs")
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--baseline", default=leg_spec.BASELINE_DIR)
    ap.add_argument("--probe-timeout", type=int, default=300)
    ap.add_argument("--classes", default=os.path.join(HERE, "attribution", "classes.tsv"), help="DT-7: the attribution rules (console-proved rows)")
    ap.add_argument("--pairing", choices=("run", "frozen"), default="run", help="probe_images.py --pairing (the per-run scene edge, or the frozen map)")
    ap.add_argument("--verify-edge2", action="store_true", help="probe_images.py --verify-edge2 (two one-image legs at the predicted pass-2 edge)")
    ap.add_argument("--regression-ref", help="DT-8: the REGRESSION_REF.tsv to decide against (default <--baseline>/REGRESSION_REF.tsv; absent = NOT RUN, reported)")
    ap.add_argument("--retirement", action="store_true",
                    help="DT-12 by default: same-input NEW oracles, 13x3, zero model work and OLD-bug pins")
    ap.add_argument("--retirement-policy", "--policy", choices=("dt12", "dt11"), default="dt12")
    ap.add_argument("--retirement-evidence", "--evidence", dest="retirement_evidence",
                    help="DT-12 evidence manifest; schema and producer contract in verify/README.md")
    ap.add_argument("--ctest-inventory", help="unfiltered ctest --show-only=json-v1 output")
    ap.add_argument("--evaluate-only", action="store_true", help="DT-12: consume <out>/battery without running games")
    a = ap.parse_args()
    if a.retirement and a.retirement_policy == "dt12":
        return dt12_run(a, ap)
    if a.evaluate_only or a.retirement_evidence or a.ctest_inventory:
        ap.error("DT-12 evidence/evaluate-only options require --retirement")
    if not a.release or not a.debug:
        ap.error("historical pipeline requires --release and --debug")
    if a.retirement and (a.reps != 3 or not a.ctest_log or a.lanes != "frames,audio,mem"):
        ap.error("--retirement requires --reps 3, --ctest-log and frames,audio,mem lanes")
    os.makedirs(a.out, exist_ok=True)
    sets = a.set or []
    set_args = []
    for kv in sets:
        set_args += ["--set", kv]
    legs = select_legs(baseline_legs(a.baseline), a.legs)
    if a.retirement and len(legs) != 13:
        ap.error("--retirement requires all 13 scenarios")
    summary = ["# gate_run %s" % time.strftime("%Y-%m-%d %H:%M:%S"),
               "release=%s" % a.release, "debug=%s" % a.debug, "reps=%d lanes=%s timeout=%d" % (a.reps, a.lanes, a.timeout),
               "set=%s" % (" ".join(sets) if sets else "(none: defaults)"), "legs=%s" % ",".join(legs), ""]
    rebased = rebased_windows(a.baseline, legs)
    summary += ["## Re-based windows (REBASELINE.tsv; the effective reference, DT-4)"] + (rebased or ["none: every lane window and probe pair against the frozen OLD files"]) + [""]
    for l in rebased:
        print("[%s] %s" % (stamp(), l))

    # 1. the guard
    guard_line = "guard=SKIPPED (--skip-guard)"
    if not a.skip_guard:
        anchor = os.path.join(a.repo, "save_states", "artdet_f37152.ps2st")
        cmd = [sys.executable, GUARD, "--debug", a.debug, "--release", a.release, "--anchor", anchor,
               "--repo", a.repo, "--out", os.path.join(a.out, "guard")] + set_args
        if a.keep:
            cmd.append("--keep")
        code, out = run(cmd, os.path.join(a.out, "guard.log"))
        guard_line = next((l for l in out.splitlines() if l.startswith("guard=")), "guard=ERROR exit=%d" % code)
        if code == 77:
            guard_line = "guard=SKIPPED (%s)" % next((l for l in out.splitlines() if l.startswith("SKIP")), "77")
    print("[%s] %s" % (stamp(), guard_line))
    summary += ["## Guard", guard_line, ""]
    if guard_line.startswith("guard=DIVERGENT") or guard_line.startswith("guard=ERROR"):
        summary += ["BATTERY VOIDED by the guard.", ""]
        with open(os.path.join(a.out, "SUMMARY.md"), "w") as f:
            f.write("\n".join(summary) + "\n")
        return 2

    # 2. the battery (Release)
    bdir = os.path.join(a.out, "battery")
    cmd = [sys.executable, os.path.join(HERE, "battery.py"), "--binary", a.release, "--lanes", a.lanes,
           "--reps", str(a.reps), "--out", bdir, "--baseline", a.baseline, "--repo", a.repo,
           "--timeout", str(a.timeout), "--legs", ",".join(legs)] + set_args
    os.makedirs(bdir, exist_ok=True)
    if a.retirement:
        cmd.append("--advisory-mem")
    code, out = run(cmd, os.path.join(bdir, "battery.log"))
    table = [l for l in out.splitlines() if l.strip()]
    print("\n".join(table[-(len(legs) + 2):]))
    lanes = parse_lanes(a.lanes)
    mapped_legs = [leg for leg in legs if (leg_spec.read_leg(a.baseline, leg) or {}).get("pair_edge") is not None]

    def write_diff(leg, pairing=None):
        rep1 = os.path.join(bdir, leg + ".rep1")
        if os.path.isfile(os.path.join(rep1, "run.meta")):
            _, rep = diff_legs(os.path.join(a.baseline, leg), rep1, cross_engine=True, lanes=lanes, quiet=True, pairing=pairing)
            with open(os.path.join(bdir, leg + ".diff.out"), "w") as f:
                f.write("\n".join(rep) + "\n")
    for leg in legs:
        if leg not in mapped_legs:
            write_diff(leg)       # a scene-paired leg's diff waits for the run's edge (step 3)
    summary += ["## Battery (Release, %d rep%s)" % (a.reps, "" if a.reps == 1 else "s"), "```"] + table[-(len(legs) + 2):] + ["```", ""]

    # 3. the NEW probe legs, scored pair by pair
    rows = score_mod.read_mask_ranges(a.mask)
    pdir = os.path.join(a.out, "probes")
    os.makedirs(pdir, exist_ok=True)
    words = {}
    pairing_lines = []
    for leg in legs:
        cmd = [sys.executable, os.path.join(HERE, "probe_images.py"), "--binary", a.release, "--out", pdir,
               "--legs", leg, "--repo", a.repo, "--baseline", a.baseline, "--timeout", str(a.probe_timeout),
               "--pairing", a.pairing] + (["--verify-edge2"] if a.verify_edge2 else []) + set_args
        code, out = run(cmd, os.path.join(pdir, leg + ".probe.log"))
        for line in out.splitlines():
            if "new_b" in line or "scene_pairing:" in line or "pairing:" in line:
                print("  " + line)
        pairing = scene_pairing.read(os.path.join(pdir, leg)) if leg in mapped_legs else None
        if leg in mapped_legs:
            spec = leg_spec.read_leg(a.baseline, leg)
            note = scene_pairing.note(spec, pairing, spec["probes"][0][0]) if spec["probes"] else "(no probes)"
            pairing_lines.append("- %s: %s%s" % (leg, note, "" if pairing is None else "; status %s, census %s frames, edge words %s" % (
                pairing["status"], pairing["census_frames"], pairing["edge_words"])))
            print("  [%s] %s" % (stamp(), pairing_lines[-1]))
            write_diff(leg, pairing)
        reb = score_mod.rebased_pairs(a.baseline, [leg])
        notes = score_mod.pairing_notes(a.baseline, [leg], pdir)
        for leg_, b, old_path, new_path in score_mod.probe_pairs(a.baseline, pdir, [leg]):
            w = score_mod.pair_words(old_path, new_path, rows)
            key = "%s@b%d" % (leg_, b)
            if key in reb:
                w["rebased"] = reb[key]
            if key in notes:
                w.update(notes[key])
            # DT-7: the console-proved booked words of this pair, while both images exist
            booked = attribute_probe.booked_console_only(old_path, new_path, a.classes, a.mask, leg_, b) if not w["missing"] else \
                {"booked_console": 0, "method": "NEW image missing"}
            w["words_booked_console"], w["booking"] = booked["booked_console"], booked["method"]
            words[key] = w
            print("  [%s] pair %s@b%d: RAM0 %d SPAD %d total %d booked_console %d unbooked %d%s%s%s" % (
                stamp(), leg_, b, w["RAM0"], w["SPAD"], w["total"], w["words_booked_console"], w["total"] - w["words_booked_console"],
                " (NEW MISSING)" if w["missing"] else "",
                " REBASED (%s)" % ", ".join(reb[key]["rows"]) if key in reb else "",
                "  new_b%d: %s" % (w["new_boundary"], w["pairing"]) if key in notes else ""))
            if not a.keep and new_path and os.path.isfile(new_path):
                os.remove(new_path)
        with open(os.path.join(a.out, "probe_words.json"), "w") as f:
            json.dump(words, f, indent=1, sort_keys=True)
    summary += ["## Scene pairing (per run; DT-6 -- the frozen LEG.tsv map is the reference, every deviation reported)"] + \
        (pairing_lines or ["none: no leg with a pair_map"]) + [""]

    # 4. the score
    cmd = [sys.executable, os.path.join(HERE, "score.py"), bdir, "--probe-words", os.path.join(a.out, "probe_words.json"),
           "--mask", a.mask, "--baseline", a.baseline, "--out", os.path.join(a.out, "score.json"), "--legs", ",".join(legs),
           "--probes", pdir, "--classes", a.classes]
    if a.reps == 1:
        cmd.append("--reps-not-required")
    if a.ctest_log:
        cmd += ["--ctest-log", a.ctest_log]
    code, out = run(cmd, os.path.join(a.out, "score.log"))
    print(out)
    summary += ["## Score", "```"] + out.splitlines() + ["```", ""]
    # 5. the regression verdict (DT-8): against the reference next to the frozen legs, when one exists
    ref = a.regression_ref or regression_ref.default_path(a.baseline)
    summary += regression_section(ref, a.out, code)
    retirement_code = 0
    if a.retirement:
        cmd = [sys.executable, os.path.join(HERE, "retirement_gate.py"),
               "--policy", "dt11",
               "--score", os.path.join(a.out, "score.json"), "--battery", bdir,
               "--ctest-log", a.ctest_log, "--ref", ref,
               "--json", os.path.join(a.out, "retirement.json")]
        retirement_code, retirement_out = run(cmd, os.path.join(a.out, "retirement.log"))
        print(retirement_out)
        summary += ["## Retirement acceptance (DT-11)", "```"] + retirement_out.splitlines() + ["```", ""]
    with open(os.path.join(a.out, "SUMMARY.md"), "w") as f:
        f.write("\n".join(summary) + "\n")
    print("[%s] SUMMARY: %s" % (stamp(), os.path.join(a.out, "SUMMARY.md")))
    return 0 if code == 0 and retirement_code == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
