#!/usr/bin/env python3
"""Artifact verifier for DT-12. No games, PCSX2, builds, or tests are launched here.

Hashes bind the parent's reviewed run collection to its files; they are not
cryptographic attestation that an untrusted producer really executed a binary.
The verifier recomputes comparisons and reads counters. A PASS field, a passed
count, or a claimed first-divergent key is never evidence.
"""
import hashlib
import json
import re
from pathlib import Path

import clock_pair
import leg_spec
import regression_ref
import retirement_gate as history
import retirement_scope
import scene_pairing
from retirement_counters import COUNTERS, COUNT_PROVIDERS

SCHEMA = "dt12-retirement-v1"
SOURCE_SCHEMA = "dt12-sources-v1"
RUN_SCHEMA = "dt12-battery-run-v1"
ORACLE_SCHEMA = "dt12-oracle-run-v1"
REPO_ROOT = Path(__file__).resolve().parent.parent
HASH = re.compile(r"[0-9a-f]{64}")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    # No executable or giant capture is read into memory solely to hash it.
    import stat
    require(stat.S_ISREG(path.lstat().st_mode), "%s is not a regular file" % path)
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def json_bytes(data):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate JSON member " + key)
            result[key] = value
        return result

    def constant(value):
        raise ValueError("non-JSON constant " + value)

    return json.loads(data, object_pairs_hook=pairs, parse_constant=constant)


def text(value, label):
    require(isinstance(value, str) and bool(value.strip()) and value == value.strip(),
            "missing/invalid " + label)
    return value


def counter(meta, selector):
    """Exact scalar or comma-separated member; missing is not zero."""
    field, member = text(selector["field"], "counter field"), selector.get("member")
    raw = meta[field]
    require(isinstance(raw, str), "counter %s must be a run.meta string" % field)
    if member is not None:
        values = {}
        for part in raw.split(","):
            key, separator, value = part.partition("=")
            require(separator and key and key not in values, "malformed counter group " + field)
            require(re.fullmatch(r"0|[1-9]\d*", value), "invalid counter value " + field + "." + key)
            values[key] = value
        raw = values[member]
    require(re.fullmatch(r"0|[1-9]\d*", raw), "invalid counter value " + field)
    return int(raw)


def ctest_results(log, inventory):
    """One whole selected invocation, not a count or a convenient passing subset."""
    tests = retirement_scope.inventory_tests(inventory)
    selected = retirement_scope.scope(inventory)
    expected = set(selected["selected"])
    require(expected, "empty DT-12 CTest selection")
    results, numbers, progress, summaries = {}, set(), [], []
    for line in log.splitlines():
        summary = history.SUMMARY.fullmatch(line.strip())
        if summary:
            summaries.append(int(summary[1]))
            continue
        match = history.TEST_RESULT.fullmatch(line)
        if match:
            done, total, number, name, status, _ = match.groups()
            require(name not in results and number not in numbers and not summaries,
                    "duplicate/late CTest result: " + name)
            require(name in expected, "test outside DT-12 scope: " + name)
            require(int(number) == list(tests).index(name) + 1, "CTest test number does not match inventory: " + name)
            want = ("***Not Run (Disabled)" if tests[name]["disabled"] else
                    "***Skipped" if tests[name]["unavailable"] else "Passed")
            require(status == want, "CTest %s: %s (required %s)" % (name, status, want))
            results[name] = status
            numbers.add(number)
            progress.append((int(done), int(total)) if done else None)
        elif re.search(r"\bTest\s+#\d+:", line) or re.search(r"\d+% tests passed,", line):
            raise ValueError("malformed/failed CTest result or summary: " + line.strip())
    require(set(results) == expected, "CTest selected scope incomplete: missing=%s" % sorted(expected - results.keys()))
    passed = sum(value == "Passed" for value in results.values())
    skipped = sum(value == "***Skipped" for value in results.values())
    require(passed > 0 and summaries == [passed + skipped], "CTest needs one complete matching successful summary")
    if any(progress):
        require(None not in progress and sorted(progress) == [(n, len(expected)) for n in range(1, len(expected) + 1)],
                "CTest selected progress incomplete or inconsistent")
    return results, selected


