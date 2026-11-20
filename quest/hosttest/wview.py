import sys, numpy as np
from render import load, render
from PIL import Image
ims=[]
for i in range(1,10):
    g=load(f'w{i}.txt',1)
    P=g[0][:,:3]; c=(P.min(0)+P.max(0))/2; ext=np.linalg.norm(P.max(0)-P.min(0))
    def look(eye,up=(0,1,0)):
        f=c-eye; f/=np.linalg.norm(f); r=np.cross(f,up); r/=np.linalg.norm(r); u=np.cross(r,f); return (eye,np.array([r,u,-f]))
    for j,d in enumerate([np.array([1.0,0.15,0.0]), np.array([-0.7,0.5,0.6])]):
        d=d/np.linalg.norm(d)
        render(g,W=360,H=240,view=look(c+d*ext*0.62),out="_t.png"); ims.append(Image.open('_t.png').copy())
W=Image.new('RGB',(720*3,240*3))
for k in range(9):
    W.paste(ims[2*k],((k%3)*720,(k//3)*240)); W.paste(ims[2*k+1],((k%3)*720+360,(k//3)*240))
W.save('weapons.png')
