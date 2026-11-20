# Lists objects of a weapon .blend that do not touch any other object (gap > tol).
import bpy, sys, bmesh
from mathutils.bvhtree import BVHTree
tol = 0.0005
obs = [o for o in bpy.data.objects if o.type == 'MESH']
dg = bpy.context.evaluated_depsgraph_get()
trees, verts = {}, {}
for o in obs:
    e = o.evaluated_get(dg); m = e.to_mesh()
    bm = bmesh.new(); bm.from_mesh(m); bm.transform(o.matrix_world); bm.verts.ensure_lookup_table()
    trees[o.name] = BVHTree.FromBMesh(bm); verts[o.name] = [v.co.copy() for v in bm.verts]
    bbmin = [min(v[i] for v in verts[o.name]) for i in range(3)] if verts[o.name] else [0,0,0]
    bbmax = [max(v[i] for v in verts[o.name]) for i in range(3)] if verts[o.name] else [0,0,0]
    o['bb'] = (bbmin, bbmax); bm.free(); e.to_mesh_clear()
for o in obs:
    best = 1e9; bestn = None
    t = trees[o.name]
    for p in obs:
        if p is o: continue
        if t.overlap(trees[p.name]): best = 0; bestn = p.name; break
        tp = trees[p.name]
        for v in verts[o.name][::max(1, len(verts[o.name]) // 400)]:
            r = tp.find_nearest(v)
            if r[0] is not None and r[3] < best: best = r[3]; bestn = p.name
    if best > tol:
        bb = o['bb']
        print('FLOAT %s gap %.4f nearest %s bbox x %.4f..%.4f y %.4f..%.4f z %.4f..%.4f' % (o.name, best, bestn, bb[0][0], bb[1][0], bb[0][1], bb[1][1], bb[0][2], bb[1][2]))
print('checked', len(obs))
