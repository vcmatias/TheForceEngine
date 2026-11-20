# Blender (headless):  blender -b -P blender_finish.py -- <workdir> <weapon id>
# Adds the parts the DFmod first-person models never had, following the Dark Forces / Star Wars
# descriptions (Imperial repeater: drum magazine and slender stock from the grip; Packered mortar: slender
# stock ending in a flat plate and sling hooks; Jeron fusion cutter: handle under the body). Parts are
# beveled, unwrapped and textured by baking Blender procedural materials (color x ambient occlusion)
# into w<id>_add.png; geometry goes to w<id>_add.npz (weapon frame, meters, per-corner normals).
import bpy, sys, os, math
import numpy as np
from mathutils import Vector, Matrix
argv = sys.argv[sys.argv.index('--') + 1:]
wd, wid = argv[0], int(argv[1])
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 64
def B(p): return Vector((p[0], -p[2], p[1]))          # weapon frame -> Blender (Z up)
# ---------------------------------------------------------------- materials
def material(kind):
    m = bpy.data.materials.new(kind); m.use_nodes = True
    nt = m.node_tree; b = nt.nodes['Principled BSDF']
    tc = nt.nodes.new('ShaderNodeTexCoord')
    noise = nt.nodes.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value = 180.0; noise.inputs['Detail'].default_value = 8
    nt.links.new(tc.outputs['Object'], noise.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    nt.links.new(noise.outputs['Fac'], ramp.inputs['Fac'])
    if kind == 'gun':
        ramp.color_ramp.elements[0].color = (0.035, 0.038, 0.045, 1); ramp.color_ramp.elements[1].color = (0.09, 0.095, 0.11, 1)
    elif kind == 'steel':
        ramp.color_ramp.elements[0].color = (0.16, 0.17, 0.19, 1); ramp.color_ramp.elements[1].color = (0.30, 0.31, 0.34, 1)
    elif kind == 'chrome':
        ramp.color_ramp.elements[0].color = (0.45, 0.47, 0.50, 1); ramp.color_ramp.elements[1].color = (0.70, 0.72, 0.75, 1)
    elif kind == 'wood':
        wave = nt.nodes.new('ShaderNodeTexWave'); wave.inputs['Scale'].default_value = 60.0; wave.inputs['Distortion'].default_value = 6.0
        nt.links.new(tc.outputs['Object'], wave.inputs['Vector'])
        nt.links.new(wave.outputs['Fac'], ramp.inputs['Fac'])
        ramp.color_ramp.elements[0].color = (0.10, 0.045, 0.02, 1); ramp.color_ramp.elements[1].color = (0.24, 0.12, 0.05, 1)
    elif kind == 'rubber':
        ramp.color_ramp.elements[0].color = (0.02, 0.02, 0.022, 1); ramp.color_ramp.elements[1].color = (0.05, 0.05, 0.055, 1)
    elif kind == 'brass':
        ramp.color_ramp.elements[0].color = (0.35, 0.24, 0.07, 1); ramp.color_ramp.elements[1].color = (0.62, 0.45, 0.15, 1)
    nt.links.new(ramp.outputs['Color'], b.inputs['Base Color'])
    return m
MATS = {}
def mat(kind):
    if kind not in MATS: MATS[kind] = material(kind)
    return MATS[kind]
# ---------------------------------------------------------------- parts
parts = []
def finish(ob, kind, bevel=0.0015):
    ob.data.materials.append(mat(kind))
    if bevel > 0:
        m = ob.modifiers.new('bev', 'BEVEL'); m.width = bevel; m.segments = 2; m.limit_method = 'ANGLE'
    parts.append(ob)
    return ob
def cyl(a, b, r, kind, verts=32, bevel=0.0012):
    a = B(a); b = B(b); d = b - a
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d.length, location=(a + b) / 2)
    ob = bpy.context.object
    ob.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    return finish(ob, kind, bevel)
def box(c, size, kind, rot=(0, 0, 0), bevel=0.0015):
    bpy.ops.mesh.primitive_cube_add(size=1, location=B(c))
    ob = bpy.context.object
    ob.scale = (size[0], size[2], size[1])           # weapon (x, y, z) sizes -> Blender (x, z->-y, y->z)
    ob.rotation_euler = (rot[0], rot[2], rot[1])
    bpy.ops.object.transform_apply(scale=True)
    return finish(ob, kind, bevel)
def torus(c, axis, R, r, kind):
    bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=r, major_segments=32, minor_segments=10, location=B(c))
    ob = bpy.context.object; ob.rotation_euler = B(axis).to_track_quat('Z', 'Y').to_euler()
    return finish(ob, kind, 0)
