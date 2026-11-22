# DFmod weapons, complete: the first-person model (detailed, one-sided) where it has surface, and the
# mod's own third-person model (complete) registered onto it (register.py) filling everything else.
# Output per weapon (hand space units): triangles, UVs, texture paths. Used by pack_comb.py.
import os, sys
import numpy as np
from scipy.spatial import cKDTree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from assemble import assemble
from register import world_surfs, WORLD
import convert
def tex_path(root, shader):
    return convert.find_tex(root, shader)
def refine(V, vt, Vs, rng):
    """Rigid ICP of one world part onto the first-person model; kept only when it clearly fits better
    and moves little (parts the view model never had stay where the global registration put them)."""
    from register import sample, umeyama
    T = V
    X = T.reshape(-1, 3)
    A = X.copy(); Rt, tt = np.eye(3), np.zeros(3)
    f0 = (vt.query(A)[0] < 0.3).mean()
    for it in range(30):
        d, j = vt.query(A); k = d <= np.quantile(d, 0.5)
        _, R2, t2 = umeyama(A[k], Vs[j[k]])
        A = A @ R2.T + t2; Rt = R2 @ Rt; tt = R2 @ tt + t2
    f1 = (vt.query(A)[0] < 0.3).mean()
    ang = np.degrees(np.arccos(np.clip((np.trace(Rt) - 1) / 2, -1, 1)))
    move = np.linalg.norm(A.mean(0) - X.mean(0))
    ok = f1 > f0 + 0.10 and ang < 25 and move < 1.2
    return ok, Rt, tt, f0, f1, ang, move
def tri_records(surfs, root, xf=None, vt=None, Vs=None, log=None):
    P, UV, TX = [], [], []
    rng = np.random.default_rng(3)
    for su in surfs:
        tp = tex_path(root, su['shader'])
        V = np.array(su['verts'], float)
        if xf is not None: V = xf(V)
        if vt is not None and len(su['tris']) >= 4:
            from register import sample
            pts = sample(np.array([V[list(t)] for t in su['tris']]), 2000, rng)
            ok, Rt, tt, f0, f1, ang, move = refine(pts, vt, Vs, rng)
            if log is not None: log.append('%s %s %.2f->%.2f %.0fdeg %.2f' % (su['name'], 'ref' if ok else '-', f0, f1, ang, move))
            if ok: V = V @ Rt.T + tt
        for t in su['tris']:
            P.append(V[list(t)]); UV.append([su['st'][i] for i in t]); TX.append(tp)
    return np.array(P), np.array(UV, float), TX
def surf_samples(P, step=0.08):
    out = []
    for p in P:
        a = 0.5 * np.linalg.norm(np.cross(p[1] - p[0], p[2] - p[0]))
        k = max(3, int(a / (step * step)))
        r = np.random.rand(k, 2); m = r.sum(1) > 1; r[m] = 1 - r[m]
        out.append(p[0] + r[:, :1] * (p[1] - p[0]) + r[:, 1:] * (p[2] - p[0]))
    return np.concatenate(out)
def split(p, uv):
    m = [(p[0] + p[1]) / 2, (p[1] + p[2]) / 2, (p[2] + p[0]) / 2]
    mu = [(uv[0] + uv[1]) / 2, (uv[1] + uv[2]) / 2, (uv[2] + uv[0]) / 2]
    idx = [(0, 3, 5), (3, 1, 4), (5, 4, 2), (3, 4, 5)]
    pp = [p[0], p[1], p[2]] + m; uu = [uv[0], uv[1], uv[2]] + mu
    return [(np.array([pp[a], pp[b], pp[c]]), np.array([uu[a], uu[b], uu[c]])) for a, b, c in idx]
def combine(root, wid, regdir, thr=0.30, depth=4, verbose=True):
    np.random.seed(1)
    vs, _ = assemble(root, wid)
    VP, VUV, VTX = tri_records(vs, root)
    r = np.load(os.path.join(regdir, 'reg%d.npz' % wid))
    s, R, t = float(r['s']), r['R'], r['t']
    Vs = surf_samples(VP)
    tree = cKDTree(Vs)
    log = []
    WP, WUV, WTX = tri_records(world_surfs(root, wid), root, lambda V: s * V @ R.T + t, tree, Vs, log)
    if verbose: print(wid, '; '.join(log))
    outP, outUV, outTX = [], [], []
    def near(pts): return tree.query(pts)[0] < thr
    def walk(p, uv, tx, d):
        q = np.vstack([p, p.mean(0, keepdims=True), (p[0] + p[1]) / 2, (p[1] + p[2]) / 2, (p[2] + p[0]) / 2])
        n = near(q)
        if not n.any():
            outP.append(p); outUV.append(uv); outTX.append(tx); return
        if n.all() or d >= depth:
            if n[3]: return
            outP.append(p); outUV.append(uv); outTX.append(tx); return
        for pp, uu in split(p, uv): walk(pp, uu, tx, d + 1)
    for p, uv, tx in zip(WP, WUV, WTX): walk(p, uv, tx, 0)
    added = len(outP)
    P = np.concatenate([VP, np.array(outP).reshape(-1, 3, 3)]); UV = np.concatenate([VUV, np.array(outUV).reshape(-1, 3, 2)])
    TX = list(VTX) + outTX
    src = np.array([0] * len(VP) + [1] * added)
    return P, UV, TX, src
def run(root, cdir, verbose=True):
    """Combines every weapon (needs <cdir>/reg<id>.npz from register.run) and writes <cdir>/c<id>.npz."""
    for wid in WORLD:
        P, UV, TX, src = combine(root, wid, cdir, verbose=verbose)
        np.savez(os.path.join(cdir, 'c%d.npz' % wid), P=P, UV=UV, TX=np.array([x or '' for x in TX]), src=src)
        if verbose: print(wid, 'first-person tris', int((src == 0).sum()), 'world tris added', int((src == 1).sum()))
if __name__ == '__main__':
    # python3 combine.py <mod models/weapons2 folder> <comb dir>
    run(sys.argv[1], sys.argv[2])
