# Ghoul2 GLM reader (LOD 0, all surfaces, bind pose: weapon world models use a single root bone).
import struct
def read_glm(path):
    b = open(path, 'rb').read()
    ident, ver = struct.unpack_from('<4si', b, 0)
    assert ident == b'2LGM', path
    name = b[8:72].split(b'\0')[0].decode('latin1')
    anim = b[72:136].split(b'\0')[0].decode('latin1')
    animIndex, numBones, numLODs, ofsLODs, numSurfaces, ofsSurfHier, ofsEnd = struct.unpack_from('<7i', b, 136)
    # surface hierarchy: offsets table then entries
    hier = []
    for i in range(numSurfaces):
        off = struct.unpack_from('<i', b, 164 + i * 4)[0]
        o = 164 + off
        sname = b[o:o + 64].split(b'\0')[0].decode('latin1')
        flags = struct.unpack_from('<I', b, o + 64)[0]
        shader = b[o + 68:o + 132].split(b'\0')[0].decode('latin1')
        hier.append(dict(name=sname, flags=flags, shader=shader))
    # LOD 0
    o = ofsLODs
    lodEnd = struct.unpack_from('<i', b, o)[0]
    tbl = o + 4
    surfs = []
    for i in range(numSurfaces):
        so = tbl + struct.unpack_from('<i', b, tbl + i * 4)[0]
        ident2, idx, ofsHeader, nv, ofsV, nt, ofsT, nBR, ofsBR, ofsE = struct.unpack_from('<10i', b, so)
        tris = [struct.unpack_from('<3i', b, so + ofsT + k * 12) for k in range(nt)]
        verts = []; norms = []
        for k in range(nv):
            vo = so + ofsV + k * 32
            n = struct.unpack_from('<3f', b, vo); p = struct.unpack_from('<3f', b, vo + 12)
            verts.append(p); norms.append(n)
        sto = so + ofsV + nv * 32
        st = [struct.unpack_from('<2f', b, sto + k * 8) for k in range(nv)]
        h = hier[idx]
        surfs.append(dict(name=h['name'], shaders=[h['shader']], flags=h['flags'], tris=tris, st=st, verts=verts, norms=norms))
    return dict(name=name, tags={}, surfs=surfs)
if __name__ == '__main__':
    import sys
    for p in sys.argv[1:]:
        m = read_glm(p)
        nt = sum(len(s['tris']) for s in m['surfs'])
        xs = [v for s in m['surfs'] for v in s['verts']]
        mn = [min(v[i] for v in xs) for i in range(3)]; mx = [max(v[i] for v in xs) for i in range(3)]
        print(p, 'tris', nt, 'bbox', [round(a, 1) for a in mn], [round(a, 1) for a in mx])
        for s in m['surfs']: print('  surf', s['name'], s['shaders'], hex(s['flags']), len(s['tris']))