class Evidence:
    def __init__(self, path):
        self.path = Path(path).absolute()
        self.root = self.path.parent
        self.files = {}
        self.manifest = json_bytes(history.artifact(self.path))
        require(self.manifest["schema"] == SCHEMA, "unsupported DT-12 manifest schema")
        # Reference locations are parent-supplied data, never title-specific
        # literals in the generic verifier.
        self.authorities = {}
        for role in ("decomp", "old"):
            root = Path(text(self.manifest["authorities"][role], role + " authority root"))
            require(root.is_absolute() and root.is_dir(), "missing/relative authority root: " + role)
            self.authorities[role] = root
        self.sources = {}
        self.build = self.manifest["build"]
        self.binary_path = self.file(self.build["binary"])
        self.source_path = self.file(self.build["sources"])
        source_manifest = json_bytes(self.source_path.read_bytes())
        require(source_manifest["schema"] == SOURCE_SCHEMA, "unsupported source manifest")
        source_root = Path(source_manifest["root"])
        require(source_root.is_absolute(), "source root must be absolute")
        require(source_manifest["files"], "empty source manifest")
        for name, expected in source_manifest["files"].items():
            relative = Path(name)
            require(not relative.is_absolute() and ".." not in relative.parts, "unsafe source path " + name)
            path = source_root / relative
            require(HASH.fullmatch(expected) and digest(path) == expected, "source fingerprint mismatch: " + name)
            self.sources[name] = path
        self.source_root = source_root

    def file(self, record):
        require(isinstance(record, dict), "artifact needs path and sha256")
        name = text(record["path"], "artifact path")
        require(isinstance(record["sha256"], str) and HASH.fullmatch(record["sha256"]), "invalid artifact sha256")
        path = Path(name)
        if not path.is_absolute():
            path = self.root / path
        require(digest(path) == record["sha256"], "artifact digest mismatch: " + str(path))
        self.files[str(path.absolute())] = record["sha256"]
        return path

    def load(self, record):
        return json_bytes(self.file(record).read_bytes())

    def reference(self, record, role):
        """A short search needle, not copied source, binds an auditable reference."""
        path = self.file(record["file"])
        needle = text(record["needle"], "reference needle")
        require(needle in path.read_text(), "reference needle absent: " + str(path))
        resolved = path.resolve()
        roots = {"decomp": [self.authorities["decomp"]], "old": [self.authorities["old"], REPO_ROOT / "docs/bugs"],
                 "implementation": [self.source_root]}
        require(any(resolved.is_relative_to(root.resolve()) for root in roots[role]),
                "reference outside %s authority: %s" % (role, path))
        if role == "implementation":
            name = str(resolved.relative_to(self.source_root.resolve()))
            require(name in self.sources, "implementation reference missing from source manifest: " + name)
        return str(path)

    def model_selector(self, name):
        require(name in COUNTERS, "unreviewed model execution counter: " + str(name))
        descriptor = COUNTERS[name]
        if "catalog" in descriptor:
            catalog = descriptor["catalog"]
            require(catalog in self.sources, "reviewed counter catalog missing from build manifest: " + catalog)
            require(digest(self.sources[catalog]) == digest(REPO_ROOT / catalog),
                    "counter catalog changed; review candidate: " + name)
        for file_key, needle_key in (("source", "increment"), ("publisher", "publish")):
            path = descriptor[file_key]
            require(path in self.sources, "counter source missing from build manifest: " + path)
            require(descriptor[needle_key] in self.sources[path].read_text(),
                    "counter instrumentation changed; review catalog: " + name)
        if "provider" in descriptor:
            provider = COUNT_PROVIDERS[descriptor["provider"]]
            path = provider["source"]
            require(path in self.sources, "counter provider missing from build manifest: " + path)
            require(digest(self.sources[path]) == digest(REPO_ROOT / path),
                    "counter provider changed; review implementation: " + name)
            contents = self.sources[path].read_text()
            require(all(needle in contents for needle in provider["needles"]),
                    "counter provider increment/delegation missing: " + name)
        return descriptor


