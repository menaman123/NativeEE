#!/usr/bin/env python3
"""spad_stack.py -- the scratchpad as the main thread's stack: which differing SPAD words are DEAD (stale callee
frames below the live `$sp`, the stack growing DOWN through the 16 KB mirror) and which lie inside a live frame,
read from the IMAGES, with the per-pair CONDITIONS the dead-stack claim needs stated as evidence.

  spad_stack.py <old.ps2st> <new.ps2st> --fill 0xA1B2C3D4 [--text lo-hi] [--decoder-ctx <spec>]
                [--decoder-text lo-hi] [--out spad.json]
  spad_stack.py --selftest

Facts the tool reads, never assumes:
  * every THRD record's saved `$sp` (CpuContext GPR[29] low word) on each image; the record flagged
    `is_boundary` is the thread the probe boundary interrupted;
  * the EE selects the 16 KB scratchpad by `(addr & 0x70000000) == 0x70000000` with the offset in the low 14
    bits (NEW `Scratchpad::Offset`, OLD the same), so `$sp` names the SPAD offset `sp & 0x3fff` and a callee
    that pushes more than `sp & 0x3fff` bytes WRAPS to the top of the scratchpad: the distance of a word below
    the live `$sp` is `(sp_off - o) & 0x3fff`;
  * the title's crt0 fills the whole scratchpad with one word (`--fill`, title data: the TSV's `@spad-fill`)
    before setting `$sp`, so a word still holding the fill was never written -- the first fill word at or
    above `$sp` on BOTH images bounds the live frames (the chain's outermost frame has no guest caller frame:
    the host enters the thread body), and the fill run above it is the span the stack never reached.

The scratchpad is DUAL-USE in this title (its code, read in the recomp): (a) a SPAD-stack CONTEXT SWITCH copies
the 16 KB to a RAM save area, relocates `$sp` into that copy (`daddu sp`), runs a callback, copies the 16 KB
back and restores `$sp` -- while it is active the live `$sp` is in RAM, not in the scratchpad; (b) the MPEG
decoder's context takes its work buffers in the scratchpad when its mode flag is 0 (the init writes five
`0x7000xxxx` buffer pointers into the context; `--decoder-ctx` is that signature, title data from the TSV's
`@spad-decoder-ctx`), and the IPU DMA channels (3 fromIPU / 4 toIPU) stream through them -- while a decode is
in flight the scratchpad holds decoder DATA (guest state), not stack.

Conditions evaluated per pair (all in the JSON; the dead-stack classes are issued only when they hold):
  C1 stack switch not active: the boundary thread's `$sp` is in the scratchpad domain on BOTH images and equal;
  C2 no other thread's stack in the scratchpad: every other THRD record with a context has `$sp` outside it;
  C3 no decode in flight: IPU DMA channels 3/4 have CHCR.STR clear on both images (DMAC section; an image
     WITHOUT a DMAC section proves nothing -> C3 unproved) and no saved frame (function or return address) of
     ANY thread's chain -- every THRD record with a context, on either image -- lies in the decoder libraries'
     text (`--decoder-text`, title data: the WHOLE libmpeg + libipu band from the binding manifest; without the
     range C3 is unproved). The decode is synchronous: `sceMpegGetPicture -> _getpic -> _sceMpegPictureData0 ->
     _slice0` runs the IPU through the scratchpad from the calling thread, so a decode interrupted at a boundary
     has that thread's chain inside the band or the IPU DMA running -- the two tests above. The decoder contexts
     in RAM carrying the scratchpad work-buffer signature are REPORTED with their state words (`+0x87c` mode,
     `+0x0` inner status, `+0x820` the work-buffer bank toggle `_slice0` flips per macroblock row and
     `_sceMpegPictureData0` resets, `+0x848/+0x84c` the slice's DMA source words) -- corroboration, not a test:
     libmpeg keeps no "decoding" flag, and neither tree's image carries the IPU's CTRL busy bit (no IPU section).

Classes of a differing SPAD word x (offset o = x & 0x3fff):
  spad-live-frame      sp_off <= o < live_end: inside the frames of the boundary call chain (STATE)
  spad-dead-below-sp   every other word while C1-C3 hold: below the live stack pointer by
                       (sp_off - o) & 0x3fff bytes (the wrap included) -- stale callee frames, unreadable by
                       well-formed code
  spad-decoder-work    C1/C2 hold but C3 fails (a decode is in flight): every non-live word is the decoder's
                       work data -- guest STATE of the FMV path, never non-state
  spad-sp-unproved     C1 or C2 fails (the images disagree on `$sp`, one carries no boundary context, the
                       live `$sp` is not in the scratchpad, or another stack lives there), or C3 cannot be
                       evaluated (no DMAC section / no decoder-text range): nothing proved
Evidence carried per pair: the fill run above the live frames and the first WRITTEN offset above it (what was
ever written since the crt0 fill -- stack or earlier decoder work, not the stack's proven reach; the proven
frame depth is the dead words' max wrap distance), the deepest wrap distance of the dead words, and the frame
shape of the dead words -- how many hold a code address (`--text`,
title data: the TSV's `@spad-text`) and how many 8-byte slots have a zero high word on both sides (the
`sd`-saved 64-bit register shape of EE frames).
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import state_hash  # noqa: E402

TAG_THRD = state_hash.tag("THRD")
TAG_DMAC = state_hash.tag("DMAC")
SPAD_BASE = 0x70000000
SPAD_MASK = 0x3fff
SP_GPR_OFFSET = 29 * 16
DMAC_CHANNEL_BYTES = 28          # chcr madr qwc tadr asr0 asr1 sadr
DMAC_CHCR_STR = 1 << 8
IPU_DMA_CHANNELS = (3, 4)        # fromIPU, toIPU
CLASSES = ("spad-dead-below-sp", "spad-live-frame", "spad-decoder-work", "spad-sp-unproved")


def in_spad(addr):
    return addr is not None and (addr & 0x70000000) == 0x70000000


def threads(path):
    """Every THRD record: [(tid, started, has_ctx, is_boundary, sp or None, chain [(func, ra)])]."""
    _, _, sections = state_hash.parse(path)
    out = []
    if TAG_THRD not in sections:
        return out
    p = sections[TAG_THRD][2]
    off = 4
    n = struct.unpack_from("<I", p, 0)[0]
    for _ in range(n):
        tid = struct.unpack_from("<I", p, off)[0]
        off += 60
        started, has_ctx, is_b, _pad = p[off:off + 4]
        off += 4
        _hb, _he, csz, cd = struct.unpack_from("<4I", p, off)
        off += 16
        sp = None
        if has_ctx:
            sp = struct.unpack_from("<I", p, off + SP_GPR_OFFSET)[0]
            off += csz
        chain = [struct.unpack_from("<II", p, off + 8 * c) for c in range(cd)]
        off += 8 * cd
        out.append((tid, started, has_ctx, is_b, sp, chain))
    return out


def boundary_sp(path):
    """(sp, chain) of the THRD record flagged is_boundary that carries a context; (None, []) when absent."""
    for tid, started, has_ctx, is_b, sp, chain in threads(path):
        if is_b and sp is not None:
            return sp, chain
    return None, []


def ipu_dma_busy(path):
    """[channel] of the IPU DMA channels whose CHCR.STR is set (DMAC section); None when the section is absent
    (the rule fails closed: no DMAC section -> C3 unproved)."""
    _, _, sections = state_hash.parse(path)
    if TAG_DMAC not in sections:
        return None
    p = sections[TAG_DMAC][2]
    busy = []
    for ch in IPU_DMA_CHANNELS:
        chcr = struct.unpack_from("<I", p, ch * DMAC_CHANNEL_BYTES)[0]
        if chcr & DMAC_CHCR_STR:
            busy.append(ch)
    return busy


def parse_ctx_spec(spec):
    """'0x5a0:0x70000000,0x5a4:0x70001800,...' -> [(offset, value)] (the decoder context's work-buffer signature)."""
    out = []
    for item in spec.split(","):
        o, v = item.split(":", 1)
        out.append((int(o, 16), int(v, 16)))
    return out


def decoder_contexts(ram, sig):
    """RAM addresses of every context carrying the signature (all (offset, value) pairs present)."""
    if not sig:
        return []
    o0, v0 = sig[0]
    key = struct.pack("<I", v0)
    hits = []
    i = ram.find(key)
    span = max(o for o, _ in sig) + 4
    while i >= 0:
        base = i - o0
        if base >= 0 and base + span <= len(ram) and all(struct.unpack_from("<I", ram, base + o)[0] == v for o, v in sig):
            hits.append(base)
        i = ram.find(key, i + 4)
    return hits


DECODER_CTX_STATE_WORDS = (("mode", 0x87c), ("status", 0x0), ("bank", 0x820), ("w824", 0x824), ("w834", 0x834),
                           ("dma_src0", 0x848), ("dma_src1", 0x84c))


def decoder_ctx_state(ram, base):
    """The libmpeg inner context's state words at `base` (corroboration for C3; see the module docstring)."""
    return {name: "0x%x" % struct.unpack_from("<I", ram, base + off)[0] for name, off in DECODER_CTX_STATE_WORDS
            if base + off + 4 <= len(ram)}


def chain_frames_in(threads_list, side, band):
    """['SIDE tidN 0xADDR'] for every saved frame (function or return address) of every thread's chain inside band."""
    hits = []
    for tid, _started, _has_ctx, _is_b, _sp, chain in threads_list:
        for f, ra in chain:
            for a in (f, ra):
                if band[0] <= a < band[1]:
                    hits.append("%s tid%d 0x%x" % (side, tid, a))
    return hits


def live_end(spad, sp_off, fill):
    """The first offset >= sp_off whose word is the fill (the frames' upper bound), or 0x4000."""
    for o in range(sp_off & ~3, len(spad), 4):
        if struct.unpack_from("<I", spad, o)[0] == fill:
            return o
    return len(spad)


def fill_run_end(spad, start, fill):
    """The first offset >= start whose word is NOT the fill (the end of the never-written run), or 0x4000."""
    for o in range(start, len(spad), 4):
        if struct.unpack_from("<I", spad, o)[0] != fill:
            return o
    return len(spad)


def compare(old_path, new_path, old_spad, new_spad, fill, collect=None, text=None, decoder_ctx=None, decoder_text=None):
    th_o, th_n = threads(old_path), threads(new_path)
    sp_o, chain_o = boundary_sp(old_path)
    sp_n, chain_n = boundary_sp(new_path)
    counts = {k: 0 for k in CLASSES}
    res = {"old_sp": None if sp_o is None else "0x%x" % sp_o, "new_sp": None if sp_n is None else "0x%x" % sp_n,
           "chain_old": ["0x%x/ra 0x%x" % c for c in chain_o], "chain_new": ["0x%x/ra 0x%x" % c for c in chain_n],
           "threads_old": ["tid%d%s sp=%s" % (t[0], " boundary" if t[3] else "", None if t[4] is None else "0x%x" % t[4]) for t in th_o],
           "threads_new": ["tid%d%s sp=%s" % (t[0], " boundary" if t[3] else "", None if t[4] is None else "0x%x" % t[4]) for t in th_n]}
    # C1: the boundary `$sp` in the scratchpad domain on both images and equal (the stack switch is not active)
    c1 = sp_o is not None and sp_o == sp_n and in_spad(sp_o)
    # C2: no other thread's `$sp` in the scratchpad
    others = [("OLD", t) for t in th_o if not t[3] and t[4] is not None] + [("NEW", t) for t in th_n if not t[3] and t[4] is not None]
    other_in_spad = ["%s tid%d sp=0x%x" % (side, t[0], t[4]) for side, t in others if in_spad(t[4])]
    c2 = not other_in_spad
    # C3: no decode in flight -- the IPU DMA idle on both images (a missing DMAC section proves nothing) and no
    # thread's saved frame inside the decoder band (every THRD record with a context; no band -> nothing proved)
    busy_o, busy_n = ipu_dma_busy(old_path), ipu_dma_busy(new_path)
    dt = decoder_text
    chain_in_dec = (chain_frames_in(th_o, "OLD", dt) + chain_frames_in(th_n, "NEW", dt)) if dt else []
    ctx_o = ctx_n = []
    ctx_state_o = ctx_state_n = {}
    if decoder_ctx:
        _, _, so = state_hash.parse(old_path)
        _, _, sn = state_hash.parse(new_path)
        ram_o, ram_n = so[state_hash.TAG_RAM][2], sn[state_hash.TAG_RAM][2]
        ctx_o = decoder_contexts(ram_o, decoder_ctx)
        ctx_n = decoder_contexts(ram_n, decoder_ctx)
        ctx_state_o = {"0x%x" % a: decoder_ctx_state(ram_o, a) for a in ctx_o}
        ctx_state_n = {"0x%x" % a: decoder_ctx_state(ram_n, a) for a in ctx_n}
    c3_evaluable = busy_o is not None and busy_n is not None and dt is not None
    c3 = c3_evaluable and not busy_o and not busy_n and not chain_in_dec
    res["conditions"] = {
        "C1_sp_in_spad_both_equal": c1,
        "C2_no_other_stack_in_spad": c2, "other_sp_in_spad": other_in_spad,
        "C3_no_decode_in_flight": c3, "C3_evaluable": c3_evaluable,
        "ipu_dma_busy_old": busy_o, "ipu_dma_busy_new": busy_n, "chain_in_decoder_text": chain_in_dec,
        "threads_tested_old": len(th_o), "threads_tested_new": len(th_n),
        "decoder_ctx_spad_resident_old": ["0x%x" % a for a in ctx_o], "decoder_ctx_spad_resident_new": ["0x%x" % a for a in ctx_n],
        "decoder_ctx_state_old": ctx_state_o, "decoder_ctx_state_new": ctx_state_n,
        "decoder_ctx_state_equal": ctx_state_o == ctx_state_n,
        "decoder_text": None if not dt else "0x%x-0x%x" % dt}
    sp_off = (sp_o & SPAD_MASK) if c1 else None
    le = run_end = None
    if c1:
        le = min(live_end(old_spad, sp_off, fill), live_end(new_spad, sp_off, fill))
        run_end = min(fill_run_end(old_spad, le, fill), fill_run_end(new_spad, le, fill))
    res.update({"sp_off": None if sp_off is None else "0x%x" % sp_off, "live_end": None if le is None else "0x%x" % le,
                "fill_run": None if le is None else "[0x%x, 0x%x)" % (le, run_end),
                "first_written_above_fill_run": None if le is None else "0x%x" % run_end,
                "written_span_reach_below_sp": None if le is None else "0x%x" % ((sp_off - run_end) & SPAD_MASK if run_end < len(old_spad) else 0)})
    n = min(len(old_spad), len(new_spad))
    wo = struct.unpack_from("<%dI" % (n // 4), old_spad)
    wn = struct.unpack_from("<%dI" % (n // 4), new_spad)
    max_dist = 0
    code_words = 0
    slots = slots_zero_hi = 0
    for i in range(n // 4):
        if wo[i] == wn[i]:
            continue
        o = i * 4
        if not (c1 and c2 and c3_evaluable):
            cls = "spad-sp-unproved"
        elif sp_off <= o < le:
            cls = "spad-live-frame"
        elif not c3:
            cls = "spad-decoder-work"
        else:
            cls = "spad-dead-below-sp"
            d = (sp_off - o) & SPAD_MASK
            max_dist = max(max_dist, d)
            if text and (text[0] <= wo[i] < text[1] or text[0] <= wn[i] < text[1]):
                code_words += 1
            if not (o & 4) and i + 1 < n // 4:
                slots += 1
                if wo[i + 1] == 0 and wn[i + 1] == 0:
                    slots_zero_hi += 1
        counts[cls] += 1
        if collect is not None:
            collect[SPAD_BASE + o] = cls
    res["words"] = counts
    res["dead_evidence"] = {"max_distance_below_sp": "0x%x" % max_dist, "code_address_words": code_words,
                            "text": None if not text else "0x%x-0x%x" % text,
                            "slots_8_aligned": slots, "slots_zero_high_both": slots_zero_hi}
    return res


def render(res):
    c = res["conditions"]
    out = ["spad_stack: boundary sp OLD %s NEW %s -> SPAD offset %s, live frames [%s, %s), fill run %s (first written offset above it %s = %s below sp through the mirror; written span, not the stack's proven reach)"
           % (res["old_sp"], res["new_sp"], res["sp_off"], res["sp_off"], res["live_end"], res["fill_run"], res["first_written_above_fill_run"], res["written_span_reach_below_sp"])]
    out.append("  chain OLD: %s" % ", ".join(res["chain_old"]))
    out.append("  chain NEW: %s" % ", ".join(res["chain_new"]))
    out.append("  C1 sp in SPAD both/equal %s | C2 no other stack in SPAD %s %s | C3 no decode in flight %s%s (IPU DMA busy OLD %s NEW %s; frames of %d/%d threads in decoder text %s: %s; decoder ctx with scratchpad work buffers OLD %s NEW %s, state words equal %s)"
               % (c["C1_sp_in_spad_both_equal"], c["C2_no_other_stack_in_spad"], c["other_sp_in_spad"] or "", c["C3_no_decode_in_flight"],
                  "" if c["C3_evaluable"] else " [NOT EVALUABLE: DMAC section or decoder-text range missing]",
                  c["ipu_dma_busy_old"], c["ipu_dma_busy_new"], c["threads_tested_old"], c["threads_tested_new"], c["decoder_text"], c["chain_in_decoder_text"],
                  c["decoder_ctx_spad_resident_old"], c["decoder_ctx_spad_resident_new"], c["decoder_ctx_state_equal"]))
    for side in ("old", "new"):
        for a, st in c["decoder_ctx_state_%s" % side].items():
            out.append("    decoder ctx %s %s: %s" % (side.upper(), a, " ".join("%s=%s" % kv for kv in st.items())))
    for k in CLASSES:
        out.append("    %-24s %6d" % (k, res["words"][k]))
    e = res["dead_evidence"]
    out.append("  dead words: max distance below sp %s; code-address words %d (text %s); 8-byte slots %d, zero high word on both sides %d"
               % (e["max_distance_below_sp"], e["code_address_words"], e["text"], e["slots_8_aligned"], e["slots_zero_high_both"]))
    return "\n".join(out)


# ------------------------------------------------------------------ selftest

def _state(path, spad, sp, chain, vf=2, ram=None, extra_threads=(), dmac_str=(), extra_chains=None, dmac=True):
    """A minimal image: RAM0, SPAD, META, THRD (the boundary thread + extra (sp) threads, each with its own chain
    from extra_chains), DMAC (10 channels; omitted when dmac=False)."""
    import zlib

    def rec(tid, sp, chain, is_b):
        r = struct.pack("<i", tid) + struct.pack("<8I", 0x5000, 0x2000000, 0, 0, 0, 0, 0, 0) + struct.pack("<6i", 1, 0, 0, 0, 0, 0)
        r += bytes([1, 1, 1 if is_b else 0, 0]) + struct.pack("<4I", 0, 0, 1600, len(chain))
        ctx = bytearray(1600)
        struct.pack_into("<I", ctx, SP_GPR_OFFSET, sp)
        r += bytes(ctx)
        for f, ra in chain:
            r += struct.pack("<II", f, ra)
        return r
    thrd = struct.pack("<I", 1 + len(extra_threads)) + rec(1, sp, chain, True)
    for k, esp in enumerate(extra_threads):
        thrd += rec(2 + k, esp, (extra_chains or {}).get(k, []), False)
    dmac_regs = bytearray(10 * DMAC_CHANNEL_BYTES + 9 * 4 + 8)
    for ch in dmac_str:
        struct.pack_into("<I", dmac_regs, ch * DMAC_CHANNEL_BYTES, DMAC_CHCR_STR)
    secs = [(state_hash.TAG_RAM, bytes(ram) if ram is not None else bytes(0x1000)), (state_hash.TAG_SPAD, bytes(spad)),
            (state_hash.TAG_META, bytes(64)), (TAG_THRD, thrd)]
    if dmac:
        secs.append((TAG_DMAC, bytes(dmac_regs)))
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
    tmp = tempfile.mkdtemp(prefix="spad_selftest_")
    FILL = 0xa1b2c3d4
    TEXT = (0x500000, 0x600000)
    so, sn = bytearray(0x4000), bytearray(0x4000)
    for o in range(0, 0x4000, 4):
        struct.pack_into("<I", so, o, FILL)
        struct.pack_into("<I", sn, o, FILL)
    # live frames at [0x220, 0x270) on both; stale callee frames below; a deep callee WRAPPED to the top
    for o in range(0x220, 0x270, 4):
        struct.pack_into("<I", so, o, 0x1000 + o); struct.pack_into("<I", sn, o, 0x1000 + o)
    struct.pack_into("<I", so, 0x230, 0x59fd6c); struct.pack_into("<I", sn, 0x230, 0x59fd6c)
    struct.pack_into("<I", so, 0x240, 0x11); struct.pack_into("<I", sn, 0x240, 0x22)        # live word differs
    struct.pack_into("<I", so, 0x100, 0x33); struct.pack_into("<I", sn, 0x100, 0x44)        # dead below sp (slot 0x100/0x104, zero high word)
    struct.pack_into("<I", so, 0x104, 0); struct.pack_into("<I", sn, 0x104, 0)
    struct.pack_into("<I", so, 0x108, 0x33); struct.pack_into("<I", sn, 0x108, FILL)        # dead below sp (fill on one side)
    for o in range(0x3f00, 0x4000, 4):                                                       # the wrapped frame: written on both
        struct.pack_into("<I", so, o, 0x77); struct.pack_into("<I", sn, o, 0x77)
    struct.pack_into("<I", so, 0x3ff0, 0x55); struct.pack_into("<I", sn, 0x3ff0, 0x5a0150)  # wrapped: distance 0x230; NEW holds a code address
    struct.pack_into("<I", so, 0x3f08, 0x1); struct.pack_into("<I", sn, 0x3f08, 0x2)        # wrapped: distance 0x318 (slot 0x3f08/0x3f0c zero high)
    struct.pack_into("<I", so, 0x3f0c, 0); struct.pack_into("<I", sn, 0x3f0c, 0)
    po, pn = os.path.join(tmp, "o.ps2st"), os.path.join(tmp, "n.ps2st")
    chain = [(0x5a0150, 0x59fd6c), (0x59fd58, 0x57d68c)]
    ram_o, ram_n = bytearray(0x2000), bytearray(0x2000)
    SIG = parse_ctx_spec("0x5a0:0x70000000,0x5a4:0x70001800,0x6e0:0x70001b00,0x6e4:0x70003300,0x830:0x70003600")
    for o, v in SIG:                                                                          # a decoder context at 0x1000 (both)
        struct.pack_into("<I", ram_o, 0x1000 + o, v); struct.pack_into("<I", ram_n, 0x1000 + o, v)
    _state(po, so, 0x70004220, chain, ram=ram_o, extra_threads=(0x3fd0f0,))
    _state(pn, sn, 0x70004220, chain, ram=ram_n, extra_threads=(0x3fd100,))
    DEC = (0x5b0000, 0x5c0000)
    res = compare(po, pn, bytes(so), bytes(sn), FILL, text=TEXT, decoder_ctx=SIG, decoder_text=DEC)
    w, c, e = res["words"], res["conditions"], res["dead_evidence"]
    check(res["sp_off"] == "0x220" and res["live_end"] == "0x270", "sp 0x70004220 -> offset 0x220 (14-bit mirror), live frames end at the first fill word 0x270 -> %s/%s" % (res["sp_off"], res["live_end"]))
    check(w["spad-dead-below-sp"] == 4 and w["spad-live-frame"] == 1 and w["spad-decoder-work"] == 0 and w["spad-sp-unproved"] == 0,
          "classes: dead-below 4 (2 under sp + 2 wrapped through the mirror), live 1 -> %r" % w)
    check(e["max_distance_below_sp"] == "0x318" and e["code_address_words"] == 1 and e["slots_8_aligned"] == 4 and e["slots_zero_high_both"] == 2,
          "wrap distance (0x220 - 0x3f08) & 0x3fff = 0x318; one code-address word; 4 slots, 2 with zero high words -> %r" % e)
    check(res["fill_run"] == "[0x270, 0x3f00)" and res["first_written_above_fill_run"] == "0x3f00" and res["written_span_reach_below_sp"] == "0x320",
          "the fill run above the frames [0x270, 0x3f00); the first written offset above it 0x3f00 = 0x320 below sp (written span, not proven reach) -> %s / %s / %s" % (res["fill_run"], res["first_written_above_fill_run"], res["written_span_reach_below_sp"]))
    check(c["C1_sp_in_spad_both_equal"] and c["C2_no_other_stack_in_spad"] and c["C3_no_decode_in_flight"], "C1-C3 hold: sp in SPAD both, other thread's sp in RAM, IPU DMA idle, chain outside the decoder text -> %r" % c)
    check(c["decoder_ctx_spad_resident_old"] == ["0x1000"] and c["decoder_ctx_spad_resident_new"] == ["0x1000"], "the decoder context with scratchpad work buffers is found and REPORTED (not a decode) -> %r" % c["decoder_ctx_spad_resident_old"])
    check(c["decoder_ctx_state_old"]["0x1000"]["mode"] == "0x0" and c["decoder_ctx_state_old"]["0x1000"]["bank"] == "0x0" and c["decoder_ctx_state_equal"] and c["threads_tested_old"] == 2,
          "the context's state words (mode +0x87c, bank +0x820, ...) are read on both sides and compared; both threads tested -> %r" % c["decoder_ctx_state_old"])
    check(res["chain_old"][0] == "0x5a0150/ra 0x59fd6c" and res["threads_new"] == ["tid1 boundary sp=0x70004220", "tid2 sp=0x3fd100"], "chain and every thread's sp are read from THRD -> %r" % res["threads_new"])
    # C1 fails: sp disagreement
    pn2 = os.path.join(tmp, "n2.ps2st")
    _state(pn2, sn, 0x70004200, chain, ram=ram_n)
    res2 = compare(po, pn2, bytes(so), bytes(sn), FILL)
    check(res2["words"]["spad-sp-unproved"] == 5 and res2["sp_off"] is None and not res2["conditions"]["C1_sp_in_spad_both_equal"], "sp disagreement proves nothing: every word spad-sp-unproved -> %r" % res2["words"])
    # C1 fails: the live sp is in RAM on both (the SPAD-stack context switch active: the scratchpad is not the stack)
    po3, pn3 = os.path.join(tmp, "o3.ps2st"), os.path.join(tmp, "n3.ps2st")
    _state(po3, so, 0x00a01f0 + 0x4000, chain, ram=ram_o); _state(pn3, sn, 0x00a01f0 + 0x4000, chain, ram=ram_n)
    res3 = compare(po3, pn3, bytes(so), bytes(sn), FILL)
    check(res3["words"]["spad-sp-unproved"] == 5 and not res3["conditions"]["C1_sp_in_spad_both_equal"], "sp in RAM (stack switched out of the scratchpad): unproved -> %r" % res3["words"])
    # C2 fails: another thread's stack in the scratchpad
    pn4 = os.path.join(tmp, "n4.ps2st")
    _state(pn4, sn, 0x70004220, chain, ram=ram_n, extra_threads=(0x70003e00,))
    res4 = compare(po, pn4, bytes(so), bytes(sn), FILL)
    check(res4["words"]["spad-sp-unproved"] == 5 and res4["conditions"]["other_sp_in_spad"] == ["NEW tid2 sp=0x70003e00"], "another thread's sp in the scratchpad: unproved, the thread named -> %r" % res4["conditions"]["other_sp_in_spad"])
    # C3 fails: IPU DMA busy -> the non-live words are decoder work data (STATE), the live frame stays live
    pn5 = os.path.join(tmp, "n5.ps2st")
    _state(pn5, sn, 0x70004220, chain, ram=ram_n, dmac_str=(4,))
    res5 = compare(po, pn5, bytes(so), bytes(sn), FILL, decoder_ctx=SIG, decoder_text=DEC)
    check(res5["words"]["spad-decoder-work"] == 4 and res5["words"]["spad-live-frame"] == 1 and res5["conditions"]["ipu_dma_busy_new"] == [4],
          "IPU DMA ch4 STR set: a decode in flight -> non-live words spad-decoder-work (STATE), live frame kept -> %r" % res5["words"])
    # C3 fails: a chain frame inside the decoder library's text
    pn6 = os.path.join(tmp, "n6.ps2st")
    _state(pn6, sn, 0x70004220, [(0x5b1234, 0x59fd6c)] + chain, ram=ram_n)
    res6 = compare(po, pn6, bytes(so), bytes(sn), FILL, decoder_text=DEC)
    check(res6["words"]["spad-decoder-work"] == 4 and res6["conditions"]["chain_in_decoder_text"] == ["NEW tid1 0x5b1234"], "a boundary-chain frame in the decoder text: decoder-work, the thread and frame named -> %r" % res6["conditions"]["chain_in_decoder_text"])
    # C3 fails on a NON-boundary thread: tid 2 (its sp in RAM) suspended inside the decoder band (function frame) -- a decode on
    # the other thread with the IPU DMA idle between kicks; and the same through a RETURN address inside the band
    pn7 = os.path.join(tmp, "n7.ps2st")
    _state(pn7, sn, 0x70004220, chain, ram=ram_n, extra_threads=(0x3fd100,), extra_chains={0: [(0x5b2000, 0x5a0100), (0x5a0150, 0x59fd6c)]})
    res7 = compare(po, pn7, bytes(so), bytes(sn), FILL, decoder_ctx=SIG, decoder_text=DEC)
    check(res7["words"]["spad-decoder-work"] == 4 and res7["words"]["spad-live-frame"] == 1 and res7["conditions"]["chain_in_decoder_text"] == ["NEW tid2 0x5b2000"]
          and res7["conditions"]["C2_no_other_stack_in_spad"], "a NON-boundary thread's frame in the decoder text: decoder-work, tid2 named -> %r" % res7["conditions"]["chain_in_decoder_text"])
    pn7b = os.path.join(tmp, "n7b.ps2st")
    _state(pn7b, sn, 0x70004220, chain, ram=ram_n, extra_threads=(0x3fd100,), extra_chains={0: [(0x5a0150, 0x5b0010)]})
    res7b = compare(po, pn7b, bytes(so), bytes(sn), FILL, decoder_text=DEC)
    check(res7b["words"]["spad-decoder-work"] == 4 and res7b["conditions"]["chain_in_decoder_text"] == ["NEW tid2 0x5b0010"], "a return address inside the band counts too -> %r" % res7b["conditions"]["chain_in_decoder_text"])
    # C3 not evaluable: no DMAC section on one image -> nothing proved (fail closed); no decoder-text range -> the same
    pn8 = os.path.join(tmp, "n8.ps2st")
    _state(pn8, sn, 0x70004220, chain, ram=ram_n, dmac=False)
    res8 = compare(po, pn8, bytes(so), bytes(sn), FILL, decoder_text=DEC)
    check(res8["words"]["spad-sp-unproved"] == 5 and res8["conditions"]["ipu_dma_busy_new"] is None and not res8["conditions"]["C3_evaluable"] and not res8["conditions"]["C3_no_decode_in_flight"],
          "an image without a DMAC section: C3 not evaluable, every word unproved -> %r" % res8["words"])
    res9 = compare(po, pn, bytes(so), bytes(sn), FILL)
    check(res9["words"]["spad-sp-unproved"] == 5 and not res9["conditions"]["C3_evaluable"], "no decoder-text range: C3 not evaluable, every word unproved -> %r" % res9["words"])
    check("[NOT EVALUABLE" in render(res8) and "decoder ctx OLD 0x1000: mode=0x0" in render(res), "render flags an unevaluable C3 and prints the context state words")
    check("C1 sp in SPAD both/equal True" in render(res) and "live frames [0x220, 0x270)" in render(res), "render names the live span and the conditions")
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("spad_stack selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def _range(s):
    lo, hi = s.split("-", 1)
    return (int(lo, 16), int(hi, 16))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old", nargs="?")
    ap.add_argument("new", nargs="?")
    ap.add_argument("--fill", help="the crt0's scratchpad fill word (title data)")
    ap.add_argument("--text", help="lo-hi of the title's code (frame-shape evidence: code addresses in dead words)")
    ap.add_argument("--decoder-ctx", help="off:val,... the decoder context's scratchpad work-buffer signature (title data)")
    ap.add_argument("--decoder-text", help="lo-hi of the decoder library's text (a chain frame inside it = a decode in flight)")
    ap.add_argument("--out")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.old and a.new and a.fill):
        ap.error("old, new and --fill are required")
    _, _, so = state_hash.parse(a.old)
    _, _, sn = state_hash.parse(a.new)
    res = compare(a.old, a.new, so[state_hash.TAG_SPAD][2], sn[state_hash.TAG_SPAD][2], int(a.fill, 16),
                  text=_range(a.text) if a.text else None, decoder_ctx=parse_ctx_spec(a.decoder_ctx) if a.decoder_ctx else None,
                  decoder_text=_range(a.decoder_text) if a.decoder_text else None)
    print(render(res))
    if a.out:
        with open(a.out, "w") as f:
            json.dump(res, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
