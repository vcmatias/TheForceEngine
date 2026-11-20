"""Renders the Portuguese briefings: same pictures and bars, Portuguese text in the game's own
briefing fonts, paragraphs reflowed into the original line slots (extra lines push the rest down)."""
import sys, re
sys.path.insert(0, '/home/claude/tfevr/tools/translate')
from lfdlib import read_lfd, decode_delt, encode_delt
from brief_seg import segment, bar_lines, BAR, W
from brief_ocr import Lib, baseline, line_blobs
from brief_slots import find_slots
from glyphfont import GlyphFont, wrap

TR = '/home/claude/tfevr/tools/translate/brief/'
PITCH = 12


def load_pt(name):
    bars, paras = {}, {}
    for line in open(TR + 'pt/' + name + '.txt', encoding='utf-8'):
        line = line.rstrip('\n')
        m = re.match(r'bar (\d+): (.*)$', line)
        if m:
            bars[int(m.group(1))] = m.group(2); continue
        m = re.match(r'p(\d+): (.*)$', line)
        if m:
            paras[int(m.group(1))] = m.group(2)
    return bars, paras


def render(name, delt, body_lib, body_font, bar_font, log):
    h, rows, blocks, slots, paras, picmask = find_slots(delt, body_lib)
    x0_, y0_, x1_, y1_, pix = decode_delt(delt)
    pix = dict(pix)
    bars_pt, paras_pt = load_pt(name)
    draws = []        # (orig_base, extra_lines_after_anchor, anchor_row, x, text, color, font)
    inserts = {}      # row -> rows inserted before it
    deletes = set()   # rows removed

    # ---- bars
    for b in blocks:
        if b[0] != 'bar':
            continue
        for ya, yb, col in bar_lines(rows, b[1], b[2]):
            if ya not in bars_pt:
                continue
            xs = [x for y in range(ya, yb + 1) for x, v in rows[y].items() if v != BAR]
            ox0, ox1 = min(xs), max(xs)
            base = baseline(rows, ya, yb)
            for y in range(ya, yb + 1):
                for x, v in list(rows[y].items()):
                    if v != BAR:
                        pix[(x, y)] = BAR
            t = bars_pt[ya]
            tw = bar_font.text_width(t)
            if tw > W - 4:
                log.append(f'{name}: bar {ya} too wide ({tw}): {t}')
            x = ox0 if ox0 <= 8 else max(1, (W - tw) // 2)
            draws.append((base, 0, None, x, t, col, bar_font))

    # ---- paragraphs
    for pi, para in enumerate(paras):
        if pi not in paras_pt:
            continue
        text = paras_pt[pi]
        # erase the original text
        for s in para:
            if s['inpic']:
                for p in s['text']:
                    pix.pop(p, None)
            else:
                for y in range(s['ya'], s['yb'] + 1):
                    for x in range(0, W):
                        pix.pop((x, y), None)
        objective = para[0]['color'] == 5
        indent = None
        if objective:
            indent = 8
            # the "- " of the original stays the same width: text after it lines up with indent
            text = re.sub(r'^-\s*', '', text)
        first_x = para[0]['x0'] if not objective else 0

        def room(i):
            if i < len(para):
                s = para[i]
                xs = s['x0'] if (s['inpic'] or not objective or i == 0) else s['x0']
                if objective:
                    xs = indent
                elif i > 0 or s['inpic']:
                    xs = s['x0'] if s['inpic'] else 0
                else:
                    xs = first_x
                return s['xe'] - xs
            return W - 1 - (indent if objective else 0)
        lines = wrap(body_font, text, room)
        # place lines
        for i, ln in enumerate(lines):
            if i < len(para):
                s = para[i]
                if objective:
                    x = indent
                else:
                    x = s['x0'] if s['inpic'] else (first_x if i == 0 else 0)
                draws.append((s['base'], 0, None, x, ln, s['color'], body_font))
            else:
                last = para[-1]
                draws.append((last['base'], i - len(para) + 1, 'anchor', indent if objective else 0, ln, last['color'], body_font, last))
        if objective:
            # the dash
            draws.append((para[0]['base'], 0, None, 0, '-', para[0]['color'], body_font))
        extra = len(lines) - len(para)
        if extra > 0:
            last = para[-1]
            # insertion row: first fully empty row after the last slot (never through a picture)
            y = last['yb'] + 1
            while y < h and rows[y]:
                y += 1
            inserts[y] = inserts.get(y, 0) + extra * PITCH
            # remember the anchor for these draws
            for d in draws:
                if len(d) == 8 and d[7] is last:
                    pass
            para[-1]['insert_row'] = y
            para[-1]['extra'] = extra
        elif extra < 0:
            # unused slots (plain text lines): remove them with the gap above
            for s in para[len(lines):]:
                if s['inpic']:
                    continue
                prev_yb = s['ya'] - 1
                while prev_yb > 0 and not rows[prev_yb] and prev_yb > s['ya'] - 6:
                    prev_yb -= 1
                for y in range(prev_yb + 1, s['yb'] + 1):
                    deletes.add(y)

    # ---- final row mapping
    def newy(y):
        d = 0
        for r, n in inserts.items():
            if y >= r:
                d += n
        d -= sum(1 for r in deletes if r < y)
        return y + d

    out = {}
    for (x, y), v in pix.items():
        if y in deletes:
            continue
        out[(x, newy(y))] = v
    missing = set()
    for d in draws:
        if d[2] == 'anchor':
            last = d[7]
            ins = last['insert_row']
            # lines below the last slot: in the inserted band, one pitch each
            if ins == last['yb'] + 1:
                base = newy(last['base']) + d[1] * PITCH
            else:
                base = newy(ins) - last['extra'] * PITCH + (d[1] - 1) * PITCH + 8
            missing |= d[6].draw(out, d[4], d[3], base, d[5])
        else:
            missing |= d[6].draw(out, d[4], d[3], newy(d[0]), d[5])
    if missing:
        log.append(f'{name}: missing glyphs {sorted(missing)}')
    hh = max(y for x, y in out) + 4
    for (x, y) in out:
        if x >= W:
            log.append(f'{name}: pixel past the right edge at y {y}')
            break
    out = {k: v for k, v in out.items() if 0 <= k[0] < W}
    return encode_delt(0, 0, W - 1, hh - 1, out), out, hh


if __name__ == '__main__':
    from lfdlib import read_pltt, to_image
    src = '/home/claude/lfd/DFBRIEF.LFD'
    res = read_lfd(src)
    r = {n + '.' + t: b for t, n, b in res}
    body_lib = Lib().load(TR + 'body_glyphs.json')
    body_font = GlyphFont(body_lib.best(), space=4, gap=1)
    bar_font = GlyphFont(Lib().load(TR + 'bar_glyphs.json').best(), space=5, gap=1, upper_only=True, cap_accent_rows=2)
    if '¤' in body_lib.g.values():
        pass
    names = sys.argv[1:] or ['secbase', 'talay', 'sewers', 'testbase', 'gromas', 'dtention', 'ramshed', 'robotics', 'narshada', 'jabship', 'impcity', 'fuelstat', 'executor', 'arc']
    pal = read_pltt({n + '.' + t: b for t, n, b in read_lfd('/home/claude/lfd/MENU.LFD')}['menu.PLTT'])
    log = []
    for n in names:
        data, out, hh = render(n, r[n + '.DELT'], body_lib, body_font, bar_font, log)
        open(f'/home/claude/lfd/out/{n}.DELT', 'wb').write(data)
        to_image([data], pal, bg=(40, 0, 40)).save(f'/home/claude/lfd/out/{n}.png')
    print('\n'.join(log) or 'ok')
