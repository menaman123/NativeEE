#!/usr/bin/env python3
"""Report DMA/IPU/IOP coverage from completed runtime legs.

Zero hits mean only "not observed in these runs". They do not establish that a
path is unreachable in the executable or throughout a complete playthrough.
Missing instrumentation, unfinished runs, and observed unsupported operations
fail the check instead of being interpreted as zero hits.
"""
import argparse
import json
import tempfile
from pathlib import Path


GROUPS = {
    "dma_spr": (
        "from_normal", "from_chain", "from_interleave",
        "to_normal", "to_chain", "to_interleave", "unsupported", "faults",
        "unsupported_ctrl_writes",
    ),
    "ipu_commands": (
        "bclr", "idec", "bdec", "vdec", "fdec",
        "setiq", "setvq", "csc", "pack", "setth", "unknown",
    ),
    "iop_thread_ops": (
        "priority_calls", "priority_changes", "priority_errors",
        "delay_kernel", "delay_host", "injected_wake_errors", "injected_signal_errors",
    ),
    "ipu": ("divergent", "unhandled_writes", "dmac_face_oor"),
    "fmv": ("pictures", "raw8", "served_total"),
}
GROUPS["ipu_unsupported"] = GROUPS["ipu_commands"]
UNSUPPORTED = (
    "dma_spr.unsupported", "dma_spr.faults", "dma_spr.unsupported_ctrl_writes",
    "ipu_unmodelled",
    "iop_thread_ops.delay_host", "ipu.unhandled_writes", "ipu.dmac_face_oor",
    "unhandled_mmio", "iop_thread_ops.injected_wake_errors",
    "iop_thread_ops.injected_signal_errors", "ipu_dma_snapshot_errors",
    "fmv.raw8",  # Retail continuation after the RAW8 HLE remains unvalidated.
)


def read_meta(path):
    result = {}
    for line in path.read_text().splitlines():
        key, sep, value = line.partition("=")
        if sep:
            if key in result:
                raise ValueError("duplicate metadata key: " + key)
            result[key] = value
    return result


def natural(value, name):
    if not value or not value.isascii() or not value.isdecimal():
        raise ValueError(name + " must be a nonnegative integer")
    return int(value)


def inspect(meta):
    """Return counters and errors; no missing field is silently defaulted."""
    counts, errors = {}, []
    if meta.get("subsystem_census") != "1":
        errors.append("missing or unsupported subsystem_census version")
    if meta.get("finalized") != "1":
        errors.append("run did not finalize")
    if meta.get("park") != "none":
        errors.append("guest parked or park metadata is missing")
    try:
        if natural(meta.get("frames", ""), "frames") == 0:
            errors.append("run presented no frames")
        for group, required in GROUPS.items():
            values = {}
            for field in meta.get(group, "").split(","):
                key, sep, value = field.partition("=")
                if not sep or key in values:
                    raise ValueError("missing, malformed, or duplicate " + group + " field")
                values[key] = natural(value, group + "." + key)
            missing = set(required) - values.keys()
            if missing:
                raise ValueError(group + " missing fields: " + ", ".join(sorted(missing)))
            counts.update((group + "." + key, value) for key, value in values.items())
        counts["ipu_unmodelled"] = natural(meta.get("ipu_unmodelled", ""), "ipu_unmodelled")
        counts["unhandled_mmio"] = natural(meta.get("unhandled_mmio", ""), "unhandled_mmio")
        counts["ipu_dma_snapshot_errors"] = natural(
            meta.get("ipu_dma_snapshot_errors", ""), "ipu_dma_snapshot_errors")
        total = sum(counts["ipu_unsupported." + name] for name in GROUPS["ipu_commands"])
        if total != counts["ipu_unmodelled"] or total != counts["ipu.divergent"]:
            errors.append("IPU unsupported total does not match its per-command counters")
        for name in GROUPS["ipu_commands"]:
            if counts["ipu_unsupported." + name] > counts["ipu_commands." + name]:
                errors.append("IPU unsupported count exceeds issued count: " + name)
    except ValueError as error:
        errors.append(str(error))
    for key in UNSUPPORTED:
        if counts.get(key, 0):
            errors.append("%s reached %d time(s)" % (key, counts[key]))
    return counts, errors


def report(paths, expected=None):
    totals, runs, failures = {}, [], []
    if not paths:
        failures.append("no run.meta files found")
    if expected is not None and len(paths) != expected:
        failures.append("expected %d runs, found %d" % (expected, len(paths)))
    for path in paths:
        try:
            meta = read_meta(path)
            counts, errors = inspect(meta)
        except (OSError, UnicodeError, ValueError) as error:
            counts, errors = {}, [str(error)]
        runs.append({"path": str(path), "counters": counts, "errors": errors})
        failures.extend(str(path) + ": " + error for error in errors)
        for key, value in counts.items():
            totals[key] = totals.get(key, 0) + value
    return {
        "pass": not failures,
        "scope": "Observed runs only; zero hits do not prove whole-game unreachability.",
        "run_count": len(paths),
        "totals": totals,
        "runs": runs,
        "errors": failures,
    }


def selftest():
    meta = {"subsystem_census": "1", "finalized": "1", "park": "none",
            "frames": "200", "ipu_unmodelled": "0", "unhandled_mmio": "0",
            "ipu_dma_snapshot_errors": "0"}
    for group, keys in GROUPS.items():
        meta[group] = ",".join(key + "=0" for key in keys)
    assert not inspect(meta)[1]
    for key in ("subsystem_census", "dma_spr", "ipu_commands", "ipu_unsupported", "iop_thread_ops",
                "ipu", "fmv", "ipu_unmodelled", "unhandled_mmio", "ipu_dma_snapshot_errors",
                "finalized", "park", "frames"):
        bad = dict(meta)
        del bad[key]
        assert inspect(bad)[1], key
    for key, value in (("ipu_unmodelled", "1"), ("ipu_unmodelled", "-1"),
                       ("ipu_unmodelled", "x"), ("frames", "0"),
                       ("finalized", "0"), ("park", "sdk_unimplemented"),
                       ("unhandled_mmio", "1"), ("ipu_dma_snapshot_errors", "1")):
        assert inspect(dict(meta, **{key: value}))[1], (key, value)
    for key in ("unsupported", "faults", "unsupported_ctrl_writes"):
        bad = dict(meta)
        bad["dma_spr"] = bad["dma_spr"].replace(key + "=0", key + "=1")
        assert inspect(bad)[1], key
    for key in ("delay_host", "injected_wake_errors", "injected_signal_errors"):
        bad = dict(meta)
        bad["iop_thread_ops"] = bad["iop_thread_ops"].replace(key + "=0", key + "=1")
        assert inspect(bad)[1], key
    for key in GROUPS["ipu"]:
        bad = dict(meta)
        bad["ipu"] = bad["ipu"].replace(key + "=0", key + "=1")
        assert inspect(bad)[1], key
    bad = dict(meta)
    bad["fmv"] = bad["fmv"].replace("raw8=0", "raw8=1")
    assert inspect(bad)[1], "RAW8 retail continuation needs an integration pin"
    duplicate = dict(meta)
    duplicate["ipu_commands"] += ",bclr=0"
    assert inspect(duplicate)[1]
    hits = dict(meta)
    hits["ipu_commands"] = hits["ipu_commands"].replace("bclr=0", "bclr=7")
    assert inspect(hits)[0]["ipu_commands.bclr"] == 7
    bad = dict(meta)
    bad["ipu_unsupported"] = bad["ipu_unsupported"].replace("bdec=0", "bdec=1")
    assert inspect(bad)[1]
    assert not report([])["pass"]
    with tempfile.TemporaryDirectory(prefix="subsystem_reachability_") as temporary:
        path = Path(temporary) / "run.meta"
        text = "".join(key + "=" + value + "\n" for key, value in hits.items())
        path.write_text(text)
        result = report([path], expected=1)
        assert result["pass"] and result["totals"]["ipu_commands.bclr"] == 7
        assert not report([path], expected=2)["pass"]
        assert not report([path.parent / "missing.meta"])["pass"]
        path.write_text(text + "frames=200\n")
        assert not report([path])["pass"]
    print("subsystem_reachability selftest: PASS")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="*", type=Path, help="run.meta files or run directories")
    parser.add_argument("--expect-runs", type=int)
    parser.add_argument("--json", type=Path, help="write the detailed report")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    paths = set()
    for root in args.roots:
        if root.is_file():
            paths.add(root)
        elif root.is_dir():
            paths.update(root.rglob("run.meta"))
        else:
            paths.add(root)
    result = report(sorted(paths), args.expect_runs)
    if args.json:
        args.json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print("subsystem reachability: %s (%d runs)" %
          ("PASS" if result["pass"] else "FAIL", result["run_count"]))
    print(result["scope"])
    for key, value in sorted(result["totals"].items()):
        print("  %s=%d" % (key, value))
    for error in result["errors"]:
        print("  FAIL " + error)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
