# Hard-surface modeling kit for the weapons (Blender 4.2, headless). Units: meters, weapon frame
# (x right, y up, forward -z, grip point at the origin) converted to Blender (Z up) by W().
import bpy, bmesh, math, os
import numpy as np
from mathutils import Vector, Matrix, Euler
def W(p): return Vector((p[0], -p[2], p[1]))
def WD(d): return Vector((d[0], -d[2], d[1]))
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'
    return sc
NOTRIG = bool(os.environ.get('NOTRIG'))   # geometry without the trigger group (hand fitting)
FAST = bool(os.environ.get('FAST'))       # no bakes: geometry only
PARTS = []
def _link(ob, mat, bevel, seg, angle, smooth=True):
    if mat: ob.data.materials.append(MATS[mat] if isinstance(mat, str) else mat)
    if bevel:
        m = ob.modifiers.new('bev', 'BEVEL'); m.width = bevel; m.segments = seg; m.limit_method = 'ANGLE'; m.angle_limit = angle
        m.harden_normals = False
    if smooth:
        for p in ob.data.polygons: p.use_smooth = True
    PARTS.append(ob); return ob
def box(center, size, mat, rot=(0, 0, 0), bevel=0.0012, seg=3, angle=0.6, name='box'):
    """center/size in weapon frame (size = full x, y, z extents); rot = (about x, about y, about z) in weapon frame"""
    bpy.ops.mesh.primitive_cube_add(size=1, location=W(center))
    ob = bpy.context.object; ob.name = name
    ob.scale = (size[0], size[2], size[1])
    ob.rotation_euler = Euler((rot[0], -rot[2], rot[1]), 'XYZ') if any(rot) else ob.rotation_euler
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return _link(ob, mat, bevel, seg, angle)
def cyl(a, b, r, mat, verts=48, bevel=0.0008, seg=2, angle=0.6, r2=None, name='cyl'):
    a = W(a); b = W(b); d = b - a
    if r2 is None:
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d.length, location=(a + b) / 2)
    else:
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=r2, depth=d.length, location=(a + b) / 2)
    ob = bpy.context.object; ob.name = name
    ob.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    return _link(ob, mat, bevel, seg, angle)
def tube(a, b, r_out, r_in, mat, verts=48, bevel=0.0006, name='tube'):
    ob = cyl(a, b, r_out, mat, verts, bevel=0, name=name)
    inner = cyl(a, b, r_in, None, verts, bevel=0, name='cut'); PARTS.remove(inner)
    a3 = W(a); b3 = W(b); d = (b3 - a3).normalized()
    inner.location = inner.location; inner.scale = (1, 1, 1.02)
    cut(ob, inner)
    if bevel:
        m = ob.modifiers.new('bev', 'BEVEL'); m.width = bevel; m.segments = 2; m.limit_method = 'ANGLE'
    return ob
def sphere(c, r, mat, seg=64, rings=32, name='sphere'):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=rings, radius=r, location=W(c))
    ob = bpy.context.object; ob.name = name
    return _link(ob, mat, 0, 0, 0)
def torus(c, axis, R, r, mat, maj=48, mn=12, name='torus'):
    bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=r, major_segments=maj, minor_segments=mn, location=W(c))
    ob = bpy.context.object; ob.name = name; ob.rotation_euler = WD(axis).to_track_quat('Z', 'Y').to_euler()
    return _link(ob, mat, 0, 0, 0)
def helix(a, b, R, wire, turns, mat, name='coil'):
    """coil of `turns` around the axis a->b (radius R, wire radius `wire`)"""
    a3 = W(a); b3 = W(b); d = b3 - a3; L = d.length
    cu = bpy.data.curves.new(name, 'CURVE'); cu.dimensions = '3D'; cu.bevel_depth = wire; cu.bevel_resolution = 4
    sp = cu.splines.new('POLY'); n = int(turns * 48) + 1; sp.points.add(n - 1)
    for i in range(n):
        t = i / (n - 1); ang = t * turns * 2 * math.pi
        sp.points[i].co = (R * math.cos(ang), R * math.sin(ang), (t - 0.5) * L, 1)
    ob = bpy.data.objects.new(name, cu); bpy.context.scene.collection.objects.link(ob)
    ob.location = (a3 + b3) / 2; ob.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    bpy.context.view_layer.objects.active = ob; ob.select_set(True)
    bpy.ops.object.convert(target='MESH')
    ob = bpy.context.object
    return _link(ob, mat, 0, 0, 0)
