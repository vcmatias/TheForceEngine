# Quake 3 / Jedi Academy MD3 reader (frame 0).
import struct, math
def read_md3(path):
    b = open(path, 'rb').read()
    ident, ver = struct.unpack_from('<4si', b, 0)
    assert ident == b'IDP3', path
    name = b[8:72].split(b'\0')[0].decode('latin1')
    flags, nf, nt, ns, nsk, of, ot, osf, oe = struct.unpack_from('<9i', b, 72)
    tags = {}
    for t in range(nt):  # frame 0
        o = ot + t * 112
        tn = b[o:o + 64].split(b'\0')[0].decode('latin1')
        org = struct.unpack_from('<3f', b, o + 64); ax = struct.unpack_from('<9f', b, o + 76)
        tags[tn] = (org, (ax[0:3], ax[3:6], ax[6:9]))
    surfs = []
    o = osf
    for s in range(ns):
        sid = b[o:o + 4]; sname = b[o + 4:o + 68].split(b'\0')[0].decode('latin1')
        sflags, snf, nsh, nv, ntri, otri, osh, ost, oxyz, oend = struct.unpack_from('<10i', b, o + 68)
        shaders = [b[o + osh + i * 68:o + osh + i * 68 + 64].split(b'\0')[0].decode('latin1') for i in range(nsh)]
        tris = [struct.unpack_from('<3i', b, o + otri + i * 12) for i in range(ntri)]
        st = [struct.unpack_from('<2f', b, o + ost + i * 8) for i in range(nv)]
        verts = []; norms = []
        for i in range(nv):
            x, y, z, n = struct.unpack_from('<3hH', b, o + oxyz + i * 8)
            verts.append((x / 64.0, y / 64.0, z / 64.0))
            lat = ((n >> 8) & 255) * 2 * math.pi / 255; lng = (n & 255) * 2 * math.pi / 255
            norms.append((math.cos(lat) * math.sin(lng), math.sin(lat) * math.sin(lng), math.cos(lng)))
        surfs.append(dict(name=sname, shaders=shaders, tris=tris, st=st, verts=verts, norms=norms))
        o += oend
    return dict(name=name, tags=tags, surfs=surfs)
if __name__ == '__main__':
    import sys
    for p in sys.argv[1:]:
        m = read_md3(p)
        nt = sum(len(s['tris']) for s in m['surfs'])
        xs = [v for s in m['surfs'] for v in s['verts']]
        mn = [min(v[i] for v in xs) for i in range(3)]; mx = [max(v[i] for v in xs) for i in range(3)]
        print(p, 'tris', nt, 'bbox', [round(a, 1) for a in mn], [round(a, 1) for a in mx])
        for k, v in m['tags'].items(): print('  tag', k, [round(a, 2) for a in v[0]])
        for s in m['surfs']: print('  surf', s['name'], s['shaders'], len(s['tris']))
