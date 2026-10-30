#!/usr/bin/env python3
"""gen_settings_schema.py -- engine/core/config/settings_schema.json: the settings
file's schema for a settings UI (the launcher's Settings page, a web page, a
test), title-neutral, generated from the Config schema in machine form
(docs/filemap/settings_table.tsv, itself checked against the compiled Config by
Coretest_config_schema) plus the UI metadata below (label / help / kind /
choice labels / ranges) for the rows a player is shown.

    python3 tools/gen_settings_schema.py > engine/core/config/settings_schema.json
    python3 tools/gen_settings_schema.py --check engine/core/config/settings_schema.json
    python3 tools/gen_settings_schema.py --lint          # the UI-string lint alone

The JSON (schema 1):
  file            "nativeee.ini" -- the ONE settings file (ConfigStore)
  bindings_file   "bindings.ini" -- the controls file kept beside it
  legacy_files    the former files a first start converts and renames .migrated
  resolution      where the game looks for the file, in order
  grammar         how a line is written (the launcher's writer must match)
  settings[]      one entry per writable Config key (harness.* excluded), sorted:
      key, section, name, type (bool|int|float|str|enum), default (string form),
      kind (switch|choice|number|text|folder|file), user_facing (bool),
      and for a user-facing row: label, help, choices[{value,label}] (choice),
      min/max (number), hidden (a row the UI should not show yet).

A title's launcher manifest names rows by KEY only (its `settings.groups[].keys`);
label, help, kind and choices come from here. Every label / help / choice label
passes the no-codenames / no-developer-vocabulary lint (BANNED below): the file
is not written while one fails.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
TABLE = os.path.join(ROOT, "docs", "filemap", "settings_table.tsv")
SCHEMA_VERSION = 1

# Words a player must never read in a settings row. Matched on word boundaries,
# case-insensitive, over every label / help / choice label. Developer vocabulary,
# internal names and codenames alike.
BANNED = [
    "recompile", "recompiled", "recompiler", "recompilation", "shard", "shards", "aot", "cmake", "hle",
    "vu0", "vu1", "mtvu", "iop", "ee", "gs", "gif", "vif", "dmac", "spu2", "harness", "ps2r", "fingerprint",
    "lockstep", "codegen", "clang", "ninja", "ctest", "judge", "oracle", "charter", "deterministic",
    "savestate", "savestates", "nativeee", "sly", "crash", "twinsanity", "paris", "cairo", "prague",
    "elf", "iso9660", "sha", "sha-256", "ini", "cli", "argv", "sdl", "imgui", "qt", "metal", "opengl",
    "vsync", "mipmap", "anisotropic", "anisotropy", "bilinear", "trilinear", "interpolation", "backend",
    "config", "cfg", "env", "profile", "tier", "kick", "xgkick", "runahead", "tick", "bundle",
]
BANNED_RE = re.compile(r"(?<![a-z0-9])(" + "|".join(re.escape(w) for w in BANNED) + r")(?![a-z0-9])", re.IGNORECASE)

# key -> UI metadata. `kind` defaults from the type (bool -> switch, enum ->
# choice, int/float -> number, str -> text). `choices` maps enum value -> label
# (every enum value of the type must be named). `hidden` marks a row the UI
# should keep off the page for now (the key stays writable from the file).
UI = {
    # Picture
    "video.fullscreen": dict(label="Window", help="A window, the whole screen, or a borderless window covering the desktop.",
                             choices={"windowed": "Window", "fullscreen": "Full screen", "borderless": "Borderless"}),
    "video.aspect": dict(label="Aspect ratio", help="The shape the game draws for. Auto follows the window; 4:3 is the original television.",
                         choices={"auto": "Auto", "4:3": "4:3", "16:9": "16:9", "16:10": "16:10", "21:9": "21:9", "32:9": "32:9"}),
    "video.scale": dict(label="Picture scale", kind="choice", help="How many times the original resolution the game is drawn at. Window follows the window size.",
                        choices={"0": "Window", "1": "1x", "2": "2x", "3": "3x", "4": "4x"}),
    "video.vsync": dict(label="Wait for the display", help="Shows each picture in step with the display. Removes tearing; may add a little delay."),
    "video.frame_cap": dict(label="Picture rate", help="How often a new picture is shown. 60 shows every one; 30 shows every other one at a steady pace; Off shows them as they come.",
                            choices={"off": "Off", "30": "30", "60": "60"}),
    "video.frame_limit": dict(label="Frame rate", kind="choice", help="How many frames a second the game runs at. 60 is the original; above it every frame is a real game frame, and the game keeps its normal speed.",
                              choices={"60": "60 (original)", "120": "120", "180": "180", "240": "240", "360": "360", "0": "Unlimited"}),
    "video.motion_blur": dict(label="Motion blur", help="The game's own blur of fast movement. Off draws every picture sharp."),
    "video.render_rate": dict(label="Render rate", help="60 shows one picture per game step; Display draws at the display's own rate.",
                              choices={"60": "60", "display": "Display"}, hidden=True),
    "video.texture_filter": dict(label="Texture smoothing", help="How textures are sampled. Original is what the game asked for.",
                                 choices={"title": "Original", "point": "Sharp", "bilinear": "Smooth", "trilinear": "Smooth, blended with distance"}),
    "video.anisotropy": dict(label="Texture detail at an angle", kind="choice", help="Keeps textures sharp when seen at a slant. Off leaves them as the game drew them.",
                             choices={"1": "Off", "2": "2x", "4": "4x", "8": "8x", "16": "16x"}),
    "video.texture_pack": dict(label="Texture pack", help="A folder of replacement textures inside your textures folder. none keeps the game's own."),
    "video.backend": dict(label="Graphics", help="Which graphics system draws the game. Automatic picks the best one for this computer.",
                          choices={"auto": "Automatic", "metal": "Apple graphics", "gl": "Open graphics", "soft": "Software"}),
    "ui.perf_overlay": dict(label="Performance readout", help="Shows the game speed and the pictures per second in a corner of the window."),
    # Sound
    "audio.volume": dict(label="Volume", help="The game's loudness, 0 to 100.", min=0, max=100),
    "audio.interpolation": dict(label="Sound smoothing", help="How the console's sound samples are smoothed. Console-accurate matches the original hardware; Standard is a little brighter.",
                                choices={"linear": "Standard", "gaussian": "Console-accurate"}),
    # Controls
    "input.kbm_pad": dict(label="Keyboard and mouse", help="Use the keyboard and mouse as the controller."),
    "input.rumble": dict(label="Vibration", help="Lets the game shake your controller."),
    "input.invert_camera_x": dict(label="Invert camera left/right", help="Turns the camera the other way when you push the camera stick or move the mouse sideways."),
    "input.invert_camera_y": dict(label="Invert camera up/down", help="Tilts the camera the other way when you push the camera stick or move the mouse up and down."),
    "input.device": dict(label="Controller", help="auto uses the first controller found; none turns controllers off."),
    # Saves
    "save.slot": dict(label="Quick-save slot", help="The slot F5 saves to and F8 loads from.", min=0, max=9),
    # Play
    "qol.skip_movies": dict(label="Skip movies", help="Start or Cross skips a movie, the opening one included."),
    "qol.fast_forward": dict(label="Cutscene fast-forward", help="How many times faster a cutscene plays while the fast-forward button is held. 1 is off.", min=1, max=8),
    # Fixes (each a documented defect of the original game, off = as shipped)
    "fixes.look_rest_seconds": dict(label="Fix: head-look rest time", help="The original never set how long a head-look rests, so it used leftover memory. On stores zero."),
    "fixes.skid_mark_init": dict(label="Fix: skid-mark colours", help="The original never set the first skid-mark strip's colours, so it used leftover memory. On clears them."),
    # Folders and files
    "paths.user_data": dict(label="Data folder", kind="folder", help="Where memory cards, save states, textures and the game files live. Empty uses the usual place for this computer."),
    "paths.mc_dir": dict(label="Memory cards folder", kind="folder", help="Empty uses the memory cards folder inside the data folder."),
    "paths.save_dir": dict(label="Save states folder", kind="folder", help="Empty uses the logs folder inside the data folder."),
    "paths.texture_packs": dict(label="Textures folder", kind="folder", help="Each texture pack is a folder in here. Empty uses the textures folder inside the data folder."),
    "paths.game_files": dict(label="Game files folder", kind="folder", help="The files taken from your disc. Empty uses the game files folder inside the data folder."),
    "paths.iso": dict(label="Disc image", kind="file", help="Your disc image. Empty uses the copy inside the game files folder."),
}

RESOLUTION = [
    "--config <file>",
    "<executable folder>/nativeee.ini",
    "<user data>/nativeee.ini",
]
GRAMMAR = {
    "line": "key = value",
    "sections": "a [section] header scopes the short key name; the flat section.key form is read too",
    "comments": "# or ; to the end of the line",
    "write": "replace the key's line in place; a new key goes after the last key of its [section], or in a new [section] appended at the end; write the whole file to a temporary and rename it",
    "booleans": "on | off",
}


def read_table(path):
    rows = []
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if parts[0] == "key":
                continue
            key, typ, peak = parts[0], parts[1], parts[2]
            if peak == '""':
                peak = ""
            rows.append((key, typ, peak))
    return rows


def kind_of(typ, meta):
    if "kind" in meta:
        return meta["kind"]
    if typ == "bool":
        return "switch"
    if typ.startswith("enum("):
        return "choice"
    if typ in ("int", "float"):
        return "number"
    return "text"


def build(rows):
    settings = []
    seen = set()
    for key, typ, peak in rows:
        if key.startswith("harness."):
            continue
        section, name = key.split(".", 1)
        meta = UI.get(key, {})
        entry = {"key": key, "section": section, "name": name, "type": "enum" if typ.startswith("enum(") else typ,
                 "default": peak, "kind": kind_of(typ, meta), "user_facing": bool(meta)}
        if typ.startswith("enum("):
            entry["values"] = typ[len("enum("):-1].split("|")
        if meta:
            seen.add(key)
            entry["label"] = meta["label"]
            entry["help"] = meta.get("help", "")
            if entry["kind"] == "choice":
                values = entry.get("values")
                choices = meta.get("choices", {})
                if values is None:
                    values = list(choices.keys())
                missing = [v for v in values if v not in choices]
                if missing:
                    raise SystemExit(f"{key}: no label for choice value(s) {missing}")
                entry["choices"] = [{"value": v, "label": choices[v]} for v in values]
            if "min" in meta:
                entry["min"] = meta["min"]
            if "max" in meta:
                entry["max"] = meta["max"]
            if meta.get("hidden"):
                entry["hidden"] = True
        settings.append(entry)
    unknown = sorted(set(UI) - seen)
    if unknown:
        raise SystemExit(f"UI metadata names keys the Config schema lacks: {unknown}")
    return {
        "schema": SCHEMA_VERSION,
        "file": "nativeee.ini",
        "bindings_file": "bindings.ini",
        "legacy_files": ["ps2r.ini", "display.ini"],
        "resolution": RESOLUTION,
        "grammar": GRAMMAR,
        "settings": settings,
    }


def lint(doc):
    """Every UI string of the schema against BANNED; returns the failures."""
    failures = []

    def check(where, text):
        for m in BANNED_RE.finditer(text or ""):
            failures.append(f"{where}: '{m.group(1)}' in \"{text}\"")

    for s in doc["settings"]:
        if not s.get("user_facing"):
            continue
        check(f"{s['key']}.label", s.get("label"))
        check(f"{s['key']}.help", s.get("help"))
        for c in s.get("choices", []):
            check(f"{s['key']}.choice[{c['value']}]", c["label"])
    return failures


def render(doc):
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", metavar="JSON", help="compare against the checked-in file; exit 1 on a difference")
    ap.add_argument("--lint", action="store_true", help="run the UI-string lint only")
    ap.add_argument("--table", default=TABLE)
    ap.add_argument("--selftest", action="store_true", help="the lint catches a banned word and passes a clean row")
    args = ap.parse_args()
    if args.selftest:
        bad = {"settings": [{"key": "x.y", "user_facing": True, "label": "Recompile the AOT shard", "help": "Run CMake",
                             "choices": [{"value": "a", "label": "Paris (VU1)"}]}]}
        hits = lint(bad)
        ok = {"settings": [{"key": "x.y", "user_facing": True, "label": "Picture rate", "help": "How often a new picture is shown.",
                            "choices": [{"value": "a", "label": "Every one"}]}]}
        if len(hits) != 6 or lint(ok):   # Recompile, AOT, shard, CMake, Paris, VU1
            print("selftest FAILED:", hits, lint(ok), file=sys.stderr)
            return 1
        print("selftest ok")
        return 0
    doc = build(read_table(args.table))
    failures = lint(doc)
    if failures:
        for f in failures:
            print("settings_schema lint:", f, file=sys.stderr)
        return 1
    if args.lint:
        print(f"settings_schema lint: ok ({sum(1 for s in doc['settings'] if s['user_facing'])} user-facing rows)")
        return 0
    text = render(doc)
    if args.check:
        with open(args.check) as f:
            current = f.read()
        if current != text:
            print(f"{args.check} is stale: regenerate with tools/gen_settings_schema.py", file=sys.stderr)
            return 1
        print(f"{args.check}: up to date ({len(doc['settings'])} settings, "
              f"{sum(1 for s in doc['settings'] if s['user_facing'])} user-facing)")
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
