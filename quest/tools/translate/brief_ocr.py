"""Glyph library + OCR for the briefing fonts (exact pixel matching)."""
import sys, json
sys.path.insert(0, '/home/claude/tfevr/tools/translate')
from lfdlib import read_lfd
from brief_seg import segment, blobs, bar_lines, BAR


def baseline(rows, ya, yb, ignore=(BAR,)):
    cnt = [sum(1 for v in rows[y].values() if v not in ignore) for y in range(ya, yb + 1)]
    m = max(cnt)
    base = ya
    for i, c in enumerate(cnt):
        if c >= 0.4 * m:
            base = ya + i
    return base


def blob_key(rows, x0, x1, ya, yb, base, ignore=(BAR,)):
    pts = []
    for y in range(ya, yb + 1):
        for x in range(x0, x1 + 1):
            v = rows[y].get(x)
            if v is not None and v not in ignore:
                pts.append((x - x0, y - base))
    return tuple(sorted(pts))


def line_blobs(rows, ya, yb, ignore=(BAR,), pad=3):
    """Blobs with baseline-relative keys. The band is widened a little (accents/descenders of
    neighbours never touch other lines: lines are 12 apart)."""
    base = baseline(rows, ya, yb, ignore)
    bs = blobs(rows, ya, yb, ignore)
    return base, [(x0, x1, blob_key(rows, x0, x1, ya, yb, base, ignore)) for x0, x1, _ in bs]


def key_width(k):
    return max(p[0] for p in k) + 1 if k else 0


def split_key(k, at):
    a = tuple(sorted(p for p in k if p[0] < at))
    b = tuple(sorted((p[0] - at, p[1]) for p in k if p[0] >= at))
    return a, b


class Lib:
    def __init__(self):
        self.g = {}   # key(tuple) -> char
        self.n = {}   # key -> times seen

    def learn(self, key, ch):
        self.g[key] = ch
        self.n[key] = self.n.get(key, 0) + 1

    def best(self):
        """char -> most frequent key"""
        out = {}
        for k, c in self.g.items():
            if c not in out or self.n.get(k, 0) > self.n.get(out[c], 0):
                out[c] = k
        return out

    def ocr_blob(self, key, depth=0):
        if key in self.g:
            return self.g[key]
        if depth > 3:
            return None
        w = key_width(key)
        # merged glyphs (touching): try every split where the left part is a known glyph
        for at in range(1, w):
            a, b = split_key(key, at)
            if a in self.g and b:
                rest = self.ocr_blob(b, depth + 1)
                if rest:
                    return self.g[a] + rest
        return None

    def save(self, path):
        json.dump([[list(map(list, k)), c, self.n.get(k, 1)] for k, c in self.g.items()], open(path, 'w'))

    def load(self, path):
        for e in json.load(open(path)):
            k = tuple(tuple(p) for p in e[0])
            self.g[k] = e[1]
            self.n[k] = e[2] if len(e) > 2 else 1
        return self


def ocr_line(lib, rows, ya, yb, ignore=(BAR,), space=3):
    base, bs = line_blobs(rows, ya, yb, ignore)
    out = ''
    unknown = []
    prev = None
    for x0, x1, k in bs:
        if prev is not None and x0 - prev - 1 >= space:
            out += ' '
        c = lib.ocr_blob(k)
        if c is None:
            out += '█'
            unknown.append(k)
        else:
            out += c
        prev = x1
    return out, unknown, (bs[0][0] if bs else 0)
