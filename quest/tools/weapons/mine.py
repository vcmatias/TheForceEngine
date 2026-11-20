# I.M. mine (Dark Forces): flat silver disc with a chrome top plate, rim ring with grip notches, three
# orange arming bars, central detonator cap with a red light, hazard label and a sensor lens.
#   blender -b -P mine.py -- <out prefix>
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
cy, cz, R = 0.014, -0.024, 0.046
body = cyl((0, cy - 0.008, cz), (0, cy + 0.008, cz), R, 'steel', 128, bevel=0.003, seg=3)
for i in range(16):
    a = i * 2 * math.pi / 16
    cut(body, cutter_box((math.cos(a) * R, cy, cz + math.sin(a) * R), (0.005, 0.010, 0.0045), rot=(0, -a, 0)))
cyl((0, cy + 0.007, cz), (0, cy + 0.0105, cz), R - 0.007, 'chrome', 128, bevel=0.0015)
cyl((0, cy - 0.007, cz), (0, cy - 0.0105, cz), R - 0.005, 'gunmetal', 128, bevel=0.0015)
torus((0, cy, cz), (0, 1, 0), R + 0.0004, 0.0016, 'gunmetal', 128, 12)
# arming bars
for k in range(3):
    a = k * 2 * math.pi / 3 + math.pi / 2
    box((math.cos(a) * 0.024, cy + 0.0115, cz + math.sin(a) * 0.024), (0.017, 0.003, 0.0050), 'hazard', rot=(0, -a, 0), bevel=0.0009)
# detonator cap and light
cyl((0, cy + 0.010, cz), (0, cy + 0.0145, cz), 0.009, 'black', 64, bevel=0.0012)
for i in range(8):
    a = i * math.pi / 4
    box((math.cos(a) * 0.0088, cy + 0.0125, cz + math.sin(a) * 0.0088), (0.0016, 0.0030, 0.0030), 'gunmetal', rot=(0, -a, 0), bevel=0.0004)
cyl((0, cy + 0.0145, cz), (0, cy + 0.0170, cz), 0.0042, 'glowred', 32, bevel=0.0010)
# status panel: black plate with three lights
box((0, cy + 0.0108, cz + 0.030), (0.022, 0.0012, 0.008), 'black', bevel=0.0005)
for i, g in enumerate(('glowred', 'glowamber', 'glowgreen')):
    box((-0.006 + i * 0.006, cy + 0.0116, cz + 0.030), (0.0036, 0.0010, 0.0036), g, bevel=0.0003)
cyl((0.022, cy + 0.0105, cz + 0.020), (0.022, cy + 0.0125, cz + 0.020), 0.0028, 'lens', 24, bevel=0.0004)
torus((0.022, cy + 0.0115, cz + 0.020), (0, 1, 0), 0.0030, 0.0006, 'steel', 24, 8)
finish(out, muzzle=(0, cy, cz - 0.050))