def build_fingerprint(evidence_path):
    """Run collector calls before AND after execution, never retroactively guesses."""
    evidence = Evidence(evidence_path)
    return {"binary_sha256": evidence.build["binary"]["sha256"],
            "source_sha256": evidence.build["sources"]["sha256"]}


def seal_battery(battery, legs, fingerprint):
    """Collector receipt; call only for a fresh successful battery from this build."""
    for leg in legs:
        for rep in (1, 2, 3):
            path = Path(battery) / ("%s.rep%d" % (leg, rep))
            receipt = path / "retirement-run.json"
            require(not receipt.exists(), "refusing to overwrite run receipt " + str(receipt))
            value = dict(schema=RUN_SCHEMA, **fingerprint,
                         artifacts={name: digest(path / name) for name in ("run.meta", "frames.fhash", "audio.ahash")})
            receipt.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def check_receipt(path, evidence, seen):
    receipt = json_bytes(history.artifact(path / "retirement-run.json", seen))
    require(receipt["schema"] == RUN_SCHEMA, "unsupported battery receipt")
    for key, record in (("binary_sha256", "binary"), ("source_sha256", "sources")):
        require(receipt[key] == evidence.build[record]["sha256"], "battery build receipt mismatch: " + str(path))
    require(set(receipt["artifacts"]) == {"run.meta", "frames.fhash", "audio.ahash"}, "incomplete battery receipt")
    for name, expected in receipt["artifacts"].items():
        require(digest(path / name) == expected, "battery artifact changed after collection: " + str(path / name))
    return digest(path / "retirement-run.json")


def read_pairing(evidence, baseline, leg, identity):
    spec = leg_spec.read_leg(str(baseline), leg)
    if not spec or spec.get("pair_edge") is None:
        require(leg not in evidence.manifest.get("pairings", {}), "unexpected pairing for " + leg)
        return None
    proof = evidence.manifest["pairings"][leg]
    require(proof["binary_sha256"] == evidence.build["binary"]["sha256"] and
            proof["source_sha256"] == evidence.build["sources"]["sha256"],
            "scene census build fingerprint mismatch: " + leg)
    pairing = scene_pairing.parse_pairing(history.artifact(evidence.file(proof["record"])))
    old, reference = spec["pair_edge"]
    require(pairing["status"] == "EDGE-FOUND" and pairing["method"] == "image-census" and
            (pairing["old_edge"], pairing["ref_old"], pairing["ref_new"]) == (old, old, reference) and
            pairing["ref_offset"] == reference - old and
            pairing["pair_map"] == scene_pairing.pair_map_text(spec, pairing["offset"]) and
            0 < pairing["new_edge"] < pairing["census_frames"] <= spec["frames"],
            "invalid measured scene pairing: " + leg)
    meta_path = evidence.file(proof["run_meta"])
    meta = history.metadata(meta_path.parent, name=meta_path.name)
    require(history.identity_fields(meta) == identity and meta.get("finalized") == "1" and
            meta.get("frames") == str(pairing["census_frames"]) and meta.get("park") == "none" and
            meta.get("park_detail") == "frames_cap", "scene census run identity/completion mismatch: " + leg)
    keys = clock_pair.read_keys(str(Path(baseline) / leg / "SCENE_KEYS.tsv"))
    # Keep just the two raw RAM captures. Full PS2 device snapshots and the
    # historical 29 memory probes are not necessary for this comparison edge.
    before = clock_pair.words(str(evidence.file(proof["before"])), keys)
    after = clock_pair.words(str(evidence.file(proof["after"])), keys)
    require((before["case"], after["case"]) == (6, 7), "scene edge raw captures disagree: " + leg)
    require(proof["before_boundary"] == pairing["new_edge"] - 1 and
            proof["after_boundary"] == pairing["new_edge"], "scene edge captures not adjacent: " + leg)
    require(before["vsync_frame"] == proof["before_boundary"] and after["vsync_frame"] == proof["after_boundary"],
            "scene capture boundary label/header mismatch: " + leg)
    require(pairing["offset"] == pairing["new_edge"] - old and
            pairing["deviation"] == pairing["offset"] - pairing["ref_offset"], "scene offset inconsistent: " + leg)
    words = pairing["edge_words"]
    for boundary, observed in ((proof["before_boundary"], before), (proof["after_boundary"], after)):
        expected = "b%d:case%d/bit63=%d/clock%d" % (boundary, observed["case"], observed["bit63"], observed["clock"])
        require(expected in words.split(";"), "scene edge words not supported by capture: " + leg)
    require(pairing["edge2_status"] in ("n/a", "PREDICTED"),
            "DT-12 accepts only unclaimed/predicted second edges; VERIFIED needs an additional reviewed proof")
    if pairing["edge2_status"] == "PREDICTED":
        require(pairing["edge2_old"] is not None and pairing["edge2_new"] == pairing["edge2_old"] + pairing["offset"],
                "inconsistent predicted second edge")
    return pairing


