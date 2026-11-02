#!/usr/bin/env python3
"""gen_fps_glyphs.py -- app/gui/fps_glyphs.h: the FPS counter's stroke glyph set.

    python3 tools/gen_fps_glyphs.py > app/gui/fps_glyphs.h
    python3 tools/gen_fps_glyphs.py --check app/gui/fps_glyphs.h
    python3 tools/gen_fps_glyphs.py --svg out.svg          # a preview sheet

An ORIGINAL vector glyph set, authored here as control polylines and committed
as generated source: no font or texture bytes from any game disc are involved.
The look aims at a bold, rounded, comic-style HUD numeral -- heavy strokes,
round caps, a dark outline, a slight forward lean -- which the renderer
(app/gui/fps_overlay.cpp) draws from these polylines with ImDrawList::AddPolyline:
an outline pass (a wider dark polyline + round caps) under a fill pass. Colours
are not part of the glyphs: the renderer takes them from a theme the title
supplies.

Each glyph lives in a cell `advance` units wide and 14 units tall (y down, the
baseline at y = 14). Control polylines are hand-placed on that grid; corners are
rounded by two rounds of Chaikin corner cutting, except the points marked
sharp ('s'). A closed path is written with its first point repeated by the
generator.

The header is a C++ table in namespace ps2r::app::fps_glyphs (included by fps_overlay.cpp):
    struct FpsGlyphPoint { float x, y; };
    struct FpsGlyphStroke { uint16_t first, count; };     // into kFpsGlyphPoints
    struct FpsGlyph { char ch; float advance; uint16_t first_stroke, stroke_count; };
"""
import argparse
import sys

CELL_H = 14.0
# ---------------------------------------------------------------------------
# The control polylines. A point is (x, y) or (x, y, 's') = keep the corner sharp.
# A stroke tuple whose last element is the string "closed" is a closed path.
GLYPHS = {
    "0": (10.0, [
        [(5, 0), (8.6, 1.4), (10, 5), (10, 9), (8.6, 12.6), (5, 14), (1.4, 12.6), (0, 9), (0, 5), (1.4, 1.4), "closed"],
    ]),
    "1": (7.0, [
        [(0.6, 3.6), (4.2, 0, 's'), (4.2, 14, 's')],
    ]),
    "2": (10.0, [
        [(0.6, 3.6), (1.8, 1.0), (5, 0), (8.4, 1.0), (9.8, 3.6), (8.8, 6.6), (5.2, 9.4), (0.2, 14, 's'), (10, 14, 's')],
    ]),
    "3": (10.0, [
        [(0.8, 2.8), (3, 0.4), (6.4, 0), (9.2, 2.0), (9.0, 5.0), (5.6, 6.8, 's'), (9.2, 8.6), (10, 11.2), (7.0, 14), (3.0, 14), (0.4, 11.6)],
    ]),
    "4": (10.0, [
        [(7.4, 0, 's'), (7.4, 14, 's')],
        [(7.4, 0, 's'), (0.2, 10.2), (10, 10.2, 's')],
    ]),
    "5": (10.0, [
        [(9.4, 0, 's'), (1.6, 0, 's'), (0.8, 6.4), (4.0, 5.3), (7.4, 5.6), (9.8, 8.6), (9.4, 12), (6.6, 14), (2.6, 14), (0.2, 11.8)],
    ]),
    "6": (10.0, [
        [(8.4, 1.2), (6.0, 0), (3.2, 0.6), (1.0, 3.6), (0, 8), (0.6, 11.8), (3, 14), (6.6, 14), (9.6, 12), (10, 9.2), (8.6, 6.6), (5.6, 5.6), (2.6, 6.3), (0.4, 8.6)],
    ]),
    "7": (10.0, [
        [(0, 0, 's'), (10, 0, 's'), (3.6, 14, 's')],
    ]),
    "8": (10.0, [
        [(5, 6.6), (1.6, 4.6), (1.6, 1.8), (5, 0), (8.4, 1.8), (8.4, 4.6), (5, 6.6), (9.4, 9.4), (9.6, 12), (5, 14), (0.4, 12), (0.6, 9.4), "closed"],
    ]),
    "9": (10.0, [
        [(1.6, 12.8), (4.0, 14), (6.8, 13.4), (9.0, 10.4), (10, 6), (9.4, 2.2), (7, 0), (3.4, 0), (0.4, 2), (0, 4.8), (1.4, 7.4), (4.4, 8.4), (7.4, 7.7), (9.6, 5.4)],
    ]),
    ".": (3.6, [
        [(1.6, 12.6), (1.6, 13.4)],
    ]),
    # Lowercase labels ("fps", "ms"): x-height 8.4 (the small glyphs sit on the baseline).
    "f": (6.4, [
        [(5.6, 0.8), (4.0, 0), (2.4, 1.4), (2.2, 14, 's')],
        [(0, 5.8, 's'), (4.8, 5.8, 's')],
    ]),
    "p": (8.4, [
        [(0.6, 5.8, 's'), (0.6, 14, 's')],
        [(0.6, 7.6), (2.6, 5.8), (5.6, 5.8), (7.6, 8.0), (7.6, 10.6), (5.6, 12.6), (2.6, 12.6), (0.6, 10.8)],
    ]),
    "s": (7.4, [
        [(6.8, 7.0), (5.0, 5.8), (2.2, 5.8), (0.6, 7.4), (1.6, 9.2), (5.4, 10.0), (7.0, 11.8), (5.4, 14), (2.2, 14), (0.2, 12.6)],
    ]),
    "m": (11.0, [
        [(0.6, 14, 's'), (0.6, 5.8, 's')],
        [(0.6, 7.6), (2.2, 5.8), (4.2, 6.4), (5.2, 8.4, 's'), (5.2, 14, 's')],
        [(5.2, 7.6), (6.8, 5.8), (8.8, 6.4), (9.8, 8.4), (9.8, 14, 's')],
    ]),
}
ORDER = "0123456789.fpsm"


