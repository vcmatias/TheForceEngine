"""Portuguese PDA mission objectives: rebuilds every goals ANIM (frame 0 = full list, frame k =
item k completed) with the game's own shaded fonts."""
import sys
sys.path.insert(0, '/home/claude/tfevr/tools/translate')
from lfdlib import read_lfd, read_anim, write_anim, decode_delt, encode_delt
from goals_lib import ShadedFont, line_groups, MAIN

TR = '/home/claude/tfevr/tools/translate/brief/'
X0 = 69          # text start
XMAX = 296       # right limit of the PDA window
PITCH = 11       # lines of one item (room for accents)
ITEM_GAP = 14    # first line of the next item
Y_ITEMS = 78
SCHEME = {'g': (11, 13, 12), 'y': (19, 90, 20), 'h': (11, 13, 12)}   # main, shadow, highlight
ACC = {'Á': ('A', 'acute'), 'À': ('A', 'grave'), 'Â': ('A', 'circ'), 'Ã': ('A', 'tilde'), 'É': ('E', 'acute'),
       'Ê': ('E', 'circ'), 'Í': ('I', 'acute'), 'Ó': ('O', 'acute'), 'Ô': ('O', 'circ'), 'Õ': ('O', 'tilde'),
       'Ú': ('U', 'acute')}


class Shaded:
    def __init__(self, font, scheme, space):
        self.g = font.g
        self.main, self.shadow, self.hi = SCHEME[scheme]
        self.space = space
        self._derive()

    def _derive(self):
        g = self.g
        b, d = self.main, self.shadow
        for ch, (base, kind) in ACC.items():
            if ch in g or base not in g:
                continue
            pts = dict(g[base])
            mainx = [x for (x, y), v in pts.items() if y <= 6 and v != d] or [x for x, y in pts]
            cx = (min(mainx) + max(mainx)) // 2
            r1, r2 = -3, -2
            if kind == 'acute':
                add = {(cx + 1, r1): b, (cx, r2): b, (cx + 1, r2): d}
            elif kind == 'grave':
                add = {(cx - 1, r1): b, (cx, r2): b, (cx + 1, r2): d}
            elif kind == 'circ':
                add = {(cx, r1): b, (cx - 1, r2): b, (cx + 1, r2): b, (cx + 2, r2): d}
            else:
                add = {(cx - 1, r2): b, (cx, r1): b, (cx + 1, r2): b, (cx + 2, r1): b, (cx + 3, r1): d}
            pts.update(add)
            g[ch] = pts
        def art(rows):
            m = {'b': self.main, 'd': self.shadow, 'c': self.hi, 'a': self.hi}
            return {(x, y): m[ch] for y, r in enumerate(rows) for x, ch in enumerate(r) if ch in m}
        if 'Z' not in g:
            g['Z'] = art(['cbbbbbd', '.dddcbd', '...cbd.', '..cbd..', '.cbd...', 'cbd....', 'bbbbbbd', 'dddddd.'])
        if ':' not in g:
            g[':'] = art(['..', '..', 'bd', 'd.', '..', 'bd', 'd.', '..'])
        if '?' not in g:
            g['?'] = art(['.cbbbd.', 'cbd.cbd', '...cbd.', '..cbd..', '..bd...', '.......', '..bd...', '..d....'])
        if 'U' not in g and self.space >= 7:
            g['U'] = art(['cbd...cbd', 'bad...bad', 'bad...bad', 'bad...bad', 'bad...bad', 'bad..cbad', '.dbbbbad.', '..ddddd..'])
        if 'U' not in g:
            g['U'] = art(['bd...bd', 'bd...bd', 'bd...bd', 'bd...bd', 'bd...bd', 'bd..cbd', '.dbbbd.', '..ddd..'])
        if ',' not in g:
            g[','] = art(['..', '..', '..', '..', '..', '.bd', 'bd.', 'd..'])
        if 'Ç' not in g and 'C' in g:
            pts = dict(g['C'])
            hgt = max(y for x, y in pts)
            mainx = [x for (x, y), v in pts.items() if v != d]
            cx = (min(mainx) + max(mainx)) // 2
            pts.update({(cx, hgt + 1): b, (cx, hgt + 2): b, (cx - 1, hgt + 2): b, (cx + 1, hgt + 2): d})
            g['Ç'] = pts

    def adv(self, ch):
        if ch == ' ':
            return self.space
        k = self.g.get(ch)
        if not k:
            return self.space
        return max(x for x, y in k) + 2

    def width(self, s):
        return sum(self.adv(c) for c in s) - (1 if s else 0)

    def draw(self, pix, s, x, top):
        miss = set()
        for ch in s:
            if ch != ' ':
                k = self.g.get(ch)
                if not k:
                    miss.add(ch)
                else:
                    for (dx, dy), v in k.items():
                        pix[(x + dx, top + dy)] = v
            x += self.adv(ch)
        return miss


