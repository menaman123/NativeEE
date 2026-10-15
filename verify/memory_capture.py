"""Inspect game memory without requiring a resumable device snapshot."""
from pathlib import Path
import re

import state_hash


def boundaries(directory):
    found = set()
    for entry in Path(directory).iterdir():
        match = re.fullmatch(r"(?:state_b(\d+)\.ps2st|memory_b(\d+)\.bin)", entry.name)
        if match:
            found.add(int(match.group(1) or match.group(2)))
    return sorted(found)


def remove(directory, boundary):
    for name in ("memory_b%d.bin", "state_b%d.ps2st", "state_b%d.refused"):
        (Path(directory) / (name % boundary)).unlink(missing_ok=True)


def path(directory, boundary):
    """Prefer the bytes captured before save preparation; support older artifacts."""
    raw = Path(directory) / ("memory_b%d.bin" % boundary)
    if raw.is_file():
        if raw.stat().st_size != 32 * 1024 * 1024:
            raise ValueError("incomplete memory capture: %s" % raw)
        return str(raw)
    state = Path(directory) / ("state_b%d.ps2st" % boundary)
    if state.is_file():
        return str(state)
    raise FileNotFoundError("no memory capture for boundary %d in %s" % (boundary, directory))


def read(directory, boundary):
    source = path(directory, boundary)
    if source.endswith(".bin"):
        return Path(source).read_bytes()
    _, _, sections = state_hash.parse(source)
    return bytes(sections[state_hash.TAG_RAM][2])
