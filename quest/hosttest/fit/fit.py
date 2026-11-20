import sys, json, numpy as np
from PIL import Image
from scipy.optimize import minimize
sys.path.insert(0,'/home/claude/tfevr/voxel'); import dfdata
P=dfdata.pal()
SPR={1:('PISTOL1.BM',165,142),2:('RIFLE1.BM',113,127),3:('THERM1.BM',161,131),4:('AUTOGUN1.BM',156,138),5:('FUSION1.BM',19,152),6:('MORTAR1.BM',123,119),7:('CLAY1.BM',105,153),8:('CONCUSS1.BM',130,131),9:('ASSAULT3.BM',224,81)}
FX=160.0; FY=160.0/1.2
def sprite(id):
    n,x0,y0=SPR[id]; ix,_=dfdata.bm(n)   # ix[x][y] bottom-up? toimg flips: row = h-1-y
    w,h=ix.shape
    rgb=np.zeros((200,320,3),np.uint8); mask=np.zeros((200,320),bool); skin=np.zeros((200,320),bool)
    for x in range(w):
        for y in range(h):
            c=ix[x,y]
            if not c: continue
            sx=x0+x; sy=y0+(h-1-y)
            if 0<=sx<320 and 0<=sy<200:
                col=P[c]; rgb[sy,sx]=col; r,g,b=[int(v) for v in col]
                if r>g+35 and r>b+50: skin[sy,sx]=True
                else: mask[sy,sx]=True
    return rgb,mask,skin
def model(id):
    pts=[]; cur=[]
    for line in open('../w%d.txt'%id):
        if line.startswith('END'): break
        cur.append([float(v) for v in line.split()[:3]])
    T=np.array(cur).reshape(-1,6,3)   # 2 tris per face
    return T.mean(axis=1)            # face centers
def R(yaw,pitch,roll):
    cy,sy=np.cos(yaw),np.sin(yaw); cp,sp=np.cos(pitch),np.sin(pitch); cr,sr=np.cos(roll),np.sin(roll)
    Ry=np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]]); Rx=np.array([[1,0,0],[0,cp,-sp],[0,sp,cp]]); Rz=np.array([[cr,-sr,0],[sr,cr,0],[0,0,1]])
    return Ry@Rx@Rz
def project(pts,p):
    yaw,pitch,roll,tx,ty,tz=p
    q=pts@R(yaw,pitch,roll).T+np.array([tx,ty,tz])
    z=-q[:,2]; ok=z>0.05
    sx=160+FX*q[:,0]/np.maximum(z,1e-3); sy=100-FY*q[:,1]/np.maximum(z,1e-3)
    return sx,sy,z,ok
def raster(pts,p):
    sx,sy,z,ok=project(pts,p)
    m=np.zeros((200,320),bool)
    xi=np.round(sx[ok]).astype(int); yi=np.round(sy[ok]).astype(int)
    for dx in (-1,0,1):
        for dy in (-1,0,1):
            a=xi+dx; b=yi+dy; v=(a>=0)&(a<320)&(b>=0)&(b<200); m[b[v],a[v]]=True
    return m
def cost(p,pts,mask,skin):
    m=raster(pts,p)
    inter=(m&mask).sum(); uni=(m|mask).sum()-(m&skin).sum()
    return -inter/max(uni,1)
def fit(id):
    rgb,mask,skin=sprite(id); pts=model(id)
    ys,xs=np.nonzero(mask); cx,cy=xs.mean(),ys.mean()
    best=None
    for z0 in (0.30,0.40,0.55):
        # place grip under the sprite centroid
        tx=(cx-160)/FX*z0; ty=-(cy-100)/FY*z0
        for yaw0 in (0.0,0.25,0.5):
            p0=np.array([yaw0,0.15,0.0,tx,ty,-z0])
            r=minimize(cost,p0,args=(pts,mask,skin),method='Nelder-Mead',options={'maxiter':400,'xatol':1e-3,'fatol':1e-4,'initial_simplex':None})
            if best is None or r.fun<best.fun: best=r
    return best,rgb,mask,pts
if __name__=='__main__':
    res={}
    ids=[int(a) for a in sys.argv[1:]] or list(range(1,10))
    for id in ids:
        b,rgb,mask,pts=fit(id)
        res[id]=list(map(float,b.x)); print(id,'IoU',-b.fun,res[id],flush=True)
        m=raster(pts,b.x)
        img=rgb.copy(); img[m&~mask]=[255,0,255]; img[m&mask]=(img[m&mask]*0.5+np.array([0,128,0])).astype(np.uint8)
        Image.fromarray(img).resize((640,400),Image.NEAREST).save('fit%d.png'%id)
    json.dump(res,open('fit_%s.json'%('_'.join(map(str,ids))),'w'))
