# E-11 blaster rifle (Sterling L2A3 based): tube receiver, perforated barrel jacket with the barrel inside,
# muzzle with front sight ears, power pack (magazine) on the left, Hengstler counter, T-tracks, the big
# scope of the DFmod/film version on the left rail, folding stock on top, pistol grip and trigger guard.
#   blender -b -P e11.py -- <out prefix>
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
by, R = 0.050, 0.0185
# receiver tube + end cap
cyl((0, by, 0.105), (0, by, -0.115), R, 'black', 64, bevel=0.0008)
cyl((0, by, 0.100), (0, by, 0.112), R + 0.0015, 'steel', 64, bevel=0.0008)
cyl((0, by, 0.112), (0, by, 0.116), R - 0.006, 'gunmetal', 48, bevel=0.0005)
# perforated jacket (hollow) with the barrel inside
jk = cyl((0, by, -0.110), (0, by, -0.330), R, 'blackplain', 64, bevel=0.0006)
cut(jk, cutter_cyl((0, by, -0.118), (0, by, -0.322), R - 0.0022, 48))
for row in range(9):
    z = -0.130 - row * 0.022
    for k in range(6):
        a = math.pi / 6 + k * math.pi / 3
        cut(jk, cutter_cyl((math.cos(a) * (R - 0.004), by + math.sin(a) * (R - 0.004), z), (math.cos(a) * (R + 0.004), by + math.sin(a) * (R + 0.004), z), 0.0055, 20))
cyl((0, by, -0.110), (0, by, -0.338), 0.0065, 'steel', 32, bevel=0.0004)
for i in range(10): cyl((0, by, -0.135 - i * 0.018), (0, by, -0.138 - i * 0.018), 0.0078, 'gunmetal', 32, bevel=0)
mz = cyl((0, by, -0.326), (0, by, -0.346), R + 0.0008, 'black', 64, bevel=0.0012)
cut(mz, cutter_cyl((0, by, -0.330), (0, by, -0.352), 0.0058, 32))
box((0, by + R + 0.004, -0.316), (0.016, 0.006, 0.016), 'gunmetal', bevel=0.0012)
for sd in (-1, 1): box((sd * 0.0068, by + R + 0.010, -0.316), (0.0025, 0.012, 0.012), 'gunmetal', bevel=0.0006)
box((0, by + R + 0.008, -0.316), (0.0016, 0.012, 0.004), 'steel', bevel=0.0003)
# rear sight
box((0, by + R + 0.003, 0.050), (0.012, 0.006, 0.016), 'gunmetal', bevel=0.001)
rs = box((0, by + R + 0.010, 0.046), (0.009, 0.010, 0.003), 'steel', bevel=0.0004)
cut(rs, cutter_cyl((0, by + R + 0.012, 0.050), (0, by + R + 0.012, 0.040), 0.0014, 16))
# power pack on the left (magazine well + cell with ribs and a contact cap)
cyl((-R + 0.002, by - 0.002, -0.078), (-0.034, by - 0.002, -0.078), 0.012, 'gunmetal', 32, bevel=0.001)
pk = box((-0.072, by - 0.002, -0.078), (0.064, 0.020, 0.028), 'black', bevel=0.002)
for i in range(6):
    for sy in (-1, 1): cut(pk, cutter_box((-0.050 - i * 0.008, by - 0.002 + sy * 0.0105, -0.078), (0.003, 0.004, 0.030)))