if wid == 4:
    # Imperial repeater: drum magazine on the side the view model never showed, slender stock from the
    # bottom of the grip ending in a butt plate.
    cyl((-0.040, 0.075, -0.115), (-0.082, 0.075, -0.115), 0.042, 'gun', 48)
    cyl((-0.084, 0.075, -0.115), (-0.088, 0.075, -0.115), 0.030, 'steel', 48)
    for i in range(6):
        a = i * math.pi / 3
        cyl((-0.086, 0.075 + math.sin(a) * 0.022, -0.115 + math.cos(a) * 0.022), (-0.090, 0.075 + math.sin(a) * 0.022, -0.115 + math.cos(a) * 0.022), 0.004, 'brass', 12, 0)
    cyl((0.0, -0.045, 0.010), (0.0, -0.010, 0.210), 0.0065, 'steel', 20)
    cyl((0.0, 0.020, 0.010), (0.0, -0.002, 0.205), 0.0055, 'steel', 20)
    box((0.0, -0.005, 0.215), (0.030, 0.095, 0.014), 'gun')
    box((0.0, -0.005, 0.224), (0.026, 0.088, 0.004), 'rubber', bevel=0.001)
elif wid == 6:
    # Packered mortar gun: slender stock ending in a flat plate, sling hooks behind the shroud.
    cyl((0.0, 0.055, 0.070), (0.0, 0.035, 0.300), 0.0085, 'gun', 24)
    box((0.0, 0.035, 0.305), (0.050, 0.120, 0.014), 'gun')
    box((0.0, 0.035, 0.314), (0.044, 0.112, 0.004), 'rubber', bevel=0.001)
    torus((0.0, 0.158, -0.040), (1, 0, 0), 0.010, 0.0025, 'steel')
    torus((0.0, -0.030, -0.040), (1, 0, 0), 0.010, 0.0025, 'steel')
elif wid == 5:
    # Jeron fusion cutter: handle under the body.
    box((0.0, -0.055, 0.008), (0.030, 0.095, 0.040), 'gun', rot=(-0.30, 0, 0))
    box((0.0, -0.060, 0.010), (0.032, 0.070, 0.030), 'rubber', rot=(-0.30, 0, 0), bevel=0.003)
    box((0.0, -0.015, -0.020), (0.006, 0.030, 0.004), 'steel', rot=(0.25, 0, 0), bevel=0.001)
if not parts:
    np.savez(os.path.join(wd, 'w%d_add.npz' % wid), V=np.zeros((0, 8), np.float32)); print('WEAPON', wid, 'no parts'); sys.exit(0)
# ---------------------------------------------------------------- join, unwrap, bake color x AO
for o in sc.objects: o.select_set(False)
for p in parts:
    p.select_set(True); bpy.context.view_layer.objects.active = p
    for m in list(p.modifiers): bpy.ops.object.modifier_apply(modifier=m.name)
for p in parts: p.select_set(True)
bpy.ops.object.join()
ob = bpy.context.object
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.quads_convert_to_tris()
bpy.ops.uv.smart_project(angle_limit=1.15, island_margin=0.01)
bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.object.shade_smooth_by_angle(angle=0.6)
S = 1024
def bake(kind):
    img = bpy.data.images.new(kind, S, S, alpha=False)
    for m in ob.data.materials:
        nt = m.node_tree
        n = nt.nodes.get('bakeimg') or nt.nodes.new('ShaderNodeTexImage'); n.name = 'bakeimg'; n.image = img; nt.nodes.active = n
    if kind == 'col': bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'}, margin=4)
    else: bpy.ops.object.bake(type='AO', margin=4)
    return np.array(img.pixels[:]).reshape(S, S, 4)
col = bake('col'); ao = bake('ao')
out = col.copy(); out[..., :3] = col[..., :3] * (0.35 + 0.65 * ao[..., :3])
img = bpy.data.images.new('final', S, S, alpha=False); img.pixels[:] = out.reshape(-1)
img.filepath_raw = os.path.join(wd, 'w%d_add.png' % wid); img.file_format = 'PNG'; img.save()
me = ob.data; me.calc_loop_triangles(); uv = me.uv_layers.active.data
V = []
for t in me.loop_triangles:
    for li in t.loops:
        l = me.loops[li]; p = me.vertices[l.vertex_index].co; n = l.normal; u = uv[li].uv
        V.append((p[0], p[2], -p[1], n[0], n[2], -n[1], u[0], 1.0 - u[1]))
np.savez(os.path.join(wd, 'w%d_add.npz' % wid), V=np.array(V, np.float32))
print('WEAPON', wid, 'added tris', len(V) // 3)
