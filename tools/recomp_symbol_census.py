#!/usr/bin/env python3
"""recomp_symbol_census.py -- the generated-code seam roster (PHASE3_PLAN section
3.4 / 3.4.4) as an executable check.

ROSTER below is the source of truth for "which symbols does the frozen
recompiler's output reference that the runtime must provide (or must NOT
define twice)". PHASE3_PLAN 3.4.4 quotes it; the 3B census criterion is that
this script reports 0 missing and 0 unlisted against the NEW output.

    python3 tools/recomp_symbol_census.py --dir <dir with recompiled.h/.cpp + shards>
    python3 tools/recomp_symbol_census.py --dir ... --check   # exit 1 on any MISSING or UNLISTED

The UNLISTED scan (3B) collects every identifier of the seam families (hle_*, HLE_*, g_*,
memory::*, ps2_threading::*, ps2_*, recomp_*, crashring_*, ee_*call_stack*, call_census_*,
runtime_*, handle_*, StreamPoll*) that the generated TUs reference, drops the roster and
the names the TUs DEFINE themselves, and fails loudly on what remains.

Per roster entry it prints the reference count in the common TU
(recompiled.cpp), the header (recompiled.h) and the shards
(recompiled_shard_*.cpp). An explicitly included continuations.h participates
in the definition/unlisted scan as generated source, not a runtime exemption.
The directory is read only; nothing is written.

Row numbers are PHASE3_PLAN 3.4's. Row 1 (the 11 "../host_app/*.h" include
paths) is checked separately as INCLUDES.
"""
import argparse
import glob
import os
import re
import sys

ROSTER = {
    2: ["CpuContext"],
    3: ["QuadWord"],
    4: ["memory::read<", "memory::write<", "memory::read_quad", "memory::write_quad"],
    5: ["ps2_float_clamp", "ps2_div_s", "ps2_sqrt_s", "ps2_rsqrt_s"],
    6: ["runtime_syscall_dispatcher", "handle_mtc0_write"],
    7: ["recomp_call", "recomp_indirect_call", "recomp_dispatch_fallback", "recomp_mark_hooked",
        "find_containing_function", "update_function_impl", "initialize_recompiled_functions",
        "recomp_fn_id_for_containing", "recomp_fn_id_for", "g_recomp_fn_count", "g_recomp_fn_addrs",
        "g_recomp_fn_cache", "g_recomp_fn_hooked", "g_recomp_fn_raw", "recomp_raw_dispatch",
        "g_sorted_func_impls", "g_sorted_func_starts", "g_sorted_func_ends", "g_sorted_tables_ready",
        "recompiled_functions", "function_ranges",
        # 3B census finding (PHASE3_PLAN 7.1 P3-SEAM-1): declared recompiled.cpp:4195, CALLED :4270
        # (find_containing_function's all-tiers-missed tail), defined by NO generated TU -- it is the
        # one real seam of OLD's recomp_seams.cpp; recomp_dispatch_fallback (:1340) and
        # recomp_mark_hooked (:4182) are DEFINED by the common TU and are not seams.
        "recomp_fcf_miss"],
    8: ["g_ee_call_stack_depth", "g_ee_call_stack", "ee_call_stack_push", "ee_call_stack_pop",
        "ee_get_call_stack_depth", "ee_get_call_stack_top"],
    9: ["call_census_enter", "call_census_exit", "g_call_census_enabled"],
    10: ["g_recomp_corpus_enabled", "recomp_corpus_record", "recomp_corpus_flush_now", "g_recomp_corpus_pairs"],
    11: ["g_func_trace_enabled"],
    12: ["g_logFile"],
    13: ["ps2_threading::exitThread", "ps2_threading::lookupThreadInfo"],
    14: ["hle_sceSifInitRpc", "hle_sceSifBindRpc", "hle_sceSifCallRpc"],
    15: ["hle_sceGsSyncV", "hle_sceGsSyncPath", "g_sceGsSyncV_count", "g_vsync_frame"],
    16: ["hle_sceGsResetGraph", "hle_sceGsResetPath", "hle_sceGsSendImage"],
    17: ["hle_sceGifSyncDma", "hle_sceDmaSync"],
    18: ["hle_TimerWaitMicroseconds"],
    19: ["hle_SetupDisplayRegisters"],
    20: ["hle_InitTLB", "hle_ContextRestore"],
    21: ["hle_DoGlobalConstructors"],
    22: ["HLE_002c5080", "HLE_002c5820", "HLE_002c5940", "HLE_002c6ce0", "HLE_002c6d8c", "HLE_002c6e88",
         "HLE_002c7070", "HLE_002c71bc", "HLE_002c7418", "HLE_002c75e8", "HLE_002ccba0", "HLE_002cf930"],
    23: ["hle_sceCdStInit", "hle_sceCdStStart", "hle_sceCdStStop", "hle_sceCdStRead", "hle_sceCdStSeek",
         "hle_sceCdStPause", "hle_sceCdStResume", "hle_sceCdStStat", "hle_sceCdDiskReady2", "hle_sceCdDiskReady"],
    24: ["StreamPollCompleteMarker"],
    25: ["g_alloc_trace_seq", "g_alloc_live_mutex", "g_alloc_live", "g_pose_wb_count", "g_ring_watch_addr"],
    26: ["crashring_enabled", "crashring_record_indirect", "crashring_fatal_no_jump"],
    27: ["g_vu0.Execute", "g_vu0.ResumeIfWaiting", "g_vu1.Execute"],
    # 3B compile findings (PHASE3_PLAN 7.1 P3-SEAM-2/3): the first regex family missed the
    # `HLE_FUN_` spelling and GetPtcRecord belongs to no family; both surfaced when the common TU
    # was compiled against the ABI headers. Park (iop_dependent) / inert providers.
    28: ["HLE_FUN_002aac80", "HLE_FUN_002ad1a0", "HLE_FUN_002ad8f0", "HLE_FUN_002adb18", "HLE_FUN_002adb40"],
    29: ["GetPtcRecord"],
}

