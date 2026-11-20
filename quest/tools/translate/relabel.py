"""Replaces the English labels drawn in menu art (buttons, dialogs, PDA buttons) with Portuguese
text in the game's shaded caps fonts, recoloured to the colours of the original label."""
import sys
sys.path.insert(0, '/home/claude/tfevr/tools/translate')
from collections import Counter
from lfdlib import read_lfd, write_lfd, read_anim, write_anim, decode_delt, encode_delt, read_pltt
from goals_render import load_fonts, Shaded


def lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def ring_colors(pix, r):
    x0, y0, x1, y1 = r
    ring = []
    for x in range(x0, x1 + 1):
        for y in (y0, y1):
            if (x, y) in pix: ring.append(pix[(x, y)])
    for y in range(y0, y1 + 1):
        for x in (x0, x1):
            if (x, y) in pix: ring.append(pix[(x, y)])
    return set(ring)


def text_pixels(pix, r, bgset):
    x0, y0, x1, y1 = r
    return {(x, y): v for (x, y), v in pix.items() if x0 <= x <= x1 and y0 <= y <= y1 and v not in bgset}


def inpaint(pix, pts, r):
    """Each text pixel takes the colour of the nearest background pixel of the same row parity."""
    x0, y0, x1, y1 = r
    for (x, y) in sorted(pts):
        best = None
        for d in range(2, 200, 2):
            for xx in (x - d, x + d):
                if x0 - 6 <= xx <= x1 + 6 and (xx, y) in pix and (xx, y) not in pts:
                    best = pix[(xx, y)]; break
            if best is not None:
                break
        if best is None:
            for d in range(1, 200):
                for xx in (x - d, x + d):
                    if (xx, y) in pix and (xx, y) not in pts:
                        best = pix[(xx, y)]; break
                if best is not None:
                    break
        if best is not None:
            pix[(x, y)] = best


def scheme_of(txt, pal):
    cnt = Counter(txt.values())
    cols = [c for c, n in cnt.most_common()]
    bright = sorted(cols, key=lambda c: -lum(pal[c]))
    hi = bright[0]
    strong = [c for c, n in cnt.most_common() if lum(pal[c]) >= 0.55 * lum(pal[hi])]
    main = strong[0] if strong else cnt.most_common(1)[0][0]
    dark = [c for c in bright if lum(pal[c]) < lum(pal[main]) * 0.75]
    shadow = dark[len(dark) // 2] if dark else main
    return main, shadow, hi


def recolor(font, main, shadow, hi):
    f = Shaded.__new__(Shaded)
    m = {font.main: main, font.shadow: shadow, font.hi: hi}
    f.g = {c: {p: m.get(v, main if v not in (13, 90, 91, 141) else shadow) for p, v in g.items()} for c, g in font.g.items()}
    f.main, f.shadow, f.hi, f.space = main, shadow, hi, font.space
    return f


def line_tops(txt):
    ys = sorted(set(y for x, y in txt))
    g = []
    for y in ys:
        if g and y - g[-1][-1] <= 1:
            g[-1].append(y)
        else:
            g.append([y])
    # underline rows of hotkeys belong to the line above
    out = []
    for a in g:
        if out and len(a) <= 3 and a[0] - out[-1][-1] <= 2:
            out[-1] += a
        else:
            out.append(a)
    return [(a[0], a[-1]) for a in out]


TXT = set(range(6, 14)) | set(range(119, 125)) | set(range(162, 168)) | set(range(53, 64))


def relabel(pix, r, lines, font, pal, txtset=None, log=None, tag='', valign=None):
    x0, y0, x1, y1 = r
    cnt = Counter(v for (x, y), v in pix.items() if x0 <= x <= x1 and y0 <= y <= y1)
    if txtset == 'notbg':
        ts = set(cnt) - {cnt.most_common(1)[0][0]}
    elif txtset == 'black':
        ts = set(cnt) - {76, 77, 78, 79}
    elif isinstance(txtset, tuple) and txtset and txtset[0] == 'bg':
        ts = set(cnt) - set(txtset[1:])
    elif txtset is None:
        ts = set(cnt) - set(range(66, 80))     # button faces: blue grey dither
    else:
        ts = txtset
    txt = {(x, y): v for (x, y), v in pix.items() if x0 <= x <= x1 and y0 <= y <= y1 and v in ts}
    if not txt:
        if log is not None: log.append(f'{tag}: no text in {r}')
        return
    main, shadow, hi = scheme_of(txt, pal)
    f = recolor(font, main, shadow, hi)
    tops = line_tops(txt)
    tx0 = min(x for x, y in txt); tx1 = max(x for x, y in txt)
    inpaint(pix, set(txt), r)
    x0, y0, x1, y1 = r
    if len(tops) != len(lines):
        # spread the new lines over the old text band
        a, b = tops[0][0], tops[-1][1]
        h = 8
        n = len(lines)
        total = n * h + (n - 1) * 2
        start = (a + b + 1 - total) // 2
        tops = [(start + i * (h + 2), start + i * (h + 2) + h - 1) for i in range(n)]
    for (ta, tb), s in zip(tops, lines):
        w = f.width(s)
        cx = (x0 + x1) // 2 if valign != 'left' else None
        x = cx - w // 2 if cx is not None else tx0
        top = ta if (tb - ta) >= 6 else tb - 7
        if w > (x1 - x0 + 1) and log is not None:
            log.append(f'{tag}: "{s}" {w}px > {x1 - x0 + 1}px')
        miss = f.draw(pix, s, x, top)
        if miss and log is not None:
            log.append(f'{tag}: missing {sorted(miss)}')
