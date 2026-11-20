"""Shaded caps fonts of the PDA mission objectives (goals ANIM frames): learned per colour scheme
from the game's own frames; glyph = {(dx, dy): colour}, dy from the line top."""
import sys, json
sys.path.insert(0, '/home/claude/tfevr/tools/translate')
from lfdlib import read_lfd, read_anim, decode_delt

MAIN = {'g': {10, 11, 12}, 'y': {19, 20}, 'h': {10, 11, 12}}


def line_groups(pix, ymin, ymax, xmin=66):
    ys = sorted(set(y for (x, y) in pix if x >= xmin and ymin <= y <= ymax))
    g = []
    for y in ys:
        if g and y - g[-1][-1] <= 1:
            g[-1].append(y)
        else:
            g.append([y])
    return [(a[0], a[-1]) for a in g]


def glyph_blobs(pix, ya, yb, main, xmin=66, bg=()):
    """Blobs from columns holding main colour pixels; every other pixel joins the blob on its left."""
    pts = {(x, y): v for (x, y), v in pix.items() if ya <= y <= yb and x >= xmin and v not in bg}
    cols = sorted(set(x for (x, y), v in pts.items() if v in main))
    runs = []
    for x in cols:
        if runs and x == runs[-1][1] + 1:
            runs[-1][1] = x
        else:
            runs.append([x, x])
    blobs = []
    for i, (a, b) in enumerate(runs):
        nxt = runs[i + 1][0] if i + 1 < len(runs) else 10 ** 6
        g = {}
        for (x, y), v in pts.items():
            if a <= x < nxt and (v in main or x <= b + 2):
                g[(x - a, y - ya)] = v
        blobs.append((a, b, g))
    return blobs


def shape_key(g, main):
    return tuple(sorted(p for p, v in g.items() if v in main))


class ShadedFont:
    def __init__(self):
        self.g = {}      # char -> {(dx,dy): colour}
        self.shape = {}  # shape key -> char

    def learn(self, ch, g, main):
        if ch not in self.g or len(g) > len(self.g[ch]):
            self.g[ch] = dict(g)
        self.shape[shape_key(g, main)] = ch

    def save(self, path):
        json.dump({c: [[x, y, v] for (x, y), v in g.items()] for c, g in self.g.items()}, open(path, 'w'))

    def load(self, path):
        for c, pts in json.load(open(path)).items():
            self.g[c] = {(x, y): v for x, y, v in pts}
        return self
