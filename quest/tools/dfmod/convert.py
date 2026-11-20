# Converts the DFmod (Dark Forces Mod Remastered for Jedi Academy) first-person weapons into
# dfmod_weapons.bin for the Quest port. Run on the player's own copy of the mod:
#   python3 convert.py <folder with models/weapons2 contents> dfmod_weapons.bin
# The mod's assets are not part of this source tree.
import os, sys, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from assemble import assemble
import hull
from PIL import Image
UNIT = 0.026   # meters per game unit
# DF weapon id -> grip point and muzzle in hand space (x forward, y left, z up), extra scale.
# mirror: copy the modeled side to the missing one (about the barrel axis); mirror_r: only parts within
# this distance (m) of the barrel axis (asymmetric extras like the E-11 scope or the Bryar lever stay single).
SPEC = {
    1: dict(grip=(7.0, -3.0, -6.0), muzzle=(15.6, -2.99, -3.46), k=1.0, mirror=True, mirror_r=0.034),
    2: dict(grip=(9.5, -3.0, -8.0), muzzle=(26.2, -4.3, -6.3), k=0.85, mirror=True, mirror_r=0.024),
    4: dict(grip=(3.6, -2.0, -5.0), muzzle=(13.0, -1.7, -2.5), k=1.2),
    5: dict(grip=(6.0, 1.0, -5.5), muzzle=(12.7, 1.0, -1.5), k=0.65, mirror=True),
    6: dict(grip=(8.0, -2.7, -8.0), muzzle=(15.2, -2.7, -2.7), k=0.8, mirror=True),
    8: dict(grip=(11.0, -3.5, -8.5), muzzle=(24.8, -3.5, -6.7), k=1.0, mirror=True),
    9: dict(grip=(4.0, -5.0, -4.5), muzzle=(14.6, -4.45, -2.1), k=1.0, mirror=True),
}
def find_tex(root, shader):
    rel = shader.split('weapons2/')[-1] if 'weapons2/' in shader else shader
    base = os.path.splitext(rel)[0]
    for e in ('.jpg', '.tga', '.png'):
        p = os.path.join(root, base + e)
        if os.path.exists(p): return p
    return None
def load_tex(root, shader, cache):
    p = find_tex(root, shader)
    if p is None: return -1
    if p in cache: return cache[p][0]
    im = Image.open(p).convert('RGB')
    # Specular map (luminance) into alpha when the mod has one.
    base = os.path.splitext(p)[0]
    spec = None
    for c in (base + '_spec.jpg', os.path.join(os.path.dirname(p), os.path.basename(base) + '_spec.jpg')):
        if os.path.exists(c): spec = c; break
    w, h = im.size
    s = min(1.0, 512.0 / max(w, h))
    if s < 1.0: im = im.resize((max(1, int(w * s)), max(1, int(h * s))), Image.LANCZOS)
    a = Image.open(spec).convert('L').resize(im.size) if spec else Image.new('L', im.size, 60)
    im = im.convert('RGBA'); im.putalpha(a)
    cache[p] = (len(cache), im, np.asarray(im, dtype=np.float32) / 255.0)
    return cache[p][0]
def to_weapon(v, g, s):
    # hand space -> weapon frame (x right, y up, forward -z), meters, relative to the grip point
    return (-(v[1] - g[1]) * s, (v[2] - g[2]) * s, -(v[0] - g[0]) * s)
def sample_surface(tri_list, cache_by_idx, n_per_m2=2.0e6):
    pts = []; cols = []
    for (p, uv, t) in tri_list:
        p = np.array(p); uv = np.array(uv)
        area = 0.5 * np.linalg.norm(np.cross(p[1] - p[0], p[2] - p[0]))
        k = max(3, int(area * n_per_m2))
        r = np.random.rand(k, 2); m = r.sum(1) > 1; r[m] = 1 - r[m]
        b = np.stack([1 - r[:, 0] - r[:, 1], r[:, 0], r[:, 1]], 1)
        pts.append(b @ p)
        if t >= 0:
            img = cache_by_idx[t]; h, w = img.shape[:2]
            u = (b @ uv[:, 0]) % 1.0; v = (b @ uv[:, 1]) % 1.0
            c = img[(v * h).astype(int) % h, (u * w).astype(int) % w, :3]
        else:
            c = np.tile([0.3, 0.3, 0.33], (k, 1))
        cols.append(c)
    return np.concatenate(pts), np.concatenate(cols)
