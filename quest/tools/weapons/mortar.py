# Packered mortar gun (Dark Forces): cylindrical chrome body, three shell containers around it (they
# rotate on each shot), big-bore barrel inside a perforated shroud, sling hooks behind the shroud, dark
# gray section between the body and the stock, slender stock ending in a flat plate, trigger in front of
# the stock behind a guard (lore and the DF first-person render).
#   blender -b -P mortar.py -- <out prefix>
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
by = 0.078
cyl((0, by, 0.000), (0, by, -0.200), 0.038, 'chrome', 64, bevel=0.003)
for z in (-0.004, -0.196): cyl((0, by, z), (0, by, z - 0.008 if z < -0.1 else z + 0.008), 0.0405, 'gunmetal', 64, bevel=0.0015)
# three shell containers with the shell bases showing at the front
for k in range(3):
    a = math.radians(90 + 120 * k)
    x, y = math.cos(a) * 0.043, by + math.sin(a) * 0.043
    cyl((x, y, -0.020), (x, y, -0.175), 0.0165, 'gunmetal', 40, bevel=0.0015)
    cyl((x, y, -0.175), (x, y, -0.181), 0.0125, 'brass', 32, bevel=0.0006)
    cyl((x, y, -0.181), (x, y, -0.184), 0.0060, 'red', 24, bevel=0.0004)
    for z in (-0.050, -0.145): torus((x, y, z), (0, 0, 1), 0.0168, 0.0014, 'steel')
# perforated shroud (hollow) around the big-bore barrel
sh = cyl((0, by, -0.200), (0, by, -0.335), 0.034, 'steel', 64, bevel=0.0008)
cut(sh, cutter_cyl((0, by, -0.208), (0, by, -0.327), 0.0305, 48))
for row in range(6):
    z = -0.215 - row * 0.020
    for k in range(10):
        a = k * 2 * math.pi / 10 + (row % 2) * math.pi / 10
        cut(sh, cutter_cyl((math.cos(a) * 0.028, by + math.sin(a) * 0.028, z), (math.cos(a) * 0.040, by + math.sin(a) * 0.040, z), 0.0055, 16))
bl = cyl((0, by, -0.190), (0, by, -0.338), 0.022, 'blackplain', 48, bevel=0.0006)
cut(bl, cutter_cyl((0, by, -0.250), (0, by, -0.345), 0.0165, 40))
mz = cyl((0, by, -0.333), (0, by, -0.348), 0.037, 'chrome', 64, bevel=0.002)
cut(mz, cutter_cyl((0, by, -0.330), (0, by, -0.352), 0.0230, 48))
# sling hooks behind the shroud
torus((0, by + 0.043, -0.208), (1, 0, 0), 0.008, 0.0022, 'steel')
torus((0, by - 0.043, -0.208), (1, 0, 0), 0.008, 0.0022, 'steel')
# dark section behind the body, trigger housing, guard, trigger, grip
cyl((0, by - 0.004, 0.000), (0, by - 0.008, 0.055), 0.030, 'gunmetal', 48, bevel=0.003, r2=0.022)
box((0, 0.034, 0.000), (0.026, 0.030, 0.070), 'black', bevel=0.003)
std_trigger(top=0.019)
std_grip('black', 'rubber')
# slender stock ending in a flat plate
for sd in (-1, 1): cyl((sd * 0.009, by - 0.010, 0.050), (sd * 0.012, by - 0.038, 0.275), 0.0058, 'steel', 24, bevel=0.0005)
cyl((0, by - 0.030, 0.050), (0, by - 0.070, 0.276), 0.0050, 'gunmetal', 24, bevel=0.0005)
box((0, by - 0.020, 0.150), (0.030, 0.012, 0.010), 'gunmetal', rot=(-0.13, 0, 0), bevel=0.002)
box((0, by - 0.042, 0.280), (0.052, 0.122, 0.012), 'gunmetal', bevel=0.003)
box((0, by - 0.042, 0.288), (0.046, 0.112, 0.005), 'rubber', bevel=0.0015)
finish(out, muzzle=(0, by, -0.350))
