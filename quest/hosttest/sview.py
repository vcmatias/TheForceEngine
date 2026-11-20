import sys, numpy as np
from render import load, render
from PIL import Image
g=load(sys.argv[1],1)
P=g[0][:,:3]; c=P.mean(axis=0); ext=np.abs(P-c).max()
def look(eye,up=(0,1,0)):
    f=c-eye; f/=np.linalg.norm(f); r=np.cross(f,up); r/=np.linalg.norm(r); u=np.cross(r,f); return (eye,np.array([r,u,-f]))
ims=[]
for d in [(1,0.3,0.2),(0.4,0.6,1.0),(-0.8,0.5,-0.6)]:
    d=np.array(d); d=d/np.linalg.norm(d)
    render(g,W=420,H=420,view=look(c+d*ext*2.4),out='_s.png'); ims.append(Image.open('_s.png').copy())
W=Image.new('RGB',(1260,420))
for i,im in enumerate(ims): W.paste(im,(420*i,0))
W.save(sys.argv[2])
