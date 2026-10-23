#!/usr/bin/env python3
"""gen_settings_table.py -- docs/filemap/settings_table.tsv: the Config schema
in machine form (CONSTRUCTION_PLAN R14), derived from FLAG_LEDGER.md's SETTING
rows and PHASE1_PLAN.md section 3, and verified against both documents.

    python3 tools/gen_settings_table.py > docs/filemap/settings_table.tsv
    python3 tools/gen_settings_table.py --check docs/filemap/settings_table.tsv

Columns (tab-separated; one row per Config::Keys() entry, sorted):
  key         "section.key" exactly as Config::Keys() spells it
  type        bool | int | float | str | enum(a|b|...)
  peak        the peak default in Config::Get() string form ("on"/"off",
              enum name, decimal, raw string; an empty string is written "")
  det         the deterministic preset's value in the same form; "=" means the
              preset leaves the field at its peak default
  source_var  the OLD variable / artifact the row descends from (PHASE1_PLAN
              section 3, last column); "-" when the field is new plumbing
  ledger      FLAG_LEDGER master-table outcome of source_var (SETTING, NORM,
              ...) or "-" when source_var has no ledger row (display.ini keys,
              GUI fields, CLI plumbing)

Header lines start with "#". `# deterministic_fingerprint=` is derived from
the det column exactly as Config::Fingerprint() forms it (sorted non-default
`key=value;` entries, harness./paths./meta.* excluded, then
`profile=deterministic`). It is the ONE source of that golden:
tests/core/test_config.cpp, tests/core/test_config_schema.cpp and
verify/profile_deterministic.py read it from the TSV.

Why the rows live here rather than being scraped: PHASE1_PLAN section 3 states
several peak defaults in prose ("dev on", "today's set", "TRIAGE section 3",
"GUI default") and groups keys in shared rows. The ROWS table below is that
section resolved to Config::Get() strings; the generator then refuses to write
unless (a) every key is named by a PHASE1_PLAN section 3 row, (b) every runtime
variable in FLAG_LEDGER's SETTINGS section maps to a key (or is listed in
FOLDED / BUILD_ONLY with its reason), (c) every PS2_* source_var has a
master-table row, whose outcome becomes the ledger column, and (d) the keys are
unique and sorted. tests/core/test_config_schema.cpp closes the loop against the
compiled Config (every key exists, peak and det values match, no extra key).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
LEDGER = os.path.join(ROOT, "docs", "filemap", "FLAG_LEDGER.md")
PLAN = os.path.join(ROOT, "docs", "phases", "PHASE1_PLAN.md")
PROFILE_NAME = "deterministic"
FINGERPRINT_EXCLUDED_PREFIXES = ("harness.", "paths.", "meta.", "diag.")
# Keys Config::Fingerprint() excludes by name (engine/core/config/config.cpp
# FingerprintExcluded): host scheduling / toolchain choices that change nothing
# the title executes or an image carries. A preset may pin them (det column)
# without entering the fingerprint.
FINGERPRINT_EXCLUDED_KEYS = ("engine.mtvu_kick_drop", "engine.mtvu_early_finish", "engine.mtvu_wait_probe", "engine.ee_yield_spin_gate", "engine.vu1_aot_corpus",
                             "engine.vu1_aot_codegen", "engine.vu1_aot_compiler", "engine.vu1_aot_prelude",
                             "engine.vu1_aot_opt", "engine.vu1_aot_pack", "engine.vu1_aot_region_blocks",
                             "engine.vu1_aot_region_ops", "engine.vu1_aot_region_taken", "engine.vu1_aot_chain", "engine.vu1_aot_mac_acc",
                             "save.restore_mode")

# key, type, peak, det ("=" = unchanged by the preset), source_var
ROWS = [
    ("audio.enabled", "bool", "on", "=", "PS2_SOUND"),
    ("audio.interpolation", "enum(linear|gaussian)", "linear", "=", "-"),
    ("audio.sdr_block_ms", "int", "8", "=", "PS2_SDR_BLOCK_MS"),
    ("audio.volume", "int", "100", "=", "GUI"),
    ("cdvd.delivery_visibility", "enum(boundary|poll)", "poll", "=", "-"),
    ("cdvd.latency_model", "enum(instant|modelled)", "instant", "=", "PS2_CDVD_INSTANT"),
    ("cdvd.latency_scale", "float", "1", "0", "PS2_DVD_LATENCY_SCALE"),
    ("cdvd.load_turbo", "bool", "off", "=", "PS2_LOAD_TURBO"),
    ("cdvd.mc_latency_polls", "int", "4", "0", "PS2_MC_LATENCY"),
    ("cdvd.poll_latency_scale", "float", "1", "=", "-"),
    ("dev.log_level", "enum(quiet|error|warn|info|narrate)", "quiet", "=", "PS2_LOG"),
    ("diag.perf", "bool", "off", "=", "PS2_PERF"),
    ("diag.tick_census", "bool", "off", "=", "-"),
    ("diag.vu1_lockstep", "bool", "off", "=", "-"),
    ("engine.ee_runahead", "int", "2", "=", "PS2_EE_RUNAHEAD"),
    ("engine.ee_yield_spin_gate", "bool", "on", "off", "-"),
    ("engine.mtvu", "bool", "on", "off", "PS2_MTVU"),
    ("engine.mtvu_depth", "int", "2", "=", "PS2_MTVU_MAX_FRAMES"),
    ("engine.mtvu_early_finish", "bool", "on", "off", "-"),
    ("engine.mtvu_kick_drop", "bool", "on", "=", "-"),
    ("engine.mtvu_wait_probe", "bool", "on", "off", "-"),
    ("engine.pacing", "enum(realtime|freerun)", "realtime", "freerun", "PS2_VBLANK_FREERUN"),
    ("engine.shutdown_wait_s", "int", "10", "=", "PS2_SHUTDOWN_WAIT"),
    ("engine.time_regime_pin", "int", "-1", "1", "PS2_TIME_EMU_REGIME"),
    ("engine.turbo", "int", "0", "=", "-"),
    ("engine.vu1_aot_chain", "bool", "on", "=", "-"),
    ("engine.vu1_aot_codegen", "bool", "on", "=", "-"),
    ("engine.vu1_aot_compiler", "str", "", "=", "-"),
    ("engine.vu1_aot_corpus", "str", "", "=", "-"),
    ("engine.vu1_aot_fallback", "enum(block|interp)", "block", "=", "-"),
    ("engine.vu1_aot_mac_acc", "bool", "off", "=", "-"),
    ("engine.vu1_aot_opt", "int", "2", "=", "-"),
    ("engine.vu1_aot_pack", "str", "", "=", "-"),
    ("engine.vu1_aot_prelude", "str", "", "=", "-"),
    ("engine.vu1_aot_region_blocks", "int", "1", "=", "-"),
    ("engine.vu1_aot_region_ops", "int", "48", "=", "-"),
    ("engine.vu1_aot_region_taken", "bool", "on", "=", "-"),
    ("engine.vu1_aot_verify", "bool", "on", "=", "-"),
    ("engine.vu1_mode", "enum(hle|cached|interp|block|aot)", "cached", "=", "PS2_VU_MODE"),
    ("fixes.look_rest_seconds", "bool", "off", "=", "-"),
    ("fixes.skid_mark_init", "bool", "off", "=", "-"),
    ("game.rtc_fixed", "bool", "off", "on", "PS2_RTC_FIXED"),
    ("gs.queue", "enum(inline|thread|pipelined)", "pipelined", "=", "-"),
    ("harness.close_after_ms", "int", "0", "=", "-"),
    ("harness.close_at_frame", "int", "0", "=", "-"),
    ("harness.display_hz", "int", "0", "=", "-"),
    ("harness.display_sequence", "str", "", "=", "-"),
    ("harness.frames", "int", "0", "=", "--bench-frames"),
    ("harness.headless", "bool", "off", "=", "PS2_VERIFY_HEADLESS"),
    ("harness.inject_fault_frame", "int", "0", "=", "-"),
    ("harness.inject_key", "str", "", "=", "-"),
    ("harness.inputs", "str", "", "=", "PS2_PAD_SCRIPT_FILE"),
    ("harness.legacy_input_save", "bool", "off", "=", "-"),
    ("harness.live_pad_probe", "bool", "off", "=", "-"),
    ("harness.live_restore", "bool", "off", "=", "-"),
    ("harness.load_state", "str", "", "=", "--load-state"),
    ("harness.out_dir", "str", "", "=", "PS2_VERIFY_OUT"),
    ("harness.record_input", "bool", "off", "=", "PS2_VERIFY_RECORD_INPUT"),
    ("harness.record_input_path", "str", "", "=", "--record-input"),
    ("harness.restore_at_frame", "int", "0", "=", "-"),
    ("harness.save_at_frame", "int", "0", "=", "-"),
    ("harness.state_every", "int", "0", "=", "PS2_VERIFY_STATE_EVERY"),
    ("harness.virtual_pad", "str", "", "=", "-"),
    ("harness.warp_to_frame", "int", "0", "=", "PS2_WARP_TO"),
    ("hle.audio_mix", "bool", "on", "=", "-"),
    ("hle.culling", "bool", "on", "=", "-"),
    ("hle.files", "bool", "on", "=", "-"),
    ("hle.graphics_distortion", "bool", "on", "=", "-"),
    ("hle.graphics_frame", "bool", "on", "=", "-"),
    ("hle.graphics_meshes", "bool", "on", "=", "-"),
    ("hle.graphics_movie", "bool", "on", "=", "-"),
    ("hle.graphics_screen", "bool", "on", "=", "-"),
    ("hle.graphics_shadows", "bool", "on", "=", "-"),
    ("hle.graphics_sky", "bool", "on", "=", "-"),
    ("hle.graphics_sprites", "bool", "on", "=", "-"),
    ("hle.graphics_strips", "enum(model|plain|all)", "all", "=", "-"),
    ("hle.graphics_text", "bool", "on", "=", "-"),
    ("hle.graphics_textures", "bool", "on", "=", "-"),
    ("hle.input_service", "bool", "on", "=", "-"),
    ("hle.libmc_format_in_t0", "bool", "on", "=", "-"),
    ("hle.math_helpers", "bool", "on", "=", "-"),
    ("hle.meta_arena", "bool", "on", "=", "-"),
    ("hle.movie_lifecycle", "bool", "on", "=", "-"),
    ("hle.movie_start", "bool", "on", "=", "-"),
    ("hle.movie_step", "bool", "on", "=", "-"),
    ("hle.movie_stop", "bool", "on", "=", "-"),
    ("hle.movie_wait", "bool", "on", "=", "-"),
    ("hle.music", "bool", "on", "=", "-"),
    ("hle.music_buffers", "bool", "on", "=", "-"),
    ("hle.save_storage", "bool", "on", "=", "-"),
    ("hle.service_time", "bool", "on", "=", "-"),
    ("hle.sound_bank_headers", "bool", "on", "=", "-"),
    ("hle.sound_samples", "bool", "on", "=", "-"),
    ("hle.startup", "enum(retained|native)", "native", "retained", "-"),
    ("hle.stream_files", "bool", "on", "=", "-"),
    ("input.bindings_file", "str", "default_bindings.ini", "=", "GUI"),
    ("input.device", "str", "auto", "=", "GUI"),
    ("input.kbm_pad", "bool", "on", "=", "GUI"),
    ("input.rumble", "bool", "on", "=", "-"),
    ("iop.dispatch", "enum(realtime|frame)", "frame", "=", "PS2_IOP_DISPATCH"),
    ("iop.restored_timer_due", "enum(one_period|at_restore)", "one_period", "=", "-"),
    ("launcher.elf_sha256", "str", "", "=", "-"),
    ("launcher.show", "bool", "on", "=", "-"),
    ("loader.ring_cache", "bool", "on", "=", "-"),
    ("log.channels", "str", "all", "=", "PS2_LOG_CHANNELS"),
    ("log.ring_events", "int", "65536", "=", "PS2_LOG_RING_EVENTS"),
    ("meta.profile", "str", "default", PROFILE_NAME, "--profile"),
    ("meta.source_fingerprint", "str", "", "=", "build-id"),
    ("paths.game_files", "str", "", "=", "GAME_FILES_DIR"),
    ("paths.iso", "str", "", "=", "GAME_FILES_DIR"),
    ("paths.log_dir", "str", "", "=", "-"),
    ("paths.mc_dir", "str", "", "=", "-"),
    ("paths.save_dir", "str", "", "=", "-"),
    ("paths.texture_packs", "str", "", "=", "-"),
    ("paths.user_data", "str", "", "=", "-"),
    ("qol.fast_forward", "int", "1", "=", "-"),
    ("qol.skip_movies", "bool", "on", "=", "-"),
    ("save.restore_mode", "enum(live|relaunch)", "live", "=", "-"),
    ("save.slot", "int", "0", "=", "PS2_SAVE_SLOT"),
    ("time.maint_eepaced", "bool", "off", "on", "PS2_MAINT_EEPACED"),
    ("time.t0_tickpaced", "bool", "off", "on", "PS2_T0_TICKPACED"),
    ("time.tick_deterministic", "bool", "off", "on", "PS2_TICK_DETERMINISTIC"),
    ("time.tick_sync", "bool", "off", "on", "PS2_TICK_SYNC"),
    ("triage.disk_budget_mb", "int", "2048", "=", "TRIAGE-3"),
    ("triage.enabled", "bool", "on", "=", "PS2_SLOWSPOT"),
    ("triage.fallbacks", "bool", "on", "=", "TRIAGE-3"),
    ("triage.fps_target", "int", "60", "=", "TRIAGE-3"),
    ("triage.perf", "bool", "on", "=", "PS2_BUGREPORT_SLOWSPOT"),
    ("triage.ring_period_s", "int", "10", "=", "TRIAGE-3"),
    ("triage.spikes", "bool", "on", "=", "TRIAGE-3"),
    ("ui.gui_open", "bool", "on", "off", "PS2_GUI_OPEN"),
    ("ui.menu_pages", "bool", "on", "=", "-"),
    ("ui.perf_overlay", "bool", "off", "=", "-"),
    ("video.anisotropy", "int", "1", "=", "-"),
    ("video.aspect", "enum(auto|4:3|16:9|16:10|21:9)", "auto", "=", "display.ini:aspect"),
    ("video.backend", "enum(gl|soft|metal|auto)", "auto", "gl", "RENDERER_BACKEND"),
    ("video.custom_offset_x", "int", "0", "=", "display.ini:custom_offset_x"),
    ("video.custom_offset_y", "int", "0", "=", "display.ini:custom_offset_y"),
    ("video.custom_scale_pct", "int", "100", "=", "display.ini:custom_scale"),
    ("video.fit_mode", "enum(stretch|fit|fill|integer|custom)", "fit", "=", "display.ini:mode"),
    ("video.fps_cap", "int", "60", "0", "PS2_FPS_CAP"),
    ("video.frame_cap", "enum(off|30|60)", "60", "=", "-"),
    ("video.fullscreen", "enum(windowed|fullscreen|borderless)", "windowed", "=", "-"),
    ("video.interp", "bool", "on", "off", "PS2_GS_INTERP"),
    ("video.render_rate", "enum(60|display)", "60", "=", "-"),
    ("video.scale", "int", "1", "=", "PS2_GS_SCALE"),
    ("video.texture_filter", "enum(title|point|bilinear|trilinear)", "title", "=", "-"),
    ("video.texture_pack", "str", "none", "=", "-"),
    ("video.vsync", "bool", "off", "=", "PS2_NOVSYNC"),
]

# FLAG_LEDGER SETTINGS-section variables that do not become their own key.
FOLDED = {
    "PS2_BUGREPORT": "triage.enabled (the PS2_SLOWSPOT master; pre-D9 family folds into triage.*)",
    "PS2_BUGREPORT_SLOWSPOT_MAX": "no field: the per-run cap is a TRIAGE_CAPTURE section 3 "
                                  "cooldown/sustain constant; PHASE1_PLAN section 3 lists no key",
}
BUILD_ONLY = {
    "PS2_RECOMP_SHARD_COUNT": "build parameter only (PHASE1_PLAN section 3 count note)",
}
# Non-variable source tokens (no ledger row expected). PS2_VERIFY_* is harness
# plumbing that never had a ledger row (not in the A2 registry; PHASE1_PLAN
# section 3 names it as the source of harness.*).
NON_LEDGER_SOURCES = ("GUI", "-", "TRIAGE-3", "build-id", "--bench-frames", "--load-state",
                      "--profile", "--record-input", "GAME_FILES_DIR", "RENDERER_BACKEND")
NON_LEDGER_PREFIXES = ("display.ini:", "PS2_VERIFY_")
# Sub-keys whose ledger row is their parent's.
LEDGER_PARENT = {"PS2_PAD_SCRIPT_FILE": "PS2_PAD_SCRIPT"}


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def section(text, start_re, end_re):
    m = re.search(start_re, text, re.M)
    if not m:
        raise SystemExit("section start not found: %s" % start_re)
    rest = text[m.end():]
    e = re.search(end_re, rest, re.M)
    return rest[: e.start()] if e else rest


def table_rows(block):
    """Markdown table rows -> list of cell lists (escaped pipes kept as '|')."""
    rows = []
    for line in block.splitlines():
        if not line.startswith("|") or line.startswith("|---"):
            continue
        cells = [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", line.strip())[1:-1]]
        rows.append(cells)
    return rows


def plan_keys():
    """Every Config key PHASE1_PLAN section 3 names (shared rows split)."""
    block = section(read(PLAN), r"^## 3\. ", r"^## 4\. ")
    keys = set()
    for cells in table_rows(block):
        if not cells or cells[0] in ("Field",):
            continue
        for tok in re.findall(r"`([^`]+)`", cells[0]):
            m = re.match(r"^([a-z_]+\.)([a-z_]+)/([a-z_]+)$", tok)   # video.custom_offset_x/y
            if m:
                base, a, b = m.groups()
                stem = a[: a.rfind("_") + 1]
                keys.add(base + a)
                keys.add(base + stem + b)
            else:
                keys.add(tok)
    return keys


def ledger_settings_section_vars():
    block = section(read(LEDGER), r"^## SETTINGS ", r"^## Reconciliations")
    out = []
    for cells in table_rows(block):
        if not cells or cells[0].startswith("Var"):
            continue
        var = re.sub(r"\s*\(.*\)$", "", cells[0]).strip()
        out.append(var)
    return out


def ledger_master_outcomes():
    block = section(read(LEDGER), r"^## Master table", r"^## (?!Master)")
    out = {}
    for cells in table_rows(block):
        if len(cells) < 5 or cells[0] == "var":
            continue
        out[cells[0].strip("`")] = cells[4].split()[0].rstrip(";:,")
    return out


def fingerprint(rows):
    parts = []
    for key, _t, peak, det, _s in rows:
        if key.startswith(FINGERPRINT_EXCLUDED_PREFIXES) or key in FINGERPRINT_EXCLUDED_KEYS:
            continue
        if det != "=" and det != peak:
            parts.append("%s=%s;" % (key, det))
    return "".join(parts) + "profile=" + PROFILE_NAME


def verify(rows):
    errors = []
    keys = [r[0] for r in rows]
    if keys != sorted(keys):
        errors.append("ROWS not sorted by key")
    if len(set(keys)) != len(keys):
        errors.append("duplicate keys in ROWS")
    plan = plan_keys()
    for k in keys:
        if k not in plan:
            errors.append("key %s is not named by PHASE1_PLAN section 3" % k)
    for k in sorted(plan):
        if k not in keys:
            errors.append("PHASE1_PLAN section 3 names %s but ROWS has no such key" % k)
    sources = {r[4] for r in rows}
    for var in ledger_settings_section_vars():
        if var in sources or var in FOLDED or var in BUILD_ONLY:
            continue
        errors.append("FLAG_LEDGER SETTINGS var %s maps to no key (add a row or list it in FOLDED/BUILD_ONLY)" % var)
    outcomes = ledger_master_outcomes()
    ledger_col = {}
    for _k, _t, _p, _d, src in rows:
        if src in NON_LEDGER_SOURCES or src.startswith(NON_LEDGER_PREFIXES):
            ledger_col[src] = "-"
            continue
        var = LEDGER_PARENT.get(src, src)
        if var not in outcomes:
            errors.append("source var %s has no FLAG_LEDGER master-table row" % src)
            ledger_col[src] = "?"
        else:
            ledger_col[src] = outcomes[var]
    for key, typ, peak, det, _s in rows:
        m = re.match(r"^enum\((.*)\)$", typ)
        if typ == "bool":
            if peak not in ("on", "off") or det not in ("=", "on", "off"):
                errors.append("%s: bool values must be on/off" % key)
        elif typ == "int":
            for v in (peak, det):
                if v != "=" and not re.match(r"^-?\d+$", v):
                    errors.append("%s: int value %r" % (key, v))
        elif m:
            names = m.group(1).split("|")
            for v in (peak, det):
                if v != "=" and v not in names:
                    errors.append("%s: enum value %r not in %s" % (key, v, names))
        elif typ not in ("float", "str"):
            errors.append("%s: unknown type %s" % (key, typ))
    return errors, ledger_col


def render(rows, ledger_col):
    out = ["# settings_table.tsv -- generated by tools/gen_settings_table.py from "
           "docs/filemap/FLAG_LEDGER.md (SETTING rows) + docs/phases/PHASE1_PLAN.md section 3; "
           "do not edit (ctest SettingsTableReproducible).",
           "# columns: key<TAB>type<TAB>peak<TAB>det<TAB>source_var<TAB>ledger  "
           "(det \"=\" = unchanged by the deterministic preset; \"\" = empty string)",
           "# rows=%d" % len(rows),
           "# deterministic_fingerprint=%s" % fingerprint(rows),
           "key\ttype\tpeak\tdet\tsource_var\tledger"]
    for key, typ, peak, det, src in rows:
        out.append("\t".join([key, typ, peak if peak != "" else '""',
                              det if det != "" else '""', src, ledger_col[src]]))
    return "\n".join(out) + "\n"


def load_fingerprint(path):
    """The golden the tests and the harness read (single source)."""
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("# deterministic_fingerprint="):
                return line[len("# deterministic_fingerprint="):].rstrip("\n")
    raise SystemExit("no deterministic_fingerprint header in %s" % path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", metavar="TSV", help="regenerate and compare with the checked-in file")
    a = ap.parse_args()
    errors, ledger_col = verify(ROWS)
    if errors:
        for e in errors:
            print("gen_settings_table: %s" % e, file=sys.stderr)
        return 2
    text = render(ROWS, ledger_col)
    if a.check:
        current = read(a.check)
        if current != text:
            print("gen_settings_table: %s is STALE -- regenerate with "
                  "`python3 tools/gen_settings_table.py > %s`" % (a.check, a.check), file=sys.stderr)
            return 1
        print("gen_settings_table: %s reproducible (%d rows) -> PASS" % (a.check, len(ROWS)))
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
