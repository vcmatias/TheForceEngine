import sys, numpy as np
from render import load, render
from PIL import Image
g=load(sys.argv[1],1)
c=np.array([-0.04,-0.19,-0.36])
def look(eye,up=(0,1,0)):
    f=c-eye; f/=np.linalg.norm(f); r=np.cross(f,up); r/=np.linalg.norm(r); u=np.cross(r,f); return (eye,np.array([r,u,-f]))
ims=[]
for i,e in enumerate([c+np.array([0.03,0.1,0.12]), c+np.array([-0.14,0.03,0.0]), c+np.array([0.0,0.14,0.001])]):
    render(g,W=360,H=360,view=look(e,(0,1,0) if i<2 else (0,0,-1)),out='_f%d.png'%i); ims.append(Image.open('_f%d.png'%i))
W=Image.new('RGB',(1080,360))
for i,im in enumerate(ims): W.paste(im,(360*i,0))
W.save(sys.argv[2])
