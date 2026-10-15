#!/usr/bin/env python3
"""DT-12 retirement acceptance, with explicit --policy dt11 historical evaluation.

DT-12 consumes --evidence, --battery, --ctest-log and --ctest-inventory.
Same-input NEW self-oracles, 13x3 frames/audio, actual model counters at zero,
and executed OLD-bug pins decide. No PCSX2 evidence is read or required.
Memory is advisory. DT-11's evaluator below is retained for history only.
"""
import argparse
import hashlib
import json
import os
import re
import stat
import sys
from pathlib import Path

import leg_spec
import regression_ref
import scene_pairing
from battery import baseline_legs, leg_frames
from diff_legs import AUDIO_BOUNDARIES_PER_WINDOW, diff_legs
from profile_deterministic import NEW_DETERMINISTIC_FINGERPRINT

REQUIRED_PINS = (
    "Render_TitleLogoDrawPin", "Render_TitleFrameReplay",
    "Render_TitleFrameReplayDisplay",
    "Render_AttractReplay_attract_start_f2845",
    "Render_AttractReplay_attract_mid1_f3300",
    "Render_AttractReplay_attract_mid2_f3600",
    "Render_AttractReplay_attract_end_f3945",
    "Render_AttractReplay_title_return_f4200",
    "RootTitleScreenEffects", "RootAttractCutsceneFrames",
    "RootReleaseLanesIdentical",
)
TEST_RESULT = re.compile(
    r"^\s*(?:(\d+)/(\d+)\s+)?Test\s+#(\d+):\s+(\S+)\s+\.{2,}\s*"
    r"(.*?)\s+(\d+(?:\.\d+)?)\s+sec\s*$")
SUMMARY = re.compile(r"^100% tests passed, 0 tests failed out of (\d+)\s*$")
HASH_ROW = re.compile(r"(\d+),0x([0-9a-fA-F]{1,16})")
FIRST_DIV = re.compile(
    r"^(frames\.fhash|audio\.ahash): FIRST DIVERGENT .*? at "
    r"(?:key (\d+)|line \d+ \(key (\d+)\))")
IDENTITY_FIELDS = ("build_id", "gl_renderer", "tree", "profile", "env_ps2", "env_hash")
DIGEST_FIELDS = ("source_sha256", "binary_sha256")
LANE_FILES = {"frames": "frames.fhash", "audio": "audio.ahash"}


def pin_failures(text):
    """Accept one complete CTest invocation, not a bag of passing pin names."""
    results, numbers, progress, summaries, failures = {}, set(), [], [], []
    for line in text.splitlines():
        summary = SUMMARY.fullmatch(line.strip())
        if summary:
            summaries.append(int(summary[1]))
            continue
        match = TEST_RESULT.fullmatch(line)
        if match:
            done, total, number, name, status, _seconds = match.groups()
            if name in results or number in numbers:
                failures.append("CTest duplicate result: " + name)
            if summaries:
                failures.append("CTest result after completed summary: " + name)
            if status not in ("Passed", "***Skipped", "***Not Run (Disabled)"):
                failures.append("CTest failed or unknown result: %s: %s" % (name, status))
            results[name] = status
            numbers.add(number)
            progress.append((int(done), int(total)) if done else None)
        elif re.search(r"\bTest\s+#\d+:", line):
            failures.append("CTest malformed result: " + line.strip())
        elif re.search(r"\d+% tests passed,", line):
            failures.append("CTest unsuccessful or malformed summary: " + line.strip())
    completed = sum(status != "***Not Run (Disabled)" for status in results.values())
    if summaries != [completed] or not completed:
        failures.append("CTest needs one successful summary matching all actual results")
    if any(progress) and (None in progress or
                         sorted(progress) != [(i, len(progress)) for i in range(1, len(progress) + 1)]):
        failures.append("CTest progress is incomplete or inconsistent")
    for name in REQUIRED_PINS:
        if results.get(name) != "Passed":
            failures.append("%s: required pin did not pass (%s)" %
                            (name, results.get(name, "missing")))
    return failures


def claimed_key(row, lane):
    """Reject missing/ambiguous score evidence instead of inferring IDENTICAL."""
    if not isinstance(row, dict):
        raise ValueError("score per-leg entry is not an object")
    match = re.fullmatch(r"META-OK (IDENTICAL|DIVERGENT)\((frames|audio|mem)(,(frames|audio|mem))*\)",
                         row.get("row", ""))
    if not match or not isinstance(row.get("first_divergent"), dict):
        raise ValueError("score needs explicit lane results and first_divergent object")
    kind, names = match[1], row["row"].split("(", 1)[1][:-1].split(",")
    if len(names) != len(set(names)):
        raise ValueError("score repeats a lane")
    divergent = row["first_divergent"]
    if set(divergent) - set(regression_ref.LANES):
        raise ValueError("score has unknown first-divergent lanes")
    if kind == "IDENTICAL":
        if lane not in names or lane in divergent:
            raise ValueError("%s score identity is missing or contradictory" % lane)
        return regression_ref.IDENTICAL
    if lane not in names:
        if lane in divergent:
            raise ValueError("%s score divergence contradicts its row" % lane)
        return regression_ref.IDENTICAL
    key = divergent.get(lane)
    if type(key) is not int or key < 0:
        raise ValueError("%s score divergent key is missing/unknown/invalid" % lane)
    return key


