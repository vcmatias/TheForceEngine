# Imperial repeater rifle (Dark Forces autogun): chunky rounded dark body with two rows of brass studs on
# each side and a ribbed top (DF first-person render), triangular barrel housing with three barrels and a
# brass band, belt of rounds visible through a window on the left, drum magazine on the right, wooden
# grip at the rear, slender two-rod stock from the bottom of the grip to a butt plate (lore).
#   blender -b -P repeater.py -- <out prefix>
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
by = 0.072
body = box((0, 0.074, -0.118), (0.062, 0.074, 0.200), 'black', bevel=0.010, seg=5)
cyl((-0.029, 0.074, -0.020), (0.029, 0.074, -0.020), 0.037, 'black', 64, bevel=0.006)          # rounded rear (inset 2 mm)
box((0, 0.047, 0.002), (0.050, 0.054, 0.056), 'black', bevel=0.004, seg=3)                      # rear lower block: ties the trigger housing and the grip to the body
# belt window on the left with the rounds inside
cut(body, cutter_box((-0.031, 0.070, -0.125), (0.012, 0.040, 0.100)))
box((-0.0245, 0.070, -0.125), (0.004, 0.044, 0.104), 'gunmetal', bevel=0.0008)   # backing plate on the pocket floor
for i in range(12):
    z = -0.083 - i * 0.0078
    cyl((-0.027, 0.054, z), (-0.027, 0.086, z), 0.0034, 'brass', 20, bevel=0.0004)
    cyl((-0.027, 0.086, z), (-0.027, 0.090, z), 0.0024, 'copper', 16, bevel=0.0003)
# brass studs (two rows each side), ribbed top, charge lights
for sd in (-1, 1):
    for row, y in enumerate((0.098, 0.048)):
        for i in range(7):
            z = -0.035 - i * 0.026
            if sd < 0 and row == 0 and -0.180 < z < -0.070: continue
            cyl((sd * 0.030, y, z), (sd * 0.0345, y, z), 0.0042, 'brass', 24, bevel=0.0006)
for i in range(9):
    box((0, 0.1125, -0.035 - i * 0.020), (0.044, 0.006, 0.009), 'gunmetal', bevel=0.0015)
for i in range(3):
    box((0.0315, 0.106, -0.200 + i * 0.012), (0.003, 0.004, 0.006), 'glowamber', bevel=0.0006)   # charge lights on the right side (they were buried under the top)
# triangular barrel housing, brass band, three barrels
hs = cyl((0, by, -0.212), (0, by, -0.272), 0.046, 'blackplain', 3, bevel=0.009, seg=5)
cyl((0, by, -0.214), (0, by, -0.226), 0.049, 'brass', 3, bevel=0.004)
for k in range(3):
    a = math.radians(90 + 120 * k)
    x, y = math.cos(a) * 0.022, by + math.sin(a) * 0.022
    b = cyl((x, y, -0.262), (x, y, -0.300), 0.0085, 'gunmetal', 32, bevel=0.0008)
    cut(b, cutter_cyl((x, y, -0.285), (x, y, -0.305), 0.0042, 20))
    torus((x, y, -0.292), (0, 0, 1), 0.0086, 0.0012, 'steel')
cyl((0, by, -0.268), (0, by, -0.282), 0.010, 'steel', 32, bevel=0.0008)
# drum magazine on the right, inside the body's side silhouette (seen from the left it must not hang
# below the body like a loose disc)
dy = 0.074
cyl((0.031, dy, -0.120), (0.068, dy, -0.120), 0.036, 'gunmetal', 64, bevel=0.003)
cyl((0.068, dy, -0.120), (0.072, dy, -0.120), 0.027, 'black', 48, bevel=0.0015)
torus((0.069, dy, -0.120), (1, 0, 0), 0.032, 0.0022, 'brass')
for k in range(6):
    a = k * math.pi / 3
    cyl((0.071, dy + math.sin(a) * 0.019, -0.120 + math.cos(a) * 0.019), (0.0745, dy + math.sin(a) * 0.019, -0.120 + math.cos(a) * 0.019), 0.0026, 'steel', 16, bevel=0.0003)
cyl((0.072, dy, -0.120), (0.077, dy, -0.120), 0.007, 'steel', 24, bevel=0.0008)
# trigger group, guard, wooden grip with grooves
box((0, 0.030, -0.012), (0.022, 0.020, 0.060), 'black', bevel=0.002)
std_trigger(top=0.020)
std_grip('wood', None, grooves=True)
# slender stock: two rods from the grip bottom and the body to the butt plate
cyl((0, -0.070, 0.036), (0, -0.022, 0.230), 0.0058, 'steel', 24, bevel=0.0004)
cyl((0, 0.046, 0.010), (0, 0.020, 0.228), 0.0058, 'steel', 24, bevel=0.0004)
box((0, 0.000, 0.234), (0.034, 0.104, 0.014), 'gunmetal', bevel=0.003)
box((0, 0.000, 0.243), (0.030, 0.096, 0.005), 'rubber', bevel=0.0015)
finish(out, muzzle=(0, by, -0.302))