def check_battery(evidence, ref, battery, baseline):
    legs, root = ref["legs"], Path(battery)
    require(len(legs) == 13 and set(legs) == set(history.baseline_legs(str(baseline))),
            "reference/baseline must contain the same 13 scenarios")
    require(all(re.fullmatch(r"[A-Za-z0-9_-]+", leg) for leg in legs), "invalid scenario name")
    expected = {"%s.rep%d" % (leg, rep) for leg in legs for rep in (1, 2, 3)}
    found = {path.name for path in root.iterdir() if path.is_dir() or re.fullmatch(r".+\.rep\d+", path.name)}
    require(found == expected, "battery repetition set differs: missing=%s unexpected=%s" %
            (sorted(expected - found), sorted(found - expected)))
    require(set(evidence.manifest.get("pairings", {})) <= set(legs), "unknown scene pairing leg")
    seen, identity, runs, rows, failures, memory, receipts = set(), None, {}, [], [], {}, {}
    for leg in sorted(legs):
        contract = history.run_contract(baseline, leg)
        replicas = []
        for rep in (1, 2, 3):
            name = "%s.rep%d" % (leg, rep)
            path = root / name
            meta, hashes = history.read_run(path, contract, seen)
            receipts[name] = check_receipt(path, evidence, seen)
            fields = history.identity_fields(meta)
            if identity is None:
                identity = fields
            require(identity == fields, "build/config identity differs across battery: " + name)
            for field, record in (("binary_sha256", "binary"), ("source_sha256", "sources")):
                if field in meta:
                    require(meta[field] == evidence.build[record]["sha256"], "runtime digest disagrees: " + name)
            runs[name] = meta
            replicas.append(hashes)
        stable = replicas[0] == replicas[1] == replicas[2]
        if not stable:
            failures.append(leg + ": frames/audio repetition mismatch")
        pairing = read_pairing(evidence, baseline, leg, identity)
        row = {"leg": leg, "stable": stable, "mem": "advisory"}
        for lane in regression_ref.DECIDING_LANES:
            key = history.actual_key(Path(baseline) / leg, root / (leg + ".rep1"), lane, pairing)
            reference = ref["legs"][leg][lane + "_first_div"]
            row[lane] = "%s -> %s" % (reference, key)
            if regression_ref.key_rank(key) < regression_ref.key_rank(reference):
                failures.append("%s: %s first-divergent key %s < reference %s" % (leg, lane, key, reference))
        try:
            values = [history.artifact(root / ("%s.rep%d/mem.mhash" % (leg, rep))) for rep in (1, 2, 3)]
            memory[leg] = {"repetition_identity": values[0] == values[1] == values[2]}
        except (OSError, ValueError) as error:
            memory[leg] = {"unavailable": str(error)}
        rows.append(row)
    return runs, rows, failures, {"advisory": True, "legs": memory}, receipts