def decide(ref, score, stable, ctest_text):
    failures = pin_failures(ctest_text)
    table = []
    if len(ref["legs"]) != 13:
        failures.append("reference must contain all 13 scenarios")
    if set(score.get("per_leg", {})) != set(ref["legs"]):
        failures.append("candidate scenario set differs from the reference")
    for leg, expected in sorted(ref["legs"].items()):
        actual = score.get("per_leg", {}).get(leg, {})
        row = {"leg": leg, "stable": stable.get(leg, False)}
        if not row["stable"]:
            failures.append("%s: three completed stable repetitions required" % leg)
        for lane in regression_ref.LANES:
            if lane in regression_ref.DECIDING_LANES:
                try:
                    key = claimed_key(actual, lane)
                except (TypeError, ValueError) as error:
                    key = None
                    failures.append("%s: %s" % (leg, error))
            else:
                # Even a missing/invalid memory key is advisory.
                try:
                    key = regression_ref.lane_key(actual, lane)
                except (TypeError, ValueError, AttributeError):
                    key = None
            reference = expected[lane + "_first_div"]
            row[lane] = "%s -> %s" % (reference, regression_ref.key_text(key))
            if (lane in regression_ref.DECIDING_LANES and
                    regression_ref.key_rank(key) < regression_ref.key_rank(reference)):
                failures.append("%s: %s first-divergent key %s < reference %s" %
                                (leg, lane, regression_ref.key_text(key), reference))
        table.append(row)
    return {"verdict": "FAIL" if failures else "PASS", "failures": failures,
            "legs": table, "policy": "DT-11", "memory": {
                "advisory": True, "reference_raw": int(ref["all"]["raw_words"]),
                "candidate_raw": score.get("outside_mask_words"),
                "reference_unbooked": int(ref["all"]["unbooked"]),
                "candidate_unbooked": score.get("outside_mask_words_unbooked")}}


def artifact(path, seen=None):
    """Read a regular, non-aliased file. A repeated inode is not another run."""
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode):
        raise ValueError("%s is not a regular artifact" % path)
    if seen is not None:
        identity = (info.st_dev, info.st_ino)
        if identity in seen:
            raise ValueError("%s reuses another run's artifact" % path)
        seen.add(identity)
    return path.read_text(encoding="utf-8")


def metadata(path, seen=None, name="run.meta"):
    meta = {}
    for line in artifact(path / name, seen).splitlines():
        if not line:
            continue
        key, separator, value = line.partition("=")
        if not separator or not key or key in meta:
            raise ValueError("%s: malformed/duplicate metadata key %r" % (path, key))
        meta[key] = value
    return meta


def lane_rows(path, name, seen=None):
    rows = []
    for line in artifact(path / name, seen).splitlines():
        match = HASH_ROW.fullmatch(line)
        if not match:
            raise ValueError("%s: malformed %s row %r" % (path, name, line))
        rows.append((int(match[1]), int(match[2], 16)))
    if not rows or any(a[0] >= b[0] for a, b in zip(rows, rows[1:])):
        raise ValueError("%s: empty, duplicate or unordered %s keys" % (path, name))
    return rows


def config_hash(meta):
    # Config::FingerprintHash plus VerifyHost::Arm's decoder dependency fold.
    text = meta["env_ps2"] + ";avcodec_version=" + meta["avcodec_version"] + ";"
    value = 0xcbf29ce484222325
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return "0x%016x" % value


def run_contract(baseline, leg):
    """Use the source scenario, not a candidate's claimed length/restore point."""
    path = Path(baseline) / leg
    meta = metadata(path)
    if meta.get("tree") != "legacy" or meta.get("profile") != "deterministic" or meta.get("finalized") != "1":
        raise ValueError("%s: invalid frozen baseline identity" % leg)
    frames = lane_rows(path, "frames.fhash")
    lane_rows(path, "audio.ahash")
    count = leg_frames(str(baseline), leg)
    if count <= 0 or frames[0][0] != 1:
        raise ValueError("%s: invalid baseline frame contract" % leg)
    spec = leg_spec.read_leg(str(baseline), leg)
    cold = spec["anchor"] == "coldboot" if spec else leg == "coldboot"
    if not cold and len(frames) < 2:
        raise ValueError("%s: unknown restore point" % leg)
    restored = 0 if cold else frames[1][0] - 1
    if not cold and restored <= 1:
        raise ValueError("%s: unknown restore point" % leg)
    keys = [1] + list(range(restored + 1 if restored else 2,
                            restored + count if restored else count + 1))
    return count, restored, keys


