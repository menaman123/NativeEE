#!/usr/bin/env python3
"""flag_phase_counts.py -- per-phase flag-family counts from FLAG_LEDGER.md.

Reads the FLAG_LEDGER master table (553 rows), maps every site file of every
row to a CONSTRUCTION_PLAN phase (1..9), and tallies outcomes
(NORM/SETTING/DIAG/TEST/DELETE/NEW) per phase. A var whose sites span several
phases is attributed to the EARLIEST phase that transplants one of its site
files (that is where its outcome is first applied); the "touched" table counts
every phase any site falls in.

The checked-in output is docs/filemap/FLAG_PHASE_COUNTS.txt; the ctest target
FlagPhaseCountsReproducible regenerates it and diffs. Regenerate by hand with:

    python3 tools/flag_phase_counts.py > docs/filemap/FLAG_PHASE_COUNTS.txt

The PHASE_OF table is the plan's phase assignment (CONSTRUCTION_PLAN.md section 3)
expressed as file patterns over the OLD tree; change the plan -> change this table
-> regenerate -> the plan's tables are updated from the new output.
"""
import argparse
import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LEDGER_DEFAULT = os.path.join(HERE, "..", "docs", "filemap", "FLAG_LEDGER.md")

# file (basename or path fragment) -> phase. Order matters: first match wins.
PHASE_OF = [
    # ---- phase 1: core (config/log/diag mechanism/savestate/threads/hash) + app shell +
    #      verify emitters. Triage CONSTANTS are fixed here (mechanism lands 9.2).
    (r"host_app/main\.cpp", 1),
    (r"host_app/ring_log", 1), (r"ring_tags\.h", 1), (r"hw_verbose\.h", 1), (r"debug_budget\.h", 1),
    (r"savestate", 1), (r"display_settings", 1), (r"shutdown_policy", 1),
    (r"bug_report", 1), (r"slowspot", 1), (r"perf_profiler", 1), (r"bench", 1),
    (r"timeline\.h", 1), (r"call_census", 1), (r"mid_census", 1),
    (r"thread_qos", 1), (r"CMakeLists", 1), (r"^tests/", 1), (r"scripts/", 1), (r"verify/", 1),
    # ---- phase 2: gs/format pure math
    (r"texture_decoder", 2), (r"gs_deswizzle", 2), (r"gs_mipmap", 2), (r"gs_vram_census", 2),
    (r"s9_shadow", 2),
    # ---- phase 3: ee (mem/cpu/hle/timing/intc/sched) + recompiled output + boundary stub
    (r"memory\.(cpp|h)", 3), (r"hle_meta_arena", 3), (r"cpu_state", 3), (r"recomp_seams", 3),
    (r"syscalls", 3), (r"ps2_threading", 3), (r"ps2_syscall_wrappers", 3), (r"libc_hle", 3),
    (r"ps2_argv", 3), (r"time_domain", 3), (r"ee_timers", 3), (r"intc\.", 3), (r"turbo\.h", 3),
    (r"warp\.h", 3), (r"quantum\.h", 3), (r"ee_pace", 3), (r"ps2_vblank", 3), (r"budget", 3),
    (r"timer_hle", 3), (r"ee_timing/", 3), (r"ee_sched/", 3), (r"manual_switch_dispatchers", 3),
    (r"anim_pose", 3), (r"recompiler_tool/", 3), (r"recompiler\.cpp", 3), (r"maint_pump", 3),
    (r"sce_hle/(libkernl|libscf|libdev|sce_hle_registry)", 3), (r"w10_eefeed", 3),
    # ---- phase 4: iop + sif + cdvd + STREAM.IRX + the IOP-bus devices (SPU2 core, pad HLE,
    #      mc HLE, libsdr) -- CONSTRUCTION_PLAN section 0.2 deviation D-SEQ-1
    (r"iop_", 4), (r"sif", 4), (r"hle_heap", 4), (r"cdvd", 4), (r"iop_recompiler", 4),
    (r"sce_hle/libcdvd", 4), (r"sce_hle/netcnfif", 4), (r"hle/loader", 4), (r"loadspan", 4),
    (r"sound/", 4), (r"snd_bank", 4), (r"snd_census", 4), (r"sce_hle/libsdr", 4),
    (r"pad_input", 4), (r"stick_trace", 4), (r"sce_hle/libpad", 4), (r"sce_hle/libmc", 4),
    # ---- phase 5: gif/vif/dmac/vu (+ AOT never transplanted)
    (r"gif", 5), (r"vif", 5), (r"dmac", 5), (r"vu\.(cpp|h)", 5), (r"vu1_async", 5),
    (r"vu_fast_exec", 5), (r"vu_decode", 5), (r"vu_span", 5), (r"vu_hash", 5), (r"vu_trace", 5),
    (r"runahead_guard", 5), (r"vu1_hle", 5), (r"vu_function_registry", 5), (r"sce_hle/libdma", 5),
    (r"s8_ctxlatch", 5), (r"vu_aot", 5),
    # ---- phase 6: gs state/vram/hle + render/gl + present + interp + classrdr + gs diag
    (r"gs_state", 6), (r"gs\.(cpp|h)", 6), (r"gs_hle", 6), (r"gs_page_tracker", 6),
    (r"gs_deposit_overlay", 6), (r"idx_epoch_guard", 6), (r"idx16_lane", 6), (r"alias_rect", 6),
    (r"opengl", 6), (r"graphics", 6), (r"render\.h", 6), (r"gs_interp", 6), (r"gs_class_renderer", 6),
    (r"gs_crtc", 6), (r"present_pace", 6), (r"viewport_math", 6), (r"vconv", 6), (r"uv_bbox", 6),
    (r"texcache_key", 6), (r"foglane", 6), (r"rt_neverstale", 6), (r"rt_copy", 6), (r"s9_", 6),
    (r"shuf_snap", 6), (r"s8_inrt", 6), (r"gl_state_cache", 6), (r"blend_ff", 6), (r"gs_scale", 6),
    (r"sce_hle/libgraph", 6), (r"hle/gs_", 6), (r"hle/drawlist", 6), (r"gs_glbatch", 6),
    (r"shaders/", 6), (r"gs_c3", 6), (r"compute_data", 6), (r"vulkan", 6), (r"artdet", 6),
    # ---- phase 7: ipu/FMV + host device backends (SDL audio sink, SDL input)
    (r"mpeg_hle", 7), (r"ipu\.", 7), (r"sce_hle/libipu", 7), (r"sce_hle/libmpeg", 7),
    (r"input/", 7),
    # ---- phase 8: GUI + play-mode composition
    (r"gui/", 8),
    # ---- phase 9: one-off probe files never transplanted (their DELETE rows are consumed by
    #      non-transplant) + platform folders
    (r"bullseye", 9), (r"cuelag", 9), (r"loop_hist", 9), (r"pacebrk", 9), (r"mipsrc", 9),
    (r"loader_prof", 9), (r"platform/", 9),
]