def cut(target, cutter, solver='EXACT'):
    m = target.modifiers.new('cut%d' % len(CUTTERS), 'BOOLEAN'); m.operation = 'DIFFERENCE'; m.object = cutter; m.solver = solver
    cutter.hide_render = True
    CUTTERS.append(cutter)
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_move_to_index(modifier=m.name, index=0)   # booleans before the bevel
    return m
CUTTERS = []
def cutter_box(center, size, rot=(0, 0, 0)):
    ob = box(center, size, None, rot, bevel=0); PARTS.remove(ob); return ob
def cutter_cyl(a, b, r, verts=32):
    ob = cyl(a, b, r, None, verts, bevel=0); PARTS.remove(ob); return ob

# ---------------------------------------------------------------- standard grip (the right hand is fitted on it)
# Every pistol-grip weapon uses the same handle, so one fitted hand pose (hosttest/gripfit.cpp) holds them all.
GRIP_C, GRIP_S, GRIP_T = (0.0, -0.024, 0.018), (0.024, 0.100, 0.030), -0.30   # 2 cm below the bodies: the wrist passes under them
def std_grip(mat='black', panels='rubber', grooves=False):
    gr = box(GRIP_C, GRIP_S, mat, rot=(GRIP_T, 0, 0), bevel=0.005, seg=4)
    if grooves:
        for i in range(4):
            yy = -0.040 + i * 0.018
            cut(gr, cutter_box((0, yy, GRIP_C[2] - 0.004 + yy * 0.30 - 0.019), (0.034, 0.005, 0.006), rot=(GRIP_T, 0, 0)))
    box((0, GRIP_C[1] + 0.059, GRIP_C[2] + 0.004), (0.024, 0.026, 0.040), mat, bevel=0.003)   # tang up to the body
    if panels:
        for sd in (-1, 1): box((sd * 0.0115, GRIP_C[1] - 0.004, GRIP_C[2] + 0.001), (0.0025, 0.072, 0.023), panels, rot=(GRIP_T, 0, 0), bevel=0.0012)
    return gr
# Trigger group placed on the fitted index: the pad rests on the blade, the guard clears the finger.
TRIG_Z, TRIG_Y0, TRIG_TOP, GUARD_Y, GUARD_Z, GUARD_END = -0.038, -0.036, -0.002, -0.044, -0.060, -0.026
def std_trigger(mat='black', top=0.018):
    """top = bottom of the weapon's housing above the trigger; a lower receiver fills down to TRIG_TOP"""
    if top > TRIG_TOP + 0.001:
        box((0, (TRIG_TOP + top) / 2 + 0.001, (GUARD_Z - 0.004 + 0.010) / 2), (0.022, top - TRIG_TOP + 0.002, 0.014 - GUARD_Z), mat, bevel=0.002)
        top = TRIG_TOP
    if NOTRIG: return
    # open guard (no rear link): the middle finger wraps the grip right under it
    box((0, GUARD_Y, (GUARD_Z + GUARD_END) / 2), (0.007, 0.004, GUARD_END - GUARD_Z), mat, bevel=0.0012)
    box((0, (GUARD_Y + top) / 2, GUARD_Z), (0.007, top - GUARD_Y, 0.005), mat, bevel=0.0012)
    box((0, (TRIG_Y0 + top) / 2, TRIG_Z), (0.004, top - TRIG_Y0, 0.0045), 'steel', rot=(-0.15, 0, 0), bevel=0.0012)