def main(root, out):
    np.random.seed(1)
    cache = {}
    recs = []
    for wid, sp in SPEC.items():
        surfs, _ = assemble(root, wid)
        s = UNIT * sp['k']; g = sp['grip']
        subs = {}
        tri_list = []
        for su in surfs:
            t = load_tex(root, su['shader'], cache)
            vs, ix = subs.setdefault(t, ([], []))
            base = len(vs)
            for v, n, st in zip(su['verts'], su['norms'], su['st']):
                p = to_weapon(v, g, s)
                nn = (-n[1], n[2], -n[0])
                vs.append(p + nn + (st[0], st[1], 1.0, 1.0, 1.0))
            for tri in su['tris']:
                ix.extend([base + tri[0], base + tri[1], base + tri[2]])
                tri_list.append(([to_weapon(su['verts'][i], g, s) for i in tri], [su['st'][i] for i in tri], t))
        by_idx = {c[0]: c[2] for c in cache.values()}
        if sp.get('mirror'):
            # Left/right symmetric weapons: the side the first-person view never showed is the mirror of
            # the side it did. Mirror about the barrel axis, only where the model has no surface yet.
            ax = to_weapon(sp['muzzle'], g, s)[0]
            sp_pos, _ = sample_surface(tri_list, by_idx)
            from scipy.spatial import cKDTree
            tree = cKDTree(sp_pos)
            added = 0
            for (pts, uvs, t) in list(tri_list):
                mp = [(2 * ax - p[0], p[1], p[2]) for p in pts]
                c = np.mean(mp, 0)
                if abs(c[0] - ax) < 0.004: continue
                if tree.query(c)[0] < 0.004: continue
                vs, ix = subs.setdefault(t, ([], []))
                base = len(vs)
                e1 = np.subtract(mp[1], mp[0]); e2 = np.subtract(mp[2], mp[0]); n = np.cross(e2, e1)
                n = n / (np.linalg.norm(n) + 1e-12)
                for p, uv in zip(mp, uvs): vs.append(tuple(p) + tuple(n) + (uv[0], uv[1], 1.0, 1.0, 1.0))
                ix.extend([base, base + 2, base + 1])
                tri_list.append((mp, uvs, t)); added += 1
            print(wid, 'mirrored triangles', added)
        # Closed hull that completes the one-sided first-person model.
        hv, hn, hf = hull.build([(tl[0],) for tl in tri_list])
        sp_pos, sp_col = sample_surface(tri_list, by_idx)
        hc = hull.colors_from(hv, sp_pos, sp_col)
        hvs = [tuple(hv[i]) + tuple(hn[i]) + (0.0, 0.0) + tuple(hc[i]) for i in range(len(hv))]
        subs[-2] = (hvs, [int(x) for x in hf.reshape(-1)])
        m = to_weapon(sp['muzzle'], g, s)
        recs.append((wid, m, subs))
        print(wid, 'tris', sum(len(i) // 3 for _, i in subs.values()), 'hull tris', len(hf), 'muzzle', [round(a, 3) for a in m])
    with open(out, 'wb') as f:
        f.write(b'DFMW'); f.write(struct.pack('<ii', 2, len(recs)))
        for wid, m, subs in recs:
            f.write(struct.pack('<i3fi', wid, *m, len(subs)))
            for t, (vs, ix) in subs.items():
                f.write(struct.pack('<iii', t, len(vs), len(ix)))
                f.write(np.asarray(vs, dtype=np.float32).tobytes())
                f.write(np.asarray(ix, dtype=np.uint32).tobytes())
        texs = sorted(cache.values(), key=lambda c: c[0])
        f.write(struct.pack('<i', len(texs)))
        for i, im, _ in texs:
            f.write(struct.pack('<ii', im.width, im.height)); f.write(im.tobytes())
    print('textures', len(cache), 'bytes', os.path.getsize(out))
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
