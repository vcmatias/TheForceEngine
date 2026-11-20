# Tiny z-buffer rasterizer for review images: textured, lambert + ambient, double sided.
import numpy as np
from PIL import Image
_tex = {}
def tex(path):
    if path not in _tex:
        _tex[path] = np.asarray(Image.open(path).convert('RGB'), np.float32) / 255.0 if path else None
    return _tex[path]
def look(yaw, pitch):
    cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
    Rz = np.array([[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]]); Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    return Rx @ Rz
def render(P, UV, TX, R, W=480, H=360, colors=None, bbox=None):
    """P: (n,3,3) points (z up); camera looks along +y after rotation R; returns RGB image"""
    Q = P @ R.T
    lo = Q.reshape(-1, 3).min(0) if bbox is None else bbox[0]; hi = Q.reshape(-1, 3).max(0) if bbox is None else bbox[1]
    sc = 0.92 * min(W / (hi[0] - lo[0]), H / (hi[2] - lo[2]))
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    X = (Q[..., 0] - cx) * sc + W / 2; Y = H / 2 - (Q[..., 2] - cz) * sc; Z = Q[..., 1]
    img = np.full((H, W, 3), 0.12, np.float32); zb = np.full((H, W), np.inf)
    n = np.cross(Q[:, 1] - Q[:, 0], Q[:, 2] - Q[:, 0]); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    L = np.array([0.3, -0.6, 0.75]); L /= np.linalg.norm(L)
    shade = 0.35 + 0.65 * np.abs(n @ L)
    for i in range(len(P)):
        x, y, z = X[i], Y[i], Z[i]
        x0, x1 = int(max(0, np.floor(x.min()))), int(min(W - 1, np.ceil(x.max())))
        y0, y1 = int(max(0, np.floor(y.min()))), int(min(H - 1, np.ceil(y.max())))
        if x1 < x0 or y1 < y0: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(d) < 1e-9: continue
        a = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / d
        b = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / d
        c = 1 - a - b
        m = (a >= -1e-4) & (b >= -1e-4) & (c >= -1e-4)
        if not m.any(): continue
        zz = a * z[0] + b * z[1] + c * z[2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m &= zz < sub
        if not m.any(): continue
        if colors is not None:
            col = np.broadcast_to(colors[i], m.shape + (3,))[m]
        else:
            tx = tex(TX[i])
            if tx is None: col = np.full((m.sum(), 3), 0.5)
            else:
                u = (a * UV[i, 0, 0] + b * UV[i, 1, 0] + c * UV[i, 2, 0])[m] % 1.0
                v = (a * UV[i, 0, 1] + b * UV[i, 1, 1] + c * UV[i, 2, 1])[m] % 1.0
                h, w = tx.shape[:2]
                col = tx[(v * h).astype(int) % h, (u * w).astype(int) % w]
        sub[m] = zz[m]
        img[y0:y1 + 1, x0:x1 + 1][m] = col * shade[i]
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
