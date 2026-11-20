# Standard grip + trigger group + generic housing (reference for the standard hand pose).
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kit import *
out = sys.argv[sys.argv.index('--') + 1]
reset(); standard_materials()
std_grip()
std_trigger(top=0.020)
box((0, 0.040, -0.020), (0.026, 0.040, 0.250), 'black', bevel=0.002)   # generic body over the hand
finish(out, muzzle=(0, 0.06, -0.2))
