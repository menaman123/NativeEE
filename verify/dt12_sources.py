#!/usr/bin/env python3
"""dt12_sources.py -- write or check the `dt12-sources-v1` source manifest.

  dt12_sources.py --root DIR --out FILE [--replacements JSON...] [FILE...]
  dt12_sources.py --check FILE
  dt12_sources.py --selftest

The source manifest is the file `PS2R_MOVIE_ORACLE_SOURCE_MANIFEST` names at
configure time (tests/recomp/movie_oracle_args.cmake) and `build.sources` of a
`dt12-retirement-v1` evidence manifest (verify/README.md): every oracle
receipt and battery run carries its sha256, and retirement_dt12.py requires
every listed file to still match before it binds an implementation reference
or a counter descriptor. This tool only enumerates and hashes files under one
absolute root; it never builds, runs or edits anything.

Files come from three places, unioned and sorted:
  * explicit FILE arguments (paths relative to --root);
  * --replacements JSON: for every entry of a `replacements` list, the relative
    `toggle.source.file.path`, `native_counter.source.file.path`, every
    `old_bug_pins[].record.file.path` and `source_intents[].file.path` that is
    RELATIVE (absolute decomp/OLD references stay outside the tree and are not
    listed), and for every `model_counters` name the reviewed counter
    descriptor's `source`, `publisher`, `catalog` and provider source from
    verify/retirement_counters.py (an unknown counter name is an error);
  * --check FILE re-hashes an existing manifest and exits 1 on any mismatch,
    printing each differing path (what retirement_dt12.py would refuse).

The manifest root is written as the absolute, resolved --root. Symlinks,
paths that escape the root and non-regular files are refused. The output is
canonical JSON (sorted keys, two-space indent, trailing newline) so the same
tree yields the same sha256.
"""
import argparse
import hashlib
import json
import os
import sys
import tempfile
from pathlib import Path

SCHEMA = "dt12-sources-v1"


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def relative_name(root, name):
    relative = Path(name)
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError("source path must be relative to the root and inside it: " + str(name))
    path = root / relative
    if path.is_symlink() or not path.is_file():
        raise ValueError("source is not a regular file under the root: " + str(name))
    return relative.as_posix()


def counter_files(names, counters, providers):
    files = set()
    for name in names:
        if name not in counters:
            raise ValueError("model counter is not in the reviewed catalog: " + str(name))
        descriptor = counters[name]
        for key in ("source", "publisher", "catalog"):
            if key in descriptor:
                files.add(descriptor[key])
        if "provider" in descriptor:
            files.add(providers[descriptor["provider"]]["source"])
    return files


def replacement_files(document, counters, providers):
    files = set()
    for replacement in document.get("replacements", []):
        for record in (replacement.get("toggle", {}).get("source"),
                       replacement.get("native_counter", {}).get("source")):
            if record:
                files.add(record["file"]["path"])
        for reference in replacement.get("source_intents", []):
            if not Path(reference["file"]["path"]).is_absolute():
                files.add(reference["file"]["path"])
        for pin in replacement.get("old_bug_pins", []):
            if not Path(pin["record"]["file"]["path"]).is_absolute():
                files.add(pin["record"]["file"]["path"])
        files |= counter_files(replacement.get("model_counters", []), counters, providers)
    return {name for name in files if not Path(name).is_absolute()}


def build(root, names):
    root = Path(root).resolve()
    if not root.is_absolute() or not root.is_dir():
        raise ValueError("root must be an existing absolute directory: " + str(root))
    files = {}
    for name in sorted({relative_name(root, name) for name in names}):
        files[name] = digest(root / name)
    if not files:
        raise ValueError("empty source manifest")
    return {"schema": SCHEMA, "root": str(root), "files": files}


def dump(manifest):
    return json.dumps(manifest, indent=2, sort_keys=True) + "\n"


def check(path):
    manifest = json.loads(Path(path).read_text())
    if manifest.get("schema") != SCHEMA:
        raise ValueError("unsupported source manifest schema")
    root = Path(manifest["root"])
    if not root.is_absolute():
        raise ValueError("source root must be absolute")
    mismatches = []
    for name, expected in manifest["files"].items():
        try:
            actual = digest(root / relative_name(root, name))
        except (ValueError, OSError) as error:
            mismatches.append((name, "unreadable: " + str(error)))
            continue
        if actual != expected:
            mismatches.append((name, "sha256 %s != manifest %s" % (actual, expected)))
    return mismatches


