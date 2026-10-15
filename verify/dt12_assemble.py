#!/usr/bin/env python3
"""dt12_assemble.py -- assemble a dt12-retirement-v1 evidence manifest from an
owner's proposed replacements file and the exported dt12-oracle-run-v1 pairs,
then preflight everything retirement_dt12.py can check without a battery.

  dt12_assemble.py --proposal JSON --oracles DIR --sources sources.json
                   --binary PATH --ctest-inventory JSON --ctest-log LOG
                   --decomp DIR --old DIR --out evidence.json
                   [--default KEY=VALUE]... [--preflight-results LOG]
  dt12_assemble.py --selftest

Inputs are the parent's or an owner's frozen artifacts; nothing is built or run.
  * --proposal: the `replacements`/`oracles` skeleton (e.g. the Graphics
    integration/dt12-graphics-replacements.json). Every `"sha256": "<fill>"`
    is replaced by the digest of the named file (relative paths are resolved
    against the source manifest's root; absolute decomp/OLD paths as-is).
    `toggle.default` is written from --default KEY=VALUE when given, else from
    the `peak` column of docs/filemap/settings_table.tsv under the sources
    root (the registry of the committed default). It is NEVER invented: the
    gate requires "native", and that is true only after the parent flips it.
    Proposal-only annotation members (`*_note`, `assertion`, `_comment`,
    `schema_note`, `not_yet_exportable`) are dropped from the output.
  * --oracles: the PS2R_MOVIE_ORACLE_EXPORT_ROOT the oracle tests wrote;
    each `oracles[].id` names `<root>/<test>/artifacts.json`, whose `arms`
    (`model`/`native`: input, receipt, outputs) and `attributions` become the
    oracle entry, with paths rewritten relative to the evidence file.
  * --ctest-inventory: the unfiltered `ctest --show-only=json-v1` of the SAME
    build; the oracle `executable` is that test's command[0] (hashed).
  * --binary, --ctest-log, --decomp, --old: the remaining artifact fields.

Preflight (printed, exit 1 on the first failure): Evidence() loads (every
artifact and source digest verified), every decomp/OLD/implementation
reference needle is found inside its authority, every model counter is a
reviewed catalog descriptor whose increment/publish needles are present, and
check_oracle() recomputes each exported pair (same input digest, zero native
model work, positive native replacement work, byte-identical channels or
exactly attributed differences). The ctest results fed to check_oracle come
from --preflight-results (a ctest log of the oracle tests from this build;
default --ctest-log) and cover only the oracle tests -- the full DT-12 scope,
the battery legs and the native default are the parent's gate, so a
preflight PASS is NOT acceptance and says so.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

FILL = "<fill>"
ANNOTATIONS = ("_comment", "schema_note", "not_yet_exportable")
RESULT_LINE = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: (\S+) \.+\s*(Passed|\*\*\*Failed|\*\*\*Not Run|\*\*\*Skipped|\*\*\*Timeout)")


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def settings_defaults(source_root):
    table = Path(source_root) / "docs/filemap/settings_table.tsv"
    defaults = {}
    for line in table.read_text().splitlines():
        if not line or line.startswith("#") or line.startswith("key\t"):
            continue
        key, _type, peak = line.split("\t")[:3]
        defaults[key] = peak
    return defaults


def record(path, evidence_root):
    """An artifact reference relative to the evidence file when possible."""
    path = Path(path).resolve()
    try:
        shown = os.path.relpath(path, Path(evidence_root).resolve())
        if shown.startswith(".."):
            shown = str(path)
    except ValueError:
        shown = str(path)
    return {"path": shown, "sha256": digest(path)}


def fill(node, source_root, evidence_root, drop_notes):
    """Replace every <fill> file record; relative paths name sources under the
    manifest root and are rewritten relative to the evidence file, which is
    how retirement_dt12.Evidence.file resolves them."""
    if isinstance(node, dict):
        out = {}
        for key, value in node.items():
            if drop_notes and (key.endswith("_note") or key == "assertion"):
                continue
            out[key] = fill(value, source_root, evidence_root, drop_notes)
        if set(out) == {"path", "sha256"} and out["sha256"] == FILL:
            path = Path(out["path"])
            if not path.is_absolute():
                path = Path(source_root) / path
            if not path.is_file():
                raise ValueError("referenced file missing: " + str(path))
            out = record(path, evidence_root)
        return out
    if isinstance(node, list):
        return [fill(item, source_root, evidence_root, drop_notes) for item in node]
    return node


def oracle_entry(skeleton, oracles_root, inventory_tests, evidence_root):
    ident, test = skeleton["id"], skeleton["test"]
    directory = Path(oracles_root) / test
    index = json.loads((directory / "artifacts.json").read_text())
    if index.get("test") != test:
        raise ValueError("artifacts.json names another test: " + str(directory))
    command = inventory_tests[test]["command"]
    entry = {"id": ident, "test": test, "executable": record(command[0], evidence_root)}
    for mode in ("model", "native"):
        arm = index["arms"][mode]
        side = {"input": record(directory / arm["input"]["path"], evidence_root),
                "receipt": record(directory / arm["receipt"]["path"], evidence_root),
                "outputs": {channel: record(directory / value["path"], evidence_root)
                            for channel, value in arm["outputs"].items()}}
        for name, value in (("input", arm["input"]), ("receipt", arm["receipt"])):
            if side[name]["sha256"] != value["sha256"]:
                raise ValueError("%s %s %s digest differs from artifacts.json" % (ident, mode, name))
        entry[mode] = side
    attributions = {}
    for channel, value in index.get("attributions", {}).items():
        attributions[channel] = {
            "ranges": value["ranges"], "reason": value["reason"],
            "specification": {"file": record(value["specification"]["file"]["path"], evidence_root),
                              "needle": value["specification"]["needle"]},
            "expected": record(directory / value["expected"]["path"], evidence_root),
            "pin": value["pin"]}
    entry["attributions"] = attributions
    return entry


def assemble(args):
    import retirement_scope
    proposal = json.loads(Path(args.proposal).read_text())
    sources = json.loads(Path(args.sources).read_text())
    if sources.get("schema") != "dt12-sources-v1":
        raise ValueError("unsupported source manifest")
    source_root = Path(sources["root"])
    evidence_path = Path(args.out).resolve()
    evidence_root = evidence_path.parent
    inventory = json.loads(Path(args.ctest_inventory).read_text())
    tests = retirement_scope.inventory_tests(inventory)
    defaults = settings_defaults(source_root)
    overrides = dict(item.split("=", 1) for item in args.default)
    replacements = []
    for skeleton in proposal["replacements"]:
        replacement = fill({k: v for k, v in skeleton.items() if k not in ANNOTATIONS}, source_root,
                           evidence_root, True)
        name = replacement["toggle"]["name"]
        if name in overrides:
            replacement["toggle"]["default"] = overrides[name]
        elif name in defaults:
            replacement["toggle"]["default"] = defaults[name]
        else:
            raise ValueError("no committed default for toggle " + name)
        replacements.append(replacement)
    oracles = [oracle_entry(skeleton, args.oracles, tests, evidence_root) for skeleton in proposal["oracles"]]
    manifest = {
        "schema": "dt12-retirement-v1",
        "authorities": {"decomp": str(Path(args.decomp).resolve()), "old": str(Path(args.old).resolve())},
        "build": {"binary": record(args.binary, evidence_root), "sources": record(args.sources, evidence_root)},
        "ctest": {"inventory": record(args.ctest_inventory, evidence_root),
                  "log": record(args.ctest_log, evidence_root),
                  "source_sha256": digest(args.sources)},
        "replacements": replacements,
        "oracles": oracles,
    }
    evidence_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest, evidence_path


def oracle_results(log_path):
    results = {}
    for line in Path(log_path).read_text().splitlines():
        match = RESULT_LINE.match(line)
        if match:
            results[match.group(1)] = match.group(2)
    return results


def preflight(evidence_path, results_log):
    import retirement_dt12
    evidence = retirement_dt12.Evidence(evidence_path)
    print("preflight: evidence loaded; %d artifacts, %d sources verified" % (len(evidence.files), len(evidence.sources)))
    inventory = evidence.load(evidence.manifest["ctest"]["inventory"])
    results = oracle_results(results_log)
    manifest = evidence.manifest
    by_id = {oracle["id"]: oracle for oracle in manifest["oracles"]}
    seen, defaults_not_native = set(), []
    for replacement in manifest["replacements"]:
        ident = replacement["id"]
        for reference in replacement["source_intents"]:
            evidence.reference(reference, "decomp")
        evidence.reference(replacement["toggle"]["source"], "implementation")
        evidence.reference(replacement["native_counter"]["source"], "implementation")
        for name in replacement["model_counters"]:
            evidence.model_selector(name)
        for pin in replacement["old_bug_pins"]:
            evidence.reference(pin["record"], "old")
        if replacement["toggle"]["default"] != "native":
            defaults_not_native.append("%s %s=%s" % (ident, replacement["toggle"]["name"], replacement["toggle"]["default"]))
        for name in replacement["oracles"]:
            compared = retirement_dt12.check_oracle(evidence, by_id[name], replacement, results, inventory, seen)
            print("preflight: %s/%s channels=%s model_work=%s differences=%s" % (
                ident, name, ",".join(compared["channels"]),
                ",".join("%s=%d" % kv for kv in sorted(compared["model_work"].items())),
                json.dumps(compared["differences"]) if compared["differences"] else "none"))
    verdict = "preflight PASS (NOT acceptance: no battery, no full-scope CTest, defaults still %s)" % (
        "; ".join(defaults_not_native) if defaults_not_native else "native")
    print(verdict)
    return 0


def selftest():
    directory = Path(tempfile.mkdtemp(prefix="dt12_assemble_selftest."))
    root = directory / "tree"
    (root / "docs/filemap").mkdir(parents=True)
    (root / "docs/filemap/settings_table.tsv").write_text(
        "# header\nkey\ttype\tpeak\tdet\tsource_var\tledger\nhle.thing\tbool\toff\t=\t-\t-\n")
    (root / "impl.cpp").write_text("BOOL_FIELD(\"hle.thing\")\n")
    proposal = {"_comment": "x", "replacements": [{
        "id": "T", "intent": "i", "toggle": {"name": "hle.thing", "model": "off", "native": "on",
                                              "default": "<native after flip>",
                                              "source": {"file": {"path": "impl.cpp", "sha256": FILL},
                                                         "needle": "BOOL_FIELD"}},
        "model_counters_note": "drop me", "old_bug_pins": [{"id": "B", "assertion": "drop me too",
                                                             "record": {"file": {"path": str(root / "impl.cpp"),
                                                                                 "sha256": FILL}, "needle": "x"}}]}]}
    filled = fill({k: v for k, v in proposal["replacements"][0].items()}, root, directory, True)
    assert "model_counters_note" not in filled and "assertion" not in filled["old_bug_pins"][0]
    assert filled["toggle"]["source"]["file"] == {"path": "tree/impl.cpp", "sha256": digest(root / "impl.cpp")}
    assert filled["old_bug_pins"][0]["record"]["file"]["sha256"] == digest(root / "impl.cpp")
    assert settings_defaults(root) == {"hle.thing": "off"}
    rec = record(root / "impl.cpp", directory)
    assert rec["path"] == "tree/impl.cpp" and rec["sha256"] == digest(root / "impl.cpp")
    outside = record(root / "impl.cpp", directory / "elsewhere/deeper")
    assert outside["path"].startswith("..") or Path(outside["path"]).is_absolute()
    log = directory / "ctest.log"
    log.write_text("1/2 Test #5: AlphaOracle ......   Passed    1.00 sec\n"
                   "2/2 Test #6: BetaOracle .......***Failed    1.00 sec\n100% tests passed\n")
    assert oracle_results(log) == {"AlphaOracle": "Passed", "BetaOracle": "***Failed"}
    try:
        fill({"file": {"path": "missing.cpp", "sha256": FILL}}, root, directory, True)
    except ValueError:
        pass
    else:
        raise AssertionError("accepted a missing file")
    shutil.rmtree(directory)
    print("dt12_assemble selftest: PASS")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--proposal")
    parser.add_argument("--oracles")
    parser.add_argument("--sources")
    parser.add_argument("--binary")
    parser.add_argument("--ctest-inventory")
    parser.add_argument("--ctest-log")
    parser.add_argument("--decomp")
    parser.add_argument("--old")
    parser.add_argument("--out")
    parser.add_argument("--default", action="append", default=[], help="KEY=VALUE committed default to record")
    parser.add_argument("--preflight-results", help="ctest log holding the oracle tests' result lines (default --ctest-log)")
    parser.add_argument("--no-preflight", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    required = ("proposal", "oracles", "sources", "binary", "ctest_inventory", "ctest_log", "decomp", "old", "out")
    missing = [name for name in required if not getattr(args, name)]
    if missing:
        parser.error("missing: " + ", ".join("--" + name.replace("_", "-") for name in missing))
    manifest, path = assemble(args)
    print("dt12_assemble: wrote %s (%d replacements, %d oracles, sha256 %s)" % (
        path, len(manifest["replacements"]), len(manifest["oracles"]), digest(path)))
    if args.no_preflight:
        return 0
    try:
        return preflight(path, args.preflight_results or args.ctest_log)
    except (ValueError, KeyError) as error:
        print("preflight FAIL: %s" % error)
        return 1


if __name__ == "__main__":
    sys.exit(main())
