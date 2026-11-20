import sys, numpy as np
from render import load, render
from PIL import Image
fn,nb,out=sys.argv[1],int(sys.argv[2]),sys.argv[3]
g=load(fn,nb)
c=np.array([0.14,-0.14,-0.42])
def look(eye,up=(0,1,0)):
    f=c-eye; f/=np.linalg.norm(f); r=np.cross(f,up); r/=np.linalg.norm(r); u=np.cross(r,f); return (eye,np.array([r,u,-f]))
vs={'right':look(c+np.array([0.35,0,0])),'top':look(c+np.array([0,0.35,0.001]),(0,0,-1)),'front':look(c+np.array([0.05,0.05,-0.35])),'back':look(c+np.array([-0.12,0.15,0.3]))}
ims=[]
for k,v in vs.items():
    render(g,W=400,H=400,view=v,out='_'+k+'.png'); ims.append(Image.open('_'+k+'.png'))
W=Image.new('RGB',(1600,400))
for i,im in enumerate(ims): W.paste(im,(400*i,0))
W.save(out)