def difference_ranges(left, right):
    """All differing byte intervals, including missing tails, with no tolerance."""
    intervals, begin = [], None
    for index in range(max(len(left), len(right))):
        different = index >= min(len(left), len(right)) or left[index] != right[index]
        if different and begin is None:
            begin = index
        if not different and begin is not None:
            intervals.append([begin, index])
            begin = None
    if begin is not None:
        intervals.append([begin, max(len(left), len(right))])
    return intervals


def check_oracle(evidence, oracle, replacement, results, inventory, seen):
    ident, test = text(oracle["id"], "oracle id"), text(oracle["test"], "oracle test")
    require(results.get(test) == "Passed", "self-oracle test did not run/pass in selected scope: " + test)
    executable = evidence.file(oracle["executable"])
    command = retirement_scope.inventory_tests(inventory)[test]["command"]
    require(Path(command[0]).resolve() == executable.resolve(), "oracle executable not the selected CTest command: " + ident)
    sides, channels = {}, None
    for mode in ("model", "native"):
        side = oracle[mode]
        input_path = evidence.file(side["input"])
        require(input_path.stat().st_size > 0, "empty self-oracle input: " + ident)
        meta_path = evidence.file(side["receipt"])
        receipt = json_bytes(history.artifact(meta_path, seen))
        require(receipt["schema"] == ORACLE_SCHEMA and receipt["mode"] == mode and receipt["test"] == test,
                "oracle receipt identity mismatch: " + ident)
        require(receipt["command"] == command, "oracle receipt does not match selected test command: " + ident)
        require(receipt["exit_code"] == 0 and type(receipt["exit_code"]) is int and receipt["completed"] is True,
                "incomplete oracle execution: " + ident)
        require(receipt["binary_sha256"] == oracle["executable"]["sha256"] and
                receipt["source_sha256"] == evidence.build["sources"]["sha256"] and
                receipt["input_sha256"] == side["input"]["sha256"], "oracle build/input mismatch: " + ident)
        toggle = replacement["toggle"]
        settings = receipt["settings"]
        require(isinstance(settings, dict) and settings.get(toggle["name"]) == toggle[mode],
                "oracle toggle did not select " + mode + ": " + ident)
        outputs = {}
        require(side["outputs"], "no oracle outputs: " + ident)
        for channel, record in side["outputs"].items():
            text(channel, "output channel")
            file = evidence.file(record)
            info = file.stat()
            key = info.st_dev, info.st_ino
            require(key not in seen, "oracle output aliases another side/case: " + str(file))
            seen.add(key)
            outputs[channel] = file.read_bytes()
        require(any(outputs.values()), "oracle produced no observable output: " + ident)
        require(receipt["outputs"] == {name: record["sha256"] for name, record in side["outputs"].items()},
                "oracle receipt output mismatch: " + ident)
        require(channels is None or set(outputs) == channels, "oracle channel sets differ: " + ident)
        channels = set(outputs)
        sides[mode] = (receipt, outputs)
    require(oracle["model"]["input"]["sha256"] == oracle["native"]["input"]["sha256"],
            "self-oracle inputs differ: " + ident)
    model, native = sides["model"][0], sides["native"][0]
    toggle_name = replacement["toggle"]["name"]
    require({k: v for k, v in model["settings"].items() if k != toggle_name} ==
            {k: v for k, v in native["settings"].items() if k != toggle_name},
            "oracle settings differ beyond replacement toggle: " + ident)
    model_work = {}
    for name in replacement["model_counters"]:
        selector = evidence.model_selector(name)
        model_work[name] = counter(model["counters"], selector)
        require(counter(native["counters"], selector) == 0, "native oracle still executes " + name)
    require(counter(native["counters"], replacement["native_counter"]) > 0,
            "native oracle has no completed replacement work: " + ident)
    require(counter(model["counters"], replacement["native_counter"]) == 0,
            "model oracle executed replacement too: " + ident)
    differences = {name: difference_ranges(sides["model"][1][name], sides["native"][1][name])
                   for name in sorted(channels)}
    differences = {name: spans for name, spans in differences.items() if spans}
    attributions = oracle.get("attributions", {})
    require(set(attributions) == set(differences), "unattributed or spurious oracle differences: " + ident)
    for channel, spans in differences.items():
        attribution = attributions[channel]
        require(attribution["ranges"] == spans, "attribution does not cover exact differing bytes: " + ident)
        text(attribution["reason"], "difference reason")
        evidence.reference(attribution["specification"], "decomp")
        require(evidence.file(attribution["expected"]).read_bytes() == sides["native"][1][channel],
                "native result does not match attributed expected output: " + ident)
        require(results.get(attribution["pin"]) == "Passed", "attribution pin did not pass: " + ident)
    return {"id": ident, "test": test, "differences": differences, "model_work": model_work,
            "channels": sorted(channels), "input_sha256": oracle["native"]["input"]["sha256"]}


