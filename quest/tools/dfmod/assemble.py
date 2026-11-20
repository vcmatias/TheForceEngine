# Assembles a DFmod first-person weapon in "hand space" (x forward, y left, z up, game units):
# weapon md3 placed by tag_weapon of the *_hand.md3, barrel md3 by tag_barrel, muzzle = tag_flash.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from md3 import read_md3
# DF weapon id -> (folder, weapon md3, barrel md3 or None)
WEAPONS = {
    1: ('briar_pistol', 'briar_pistol', None),
    2: ('blaster_r', 'blaster', None),
    4: ('heavy_repeater', 'heavy_repeater', 'heavy_repeater_barrel'),
    5: ('demp2', 'demp2', None),
    6: ('golan_arms', 'golan_arms', 'golan_arms_barrel'),
    8: ('concussion', 'c_rifle', 'c_rifle_barrel'),
    9: ('merr_sonn', 'merr_sonn', 'merr_sonn_barrel'),
}
def xf(tag, v):
    o, a = tag
    return tuple(o[i] + v[0] * a[0][i] + v[1] * a[1][i] + v[2] * a[2][i] for i in range(3))
def xn(tag, n):
    o, a = tag
    return tuple(n[0] * a[0][i] + n[1] * a[1][i] + n[2] * a[2][i] for i in range(3))
IDENT = ((0, 0, 0), ((1, 0, 0), (0, 1, 0), (0, 0, 1)))
def assemble(root, wid):
    folder, wname, bname = WEAPONS[wid]
    d = os.path.join(root, folder)
    hand = read_md3(os.path.join(d, wname + '_hand.md3'))
    w = read_md3(os.path.join(d, wname + '.md3'))
    tw = hand['tags'].get('tag_weapon', IDENT)
    parts = [(w, tw)]
    if bname:
        tb = hand['tags'].get('tag_barrel', tw)
        parts.append((read_md3(os.path.join(d, bname + '.md3')), tb))
    surfs = []
    for m, t in parts:
        for s in m['surfs']:
            if 'forearm' in s['name'] or s['name'].startswith('*'): continue
            surfs.append(dict(name=s['name'], shader=s['shaders'][0] if s['shaders'] else '', tris=s['tris'], st=s['st'],
                              verts=[xf(t, v) for v in s['verts']], norms=[xn(t, n) for n in s['norms']]))
    # muzzle: hand tag_flash, else the weapon's own tag_flash through tag_weapon
    fl = None
    for k in ('tag_flash', 'tag_flash1'):
        if k in hand['tags']: fl = hand['tags'][k][0]; break
    if fl is None and 'tag_flash' in w['tags']: fl = xf(tw, w['tags']['tag_flash'][0])
    return surfs, fl