VAR_PHASE = {  # var-level overrides where the site file is split across phases
    "PS2_BUDGET_EMU": 3, "PS2_BUDGET_DIAG": 3, "PS2_BUDGET_STEP": 3, "PS2_BUDGET_BASE": 3,
    "PS2_BUDGET_PHASE": 3, "PS2_BUDGET_LOWDRAW": 3, "PS2_BUDGET_BASE_LOW": 3, "PS2_ARGV": 3,
    "PS2_LVL_DIAG": 3, "PS2_BOOT_WATCH": 3, "PS2_RECOMP_DIRECT_CALLS": 3,
    "PS2_RECOMP_SHARD_COUNT": 3, "PS2_SHARDED_RECOMP": 3, "GAME_FILES_DIR": 3, "RECOMP_ENGINE": 3,
    "PS2_VU_O3": 5, "RENDERER_BACKEND": 6, "PS2_GS_SCALE": 6, "PS2_GS_INTERP": 6,
    "PS2_VUAOT_INI": 5, "PS2_VUAOT_COLLECT_DIR": 5, "PS2_TEST_RESIZE": 6, "PS2_SAVE_SLOT": 1,
    # the VU-AOT GUI test hooks live in gui/vu_microcode_section.cpp but are AOT rows: the AOT
    # panel is never written (NOAOT), so they are consumed by the vu phase as "not written"
    "PS2_VUAOT_TEST_REGEN": 5, "PS2_VUAOT_TEST_SETCOLLECT": 5, "PS2_VUAOT_TEST_SETMODE": 5,
}
TEST_FILE_PHASE = [
    (r"gs_replay|resmemo|page_invariance|test_gl|test_texture|test_gs|test_present|test_scanout|drawlist", 6),
    (r"vif_replay|vu1_replay|test_vu|test_vif|test_gif|mtvu", 5),
    (r"ee_pace|test_time|test_ee|recomp", 3),
    (r"iop|sif|loader|sound|pad|libmc|vag|snd", 4),
    (r"mpeg|input", 7),
]
ORDER = ["NORM", "SETTING", "DIAG", "TEST", "DELETE", "NEW"]


