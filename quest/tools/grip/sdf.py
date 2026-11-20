# Signed distance grid of a Blender weapon (npz from kit.finish) around the grip, in the kit frame.
#   python3 sdf.py <weapon.npz> <out.bin>
# Output: int nx,ny,nz; float x0,y0,z0,step; float d[nz][ny][nx] (meters, negative inside).
import sys, struct, numpy as np
from scipy.spatial import cKDTree
V = np.load(sys.argv[1])['V']
P = V[:, :3].reshape(-1, 3, 3)
LO = np.array([-0.07, -0.08, -0.10]); HI = np.array([0.06, 0.07, 0.09]); ST = 0.002
drops = [np.array([float(x) for x in a.split(',')]) for a in sys.argv[3:]]   # x0,y0,z0,x1,y1,z1 boxes whose triangles are removed
c = P.mean(1); keep = np.all((c > LO - 0.03) & (c < HI + 0.03), 1)
for d in drops: keep &= ~np.all((c > d[:3]) & (c < d[3:]), 1)
P = P[keep]
e1 = P[:, 1] - P[:, 0]; e2 = P[:, 2] - P[:, 0]; n = np.cross(e1, e2); A = np.linalg.norm(n, axis=1) * 0.5
n = n / (2 * A[:, None] + 1e-12)
cnt = np.maximum(1, np.ceil(A / (0.0004 ** 2)).astype(int))
idx = np.repeat(np.arange(len(P)), cnt)
r1 = np.random.rand(len(idx)); r2 = np.random.rand(len(idx)); s = np.sqrt(r1)
S = P[idx, 0] * (1 - s)[:, None] + P[idx, 1] * (s * (1 - r2))[:, None] + P[idx, 2] * (s * r2)[:, None]
S = np.vstack([S, P.reshape(-1, 3)]); N = np.vstack([n[idx], np.repeat(n, 3, 0)])
tree = cKDTree(S)
nx, ny, nz = [int(round((HI[i] - LO[i]) / ST)) + 1 for i in range(3)]
zz, yy, xx = np.meshgrid(LO[2] + ST * np.arange(nz), LO[1] + ST * np.arange(ny), LO[0] + ST * np.arange(nx), indexing='ij')
Q = np.stack([xx.ravel(), yy.ravel(), zz.ravel()], 1)
d, k = tree.query(Q, k=8)
# sign: majority of the nearest samples' normals
sg = np.einsum('qkj,qkj->qk', Q[:, None, :] - S[k], N[k])
sign = np.where((sg > 0).sum(1) >= 4, 1.0, -1.0)
D = (d[:, 0] * sign).astype(np.float32)
with open(sys.argv[2], 'wb') as f:
    f.write(struct.pack('<iii4f', nx, ny, nz, LO[0], LO[1], LO[2], ST)); f.write(D.tobytes())
print(sys.argv[1], 'samples', len(S), 'grid', nx, ny, nz, 'inside', int((D < 0).sum()))