def chaikin(points, closed, rounds=2):
    """Corner cutting; points marked sharp stay fixed; the ends of an open path stay fixed."""
    pts = [(float(p[0]), float(p[1]), len(p) > 2 and p[2] == 's') for p in points]
    for _ in range(rounds):
        out = []
        n = len(pts)
        if closed:
            for i in range(n):
                a, b = pts[i], pts[(i + 1) % n]
                if a[2]:
                    out.append(a)
                if a[2] and b[2]:
                    continue
                qa = (0.75 * a[0] + 0.25 * b[0], 0.75 * a[1] + 0.25 * b[1], False)
                qb = (0.25 * a[0] + 0.75 * b[0], 0.25 * a[1] + 0.75 * b[1], False)
                if not a[2]:
                    out.append(qa)
                if not b[2]:
                    out.append(qb)
        else:
            out.append(pts[0])
            for i in range(n - 1):
                a, b = pts[i], pts[i + 1]
                if i > 0 and a[2]:
                    out.append(a)
                qa = (0.75 * a[0] + 0.25 * b[0], 0.75 * a[1] + 0.25 * b[1], False)
                qb = (0.25 * a[0] + 0.75 * b[0], 0.25 * a[1] + 0.75 * b[1], False)
                if not a[2] and i > 0:
                    out.append(qa)
                if not b[2] and i + 1 < n - 1:
                    out.append(qb)
            out.append(pts[-1])
        pts = out
    return [(round(p[0], 3), round(p[1], 3)) for p in pts]


def build():
    points, strokes, glyphs = [], [], []
    for ch in ORDER:
        advance, raw_strokes = GLYPHS[ch]
        first_stroke = len(strokes)
        for stroke in raw_strokes:
            closed = stroke[-1] == "closed"
            ctrl = stroke[:-1] if closed else stroke
            poly = chaikin(ctrl, closed)
            if closed:
                poly.append(poly[0])
            for x, y in poly:
                assert -0.01 <= x <= advance + 0.01 and -0.01 <= y <= CELL_H + 0.01, (ch, x, y)
            strokes.append((len(points), len(poly)))
            points.extend(poly)
        glyphs.append((ch, advance, first_stroke, len(raw_strokes)))
    return points, strokes, glyphs


def flit(v):
    """A C++ float literal with a decimal point (14 -> 14.0f)."""
    t = "%g" % v
    if "." not in t and "e" not in t:
        t += ".0"
    return t + "f"


def render(points, strokes, glyphs):
    out = []
    out.append("#pragma once")
    out.append("// GENERATED by tools/gen_fps_glyphs.py -- do not edit; edit the control polylines there.")
    out.append("// The FPS counter's stroke glyph set: an original design (no game asset), see the script's header.")
    out.append("// Cell: `advance` units wide, %g tall, y down, baseline at y = %g." % (CELL_H, CELL_H))
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace ps2r::app::fps_glyphs {")
    out.append("")
    out.append("struct FpsGlyphPoint { float x, y; };")
    out.append("struct FpsGlyphStroke { uint16_t first, count; };   // into kFpsGlyphPoints")
    out.append("struct FpsGlyph { char ch; float advance; uint16_t first_stroke, stroke_count; };")
    out.append("")
    out.append("inline constexpr float kFpsGlyphCellHeight = %s;" % flit(CELL_H))
    out.append("inline constexpr FpsGlyphPoint kFpsGlyphPoints[] = {")
    line = "   "
    for x, y in points:
        item = " {%s, %s}," % (flit(x), flit(y))
        if len(line) + len(item) > 116:
            out.append(line)
            line = "   "
        line += item
    out.append(line)
    out.append("};")
    out.append("inline constexpr FpsGlyphStroke kFpsGlyphStrokes[] = {")
    out.append("    " + " ".join("{%d, %d}," % s for s in strokes))
    out.append("};")
    out.append("inline constexpr FpsGlyph kFpsGlyphs[] = {")
    for ch, advance, first, count in glyphs:
        out.append("    {'%s', %s, %d, %d}," % (ch, flit(advance), first, count))
    out.append("};")
    out.append("")
    out.append("}  // namespace ps2r::app::fps_glyphs")
    return "\n".join(out) + "\n"


def svg(points, strokes, glyphs):
    s = 6.0
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" style="background:#223">' % (s * 190, s * 20)]
    x0 = 2.0
    for ch, advance, first, count in glyphs:
        for si in range(first, first + count):
            p0, n = strokes[si]
            pts = points[p0:p0 + n]
            d = " ".join("%g,%g" % ((x0 + x + (CELL_H - y) * 0.12) * s, (y + 3) * s) for x, y in pts)
            for col, w in (("#0b1430", 3.2 * s), ("#f2c54a", 1.8 * s)):
                parts.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="%g" stroke-linecap="round" stroke-linejoin="round"/>' % (d, col, w))
        x0 += advance + 2.0
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", metavar="FILE")
    ap.add_argument("--svg", metavar="FILE")
    a = ap.parse_args()
    points, strokes, glyphs = build()
    if a.svg:
        with open(a.svg, "w") as f:
            f.write(svg(points, strokes, glyphs))
        return 0
    text = render(points, strokes, glyphs)
    if a.check:
        with open(a.check) as f:
            current = f.read()
        if current != text:
            sys.stderr.write("%s is stale: regenerate with tools/gen_fps_glyphs.py\n" % a.check)
            return 1
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
