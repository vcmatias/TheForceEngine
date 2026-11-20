"""Pixel fonts built from glyphs learned out of the game's own images, with Portuguese accents
added in the same pixel style. A glyph = list of (dx, dy) points, dy relative to the baseline."""
import sys
sys.path.insert(0, '/home/claude/tfevr/tools/translate')
from brief_ocr import Lib, key_width

ACC = {
    'á': ('a', 'acute'), 'à': ('a', 'grave'), 'â': ('a', 'circ'), 'ã': ('a', 'tilde'),
    'é': ('e', 'acute'), 'ê': ('e', 'circ'), 'í': ('i', 'acute'), 'ó': ('o', 'acute'),
    'ô': ('o', 'circ'), 'õ': ('o', 'tilde'), 'ú': ('u', 'acute'), 'ü': ('u', 'uml'),
    'Á': ('A', 'acute'), 'À': ('A', 'grave'), 'Â': ('A', 'circ'), 'Ã': ('A', 'tilde'),
    'É': ('E', 'acute'), 'Ê': ('E', 'circ'), 'Í': ('I', 'acute'), 'Ó': ('O', 'acute'),
    'Ô': ('O', 'circ'), 'Õ': ('O', 'tilde'), 'Ú': ('U', 'acute'),
}


def mark(kind, cx, top):
    """Accent pixels centred on column cx, lowest row = top (rows go up from there)."""
    if kind == 'acute':
        return [(cx, top), (cx + 1, top - 1)]
    if kind == 'grave':
        return [(cx, top), (cx - 1, top - 1)]
    if kind == 'circ':
        return [(cx - 1, top), (cx, top - 1), (cx + 1, top)]
    if kind == 'tilde':
        return [(cx - 1, top), (cx, top - 1), (cx + 1, top), (cx + 2, top - 1)]
    if kind == 'uml':
        return [(cx - 1, top), (cx + 1, top)]
    return []


class GlyphFont:
    def __init__(self, glyphs, space=4, gap=1, upper_only=False, accent_gap=1, cap_accent_rows=1):
        self.g = {c: list(k) for c, k in glyphs.items()}
        self.space = space
        self.gap = gap
        self.upper_only = upper_only
        if 'I' not in self.g and 'l' in self.g:
            self.g['I'] = self.g['l']
        if 'l' not in self.g and 'I' in self.g:
            self.g['l'] = self.g['I']
        self.accent_gap = accent_gap
        self.cap_accent_rows = cap_accent_rows
        self._derive()

    def _derive(self):
        g = self.g
        if 'Q' not in g and 'O' in g:
            o = g['O']; w = max(p[0] for p in o)
            g['Q'] = o + [(w, 1), (w + 1, 1)] if (w, 1) not in o else o + [(w + 1, 1)]
        if ';' not in g and ',' in g and ':' in g:
            top = [p for p in g[':'] if p[1] == min(q[1] for q in g[':'])]
            g[';'] = g[','] + [(top[0][0] + (1 if len(g[',']) > 1 else 0), top[0][1])]
        if '!' not in g and 'l' in g:
            l = g['l']; bottom = max(p[1] for p in l)
            g['!'] = [p for p in l if p[1] < bottom - 1] + [(l[0][0], bottom)]
        if '"' not in g and "'" in g:
            a = g["'"]; w = max(p[0] for p in a) + 2
            g['"'] = a + [(x + w, y) for x, y in a]
        # cedilla
        for base, ch in (('c', 'ç'), ('C', 'Ç')):
            if base in g and ch not in g:
                pts = g[base]; w = max(p[0] for p in pts) + 1
                cx = w // 2
                g[ch] = pts + [(cx, 1), (cx, 2), (cx - 1, 2)]
        for ch, (base, kind) in ACC.items():
            if ch in g or base not in g:
                continue
            pts = list(g[base])
            if base == 'i':
                top0 = min(p[1] for p in pts)
                stem_top = min(p[1] for p in pts if p[1] > top0 + 1) if any(p[1] > top0 + 1 for p in pts) else top0
                pts = [p for p in pts if p[1] >= stem_top]
            w = max(p[0] for p in pts) + 1
            top = min(p[1] for p in pts)
            cx = (w - 1) // 2
            low = top - 1 - self.accent_gap
            m = mark(kind, cx, low)
            if base.isupper() and self.cap_accent_rows == 1:
                # one row above capitals (the line above is close)
                m = [(x, low) for x, y in m if y == low] or [(cx, low)]
                if kind in ('acute', 'grave') and len(m) == 1:
                    m = [(cx, low), (cx + (1 if kind == 'acute' else -1), low)]
            g[ch] = pts + m

    def width(self, ch):
        if ch == ' ':
            return self.space
        k = self.g.get(ch)
        if k is None:
            return self.space
        return max(p[0] for p in k) + 1 - min(0, min(p[0] for p in k))

    def text_width(self, s):
        if not s:
            return 0
        w = 0
        for i, ch in enumerate(s):
            w += self.width(ch)
            if i + 1 < len(s) and ch != ' ' and s[i + 1] != ' ':
                w += self.gap
        return w

    def draw(self, pix, s, x, base, color):
        if self.upper_only:
            s = s.upper()
        missing = set()
        for i, ch in enumerate(s):
            if ch != ' ':
                k = self.g.get(ch)
                if k is None:
                    missing.add(ch)
                else:
                    for dx, dy in k:
                        pix[(x + dx, base + dy)] = color
            x += self.width(ch)
            if i + 1 < len(s) and ch != ' ' and s[i + 1] != ' ':
                x += self.gap
        return missing


def load_font(path, **kw):
    lib = Lib().load(path)
    return GlyphFont(lib.best(), **kw)


def wrap(font, text, widths):
    """Greedy word wrap; widths = callable(line_index) -> available width. Returns lines."""
    words = text.split()
    lines = []
    cur = ''
    for w in words:
        cand = (cur + ' ' + w) if cur else w
        if font.text_width(cand) <= widths(len(lines)):
            cur = cand
        else:
            if cur:
                lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines
