# Renders a finished weapon (<prefix>.npz + <prefix>.png) from the player's viewpoints.
#   blender -b -P review.py -- <prefix> <out prefix> [file.blend]
import bpy, sys, os, math
import numpy as np
from mathutils import Vector
argv = sys.argv[sys.argv.index('--') + 1:]
pre, out = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.samples = 32; sc.cycles.device = 'CPU'
sc.render.resolution_x = 720; sc.render.resolution_y = 460
w = bpy.data.worlds.new('w'); w.use_nodes = True
bg = w.node_tree.nodes['Background']; bg.inputs[0].default_value = (0.20, 0.21, 0.24, 1); bg.inputs[1].default_value = 1.0; sc.world = w
V = np.load(pre + '.npz')['V']
def B(p): return (p[0], -p[2], p[1])
n = len(V) // 3
me = bpy.data.meshes.new('w'); me.from_pydata([B(v) for v in V], [], [(3*i, 3*i+1, 3*i+2) for i in range(n)]); me.update()
ob = bpy.data.objects.new('weapon', me); sc.collection.objects.link(ob)
me.uv_layers.new(name='UV'); uv = me.uv_layers['UV']
for i, l in enumerate(me.loops): uv.data[i].uv = (V[l.vertex_index][6], 1 - V[l.vertex_index][7])
for p in me.polygons: p.use_smooth = True
me.normals_split_custom_set([tuple(B(V[l.vertex_index][3:6])) for l in me.loops])
m = bpy.data.materials.new('t'); m.use_nodes = True; nt = m.node_tree; b = nt.nodes['Principled BSDF']
tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = bpy.data.images.load(pre + '.png'); tn.image.alpha_mode = 'CHANNEL_PACKED'
nt.links.new(tn.outputs['Color'], b.inputs['Base Color'])
inv = nt.nodes.new('ShaderNodeMath'); inv.operation = 'SUBTRACT'; inv.inputs[0].default_value = 1.0
nt.links.new(tn.outputs['Alpha'], inv.inputs[1]); nt.links.new(inv.outputs[0], b.inputs['Roughness'])
b.inputs['Metallic'].default_value = 0.5
me.materials.append(m)
if len(argv) > 2:
    bpy.ops.file.pack_all(); bpy.ops.wm.save_as_mainfile(filepath=argv[2]); sys.exit(0)
vs = [Vector(B(v)) for v in V]
mn = Vector([min(v[i] for v in vs) for i in range(3)]); mx = Vector([max(v[i] for v in vs) for i in range(3)])
c = (mn + mx) / 2; r = (mx - mn).length
cam = bpy.data.objects.new('c', bpy.data.cameras.new('c')); sc.collection.objects.link(cam); sc.camera = cam; cam.data.lens = 50
for nm, loc, e in (('key', (0.3, 0.4, 0.6), 50), ('fill', (-0.5, 0.2, 0.2), 15), ('rim', (0.0, -0.6, 0.3), 25)):
    L = bpy.data.objects.new(nm, bpy.data.lights.new(nm, 'AREA')); L.data.energy = e * (r / 0.3) ** 2; L.data.size = 0.5 * r / 0.3
    sc.collection.objects.link(L); L.location = c + Vector(loc) * (r / 0.3); L.rotation_euler = (c - L.location).to_track_quat('-Z', 'Y').to_euler()
views = eval(os.environ.get('RV_VIEWS', '[(-60, 25), (180, 8), (0, 8), (140, 30), (-120, -35)]'))
zoom = float(os.environ.get('RV_ZOOM', '1.6'))
for k, (az, el) in enumerate(views):
    a = math.radians(az); e = math.radians(el)
    cam.location = c + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * r * zoom
    cam.rotation_euler = (c - cam.location).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = '%s_%d.png' % (out, k); bpy.ops.render.render(write_still=True)