def wrap(font, text, width):
    out, cur = [], ''
    for w in text.split():
        c = (cur + ' ' + w) if cur else w
        if font.width(c) <= width:
            cur = c
        else:
            out.append(cur); cur = w
    if cur:
        out.append(cur)
    return out


def bullet(pix, ya):
    return {(x, y - ya): v for (x, y), v in pix.items() if x < 66 and ya - 1 <= y <= ya + 8}


def render(name, anim, fonts, pt, log):
    frames = read_anim(anim)
    f0 = decode_delt(frames[0])[4]
    groups = line_groups(f0, 62, 300)
    ring = bullet(f0, groups[1][0])
    filled = bullet(decode_delt(frames[1])[4], decode_delt(frames[1])[1]) if len(frames) > 1 else ring
    out0 = {}
    # header bar: same bar, Portuguese title
    for (x, y), v in f0.items():
        if y <= 59:
            out0[(x, y)] = v if not (47 <= y <= 54 and v in (10, 11, 12, 13) and x > 60) else 166
    hx = min(x for (x, y), v in f0.items() if 47 <= y <= 54 and v in (10, 11, 12, 13) and x > 60)
    miss = fonts['h'].draw(out0, 'OBJETIVOS DA MISSÃO', hx, 47)
    # level name
    nya, nyb = groups[0]
    if pt[0] == '*':
        for (x, y), v in f0.items():
            if nya - 1 <= y <= nyb + 1:
                out0[(x, y)] = v
    else:
        miss |= fonts['h'].draw(out0, pt[0], X0, nya)
        if X0 + fonts['h'].width(pt[0]) > XMAX:
            log.append(f'{name}: name too wide')
    # items
    y = Y_ITEMS
    new_frames = []
    for item in pt[1:]:
        lines = wrap(fonts['g'], item, XMAX - X0)
        fk = {}
        for (dx, dy), v in ring.items():
            out0[(dx, y + dy)] = v
        for (dx, dy), v in filled.items():
            fk[(dx, y + dy)] = v
        for i, ln in enumerate(lines):
            miss |= fonts['g'].draw(out0, ln, X0, y + i * PITCH)
            miss |= fonts['y'].draw(fk, ln, X0, y + i * PITCH)
        new_frames.append(fk)
        y += (len(lines) - 1) * PITCH + ITEM_GAP
    if y > 175:
        log.append(f'{name}: objectives run to y {y}')
    if miss:
        log.append(f'{name}: missing {sorted(miss)}')

    def enc(p):
        xs = [x for x, y in p]; ys = [y for x, y in p]
        return encode_delt(min(xs), min(ys), max(xs), max(ys), p)
    # the original has one frame per goal; keep the count (extra originals stay if any)
    res = [enc(out0)] + [enc(f) for f in new_frames]
    if len(res) < len(frames):
        res += frames[len(res):]
    return write_anim(res[:len(frames)]), out0, new_frames


def load_fonts():
    g = ShadedFont().load(TR + 'goals_g.json')
    yv = ShadedFont().load(TR + 'goals_y.json')
    h = ShadedFont().load(TR + 'goals_h.json')
    n = ShadedFont().load(TR + 'goals_n.json')
    for c, v in n.g.items():
        h.g.setdefault(c, v)
    return {'g': Shaded(g, 'g', 6), 'y': Shaded(yv, 'y', 6), 'h': Shaded(h, 'h', 7)}


if __name__ == '__main__':
    from lfdlib import read_pltt, to_image
    from PIL import Image
    r = {n + '.' + t: b for t, n, b in read_lfd('/home/claude/lfd/DFBRIEF.LFD')}
    pal = read_pltt({n + '.' + t: b for t, n, b in read_lfd('/home/claude/lfd/MENU.LFD')}['menu.PLTT'])
    fonts = load_fonts()
    log = []
    tiles = []
    for line in open(TR + 'goals_pt.txt', encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        p = line.rstrip('\n').split('|')
        data, o0, fk = render(p[0], r[p[0] + '.ANIM'], fonts, p[1:], log)
        open(f'/home/claude/lfd/out/{p[0]}.ANIM', 'wb').write(data)
        fr = read_anim(data)
        tiles.append([to_image([f], pal, 320, 200, bg=(30, 30, 30)).crop((50, 38, 300, 180)) for f in fr if f])
    W = 250 * max(len(t) for t in tiles)
    big = Image.new('RGB', (W, 142 * len(tiles)), (255, 255, 255))
    for i, row in enumerate(tiles):
        for j, im in enumerate(row):
            big.paste(im, (j * 250, i * 142))
    big.save('/home/claude/lfd/out/goals.png')
    print('\n'.join(log) or 'ok')
