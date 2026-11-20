# Blender (headless): renders the finished weapon (w<id>_fin.npz + w<id>_add.npz) as it goes to the game,
# from the player's usual viewpoints, and optionally saves a .blend with everything packed.
#   blender -b -P blender_review.py -- <workdir> <weapon id> <out prefix> [file.blend]
import bpy, sys, os, math, json
import numpy as np
from mathutils import Vector
argv = sys.argv[sys.argv.index('--') + 1:]
wd, wid, out = argv[0], int(argv[1]), argv[2]
idx = json.load(open(os.path.join(wd, 'index.json')))
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.samples = 24; sc.cycles.device = 'CPU'
sc.render.resolution_x = 640; sc.render.resolution_y = 420
w = bpy.data.worlds.new('w'); w.use_nodes = True
bg = w.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.20, 0.21, 0.24, 1); bg.inputs[1].default_value = 1.2; sc.world = w
def B(p): return (p[0], -p[2], p[1])
def texmat(path, name):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; b = nt.nodes['Principled BSDF']
    tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = bpy.data.images.load(path); tn.image.alpha_mode = 'NONE'
    nt.links.new(tn.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.42; b.inputs['Metallic'].default_value = 0.35
    return m
def colmat():
    m = bpy.data.materials.new('caps'); m.use_nodes = True; nt = m.node_tree
    vc = nt.nodes.new('ShaderNodeVertexColor'); vc.layer_name = 'Col'
    nt.links.new(vc.outputs['Color'], nt.nodes['Principled BSDF'].inputs['Base Color'])
    nt.nodes['Principled BSDF'].inputs['Roughness'].default_value = 0.5
    return m
def build(name, V, T, texpath):
    n = len(V) // 3
    me = bpy.data.meshes.new(name); me.from_pydata([B(v) for v in V], [], [(3*i, 3*i+1, 3*i+2) for i in range(n)]); me.update()
    ob = bpy.data.objects.new(name, me); sc.collection.objects.link(ob)
    me.uv_layers.new(name='UV'); me.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
    uv = me.uv_layers['UV']; ca = me.color_attributes['Col']   # fetched after both exist (adding one invalidates the other)
    for i, l in enumerate(me.loops):
        v = V[l.vertex_index]; uv.data[i].uv = (v[6], 1 - v[7])
        c = v[8:11] if len(v) > 8 else (1, 1, 1); ca.data[i].color = (c[0], c[1], c[2], 1)
    slots = {}
    for t in sorted(set(int(x) for x in T)):
        me.materials.append(colmat() if t < 0 else texmat(texpath(t), 't%d' % t)); slots[t] = len(me.materials) - 1
    for i, p in enumerate(me.polygons): p.material_index = slots[int(T[i])]; p.use_smooth = True
    me.normals_split_custom_set([tuple(B(V[l.vertex_index][3:6])) for l in me.loops])
    return ob
F = np.load(os.path.join(wd, 'w%d_fin.npz' % wid))
build('dfmod', F['V'], F['T'], lambda t: idx['textures'][str(t)])
af = os.path.join(wd, 'w%d_add.npz' % wid)
if os.path.exists(af):
    A = np.load(af)['V']
    if len(A): build('added', A, [0] * (len(A) // 3), lambda t: os.path.join(wd, 'w%d_add.png' % wid))
if len(argv) > 3:
    bpy.ops.file.pack_all(); bpy.ops.wm.save_as_mainfile(filepath=argv[3]); sys.exit(0)
allv = [Vector(B(v)) for v in F['V']]
mn = Vector([min(v[i] for v in allv) for i in range(3)]); mx = Vector([max(v[i] for v in allv) for i in range(3)])
c = (mn + mx) / 2; r = (mx - mn).length
cam = bpy.data.objects.new('c', bpy.data.cameras.new('c')); sc.collection.objects.link(cam); sc.camera = cam; cam.data.lens = 50
key = bpy.data.objects.new('key', bpy.data.lights.new('key', 'AREA')); key.data.energy = 60; key.data.size = 0.6; sc.collection.objects.link(key)
key.location = c + Vector((0.3, 0.4, 0.6)); key.rotation_euler = (c - key.location).to_track_quat('-Z', 'Y').to_euler()
fill = bpy.data.objects.new('fill', bpy.data.lights.new('fill', 'AREA')); fill.data.energy = 20; fill.data.size = 0.8; sc.collection.objects.link(fill)
fill.location = c + Vector((-0.5, 0.2, 0.2)); fill.rotation_euler = (c - fill.location).to_track_quat('-Z', 'Y').to_euler()
# player's view (behind, above, slightly right), left side, right side, front 3/4, under
views = [(-60, 25, 'jogador'), (180, 8, 'esquerda'), (0, 8, 'direita'), (140, 30, 'frente'), (-120, -35, 'baixo')]
for k, (az, el, nm) in enumerate(views):
    a = math.radians(az); e = math.radians(el)
    cam.location = c + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * r * 1.7
    cam.rotation_euler = (c - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = '%s_%d.png' % (out, k); bpy.ops.render.render(write_still=True)