def read_run(path, contract, seen):
    if path.is_symlink() or not path.is_dir():
        raise ValueError("%s: missing or aliased repetition" % path)
    # These manifests can alter diff_legs semantics. Only the trusted baseline
    # owns comparison rules; a candidate cannot rebase away its own regression.
    for name in ("REBASELINE.tsv", "LEG.tsv", "PAIRING.tsv"):
        if (path / name).exists():
            raise ValueError("%s: candidate comparison override %s" % (path, name))
    meta = metadata(path, seen)
    count, restored, keys = contract
    required = dict(finalized="1", profile="deterministic", tree="new",
                    env_ps2=NEW_DETERMINISTIC_FINGERPRINT, frames=str(count),
                    snd_frames=str(count * 800), park="none", park_detail="frames_cap")
    for key, expected in required.items():
        if meta.get(key) != expected:
            raise ValueError("%s: %s=%r, expected %r" % (path, key, meta.get(key), expected))
    for key in IDENTITY_FIELDS:
        if meta.get(key, "").strip().lower() in ("", "none", "unknown", "?", "n/a"):
            raise ValueError("%s: unknown %s" % (path, key))
    if not re.fullmatch(r"0x[0-9a-fA-F]{16}", meta["env_hash"]):
        raise ValueError("%s: invalid config fingerprint" % path)
    if (not re.fullmatch(r"\d+\.\d+\.\d+", meta.get("avcodec_version", "")) or
            meta["env_hash"] != config_hash(meta)):
        raise ValueError("%s: fingerprint does not match config/decoder identity" % path)
    if meta.get("restored_vsync_frame", "0") != str(restored):
        raise ValueError("%s: restore point differs from source scenario" % path)
    for key in DIGEST_FIELDS:
        if key in meta and not re.fullmatch(r"[0-9a-f]{64}", meta[key]):
            raise ValueError("%s: invalid %s" % (path, key))
    rows = {lane: lane_rows(path, name, seen) for lane, name in LANE_FILES.items()}
    if [key for key, _ in rows["frames"]] != keys:
        raise ValueError("%s: missing/extra/wrong frame keys" % path)
    windows = (count + AUDIO_BOUNDARIES_PER_WINDOW - 1) // AUDIO_BOUNDARIES_PER_WINDOW
    if [key for key, _ in rows["audio"]] != list(range(windows)):
        raise ValueError("%s: missing/extra/wrong audio windows" % path)
    return meta, rows


def identity_fields(meta):
    return {key: meta.get(key) for key in IDENTITY_FIELDS + DIGEST_FIELDS + ("avcodec_version",)}


def pairing_record(probes, baseline, leg, identity, score):
    """Use the gate's measured edge, never silently substitute the frozen map."""
    spec = leg_spec.read_leg(str(baseline), leg)
    if not spec or spec.get("pair_edge") is None:
        return None, None
    path = Path(probes) / leg
    text = artifact(path / scene_pairing.FILE)
    pairing = scene_pairing.parse_pairing(text)
    old, reference = spec["pair_edge"]
    if (pairing["status"] != "EDGE-FOUND" or pairing["method"] != "image-census" or
            (pairing["old_edge"], pairing["ref_old"], pairing["ref_new"]) != (old, old, reference) or
            pairing["ref_offset"] != reference - old or
            not 0 < pairing["new_edge"] < pairing["census_frames"] <= spec["frames"] or
            pairing["pair_map"] != scene_pairing.pair_map_text(spec, pairing["offset"]) or
            pairing["edge2_status"] not in ("n/a", "PREDICTED", "VERIFIED")):
        raise ValueError("%s: unknown or inconsistent scene pairing" % leg)
    words = {}
    for token in pairing["edge_words"].split(";"):
        match = re.fullmatch(r"b(\d+):case(\d+)/bit63=[01]/clock\d+", token)
        if not match or int(match[1]) in words:
            raise ValueError("%s: invalid scene edge evidence" % leg)
        words[int(match[1])] = int(match[2])
    if words.get(pairing["new_edge"] - 1) != 6 or words.get(pairing["new_edge"]) != 7:
        raise ValueError("%s: scene edge lacks adjacent load/title observations" % leg)
    expected_subset = {key: pairing[key] for key in
                       ("status", "new_edge", "offset", "ref_offset", "deviation", "edge2_status")}
    pairs = [pair for pair in score.get("pairs", []) if pair.get("leg") == leg]
    if (sorted(pair.get("boundary", -1) for pair in pairs) != sorted(b for b, _ in spec["probes"]) or
            any(pair.get("pairing_record") != expected_subset or
                pair.get("new_boundary") != pair["boundary"] + pairing["offset"] for pair in pairs)):
        raise ValueError("%s: score scene pairing does not match actual PAIRING.tsv" % leg)
    if pairing["edge2_status"] != "n/a":
        if (pairing["edge2_old"] is None or pairing["edge2_new"] is None or
                pairing["edge2_new"] != pairing["edge2_old"] + pairing["offset"]):
            raise ValueError("%s: inconsistent second scene edge" % leg)
    proof = {"path": str(path / scene_pairing.FILE),
             "sha256": hashlib.sha256(text.encode()).hexdigest(), "record": pairing, "probes": {}}
    for boundary, _frames in spec["probes"]:
        frames = boundary + pairing["offset"] + 1
        name = "f%d_run.meta" % frames
        meta = metadata(path, name=name)
        if (identity_fields(meta) != identity or meta.get("finalized") != "1" or
                meta.get("frames") != str(frames) or meta.get("park") != "none" or
                meta.get("park_detail") != "frames_cap"):
            raise ValueError("%s: scene probe %s does not match completed battery source/build" % (leg, name))
        proof["probes"][name] = hashlib.sha256((path / name).read_bytes()).hexdigest()
    return pairing, proof


