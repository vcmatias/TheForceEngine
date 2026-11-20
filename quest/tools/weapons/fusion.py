# Jeron fusion cutter (Dark Forces): a handheld mining cutter with four chambers in a horizontal row (it
# fires them in turn or all four in a line). DF render: four fat dark barrels with diagonal orange hazard
# stripes and rounded black tips, a breech block behind them with side caps; DFmod: flat body plate with
# the four chambers on top. Handle under the breech.
#   blender -b -P fusion.py -- <out prefix>
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
import bpy
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
# hazard-striped chamber material (diagonal orange stripes over dark metal)
m = bpy.data.materials.new('stripes'); m.use_nodes = True; nt = m.node_tree; L = nt.links
tc = nt.nodes.new('ShaderNodeTexCoord'); wv = nt.nodes.new('ShaderNodeTexWave'); wv.wave_type = 'BANDS'; wv.bands_direction = 'DIAGONAL'
wv.inputs['Scale'].default_value = 9.0; L.new(tc.outputs['Object'], wv.inputs['Vector'])
rp = nt.nodes.new('ShaderNodeValToRGB'); rp.color_ramp.interpolation = 'CONSTANT'; rp.color_ramp.elements[1].position = 0.55
rp.color_ramp.elements[0].color = (0.006, 0.007, 0.009, 1); rp.color_ramp.elements[1].color = (0.70, 0.15, 0.004, 1)   # linear
L.new(wv.outputs['Fac'], rp.inputs['Fac']); L.new(rp.outputs['Color'], nt.nodes['Principled BSDF'].inputs['Base Color'])
em = nt.nodes.new('ShaderNodeEmission'); em.name = 'emcol'; L.new(rp.outputs['Color'], em.inputs['Color'])
eg = nt.nodes.new('ShaderNodeEmission'); eg.name = 'emgloss'; eg.inputs['Color'].default_value = (0.6, 0.6, 0.6, 1)
MATS['stripes'] = m
by = 0.060
xs = (-0.066, -0.022, 0.022, 0.066)
for x in xs:
    cyl((x, by, -0.010), (x, by, -0.150), 0.0185, 'stripes', 48, bevel=0.0008)
    tip = cyl((x, by, -0.150), (x, by, -0.168), 0.0195, 'black', 48, bevel=0.006, seg=5)
    cut(tip, cutter_cyl((x, by, -0.155), (x, by, -0.175), 0.0065, 24))
    for z in (-0.030, -0.075, -0.120): cyl((x, by, z), (x, by, z - 0.004), 0.0195, 'gunmetal', 48, bevel=0.0005)
    cyl((x, by, -0.168), (x, by, -0.170), 0.0055, 'glowgreen', 24, bevel=0)
# breech block with side caps and a top vent plate
br = box((0, by - 0.004, 0.020), (0.170, 0.046, 0.062), 'black', bevel=0.006, seg=4)
for sd in (-1, 1): cyl((sd * 0.084, by - 0.004, 0.020), (sd * 0.096, by - 0.004, 0.020), 0.024, 'black', 48, bevel=0.004)
vp = box((0, by + 0.021, 0.022), (0.080, 0.006, 0.040), 'black', bevel=0.0015)
for i in range(6): cut(vp, cutter_box((-0.030 + i * 0.012, by + 0.025, 0.022), (0.004, 0.006, 0.032)))
box((0.070, by + 0.020, 0.030), (0.010, 0.006, 0.006), 'glowred', bevel=0.0008)     # status lights on top of the breech
box((-0.070, by + 0.020, 0.030), (0.010, 0.006, 0.006), 'glowamber', bevel=0.0008)
# body plate under the chambers (DFmod)
box((0, by - 0.022, -0.075), (0.170, 0.010, 0.150), 'black', bevel=0.003)
# handle, guard and trigger under the breech
box((0, 0.028, 0.010), (0.030, 0.020, 0.050), 'gunmetal', bevel=0.003)
std_trigger()
std_grip('black', 'rubber')
finish(out, muzzle=(0, by, -0.172))