INCLUDES = ["cpu_state", "memory", "syscalls", "sif_hle", "gs_hle", "hle_heap", "ps2_threading",
            "cdvd_stream", "timer_hle", "vu", "sif_rpc_handlers"]

ROSTER_TOTAL = sum(len(v) for v in ROSTER.values())


def count(txt, sym):
    """Whole-identifier match: the symbol must not be a substring of a longer
    identifier or of a qualified/member name that the roster lists separately."""
    pat = re.escape(sym)
    if sym[-1].isalnum() or sym[-1] == "_":
        pat += r"(?![A-Za-z0-9_])"
    if sym[0].isalnum() or sym[0] == "_":
        pat = r"(?<![A-Za-z0-9_:.])" + pat
    return len(re.findall(pat, txt))


def continuation_provider(directory, common):
    """Only the sidecar actually included by the common TU supplies definitions.

    continuations.py emits its lookup in an anonymous namespace. Scan that
    body normally: merely naming the file or declaring the helper proves
    nothing, and any new unresolved seam inside it must still fail.
    """
    if not re.search(r'^\s*#include\s+"continuations\.h"\s*$', common, re.M):
        return []
    with open(os.path.join(directory, "continuations.h"), errors="replace") as source:
        return [("continuations.h", source.read())]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True, help="directory holding recompiled.h, recompiled.cpp, shards")
    ap.add_argument("--check", action="store_true", help="exit 1 if any roster entry is missing")
    a = ap.parse_args()

    def read(p):
        with open(p, errors="replace") as f:
            return f.read()

    common = read(os.path.join(a.dir, "recompiled.cpp"))
    header = read(os.path.join(a.dir, "recompiled.h"))
    shard_paths = sorted(glob.glob(os.path.join(a.dir, "recompiled_shard_*.cpp")))
    shards = [read(p) for p in shard_paths]
    print(f"census over {a.dir}: common TU, header, {len(shards)} shards")
    providers = continuation_provider(a.dir, common)
    print("GENERATED PROVIDERS: " + (", ".join(name for name, _ in providers) or "none"))

    missing = []
    for row, syms in ROSTER.items():
        for s in syms:
            c, h, sh = count(common, s), count(header, s), sum(count(t, s) for t in shards)
            flag = "" if (c + h + sh) else "   <-- MISSING"
            print(f"row {row:2d}  {s:32s} common={c:6d} header={h:4d} shards={sh:7d}{flag}")
            if not (c + h + sh):
                missing.append(s)

    inc_missing = []
    for name in INCLUDES:
        needle = f'#include "../host_app/{name}.h"'
        if needle not in common:
            inc_missing.append(name)
    print(f"INCLUDES row 1: {len(INCLUDES) - len(inc_missing)}/{len(INCLUDES)} present in the common TU"
          + (f"  missing={inc_missing}" if inc_missing else ""))
    print(f"ROSTER TOTAL = {ROSTER_TOTAL} entries; MISSING = {len(missing)} {missing}")

    unlisted = unlisted_scan([("recompiled.h", header), ("recompiled.cpp", common)] + providers
                             + list(zip((os.path.basename(p) for p in shard_paths), shards)))
    if a.check and (missing or inc_missing or unlisted):
        return 1
    return 0


