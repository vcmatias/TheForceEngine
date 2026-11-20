# Step 3 (system python): packs prep.py + blender_caps.py results into dfmod_weapons.bin (version 2):
# original textured surfaces (with the mirrored side), the Blender caps (vertex colors, submesh -2) and
# the parts modeled in Blender (blender_finish.py) with their baked texture.
# RECOLOR: textures turned to dark gunmetal (the mod's blue Merr-Sonn stands in for the dark trooper
# assault cannon, which is dark gray in Dark Forces).
RECOLOR = {9}
import os, sys, json, struct
import numpy as np
from PIL import Image
def main(wd, out):
    idx = json.load(open(os.path.join(wd, 'index.json')))
    recs = []
    extra = []            # baked textures of the added parts, appended after the mod textures
    recolor = set()
    ntex = len(idx['textures'])
    for wid in sorted(int(k) for k in idx['weapons']):
        d = np.load(os.path.join(wd, 'w%d.npz' % wid))
        P = d['P']; UV = d['UV']; T = d['T']
        subs = {}
        for i in range(len(T)):
            p = P[i]; n = np.cross(p[1] - p[0], p[2] - p[0]); n = n / (np.linalg.norm(n) + 1e-12)
            vs = subs.setdefault(int(T[i]), [])
            for k in range(3): vs.append(list(p[k]) + list(n) + list(UV[i][k]) + [1.0, 1.0, 1.0])
        cf = os.path.join(wd, 'w%d_caps.npz' % wid)
        if os.path.exists(cf):
            C = np.load(cf)['V']
            if len(C): subs[-2] = C.tolist()
        af = os.path.join(wd, 'w%d_add.npz' % wid)
        if os.path.exists(af):
            A = np.load(af)['V']
            if len(A):
                ti = ntex + len(extra); extra.append(os.path.join(wd, 'w%d_add.png' % wid))
                subs[ti] = [list(v) + [1.0, 1.0, 1.0] for v in A]
        if wid in RECOLOR: recolor |= set(int(t) for t in T if t >= 0)
        recs.append((wid, d['muzzle'], subs))
    with open(out, 'wb') as f:
        f.write(b'DFMW'); f.write(struct.pack('<ii', 2, len(recs)))
        for wid, m, subs in recs:
            f.write(struct.pack('<i3fi', wid, *[float(x) for x in m], len(subs)))
            for t, vs in subs.items():
                a = np.asarray(vs, np.float32)
                f.write(struct.pack('<iii', t, len(a), len(a)))
                f.write(a.tobytes()); f.write(np.arange(len(a), dtype=np.uint32).tobytes())
        texs = sorted(idx['textures'].items(), key=lambda kv: int(kv[0]))
        f.write(struct.pack('<i', len(texs) + len(extra)))
        for k, fn in texs:
            im = Image.open(fn).convert('RGBA')
            if int(k) in recolor:
                a = np.asarray(im).astype(np.float32)
                l = a[..., :3].mean(-1, keepdims=True)
                a[..., :3] = np.clip(l * np.array([0.55, 0.58, 0.66]) * 0.9, 0, 255)
                im = Image.fromarray(a.astype(np.uint8), 'RGBA')
            f.write(struct.pack('<ii', im.width, im.height)); f.write(im.tobytes())
        for fn in extra:
            im = Image.open(fn).convert('RGB'); im.putalpha(70)
            f.write(struct.pack('<ii', im.width, im.height)); f.write(im.tobytes())
    print('weapons', len(recs), 'bytes', os.path.getsize(out))
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