def actual_key(baseline, candidate, lane, pairing=None):
    code, report = diff_legs(str(baseline), str(candidate), cross_engine=True,
                            lanes=[lane], quiet=True, pairing=pairing)
    if code == 0:
        return regression_ref.IDENTICAL
    matches = [match for line in report if (match := FIRST_DIV.match(line))]
    if code != 1 or len(matches) != 1 or matches[0][1] != LANE_FILES[lane]:
        raise ValueError("%s: unknown %s comparison: %s" % (candidate, lane, "; ".join(report)))
    return int(matches[0][2] or matches[0][3])


def check_artifacts(battery, legs, baseline, score, probes):
    """Validate 39 complete runs and bind the score to their actual comparisons."""
    root = Path(battery)
    expected = {"%s.rep%d" % (leg, rep) for leg in legs for rep in (1, 2, 3)}
    found = {path.name for path in root.iterdir()
             if path.is_dir() or re.fullmatch(r".+\.rep\d+", path.name)}
    if found != expected:
        raise ValueError("battery repetition set differs: missing=%s unexpected=%s" %
                         (sorted(expected - found), sorted(found - expected)))
    if os.path.realpath(score.get("battery_dir", "")) != os.path.realpath(battery):
        raise ValueError("score battery_dir does not name the supplied artifacts")
    stable, memory, failures, seen, identity, sources, actual = {}, {}, [], set(), None, {}, {}
    pairings = {}
    for leg in sorted(legs):
        paths = [root / ("%s.rep%d" % (leg, rep)) for rep in (1, 2, 3)]
        stable[leg] = False
        try:
            contract = run_contract(baseline, leg)
            runs = [read_run(path, contract, seen) for path in paths]
            for path, (meta, _rows) in zip(paths, runs):
                fields = identity_fields(meta)
                if identity is None:
                    identity = fields
                elif fields != identity:
                    raise ValueError("%s: build/config/source identity differs across the battery" % path)
                sources[path.name] = {
                    name: hashlib.sha256((path / name).read_bytes()).hexdigest()
                    for name in ("run.meta", "frames.fhash", "audio.ahash")}
            stable[leg] = all(run[1] == runs[0][1] for run in runs[1:])
            pairing, proof = pairing_record(probes, baseline, leg, identity, score)
            if proof:
                pairings[leg] = proof
            actual[leg] = {}
            for lane in regression_ref.DECIDING_LANES:
                key = actual_key(Path(baseline) / leg, paths[0], lane, pairing)
                actual[leg][lane] = key
                if key != claimed_key(score.get("per_leg", {}).get(leg, {}), lane):
                    failures.append("%s: score %s key does not match actual artifacts (%s)" % (leg, lane, key))
            try:
                values = [artifact(path / "mem.mhash") for path in paths]
                memory[leg] = all(value == values[0] for value in values[1:])
            except (OSError, ValueError) as error:
                memory[leg] = "unavailable (advisory): %s" % error
        except (OSError, ValueError, IndexError, TypeError, KeyError, AttributeError) as error:
            stable[leg] = False
            failures.append("%s: %s" % (leg, error))
    return stable, memory, failures, {"identity": identity, "runs": sources, "actual_keys": actual, "pairings": pairings,
                                     "source_provenance": "recorded digests" if identity and
                                     all(identity.get(k) for k in DIGEST_FIELDS) else
                                     "unavailable: build_id is not a source or binary digest"}


def evaluate(ref, score, battery, baseline, ctest_text, probes=None):
    legs = ref["legs"]
    if (len(legs) != 13 or set(legs) != set(baseline_legs(str(baseline))) or
            any(not re.fullmatch(r"[A-Za-z0-9_-]+", leg) for leg in legs)):
        raise ValueError("reference must match all 13 source scenarios")
    if not isinstance(score, dict) or not isinstance(score.get("per_leg"), dict):
        raise ValueError("score must contain a per_leg object")
    stable, memory, failures, evidence = check_artifacts(
        battery, legs, baseline, score, probes or Path(battery).parent / "probes")
    result = decide(ref, score, stable, ctest_text)
    result["failures"].extend(failures)
    result["verdict"] = "FAIL" if result["failures"] else "PASS"
    result["memory"]["repetition_identity"] = memory
    result["evidence"] = evidence
    return result


