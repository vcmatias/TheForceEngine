# Packs the Blender weapons (<dir>/<name>.npz + <name>.png from kit.finish) into the DFMW v2 file the
# game loads for the weapon style 3: one textured submesh per weapon, texture alpha = gloss.
#   python3 pack.py <dir> <out.bin> [texsize]
import os, sys, struct
import numpy as np
from PIL import Image
TRIG_Z, TRIG_Y0, TRIG_TOP = -0.038, -0.036, -0.002   # kit.py std_trigger
NAMES = {1: 'bryar', 2: 'e11', 3: 'thermal', 4: 'repeater', 5: 'fusion', 6: 'mortar', 7: 'mine', 8: 'concussion', 9: 'cannon'}
def main(wd, out, ts=1024):
    recs, texs = [], []
    for wid, n in NAMES.items():
        f = os.path.join(wd, n + '.npz')
        if not os.path.exists(f): print('missing', n); continue
        d = np.load(f); V = np.asarray(d['V'], np.float32)
        if wid not in (3, 7):
            # Trigger blade (kit.std_trigger): tagged in the vertex color green (2) so the game swings it
            # with the index finger. Whole triangles inside the blade's box.
            P = V[:, :3].reshape(-1, 3, 3)
            lo = np.array([-0.0026, TRIG_Y0 - 0.0010, TRIG_Z - 0.0065]); hi = np.array([0.0026, TRIG_TOP - 0.0006, TRIG_Z + 0.0065])
            inside = np.all((P > lo) & (P < hi), axis=(1, 2))
            V = V.copy(); V.reshape(-1, 3, 11)[inside, :, 9] = 2.0
            print(n, 'trigger triangles', int(inside.sum()))
        recs.append((wid, d['muzzle'], len(texs), V))
        im = Image.open(os.path.join(wd, n + '.png')).convert('RGBA')
        if im.width > ts: im = im.resize((ts, ts), Image.LANCZOS)
        texs.append(im)
    with open(out, 'wb') as f:
        f.write(b'DFMW'); f.write(struct.pack('<ii', 2, len(recs)))
        for wid, m, ti, V in recs:
            f.write(struct.pack('<i3fi', wid, *[float(x) for x in m], 1))
            f.write(struct.pack('<iii', ti, len(V), len(V)))
            f.write(V.tobytes()); f.write(np.arange(len(V), dtype=np.uint32).tobytes())
        f.write(struct.pack('<i', len(texs)))
        for im in texs:
            f.write(struct.pack('<ii', im.width, im.height)); f.write(im.tobytes())
    print('weapons', len(recs), 'bytes', os.path.getsize(out))
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1024)
