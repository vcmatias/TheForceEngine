# Class-A thermal detonator (Dark Forces / ROTJ): silver sphere, raised equator rings with a recessed band
# of sockets (some lit), arming plate on top with three lights and the slider, knurled plunger below.
#   blender -b -P thermal.py -- <out prefix>
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
cy, cz, R = 0.006, -0.026, 0.031
s = sphere((0, cy, cz), R, 'steel', 96, 48)
# recessed equator band
cut(s, cutter_cyl((0, cy - 0.0042, cz), (0, cy + 0.0042, cz), R + 0.01, 96))
sphere((0, cy, cz), R - 0.0018, 'gunmetal', 96, 48)
for sd in (-1, 1): torus((0, cy + sd * 0.0050, cz), (0, 1, 0), R - 0.0006, 0.0016, 'chrome', 96, 16)
# sockets around the band
for i in range(12):
    a = i * 2 * math.pi / 12
    x, z = math.sin(a) * (R - 0.0012), cz + math.cos(a) * (R - 0.0012)
    box((x, cy, z), (0.0042, 0.0040, 0.0030), 'black', rot=(0, a, 0), bevel=0.0006)
    lit = 'glowred' if i % 4 == 0 else ('glowamber' if i % 4 == 2 else 'blackplain')
    box((math.sin(a) * (R + 0.0005), cy, cz + math.cos(a) * (R + 0.0005)), (0.0024, 0.0020, 0.0012), lit, rot=(0, a, 0), bevel=0.0003)
# panel seams on the hemispheres
for k in range(4):
    a = k * math.pi / 2 + math.pi / 4
    for sd in (-1, 1):
        torus((0, cy, cz), (math.cos(a), 0, math.sin(a)), R + 0.0001, 0.00045, 'gunmetal', 96, 6)
# arming plate on top
pl = box((0, cy + R - 0.0005, cz), (0.026, 0.006, 0.018), 'black', bevel=0.0018, seg=3)
cut(pl, cutter_box((0, cy + R + 0.0028, cz + 0.004), (0.018, 0.003, 0.004)))
box((-0.004, cy + R + 0.0022, cz + 0.004), (0.004, 0.0026, 0.0034), 'chrome', bevel=0.0006)
for x, g in ((-0.007, 'glowred'), (0.0, 'glowamber'), (0.007, 'glowgreen')):
    box((x, cy + R + 0.0026, cz - 0.0045), (0.004, 0.0016, 0.0036), g, bevel=0.0005)
for x in (-0.011, 0.011):
    for z in (-0.0065, 0.0065): cyl((x, cy + R + 0.0022, cz + z), (x, cy + R + 0.0030, cz + z), 0.0010, 'steel', 12, bevel=0.0002)
# plunger below
cyl((0, cy - R + 0.002, cz), (0, cy - R - 0.0035, cz), 0.0065, 'steel', 48, bevel=0.0008)
cyl((0, cy - R - 0.0035, cz), (0, cy - R - 0.0075, cz), 0.0045, 'rubber', 48, bevel=0.0008)
finish(out, muzzle=(0, cy, cz - 0.040))