# ---------------------------------------------------------------- materials (Cycles, baked later)
MATS = {}
def _pbr(name, base, rough, metal, wear=None, wear_amt=0.0, noise_scale=400.0, knurl=False, grain=False):
    # colors are given as sRGB (as picked by eye) and converted to linear for Cycles
    base = tuple(c ** 2.2 for c in base)
    if wear is not None: wear = tuple(c ** 2.2 for c in wear)
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; L = nt.links
    b = nt.nodes['Principled BSDF']
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nz = nt.nodes.new('ShaderNodeTexNoise'); nz.inputs['Scale'].default_value = noise_scale; nz.inputs['Detail'].default_value = 10
    L.new(tc.outputs['Object'], nz.inputs['Vector'])
    mix = nt.nodes.new('ShaderNodeMix'); mix.data_type = 'RGBA'
    mix.inputs['Factor'].default_value = 0.15
    mix.inputs[6].default_value = (*base, 1); mix.inputs[7].default_value = (base[0] * 1.35 + 0.002, base[1] * 1.35 + 0.002, base[2] * 1.35 + 0.0025, 1)
    nmul = nt.nodes.new('ShaderNodeMath'); nmul.operation = 'MULTIPLY'; nmul.inputs[1].default_value = 0.45
    L.new(nz.outputs['Fac'], nmul.inputs[0]); L.new(nmul.outputs[0], mix.inputs['Factor'])   # subtle mottling
    col = mix.outputs[2]
    if grain:
        wv = nt.nodes.new('ShaderNodeTexWave'); wv.inputs['Scale'].default_value = 420; wv.inputs['Distortion'].default_value = 5; wv.inputs['Detail'].default_value = 6
        L.new(tc.outputs['Object'], wv.inputs['Vector'])
        m2 = nt.nodes.new('ShaderNodeMix'); m2.data_type = 'RGBA'; m2.inputs[6].default_value = (*base, 1)
        m2.inputs[7].default_value = (base[0] * 0.55, base[1] * 0.5, base[2] * 0.45, 1)
        L.new(wv.outputs['Fac'], m2.inputs['Factor']); col = m2.outputs[2]
    if knurl:
        vo = nt.nodes.new('ShaderNodeTexVoronoi'); vo.inputs['Scale'].default_value = 1400
        L.new(tc.outputs['Object'], vo.inputs['Vector'])
        m3 = nt.nodes.new('ShaderNodeMix'); m3.data_type = 'RGBA'; L.new(col, m3.inputs[6])
        m3.inputs[7].default_value = (base[0] * 2.2 + 0.02, base[1] * 2.2 + 0.02, base[2] * 2.2 + 0.02, 1)
        L.new(vo.outputs['Distance'], m3.inputs['Factor']); col = m3.outputs[2]
    if wear is not None and wear_amt > 0:
        # Edge wear: convex edges (pointiness) show the bare metal underneath.
        geo = nt.nodes.new('ShaderNodeNewGeometry')
        ramp = nt.nodes.new('ShaderNodeValToRGB'); ramp.color_ramp.elements[0].position = 0.57; ramp.color_ramp.elements[1].position = 0.68
        L.new(geo.outputs['Pointiness'], ramp.inputs['Fac'])
        nz2 = nt.nodes.new('ShaderNodeTexNoise'); nz2.inputs['Scale'].default_value = 120
        L.new(tc.outputs['Object'], nz2.inputs['Vector'])
        mul = nt.nodes.new('ShaderNodeMath'); mul.operation = 'MULTIPLY'
        L.new(ramp.outputs['Color'], mul.inputs[0]); L.new(nz2.outputs['Fac'], mul.inputs[1])
        mul2 = nt.nodes.new('ShaderNodeMath'); mul2.operation = 'MULTIPLY'; mul2.inputs[1].default_value = wear_amt * 1.2
        L.new(mul.outputs[0], mul2.inputs[0])
        m4 = nt.nodes.new('ShaderNodeMix'); m4.data_type = 'RGBA'; m4.clamp_factor = True
        L.new(col, m4.inputs[6]); m4.inputs[7].default_value = (*wear, 1); L.new(mul2.outputs[0], m4.inputs['Factor'])
        col = m4.outputs[2]
    bake_col = col
    if name == 'chrome':
        # Fake studio reflection baked into the albedo: the game shader has no environment map, so a
        # plain bright chrome would read as white plastic. Dark ground, bright horizon band, mid sky.
        geo2 = nt.nodes.new('ShaderNodeNewGeometry')
        sep = nt.nodes.new('ShaderNodeSeparateXYZ'); L.new(geo2.outputs['Normal'], sep.inputs[0])
        mr = nt.nodes.new('ShaderNodeMapRange'); mr.inputs['From Min'].default_value = -1; mr.inputs['From Max'].default_value = 1
        L.new(sep.outputs['Z'], mr.inputs['Value'])
        rp = nt.nodes.new('ShaderNodeValToRGB'); E = rp.color_ramp.elements
        stops = [(0.0, 0.10), (0.40, 0.22), (0.50, 0.62), (0.56, 0.95), (0.64, 0.46), (1.0, 0.52)]
        E[0].position, E[0].color = stops[0][0], (stops[0][1] ** 2.2,) * 3 + (1,)
        E[1].position, E[1].color = stops[-1][0], (stops[-1][1] ** 2.2,) * 3 + (1,)
        for p, v in stops[1:-1]:
            e = E.new(p); e.color = (v ** 2.2, v ** 2.2 * 1.01, v ** 2.2 * 1.04, 1)
        L.new(mr.outputs['Result'], rp.inputs['Fac'])
        bake_col = rp.outputs['Color']
    L.new(col, b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = rough; b.inputs['Metallic'].default_value = metal
    m['rough'] = rough
    # Bake helpers: emission shaders carrying the albedo and the gloss, swapped in for the bakes
    em = nt.nodes.new('ShaderNodeEmission'); em.name = 'emcol'; L.new(bake_col, em.inputs['Color'])
    eg = nt.nodes.new('ShaderNodeEmission'); eg.name = 'emgloss'; eg.inputs['Color'].default_value = (1 - rough, 1 - rough, 1 - rough, 1)
    MATS[name] = m; return m
def standard_materials():
    # sRGB colors, picked to read like the Dark Forces renders under the game's lighting
    _pbr('black', (0.075, 0.078, 0.088), 0.32, 0.6, wear=(0.42, 0.43, 0.46), wear_amt=0.7)
    _pbr('blackplain', (0.075, 0.078, 0.088), 0.32, 0.6, wear=(0.42, 0.43, 0.46), wear_amt=0.12)   # many small edges (perforations)
    _pbr('gunmetal', (0.17, 0.18, 0.20), 0.35, 0.8, wear=(0.50, 0.51, 0.54), wear_amt=0.5)
    _pbr('steel', (0.42, 0.43, 0.46), 0.28, 1.0, wear=(0.70, 0.70, 0.72), wear_amt=0.3)
    _pbr('chrome', (0.80, 0.81, 0.84), 0.12, 1.0)
    _pbr('brass', (0.72, 0.52, 0.20), 0.22, 1.0, wear=(0.90, 0.78, 0.50), wear_amt=0.4)
    _pbr('copper', (0.70, 0.40, 0.25), 0.3, 1.0)
    _pbr('rubber', (0.085, 0.085, 0.09), 0.85, 0.0, knurl=True)
    _pbr('wood', (0.33, 0.17, 0.075), 0.5, 0.0, grain=True)
    _pbr('olive', (0.28, 0.30, 0.24), 0.5, 0.2, wear=(0.5, 0.5, 0.5), wear_amt=0.4)
    _pbr('hazard', (0.95, 0.50, 0.02), 0.45, 0.1)
    _pbr('red', (0.70, 0.10, 0.07), 0.4, 0.0)
    _pbr('lens', (0.15, 0.45, 0.55), 0.05, 0.0)
    _pbr('glowred', (1.0, 0.12, 0.06), 0.3, 0.0)
    _pbr('glowgreen', (0.15, 1.0, 0.28), 0.3, 0.0)
    _pbr('glowamber', (1.0, 0.62, 0.10), 0.3, 0.0)
    _pbr('glowblue', (0.15, 0.50, 1.0), 0.3, 0.0)
    _pbr('rust', (0.36, 0.19, 0.10), 0.6, 0.3)
# ---------------------------------------------------------------- finish: apply, join, unwrap, bake, export
def finish(out_prefix, muzzle=(0, 0, -0.2), texsize=2048, max_tris=40000):
    sc = bpy.context.scene
    for o in list(sc.objects): o.select_set(False)
    for ob in PARTS:
        bpy.context.view_layer.objects.active = ob; ob.select_set(True)
        for m in list(ob.modifiers):
            try: bpy.ops.object.modifier_apply(modifier=m.name)
            except Exception as e: print('modifier', ob.name, m.name, e)
        ob.select_set(False)
    for c in CUTTERS: bpy.data.objects.remove(c, do_unlink=True)
    if os.environ.get('PARTS_BLEND'):
        # Debug: save the separate parts (floating part check, see tools/weapons/floatcheck.py).
        bpy.ops.wm.save_as_mainfile(filepath=os.environ['PARTS_BLEND']); return
    for ob in PARTS: ob.select_set(True)
    bpy.context.view_layer.objects.active = PARTS[0]
    bpy.ops.object.join(); ob = bpy.context.object
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=0.00002)
    bpy.ops.mesh.quads_convert_to_tris(); bpy.ops.object.mode_set(mode='OBJECT')
    nt = len(ob.data.polygons)
    if nt > max_tris:
        d = ob.modifiers.new('dec', 'DECIMATE'); d.ratio = max_tris / nt; bpy.ops.object.modifier_apply(modifier='dec')
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(35))
    wn = ob.modifiers.new('wn', 'WEIGHTED_NORMAL'); wn.keep_sharp = True; bpy.ops.object.modifier_apply(modifier='wn')
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(55), island_margin=0.003, area_weight=0.8)
    bpy.ops.object.mode_set(mode='OBJECT')
    try:
        bpy.ops.uv.pack_islands(margin=0.002)
    except Exception: pass
    sc.cycles.samples = 64
    def bake(kind):
        img = bpy.data.images.new(kind, texsize, texsize, alpha=False, float_buffer=True)
        for m in ob.data.materials:
            n = m.node_tree.nodes.get('bk') or m.node_tree.nodes.new('ShaderNodeTexImage'); n.name = 'bk'; n.image = img
            m.node_tree.nodes.active = n
        outs = {}
        for m in ob.data.materials:
            nt = m.node_tree; o = nt.nodes['Material Output']
            outs[m.name] = o.inputs['Surface'].links[0].from_socket if o.inputs['Surface'].links else None
            if kind in ('col', 'gloss'):
                nt.links.new(nt.nodes['emcol' if kind == 'col' else 'emgloss'].outputs['Emission'], o.inputs['Surface'])
        if kind == 'ao': bpy.ops.object.bake(type='AO', margin=8)
        else: bpy.ops.object.bake(type='EMIT', margin=8)
        for m in ob.data.materials:
            if outs[m.name] is not None: m.node_tree.links.new(outs[m.name], m.node_tree.nodes['Material Output'].inputs['Surface'])
        return np.array(img.pixels[:], np.float32).reshape(texsize, texsize, 4)
    if FAST:
        texsize = 8; col = gl = ao = np.ones((8, 8, 4), np.float32)
    else:
        sc.cycles.samples = 4; col = bake('col'); gl = bake('gloss'); sc.cycles.samples = 48; ao = bake('ao')
    rgb = col[..., :3] * (0.30 + 0.70 * ao[..., :3])
    rgb = np.clip(rgb, 0, 1) ** (1 / 2.2)            # linear -> sRGB for the game's 8-bit texture
    spec = np.clip(gl[..., 0], 0, 1) * (0.4 + 0.6 * ao[..., 0])
    img = bpy.data.images.new('final', texsize, texsize, alpha=True)
    img.pixels[:] = np.dstack([rgb, spec]).reshape(-1)
    img.filepath_raw = out_prefix + '.png'; img.file_format = 'PNG'; img.save()
    # preview copy with the final texture on the mesh (for the review renders and the .blend)
    pm = bpy.data.materials.new('baked'); pm.use_nodes = True; nt = pm.node_tree
    tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = img
    nt.links.new(tn.outputs['Color'], nt.nodes['Principled BSDF'].inputs['Base Color'])
    nt.nodes['Principled BSDF'].inputs['Roughness'].default_value = 0.35; nt.nodes['Principled BSDF'].inputs['Metallic'].default_value = 0.4
    me = ob.data; me.calc_loop_triangles(); uv = me.uv_layers.active.data
    V = []
    glow = [bool(s.material and s.material.name.startswith('glow')) for s in ob.material_slots]
    for t in me.loop_triangles:
        e = 2.0 if (t.material_index < len(glow) and glow[t.material_index]) else 1.0   # lights: emissive in game
        for li in t.loops:
            l = me.loops[li]; p = me.vertices[l.vertex_index].co; n = l.normal; u = uv[li].uv
            V.append((p[0], p[2], -p[1], n[0], n[2], -n[1], u[0], 1.0 - u[1], e, 1.0, 1.0))
    np.savez(out_prefix + '.npz', V=np.array(V, np.float32), T=np.zeros(len(V) // 3, np.int32), muzzle=np.array(muzzle, np.float32))
    me.materials.clear(); me.materials.append(pm)
    print('FINISHED', out_prefix, 'tris', len(V) // 3)
    return ob
