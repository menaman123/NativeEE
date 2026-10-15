#!/usr/bin/env python3
"""DT-12 CTest scope. Inventory only; this script never executes a test.

Keep the console tests unchanged. Exclude consumers of saved PCSX2 data and
embedded PCSX2 answer keys, not NEW-vs-NEW or synthetic writer/parser fixtures.
"""
import argparse
import json
import re
from pathlib import Path

# Exact names, deliberately not broad Render_ / Vu / Selftest patterns.
# Values identify the test source a reviewer must inspect when updating this list.
PCSX2_TESTS = {
    "Render_TitleLogoDrawPin": "tests/render/test_title_logo_draw.cpp",
    "Render_TitleFrameReplay": "tests/render/test_title_frame_replay.cpp",
    "Render_TitleFrameReplayDisplay": "tests/render/test_title_frame_replay.cpp",
    "Render_AttractReplay_attract_start_f2845": "tests/render/attract_replay_pin.py",
    "Render_AttractReplay_attract_mid1_f3300": "tests/render/attract_replay_pin.py",
    "Render_AttractReplay_attract_mid2_f3600": "tests/render/attract_replay_pin.py",
    "Render_AttractReplay_attract_end_f3945": "tests/render/attract_replay_pin.py",
    "Render_AttractReplay_title_return_f4200": "tests/render/attract_replay_pin.py",
    "RootTitleScreenEffects": "tests/app/root_title_screen_effects.py",
    "RootAttractCutsceneFrames": "tests/app/root_attract_cutscene_frames.py",
    "Texsrc_TitleAtlasDecode": "tests/gs/texsrc/test_title_atlas_decode.cpp",
    "LoaderRingCache": "tests/cdvd/test_loader_ring_cache.cpp",
    "RootLoaderRingCache": "tests/app/root_loader_ring_cache.py",
    "RootMetaArenaOff": "tests/app/root_meta_arena_off.py",
    "RootTypeRegistryComplete": "tests/app/root_type_registry_complete.py",
    "Vu0ShadowReplay": "tests/vu/test_vu0_shadow_replay.cpp",
    "Vu0VisMicro": "tests/vu/test_vu0_vis_micro.cpp",
    "Vu0DecalEval": "tests/vu/test_vu0_decal_eval.cpp",
    "VuMacroBridge": "tests/vu/test_vu_macro_bridge.cpp",
    "IopSysmemHeap": "tests/iop/test_sysmem_heap.cpp",
}

# Existing declarations with no executable test body in this configuration.
# They remain visible as unavailable and cannot serve as oracle/bug-pin proof.
DECLARATION_STUBS = {
    "Gs_Inv25_InterpRingRotateBeforeFire",
    "Gs_Inv26_InterpIdentityStampedOnly",
    "Gs_Inv30_OwnedByPhase5",
}


def declared_unavailable(name, command):
    if not isinstance(command, list):
        return False
    if name in DECLARATION_STUBS:
        return (len(command) >= 3 and
                Path(command[1]).resolve() ==
                Path(__file__).resolve().parent.parent / "tests/gs/invariants/skip_stub.sh" and
                command[2] == name)
    return (name == "CoreDrawlistRecorder" and len(command) == 3 and
            command[1:] == ["-c", "import sys; print('SKIP: diagnostics build only'); sys.exit(77)"])


def exclusion_regex():
    return "^(" + "|".join(re.escape(name) for name in sorted(PCSX2_TESTS)) + ")$"


def inventory_tests(inventory):
    if inventory.get("kind") != "ctestInfo" or inventory.get("version", {}).get("major") != 1:
        raise ValueError("expected unfiltered ctest --show-only=json-v1 inventory")
    tests = {}
    for test in inventory["tests"]:
        name = test["name"]
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.:+/-]+", name) or name in tests:
            raise ValueError("invalid or duplicate inventory test name: %r" % name)
        properties = {prop["name"]: prop["value"] for prop in test.get("properties", [])}
        if len(properties) != len(test.get("properties", [])):
            raise ValueError("duplicate inventory properties for " + name)
        disabled = properties.get("DISABLED", False)
        if type(disabled) is not bool:
            raise ValueError("nonboolean DISABLED property for " + name)
        command = test.get("command")
        if (not disabled and
                (not isinstance(command, list) or not command or
                 any(not isinstance(arg, str) or not arg for arg in command))):
            raise ValueError("inventory has no built command for " + name)
        # Catch a newly added direct consumer rather than silently include it.
        if name not in PCSX2_TESTS and not disabled and "--selftest" not in command:
            if any(re.search(r"pcsx2|gsdumps[/\\]|\.gs\.zst$|--console-dir", arg, re.I)
                   for arg in command):
                raise ValueError("unclassified console consumer in inventory: " + name)
        tests[name] = {"disabled": disabled, "command": command,
                       "unavailable": declared_unavailable(name, command)}
    if not tests:
        raise ValueError("empty CTest inventory")
    return tests


def scope(inventory):
    tests = inventory_tests(inventory)
    return {
        "excluded": sorted(set(tests) & PCSX2_TESTS.keys()),
        "selected": sorted(set(tests) - PCSX2_TESTS.keys()),
        "disabled": sorted(name for name, test in tests.items()
                           if test["disabled"] and name not in PCSX2_TESTS),
        "unavailable": sorted(name for name, test in tests.items()
                              if test["unavailable"] and name not in PCSX2_TESTS),
        "exclusion_regex": exclusion_regex(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--regex", action="store_true", help="print exact CTest -E argument")
    parser.add_argument("--inventory", help="unfiltered ctest --show-only=json-v1 output")
    args = parser.parse_args()
    if args.regex:
        print(exclusion_regex())
    elif args.inventory:
        print(json.dumps(scope(json.loads(Path(args.inventory).read_text())), indent=2))
    else:
        for name, source in sorted(PCSX2_TESTS.items()):
            print("%s\t%s" % (name, source))


if __name__ == "__main__":
    main()