def check_replacements(evidence, runs, results, inventory):
    replacements, oracles = evidence.manifest["replacements"], evidence.manifest["oracles"]
    require(isinstance(replacements, list) and replacements and isinstance(oracles, list) and oracles,
            "retirement requires replacements and same-input self-oracles")
    ids, oracle_ids, all_counters, used_oracles, seen, report = set(), set(), set(), set(), set(), []
    by_id = {}
    for oracle in oracles:
        ident = text(oracle["id"], "oracle id")
        require(ident not in oracle_ids, "duplicate oracle id: " + ident)
        oracle_ids.add(ident)
        by_id[ident] = oracle
    for replacement in replacements:
        ident = text(replacement["id"], "replacement id")
        require(ident not in ids, "duplicate replacement id " + ident)
        ids.add(ident)
        text(replacement["intent"], "source intent")
        require(replacement["source_intents"], "replacement has no source-intent references: " + ident)
        for reference in replacement["source_intents"]:
            evidence.reference(reference, "decomp")
        toggle = replacement["toggle"]
        text(toggle["name"], "toggle name")
        require(type(toggle["model"]) is str and type(toggle["native"]) is str and
                toggle["model"] != toggle["native"] and toggle["default"] == "native",
                "retirement requires distinct toggle values and native default: " + ident)
        evidence.reference(toggle["source"], "implementation")
        model_counters = replacement["model_counters"]
        require(isinstance(model_counters, list) and model_counters and
                len(set(model_counters)) == len(model_counters), "missing/duplicate model counters: " + ident)
        require(not all_counters.intersection(model_counters), "model counter claimed by multiple replacements: " + ident)
        all_counters.update(model_counters)
        selectors = {name: evidence.model_selector(name) for name in model_counters}
        activity = replacement["native_counter"]
        evidence.reference(activity["source"], "implementation")
        require(all((activity["field"], activity.get("member")) != (c["field"], c.get("member"))
                    for c in selectors.values()), "native activity reuses a model counter")
        native_counts = {}
        for run, meta in runs.items():
            for name, selector in selectors.items():
                require(counter(meta, selector) == 0, "%s: replaced model %s executed" % (run, name))
            native_counts[run] = counter(meta, activity)
        require(sum(native_counts.values()) > 0, "zero-only census without replacement work: " + ident)
        # Repeated playback must also reproduce which path did the work.
        for leg in {run.rsplit(".rep", 1)[0] for run in runs}:
            require(len({native_counts["%s.rep%d" % (leg, rep)] for rep in (1, 2, 3)}) == 1,
                    "unstable replacement work census: " + leg + "/" + ident)
        chosen = replacement["oracles"]
        require(isinstance(chosen, list) and chosen and len(chosen) == len(set(chosen)),
                "missing/duplicate replacement oracle ids")
        require(not used_oracles.intersection(chosen), "oracle reused by multiple replacements")
        compared = [check_oracle(evidence, by_id[name], replacement, results, inventory, seen) for name in chosen]
        for name in model_counters:
            require(sum(item["model_work"][name] for item in compared) > 0, "model oracles never exercised " + name)
        used_oracles.update(chosen)
        bugs = replacement["old_bug_pins"]
        require(isinstance(bugs, list) and bugs, "no OLD-bug coverage: " + ident)
        bug_ids = set()
        for bug in bugs:
            bug_id = text(bug["id"], "OLD bug id")
            require(bug_id not in bug_ids, "duplicate OLD-bug pin: " + bug_id)
            bug_ids.add(bug_id)
            evidence.reference(bug["record"], "old")
            require(bug["oracle"] in chosen and by_id[bug["oracle"]]["test"] == bug["test"] and
                    results.get(bug["test"]) == "Passed", "OLD-bug pin lacks executed replacement oracle: " + bug_id)
        report.append({"id": ident, "intent": replacement["intent"],
                       "retired_operations": {name: c["scope"] for name, c in selectors.items()},
                       "model_counts": {run: {name: 0 for name in selectors} for run in runs},
                       "native_work": native_counts,
                       "unexercised_runs": sorted(run for run, value in native_counts.items() if not value),
                       "oracles": compared, "old_bug_pins": sorted(bug_ids)})
    require(used_oracles == oracle_ids, "unconsumed oracle evidence")
    return report


