# Step 1 of the Blender pipeline (system python): per weapon, the source triangles (with the mirrored side
# for symmetric weapons) and the closed inset hull, saved as .npz for blender_complete.py.
import os, sys, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from assemble import assemble
import convert, hull
def main(root, outdir):
    os.makedirs(outdir, exist_ok=True)
    np.random.seed(1)
    cache = {}
    info = {}
    for wid, sp in convert.SPEC.items():
        surfs, _ = assemble(root, wid)
        s = convert.UNIT * sp['k']; g = sp['grip']
        tri_list = []
        for su in surfs:
            t = convert.load_tex(root, su['shader'], cache)
            for tri in su['tris']:
                tri_list.append(([convert.to_weapon(su['verts'][i], g, s) for i in tri], [su['st'][i] for i in tri], t))
        by_idx = {c[0]: c[2] for c in cache.values()}
        if sp.get('mirror'):
            mz = convert.to_weapon(sp['muzzle'], g, s); ax = mz[0]; ay = mz[1]; mr = sp.get('mirror_r')
            sp_pos, _ = convert.sample_surface(tri_list, by_idx)
            from scipy.spatial import cKDTree
            tree = cKDTree(sp_pos)
            for (pts, uvs, t) in list(tri_list):
                mp = [(2 * ax - p[0], p[1], p[2]) for p in pts]
                c = np.mean(mp, 0)
                if abs(c[0] - ax) < 0.004 or tree.query(c)[0] < 0.004: continue
                if mr is not None and max(np.hypot(p[0] - ax, p[1] - ay) for p in pts) > mr: continue
                tri_list.append(([mp[0], mp[2], mp[1]], [uvs[0], uvs[2], uvs[1]], t))
        hv, hn, hf = hull.build([(tl[0],) for tl in tri_list])
        sp_pos, sp_col = convert.sample_surface(tri_list, by_idx)
        hc = hull.colors_from(hv, sp_pos, sp_col)
        P = np.array([tl[0] for tl in tri_list], np.float32)
        UV = np.array([tl[1] for tl in tri_list], np.float32)
        T = np.array([tl[2] for tl in tri_list], np.int32)
        np.savez(os.path.join(outdir, 'w%d.npz' % wid), P=P, UV=UV, T=T, HV=hv.astype(np.float32), HF=hf.astype(np.int32), HC=hc.astype(np.float32),
                 muzzle=np.array(convert.to_weapon(sp['muzzle'], g, s), np.float32))
        info[wid] = len(tri_list)
        print(wid, 'src tris', len(tri_list), 'hull tris', len(hf))
    texs = {}
    for p, (i, im, _) in cache.items():
        fn = os.path.join(outdir, 'tex%d.png' % i); im.save(fn); texs[i] = fn
    json.dump({'textures': texs, 'weapons': info}, open(os.path.join(outdir, 'index.json'), 'w'))
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