def load_catalog():
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from retirement_counters import COUNTERS, COUNT_PROVIDERS  # noqa: E402
    return COUNTERS, COUNT_PROVIDERS


def selftest():
    directory = Path(tempfile.mkdtemp(prefix="dt12_sources_selftest."))
    (directory / "src").mkdir()
    (directory / "src" / "a.cpp").write_text("int a;\n")
    (directory / "b.json").write_text("{}\n")
    (directory / "outside.txt").write_text("x\n")
    os.symlink(directory / "src" / "a.cpp", directory / "link.cpp")
    counters = {"X.model": {"source": "src/a.cpp", "publisher": "b.json", "catalog": "b.json",
                            "provider": "P"}}
    providers = {"P": {"source": "src/a.cpp"}}
    proposal = {"replacements": [{
        "toggle": {"source": {"file": {"path": "b.json"}}},
        "native_counter": {"source": {"file": {"path": "src/a.cpp"}}},
        "source_intents": [{"file": {"path": "/absolute/decomp/file.cpp"}}],
        "old_bug_pins": [{"record": {"file": {"path": "/absolute/old/record.md"}}}],
        "model_counters": ["X.model"]}]}
    names = replacement_files(proposal, counters, providers)
    assert names == {"src/a.cpp", "b.json"}, names
    manifest = build(directory, names)
    assert manifest["schema"] == SCHEMA and manifest["root"] == str(directory.resolve())
    assert list(manifest["files"]) == ["b.json", "src/a.cpp"], manifest["files"]
    assert manifest["files"]["src/a.cpp"] == hashlib.sha256(b"int a;\n").hexdigest()
    text = dump(manifest)
    assert text == dump(json.loads(text)) and text.endswith("\n")
    out = directory / "sources.json"
    out.write_text(text)
    assert check(out) == []
    (directory / "src" / "a.cpp").write_text("int a = 1;\n")
    mismatches = check(out)
    assert [name for name, _ in mismatches] == ["src/a.cpp"], mismatches
    for bad in ("../outside.txt", "/abs/path", "link.cpp", "src", "missing.cpp", ""):
        try:
            relative_name(directory, bad)
        except ValueError:
            pass
        else:
            raise AssertionError("accepted " + repr(bad))
    try:
        replacement_files({"replacements": [{"model_counters": ["unknown"]}]}, counters, providers)
    except ValueError:
        pass
    else:
        raise AssertionError("accepted an unreviewed counter")
    try:
        build(directory, [])
    except ValueError:
        pass
    else:
        raise AssertionError("accepted an empty manifest")
    print("dt12_sources selftest: PASS")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*", help="source paths relative to --root")
    parser.add_argument("--root", help="absolute source tree root (the worktree being built)")
    parser.add_argument("--out", help="manifest path to write")
    parser.add_argument("--replacements", action="append", default=[],
                        help="proposed replacements JSON whose relative references and counters are included")
    parser.add_argument("--check", help="re-hash an existing manifest; exit 1 on any mismatch")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if args.check:
        mismatches = check(args.check)
        for name, why in mismatches:
            print("MISMATCH %s: %s" % (name, why))
        print("dt12_sources check: %s (%d files, %d mismatches)" % (
            "FAIL" if mismatches else "PASS", len(json.loads(Path(args.check).read_text())["files"]), len(mismatches)))
        return 1 if mismatches else 0
    if not args.root or not args.out:
        parser.error("--root and --out are required to write a manifest")
    names = set(args.files)
    if args.replacements:
        counters, providers = load_catalog()
        for path in args.replacements:
            names |= replacement_files(json.loads(Path(path).read_text()), counters, providers)
    manifest = build(args.root, names)
    text = dump(manifest)
    Path(args.out).write_text(text)
    print("dt12_sources: wrote %s (%d files, sha256 %s)" % (
        args.out, len(manifest["files"]), hashlib.sha256(text.encode()).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
