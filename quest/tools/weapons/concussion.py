# Stouker concussion rifle (Dark Forces): long silver barrel on top with rings, dark lower body with a row
# of round vents and a blue status light (DF render), red concussion chamber ahead of the grip (DFmod),
# wooden stock with a cheek cut and a rubber butt plate.
#   blender -b -P concussion.py -- <out prefix>
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
ty, ly = 0.080, 0.050
cyl((0, ty, 0.050), (0, ty, -0.420), 0.0125, 'chrome', 48, bevel=0.0008)
for i in range(7): cyl((0, ty, -0.060 - i * 0.050), (0, ty, -0.066 - i * 0.050), 0.0145, 'steel', 48, bevel=0.0006)
mz = cyl((0, ty, -0.415), (0, ty, -0.440), 0.0165, 'gunmetal', 48, bevel=0.0015)
cut(mz, cutter_cyl((0, ty, -0.420), (0, ty, -0.446), 0.0070, 32))
for k in range(4):
    a = k * math.pi / 2 + math.pi / 4
    cut(mz, cutter_box((math.cos(a) * 0.016, ty + math.sin(a) * 0.016, -0.430), (0.006, 0.006, 0.012), rot=(0, 0, a)))
# barrel supports
for z in (-0.120, -0.260): box((0, (ty + ly) / 2, z), (0.012, ty - ly, 0.014), 'gunmetal', bevel=0.0015)
# lower body with vents and the blue status light
lb = box((0, ly, -0.150), (0.030, 0.034, 0.330), 'black', bevel=0.004)
for sd in (-1, 1):
    for i in range(8):
        cut(lb, cutter_cyl((sd * 0.012, ly, -0.110 - i * 0.026), (sd * 0.020, ly, -0.110 - i * 0.026), 0.0055, 20))
    box((sd * 0.0152, ly + 0.010, -0.040), (0.0012, 0.006, 0.040), 'glowblue', bevel=0.0003)
box((0, ly - 0.004, -0.330), (0.022, 0.024, 0.060), 'gunmetal', bevel=0.003)
# red concussion chamber with a glowing core
cyl((-0.021, 0.030, -0.060), (0.021, 0.030, -0.060), 0.019, 'red', 48, bevel=0.003)
for sd in (-1, 1): cyl((sd * 0.021, 0.030, -0.060), (sd * 0.023, 0.030, -0.060), 0.010, 'glowred', 32, bevel=0.0005)
torus((0, 0.030, -0.060), (1, 0, 0), 0.0195, 0.002, 'steel')
# receiver block tying the barrel to the body, with a recessed side panel and screws
rc = box((0, (ty + ly) / 2 + 0.002, 0.010), (0.034, 0.050, 0.075), 'gunmetal', bevel=0.004, seg=3)
for sd in (-1, 1):
    cut(rc, cutter_box((sd * 0.017, (ty + ly) / 2, 0.012), (0.004, 0.030, 0.055)))
    for zz in (-0.012, 0.036):
        for yy in (ly + 0.002, ty - 0.004): cyl((sd * 0.0145, yy, zz), (sd * 0.0160, yy, zz), 0.0022, 'steel', 12, bevel=0.0004)   # screws on the recess floor
# conduits along the body sides and an energy cell under the front
for sd in (-1, 1):
    cyl((sd * 0.0165, ly - 0.011, -0.030), (sd * 0.0165, ly - 0.011, -0.300), 0.0028, 'copper', 16, bevel=0.0003)
    for z in (-0.080, -0.200, -0.290): box((sd * 0.0165, ly - 0.011, z), (0.007, 0.007, 0.006), 'black', bevel=0.0008)
cyl((0, ly - 0.024, -0.170), (0, ly - 0.024, -0.280), 0.011, 'olive', 32, bevel=0.0015)
for z in (-0.176, -0.274): torus((0, ly - 0.024, z), (0, 0, 1), 0.0112, 0.0012, 'steel')
# heat-sink fins on top of the barrel's rear
for i in range(6): box((0, ty + 0.0125, -0.010 - i * 0.008), (0.012, 0.006, 0.0025), 'blackplain', bevel=0.0006)
# sight
box((0, ty + 0.018, 0.030), (0.006, 0.012, 0.010), 'brass', bevel=0.001)
# trigger group, guard, trigger, grip
box((0, 0.030, 0.010), (0.024, 0.024, 0.050), 'black', bevel=0.002)
std_trigger()
std_grip('wood', None)
# wooden stock with a cheek cut, butt plate
st = box((0, 0.040, 0.165), (0.026, 0.050, 0.250), 'wood', rot=(0.06, 0, 0), bevel=0.006, seg=4)
cut(st, cutter_box((0, 0.020, 0.175), (0.030, 0.014, 0.130), rot=(0.06, 0, 0)))
box((0, 0.048, 0.294), (0.028, 0.062, 0.010), 'rubber', rot=(0.06, 0, 0), bevel=0.002)
finish(out, muzzle=(0, ty, -0.446))
