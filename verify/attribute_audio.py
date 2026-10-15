#!/usr/bin/env python3
"""attribute_audio.py -- the audio lane's first divergent window per leg and its CAUSE CLASS, read from the
hash lane (`audio.ahash`: one 64-bit hash per 4800-mixer-frame window = 6 EE boundaries x 800 frames) and from
the per-voice state in the two trees' savestate images at the window's boundaries (SNDC: the sound core's 48
voices; IOPM: the IOP sound module's voice records in IOP RAM).

  attribute_audio.py --leg L --old-ahash F --new-ahash F [--new-ahash F ...]
                     [--pair N=old.ps2st,new.ps2st ...] [--anchor anchor.ps2st] [--iopm-records 0x12f60]
                     [--iopm-countdown 0x13050] [--out r.json]
  attribute_audio.py --onset --old-ahash F --new-ahash F [--out r.json]      (the coldboot onset/content classes)
  attribute_audio.py --selftest

Hash lane: window W of a leg covers EE boundaries [6W, 6W+6); the first key whose hash differs between the
baseline and NEW (every NEW rep must agree -- a rep disagreement is reported, never classed). Image pairs are
labelled by their EE boundary N: SNDC voices of OLD `state_b<N>` and NEW `state_b<N>` are the same instant
(the I8.7-B calibration: OLD saves before the next pump; NEW's Verify slot saves after the pump of N; only
RAM is one boundary apart). The FROZEN OLD probes `games/<title>/verify/baseline/<leg>/probe/old_b<N>.ps2st` are named by
their RAM boundary and ARE OLD's `state_b<N-1>` (probe_images.py's pairing proof): as a voice image they carry
boundary N-1 and must be given as `--pair <N-1>=old_b<N>,new_state_b<N-1>` (checked on artdet: the 32 kHz
stream voices v00/v01, pitch 0xaaa, advance 0x130 bytes per boundary; old_b2 == NEW state_b1 at nax 0x6180,
NEW state_b2 reads 0x62b0). The pair chosen as the window's EVIDENCE is the first label inside the window or,
failing that, the first label after it (reported as such).

Per-voice classes BY AUDIBILITY (v = voice 0..47; record type byte = the module's `0x12f60 + v`: 0 free,
1 releasing, 2 reserved, 4 finished-unreleased; the countdown word `0x13050 + 4v`; SNDC phase 0 = Off). A voice
differs when ANY of type / countdown / phase / nax / ssa / pitch / adsr2 / vol_l / vol_r / env differs. A side is
AUDIBLE at the pair when its voice is keyed (phase != Off), its envelope is above zero, a channel volume is
non-zero and it is not parked on the module's silent block (nax 0x5000) -- the sound core's mix of that instant.
  INAUDIBLE classes (no sample of the window's mix is explained by them; they corroborate a cause, never prove one):
  bookkeeping        the SNDC voice is IDENTICAL on both sides (phase, env, nax, ssa, pitch, vol, adsr2) and only the
                     module's record type / countdown differ; `heartbeat_shape` marks the DP-20 pattern (OLD holds a
                     type-4 / type-1 record NEW's beat-driven release pass freed)
  ORDER class (HASH-RELEVANT, observed at the pair; perceptually equivalent, never NONSTATE):
  realloc-permutation the same sound (ssa, pitch, nax, phase, env, vol_l, vol_r) sits on ANOTHER voice slot of the
                     other image at the same pair. The lane hashes the RAW FLOAT PCM (engine/core/verify/audio_hasher.h:
                     bit-identical mixing hashes identically) and the mixer sums the voices IN SLOT ORDER in float
                     (engine/sound/spu_core.cpp MixInto: `for vi in voices: mix_l += l * mvol_l`), so the same sounds on
                     other slots change the float summation order -> the low bits -> the window hash. A permutation is
                     therefore a difference of the hashed state (the slot assignment of the sound module), the observed
                     CANDIDATE cause of the window when no audible voice differs; the gate kind is ORDER (blocking).
  save-instant-kon   one side holds a key-on just processed (Attack, nax == ssa, env 0.0: zero samples so far) and the
                     other side is free/Off or parked on the dummy block: a SAVE-INSTANT artefact (OLD's image taken after the module processed the
                     KON, NEW's before) -- CONFIRMED when the next pair (label+1) of the leg shows the same sound
                     (ssa, pitch, vol) playing on the other side (any slot); UNCONFIRMED (no next pair, or not found)
                     -> OPEN, never a heartbeat class
  record-only        both parked on the dummy block (nax 0x5000) with only the record/countdown/phase differing
  AUDIBLE classes (the pair SHOWS a difference of the mix):
  one-sided-audible  one side audible, the other silent (Off / env 0 / parked / vol 0); `heartbeat_shape` when the
                     audible side is OLD holding a type-4 / type-1 record where NEW's is freed (0), or NEW playing a
                     record OLD's never-freed pool could not key (OLD type 1 countdown 0 Off) -- the DP-20 (a) class
  restore-phase      inside W0 only (pair label < 6): OLD Off where NEW still plays a RELEASING record (type 1,
                     countdown -1) -- the ENDX-latch phase both sides guess (DP-20 (c)); audible on NEW's side
  content            both audible, same record type, phase and sample, different position / pitch / envelope / volume
  cross-slot-content one side audible here and the OTHER image plays the same SAMPLE (ssa) on another slot (silent on this
                     image) with another pitch / position: the same key-on landed on another slot with different arguments --
                     an audible content difference, never booked to the heartbeat shape
  other              any other audible combination (listed verbatim; never silently absorbed)
Window verdict for the re-baseline rule, derived from the AUDIBLE subset ONLY: `candidate` when the audible voices
at the evidence pair are ALL `one-sided-audible` with the heartbeat shape (the DP-20 row's class, every audible voice
named); `OPEN order (permutation N ...)` when the audible subset is EMPTY but the pair holds slot permutations -- the
slot assignment of the sound module differs: the hash-relevant candidate cause observed AT the pair (the heartbeat /
realloc domain; a per-boundary hash or a slot-order-insensitive mix comparison would confirm it);
`OPEN-unobserved (no audible difference at the pair ...)` when the audible subset is EMPTY and there is no permutation --
bookkeeping and confirmed save-instant KONs do not show WHY the window's hash differs, the cause lies between the
images; `OPEN restore-phase` / `OPEN content` / `OPEN mixed` otherwise; an UNCONFIRMED save-instant KON is OPEN too.
The JSON carries `rebaseline_gate` = {window, by_kind {ROW = audible heartbeat voices, OPEN = the other audible
voices + unconfirmed KONs, ORDER = the permutation voices (blocking), MIXED 0, NONSTATE = the inaudible rest,
UNATTRIBUTED = `other`}, rows = [the heartbeat row id when ROW > 0]}, the block `apply_rebaseline.py` reads (0 OPEN /
0 ORDER / 0 MIXED / 0 UNATTRIBUTED and ROW + DP >= 1 required); a window with NO audible voice and NO permutation at
the pair carries its unobserved hash difference as one UNATTRIBUTED item, so it can never pass. The heartbeat beat counts are MEASURED:
NEW's `iop_hb_beats` is read from the NEW rep's `run.meta` next to its audio.ahash (scaled to the window: beats x
6(W+1) / boundaries); OLD's is DECISIONS_LOG I8.9-B's measurement (one beat at the restore) -- printed as such.
Pair labels are CHECKED: with `--anchor-vf V` the OLD image's offset `old_vf - V - 2 - label` must be the same on
every OLD image of the leg (OLD's save landing varies across runs: a label is one run's; a differing offset flags
it) and NEW's `vf == label`; mismatches are printed and carried in the JSON (`label_check`).
The first differing MIXER FRAME is not resolvable from the hash lane (no PCM dump on either tree); the window's
boundary span is the finest frame-level statement this tool makes.

`--onset`: the coldboot classes -- the first NON-SILENT window on each side (the leading run of one repeated
hash is silence), the onset delta, and the CONTENT compared from the onset: NEW[W_new + k] vs OLD[W_old + k]
for every k both sides have (aligned by the edge), plus "anywhere" membership (a NEW window equal to ANY OLD
post-onset window: a phase shift by whole windows would show here).
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import state_hash  # noqa: E402

TAG_SNDC = state_hash.tag("SNDC")
TAG_IOPM = state_hash.tag("IOPM")
BOUNDARIES_PER_WINDOW = 6
FRAMES_PER_BOUNDARY = 800
VS_FMT = "<ffHIIIBfffffii28hBBiIh"
VS_SIZE = struct.calcsize(VS_FMT)
PHASES = {0: "Off", 1: "Attack", 2: "Decay", 3: "Sustain", 4: "Release"}
N_VOICES = 48
INAUDIBLE = ("bookkeeping", "save-instant-kon-confirmed", "save-instant-kon-unconfirmed", "record-only")
ORDER = ("realloc-permutation",)      # hash-relevant (float summation order), perceptually equivalent: never NONSTATE
AUDIBLE = ("one-sided-audible", "restore-phase", "content", "cross-slot-content", "other")
CLASSES = INAUDIBLE + ORDER + AUDIBLE
PERMUTATION_NOTE = "hash-relevant: the same sound on another slot changes the mixer's float summation order (perceptually equivalent)"
SNDC_FIELDS = ("phase", "env", "nax", "ssa", "pitch", "vol_l", "vol_r", "adsr2")
SOUND_KEY = ("ssa", "pitch", "nax", "phase", "env", "vol_l", "vol_r")
DEFAULT_HEARTBEAT_ROW = "P5-ANCHORS-OLD-HEARTBEAT-DEAD-AFTER-RESTORE"
DUMMY_BLOCK = 0x5000      # the module's silent parking block (SdSetAddr LSAX = 0x5000 by the sweep)
W0_BOUNDARIES = BOUNDARIES_PER_WINDOW


def read_ahash(path):
    out = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or "," not in line:
                continue
            k, h = line.split(",", 1)
            out[int(k)] = h.strip()
    return out


def first_divergent_window(old, news):
    """(W or None, rep_disagreement [keys], compared)."""
    keys = sorted(k for k in old if all(k in n for n in news))
    disagree = [k for k in keys if len(set(n[k] for n in news)) > 1]
    for k in keys:
        if any(n[k] != old[k] for n in news):
            return k, disagree, len(keys)
    return None, disagree, len(keys)


def window_boundaries(w):
    return (w * BOUNDARIES_PER_WINDOW, (w + 1) * BOUNDARIES_PER_WINDOW)


def parse_sndc(payload):
    off = 0
    (spu_size,) = struct.unpack_from("<I", payload, off); off += 4
    off += spu_size
    (nv,) = struct.unpack_from("<I", payload, off); off += 4
    voices = []
    for _ in range(nv):
        v = struct.unpack_from(VS_FMT, payload, off); off += VS_SIZE
        voices.append({"phase": v[6], "env": v[7], "nax": v[5], "ssa": v[3], "pitch": v[2], "vol_l": v[0], "vol_r": v[1]})
    endx = struct.unpack_from("<2I", payload, off); off += 8
    for i in range(nv):
        p = struct.unpack_from("<5I", payload, off); off += 20
        voices[i]["adsr2"] = p[4]
    return {"voices": voices, "endx": endx}


def parse_iopm_ram(payload):
    (ram_size,) = struct.unpack_from("<I", payload, 0)
    return payload[4:4 + ram_size]


def voice_table(path, rec_off, cd_off):
    """[{type, countdown, phase, env, nax, ssa, adsr2}] x 48 from one image (None fields when a section is absent)."""
    _, vf, secs = state_hash.parse(path)
    out = [{"type": None, "countdown": None, "phase": None, "env": None, "nax": None, "ssa": None, "adsr2": None, "pitch": None,
            "vol_l": None, "vol_r": None} for _ in range(N_VOICES)]
    if TAG_IOPM in secs:
        ram = parse_iopm_ram(secs[TAG_IOPM][2])
        for v in range(N_VOICES):
            out[v]["type"] = ram[rec_off + v]
            out[v]["countdown"] = struct.unpack_from("<i", ram, cd_off + 4 * v)[0]
    if TAG_SNDC in secs:
        s = parse_sndc(secs[TAG_SNDC][2])
        for v in range(min(N_VOICES, len(s["voices"]))):
            out[v].update(s["voices"][v])
    return vf, out


def differs(o, n):
    return any(o[k] != n[k] for k in ("type", "countdown") + SNDC_FIELDS)


def sndc_equal(o, n):
    return all(o[k] == n[k] for k in SNDC_FIELDS)


def audible(x):
    """The voice contributes samples to the mix at the instant of the image."""
    return bool(x["phase"]) and (x["env"] or 0.0) > 0.0 and ((x["vol_l"] or 0.0) != 0.0 or (x["vol_r"] or 0.0) != 0.0) and x["nax"] != DUMMY_BLOCK


def heartbeat_shape(o, n):
    """The DP-20 (a) record pattern: OLD holds (type 1 or 4) what NEW freed (0), or OLD type 4 where NEW type 1 (released
    by NEW's beat), or NEW plays what OLD's never-freed pool could not key (OLD type 1 countdown 0 Off)."""
    to, tn = o["type"], n["type"]
    if to in (1, 4) and tn == 0:
        return True
    if to == 4 and tn == 1:
        return True
    if to == 1 and o["countdown"] == 0 and not o["phase"] and n["phase"] and tn in (1, 2, 4):
        return True
    return False


def sound_key(x):
    return tuple(x[k] for k in SOUND_KEY)


def save_instant_kon(x):
    return x["phase"] == 1 and x["nax"] == x["ssa"] and (x["env"] or 0.0) == 0.0


def classify_voice(o, n, label, others_old=(), others_new=(), next_pair=None):
    """-> (class, note). `others_*` = the other image's voice tables (for the permutation search); `next_pair` =
    (old_table, new_table) of the leg's pair label+1 when available (to confirm a save-instant KON)."""
    to, tn, po, pn = o["type"], n["type"], o["phase"], n["phase"]
    if sndc_equal(o, n):
        return "bookkeeping", "heartbeat-shape" if heartbeat_shape(o, n) else "record/countdown"
    if o["nax"] == DUMMY_BLOCK and n["nax"] == DUMMY_BLOCK:
        return "record-only", "both parked on 0x%x" % DUMMY_BLOCK
    ao, an = audible(o), audible(n)
    if not ao and not an:
        # a key-on caught at the save instant on one side (Attack, nax == ssa, env 0) and nothing playing on the other
        for mine, side in ((o, "OLD"), (n, "NEW")):
            theirs = n if side == "OLD" else o
            if save_instant_kon(mine) and (not theirs["phase"] or theirs["nax"] == DUMMY_BLOCK):   # the other side free or parked
                other_next = None if next_pair is None else (next_pair[1] if side == "OLD" else next_pair[0])   # the OTHER side's table at label+1
                if other_next is not None:
                    key = (mine["ssa"], mine["pitch"], mine["vol_l"], mine["vol_r"])
                    hit = [v for v, x in enumerate(other_next) if (x["ssa"], x["pitch"], x["vol_l"], x["vol_r"]) == key and x["phase"]]
                    if hit:
                        return "save-instant-kon-confirmed", "%s KON ssa=0x%x pitch=0x%x; the other side plays it at the next pair on v%02d" % (side, mine["ssa"], mine["pitch"], hit[0])
                    return "save-instant-kon-unconfirmed", "%s KON ssa=0x%x pitch=0x%x; not found on the other side at the next pair" % (side, mine["ssa"], mine["pitch"])
                return "save-instant-kon-unconfirmed", "%s KON ssa=0x%x pitch=0x%x; no %s image at the next label to confirm it" % (side, mine["ssa"], mine["pitch"], "NEW" if side == "OLD" else "OLD")
        return "bookkeeping", "both inaudible (phase/env/vol/parked differ): " + ("heartbeat-shape" if heartbeat_shape(o, n) else "record/countdown")
    # at least one side audible: a permutation partner on the other image explains the mix
    def partner_of(x, others):
        return next((v for v, y in enumerate(others) if audible(y) and sound_key(y) == sound_key(x)), None)
    if ao and an:
        p_o, p_n = partner_of(o, others_new), partner_of(n, others_old)
        if p_o is not None and p_n is not None:
            return "realloc-permutation", "both sounds sit on other slots of the other image (OLD's on NEW v%02d, NEW's on OLD v%02d); %s" % (p_o, p_n, PERMUTATION_NOTE)
        if to == tn and po == pn and o["ssa"] == n["ssa"]:
            return "content", "both audible, same record/phase/sample, different position/pitch/env/vol"
        return "other", "both audible, different sounds: type %s/%s phase %s/%s%s" % (to, tn, PHASES.get(po, po), PHASES.get(pn, pn),
                                                                                    "; OLD's sound on NEW v%02d" % p_o if p_o is not None else ("; NEW's sound on OLD v%02d" % p_n if p_n is not None else ""))
    mine, theirs_tab, mine_tab, side = (o, others_new, others_old, "OLD") if ao else (n, others_old, others_new, "NEW")
    partner = partner_of(mine, theirs_tab)
    if partner is not None:
        return "realloc-permutation", "the same sound (ssa/pitch/nax/phase/env/vol) on the other image's v%02d; %s" % (partner, PERMUTATION_NOTE)
    # the same SAMPLE keyed on another slot of the other image where this image's slot is silent: a cross-slot key-on with
    # another pitch/position -- an audible CONTENT difference, never the heartbeat shape
    cross = next((v for v, y in enumerate(theirs_tab) if audible(y) and y["ssa"] == mine["ssa"] and not audible(mine_tab[v])), None)
    if cross is not None:
        y = theirs_tab[cross]
        return "cross-slot-content", "%s audible here; the other image plays the same sample ssa=0x%x on v%02d with pitch 0x%x/0x%x nax 0x%x/0x%x" % (
            side, mine["ssa"], cross, mine["pitch"], y["pitch"], mine["nax"], y["nax"])
    if label < W0_BOUNDARIES and not po and pn and tn == 1 and n["countdown"] in (-1, None):
        return "restore-phase", "OLD Off, NEW releasing (type 1, countdown -1) inside W0"
    return "one-sided-audible", ("%s audible only; " % side) + ("heartbeat-shape" if heartbeat_shape(o, n) else "no heartbeat shape")


def _side(x):
    h = lambda k: None if x[k] is None else "0x%x" % x[k]
    f = lambda k: None if x[k] is None else round(x[k], 4)
    return {"type": x["type"], "cd": x["countdown"], "phase": PHASES.get(x["phase"], x["phase"]), "nax": h("nax"), "ssa": h("ssa"), "pitch": h("pitch"),
            "adsr2": h("adsr2"), "env": f("env"), "vol_l": f("vol_l"), "vol_r": f("vol_r"), "audible": audible(x)}


def compare_pair(label, old_path, new_path, rec_off, cd_off, next_pair=None):
    vf_o, to = voice_table(old_path, rec_off, cd_off)
    vf_n, tn = voice_table(new_path, rec_off, cd_off)
    nxt = None
    if next_pair is not None:
        nxt = tuple(voice_table(x, rec_off, cd_off)[1] if x else None for x in next_pair)
    voices = []
    counts = {k: 0 for k in CLASSES}
    for v in range(N_VOICES):
        if not differs(to[v], tn[v]):
            continue
        cls, note = classify_voice(to[v], tn[v], label, to, tn, nxt)
        counts[cls] += 1
        voices.append({"voice": v, "core": v // 24, "class": cls, "audible": cls in AUDIBLE, "order": cls in ORDER, "note": note,
                       "old": _side(to[v]), "new": _side(tn[v])})
    types_o = {t: sum(1 for x in to if x["type"] == t) for t in (0, 1, 2, 4)}
    types_n = {t: sum(1 for x in tn if x["type"] == t) for t in (0, 1, 2, 4)}
    aud = [x for x in voices if x["audible"]]
    return {"label": label, "old_vf": vf_o, "new_vf": vf_n, "differing_voices": len(voices), "audible_voices": len(aud),
            "order_voices": sum(1 for x in voices if x["order"]),
            "by_class": counts, "voices": voices, "next_pair_label": None if next_pair is None else label + 1,
            "record_types_old": types_o, "record_types_new": types_n,
            "playing_old": [v for v in range(N_VOICES) if to[v]["phase"] not in (0, None)],
            "playing_new": [v for v in range(N_VOICES) if tn[v]["phase"] not in (0, None)],
            "audible_old": [v for v in range(N_VOICES) if audible(to[v])], "audible_new": [v for v in range(N_VOICES) if audible(tn[v])]}


def verdict_for(pair):
    """The window verdict from the AUDIBLE subset of the evidence pair's differing voices (see the docstring)."""
    by_class, n_diff = pair["by_class"], pair["differing_voices"]
    if n_diff == 0:
        return "OPEN-unobserved (no voice difference at the evidence pair: the window's cause lies between the images)"
    aud = [x for x in pair["voices"] if x["audible"]]
    inaud = {k: by_class[k] for k in INAUDIBLE if by_class[k]}
    perm = by_class["realloc-permutation"]
    unconfirmed = by_class["save-instant-kon-unconfirmed"]
    if not aud:
        if perm:
            return ("OPEN order (permutation %d: the same sounds on other slots %s -- the slot assignment of the sound module differs, the "
                    "hash-relevant candidate cause observed AT the pair (float summation order); %sinaudible %s)"
                    % (perm, ",".join("v%02d" % x["voice"] for x in pair["voices"] if x["order"]),
                       "%d save-instant KON(s) UNCONFIRMED; " % unconfirmed if unconfirmed else "", inaud))
        if unconfirmed:
            return "OPEN-unobserved (no audible difference at the pair; %d save-instant KON(s) UNCONFIRMED; inaudible %s)" % (unconfirmed, inaud)
        return "OPEN-unobserved (no audible difference at the pair; inaudible: %s; the window's cause lies between the images)" % inaud
    kinds = sorted({x["class"] for x in aud})
    hb = [x for x in aud if x["class"] == "one-sided-audible" and x["note"].endswith("heartbeat-shape")]
    if kinds == ["one-sided-audible"] and len(hb) == len(aud) and not unconfirmed:
        if perm:
            return "OPEN order+heartbeat (audible one-sided heartbeat voices %s AND %d slot permutation(s): the permutation is hash-relevant and unbooked; inaudible %s)" % (
                ",".join("v%02d" % x["voice"] for x in hb), perm, inaud)
        return "candidate (audible one-sided voices %s, every one the DP-20 row's heartbeat shape; inaudible %s)" % (",".join("v%02d" % x["voice"] for x in hb), inaud)
    if kinds == ["restore-phase"] or (set(kinds) <= {"restore-phase", "one-sided-audible"} and "restore-phase" in kinds):
        return "OPEN restore-phase (audible %s%s)" % (", ".join("%s %d" % (k, sum(1 for x in aud if x["class"] == k)) for k in kinds), "; + %d unconfirmed KON" % unconfirmed if unconfirmed else "")
    if set(kinds) <= {"content", "cross-slot-content"}:
        return "OPEN content (audible %s%s)" % (", ".join("%s %d" % (k, sum(1 for x in aud if x["class"] == k)) for k in kinds), "; + %d unconfirmed KON" % unconfirmed if unconfirmed else "")
    return "OPEN mixed (audible %s%s%s)" % (", ".join("%s %d" % (k, sum(1 for x in aud if x["class"] == k)) for k in kinds), "; + %d unconfirmed KON" % unconfirmed if unconfirmed else "",
                                            "; + %d slot permutation(s) (ORDER)" % perm if perm else "")


def rebaseline_gate(window, pair, heartbeat_row):
    """The kinds block apply_rebaseline.py reads: ROW = audible heartbeat-shaped one-sided voices, OPEN = the other
    audible voices + unconfirmed save-instant KONs, ORDER = the slot permutations (hash-relevant, BLOCKING: the gate refuses
    them like OPEN), UNATTRIBUTED = audible `other`, NONSTATE = the inaudible rest (bookkeeping, confirmed KONs, record-only)."""
    aud = [x for x in pair["voices"] if x["audible"]]
    row = sum(1 for x in aud if x["class"] == "one-sided-audible" and x["note"].endswith("heartbeat-shape"))
    other = sum(1 for x in aud if x["class"] == "other")
    order = sum(1 for x in pair["voices"] if x["order"])
    opn = len(aud) - row - other + pair["by_class"]["save-instant-kon-unconfirmed"]
    inaud = pair["differing_voices"] - len(aud) - order - pair["by_class"]["save-instant-kon-unconfirmed"]
    # the window's HASH differs by definition: with no audible voice AND no permutation at the pair that difference itself is one
    # UNATTRIBUTED item (unobserved) -- inaudible bookkeeping never explains a hash change, so such a window can never pass the
    # gate; a permutation IS an observed hash-relevant difference (ORDER, blocking on its own) and names the candidate cause
    unobserved = 1 if not aud and not order else 0
    return {"window": window, "evidence_pair": pair["label"], "unobserved_hash_difference": bool(unobserved),
            "candidate_cause": "slot-order (realloc-permutation %d)" % order if order and not aud else None,
            "by_kind": {"ROW": row, "DP": 0, "OPEN": opn, "ORDER": order, "MIXED": 0, "NONSTATE": inaud, "UNATTRIBUTED": other + unobserved},
            "rows": [heartbeat_row] if row else [], "dps": []}


def new_beats(new_ahash_path, window):
    """NEW's measured heartbeat beats from the rep's run.meta (`iop_hb_beats`), scaled to the window's end; None when absent."""
    meta = os.path.join(os.path.dirname(os.path.abspath(new_ahash_path)), "run.meta")
    beats = frames = None
    if os.path.isfile(meta):
        for line in open(meta):
            k, _, v = line.rstrip("\n").partition("=")
            if k == "iop_hb_beats" and v.isdigit():
                beats = int(v)
            elif k == "frames" and v.isdigit():
                frames = int(v)
    if beats is None or not frames:
        return None
    return {"leg_total": beats, "leg_boundaries": frames, "by_window_end": round(beats * window_boundaries(window)[1] / frames, 1)}


def attribute(leg, old_ahash, new_ahashes, pairs, rec_off, cd_off, anchor_vf=None, heartbeat_row=DEFAULT_HEARTBEAT_ROW):
    old = read_ahash(old_ahash)
    news = [read_ahash(p) for p in new_ahashes]
    w, disagree, compared = first_divergent_window(old, news)
    res = {"leg": leg, "windows_compared": compared, "new_reps": len(news), "rep_disagreement_keys": disagree,
           "old_ahash": old_ahash, "new_ahashes": list(new_ahashes),
           # the window's two lane values bind the ledger to the lane lines it attributed (apply_rebaseline.py checks them)
           "old_window_hash": None if w is None else old[w], "new_window_hash": None if w is None else news[0][w],
           "first_divergent_window": w, "window_boundaries": None if w is None else "[%d, %d)" % window_boundaries(w),
           "window_mixer_frames": None if w is None else "[%d, %d)" % (w * BOUNDARIES_PER_WINDOW * FRAMES_PER_BOUNDARY, (w + 1) * BOUNDARIES_PER_WINDOW * FRAMES_PER_BOUNDARY),
           "heartbeat_beats": None if w is None else {"old": {"beats": 1, "source": "DECISIONS_LOG I8.9-B measurement: one beat at the restore, none after"},
                                                     "new": new_beats(new_ahashes[0], w) if new_ahashes else None},
           "pairs": [], "evidence_pair": None, "verdict": None, "label_check": [], "rebaseline_gate": None}
    by_label = {label: (op, np_) for label, op, np_ in pairs}
    for label in sorted(by_label):
        op, np_ = by_label[label]
        if not op or not np_:
            continue      # a one-sided entry (`N=,new.ps2st` / `N=old.ps2st,`) serves only as the next-label image of its side
        res["pairs"].append(compare_pair(label, op, np_, rec_off, cd_off, by_label.get(label + 1)))
    offsets = {}
    for p in res["pairs"]:
        if p["new_vf"] != p["label"]:
            res["label_check"].append("pair b%d: NEW vf %d != label (expected vf == label)" % (p["label"], p["new_vf"]))
        if anchor_vf is not None:
            offsets[p["label"]] = p["old_vf"] - anchor_vf - 2 - p["label"]
    if anchor_vf is not None and offsets:
        common = sorted(offsets.values(), key=lambda o: -list(offsets.values()).count(o))[0]
        res["label_check_offset"] = common
        for label, off in sorted(offsets.items()):
            if off != common:
                res["label_check"].append("pair b%d: OLD vf offset %d != the leg's %d (old_vf - anchor_vf - 2 - label): the OLD label is one run's landing -- check it" % (label, off, common))
    if w is not None and res["pairs"]:
        lo, hi = window_boundaries(w)
        inside = [p for p in res["pairs"] if lo <= p["label"] < hi]
        after = [p for p in res["pairs"] if p["label"] >= hi]
        ev = inside[0] if inside else (after[0] if after else None)
        if ev is not None:
            res["evidence_pair"] = {"label": ev["label"], "position": "inside the window" if inside else "after the window (first available)",
                                    "by_class": ev["by_class"], "differing_voices": ev["differing_voices"], "audible_voices": ev["audible_voices"],
                                    "order_voices": ev.get("order_voices", 0),
                                    "next_pair_label": ev["next_pair_label"]}
            res["verdict"] = verdict_for(ev)
            res["rebaseline_gate"] = rebaseline_gate(w, ev, heartbeat_row)
        else:
            res["verdict"] = "no image pair at or after the window: OPEN (unmeasured)"
    elif w is None:
        res["verdict"] = "IDENTICAL over %d windows" % compared
    else:
        res["verdict"] = "no image pairs given: hash lane only"
    if disagree:
        res["verdict"] = "NEW reps disagree at keys %s -- not classed" % disagree[:5]
        res["rebaseline_gate"] = None
    return res


def render(res):
    out = ["attribute_audio %s: first divergent window %s (boundaries %s, mixer frames %s; %d windows compared, %d NEW rep(s)%s)"
           % (res["leg"], res["first_divergent_window"], res["window_boundaries"], res["window_mixer_frames"], res["windows_compared"], res["new_reps"],
              "; REP DISAGREEMENT at %s" % res["rep_disagreement_keys"][:5] if res["rep_disagreement_keys"] else "")]
    hb = res["heartbeat_beats"]
    if hb:
        nb = hb["new"]
        out.append("  heartbeat beats: OLD %d (%s) | NEW %s" % (hb["old"]["beats"], hb["old"]["source"],
                   "not measurable (no run.meta iop_hb_beats next to the NEW audio.ahash)" if nb is None else
                   "measured %d over %d boundaries -> %.1f by the window's end" % (nb["leg_total"], nb["leg_boundaries"], nb["by_window_end"])))
    for l in res.get("label_check", []):
        out.append("  LABEL CHECK: " + l)
    for p in res["pairs"]:
        out.append("  pair b%d (OLD vf %d / NEW vf %d%s): %d differing voices, %d AUDIBLE, %d ORDER (slot permutations, hash-relevant); %s; record types OLD %s NEW %s; audible OLD %s NEW %s"
                   % (p["label"], p["old_vf"], p["new_vf"], "; next pair b%d read" % p["next_pair_label"] if p["next_pair_label"] else "; no next pair",
                      p["differing_voices"], p["audible_voices"], p.get("order_voices", 0), {k: v for k, v in p["by_class"].items() if v}, p["record_types_old"], p["record_types_new"],
                      p["audible_old"], p["audible_new"]))
        for v in p["voices"]:
            o, n = v["old"], v["new"]
            out.append("    v%02d %-28s %-9s OLD type=%s cd=%s %s ssa=%s nax=%s pitch=%s env=%s vol=%s/%s | NEW type=%s cd=%s %s ssa=%s nax=%s pitch=%s env=%s vol=%s/%s  [%s]"
                       % (v["voice"], v["class"], "AUDIBLE" if v["audible"] else ("ORDER" if v.get("order") else "inaudible"), o["type"], o["cd"], o["phase"], o["ssa"], o["nax"], o["pitch"], o["env"], o["vol_l"], o["vol_r"],
                          n["type"], n["cd"], n["phase"], n["ssa"], n["nax"], n["pitch"], n["env"], n["vol_l"], n["vol_r"], v["note"]))
    if res["evidence_pair"]:
        e = res["evidence_pair"]
        out.append("  evidence pair b%d (%s): %d differing, %d audible; %s" % (e["label"], e["position"], e["differing_voices"], e["audible_voices"], {k: v for k, v in e["by_class"].items() if v}))
    if res.get("rebaseline_gate"):
        g = res["rebaseline_gate"]
        out.append("  rebaseline gate W%d: %s rows %s" % (g["window"], g["by_kind"], g["rows"]))
    out.append("  verdict: %s" % res["verdict"])
    return "\n".join(out)


def onset(old_ahash, new_ahash):
    old, new = read_ahash(old_ahash), read_ahash(new_ahash)

    def first_nonsilent(h):
        keys = sorted(h)
        if not keys:
            return None, None
        silent = h[keys[0]]
        for k in keys:
            if h[k] != silent:
                return k, silent
        return None, silent
    wo, so = first_nonsilent(old)
    wn, sn = first_nonsilent(new)
    res = {"old_onset_window": wo, "new_onset_window": wn, "silent_hash_old": so, "silent_hash_new": sn, "silent_hash_equal": so == sn,
           "onset_delta_windows": None if None in (wo, wn) else wn - wo, "aligned": None, "anywhere": None}
    if wo is not None and wn is not None:
        ko = sorted(k for k in old if k >= wo)
        kn = sorted(k for k in new if k >= wn)
        n = min(len(ko), len(kn))
        eq = [k for k in range(n) if old[ko[k]] == new[kn[k]]]
        old_set = set(old[k] for k in ko)
        anywhere = [kn[k] for k in range(len(kn)) if new[kn[k]] in old_set]
        res["aligned"] = {"compared": n, "equal": len(eq), "equal_offsets": eq[:20], "first_offsets_old_new": [["W%d" % ko[k], "W%d" % kn[k]] for k in range(min(n, 3))]}
        res["anywhere"] = {"new_windows_in_old_post_onset_set": len(anywhere), "of": len(kn), "examples": anywhere[:10],
                           "old_post_onset_windows": len(ko), "old_distinct_hashes": len(old_set), "new_distinct_hashes": len(set(new[k] for k in kn))}
    return res


def render_onset(res):
    out = ["attribute_audio --onset: OLD first non-silent window W%s, NEW W%s (delta %s windows; silent hash equal on both sides: %s)"
           % (res["old_onset_window"], res["new_onset_window"], res["onset_delta_windows"], res["silent_hash_equal"])]
    if res["aligned"]:
        a, b = res["aligned"], res["anywhere"]
        out.append("  CONTENT aligned by the edge: %d of %d windows equal (offsets %s); NEW post-onset windows found ANYWHERE in OLD's post-onset set: %d of %d %s"
                   % (a["equal"], a["compared"], a["equal_offsets"], b["new_windows_in_old_post_onset_set"], b["of"], b["examples"]))
        out.append("  OLD post-onset windows %d (%d distinct hashes), NEW %d distinct" % (b["old_post_onset_windows"], b["old_distinct_hashes"], b["new_distinct_hashes"]))
    return "\n".join(out)


# ------------------------------------------------------------------ selftest

def _image(path, voices, types, cds, rec_off=0x12f60, cd_off=0x13050, vf=2):
    """A minimal image with SNDC (48 voices) and IOPM (IOP RAM with the record bytes / countdown words).
    voices: {v: (phase, env, nax[, ssa, pitch, vol_l, vol_r])}; a keyed voice defaults to ssa 0x5000 / pitch 0x1000 / vol 0.5."""
    import zlib
    sndc = struct.pack("<I", 16) + bytes(16) + struct.pack("<I", N_VOICES)
    for v in range(N_VOICES):
        spec = voices.get(v, (0, 0.0, 0))
        phase, env, nax = spec[:3]
        ssa = spec[3] if len(spec) > 3 else 0x5000
        pitch = spec[4] if len(spec) > 4 else 0x1000
        vl = spec[5] if len(spec) > 5 else (0.5 if phase else 0.0)
        vr = spec[6] if len(spec) > 6 else (0.5 if phase else 0.0)
        sndc += struct.pack(VS_FMT, vl, vr, pitch, ssa, 0x5000, nax, phase, env, 0.0, 0.0, 0.0, 0.0, 0, 0, *([0] * 28), 0, 0, 0, 0, 0)
    sndc += struct.pack("<2I", 0, 0)
    for v in range(N_VOICES):
        sndc += struct.pack("<5I", 0, 0, 0, 0, 0x1fcd if voices.get(v, (0,))[0] else 0x1fc0)
    sndc += bytes(N_VOICES * 8 + 16 + 256 + 16)
    ram = bytearray(0x20000)
    for v, t in types.items():
        ram[rec_off + v] = t
    for v, c in cds.items():
        struct.pack_into("<i", ram, cd_off + 4 * v, c)
    iopm = struct.pack("<I", len(ram)) + bytes(ram) + bytes(140 + 16 + 8 + 4 + 4)
    secs = [(state_hash.TAG_RAM, bytes(0x1000)), (state_hash.TAG_SPAD, bytes(0x4000)), (state_hash.TAG_META, bytes(64)), (TAG_SNDC, sndc), (TAG_IOPM, iopm)]
    body = b"PS2SAVE1" + struct.pack("<IIIIQ", 4, len(secs), vf, 0, 0)
    for tag, payload in secs:
        body += struct.pack("<IIIIQ", tag, 1, 0, zlib.crc32(payload) & 0xFFFFFFFF, len(payload)) + payload
    body += struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(body)


def selftest():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print("  %s %s" % ("PASS" if cond else "FAIL", what))
        ok &= bool(cond)
    tmp = tempfile.mkdtemp(prefix="attribute_audio_")

    def ahash(name, rows):
        p = os.path.join(tmp, name)
        with open(p, "w") as f:
            f.write("".join("%d,%s\n" % (k, h) for k, h in rows))
        return p
    old = ahash("old.ahash", [(k, "0xa%d" % k) for k in range(10)])
    new1 = ahash("new1.ahash", [(k, "0xa%d" % k if k < 3 else "0xb%d" % k) for k in range(10)])
    new2 = ahash("new2.ahash", [(k, "0xa%d" % k if k < 3 else "0xb%d" % k) for k in range(10)])
    new3 = ahash("new3.ahash", [(k, "0xa%d" % k if k < 5 else "0xb%d" % k) for k in range(10)])
    open(os.path.join(tmp, "run.meta"), "w").write("frames=60\niop_hb_beats=59\n")
    S = 3  # Sustain
    # pair b20 (W3): v5 bookkeeping (type 4->0, SNDC identical, Off), v6 bookkeeping (1->0), v24 OLD audible Sustain / NEW freed+Off
    # (one-sided heartbeat shape), v29 OLD Off type1 cd0 / NEW Sustain type1 cd-1 after W0 (one-sided, heartbeat shape: NEW keyed what
    # OLD's never-freed pool could not), v30 both audible same type/phase other nax (content), v31 record-only (both parked),
    # v40 OLD Sustain sound X on v40 / NEW the same sound on v41 (permutation), v42 volume-only difference (content; the old tool missed it)
    po, pn = os.path.join(tmp, "o.ps2st"), os.path.join(tmp, "n.ps2st")
    _image(po, {29: (0, 0.0, 0xf3c50, 0xf3c50), 30: (S, 1.0, 0x6000, 0x6000), 24: (S, 1.0, 0x7000, 0x7000), 31: (4, 0.2, 0x5000), 40: (S, 1.0, 0x9100, 0x9000, 0x800, 0.1, 0.2),
                42: (S, 1.0, 0x8100, 0x8000, 0x700, 0.3, 0.3)},
           {5: 4, 6: 1, 29: 1, 30: 2, 24: 4, 31: 1, 40: 2, 42: 2}, {29: 0, 30: 7, 31: 0, 40: 1, 42: 1}, vf=20)
    _image(pn, {29: (S, 1.0, 0xf3cf0, 0xf3c50), 30: (S, 1.0, 0x6080, 0x6000), 24: (0, 0.0, 0x7100, 0x7000), 31: (S, 0.2, 0x5000), 41: (S, 1.0, 0x9100, 0x9000, 0x800, 0.1, 0.2),
                42: (S, 1.0, 0x8100, 0x8000, 0x700, 0.3, 0.4)},
           {5: 0, 6: 0, 29: 1, 30: 2, 24: 0, 31: 1, 41: 2, 42: 2}, {29: -1, 30: 7, 31: -1, 41: 1, 42: 1}, vf=20)
    res = attribute("legA", old, [new1, new2], [(20, po, pn)], 0x12f60, 0x13050, anchor_vf=10)
    check(res["first_divergent_window"] == 3 and res["window_boundaries"] == "[18, 24)" and res["window_mixer_frames"] == "[14400, 19200)" and res["windows_compared"] == 10,
          "first divergent window 3 = boundaries [18, 24) = mixer frames [14400, 19200) -> %s %s" % (res["first_divergent_window"], res["window_boundaries"]))
    check(res["old_window_hash"] == "0xa3" and res["new_window_hash"] == "0xb3", "the window's OLD/NEW lane values are recorded (the apply tool's binding) -> %s %s" % (res["old_window_hash"], res["new_window_hash"]))
    p = res["pairs"][0]
    cls = {v["voice"]: v["class"] for v in p["voices"]}
    aud = {v["voice"]: v["audible"] for v in p["voices"]}
    check(cls == {5: "bookkeeping", 6: "bookkeeping", 24: "one-sided-audible", 29: "one-sided-audible", 30: "content", 31: "record-only",
                  40: "realloc-permutation", 41: "realloc-permutation", 42: "content"},
          "per-voice classes by audibility -> %r" % cls)
    check(aud == {5: False, 6: False, 24: True, 29: True, 30: True, 31: False, 40: False, 41: False, 42: True} and p["audible_voices"] == 4 and p["order_voices"] == 2,
          "the audible subset = v24 v29 v30 v42 (bookkeeping, record-only inaudible; the permutation pair v40/v41 is ORDER, not audible) -> %r" % aud)
    notes = {v["voice"]: v["note"] for v in p["voices"]}
    check(notes[5].endswith("heartbeat-shape") and notes[24].endswith("heartbeat-shape") and notes[29].endswith("heartbeat-shape") and "v41" in notes[40],
          "notes: the heartbeat shape named on v5/v24/v29; the permutation partner named -> %s" % notes[40])
    check(res["verdict"].startswith("OPEN mixed (audible content 2, one-sided-audible 2") and "2 slot permutation(s) (ORDER)" in res["verdict"],
          "a mixed audible subset is OPEN mixed and names the permutations -> %s" % res["verdict"])
    g = res["rebaseline_gate"]
    check(g["by_kind"] == {"ROW": 2, "DP": 0, "OPEN": 2, "ORDER": 2, "MIXED": 0, "NONSTATE": 3, "UNATTRIBUTED": 0} and g["rows"] == [DEFAULT_HEARTBEAT_ROW] and g["window"] == 3,
          "the rebaseline gate block: ROW 2 (audible heartbeat), OPEN 2 (content), ORDER 2 (the permutation pair, never NONSTATE), NONSTATE 3 (inaudible) -> %r" % g["by_kind"])
    check("hash-relevant" in notes[40] and "mix-invariant" not in notes[40], "the permutation note says hash-relevant, never mix-invariant -> %s" % notes[40])
    check(res["heartbeat_beats"]["old"]["beats"] == 1 and res["heartbeat_beats"]["new"] == {"leg_total": 59, "leg_boundaries": 60, "by_window_end": 23.6},
          "heartbeat beats: OLD 1 (I8.9-B), NEW measured from run.meta 59/60 -> 23.6 by the window's end -> %r" % res["heartbeat_beats"]["new"])
    check(res["label_check"] == [] and res["label_check_offset"] == -12, "label check: NEW vf == label; the OLD offset recorded (%r)" % res.get("label_check_offset"))
    res2 = attribute("legA", old, [new1, new3], [(20, po, pn)], 0x12f60, 0x13050)
    check(res2["rep_disagreement_keys"] == [3, 4] and res2["verdict"].startswith("NEW reps disagree") and res2["rebaseline_gate"] is None,
          "rep disagreement is reported, nothing classed, no gate block -> %s" % res2["verdict"])
    # candidate: every AUDIBLE voice is a one-sided heartbeat-shaped voice (plus inaudible bookkeeping)
    po2, pn2 = os.path.join(tmp, "o2.ps2st"), os.path.join(tmp, "n2.ps2st")
    _image(po2, {7: (S, 1.0, 0x45460, 0x452c0, 0x752, 0.29, 0.1)}, {5: 4, 7: 4, 8: 1}, {}, vf=30)
    _image(pn2, {}, {5: 0, 7: 0, 8: 0}, {}, vf=30)
    res3 = attribute("legB", old, [new1], [(30, po2, pn2)], 0x12f60, 0x13050)
    check(res3["verdict"].startswith("candidate (audible one-sided voices v07, every one the DP-20 row's heartbeat shape") and res3["evidence_pair"]["position"].startswith("after the window")
          and res3["rebaseline_gate"]["by_kind"]["ROW"] == 1 and res3["rebaseline_gate"]["by_kind"]["OPEN"] == 0,
          "a pair after the window is used (and said so); one AUDIBLE heartbeat voice + 2 bookkeeping -> candidate, gate ROW 1 -> %s" % res3["verdict"])
    # the former 'candidate' shape: ONLY bookkeeping (SNDC identical) differences -> OPEN-unobserved
    po3, pn3 = os.path.join(tmp, "o3.ps2st"), os.path.join(tmp, "n3.ps2st")
    _image(po3, {9: (S, 1.0, 0x456d0, 0x45400)}, {5: 4, 7: 4, 8: 1, 9: 4}, {}, vf=36)
    _image(pn3, {9: (S, 1.0, 0x456d0, 0x45400)}, {5: 0, 7: 1, 8: 0, 9: 1}, {}, vf=36)
    res4 = attribute("legB", old, [new1], [(36, po3, pn3)], 0x12f60, 0x13050)
    check(res4["verdict"].startswith("OPEN-unobserved (no audible difference at the pair; inaudible: {'bookkeeping': 4}")
          and res4["rebaseline_gate"]["by_kind"] == {"ROW": 0, "DP": 0, "OPEN": 0, "ORDER": 0, "MIXED": 0, "NONSTATE": 4, "UNATTRIBUTED": 1} and res4["rebaseline_gate"]["unobserved_hash_difference"],
          "SNDC-identical bookkeeping only (type 4->0 / 4->1 / 1->0, v9 same Sustain) -> OPEN-unobserved, gate ROW 0 + the unobserved hash difference UNATTRIBUTED 1 -> %s" % res4["verdict"])
    # the save-instant KON (the f44111 b4/b5 shape): OLD Attack nax == ssa env 0 vs NEW free; the NEXT pair shows NEW playing the same sound
    po4, pn4, po5, pn5 = (os.path.join(tmp, x) for x in ("o4.ps2st", "n4.ps2st", "o5.ps2st", "n5.ps2st"))
    _image(po4, {32: (1, 0.0, 0x452c0, 0x452c0, 0x74a, 0.0996, 0.2518)}, {32: 4}, {32: -1}, vf=4)
    _image(pn4, {}, {32: 0}, {}, vf=4)
    _image(po5, {32: (1, 0.78, 0x45390, 0x452c0, 0x74a, 0.0996, 0.2518)}, {32: 4}, {32: -1}, vf=5)
    _image(pn5, {24: (1, 0.78, 0x45390, 0x452c0, 0x74a, 0.0996, 0.2518)}, {24: 1}, {24: -1}, vf=5)
    new0 = ahash("new0.ahash", [(k, "0xc%d" % k) for k in range(10)])   # divergent from W0
    res5 = attribute("legC", old, [new0], [(4, po4, pn4), (5, po5, pn5)], 0x12f60, 0x13050)
    c5 = {v["voice"]: v["class"] for v in res5["pairs"][0]["voices"]}
    check(c5 == {32: "save-instant-kon-confirmed"} and "v24" in res5["pairs"][0]["voices"][0]["note"],
          "OLD Attack nax==ssa env 0 vs NEW free, NEW plays the same ssa/pitch/vol at the next pair (v24) -> save-instant-kon-confirmed (never heartbeat) -> %r" % c5)
    c5b = {v["voice"]: v["class"] for v in res5["pairs"][1]["voices"]}
    check(c5b == {24: "realloc-permutation", 32: "realloc-permutation"}, "at b5 the same sound on OLD v32 / NEW v24 is a permutation (ORDER class) -> %r" % c5b)
    check(res5["verdict"].startswith("OPEN-unobserved (no audible difference at the pair; inaudible: {'save-instant-kon-confirmed': 1}"),
          "W0 with the confirmed KON only -> OPEN-unobserved -> %s" % res5["verdict"])
    res5b = attribute("legC", old, [new0], [(4, po4, pn4), (5, "", pn5)], 0x12f60, 0x13050)
    check(len(res5b["pairs"]) == 1 and {v["voice"]: v["class"] for v in res5b["pairs"][0]["voices"]} == {32: "save-instant-kon-confirmed"},
          "a NEW-only image at the next label (`5=,new`) confirms the KON and is not compared itself")
    res6 = attribute("legC", old, [new0], [(4, po4, pn4)], 0x12f60, 0x13050)
    c6 = {v["voice"]: v["class"] for v in res6["pairs"][0]["voices"]}
    check(c6 == {32: "save-instant-kon-unconfirmed"} and res6["verdict"].startswith("OPEN-unobserved (no audible difference at the pair; 1 save-instant KON(s) UNCONFIRMED")
          and res6["rebaseline_gate"]["by_kind"]["OPEN"] == 1,
          "without a next pair the KON is UNCONFIRMED -> OPEN, gate OPEN 1 -> %s" % res6["verdict"])
    # the cross-slot key-on (the f46203 b59 shape): OLD v33 Attack ssa 0x7c800 pitch 0x759 vs NEW v29 Attack ssa 0x7c800 pitch 0x881 (both slots
    # silent on the other image) -> cross-slot-content (audible, OPEN content), never the heartbeat shape
    po6, pn6 = os.path.join(tmp, "o6.ps2st"), os.path.join(tmp, "n6.ps2st")
    _image(po6, {33: (1, 0.78, 0x7c8d0, 0x7c800, 0x759, 0.25, 0.25)}, {33: 4, 29: 1}, {29: 0}, vf=59)
    _image(pn6, {29: (1, 0.78, 0x7c8f0, 0x7c800, 0x881, 0.25, 0.25)}, {33: 0, 29: 1}, {29: -1}, vf=59)
    res6b = attribute("legE", old, [new0], [(59, po6, pn6)], 0x12f60, 0x13050)
    c6b = {v["voice"]: v["class"] for v in res6b["pairs"][0]["voices"]}
    check(c6b == {29: "cross-slot-content", 33: "cross-slot-content"} and res6b["verdict"].startswith("OPEN content (audible cross-slot-content 2")
          and res6b["rebaseline_gate"]["by_kind"]["ROW"] == 0 and res6b["rebaseline_gate"]["by_kind"]["OPEN"] == 2,
          "the same sample keyed on another slot with another pitch -> cross-slot-content on both slots, OPEN content, gate ROW 0 -> %r %s" % (c6b, res6b["verdict"]))
    # two audible voices whose sounds are swapped across slots: both-sided permutation
    po7, pn7 = os.path.join(tmp, "o7.ps2st"), os.path.join(tmp, "n7.ps2st")
    _image(po7, {32: (S, 1.0, 0xfcda0, 0xfcc10, 0x746, 0.07, 0.05), 37: (S, 1.0, 0x45460, 0x452c0, 0x752, 0.29, 0.1)}, {32: 4, 37: 4}, {}, vf=9)
    _image(pn7, {29: (S, 1.0, 0xfcda0, 0xfcc10, 0x746, 0.07, 0.05), 32: (S, 1.0, 0x45460, 0x452c0, 0x752, 0.29, 0.1)}, {29: 1, 32: 1}, {}, vf=9)
    res7c = attribute("legF", old, [new0], [(9, po7, pn7)], 0x12f60, 0x13050)
    c7c = {v["voice"]: v["class"] for v in res7c["pairs"][0]["voices"]}
    g7c = res7c["rebaseline_gate"]
    check(c7c == {29: "realloc-permutation", 32: "realloc-permutation", 37: "realloc-permutation"} and res7c["verdict"].startswith("OPEN order (permutation 3: the same sounds on other slots v29,v32,v37"),
          "OLD v32/v37 == NEW v29/v32 (the slot both sides use for different sounds included) -> all realloc-permutation, OPEN order naming them -> %s" % res7c["verdict"])
    check(g7c["by_kind"] == {"ROW": 0, "DP": 0, "OPEN": 0, "ORDER": 3, "MIXED": 0, "NONSTATE": 0, "UNATTRIBUTED": 0} and not g7c["unobserved_hash_difference"]
          and g7c["candidate_cause"] == "slot-order (realloc-permutation 3)",
          "the permutation fixture gates ORDER 3 (blocking), NONSTATE 0, no unobserved item, candidate cause slot-order -> %r" % g7c)
    # a heartbeat ROW voice plus a permutation is NOT a candidate (the permutation is hash-relevant and unbooked)
    po8, pn8 = os.path.join(tmp, "o8.ps2st"), os.path.join(tmp, "n8.ps2st")
    _image(po8, {7: (S, 1.0, 0x45460, 0x452c0, 0x752, 0.29, 0.1), 40: (S, 1.0, 0x9100, 0x9000, 0x800, 0.1, 0.2)}, {7: 4, 40: 2}, {}, vf=30)
    _image(pn8, {41: (S, 1.0, 0x9100, 0x9000, 0x800, 0.1, 0.2)}, {7: 0, 41: 2}, {}, vf=30)
    res8b = attribute("legB", old, [new1], [(30, po8, pn8)], 0x12f60, 0x13050)
    g8b = res8b["rebaseline_gate"]
    check(res8b["verdict"].startswith("OPEN order+heartbeat") and g8b["by_kind"]["ROW"] == 1 and g8b["by_kind"]["ORDER"] == 2 and g8b["by_kind"]["NONSTATE"] == 0,
          "one audible heartbeat voice + a permutation pair -> OPEN order+heartbeat, gate ROW 1 ORDER 2 (never candidate) -> %s %r" % (res8b["verdict"], g8b["by_kind"]))
    # restore-phase inside W0 is an AUDIBLE class (NEW plays) and OPEN
    res7 = attribute("legA", old, [new1], [(2, po, pn)], 0x12f60, 0x13050)
    c7 = {v["voice"]: v["class"] for v in res7["pairs"][0]["voices"]}
    res7b = attribute("legA", old, [new0], [(2, po, pn)], 0x12f60, 0x13050)
    check(c7[29] == "restore-phase" and res7b["verdict"].startswith("OPEN mixed (audible content 2, one-sided-audible 1, restore-phase 1"),
          "the same images labelled b2 (inside W0): v29 is restore-phase (audible); a W0 window with it is OPEN -> %s" % res7b["verdict"])
    # a volume-only difference is a difference (the old `differs` ignored vol/env/ssa)
    check(differs({"type": 1, "countdown": 0, "phase": S, "env": 1.0, "nax": 1, "ssa": 1, "pitch": 1, "vol_l": 0.1, "vol_r": 0.1, "adsr2": 0},
                  {"type": 1, "countdown": 0, "phase": S, "env": 1.0, "nax": 1, "ssa": 1, "pitch": 1, "vol_l": 0.2, "vol_r": 0.1, "adsr2": 0}),
          "differs() sees a volume-only difference")
    res8 = attribute("legD", old, [old], [], 0x12f60, 0x13050)
    check(res8["first_divergent_window"] is None and res8["verdict"].startswith("IDENTICAL"), "identical lanes -> IDENTICAL")
    txt = render(res)
    check("v24 one-sided-audible            AUDIBLE" in txt and "verdict: OPEN mixed" in txt and "rebaseline gate W3" in txt and "heartbeat beats: OLD 1" in txt,
          "render lists the voices with their audibility, the gate block, the beats and the verdict")
    # the label check: an OLD image whose offset differs from the leg's is flagged
    po9 = os.path.join(tmp, "o9.ps2st")
    _image(po9, {}, {}, {}, vf=99)
    res9 = attribute("legA", old, [new1], [(20, po, pn), (21, po9, pn)], 0x12f60, 0x13050, anchor_vf=10)
    check(any("pair b21: OLD vf offset" in l for l in res9["label_check"]) and any("pair b21: NEW vf 20 != label" in l for l in res9["label_check"]),
          "label check flags the OLD offset and the NEW vf mismatch -> %r" % res9["label_check"])
    # onset / content
    so = ahash("cb_old.ahash", [(k, "0x0") for k in range(5)] + [(k, "0xo%d" % k) for k in range(5, 12)])
    sn = ahash("cb_new.ahash", [(k, "0x0") for k in range(8)] + [(k, "0xo%d" % (k - 3) if k < 10 else "0xn%d" % k) for k in range(8, 15)])
    r = onset(so, sn)
    check(r["old_onset_window"] == 5 and r["new_onset_window"] == 8 and r["onset_delta_windows"] == 3 and r["silent_hash_equal"], "onset windows 5 / 8, delta 3, the same silence")
    check(r["aligned"]["compared"] == 7 and r["aligned"]["equal"] == 2 and r["aligned"]["equal_offsets"] == [0, 1] and r["anywhere"]["new_windows_in_old_post_onset_set"] == 2,
          "aligned by the edge: 2 of 7 equal (offsets 0,1); 2 NEW windows found anywhere in OLD's post-onset set -> %r" % r["aligned"])
    check("delta 3 windows" in render_onset(r), "render_onset")
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("attribute_audio selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", default="?")
    ap.add_argument("--old-ahash")
    ap.add_argument("--new-ahash", action="append", default=[])
    ap.add_argument("--pair", action="append", default=[], help="N=old.ps2st,new.ps2st (images at EE boundary N on both sides); one side may be empty (`N=,new.ps2st`): not compared, used only to confirm a save-instant KON of the previous label")
    ap.add_argument("--iopm-records", default="0x12f60", help="IOP RAM address of the module's 48 voice-record type bytes (title data)")
    ap.add_argument("--iopm-countdown", default="0x13050", help="IOP RAM address of the module's 48 countdown words (title data)")
    ap.add_argument("--anchor-vf", type=int, help="the anchor's vsync frame: checks every OLD pair label's offset (old_vf - V - 2 - label) for consistency")
    ap.add_argument("--heartbeat-row", default=DEFAULT_HEARTBEAT_ROW, help="the §7.1 row id of the DP-20 (a) class named in the rebaseline gate")
    ap.add_argument("--onset", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.old_ahash or not a.new_ahash:
        ap.error("--old-ahash and --new-ahash are required")
    if a.onset:
        res = onset(a.old_ahash, a.new_ahash[0])
        print(render_onset(res))
    else:
        pairs = []
        for spec in a.pair:
            n, rest = spec.split("=", 1)
            op, np_ = rest.split(",", 1)
            pairs.append((int(n), op, np_))
        res = attribute(a.leg, a.old_ahash, a.new_ahash, pairs, int(a.iopm_records, 16), int(a.iopm_countdown, 16), a.anchor_vf, a.heartbeat_row)
        print(render(res))
    if a.out:
        with open(a.out, "w") as f:
            json.dump(res, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
