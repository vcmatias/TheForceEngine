# Registers the DFmod third-person (world) model of each weapon onto its first-person model in hand
# space: similarity transform (rotation, uniform scale, translation) by trimmed ICP from the 24 axis
# rotations. Result: S with world_in_hand = s * R @ p + t.
import os, sys, itertools
import numpy as np
from scipy.spatial import cKDTree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from assemble import assemble, WEAPONS
from glm import read_glm
WORLD = {1: 'briar_pistol/briar_pistol_w.glm', 2: 'blaster_r/blaster_w.glm', 4: 'heavy_repeater/heavy_repeater_w.glm',
         5: 'demp2/demp2_w.glm', 6: 'golan_arms/golan_arms_w.glm', 8: 'concussion/c_rifle_w.glm', 9: 'merr_sonn/merr_sonn_w.glm'}
def world_surfs(root, wid):
    m = read_glm(os.path.join(root, WORLD[wid]))
    out = []
    for s in m['surfs']:
        if s['name'].startswith('*') or not s['tris']: continue
        out.append(dict(name=s['name'], shader=s['shaders'][0], tris=s['tris'], st=s['st'], verts=s['verts'], norms=s['norms']))
    return out
def tris_of(surfs):
    return np.array([[su['verts'][i] for i in t] for su in surfs for t in su['tris']], float)
def sample(T, n, rng):
    a = 0.5 * np.linalg.norm(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]), axis=1)
    idx = rng.choice(len(T), n, p=a / a.sum())
    r = rng.random((n, 2)); m = r.sum(1) > 1; r[m] = 1 - r[m]
    t = T[idx]
    return t[:, 0] + r[:, :1] * (t[:, 1] - t[:, 0]) + r[:, 1:] * (t[:, 2] - t[:, 0])
def rotations():
    out = []
    for perm in itertools.permutations(range(3)):
        for sg in itertools.product((1, -1), repeat=3):
            R = np.zeros((3, 3))
            for i in range(3): R[i, perm[i]] = sg[i]
            if np.linalg.det(R) > 0: out.append(R)
    return out
def umeyama(A, B):
    ma, mb = A.mean(0), B.mean(0); a, b = A - ma, B - mb
    U, D, Vt = np.linalg.svd(b.T @ a / len(A))
    S = np.eye(3)
    if np.linalg.det(U) * np.linalg.det(Vt) < 0: S[2, 2] = -1
    R = U @ S @ Vt
    s = np.trace(np.diag(D) @ S) / (a ** 2).sum(1).mean()
    return s, R, mb - s * R @ ma
def register(root, wid, verbose=False, ok=None):
    rng = np.random.default_rng(1)
    vs, _ = assemble(root, wid)
    V = sample(tris_of(vs), 6000, rng)
    W = sample(tris_of(world_surfs(root, wid)), 30000, rng)
    tree = cKDTree(V)          # map world -> view? view is partial: map view points onto the world surface
    best = None
    ev = V.max(0) - V.min(0); ew = W.max(0) - W.min(0)
    for R0 in rotations():
        # world -> hand: x = s R w + t ; ICP over view points against transformed world
        Wr = W @ R0.T
        s0 = ev.max() / (Wr.max(0) - Wr.min(0)).max()
        for sk in (0.8, 1.0, 1.25):
            s, R, t = s0 * sk, R0, None
            t = V.mean(0) - s * (W @ R.T).mean(0)
            # align centers of bounding boxes instead (view is partial): use mid of barrel extent
            t = (V.max(0) + V.min(0)) / 2 - s * ((W @ R.T).max(0) + (W @ R.T).min(0)) / 2
            for it in range(40):
                X = s * W @ R.T + t
                wt = cKDTree(X)
                d, j = wt.query(V)
                keep = d <= np.quantile(d, 0.85)
                # solve sim from world sample j to view points
                s, R, t = umeyama(W[j[keep]], V[keep])
            X = s * W @ R.T + t
            d, _ = cKDTree(X).query(V)
            score = np.mean(np.minimum(d, 1.0))
            if ok is not None and not ok(s, R, t): continue
            if best is None or score < best[0]: best = (score, s, R, t)
    if verbose: print(wid, 'fit mean dist %.3f (units)' % best[0], 'scale %.3f' % best[1])
    return best
def run(root, outdir, ids=None, verbose=True):
    """Registers the world model of every weapon and writes <outdir>/reg<id>.npz."""
    import convert
    os.makedirs(outdir, exist_ok=True)
    for wid in (ids or list(WORLD)):
        key, tgt = ('w_exit', 'muzzle') if wid == 6 else ('w_handle', 'grip')
        hs = [np.array(su['verts']) for su in world_surfs(root, wid) if su['name'] == key]
        g = np.array(convert.SPEC[wid][tgt])
        ok = (lambda s, R, t: np.linalg.norm((s * hs[0] @ R.T + t).mean(0) - g) < 5.0) if hs else None
        b = register(root, wid, verbose, ok)
        np.savez(os.path.join(outdir, 'reg%d.npz' % wid), s=b[1], R=b[2], t=b[3], score=b[0])
if __name__ == '__main__':
    # python3 register.py <mod models/weapons2 folder> <comb dir> [weapon ids]
    run(sys.argv[1], sys.argv[2], [int(a) for a in sys.argv[3:]] or None)
