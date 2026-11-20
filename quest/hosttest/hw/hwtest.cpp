// Builds a high resolution weapon and renders 3 views (software rasterizer, per pixel lighting like the game shader).
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <algorithm>
#include "tfe_xr_weapons.h"
#include "tfe_xr_mesh.h"
struct V3{float x,y,z;};
static V3 sub(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};} static float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static V3 crs(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};} static V3 nrm(V3 a){float l=sqrtf(dot(a,a));return {a.x/l,a.y/l,a.z/l};}
int main(int argc,char**argv){
 int id=atoi(argv[1]); std::vector<SVtx> v; std::vector<uint32_t> ix; float mz[3]; const float g[3]={0,0,0};
 auto t0=std::chrono::steady_clock::now();
 weapons_buildHiRes(id,v,ix,mz,g,argc>3);
 double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
 printf("id %d verts %zu tris %zu %.0f ms muzzle %.3f %.3f %.3f\n",id,v.size(),ix.size()/3,ms,mz[0],mz[1],mz[2]);
 V3 mn={1e9,1e9,1e9},mx={-1e9,-1e9,-1e9}; for(auto&a:v){mn.x=std::min(mn.x,a.x);mn.y=std::min(mn.y,a.y);mn.z=std::min(mn.z,a.z);mx.x=std::max(mx.x,a.x);mx.y=std::max(mx.y,a.y);mx.z=std::max(mx.z,a.z);}
 V3 c={(mn.x+mx.x)/2,(mn.y+mx.y)/2,(mn.z+mx.z)/2}; float ext=std::max(mx.x-mn.x,std::max(mx.y-mn.y,mx.z-mn.z))*0.5f;
 const int W=700,H=700,NV=4; std::vector<unsigned char> img(W*NV*H*3,0);
 V3 dirs[NV]={{1,0.25f,0.1f},{0.5f,0.5f,0.9f},{-0.9f,0.4f,-0.4f},{0.15f,0.15f,1.0f}};
 for(int vi=0;vi<NV;vi++){
  V3 d=nrm(dirs[vi]); float dist=ext*(vi==3?1.6f:2.3f); V3 eye={c.x+d.x*dist,c.y+d.y*dist,c.z+d.z*dist};
  V3 f=nrm(sub(c,eye)); V3 r=nrm(crs(f,{0,1,0})); V3 u=crs(r,f);
  std::vector<float> zb(W*H,1e9); std::vector<float> col(W*H*3,0);
  for(int i=0;i<W*H;i++){col[i*3]=0.16f;col[i*3+1]=0.17f;col[i*3+2]=0.2f;}
  float foc=W/2/0.55f;
  for(size_t t=0;t<ix.size();t+=3){
   float sx[3],sy[3],sz[3]; const SVtx* P[3];
   bool bad=false;
   for(int k=0;k<3;k++){P[k]=&v[ix[t+k]]; V3 p=sub({P[k]->x,P[k]->y,P[k]->z},eye); float z=dot(p,f); if(z<0.01f){bad=true;break;} sx[k]=W/2+dot(p,r)/z*foc; sy[k]=H/2-dot(p,u)/z*foc; sz[k]=z;}
   if(bad)continue;
   int x0=std::max(0,(int)floorf(std::min({sx[0],sx[1],sx[2]}))),x1=std::min(W-1,(int)ceilf(std::max({sx[0],sx[1],sx[2]})));
   int y0=std::max(0,(int)floorf(std::min({sy[0],sy[1],sy[2]}))),y1=std::min(H-1,(int)ceilf(std::max({sy[0],sy[1],sy[2]})));
   float den=(sy[1]-sy[2])*(sx[0]-sx[2])+(sx[2]-sx[1])*(sy[0]-sy[2]); if(fabsf(den)<1e-9f)continue;
   for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
    float px=x+0.5f,py=y+0.5f;
    float l0=((sy[1]-sy[2])*(px-sx[2])+(sx[2]-sx[1])*(py-sy[2]))/den, l1=((sy[2]-sy[0])*(px-sx[2])+(sx[0]-sx[2])*(py-sy[2]))/den, l2=1-l0-l1;
    if(l0<0||l1<0||l2<0)continue; float z=l0*sz[0]+l1*sz[1]+l2*sz[2]; if(z>=zb[y*W+x])continue; zb[y*W+x]=z;
    auto I=[&](float a,float b,float cc){return l0*a+l1*b+l2*cc;};
    V3 n=nrm({I(P[0]->nx,P[1]->nx,P[2]->nx),I(P[0]->ny,P[1]->ny,P[2]->ny),I(P[0]->nz,P[1]->nz,P[2]->nz)});
    V3 pos={I(P[0]->x,P[1]->x,P[2]->x),I(P[0]->y,P[1]->y,P[2]->y),I(P[0]->z,P[1]->z,P[2]->z)};
    float cr=I(P[0]->r,P[1]->r,P[2]->r),cg=I(P[0]->g,P[1]->g,P[2]->g),cb=I(P[0]->b,P[1]->b,P[2]->b),sp=I(P[0]->spec,P[1]->spec,P[2]->spec),em=I(P[0]->emis,P[1]->emis,P[2]->emis);
    V3 L=nrm({0.3f,0.9f,0.25f}); V3 Vd=nrm(sub(eye,pos)); V3 Hh=nrm({L.x+Vd.x,L.y+Vd.y,L.z+Vd.z});
    float dif=std::max(0.f,dot(n,L)), fill=0.35f*std::max(0.f,dot(n,Vd)), rim=0.25f*powf(1-std::max(0.f,dot(n,Vd)),3);
    float spec=powf(std::max(0.f,dot(n,Hh)),40)*sp*1.2f;
    float k=0.18f+0.75f*dif+fill;
    float o[3]={cr*k+spec+rim*0.4f,cg*k+spec+rim*0.45f,cb*k+spec+rim*0.55f};
    if(em>0.5f){o[0]=cr;o[1]=cg;o[2]=cb;}
    for(int q=0;q<3;q++)col[(y*W+x)*3+q]=o[q];
   }
  }
  for(int y=0;y<H;y++)for(int x=0;x<W;x++)for(int q=0;q<3;q++){float cc=col[(y*W+x)*3+q]; img[(y*W*NV+vi*W+x)*3+q]=(unsigned char)(std::min(1.f,powf(std::max(0.f,cc),1/1.1f))*255);}
 }
 FILE*fo=fopen(argv[2],"wb"); fprintf(fo,"P6 %d %d 255\n",W*NV,H); fwrite(img.data(),1,img.size(),fo); fclose(fo);
}
