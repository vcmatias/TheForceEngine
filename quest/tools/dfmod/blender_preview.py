# blender -b -P blender_preview.py -- <workdir> <weapon id> <out.png> [src]
import bpy, sys, os, math
import numpy as np
from mathutils import Vector
argv = sys.argv[sys.argv.index('--') + 1:]
wd, wid, out = argv[0], int(argv[1]), argv[2]
useSrc = len(argv) > 3 and argv[3] == 'src'
both = len(argv) > 3 and argv[3] == 'both'
caps = len(argv) > 3 and argv[3] in ('caps', 'final')
final = len(argv) > 3 and argv[3] == 'final'
if caps: useSrc = True
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.samples = 16; sc.cycles.device = 'CPU'
sc.render.resolution_x = 520; sc.render.resolution_y = 380
w = bpy.data.worlds.new('w'); w.use_nodes = True; w.node_tree.nodes['Background'].inputs[0].default_value = (0.18, 0.19, 0.22, 1); w.node_tree.nodes['Background'].inputs[1].default_value = 1.5; sc.world = w
if useSrc:
    d = np.load(os.path.join(wd, 'w%d.npz' % wid)); import json; idx = json.load(open(os.path.join(wd, 'index.json')))
    P = d['P'].reshape(-1, 3); UV = d['UV'].reshape(-1, 2); T = d['T']
    verts = [(p[0], -p[2], p[1]) for p in P]; faces = [(3*i, 3*i+1, 3*i+2) for i in range(len(T))]
else:
    d = np.load(os.path.join(wd, 'w%d_out.npz' % wid)); V = d['V']
    verts = [(v[0], -v[2], v[1]) for v in V]; faces = [(3*i, 3*i+1, 3*i+2) for i in range(len(V) // 3)]; UV = V[:, 6:8]
me = bpy.data.meshes.new('m'); me.from_pydata(verts, [], faces); me.update()
ob = bpy.data.objects.new('o', me); sc.collection.objects.link(ob)
uvl = me.uv_layers.new()
for i, l in enumerate(me.loops): uvl.data[i].uv = (UV[l.vertex_index][0], 1 - UV[l.vertex_index][1])
for p in me.polygons: p.use_smooth = not useSrc
def mat(path):
    m = bpy.data.materials.new('m'); m.use_nodes = True; nt = m.node_tree; b = nt.nodes['Principled BSDF']
    tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = bpy.data.images.load(path); tn.image.alpha_mode = 'NONE'
    nt.links.new(tn.outputs['Color'], b.inputs['Base Color']); b.inputs['Roughness'].default_value = 0.45; b.inputs['Metallic'].default_value = 0.3
    return m
if useSrc:
    ts = sorted(set(int(t) for t in T)); mi = {}
    for t in ts:
        me.materials.append(mat(idx['textures'][str(t)]) if t >= 0 else bpy.data.materials.new('x')); mi[t] = len(me.materials) - 1
    for i, p in enumerate(me.polygons): p.material_index = mi[int(T[i])]
else:
    me.materials.append(mat(os.path.join(wd, 'w%d_bake.png' % wid)))
if both:
    import json
    d2 = np.load(os.path.join(wd, 'w%d.npz' % wid)); idx = json.load(open(os.path.join(wd, 'index.json')))
    P2 = d2['P'].reshape(-1, 3); UV2 = d2['UV'].reshape(-1, 2); T2 = d2['T']
    me2 = bpy.data.meshes.new('s'); me2.from_pydata([(p[0], -p[2], p[1]) for p in P2], [], [(3*i, 3*i+1, 3*i+2) for i in range(len(T2))]); me2.update()
    ob2 = bpy.data.objects.new('s', me2); sc.collection.objects.link(ob2)
    u2 = me2.uv_layers.new()
    for i, l in enumerate(me2.loops): u2.data[i].uv = (UV2[l.vertex_index][0], 1 - UV2[l.vertex_index][1])
    mi = {}
    for t in sorted(set(int(t) for t in T2)):
        me2.materials.append(mat(idx['textures'][str(t)]) if t >= 0 else bpy.data.materials.new('x')); mi[t] = len(me2.materials) - 1
    for i, p in enumerate(me2.polygons): p.material_index = mi[int(T2[i])]
if caps:
    C = np.load(os.path.join(wd, 'w%d_caps.npz' % wid))['V']
    if len(C):
        me3 = bpy.data.meshes.new('cap'); me3.from_pydata([(v[0], -v[2], v[1]) for v in C], [], [(3*i, 3*i+1, 3*i+2) for i in range(len(C)//3)]); me3.update()
        ob3 = bpy.data.objects.new('cap', me3); sc.collection.objects.link(ob3)
        ca = me3.color_attributes.new('Col', 'FLOAT_COLOR', 'POINT')
        for i, v in enumerate(C): ca.data[i].color = (v[8], v[9], v[10], 1)
        m3 = bpy.data.materials.new('cm'); m3.use_nodes = True
        vc = m3.node_tree.nodes.new('ShaderNodeVertexColor'); vc.layer_name = 'Col'
        m3.node_tree.links.new(vc.outputs['Color'], m3.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
        me3.materials.append(m3)
if final and os.path.exists(os.path.join(wd, 'w%d_add.npz' % wid)):
    A = np.load(os.path.join(wd, 'w%d_add.npz' % wid))['V']
    if len(A):
        me4 = bpy.data.meshes.new('add'); me4.from_pydata([(v[0], -v[2], v[1]) for v in A], [], [(3*i, 3*i+1, 3*i+2) for i in range(len(A)//3)]); me4.update()
        ob4 = bpy.data.objects.new('add', me4); sc.collection.objects.link(ob4)
        u4 = me4.uv_layers.new()
        for i, l in enumerate(me4.loops): u4.data[i].uv = (A[l.vertex_index][6], 1 - A[l.vertex_index][7])
        for pp in me4.polygons: pp.use_smooth = True
        me4.materials.append(mat(os.path.join(wd, 'w%d_add.png' % wid)))
        verts = verts + [(v[0], -v[2], v[1]) for v in A]
mn = Vector((min(v[0] for v in verts), min(v[1] for v in verts), min(v[2] for v in verts)))
mx = Vector((max(v[0] for v in verts), max(v[1] for v in verts), max(v[2] for v in verts)))
c = (mn + mx) / 2; r = (mx - mn).length
if len(argv) > 4 and argv[4].endswith('.blend'):
    bpy.ops.file.pack_all(); bpy.ops.wm.save_as_mainfile(filepath=argv[4]); sys.exit(0)
cam = bpy.data.objects.new('c', bpy.data.cameras.new('c')); sc.collection.objects.link(cam); sc.camera = cam
cam.data.lens = 50
for k, (az, el) in enumerate([(90, 10), (-90, 10), (30, 35), (200, 25)]):
    a = math.radians(az); e = math.radians(el)
    cam.location = c + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * r * 1.9
    cam.rotation_euler = (c - cam.location).to_track_quat('-Z', 'Y').to_euler()
    L = bpy.data.objects.new('L%d' % k, bpy.data.lights.new('L%d' % k, 'SUN')); L.data.energy = 3; sc.collection.objects.link(L)
    L.rotation_euler = cam.rotation_euler
    sc.render.filepath = out.replace('.png', '_%d.png' % k); bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(L)
