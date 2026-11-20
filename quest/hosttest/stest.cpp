#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include "tfe_xr_weapons.h"
#include "tfe_xr_mesh.h"
int main(int argc,char**argv){ int id=atoi(argv[1]); std::vector<SVtx> v; std::vector<uint32_t> ix; float mz[3]; const float g[3]={0,-0.04f,0.015f};
 auto t0=std::chrono::steady_clock::now();
 weapons_buildSmooth(id,v,ix,mz,g);
 double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
 FILE*f=fopen(argv[2],"w"); const float L[3]={0.35f,0.85f,0.4f}; float ln=sqrtf(L[0]*L[0]+L[1]*L[1]+L[2]*L[2]);
 for(size_t i=0;i<ix.size();i++){ const SVtx& a=v[ix[i]]; float d=(a.nx*L[0]+a.ny*L[1]+a.nz*L[2])/ln; if(d<0)d=0; float k=a.emis>0.5f?1.0f:0.35f+0.65f*d; 
  float sp=powf(fmaxf(0,d),24)*a.spec; fprintf(f,"%f %f %f %f %f %f 1 0\n",a.x,a.y,a.z,fminf(1,a.r*k+sp),fminf(1,a.g*k+sp),fminf(1,a.b*k+sp)); }
 fprintf(f,"END\n"); fclose(f); printf("id %d verts %zu tris %zu  %.0f ms\n",id,v.size(),ix.size()/3,ms); }
