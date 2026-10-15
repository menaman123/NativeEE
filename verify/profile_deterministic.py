#!/usr/bin/env python3
"""profile_deterministic.py -- the ONE deterministic env preset
(EQUIVALENCE_PLAN.md section 3), shared by run_leg.py / selftest.py.

Until the Config schema exists (MIGRATION Phase 4) the profile is this env
dict. Every var's clock rationale lives in PROFILE.md next to this file.
The runner applies it on a CLEANED environment (inherited PS2_* stripped)
so a leg's config fingerprint is exactly profile + harness plumbing.
"""

import os

PROFILE_NAME = "deterministic"

# Var -> (value, one-line clock-it-removes note; full table in PROFILE.md)
DETERMINISTIC_ENV = {
    "PS2_TIME_EMU": "1",           # game-visible clocks count emulated quanta
    "PS2_TIME_EMU_REGIME": "1",    # pin the quantum regime (no adaptive detector)
    "PS2_NOVSYNC": "1",            # bench env: no wall vsync pacing
    "PS2_VBLANK_FREERUN": "1",     # ticks fire as pipeline completes
    "PS2_T0_SUBTICK": "0",         # no wall-derived low bits in T0 COUNT
    "PS2_T0_TICKPACED": "1",       # [MAINT pin, BUG-026 r2] zero the LIVE T0
                                   #   read path's wall interpolation term
                                   #   (memory.cpp 0x10000000: now-anchor RAW
                                   #   WALL ns since last SyncV — the clock
                                   #   PS2_T0_SUBTICK does NOT cover; that
                                   #   gate only exists in the default-OFF
                                   #   PS2_EE_TIM0 path). COUNT advances only
                                   #   at EE frame boundaries; the game's
                                   #   delta-seconds (FUN_001b74a0 @0x3098C4)
                                   #   become EE-stream functions
    "PS2_MTVU": "0",               # no VU1 worker interleave
    "PS2_HB_TICKCLOCK": "1",       # STREAM.IRX heartbeat rides generator ticks
    "PS2_CDVD_INSTANT": "1",       # no CDVD latency model
    "PS2_DVD_LATENCY_SCALE": "0",  # DVD latency model off
    "PS2_MC_LATENCY": "0",         # memcard latency model off
    "PS2_FPS_CAP": "0",            # no present-phase decimation
    "PS2_GS_INTERP": "0",          # no present interpolation shimmer
    "PS2_GUI_OPEN": "0",           # no GUI pixels in the backbuffer
    "PS2_GS_SCALE": "1",           # native scale: the bit-identity contract
    "PS2_SND_TICKCLOCK": "1",      # [P0 pin] audio device clock removed
    "PS2_FIXC_TICKCLOCK": "1",     # [P0 pin] deferred completions hold-to-edge
    "PS2_IOP_KERNEL": "1",         # [IOPK pin] cooperative IOP kernel ON (play
                                   #   default currently OFF pending the realtime
                                   #   boot race close-out — BUG-026)
    "PS2_IOP_DISPATCH": "frame",   # [IOPK pin] cooperative IOP kernel dispatched
                                   #   at EE frame boundaries, frame-derived clock
                                   #   (removes the free-running IOP worker clock)
    "PS2_RTC_FIXED": "1",          # [IOPK pin] sceScfGetLocalTimefromRTC constant
                                   #   (wall RTC seeds game timers/RNG in EE RAM)
    "PS2_TICK_DETERMINISTIC": "1", # [IOPK pin] no timeout-floor ticks: the vblank
                                   #   tick count is a pure function of EE progress
                                   #   (T0/master-clock jitter, BUG-014 family)
    "PS2_MAINT_EEPACED": "1",      # [MAINT pin, BUG-026 r2] the maintenance
                                   #   loop's guest-visible duties (SIF RPC
                                   #   end-callbacks, deferred-ISO delivery,
                                   #   suspended-DMA resume) run at EE
                                   #   boundaries / park-loop pumps instead of
                                   #   the ~1 kHz wall loop — removes the wall
                                   #   clock behind cdvd-fired ticks (the T0
                                   #   delta-seconds binning) and the loader
                                   #   spin-exit race
    "PS2_TICK_SYNC": "1",          # [TICKSYNC pin, round 1A / BUG-033 (1)+(3)]
                                   #   EE-synchronous ticks: NO ticker thread;
                                   #   exactly one vblank tick fired IN-LINE by
                                   #   the EE boundary thread (sema 1/3, INTC
                                   #   VBON/TIM2/VBOF direct), cdvd-delivery and
                                   #   FMV-demand ticks fired in-line at their
                                   #   EE-thread trigger sites, the graphics
                                   #   thread raises no INTC; AND the lockstep
                                   #   present rendezvous (every pushed VSync
                                   #   processed by gfx before the EE proceeds --
                                   #   replaces MAX_FRAME_LAG + the 100 ms wall
                                   #   valve). Fixed boundary sequence:
                                   #   ee_timing/boundary_sequencer.h
    "PS2_EE_RUNTOKEN": "1",        # [EETOK pin, BUG-033 (2) / BUG-038 record
                                   #   fix 2026-09-26] pins the EE run token
                                   #   ON regardless of the binary default;
                                   #   removes guest-thread host interleave.
                                   #   Until this pin the profile RELIED on
                                   #   the shipped default (ON-unless-0) --
                                   #   the frozen baselines' env_hash was
                                   #   re-stamped for this var only (hash
                                   #   streams unchanged: baseline/README_FREEZE)
    "PS2_LOG": "0",                # runtime log off (speed; not game-visible)
}

# Harness plumbing keys excluded from the cross-leg equality gate (their
# values legitimately differ per leg dir / tree).
FINGERPRINT_EXCLUDE_PREFIXES = ("PS2_VERIFY_", "PS2_PAD_SCRIPT_FILE")


def build_leg_env(base_env, out_dir, tree, extra=None):
    """Cleaned env for one leg: strip inherited PS2_*, apply the profile,
    stamp the harness plumbing."""
    env = {k: v for k, v in base_env.items() if not k.startswith("PS2_")}
    env.update(DETERMINISTIC_ENV)
    env["PS2_VERIFY_OUT"] = str(out_dir)
    env["PS2_VERIFY_HEADLESS"] = "1"
    env["PS2_VERIFY_TREE"] = tree
    env["PS2_VERIFY_PROFILE"] = PROFILE_NAME
    if extra:
        env.update(extra)
    return env


# The NEW tree's canonical fingerprint of the same preset (Config::Fingerprint():
# sorted non-default key=value entries + profile=; harness/paths/meta excluded).
# battery.py asserts every NEW leg's run.meta env_ps2 equals it (CONSTRUCTION_PLAN
# R6). It is READ from the generated settings table's header -- the single
# source, shared with tests/core/test_config.cpp and test_config_schema.cpp
# (tools/gen_settings_table.py derives it from the table's det column).
SETTINGS_TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                              "docs", "filemap", "settings_table.tsv")


def load_new_fingerprint(path=SETTINGS_TABLE):
    tag = "# deterministic_fingerprint="
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith(tag):
                return line[len(tag):].rstrip("\n")
    raise SystemExit("profile_deterministic: no deterministic_fingerprint header in %s" % path)


NEW_DETERMINISTIC_FINGERPRINT = load_new_fingerprint()
