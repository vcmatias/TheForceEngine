import sys, json, struct, numpy as np
from PIL import Image
sys.path.insert(0,'/home/claude/tfevr/hosttest/fit'); sys.path.insert(0,'/home/claude/tfevr/hosttest')
from fit import SPR, R, FX, FY
import dfdata
from render import load
P=dfdata.pal()
poses={}
for fn in ['fit_2.json','fit_1_3_4_5.json','fit_6_7_8_9.json']:
    for k,v in json.load(open('/home/claude/tfevr/hosttest/fit/'+fn)).items(): poses[int(k)]=v
def spritefile(id):
    n,x0,y0=SPR[id]; ix,_=dfdata.bm(n); w,h=ix.shape
    rgba=np.zeros((h,w,4),np.uint8)
    for x in range(w):
        for y in range(h):
            c=ix[x,y]
            if c: rgba[h-1-y,x,:3]=P[c]; rgba[h-1-y,x,3]=255
    with open('/tmp/claude-0/spr%d.bin'%id,'wb') as f:
        f.write(struct.pack('<4i',w,h,x0,y0)); f.write(struct.pack('<6f',*poses[id])); f.write(rgba.tobytes())
    return rgba,x0,y0
def renderCanon(g,pose,W=320,H=200):
    img=np.zeros((H,W,3)); img[:]=(0.25,0.27,0.3); zb=np.full((H,W),1e9)
    Rm=R(*pose[:3]); t=np.array(pose[3:])
    for grp in g:
        Pp=grp[:,:3]@Rm.T+t
        for k in range(0,len(grp),3):
            tri=Pp[k:k+3]; col=grp[k,3:6]; z=-tri[:,2]
            if (z<0.02).any(): continue
            sx=160+FX*tri[:,0]/z; sy=100-FY*tri[:,1]/z
            x0,x1=int(max(0,sx.min())),int(min(W-1,sx.max())); y0,y1=int(max(0,sy.min())),int(min(H-1,sy.max()))
            if x1<x0 or y1<y0: continue
            xs,ys=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
            d=(sy[1]-sy[2])*(sx[0]-sx[2])+(sx[2]-sx[1])*(sy[0]-sy[2])
            if abs(d)<1e-12: continue
            l0=((sy[1]-sy[2])*(xs-sx[2])+(sx[2]-sx[1])*(ys-sy[2]))/d; l1=((sy[2]-sy[0])*(xs-sx[2])+(sx[0]-sx[2])*(ys-sy[2]))/d; l2=1-l0-l1
            m=(l0>=0)&(l1>=0)&(l2>=0); zz=l0*z[0]+l1*z[1]+l2*z[2]
            sub=zb[y0:y1+1,x0:x1+1]; mm=m&(zz<sub); sub[mm]=zz[mm]; img[y0:y1+1,x0:x1+1][mm]=col
    return (np.clip(img,0,1)*255).astype(np.uint8)
if __name__=='__main__':
    import subprocess
    out=[]
    for id in [int(a) for a in sys.argv[1:]]:
        rgba,x0,y0=spritefile(id)
        subprocess.run(['/home/claude/tfevr/hosttest/wtest2',str(id),'/tmp/claude-0/wp%d.txt'%id,'/tmp/claude-0/spr%d.bin'%id],check=True)
        g=load('/tmp/claude-0/wp%d.txt'%id,1)
        a=renderCanon(g,poses[id])
        ref=np.zeros((200,320,3),np.uint8); ref[:]=(64,69,77)
        h,w=rgba.shape[:2]
        for y in range(h):
            for x in range(w):
                if rgba[y,x,3] and 0<=y0+y<200 and 0<=x0+x<320: ref[y0+y,x0+x]=rgba[y,x,:3]
        Image.fromarray(np.hstack([ref,a])).resize((1280,400),Image.NEAREST).save('/tmp/claude-0/proj%d.png'%id)
