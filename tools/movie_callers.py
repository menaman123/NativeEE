#!/usr/bin/env python3
"""Inventory NEW Movie bodies and NTSC address references; never execute a game.

Direct calls and literal address words are evidence, not a proof that computed
indirect calls are absent. The decomp is a read-only specification; this script
does not copy or parse its implementation into generated code.
"""
import argparse
import hashlib
import json
import os
import re
import struct
from pathlib import Path


TITLE = Path("games/<title>")
ELF_SHA256 = "8d114879fb704d328394771aec6276136bdb4ecf4c5f8de06139b7b42974038f"
EXTERNAL = {
    0x19FCB8, 0x1A00D0, 0x1A0150, 0x2B70A8, 0x2B7B50, 0x2B7C00,
    0x2B7C10, 0x2B7C58, 0x2B7CB0, 0x2B80C0, 0x2B80E8, 0x2B8110, 0x2BED28,
    0x2C10A8, 0x2C1190, 0x2C12E0, 0x2C13C0,
}
VTABLE = {
    0x3068EC: 0x2AE318, 0x3068F4: 0x2AE658, 0x3068FC: 0x2AF500,
    0x306904: 0x2AEA08, 0x30690C: 0x2AEB30, 0x306914: 0x2AF8F0,
    0x30691C: 0x2AF968, 0x306924: 0x2AF9B8, 0x30692C: 0x2AFA00,
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def address(value):
    return f"0x{value:08x}"


def recomp_dir(root):
    """The generated recomp TUs: PS2R_RECOMP_DIR (the build directory), else the tree."""
    override = os.environ.get("PS2R_RECOMP_DIR")
    return Path(override) if override else root / TITLE / "recomp"


def wait_registration(root):
    path = TITLE / "recomp/recompiled.cpp"
    text = (recomp_dir(root) / "recompiled.cpp").read_text()
    start = text.index("    recompiled_functions[0x2af8f0] = [](CpuContext& ctx, uint32_t addr) {")
    end = text.index("\n    };", start) + len("\n    };")
    return path, text[start:end]


def generate(root, elf):
    data = elf.read_bytes()
    if sha(data) != ELF_SHA256 or data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Movie inventory requires the reviewed little-endian NTSC ELF")
    phoff = struct.unpack_from("<I", data, 28)[0]
    phsize, phcount = struct.unpack_from("<HH", data, 42)
    words = {}
    for index in range(phcount):
        kind, offset, virtual, _, size, _, _, _ = struct.unpack_from("<8I", data, phoff + index * phsize)
        if kind != 1:
            continue
        for at in range(0, size - 3, 4):
            words[virtual + at] = struct.unpack_from("<I", data, offset + at)[0]
    for at, target in VTABLE.items():
        if words.get(at) != target:
            raise ValueError("Movie vtable pin changed at " + address(at))
    bodies, incoming = {}, {}
    for path in sorted(recomp_dir(root).glob("recompiled_shard_*.cpp")):
        text = path.read_text()
        entries = list(re.finditer(r"^void FUN_([0-9a-f]{8})\(", text, re.M))
        for index, entry in enumerate(entries):
            start = int(entry[1], 16)
            limit = entries[index + 1].start() if index + 1 < len(entries) else len(text)
            end = text.find("\n// Function:", entry.start(), limit)
            body = text[entry.start():end if end >= 0 else limit].rstrip()
            calls = []
            for call in re.finditer(r"recomp_call\(\d+u, 0x([0-9a-f]+)u, ctx\)", body):
                target = int(call[1], 16)
                prefix = body[:call.start()]
                returns = re.findall(r"ctx\.cpuRegs\.GPR\.r\[31\]\.UL\[0\] = 0x([0-9a-f]+);", prefix)
                after = int(returns[-1], 16) if returns else None
                site = {"target": address(target), "return": address(after) if after else None}
                calls.append(site)
                incoming.setdefault(target, []).append({"caller": address(start), "return": site["return"]})
            tails = re.findall(r"^\s*FUN_([0-9a-f]{8})\(ctx, 0x[0-9a-f]+u\);", body, re.M)
            for target_text in tails:
                target = int(target_text, 16)
                calls.append({"target": address(target), "tail": True})
                incoming.setdefault(target, []).append({"caller": address(start), "tail": True})
            bodies[start] = {
                "entry": address(start), "source": str(TITLE / "recomp" / path.name),
                "body_sha256": sha(body.encode()), "calls": calls,
                "continuations": sorted(set(re.findall(r"case (0x[0-9a-f]+): goto", body))),
            }
    selected = {a for a in bodies if 0x2AE018 <= a <= 0x2B17B8} | EXTERNAL
    if selected - bodies.keys():
        raise ValueError("Missing generated boundaries: " + repr(selected - bodies.keys()))
    records = []
    for entry in sorted(selected):
        row = bodies[entry]
        row["direct_callers"] = incoming.get(entry, [])
        row["literal_address_words"] = [address(a) for a, value in sorted(words.items()) if value == entry]
        records.append(row)
    # The ABI points to the prelude that sets the waiting value, not its
    # following split body. Omitting the prelude would change that input.
    if {"target": "0x002af8f4", "tail": True} not in bodies[0x2AF8F0]["calls"]:
        raise ValueError("WaitFrame no longer has its exact generated prelude")
    wait_path, wait_body = wait_registration(root)
    for value in ("recompiled_functions.find(0x2b0fe0)", "recompiled_functions.find(0x19fcb8)",
                  "static_cast<int32_t>(0x2af940)", "static_cast<int32_t>(0x2af588)"):
        if value not in wait_body:
            raise ValueError("WaitFrame live registration changed; review its actual service ABI")
    registration_callers = incoming.get(0x2C1E10, [])
    if registration_callers != [{"caller": "0x002ae318", "return": "0x002ae350"},
                                {"caller": "0x002ae658", "return": "0x002ae6ac"}]:
        raise ValueError("Movie callback registration has another source caller")
    return {
        "schema": "movie-caller-inventory-v2",
        "scope": "all generated Movie player/decoder entries 0x2ae018..0x2b17b8 plus public MPEG/IPU/SPR/present boundaries",
        "limits": "literal words are candidates, not proof of reachability; computed indirect callers require source/vtable and runtime census",
        "elf_sha256": ELF_SHA256,
        "asset_inventory_sha256": sha((root / "docs/design/movie_asset_formats.json").read_bytes()),
        "vtable": [{"word": address(a), "entry": address(v)} for a, v in VTABLE.items()],
        "wait_frame": {
            "entry": "0x002af8f0", "tail_body": "0x002af8f4",
            "live_registration": {
                "source": str(wait_path), "body_sha256": sha(wait_body.encode()),
                "calls": [{"target": "0x002b0fe0", "return": "0x002af940", "limit": 2},
                          {"target": "0x0019fcb8", "return": "0x002af588", "count": 1}],
                "note": "NEW dispatch uses this wrapper, not the dormant shard spin/prelude.",
            },
        },
        "presentation_registration": {
            "sdk_entry": "0x002c1e10", "direct_callers": registration_callers,
            "callback": "0x002af568",
            "actual_model_counter": "libgraph_calls.syncvcb",
            "selection": "hle.movie_stop pairs direct Start with complete Stop and requires owned Step/Wait",
            "limits": "all NEW direct callers enumerated; computed calls still require the actual model census",
        },
        "bodies": records,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    text = json.dumps(generate(args.root.resolve(), args.elf), indent=2) + "\n"
    if args.check:
        if args.out.read_text() != text:
            raise SystemExit("Movie caller inventory differs")
    else:
        args.out.write_text(text)
    print("Movie caller inventory: verified" if args.check else "Movie caller inventory: written")


if __name__ == "__main__":
    main()
