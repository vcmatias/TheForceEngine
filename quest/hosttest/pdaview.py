import sys, numpy as np, subprocess, os
from PIL import Image
out=subprocess.run(['./ht_f','0','0','/tmp/claude-0/remodel/pda.txt','0']+sys.argv[2:4],env=dict(os.environ,PDA='1'),capture_output=True,text=True).stdout
pl=[l for l in out.splitlines() if l.startswith('pda')][0].split()[1:]
p=np.array([float(x) for x in pl[:3]]); x,y,z,w=[float(v) for v in pl[3:]]
R=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
W=0.17; H=W*0.625
c=[p+R@np.array([sx*W/2,sy*H/2,0.0005]) for sx,sy in [(-1,-1),(1,-1),(1,1),(-1,1)]]
L=[]
def tri(a,b,cc,col):
    for v in (a,b,cc): L.append('%f %f %f %f %f %f 1 0'%(v[0],v[1],v[2],*col))
tri(c[0],c[1],c[2],(0.2,0.8,0.3)); tri(c[0],c[2],c[3],(0.2,0.8,0.3))
bl=c[0]; tri(bl+R@np.array([0,0,0.001]),bl+R@np.array([0.015,0,0.001]),bl+R@np.array([0,0.015,0.001]),(1,1,0))
open('/tmp/claude-0/remodel/extra.txt','w').write('\n'.join(L)+'\nEND\n')
n=R[:,2]
views=[(n*0.32,R[:,1]),(n*0.25-R[:,0]*0.2,R[:,1]),(-n*0.32,R[:,1]),(R[:,0]*-0.3+n*0.05,R[:,1]),(R[:,1]*0.3+n*0.05,n)]
pc=p if not os.environ.get('CORNER') else c[0]
if os.environ.get('CORNER'): views=[(e*0.5,u) for e,u in views]
args=['./crender','/tmp/claude-0/remodel/pda.txt','/tmp/claude-0/remodel/extra.txt','/tmp/claude-0/remodel/pv.ppm']+['%f'%v for v in pc]
for e,u in views: args+=['%f'%v for v in list(e)+list(u)]
subprocess.run(args)
Image.open('/tmp/claude-0/remodel/pv.ppm').save(sys.argv[1]); print(out)
