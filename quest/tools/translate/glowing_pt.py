# Builds Translation/pt/GLOWING.FNT: the HUD message font of Dark Forces (GLOWING.FNT, chars 33..126,
# 7 rows) with two extra rows on top and the Latin-1 accented letters used in Portuguese (accent marks
# drawn over the capitals, cedilla in the descender row).
#   python3 glowing_pt.py <GLOWING.FNT from DARK.GOB> <out>
import sys, struct
src = open(sys.argv[1], 'rb').read()
vs, hs, fw, fh, mn, mx = src[4], src[5], src[6], src[7], src[8], src[9]
p = 32; gl = {}
for c in range(mn, mx + 1):
    w = src[p]; p += 1
    cols = [list(src[p + i * vs:p + (i + 1) * vs]) for i in range(w)]   # column major, row 0 = bottom
    p += w * vs; gl[c] = cols
EXTRA = 2; NV = vs + EXTRA
def padded(cols): return [col + [0] * EXTRA for col in cols]
def color(cols):
    cnt = {}
    for col in cols:
        for v in col:
            if v: cnt[v] = cnt.get(v, 0) + 1
    return max(cnt, key=cnt.get) if cnt else 21
def accent(base, kind):
    cols = padded(gl[ord(base)]); w = len(cols); cx = w // 2; col = color(gl[ord(base)])
    top, mid = NV - 1, NV - 2
    marks = {
        'acute': [(top, cx), (top, cx + 1), (mid, cx - 1), (mid, cx)],
        'grave': [(top, cx - 2), (top, cx - 1), (mid, cx - 1), (mid, cx)],
        'circ':  [(top, cx - 1), (top, cx), (mid, cx - 2), (mid, cx + 1)],
        'tilde': [(top, cx - 2), (top, cx - 1), (top, cx + 1), (mid, cx - 3), (mid, cx), (mid, cx + 1)],
        'uml':   [(top, cx - 2), (top, cx + 1)],
        'ced':   [(0, cx - 1), (0, cx)],
    }[kind]
    for r, x in marks:
        x = min(w - 1, max(0, x)); cols[x][r] = col
    return cols
LAT = {0xC0: ('A', 'grave'), 0xC1: ('A', 'acute'), 0xC2: ('A', 'circ'), 0xC3: ('A', 'tilde'), 0xC7: ('C', 'ced'),
       0xC8: ('E', 'grave'), 0xC9: ('E', 'acute'), 0xCA: ('E', 'circ'), 0xCD: ('I', 'acute'), 0xD3: ('O', 'acute'),
       0xD4: ('O', 'circ'), 0xD5: ('O', 'tilde'), 0xDA: ('U', 'acute'), 0xDC: ('U', 'uml')}
out = bytearray(b'FNT') + bytes([src[3], NV, hs, fw, fh, mn, 255]) + bytes(22)
for c in range(mn, 256):
    if c in gl: cols = padded(gl[c])
    elif c in LAT: cols = accent(*LAT[c])
    elif c - 0x20 in LAT: cols = accent(*LAT[c - 0x20])   # lowercase: the font is all capitals
    else: cols = [[0] * NV]
    out.append(len(cols))
    for col in cols: out += bytes(col)
open(sys.argv[2], 'wb').write(out)
print('GLOWING.FNT pt', len(out), 'bytes, chars', mn, '..255, rows', NV)
