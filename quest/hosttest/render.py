import sys, numpy as np
from PIL import Image
def load(fn, nb):
    groups=[]; cur=[]
    for line in open(fn):
        if line.startswith('END'): groups.append(np.array(cur,dtype=float).reshape(-1,8)); cur=[]
        else: cur.append([float(x) for x in line.split()])
    return groups[-nb:]
def render(groups, W=640, H=480, view=None, out='o.png'):
    img=np.zeros((H,W,3)); img[:]=(0.25,0.27,0.3); zb=np.full((H,W),1e9)
    for gi,g in enumerate(groups):
        P=g[:,:3].copy()
        if view is not None: P=(P-view[0])@view[1].T
        for t in range(0,len(g),3):
            tri=P[t:t+3]; col=g[t,3:6]*(0.9 if gi==0 else 1.0); a=g[t,6]
            z=-tri[:,2]
            if (z<0.02).any(): continue
            sx=W/2+tri[:,0]/z/0.9*W/2; sy=H/2-tri[:,1]/z/0.9*W/2
            x0,x1=int(max(0,sx.min())),int(min(W-1,sx.max())); y0,y1=int(max(0,sy.min())),int(min(H-1,sy.max()))
            if x1<x0 or y1<y0: continue
            xs,ys=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
            d=(sy[1]-sy[2])*(sx[0]-sx[2])+(sx[2]-sx[1])*(sy[0]-sy[2])
            if abs(d)<1e-9: continue
            l0=((sy[1]-sy[2])*(xs-sx[2])+(sx[2]-sx[1])*(ys-sy[2]))/d
            l1=((sy[2]-sy[0])*(xs-sx[2])+(sx[0]-sx[2])*(ys-sy[2]))/d
            l2=1-l0-l1; m=(l0>=0)&(l1>=0)&(l2>=0)
            zz=l0*z[0]+l1*z[1]+l2*z[2]
            sub=zb[y0:y1+1,x0:x1+1]; mm=m&(zz<sub)
            if gi==2: img[y0:y1+1,x0:x1+1][m]+=col*a; continue
            sub[mm]=zz[mm]; img[y0:y1+1,x0:x1+1][mm]=col
    Image.fromarray((np.clip(img,0,1)*255).astype(np.uint8)).save(out)
if __name__=='__main__':
    fn,nb,out=sys.argv[1],int(sys.argv[2]),sys.argv[3]
    g=load(fn,nb)
    mode=sys.argv[4] if len(sys.argv)>4 else 'eye'
    if mode=='eye': render(g,out=out)
    else:
        # side view from the right, looking at the right hand
        c=np.array([0.14,-0.16,-0.45]); eye=c+np.array([0.45,0.05,0.0])
        f=(c-eye); f/=np.linalg.norm(f); r=np.cross(f,[0,1,0]); r/=np.linalg.norm(r); u=np.cross(r,f)
        R=np.array([r,u,-f]); render(g,view=(eye,R),out=out)
