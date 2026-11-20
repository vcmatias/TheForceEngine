# Renders a weapon with the fitted hand (OBJ in the kit frame) around the grip.
#   blender -b -P handview.py -- <weapon prefix> <hand.obj> <out prefix> [samples]
import bpy, sys, math
import numpy as np
from mathutils import Vector
argv = sys.argv[sys.argv.index('--') + 1:]
pre, obj, out = argv[0], argv[1], argv[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = int(argv[3]) if len(argv) > 3 else 16; sc.cycles.device = 'CPU'
sc.render.resolution_x = 640; sc.render.resolution_y = 480
w = bpy.data.worlds.new('w'); w.use_nodes = True; w.node_tree.nodes['Background'].inputs[0].default_value = (0.22, 0.23, 0.26, 1); sc.world = w
def B(p): return (p[0], -p[2], p[1])
V = np.load(pre + '.npz')['V']; n = len(V) // 3
me = bpy.data.meshes.new('w'); me.from_pydata([B(v) for v in V], [], [(3*i, 3*i+1, 3*i+2) for i in range(n)]); me.update()
ob = bpy.data.objects.new('weapon', me); sc.collection.objects.link(ob)
me.uv_layers.new(name='UV'); uv = me.uv_layers['UV']
for i, l in enumerate(me.loops): uv.data[i].uv = (V[l.vertex_index][6], 1 - V[l.vertex_index][7])
for p in me.polygons: p.use_smooth = True
m = bpy.data.materials.new('t'); m.use_nodes = True; nt = m.node_tree; b = nt.nodes['Principled BSDF']
tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = bpy.data.images.load(pre + '.png'); nt.links.new(tn.outputs['Color'], b.inputs['Base Color'])
b.inputs['Roughness'].default_value = 0.45; me.materials.append(m)
vs, fs = [], []
for line in open(obj):
    t = line.split()
    if not t: continue
    if t[0] == 'v': vs.append(B([float(x) for x in t[1:4]]))
    elif t[0] == 'f': fs.append(tuple(int(x) - 1 for x in t[1:4]))
hm = bpy.data.meshes.new('h'); hm.from_pydata(vs, [], fs); hm.update()
for p in hm.polygons: p.use_smooth = True
ho = bpy.data.objects.new('hand', hm); sc.collection.objects.link(ho)
gm = bpy.data.materials.new('g'); gm.use_nodes = True; gb = gm.node_tree.nodes['Principled BSDF']
gb.inputs['Base Color'].default_value = (0.55, 0.12, 0.10, 1); gb.inputs['Roughness'].default_value = 0.5; hm.materials.append(gm)
c = Vector(np.mean(np.array(vs), 0)); r = 0.22
cam = bpy.data.objects.new('c', bpy.data.cameras.new('c')); sc.collection.objects.link(cam); sc.camera = cam; cam.data.lens = 50
for nm, loc, e in (('key', (0.3, 0.4, 0.6), 30), ('fill', (-0.5, 0.2, 0.2), 12), ('rim', (0.0, -0.6, 0.3), 15), ('low', (0.1, 0.2, -0.6), 10)):
    L = bpy.data.objects.new(nm, bpy.data.lights.new(nm, 'AREA')); L.data.energy = e; L.data.size = 0.4
    sc.collection.objects.link(L); L.location = c + Vector(loc); L.rotation_euler = (c - L.location).to_track_quat('-Z', 'Y').to_euler()
import os
views = eval(os.environ.get('HV_VIEWS', '[(0, 5), (180, 5), (-90, 10), (90, 20), (-40, -50), (200, 55)]'))
for k, (az, el) in enumerate(views):
    a = math.radians(az); e = math.radians(el)
    cam.location = c + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * r * 1.6
    cam.rotation_euler = (c - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = '%s_%d.png' % (out, k); bpy.ops.render.render(write_still=True)