box((-0.106, by - 0.002, -0.078), (0.006, 0.022, 0.030), 'steel', bevel=0.0012)
cyl((-0.109, by - 0.002, -0.078), (-0.112, by - 0.002, -0.078), 0.006, 'brass', 24, bevel=0.0004)
# Hengstler counter (left of the receiver) with the digit window and knob
hg = box((-0.026, by - 0.001, -0.018), (0.020, 0.028, 0.034), 'black', bevel=0.0015)
cut(hg, cutter_box((-0.0365, by, -0.018), (0.003, 0.009, 0.022)))
box((-0.0352, by, -0.018), (0.0012, 0.0085, 0.021), 'chrome', bevel=0.0002)
cyl((-0.0355, by + 0.010, -0.006), (-0.040, by + 0.010, -0.006), 0.0035, 'steel', 24, bevel=0.0004)   # knob seated on the guard
# T-tracks on the top right
tt = box((0.010, by + R + 0.001, -0.130), (0.010, 0.006, 0.150), 'gunmetal', bevel=0.0008)
for i in range(7): cut(tt, cutter_box((0.010, by + R + 0.004, -0.070 - i * 0.020), (0.012, 0.004, 0.006)))
# scope (big, as in the DFmod) on the left rail: tube, bells, turrets, rings, lenses
sx, sy = -0.012, by + R + 0.030
cyl((sx, sy, -0.020), (sx, sy, -0.135), 0.010, 'black', 48, bevel=0.0006)
cyl((sx, sy, -0.135), (sx, sy, -0.160), 0.010, 'black', 48, bevel=0.0008, r2=0.0155)
cyl((sx, sy, -0.020), (sx, sy, 0.002), 0.010, 'black', 48, bevel=0.0008, r2=0.0135)
cyl((sx, sy, -0.160), (sx, sy, -0.167), 0.0162, 'gunmetal', 48, bevel=0.0008)
cyl((sx, sy, 0.002), (sx, sy, 0.008), 0.0140, 'gunmetal', 48, bevel=0.0008)
cyl((sx, sy, -0.1665), (sx, sy, -0.1675), 0.0128, 'lens', 48, bevel=0)
cyl((sx, sy, 0.0075), (sx, sy, 0.0085), 0.0108, 'lens', 48, bevel=0)
cyl((sx, sy + 0.010, -0.075), (sx, sy + 0.016, -0.075), 0.0055, 'steel', 32, bevel=0.0005)
cyl((sx + 0.010, sy, -0.075), (sx + 0.016, sy, -0.075), 0.0055, 'steel', 32, bevel=0.0005)
for z in (-0.040, -0.110):
    torus((sx, sy, z), (0, 0, 1), 0.0108, 0.0022, 'steel')
    box((sx, (by + R + sy - 0.010) / 2, z), (0.008, sy - 0.010 - by - R, 0.010), 'steel', bevel=0.0008)
# cocking handle slot (left) and ejection port (right)
rcv = PARTS[0]
cut(rcv, cutter_box((-R, by + 0.002, 0.030), (0.006, 0.004, 0.060)))
cut(rcv, cutter_box((R, by + 0.008, -0.040), (0.006, 0.012, 0.034)))
cyl((-R + 0.004, by + 0.002, 0.020), (-R - 0.012, by + 0.002, 0.020), 0.0035, 'steel', 20, bevel=0.0004)   # cocking handle rooted in its slot
# folding stock on top: two rods with hinges and the butt plate folded forward
for sd in (-1, 1): cyl((sd * 0.009, by + R + 0.008, 0.108), (sd * 0.009, by + R + 0.008, -0.120), 0.0032, 'gunmetal', 20, bevel=0.0003)
cyl((-0.012, by + R + 0.005, 0.106), (0.012, by + R + 0.005, 0.106), 0.0045, 'steel', 24, bevel=0.0004)
bp = box((0, by + R + 0.010, -0.124), (0.044, 0.016, 0.010), 'gunmetal', bevel=0.002)
cut(bp, cutter_box((0, by + R + 0.010, -0.124), (0.030, 0.008, 0.014)))
# trigger group housing, guard, trigger and the grip
box((0, 0.030, -0.004), (0.020, 0.024, 0.070), 'black', bevel=0.002)
std_trigger()
std_grip('black', 'rubber')
finish(out, muzzle=(0, 0.050, -0.350))