def evaluate(evidence_path, ref_path, battery, ctest_log, inventory_path):
    evidence = Evidence(evidence_path)
    ctest = evidence.manifest["ctest"]
    require(evidence.file(ctest["log"]).resolve() == Path(ctest_log).resolve(), "manifest names another CTest log")
    require(evidence.file(ctest["inventory"]).resolve() == Path(inventory_path).resolve(),
            "manifest names another CTest inventory")
    require(ctest["source_sha256"] == evidence.build["sources"]["sha256"],
            "CTest source identity differs from candidate")
    inventory = evidence.load(ctest["inventory"])
    results, selected = ctest_results(history.artifact(Path(ctest_log)), inventory)
    ref = regression_ref.read_ref(str(ref_path))
    runs, rows, failures, memory, receipts = check_battery(evidence, ref, battery, Path(ref_path).parent)
    replacements = check_replacements(evidence, runs, results, inventory)
    # Hash again: changing the binary or sources during validation invalidates it.
    require(build_fingerprint(evidence_path) == {
        "binary_sha256": evidence.build["binary"]["sha256"], "source_sha256": evidence.build["sources"]["sha256"]},
        "build changed during validation")
    return {"policy": "DT-12", "verdict": "FAIL" if failures else "PASS", "failures": failures,
            "legs": rows, "memory": memory, "replacements": replacements, "ctest_scope": selected,
            "evidence": {"manifest_sha256": digest(Path(evidence_path)), "reference_sha256": digest(Path(ref_path)),
                         "files": evidence.files, "battery_receipts": receipts},
            "dt11_history": {"acceptance": False, "pcsx2_pins": "excluded; not read or required",
                             "memory_probes": "optional diagnostics; missing snapshots do not block"},
            "provenance_limit": "Producer/build fingerprints are supplied by the trusted parent collector; "
                                "digests detect mutation and bind evidence, not malicious fabrication."}
