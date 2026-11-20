"""Text slots of a briefing: every text line (also the ones beside pictures), with the horizontal
room it has, grouped into paragraphs."""
import sys
sys.path.insert(0, '/home/claude/tfevr/tools/translate')
from lfdlib import decode_delt
from brief_seg import segment, BAR, W
from brief_ocr import baseline
from collections import Counter

TEXT_COLORS = (10, 11, 5)


def components(pts):
    """8-connected components of a set of points."""
    pts = set(pts)
    comps = []
    while pts:
        s = pts.pop()
        stack = [s]; comp = [s]
        while stack:
            x, y = stack.pop()
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    q = (x + dx, y + dy)
                    if q in pts:
                        pts.remove(q); stack.append(q); comp.append(q)
        comps.append(comp)
    return comps


def find_slots(delt, lib=None):
    h, rows, blocks = segment(delt)
    slots = []   # dict(ya, yb, base, x0, xs, xe, color, inpic)
    picmask = set()
    for b in blocks:
        if b[0] == 'text':
            ya, yb, col = b[1], b[2], b[3]
            xs_ = [x for y in range(ya, yb + 1) for x in rows[y]]
            slots.append(dict(ya=ya, yb=yb, base=baseline(rows, ya, yb), x0=min(xs_), x1=max(xs_), xs=min(xs_), xe=W - 1, color=col, inpic=False))
        elif b[0] == 'pic':
            ya, yb = b[1], b[2]
            pts = [(x, y) for y in range(ya, yb + 1) for x in rows[y]]
            text_pts = set()
            for comp in components(pts):
                cols = set(rows[y][x] for x, y in comp)
                hgt = max(p[1] for p in comp) - min(p[1] for p in comp) + 1
                if cols <= set(TEXT_COLORS) and hgt <= 10:
                    text_pts.update(comp)
                else:
                    picmask.update(comp)
            # group text points into lines (row runs)
            ys = sorted(set(p[1] for p in text_pts))
            groups = []
            for y in ys:
                if groups and y - groups[-1][-1] <= 1:
                    groups[-1].append(y)
                else:
                    groups.append([y])
            for g in groups:
                gy0, gy1 = g[0], g[-1]
                lp = [p for p in text_pts if gy0 <= p[1] <= gy1]
                if len(lp) < 12:
                    continue   # stray pixels of a picture
                col = Counter(rows[y][x] for x, y in lp).most_common(1)[0][0]
                if lib is not None:
                    # keep only the run of recognised glyphs (picture bits that look like text are dropped)
                    from brief_ocr import line_blobs
                    rr = [dict() for _ in rows]
                    for (x, y) in lp: rr[y][x] = rows[y][x]
                    base_, bs = line_blobs(rr, gy0, gy1)
                    ok = [b for b in bs if lib.ocr_blob(b[2])]
                    if sum(1 for b in ok if lib.ocr_blob(b[2]).isalpha()) < 6 or len(ok) < 0.6 * len(bs):
                        continue
                    # drop leading/trailing unrecognised blobs
                    i0 = next(i for i, b in enumerate(bs) if lib.ocr_blob(b[2]))
                    i1 = max(i for i, b in enumerate(bs) if lib.ocr_blob(b[2]))
                    keepx = (bs[i0][0], bs[i1][1])
                    lp = [p for p in lp if keepx[0] <= p[0] <= keepx[1]]
                    for b in bs[:i0] + bs[i1 + 1:]:
                        picmask.update(p for p in text_pts if b[0] <= p[0] <= b[1] and gy0 <= p[1] <= gy1)
                x0 = min(p[0] for p in lp); x1 = max(p[0] for p in lp)
                # horizontal room: up to the picture on the right (if any) or the edge
                band = [p for p in picmask if gy0 - 3 <= p[1] <= gy1 + 3]
                right = [p[0] for p in band if p[0] > x1]
                left = [p[0] for p in band if p[0] < x0]
                xe = (min(right) - 4) if right else W - 1
                xs = (max(left) + 4) if left else 0
                xs = min(xs, x0)
                if left and x0 - max(left) < 3:
                    continue
                # baseline from the text pixels only
                cnt = Counter(p[1] for p in lp)
                m = max(cnt.values())
                base = max(y for y, c in cnt.items() if c >= 0.4 * m)
                slots.append(dict(ya=gy0, yb=gy1, base=base, x0=x0, x1=x1, xs=x0, xe=xe, color=col, inpic=True, text=set(lp)))
    slots.sort(key=lambda s: s['ya'])
    # paragraphs: consecutive slots of the same color 11..13 rows apart
    paras = []
    for s in slots:
        if paras:
            p = paras[-1][-1]
            if s['color'] == p['color'] and 10 <= s['base'] - p['base'] <= 14 and not (s['color'] == 5 and s['x0'] < 3 and p['x0'] < 3 and False):
                # objectives: a new "- " item starts a new paragraph
                if not (s['color'] == 5 and s['x0'] <= 1):
                    paras[-1].append(s); continue
        paras.append([s])
    return h, rows, blocks, slots, paras, picmask
