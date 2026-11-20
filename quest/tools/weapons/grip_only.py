# Standard grip alone with a block above it (hand fitting reference).
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
std_grip()
box((0, 0.058, 0.010), (0.026, 0.024, 0.090), 'black', bevel=0.002)
finish(out, muzzle=(0, 0.06, -0.2))
