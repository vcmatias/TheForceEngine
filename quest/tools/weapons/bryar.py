# Bryar pistol (Kyle Katarn's modified Bryar, Dark Forces): black frame, thick barrel shroud wrapped in a
# brass cooling coil, banded muzzle sleeve with a flared tip, angled grip with rubber panels, cooling
# vents on the receiver sides, red power indicator at the back. Proportions from the DFmod model.
#   blender -b -P bryar.py -- <out prefix>
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
by = 0.066
# receiver with a sloped rear top
rec = box((0, 0.068, -0.026), (0.032, 0.050, 0.078), 'blackplain', bevel=0.0022)
cut(rec, cutter_box((0, 0.104, 0.020), (0.05, 0.03, 0.05), rot=(0.55, 0, 0)))
for sd in (-1, 1):
    cut(rec, cutter_box((sd * 0.0165, 0.066, -0.030), (0.004, 0.030, 0.052)))            # side panel recess
    for i in range(4):
        cut(rec, cutter_box((sd * 0.0140, 0.066, -0.016 - i * 0.009), (0.006, 0.018, 0.0035)))  # cooling vents (K-16)
    for z in (-0.002, -0.058):
        cyl((sd * 0.0158, 0.084, z), (sd * 0.0172, 0.084, z), 0.0022, 'steel', 16, bevel=0.0003)
# top rib and sights
box((0, 0.0945, -0.040), (0.010, 0.004, 0.070), 'gunmetal', bevel=0.0008)
rs = box((0, 0.099, -0.004), (0.014, 0.009, 0.009), 'steel', bevel=0.0008)
cut(rs, cutter_box((0, 0.103, -0.004), (0.0035, 0.008, 0.012)))
box((0, 0.093, -0.163), (0.0028, 0.012, 0.006), 'steel', bevel=0.0004)
# barrel shroud, collars, brass coil
cyl((0, by, -0.050), (0, by, -0.172), 0.0215, 'black', 64, bevel=0.001)
for z0, z1 in ((-0.064, -0.073), (-0.149, -0.158)):
    cyl((0, by, z0), (0, by, z1), 0.0258, 'gunmetal', 64, bevel=0.0009)
helix((0, by, -0.0745), (0, by, -0.1475), 0.0233, 0.0021, 15.5, 'brass')
# muzzle sleeve with bands, flared tip, bore
cyl((0, by, -0.170), (0, by, -0.213), 0.0185, 'black', 64, bevel=0.0008)
for z in (-0.178, -0.190, -0.202):
    cyl((0, by, z), (0, by, z - 0.0032), 0.0197, 'steel', 64, bevel=0.0005)
tip = cyl((0, by, -0.212), (0, by, -0.225), 0.0185, 'gunmetal', 64, bevel=0.0008, r2=0.0215)
cut(tip, cutter_cyl((0, by, -0.200), (0, by, -0.230), 0.0085))
for i in range(6):
    a = i * math.pi / 3
    cut(tip, cutter_box((math.cos(a) * 0.019, by + math.sin(a) * 0.019, -0.221), (0.006, 0.006, 0.012), rot=(0, 0, a)))
# under-barrel frame, trigger guard, trigger
box((0, 0.036, -0.074), (0.024, 0.026, 0.092), 'black', bevel=0.002)
# grip: angled, rubber side panels, lanyard ring
std_trigger(top=0.023)
gr = std_grip('black', 'rubber')
box((0, -0.0735, 0.034), (0.025, 0.010, 0.032), 'gunmetal', rot=(-0.30, 0, 0), bevel=0.0015)   # butt plate, sunk into the grip bottom
torus((0, -0.0815, 0.040), (1, 0, 0), 0.0045, 0.0011, 'steel')   # lanyard ring through the plate
# power indicator at the back, hammer
box((0, 0.074, 0.0125), (0.012, 0.006, 0.0018), 'glowred', bevel=0.0003)
box((0, 0.090, 0.016), (0.008, 0.012, 0.010), 'gunmetal', rot=(0.3, 0, 0), bevel=0.001)
finish(out, muzzle=(0, 0.066, -0.226))