def read_score(path):
    def members(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("score repeats JSON member %r" % key)
            result[key] = value
        return result

    def invalid_constant(value):
        raise ValueError("score contains non-JSON constant " + value)

    with open(path) as stream:
        result = json.load(stream, object_pairs_hook=members, parse_constant=invalid_constant)
    if not isinstance(result, dict):
        raise ValueError("score must be a JSON object")
    return result


def test_fixture(root):
    """Small real lane files: cold boot, restored keys and a partial audio tail."""
    baseline, battery = root / "baseline", root / "battery"
    baseline.mkdir()
    battery.mkdir()
    base = {}
    for number in range(13):
        leg = "coldboot_attract" if number == 12 else "scenario%d" % number if number else "coldboot"
        restored, count = (100 + number if number not in (0, 12) else 0), 13
        keys = [1] + list(range(restored + 1, restored + count)) if restored else list(range(1, count + 1))
        base[leg] = (keys[3], 1, 0, 1000, 0)
        for repetition in range(4):
            path = baseline / leg if not repetition else battery / ("%s.rep%d" % (leg, repetition))
            path.mkdir()
            meta = dict(build_id="fixture-build", gl_renderer="fixture-gpu", tree="new" if repetition else "legacy",
                        profile="deterministic", env_ps2=NEW_DETERMINISTIC_FINGERPRINT,
                        frames=str(count), snd_frames=str(count * 800), finalized="1",
                        park="none", park_detail="frames_cap", avcodec_version="62.28.101")
            if repetition and restored:
                meta["restored_vsync_frame"] = str(restored)
            meta["env_hash"] = config_hash(meta)
            (path / "run.meta").write_text("".join("%s=%s\n" % item for item in meta.items()))
            (path / "frames.fhash").write_text("".join(
                "%d,0x%016x\n" % (key, 2 if repetition and i == 3 else 1) for i, key in enumerate(keys)))
            (path / "audio.ahash").write_text("".join(
                "%d,0x%016x\n" % (key, 2 if repetition and key == 1 else 1) for key in range(3)))
            (path / "mem.mhash").write_text("0,0x1\n")
    score = regression_ref._score(base, 13000, 0)
    score["battery_dir"] = str(battery)
    mapped = baseline / "coldboot_attract"
    spec_text = leg_spec.format_spec(13, 1, 60, 60, "coldboot", [(7, 13)], "mapped fixture",
                                    pair_map="old_b6=new_b12,old_b7=new_b13", vf_offset=0)
    (mapped / "LEG.tsv").write_text(spec_text)
    spec = leg_spec.parse(spec_text)
    pairing = scene_pairing.make(spec, 6, "EDGE-FOUND", "image-census", 13,
                                 "b5:case6/bit63=0/clock1;b6:case7/bit63=0/clock2")
    probes = root / "probes/coldboot_attract"
    scene_pairing.write(str(probes), pairing)
    probe_meta = metadata(battery / "coldboot_attract.rep1")
    probe_meta.update(frames="8", snd_frames="6400")
    (probes / "f8_run.meta").write_text("".join("%s=%s\n" % item for item in probe_meta.items()))
    for pair in score["pairs"]:
        if pair["leg"] == "coldboot_attract":
            pair.update(boundary=7, new_boundary=7,
                        pairing_record={key: pairing[key] for key in
                                        ("status", "new_edge", "offset", "ref_offset", "deviation", "edge2_status")})
    rows = regression_ref.rows_from_score(score, "test")
    ref = {"legs": {row["leg"]: row for row in rows if row["leg"] != "ALL"}, "all": rows[-1]}
    ctest = "\n".join("%d/%d Test #%d: %s .... Passed 1 sec" % (i, len(REQUIRED_PINS), i, name)
                      for i, name in enumerate(REQUIRED_PINS, 1))
    ctest += "\n100%% tests passed, 0 tests failed out of %d\n" % len(REQUIRED_PINS)
    return baseline, battery, ref, score, ctest


def artifact_selftests(check):
    import copy
    import shutil
    import tempfile
    with tempfile.TemporaryDirectory(prefix="retirement_gate_") as directory:
        root = Path(directory)
        baseline, battery, ref, score, ctest = test_fixture(root)
        snapshot = {path: path.read_bytes() for path in root.rglob("*") if path.is_file()}

        def reset():
            shutil.rmtree(battery)
            battery.mkdir()
            for path, data in snapshot.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)

        def evaluate_fixture(candidate=None, log=None):
            try:
                return evaluate(ref, score if candidate is None else candidate,
                                battery, baseline, ctest if log is None else log)
            except (OSError, ValueError, TypeError, KeyError) as error:
                return {"verdict": "FAIL", "failures": [str(error)]}

        def reject(label, mutation):
            reset()
            mutation()
            result = evaluate_fixture()
            check(result["verdict"] == "FAIL" and bool(result["failures"]), label)

        def replace(path, before, after):
            path.write_text(path.read_text().replace(before, after))

        rep = battery / "coldboot.rep1"
        all_reps = [battery / ("coldboot.rep%d" % n) for n in (1, 2, 3)]
        result = evaluate_fixture()
        check(result["verdict"] == "PASS" and len(result["evidence"]["runs"]) == 39,
              "39 actual complete artifacts, restored frame keys and partial audio tail")
        check(result["evidence"]["pairings"]["coldboot_attract"]["record"]["offset"] == 0 and
              result["evidence"]["actual_keys"]["coldboot_attract"]["audio"] == 1,
              "mapped comparison uses actual offset 0 instead of frozen offset 6")
        try:
            actual_key(baseline / "coldboot_attract", battery / "coldboot_attract.rep1", "audio")
            check(False, "mapped fixture would fail under default frozen rule")
        except ValueError:
            check(True, "mapped fixture would fail under default frozen rule")
        pairing = root / "probes/coldboot_attract/PAIRING.tsv"
        reject("missing mapped pairing fails instead of frozen fallback", lambda: pairing.unlink())
        reject("malformed mapped pairing", lambda: pairing.write_text("incomplete\n"))
        reject("unknown mapped edge", lambda: replace(pairing, "EDGE-FOUND", "EDGE-NOT-FOUND"))
        reject("mapped edge lacks adjacent evidence", lambda: replace(pairing, "b5:case6", "b4:case6"))
        reject("mapped probe from a different build", lambda:
               replace(root / "probes/coldboot_attract/f8_run.meta", "fixture-build", "other-build"))
        reject("missing mapped probe metadata", lambda: (root / "probes/coldboot_attract/f8_run.meta").unlink())
        reset()
        changed = copy.deepcopy(score)
        for pair in changed["pairs"]:
            if pair["leg"] == "coldboot_attract":
                pair["pairing_record"]["new_edge"] += 1
        check(evaluate_fixture(changed)["verdict"] == "FAIL", "score pairing subset must match actual record")
        for name in ("run.meta", "frames.fhash", "audio.ahash"):
            reject("missing " + name, lambda name=name: (rep / name).unlink())
        for name in ("frames.fhash", "audio.ahash"):
            for shape in ("empty", "short", "duplicate", "malformed", "gap", "extra", "reordered"):
                def corrupt(name=name, shape=shape):
                    for path in all_reps:
                        file = path / name
                        lines = file.read_text().splitlines()
                        if shape == "empty": lines = []
                        elif shape == "short": lines.pop()
                        elif shape == "duplicate": lines[1] = lines[0]
                        elif shape == "malformed": lines[1] = lines[1].split(",")[0] + ",garbage"
                        elif shape == "gap": lines[1] = "900,0x1"
                        elif shape == "extra": lines.append("900,0x1")
                        else: lines[0], lines[1] = lines[1], lines[0]
                        file.write_text("\n".join(lines) + ("\n" if lines else ""))
                reject("%s %s rejects even when all three repeats agree" % (name, shape), corrupt)
        reject("missing repetition", lambda: shutil.rmtree(battery / "scenario1.rep3"))
        reject("unexpected fourth repetition", lambda: (battery / "coldboot.rep4").mkdir())
        reject("unknown scenario", lambda: (battery / "unknown.rep1").mkdir())
        reject("unstable actual frame value", lambda: replace(rep / "frames.fhash", "1,0x0000000000000001",
                                                               "1,0x0000000000000003"))
        for key, value in (("finalized", "0"), ("frames", "12"), ("snd_frames", "0"), ("tree", "legacy"),
                           ("profile", "unknown"), ("park", "trap"), ("park_detail", "timeout"),
                           ("build_id", "unknown"), ("gl_renderer", "none"), ("env_hash", "0x0000000000000000")):
            reject("bad metadata " + key, lambda key=key, value=value:
                   replace(rep / "run.meta", key + "=" + metadata(rep)[key], key + "=" + value))
        reject("missing metadata identity", lambda:
               replace(rep / "run.meta", "build_id=fixture-build\n", ""))
        reject("duplicate metadata cannot override failure", lambda:
               (rep / "run.meta").write_text((rep / "run.meta").read_text() + "finalized=1\n"))
        reject("wrong restored scenario", lambda:
               replace(battery / "scenario1.rep1/run.meta", "restored_vsync_frame=101", "restored_vsync_frame=102"))
        for field, value in (("build_id", "other-build"), ("gl_renderer", "other-gpu"),
                             ("env_ps2", "unknown-preset")):
            reject("cross-scenario " + field + " mismatch", lambda field=field, value=value:
                   [replace(path / "run.meta", field + "=" + metadata(path)[field], field + "=" + value)
                    for path in all_reps])
        reject("incomplete source provenance", lambda:
               (rep / "run.meta").write_text((rep / "run.meta").read_text() + "source_sha256=" + "a" * 64 + "\n"))
        for name in ("REBASELINE.tsv", "LEG.tsv", "PAIRING.tsv"):
            reject("candidate cannot override comparison with " + name,
                   lambda name=name: (rep / name).write_text("not permitted\n"))
        for kind in ("symlink", "hardlink"):
            def alias(kind=kind):
                target = battery / "coldboot.rep2/audio.ahash"
                target.unlink()
                if kind == "symlink":
                    target.symlink_to(rep / "audio.ahash")
                else:
                    os.link(rep / "audio.ahash", target)
            reject(kind + " is not an independent run artifact", alias)
        reset()
        for row in ("META-OK", "META-OK IDENTICAL(frames)", "META-OK DIVERGENT(video)", "META-OK nonsense"):
            changed = copy.deepcopy(score)
            changed["per_leg"]["coldboot"]["row"] = row
            check(evaluate_fixture(changed)["verdict"] == "FAIL", "ambiguous/unknown score row: " + row)
        for lane in ("frames", "audio"):
            for key in (None, True, -1, 1.5, "IDENTICAL", 9999):
                changed = copy.deepcopy(score)
                changed["per_leg"]["coldboot"]["first_divergent"][lane] = key
                check(evaluate_fixture(changed)["verdict"] == "FAIL", "invalid/forged %s key %r" % (lane, key))
            changed = copy.deepcopy(score)
            del changed["per_leg"]["coldboot"]["first_divergent"][lane]
            check(evaluate_fixture(changed)["verdict"] == "FAIL", "missing " + lane + " key")
            # A real regression, consistently reproduced and honestly scored.
            for path in all_reps:
                replace(path / LANE_FILES[lane], "0x0000000000000001", "0x0000000000000003")
            changed["per_leg"]["coldboot"]["first_divergent"][lane] = 1 if lane == "frames" else 0
            check(evaluate_fixture(changed)["verdict"] == "FAIL", "actual " + lane + " regression")
            reset()
        changed = copy.deepcopy(score)
        for path in all_reps:
            replace(path / "frames.fhash", "4,0x0000000000000002", "4,0x0000000000000001")
            replace(path / "frames.fhash", "5,0x0000000000000001", "5,0x0000000000000002")
        changed["per_leg"]["coldboot"]["first_divergent"]["frames"] = 5
        check(evaluate_fixture(changed)["verdict"] == "PASS", "real later frame divergence is accepted")
        reset()
        changed = copy.deepcopy(score)
        changed["per_leg"]["coldboot"] = {"row": "META-OK IDENTICAL(frames,audio,mem)", "first_divergent": {}}
        check(evaluate_fixture(changed)["verdict"] == "FAIL", "claimed identity cannot conceal artifact divergence")
        for mutation, label in ((lambda s: s["per_leg"].pop("coldboot"), "missing score scenario"),
                                (lambda s: s["per_leg"].update(unknown={}), "unknown score scenario"),
                                (lambda s: s.update(battery_dir=str(root)), "score from another battery")):
            changed = copy.deepcopy(score)
            mutation(changed)
            check(evaluate_fixture(changed)["verdict"] == "FAIL", label)
        changed = copy.deepcopy(score)
        changed.update(outside_mask_words=10**12, outside_mask_words_unbooked=10**12, legs_identical=-1)
        changed["per_leg"]["coldboot"]["first_divergent"]["mem"] = None
        (rep / "mem.mhash").unlink()
        (battery / "coldboot.rep2/mem.mhash").write_text("different memory\n")
        check(evaluate_fixture(changed)["verdict"] == "PASS", "missing/different memory and memory score are advisory")
        reset()
        for malformed in ("", ctest.replace("Passed", "***Skipped", 1), ctest.replace("Passed", "PassedMaybe", 1),
                          ctest.replace(REQUIRED_PINS[0], REQUIRED_PINS[0] + "_similar", 1),
                          ctest + ctest, ctest.replace("out of 11", "out of 480"),
                          ctest.replace("1/11", "2/11", 1), ctest + ctest.splitlines()[0] + "\n"):
            check(evaluate_fixture(log=malformed)["verdict"] == "FAIL", "incomplete/ambiguous/incorrect exact pin log")
        reset()
        # Real CTest convention: disabled tests excluded, skipped tests included.
        statuses = [(name, "Passed") for name in REQUIRED_PINS]
        statuses += [("Other%d" % i, "Passed") for i in range(463)]
        statuses += [("Skipped%d" % i, "***Skipped") for i in range(4)]
        statuses += [("Disabled%d" % i, "***Not Run (Disabled)") for i in range(2)]
        full_ctest = "\n".join("%d/480 Test #%d: %s .... %s 0.01 sec" % (i, i, name, status)
                               for i, (name, status) in enumerate(statuses, 1))
        full_ctest += "\n100% tests passed, 0 tests failed out of 478\n"
        check(evaluate_fixture(log=full_ctest)["verdict"] == "PASS",
              "480 results with 2 disabled and 4 nonrequired skipped matches summary 478")
        # Historical artifacts lack digests. Never label their timestamp build ID
        # as proof that current source/binary produced them.
        check(evaluate_fixture()["evidence"]["source_provenance"].startswith("unavailable"),
              "source provenance gap is reported, not invented")
        # Exercise the real CLI/error-report path, not just the decision helper.
        import subprocess
        ref_path, score_path = baseline / "REGRESSION_REF.tsv", root / "score.json"
        log_path, report_path = root / "ctest.log", root / "result.json"
        ref_path.write_text(regression_ref.format_ref(score, "fixture"))
        log_path.write_text(ctest)
        for text in ("{", "[]", '{"per_leg":{},"per_leg":{}}', '{"value":NaN}'):
            score_path.write_text(text)
            report_path.write_text('{"verdict":"PASS"}\n')
            process = subprocess.run(
                [sys.executable, __file__, "--score", str(score_path), "--battery", str(battery),
                 "--policy", "dt11", "--ctest-log", str(log_path), "--ref", str(ref_path), "--json", str(report_path)],
                capture_output=True, text=True, timeout=10)
            check(process.returncode == 1 and json.loads(report_path.read_text())["verdict"] == "FAIL",
                  "malformed JSON/shape replaces stale PASS and exits 1")


