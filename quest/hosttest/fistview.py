import sys, numpy as np
from render import load, render
from PIL import Image
g=load(sys.argv[1],1)
render(g,W=1600,H=1600,out='_fv.png')
Image.open('_fv.png').crop((300,1000,1100,1600)).save(sys.argv[2])
