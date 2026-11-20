"""Dark Forces LFD / DELT / ANIM / PLTT / FONT helpers (read and write)."""
import struct


def read_lfd(path):
    d = open(path, 'rb').read()
    t, n, sz = struct.unpack('<4s8sI', d[:16])
    cnt = sz // 16
    off = 16 + sz
    out = []
    for i in range(cnt):
        rt, rn, rs = struct.unpack('<4s8sI', d[off:off + 16])
        out.append((rt.decode(), rn.rstrip(b'\0').decode(), d[off + 16:off + 16 + rs]))
        off += 16 + rs
    return out


def write_lfd(path, res):
    rmap = b''.join(struct.pack('<4s8sI', t.encode(), n.encode()[:8], len(b)) for t, n, b in res)
    body = b''.join(struct.pack('<4s8sI', t.encode(), n.encode()[:8], len(b)) + b for t, n, b in res)
    with open(path, 'wb') as f:
        f.write(struct.pack('<4s8sI', b'RMAP', b'resource', len(rmap)) + rmap + body)


def read_pltt(b):
    first, last = b[0], b[1]
    pal = [(0, 0, 0)] * 256
    for i in range(last - first + 1):
        pal[first + i] = (b[2 + i * 3], b[3 + i * 3], b[4 + i * 3])
    return pal


def decode_delt(b):
    """Returns (x0, y0, x1, y1, pix) with pix = dict[(x, y)] -> index (only drawn pixels)."""
    x0, y0, x1, y1 = struct.unpack('<4h', b[:8])
    p = 8
    pix = {}
    while p + 6 <= len(b):
        st, xs, ys = struct.unpack('<3h', b[p:p + 6]); p += 6
        if st == 0:
            break
        rle = st & 1
        n = (st >> 1) & 0x3fff
        x = xs
        if rle:
            while n > 0:
                c = b[p]; p += 1
                if not (c & 1):
                    c >>= 1
                    for k in range(c):
                        pix[(x, ys)] = b[p]; p += 1; x += 1
                else:
                    c >>= 1
                    v = b[p]; p += 1
                    for k in range(c):
                        pix[(x, ys)] = v; x += 1
                n -= c
        else:
            for k in range(n):
                pix[(x, ys)] = b[p]; p += 1; x += 1
    return x0, y0, x1, y1, pix


def encode_delt(x0, y0, x1, y1, pix):
    """Plain (non RLE) runs per row of consecutive drawn pixels."""
    out = bytearray(struct.pack('<4h', x0, y0, x1, y1))
    rows = {}
    for (x, y), v in pix.items():
        rows.setdefault(y, {})[x] = v
    for y in sorted(rows):
        xs = sorted(rows[y])
        i = 0
        while i < len(xs):
            j = i
            while j + 1 < len(xs) and xs[j + 1] == xs[j] + 1 and j - i < 0x3ff0:
                j += 1
            run = [rows[y][x] for x in xs[i:j + 1]]
            out += struct.pack('<3h', len(run) << 1, xs[i], y) + bytes(run)
            i = j + 1
    out += struct.pack('<3h', 0, 0, 0)
    return bytes(out)


def read_anim(b):
    cnt = struct.unpack('<h', b[:2])[0]
    p = 2
    frames = []
    for i in range(cnt):
        sz = struct.unpack('<i', b[p:p + 4])[0]; p += 4
        if sz <= 0:
            frames.append(None); continue
        frames.append(b[p:p + sz]); p += sz
    return frames


def write_anim(frames):
    out = bytearray(struct.pack('<h', len(frames)))
    for f in frames:
        if f is None:
            out += struct.pack('<i', 0)
        else:
            out += struct.pack('<i', len(f)) + f
    return bytes(out)


def to_image(delts, pal, w=None, h=None, bg=(255, 0, 255)):
    from PIL import Image
    xs, ys = [], []
    dec = [decode_delt(d) for d in delts if d]
    for x0, y0, x1, y1, pix in dec:
        xs += [x0, x1]; ys += [y0, y1]
    W = w or max(xs) + 1
    H = h or max(ys) + 1
    im = Image.new('RGB', (W, H), bg)
    px = im.load()
    for x0, y0, x1, y1, pix in dec:
        for (x, y), v in pix.items():
            if 0 <= x < W and 0 <= y < H:
                px[x, y] = pal[v]
    return im