def phase_for(site):
    if site.startswith("tests/"):
        for pat, ph in TEST_FILE_PHASE:
            if re.search(pat, site):
                return ph
        return 1
    for pat, ph in PHASE_OF:
        if re.search(pat, site):
            return ph
    return None


def parse_ledger(path):
    rows = []
    in_table = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("| var | default | A2 category"):
                in_table = True
                continue
            if not in_table:
                continue
            if not line.startswith("|"):
                if rows:
                    break
                continue
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) < 6 or cells[0].startswith("---"):
                continue
            var, _default, _cat, _disp, outcome, sites = cells[:6]
            outcome = outcome.split()[0].rstrip(";:,")
            rows.append((var, outcome, sites))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ledger", default=LEDGER_DEFAULT)
    ap.add_argument("--check", metavar="FILE", help="regenerate and diff against FILE; exit 1 on mismatch")
    a = ap.parse_args()
    out = render(parse_ledger(os.path.abspath(a.ledger)))
    if a.check:
        with open(a.check, encoding="utf-8") as f:
            want = f.read()
        if want != out:
            sys.stderr.write("FlagPhaseCounts: %s is STALE -- regenerate with\n"
                             "  python3 tools/flag_phase_counts.py > %s\n" % (a.check, a.check))
            return 1
        print("FlagPhaseCounts: %s reproducible (%d bytes)" % (a.check, len(out)))
        return 0
    sys.stdout.write(out)
    return 0


def render(rows):
    lines = []
    p = lines.append
    p("# FLAG_PHASE_COUNTS -- generated by tools/flag_phase_counts.py from FLAG_LEDGER.md")
    p("# (do not edit; regenerate). Attribution = EARLIEST phase transplanting a site file.")
    p("rows parsed: %d" % len(rows))
    by_outcome = collections.Counter(r[1] for r in rows)
    p("outcomes: " + ", ".join("%s=%d" % (o, by_outcome.get(o, 0)) for o in ORDER))

    unmapped = collections.Counter()
    per_phase = collections.defaultdict(collections.Counter)
    touched = collections.defaultdict(collections.Counter)
    names = collections.defaultdict(lambda: collections.defaultdict(list))
    for var, outcome, sites in rows:
        site_list = [s.strip() for s in sites.replace("…", "").split(";") if s.strip()]
        phases = []
        if var in VAR_PHASE:
            phases = [VAR_PHASE[var]]
            site_list = []
        for s in site_list:
            fname = s.split(":")[0]
            ph = phase_for(fname)
            if ph is None:
                unmapped[fname] += 1
            else:
                phases.append(ph)
        if not phases:
            per_phase["?"][outcome] += 1
            names["?"][outcome].append(var)
            continue
        first = min(phases)
        per_phase[first][outcome] += 1
        names[first][outcome].append(var)
        for ph in set(phases):
            touched[ph][outcome] += 1

    p("")
    p("PHASE | " + " | ".join("%7s" % o for o in ORDER) + " | total   (attributed to earliest transplanting phase)")
    tot = collections.Counter()
    for ph in sorted(per_phase, key=lambda x: (isinstance(x, str), x)):
        c = per_phase[ph]
        for o in ORDER:
            tot[o] += c.get(o, 0)
        p("%-5s | " % ph + " | ".join("%7d" % c.get(o, 0) for o in ORDER) + " | %5d" % sum(c.values()))
    p("ALL   | " + " | ".join("%7d" % tot.get(o, 0) for o in ORDER) + " | %5d" % sum(tot.values()))

    p("")
    p("PHASE | touched (any site in phase): " + " | ".join(ORDER))
    for ph in sorted(touched):
        c = touched[ph]
        p("%-5s | " % ph + " | ".join("%7d" % c.get(o, 0) for o in ORDER) + " | %5d" % sum(c.values()))

    p("")
    p("unmapped site cells (ledger prose that is not a file; attributed by the var's other sites): %d"
      % sum(unmapped.values()))
    p("")
    p("# var names per phase and outcome (the plan's per-phase 'flags consumed' lists)")
    for ph in sorted(names, key=lambda x: (isinstance(x, str), x)):
        for o in ORDER:
            ex = sorted(names[ph][o])
            if ex:
                p("P%s %s (%d): %s" % (ph, o, len(ex), ", ".join(ex)))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.exit(main())
