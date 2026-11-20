# Blender (headless):  blender -b -P blender_caps.py -- <workdir> <weapon id>
# Finishes the DFmod first-person model in Blender:
#  - source triangles (original + mirrored side) welded, keeping UVs per corner and the texture per face;
#  - every open boundary loop filled (holes_fill) and triangulated (beauty); cap faces take the color of
#    the closest point of the inset hull (vertex colors) and use no texture (tex -2);
#  - smooth shading by angle plus Weighted Normal (hard-surface look: flat faces stay flat, rounded parts
#    smooth, edges crisp) instead of one flat normal per triangle.
# Writes w<id>_fin.npz: per corner x y z nx ny nz u v r g b and per triangle the texture index.
import bpy, bmesh, sys, os
import numpy as np
from mathutils.kdtree import KDTree
argv = sys.argv[sys.argv.index('--') + 1:]
wd, wid = argv[0], int(argv[1])
d = np.load(os.path.join(wd, 'w%d.npz' % wid))
P = d['P']; UV = d['UV']; T = d['T']
bpy.ops.wm.read_factory_settings(use_empty=True)
texs = sorted(set(int(t) for t in T)) + [-2]
slot = {t: i for i, t in enumerate(texs)}
bm = bmesh.new()
uvl = bm.loops.layers.uv.new('UV')
for i in range(len(T)):
    vs = [bm.verts.new(tuple(p)) for p in P[i]]
    try: f = bm.faces.new(vs)
    except ValueError: continue
    f.material_index = slot[int(T[i])]
    for k, l in enumerate(f.loops): l[uvl].uv = (float(UV[i][k][0]), float(UV[i][k][1]))
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0004)
bnd = [e for e in bm.edges if e.is_boundary]
res = bmesh.ops.holes_fill(bm, edges=bnd, sides=0)
tri = bmesh.ops.triangulate(bm, faces=res['faces'], quad_method='BEAUTY', ngon_method='BEAUTY')
caps = set(tri['faces']) if tri['faces'] else set(res['faces'])
for f in caps: f.material_index = slot[-2]
bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
me = bpy.data.meshes.new('w'); bm.to_mesh(me); bm.free()
ob = bpy.data.objects.new('w', me); bpy.context.scene.collection.objects.link(ob)
for t in texs: me.materials.append(bpy.data.materials.new('t%d' % t))
bpy.context.view_layer.objects.active = ob; ob.select_set(True)
bpy.ops.object.shade_smooth_by_angle(angle=0.61)            # ~35 degrees
wn = ob.modifiers.new('wn', 'WEIGHTED_NORMAL'); wn.keep_sharp = True; wn.weight = 50
bpy.ops.object.modifier_apply(modifier='wn')
# hull colors for the caps
HV = d['HV']; HC = d['HC']
kd = KDTree(len(HV))
for i, p in enumerate(HV): kd.insert(tuple(p), i)
kd.balance()
me = ob.data
me.calc_loop_triangles()
uvd = me.uv_layers['UV'].data
V = []; TT = []
for t in me.loop_triangles:
    tex = texs[me.polygons[t.polygon_index].material_index]
    for li in t.loops:
        l = me.loops[li]; p = me.vertices[l.vertex_index].co; n = l.normal
        if tex < 0: c = HC[kd.find(p)[1]]; u = (0.0, 0.0)   # caps and parts without a texture in the mod
        else: c = (1.0, 1.0, 1.0); u = uvd[li].uv
        V.append((p.x, p.y, p.z, n.x, n.y, n.z, u[0], u[1], c[0], c[1], c[2]))
    TT.append(tex)
np.savez(os.path.join(wd, 'w%d_fin.npz' % wid), V=np.array(V, np.float32), T=np.array(TT, np.int32))
print('WEAPON', wid, 'tris', len(TT), 'caps', len(caps), 'still open', sum(1 for e in me.edges if len([f for f in me.polygons if e.key in f.edge_keys]) == 1) if False else '-')