def selftest():
    failures = []

    def check(condition, label):
        print("  %s %s" % ("PASS" if condition else "FAIL", label))
        if not condition:
            failures.append(label)

    artifact_selftests(check)
    from tests_retirement_dt12 import run_tests
    dt12_ok = run_tests()
    print("retirement_gate selftest: %s" % ("FAIL" if failures or not dt12_ok else "PASS"))
    return bool(failures) or not dt12_ok


def main():
    if "--selftest" in sys.argv:
        return selftest()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", choices=("dt12", "dt11"), default="dt12")
    parser.add_argument("--score", help="DT-11 historical score only")
    parser.add_argument("--evidence", help="DT-12 evidence manifest")
    parser.add_argument("--ctest-inventory", help="unfiltered ctest --show-only=json-v1 output")
    parser.add_argument("--battery", required=True)
    parser.add_argument("--ctest-log", required=True)
    parser.add_argument("--pairing-root", "--probes", dest="pairing_root",
                        help="per-run probe/PAIRING artifacts (default: score directory/probes)")
    parser.add_argument("--ref", default=regression_ref.default_path(leg_spec.BASELINE_DIR))
    parser.add_argument("--json")
    args = parser.parse_args()
    if args.json:
        # Even an interruption or unexpected producer/parser bug must not leave
        # an older successful acceptance report at the requested output path.
        Path(args.json).write_text(json.dumps({
            "verdict": "FAIL", "policy": "DT-12" if args.policy == "dt12" else "DT-11",
            "failures": ["validation not completed"], "legs": [], "memory": {"advisory": True}
        }, indent=2) + "\n")
    try:
        if args.policy == "dt12":
            from retirement_dt12 import evaluate as evaluate_dt12
            if not args.evidence or not args.ctest_inventory:
                raise ValueError("DT-12 requires --evidence and --ctest-inventory; a DT-11 score is not acceptance")
            if args.score or args.pairing_root:
                raise ValueError("DT-12 reads its measured pairing from --evidence, not a legacy score/probe report")
            result = evaluate_dt12(args.evidence, args.ref, args.battery, args.ctest_log, args.ctest_inventory)
        else:
            if not args.score:
                raise ValueError("--policy dt11 requires --score")
            ref = regression_ref.read_ref(args.ref)
            score = read_score(args.score)
            with open(args.ctest_log) as stream:
                ctest_text = stream.read()
            probes = (args.pairing_root or score.get("probe_dir") or score.get("probes") or
                      Path(args.score).resolve().parent / "probes")
            result = evaluate(ref, score, args.battery, os.path.dirname(args.ref), ctest_text, probes)
            result["inputs"] = {name: {"path": os.path.abspath(path),
                                      "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest()}
                                for name, path in (("score", args.score), ("reference", args.ref), ("ctest", args.ctest_log))}
    except (OSError, ValueError, TypeError, KeyError, IndexError, AttributeError) as error:
        # Replace an earlier PASS report even when parsing fails.
        result = {"verdict": "FAIL", "policy": "DT-12" if args.policy == "dt12" else "DT-11", "failures": [str(error)],
                  "legs": [], "memory": {"advisory": True}}
    print("retirement: %s (%s)" % (result["verdict"], result["policy"]))
    for row in result["legs"]:
        print("  %s stable=%s frames=%s audio=%s mem=%s (advisory)" %
              (row["leg"], row["stable"], row["frames"], row["audio"], row["mem"]))
    print("  memory (advisory): %s" % json.dumps(result["memory"], sort_keys=True))
    for failure in result["failures"]:
        print("  FAIL: " + failure)
    for replacement in result.get("replacements", []):
        print("  retired %s: %s; native work=%d, unexercised runs=%d" %
              (replacement["id"], ", ".join(replacement["retired_operations"]),
               sum(replacement["native_work"].values()), len(replacement["unexercised_runs"])))
    if args.json:
        with open(args.json, "w") as stream:
            json.dump(result, stream, indent=2, sort_keys=True)
            stream.write("\n")
    return result["verdict"] != "PASS"


if __name__ == "__main__":
    sys.exit(main())
