# Packs the combined DFmod weapons (combine.py: first-person model + the mod's third-person model filling
# what it never had) into dfmod_combined.bin (DFMW v2) for the weapon style 4 (DFMOD).
#   python3 pack_comb.py <mod models/weapons2 folder> <comb dir> <blender weapons dir> <out.bin> [texsize]
# Grip: the weapon frame origin sits 2.4 cm above and 1.8 cm ahead of the handle center (kit.py GRIP_C),
# so the standard hand pose closes on the mod's own handle. Fusion cutter: the handle modeled in 0.19
# (blender_finish.py, w5_add). Thermal detonator and mine: the Blender models.
import os, sys, struct
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import convert
GRIP_C = np.array([0.0, -0.024, 0.018])
# handle boxes in hand space (x range, z range): origin from the handle center; others keep convert.SPEC
HANDLE = {1: ([4.5, 9], [-8.5, -5.6]), 2: ([5, 12], [-11.5, -7.5]), 6: ([1, 7], [-13, -9.5]), 8: ([4.5, 10], [-11.6, -8.6])}
RECOLOR = {9}
BLENDER = {3: 'thermal', 7: 'mine'}
MUZZLE = {6: (18.6, -2.95, -5.4)}      # mortar: the opening of the third-person model's barrel
def to_w(P, g, s):
    return np.stack([-(P[..., 1] - g[1]) * s, (P[..., 2] - g[2]) * s, -(P[..., 0] - g[0]) * s], -1)
def main(root, cdir, bdir, out, ts=1024):
    texs, tex_index = [], {}
    def tex_id(path, recolor=False, alpha=None):
        key = (path, recolor)
        if key in tex_index: return tex_index[key]
        im = Image.open(path).convert('RGB')
        if max(im.size) > ts:
            k = ts / max(im.size); im = im.resize((int(im.width * k), int(im.height * k)), Image.LANCZOS)
        spec = None
        b = os.path.splitext(path)[0]
        for c in (b + '_spec.jpg',):
            if os.path.exists(c): spec = c
        a = alpha if alpha is not None else (Image.open(spec).convert('L').resize(im.size) if spec else Image.new('L', im.size, 60))
        if recolor:
            x = np.asarray(im).astype(np.float32); l = x.mean(-1, keepdims=True)
            im = Image.fromarray(np.clip(l * np.array([0.55, 0.58, 0.66]) * 0.9, 0, 255).astype(np.uint8))
        im = im.convert('RGBA'); im.putalpha(a if isinstance(a, Image.Image) else Image.new('L', im.size, a))
        tex_index[key] = len(texs); texs.append(im)
        return tex_index[key]
    recs = []
    for wid in (1, 2, 3, 4, 5, 6, 7, 8, 9):
        if wid in BLENDER:
            d = np.load(os.path.join(bdir, BLENDER[wid] + '.npz')); V = np.asarray(d['V'], np.float32)
            ti = tex_id(os.path.join(bdir, BLENDER[wid] + '.png'))
            texs[ti] = Image.open(os.path.join(bdir, BLENDER[wid] + '.png')).convert('RGBA').resize((min(ts, 1024),) * 2, Image.LANCZOS)
            recs.append((wid, d['muzzle'], {ti: V})); continue
        sp = convert.SPEC[wid]; s = convert.UNIT * sp['k']
        d = np.load(os.path.join(cdir, 'c%d.npz' % wid))
        P, UV, TX = d['P'], d['UV'], list(d['TX'])
        if wid in HANDLE:
            bx, bz = HANDLE[wid]; Q = P.reshape(-1, 3)
            m = (Q[:, 0] > bx[0]) & (Q[:, 0] < bx[1]) & (Q[:, 2] > bz[0]) & (Q[:, 2] < bz[1])
            c = (Q[m].min(0) + Q[m].max(0)) / 2
            g = c + np.array([GRIP_C[2], 0, -GRIP_C[1]]) / s      # forward (+x) and up (+z) in hand space
        else:
            g = np.array(sp['grip'], float)
        W = to_w(P, g, s)
        n = np.cross(W[:, 1] - W[:, 0], W[:, 2] - W[:, 0]); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        subs = {}
        for i in range(len(W)):
            if not TX[i]: continue
            ti = tex_id(TX[i], wid in RECOLOR)
            vs = subs.setdefault(ti, [])
            for k in range(3): vs.append(list(W[i, k]) + list(n[i]) + list(UV[i, k]) + [1.0, 1.0, 1.0])
        if wid == 5:
            A = np.load('/home/claude/dfmod/work/w5_add.npz')['V']
            ti = tex_id('/home/claude/dfmod/work/w5_add.png', alpha=70)
            subs[ti] = [list(v) + [1.0, 1.0, 1.0] for v in A]
        mz = to_w(np.array(MUZZLE.get(wid, sp['muzzle']), float), g, s)
        recs.append((wid, mz, {k: np.asarray(v, np.float32) for k, v in subs.items()}))
        Wf = W.reshape(-1, 3)
        print(wid, 'tris', sum(len(v) for v in subs.values()) // 3, 'grip', np.round(g, 2), 'size cm', np.round((Wf.max(0) - Wf.min(0)) * 100, 1), 'muzzle', np.round(mz, 3))
    with open(out, 'wb') as f:
        f.write(b'DFMW'); f.write(struct.pack('<ii', 2, len(recs)))
        for wid, m, subs in recs:
            f.write(struct.pack('<i3fi', wid, *[float(x) for x in m], len(subs)))
            for t, V in subs.items():
                f.write(struct.pack('<iii', t, len(V), len(V)))
                f.write(V.tobytes()); f.write(np.arange(len(V), dtype=np.uint32).tobytes())
        f.write(struct.pack('<i', len(texs)))
        for im in texs:
            f.write(struct.pack('<ii', im.width, im.height)); f.write(im.tobytes())
    print('textures', len(texs), 'bytes', os.path.getsize(out))
if __name__ == '__main__':
    a = sys.argv
    main(a[1], a[2], a[3], a[4], int(a[5]) if len(a) > 5 else 1024)