# --- UNLISTED-symbol scan (3B) -----------------------------------------------------------
# Every identifier the generated TUs reference that belongs to a seam family (the names the
# frozen tool emits for things the RUNTIME must provide) must be in ROSTER or be DEFINED by
# the generated TUs themselves. Anything else is an unlisted extern -> plan defect -> exit 1.
FAMILY_RE = re.compile(
    r"\b(?:hle_\w+|HLE_[0-9a-fA-F]{8}|HLE_FUN_\w+|GetPtcRecord|g_\w+|memory::\w+|ps2_threading::\w+|ps2_\w+|recomp_\w+"
    r"|crashring_\w+|ee_\w*call_stack\w*|call_census_\w+|runtime_\w+|handle_\w+|StreamPoll\w+"
    r"|initialize_recompiled_functions|find_containing_function|update_function_impl|recompiled_functions"
    r"|function_ranges)\b")
# A definition: a declarator at top level (column 0) or a static/thread_local declarator anywhere,
# whose statement introduces the name with '(' '=' '[' ';' or '{'. `extern T x;` is NOT a definition;
# `extern "C" T f() {` is.
DEF_LINE_RE = re.compile(r"^(?!\s*extern\s+(?!\"C\"))(?:\S|\s*(?:static|thread_local)\s)[^;{}=\n]*?\b(%s)\b\s*(?:\(|=|\[|;|\{|,)", re.M)


def is_definition(txt, start):
    """The statement starting at `start` DEFINES its declarator when its body opens
    ('{' before the first ';'), or it initialises ('=' before ';'), or it is a plain
    non-extern, non-function variable declarator (`T x;`, `T x[N];`). A bare
    `extern "C" T x;` or `T f(...);` is only a declaration."""
    semi = txt.find(";", start)
    brace = txt.find("{", start)
    if semi < 0:
        semi = len(txt)
    if 0 <= brace < semi:
        return True
    stmt = txt[start:semi]
    if "=" in stmt:
        return True
    if stmt.lstrip().startswith("extern"):
        return False
    return "(" not in stmt


def roster_names():
    names = set()
    for syms in ROSTER.values():
        for s in syms:
            s = s.rstrip("<")
            names.add(s)
            if "." in s:  # g_vu0.Execute -> the object g_vu0
                names.add(s.split(".")[0])
    return names


def unlisted_scan(named_texts):
    listed = roster_names()
    referenced = {}
    for name, txt in named_texts:
        code = re.sub(r"//[^\n]*", "", txt)
        code = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', code)  # strings (log text mentions names)
        for m in FAMILY_RE.finditer(code):
            referenced.setdefault(m.group(0), set()).add(name)
    candidates = sorted(sym for sym in referenced if sym not in listed and sym.split("::")[0] not in listed)
    if not candidates:
        print("UNLISTED SCAN: 0 candidates outside ROSTER")
        return []
    cand_re = re.compile(DEF_LINE_RE.pattern % "|".join(re.escape(c) for c in candidates), re.M)
    defined = set()
    for name, txt in named_texts:
        for m in cand_re.finditer(txt):
            if is_definition(txt, m.start()):
                defined.add(m.group(1))
    tu_internal = [c for c in candidates if c in defined]
    unlisted = [c for c in candidates if c not in defined]
    print(f"UNLISTED SCAN: {len(referenced)} family identifiers referenced; {len(candidates)} outside ROSTER: "
          f"{len(tu_internal)} defined by the generated TUs (not seams), {len(unlisted)} UNLISTED EXTERNS")
    for c in tu_internal:
        print(f"  tu-internal  {c:40s} in {sorted(referenced[c])[:3]}")
    for c in unlisted:
        print(f"  UNLISTED     {c:40s} in {sorted(referenced[c])[:3]}   <-- plan defect (add a provider + roster row)")
    return unlisted


if __name__ == "__main__":
    sys.exit(main())
