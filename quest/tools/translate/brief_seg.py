"""Segments a Dark Forces briefing DELT (198 px wide) into bars, text lines and pictures,
and splits text lines into glyph blobs."""
import sys
sys.path.insert(0, '/home/claude/tfevr/tools/translate')
from lfdlib import decode_delt
from collections import Counter

W = 198
BAR = 14


def rows_of(pix, h):
    rows = [dict() for _ in range(h)]
    for (x, y), v in pix.items():
        if 0 <= y < h:
            rows[y][x] = v
    return rows


def is_bar(r):
    return len(r) >= 190 and Counter(r.values()).most_common(1)[0][0] == BAR


def segment(delt):
    x0, y0, x1, y1, pix = decode_delt(delt)
    h = y1 + 1
    rows = rows_of(pix, h)
    blocks = []   # (kind, y0, y1, extra)
    y = 0
    while y < h:
        r = rows[y]
        if is_bar(r):
            s = y
            while y < h and is_bar(rows[y]):
                y += 1
            blocks.append(('bar', s, y - 1))
            continue
        if not r:
            y += 1
            continue
        s = y
        while y < h and rows[y] and not is_bar(rows[y]):
            y += 1
        cols = Counter(v for yy in range(s, y) for v in rows[yy].values())
        if len(cols) == 1 and y - s <= 11:
            blocks.append(('text', s, y - 1, list(cols)[0]))
        else:
            blocks.append(('pic', s, y - 1, dict(cols.most_common(3))))
    return h, rows, blocks


def bar_lines(rows, b0, b1):
    """Text lines inside a bar: groups of rows with non bar pixels."""
    out = []
    y = b0
    while y <= b1:
        has = [x for x, v in rows[y].items() if v != BAR]
        if not has:
            y += 1
            continue
        s = y
        while y <= b1 and any(v != BAR for v in rows[y].values()):
            y += 1
        cols = Counter(v for yy in range(s, y) for v in rows[yy].values() if v != BAR)
        out.append((s, y - 1, cols.most_common(1)[0][0]))
    return out


def blobs(rows, ya, yb, ignore=(BAR,)):
    """Glyph blobs of a line: (x0, x1, bitmap string). Blobs = runs of non empty columns."""
    colset = {}
    for y in range(ya, yb + 1):
        for x, v in rows[y].items():
            if v in ignore:
                continue
            colset.setdefault(x, set()).add(y)
    xs = sorted(colset)
    out = []
    i = 0
    while i < len(xs):
        j = i
        while j + 1 < len(xs) and xs[j + 1] == xs[j] + 1:
            j += 1
        bx0, bx1 = xs[i], xs[j]
        bm = []
        for y in range(ya, yb + 1):
            bm.append(''.join('#' if (x in colset and y in colset[x]) else '.' for x in range(bx0, bx1 + 1)))
        out.append((bx0, bx1, '\n'.join(bm)))
        i = j + 1
    return out
