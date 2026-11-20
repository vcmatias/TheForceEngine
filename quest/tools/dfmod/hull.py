# Completes a first-person (one-sided) weapon model: a closed "visual hull" from its side, top and front
# silhouettes, slightly inset, colored from the nearest textured surface of the original model. The
# original textured mesh is drawn over it; the hull shows wherever the view model has no geometry.
import numpy as np
from scipy import ndimage
from scipy.spatial import cKDTree
from skimage.draw import polygon as skpoly
from skimage.measure import marching_cubes
def build(tris, res=0.003, inset=1.0):
    """tris: list of (p0,p1,p2) positions (m) and (c0,c1,c2) colors sampler -> returns verts, normals, colors, faces"""
    P = np.array([t[0] for t in tris], dtype=np.float64)          # (n,3,3)
    mn = P.reshape(-1, 3).min(0) - 3 * res; mx = P.reshape(-1, 3).max(0) + 3 * res
    dims = np.ceil((mx - mn) / res).astype(int) + 1
    Q = (P - mn) / res
    def sil(a, b):
        im = np.zeros((dims[a], dims[b]), bool)
        for t in Q:
            rr, cc = skpoly(t[:, a], t[:, b], shape=im.shape)
            im[rr, cc] = True
            # thin triangles: also mark the vertices and edge midpoints
            for k in range(3):
                for s in np.linspace(0, 1, 6):
                    p = t[k] * (1 - s) + t[(k + 1) % 3] * s
                    i, j = int(round(p[a])), int(round(p[b]))
                    if 0 <= i < im.shape[0] and 0 <= j < im.shape[1]: im[i, j] = True
        im = ndimage.binary_closing(im, structure=np.ones((3, 3)), iterations=2)
        return ndimage.binary_fill_holes(im)
    sxy = sil(0, 1); sxz = sil(0, 2); syz = sil(1, 2)
    occ = sxy[:, :, None] & sxz[:, None, :] & syz[None, :, :]
    # Inset (about one voxel) so the original textured surfaces stay in front wherever they exist.
    occ = ndimage.binary_erosion(occ, iterations=1) | (ndimage.binary_erosion(occ, iterations=1) == occ) & False
    f = ndimage.gaussian_filter(occ.astype(np.float32), 0.9)
    level = 0.5
    v, faces, n, _ = marching_cubes(np.pad(f, 1), level)
    v = (v - 1) * res + mn
    n = -n
    return v, n, faces
def colors_from(v, samples_pos, samples_col):
    tree = cKDTree(samples_pos)
    d, i = tree.query(v, k=4)
    w = 1.0 / (d + 1e-4)
    return (samples_col[i] * w[..., None]).sum(1) / w.sum(1)[:, None]
