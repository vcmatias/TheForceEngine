# Step 2 (Blender, headless):  blender -b -P blender_complete.py -- <workdir> <weapon id> [texsize]
# The original surfaces are kept as they are (crisp, original textures); the baked closed shell only
# fills what the first-person model never had. Builds the textured source (DFmod first-person model + mirrored side) and the colored hull, makes a
# closed remeshed weapon (voxel remesh of source + hull), decimates it, unwraps it and bakes the source
# textures onto it (rays that miss the source take the hull colors). Writes w<id>_out.npz + w<id>_bake.png.
import bpy, bmesh, sys, os, json
import numpy as np
argv = sys.argv[sys.argv.index('--') + 1:]
wd, wid = argv[0], int(argv[1]); texsize = int(argv[2]) if len(argv) > 2 else 2048
VOXEL = 0.0009
TARGET_TRIS = 40000
INSET = 0.0015   # the shell sits just behind the original surfaces, which stay crisp in front
idx = json.load(open(os.path.join(wd, 'index.json')))
d = np.load(os.path.join(wd, 'w%d.npz' % wid))
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 1
# Blender is Z-up: store weapon (x right, y up, z back) as (x, -z, y)
def B(p): return (p[0], -p[2], p[1])
def mesh_obj(name, verts, faces):
    me = bpy.data.meshes.new(name); me.from_pydata(verts, [], faces); me.update()
    ob = bpy.data.objects.new(name, me); scene.collection.objects.link(ob); return ob
# --- source with materials per texture
P, UV, T = d['P'], d['UV'], d['T']
verts = [B(p) for tri in P for p in tri]
faces = [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(len(P))]
src = mesh_obj('src', verts, faces)
uvl = src.data.uv_layers.new(name='UV')
for i, loop in enumerate(src.data.loops):
    u, v = UV[loop.vertex_index // 3][loop.vertex_index % 3]
    uvl.data[i].uv = (u, 1.0 - v)
mats = {}
def tex_mat(t):
    if t in mats: return mats[t]
    m = bpy.data.materials.new('m%d' % t); m.use_nodes = True
    nt = m.node_tree; bsdf = nt.nodes['Principled BSDF']
    if t >= 0:
        im = bpy.data.images.load(idx['textures'][str(t)]); im.alpha_mode = 'NONE'
        tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = im; tn.interpolation = 'Linear'
        nt.links.new(tn.outputs['Color'], bsdf.inputs['Base Color'])
    else:
        bsdf.inputs['Base Color'].default_value = (0.08, 0.08, 0.09, 1)
    src.data.materials.append(m); mats[t] = len(src.data.materials) - 1
    return mats[t]
for i, poly in enumerate(src.data.polygons): poly.material_index = tex_mat(int(T[i]))
# --- hull with vertex colors (emission-free base color from the attribute)
hull = mesh_obj('hull', [B(p) for p in d['HV']], [tuple(f) for f in d['HF']])
ca = hull.data.color_attributes.new('Col', 'FLOAT_COLOR', 'POINT')
for i, c in enumerate(d['HC']): ca.data[i].color = (c[0], c[1], c[2], 1.0)
hm = bpy.data.materials.new('hullmat'); hm.use_nodes = True
an = hm.node_tree.nodes.new('ShaderNodeVertexColor'); an.layer_name = 'Col'
hm.node_tree.links.new(an.outputs['Color'], hm.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
hull.data.materials.append(hm)
# --- target: union of source and hull, voxel remeshed (closed), decimated
tgt = mesh_obj('tgt', [], [])
bm = bmesh.new()
for ob in (src, hull):
    bm2 = bmesh.new(); bm2.from_mesh(ob.data)
    tmp = bpy.data.meshes.new('tmp'); bm2.to_mesh(tmp); bm2.free()
    bm.from_mesh(tmp); bpy.data.meshes.remove(tmp)
bm.to_mesh(tgt.data); bm.free()
bpy.context.view_layer.objects.active = tgt
for o in scene.objects: o.select_set(False)
tgt.select_set(True)
tgt.data.remesh_voxel_size = VOXEL
tgt.data.remesh_voxel_adaptivity = 0.0
bpy.ops.object.voxel_remesh()
nt = len(tgt.data.polygons) * 2
if nt > TARGET_TRIS:
    mod = tgt.modifiers.new('dec', 'DECIMATE'); mod.ratio = TARGET_TRIS / nt
    bpy.ops.object.modifier_apply(modifier='dec')
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.quads_convert_to_tris()
bpy.ops.transform.shrink_fatten(value=-INSET)
bpy.ops.uv.smart_project(angle_limit=1.15, island_margin=0.004)
bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.object.shade_smooth_by_angle(angle=0.6)
# bake image
img = bpy.data.images.new('bake', texsize, texsize, alpha=False)
img.generated_color = (0.05, 0.05, 0.06, 1)
tm = bpy.data.materials.new('tgtmat'); tm.use_nodes = True
tn = tm.node_tree.nodes.new('ShaderNodeTexImage'); tn.image = img
tm.node_tree.nodes.active = tn
tgt.data.materials.clear(); tgt.data.materials.append(tm)
for o in scene.objects: o.select_set(False)
src.select_set(True); hull.select_set(True); tgt.select_set(True)
bpy.context.view_layer.objects.active = tgt
bk = scene.render.bake
bk.use_selected_to_active = True; bk.cage_extrusion = 0.006; bk.max_ray_distance = 0.014; bk.margin = 6
bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'}, use_selected_to_active=True, cage_extrusion=0.006, max_ray_distance=0.014, margin=6)
out_png = os.path.join(wd, 'w%d_bake.png' % wid)
img.filepath_raw = out_png; img.file_format = 'PNG'; img.save()
# export: per loop (split normals), weapon frame
me = tgt.data
me.calc_loop_triangles()
uvd = me.uv_layers.active.data
try:
    ln = [tuple(l.normal) for l in me.loops]   # 4.1+: corner normals
except Exception:
    me.calc_normals_split(); ln = [tuple(l.normal) for l in me.loops]
V = []
for tri in me.loop_triangles:
    for li in tri.loops:
        lp = me.loops[li]; p = me.vertices[lp.vertex_index].co; n = ln[li]; uv = uvd[li].uv
        V.append((p[0], p[2], -p[1], n[0], n[2], -n[1], uv[0], 1.0 - uv[1]))
np.savez(os.path.join(wd, 'w%d_out.npz' % wid), V=np.array(V, np.float32), muzzle=d['muzzle'])
print('WEAPON', wid, 'tris', len(V) // 3)
