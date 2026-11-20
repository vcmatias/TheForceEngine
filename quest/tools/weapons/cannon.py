# Assault cannon (dark trooper weapon picked up in Dark Forces): heavy plasma barrel with rings and a
# slotted muzzle brake over a lower housing, a big rear body with a sloped front, rust-brown inset panels
# and glowing vents (DF render), a missile tube on top with a red tip, carry handle, grip under the body.
#   blender -b -P cannon.py -- <out prefix>
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
by = 0.062
cyl((0, by, -0.060), (0, by, -0.420), 0.0165, 'gunmetal', 48, bevel=0.0008)
for i in range(6): cyl((0, by, -0.110 - i * 0.050), (0, by, -0.122 - i * 0.050), 0.0195, 'steel', 48, bevel=0.0010)
mb = box((0, by, -0.438), (0.044, 0.036, 0.040), 'black', bevel=0.004)
cut(mb, cutter_cyl((0, by, -0.410), (0, by, -0.462), 0.0090, 32))
for sd in (-1, 1):
    for i in range(3): cut(mb, cutter_box((sd * 0.022, by, -0.426 - i * 0.012), (0.012, 0.024, 0.006)))
box((0, by - 0.028, -0.200), (0.034, 0.026, 0.300), 'black', bevel=0.004)
# rear body with a sloped front, inset panels, vents
rb = box((0, 0.094, 0.080), (0.086, 0.132, 0.300), 'blackplain', bevel=0.010, seg=4)
cut(rb, cutter_box((0, 0.190, -0.080), (0.12, 0.12, 0.20), rot=(-0.75, 0, 0)))
for sd in (-1, 1):
    cut(rb, cutter_box((sd * 0.044, 0.095, 0.090), (0.008, 0.090, 0.200)))
    box((sd * 0.0405, 0.095, 0.090), (0.002, 0.088, 0.198), 'rust', bevel=0.0006)
    for i in range(5): box((sd * 0.042, 0.130, 0.020 + i * 0.034), (0.002, 0.008, 0.018), 'glowamber', bevel=0.0004)
    for i in range(4): box((sd * 0.042, 0.065, 0.030 + i * 0.040), (0.0025, 0.020, 0.010), 'gunmetal', bevel=0.0006)
# top cover plate with bolts, rear exhaust grille, barrel collar, power cell under the body
box((0, 0.1615, 0.105), (0.074, 0.006, 0.200), 'gunmetal', bevel=0.002)
for x in (-0.031, 0.031):
    for z in (0.015, 0.105, 0.195): cyl((x, 0.164, z), (x, 0.1665, z), 0.0028, 'steel', 12, bevel=0.0005)
for i in range(6): cut(rb, cutter_box((0, 0.050 + i * 0.016, 0.232), (0.060, 0.007, 0.012)))
box((0, 0.090, 0.226), (0.064, 0.100, 0.004), 'blackplain', bevel=0.001)
cyl((0, by, -0.050), (0, by, -0.085), 0.030, 'gunmetal', 48, bevel=0.002, r2=0.020)
for k in range(8):
    a = k * math.pi / 4
    box((math.cos(a) * 0.020, by + math.sin(a) * 0.020, -0.094), (0.004, 0.004, 0.020), 'blackplain', rot=(0, 0, a), bevel=0.0008)   # fins rooted in the cone
cyl((0, 0.011, 0.090), (0, 0.011, 0.200), 0.020, 'olive', 40, bevel=0.002)   # power cell, pressed against the body
for z in (0.100, 0.190): torus((0, 0.011, z), (0, 0, 1), 0.0205, 0.0018, 'steel')
box((0, 0.028, 0.145), (0.030, 0.012, 0.100), 'black', bevel=0.002)   # cell cradle
# rivets around the rust panels
for sd in (-1, 1):
    for z in (-0.008, 0.188):
        for y in (0.055, 0.135): cyl((sd * 0.0425, y, z), (sd * 0.0445, y, z), 0.0022, 'steel', 12, bevel=0.0004)
# missile tube on top, red tip, carry handle
cyl((0, 0.175, -0.030), (0, 0.175, -0.230), 0.019, 'gunmetal', 48, bevel=0.0015)
mt = cyl((0, 0.175, -0.230), (0, 0.175, -0.242), 0.022, 'black', 48, bevel=0.002)
cut(mt, cutter_cyl((0, 0.175, -0.220), (0, 0.175, -0.250), 0.013, 32))
cyl((0, 0.175, -0.2295), (0, 0.175, -0.2320), 0.012, 'red', 32, bevel=0.0008)   # lens at the bottom of the cap hole
for z in (-0.060, -0.180): box((0, 0.150, z), (0.012, 0.030, 0.014), 'gunmetal', bevel=0.0015)
hd = box((0, 0.180, 0.110), (0.016, 0.040, 0.150), 'black', bevel=0.004)
cut(hd, cutter_box((0, 0.172, 0.110), (0.024, 0.026, 0.110)))
# grip, guard, trigger
std_trigger(top=0.028)
std_grip('black', 'rubber')
finish(out, muzzle=(0, by, -0.460))
