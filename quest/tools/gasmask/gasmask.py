# Gas mask seen from inside (Dark Forces gas mask, head-locked in VR). Subtle by design: a panoramic
# single visor (modern full-face masks such as the M50 / M53 family) whose rubber seal and lens ring
# only show at the far edge of the view; at the bottom centre a hint of the nose cup / filter port.
# Built in the eye frame (Blender: x right, y forward, z up, origin between the eyes) and written as
# gasmask.bin for the app (positions, normals, colors, triangles) plus gasmask.blend.
#   blender -b -P gasmask.py -- <out dir>
import bpy, bmesh, sys, math, struct, os
from mathutils import Vector
out = sys.argv[sys.argv.index('--') + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)

# Lens opening, in angles seen from the eye on that side (each half of the visor is shaped around its
# own eye, so the close frame does not creep into the view through parallax): wide, so the frame only
# shows at the very edge of the headset's field of view (Quest 3: about 110 x 96 degrees).
H, U, D = 51.0, 41.0, 45.0      # temporal half width, up, down
EXP = 3.2                       # superellipse exponent (rounded rectangle)
N = 200                         # samples around the outline
EYE = 0.032                     # half the distance between the eyes

def dirv(az, el):
    az, el = math.radians(az), math.radians(el)
    return Vector((math.sin(az) * math.cos(el), math.cos(az) * math.cos(el), math.sin(el)))

def outline_point(t, extra, r):
    """Frame point: t in [0, 1) around the opening, 'extra' degrees outward from the lens edge,
    at distance r from the eye of that side (the two halves blend across the nose)."""
    a = t * 2 * math.pi
    c, s = math.cos(a), math.sin(a)
    x = math.copysign(abs(c) ** (2 / EXP), c)
    y = math.copysign(abs(s) ** (2 / EXP), s)
    side = math.tanh(x * 5.0)                 # -1 left eye .. +1 right eye, smooth over the nose
    ex = side * EYE
    az = x * H                                # temporal angle from that eye
    el = y * (U if y > 0 else D)
    if y < 0: el += 3.0 * math.exp(-(x / 0.18) ** 2) * (-y)   # the lower edge rises over the nose cup
    m = math.hypot(x, y) or 1.0
    az += (x / m) * extra; el += (y / m) * extra
    return Vector((ex, 0.0, 0.0)) + dirv(az, el) * r

# Cross-section of the frame from the lens edge outward: (extra angle deg, distance m, part).
# part 0 = lens ring (glossy), 1 = rubber seal, 2 = rubber toward the face.
PROF = [(0.0, 0.046, 0), (1.5, 0.0455, 0), (2.3, 0.0450, 1), (7.0, 0.0440, 1), (14.0, 0.0410, 1),
        (22.0, 0.0365, 2), (32.0, 0.0310, 2), (44.0, 0.0260, 2)]
COL = {0: (0.24, 0.25, 0.27), 1: (0.11, 0.115, 0.12), 2: (0.085, 0.088, 0.092)}

bm = bmesh.new()
cl = bm.loops.layers.color.new('col')
rings = []
for i in range(N):
    ring = []
    for (da, r, part) in PROF:
        ring.append((bm.verts.new(outline_point(i / N, da, r)), part))
    rings.append(ring)
for i in range(N):
    a, b = rings[i], rings[(i + 1) % N]
    for k in range(len(PROF) - 1):
        f = bm.faces.new((a[k][0], a[k + 1][0], b[k + 1][0], b[k][0]))
        part = a[k][1]
        for lp in f.loops: lp[cl] = (*COL[part], 1.0)

# Nose cup / filter port: a small soft dome just under the lens, at the very bottom centre.
cx = dirv(0.0, -62.0) * 0.040
dome = bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=0.011)
for v in dome['verts']:
    v.co = Vector((v.co.x * 1.4, v.co.y * 0.8, v.co.z * 0.9)) + cx
for f in {f for v in dome['verts'] for f in v.link_faces}:
    for lp in f.loops: lp[cl] = (0.10, 0.105, 0.11, 1.0)
# Two small rivets on the seal at the temples (where the head harness straps attach).
for sd in (-1, 1):
    c = Vector((sd * EYE, 0, 0)) + dirv(sd * (H + 10.0), 3.0) * 0.0435
    r = bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=8, radius=0.0025)
    for v in r['verts']: v.co = v.co + c
    for f in {f for v in r['verts'] for f in v.link_faces}:
        for lp in f.loops: lp[cl] = (0.20, 0.21, 0.23, 1.0)

bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
# The faces must face the eye (we look at the inside of the mask).
for f in bm.faces:
    cm = f.calc_center_median(); eye = Vector((math.copysign(EYE, cm.x) if abs(cm.x) > 0.01 else 0.0, 0, 0))
    if f.normal.dot(eye - cm) < 0: f.normal_flip()
me = bpy.data.meshes.new('gasmask'); bm.to_mesh(me); bm.free()
ob = bpy.data.objects.new('gasmask', me); bpy.context.scene.collection.objects.link(ob)
bpy.context.view_layer.objects.active = ob; ob.select_set(True)
sub = ob.modifiers.new('sub', 'SUBSURF'); sub.levels = 1; sub.render_levels = 1
bpy.ops.object.modifier_apply(modifier='sub')
bpy.ops.object.shade_smooth()
mat = bpy.data.materials.new('rubber'); mat.use_nodes = True
nt = mat.node_tree; att = nt.nodes.new('ShaderNodeVertexColor'); att.layer_name = 'col'
nt.links.new(att.outputs['Color'], nt.nodes['Principled BSDF'].inputs['Base Color'])
nt.nodes['Principled BSDF'].inputs['Roughness'].default_value = 0.7
me.materials.append(mat)
os.makedirs(out, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, 'gasmask.blend'))

# Export: game frame (x right, y up, z back): game = (bx, bz, -by).
me = ob.data; me.calc_loop_triangles()
col = me.color_attributes['col']
verts, idx = [], []
for tri in me.loop_triangles:
    for li in tri.loops:
        l = me.loops[li]; v = me.vertices[l.vertex_index]
        n = l.normal if hasattr(l, 'normal') else v.normal
        cv = col.data[li] if col.domain == 'CORNER' else col.data[l.vertex_index]
        c = cv.color_srgb   # the values as authored (the byte attribute stores sRGB; .color would be linear)
        verts.append((v.co.x, v.co.z, -v.co.y, n.x, n.z, -n.y, c[0], c[1], c[2]))
        idx.append(len(verts) - 1)
with open(os.path.join(out, 'gasmask.bin'), 'wb') as f:
    f.write(b'GMSK'); f.write(struct.pack('<ii', len(verts), len(idx)))
    for v in verts: f.write(struct.pack('<9f', *v))
    f.write(struct.pack('<%dI' % len(idx), *idx))
print('gasmask verts', len(verts), 'tris', len(idx) // 3)
