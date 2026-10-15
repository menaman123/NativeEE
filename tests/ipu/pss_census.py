#!/usr/bin/env python3
# tests/ipu/pss_census.py -- offline MPEG program-stream census (the oracle of the
# PssDemux ctest's OLD-gated half). Demuxes the video PES payload with the SAME
# packet-walk rules engine/ipu/hle/pss_demux.cpp uses, then scans the joined
# elementary stream for MPEG-2 structural codes:
#   00 00 01 B3 sequence_header   00 00 01 B7 sequence_end
#   00 00 01 B8 GOP header        00 00 01 00 picture_start
#   00 00 01 B9 program_end (PS layer, counted on the PS side)
# Reports per-sequence picture counts and byte offsets (ES + source file).
# Usage: python3 tests/ipu/pss_census.py <file.PSS>
import sys, struct

def be16(b, i): return (b[i] << 8) | b[i+1]

def pes_payload_off(pkt):
    # mirrors PssDemux::ParsePesPayloadOffset
    n = len(pkt)
    if n <= 6: return n
    pos = 6
    if n >= 9 and (pkt[pos] & 0xC0) == 0x80:
        return min(n, 9 + pkt[pos+2])
    while pos < n and pkt[pos] == 0xFF: pos += 1
    if pos + 1 < n and (pkt[pos] & 0xC0) == 0x40: pos += 2
    if pos >= n: return n
    f = pkt[pos]
    if   (f & 0xF0) == 0x20: pos += 5
    elif (f & 0xF0) == 0x30: pos += 10
    elif f == 0x0F:          pos += 1
    return min(n, pos)

def main(path):
    data = open(path, 'rb').read()
    n = len(data)
    i = 0
    es = bytearray()
    es_src = []   # (es_off, file_off) anchors per PES payload
    n_pes_video = n_pes_audio = n_pack = n_sys = 0
    n_prog_end = 0
    prog_end_offs = []
    while i + 3 < n:
        if not (data[i] == 0 and data[i+1] == 0 and data[i+2] == 1):
            i += 1
            continue
        sid = data[i+3]
        if sid == 0xB9:
            n_prog_end += 1
            prog_end_offs.append(i)
            i += 4
            continue
        if sid == 0xBA:
            n_pack += 1
            if i + 13 < n and (data[i+4] & 0xC0) == 0x40:
                i += 14 + (data[i+13] & 7)
            else:
                i += 12
            continue
        if i + 5 >= n: break
        plen = be16(data, i+4)
        end = i + 6 + plen
        if sid in (0xBB, 0xBC, 0xBE, 0xBF, 0xF0, 0xF1, 0xF2, 0xF8, 0xFF):
            n_sys += 1
            i = end
            continue
        if 0xE0 <= sid <= 0xEF:
            n_pes_video += 1
            pkt = data[i:end]
            po = pes_payload_off(pkt)
            if po < len(pkt):
                es_src.append((len(es), i + po))
                es += pkt[po:]
            i = end
            continue
        if sid == 0xBD or (0xC0 <= sid <= 0xDF):
            n_pes_audio += 1
            i = end
            continue
        i += 1  # unknown id: resync

    def file_off(es_off):
        # binary-search the last anchor <= es_off
        lo, hi = 0, len(es_src) - 1
        best = (0, 0)
        while lo <= hi:
            mid = (lo + hi) // 2
            if es_src[mid][0] <= es_off:
                best = es_src[mid]; lo = mid + 1
            else:
                hi = mid - 1
        return best[1] + (es_off - best[0])

    # scan ES for structural codes
    seqs = []      # per sequence: dict
    cur = None
    total_pics = 0
    seq_end_offs = []
    j = 0
    m = len(es)
    while j + 3 < m:
        if not (es[j] == 0 and es[j+1] == 0 and es[j+2] == 1):
            j += 1
            continue
        code = es[j+3]
        if code == 0xB3:
            if cur is None:
                w = (es[j+4] << 4) | (es[j+5] >> 4) if j + 6 < m else 0
                h = ((es[j+5] & 0xF) << 8) | es[j+6] if j + 6 < m else 0
                cur = dict(es_off=j, f_off=file_off(j), w=w, h=h,
                           pics=0, gops=0, first_pic_global=total_pics)
        elif code == 0xB8:
            if cur is not None: cur['gops'] += 1
        elif code == 0x00:
            if cur is not None:
                cur['pics'] += 1
                total_pics += 1
        elif code == 0xB7:
            seq_end_offs.append((j, file_off(j)))
            if cur is not None:
                cur['end_es_off'] = j
                seqs.append(cur)
                cur = None
        j += 4
    if cur is not None:
        cur['end_es_off'] = -1
        seqs.append(cur)

    print(f"file: {path}  size={n}")
    print(f"PS layer: video_pes={n_pes_video} audio_pes={n_pes_audio} "
          f"packs={n_pack} sys={n_sys} program_end(B9)={n_prog_end} "
          f"at={['0x%x' % o for o in prog_end_offs]}")
    print(f"ES: bytes={m} sequence_end(B7)={len(seq_end_offs)} "
          f"total_pictures={total_pics}")
    for k, (eo, fo) in enumerate(seq_end_offs):
        print(f"  seq_end#{k}: es_off=0x{eo:x} file_off=0x{fo:x}")
    for k, s in enumerate(seqs):
        print(f"  sequence#{k}: {s['w']}x{s['h']} start_es=0x{s['es_off']:x} "
              f"file=0x{s['f_off']:x} pics={s['pics']} gops={s['gops']} "
              f"global_pic_range=[{s['first_pic_global']}"
              f"..{s['first_pic_global'] + s['pics'] - 1}] "
              f"end_es=0x{s['end_es_off']:x}")

if __name__ == '__main__':
    main(sys.argv[1])
