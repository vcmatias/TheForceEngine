#include "tfe_xr_handskin.h"
#include "tfe_xr_handmesh.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <random>
static void joints(const float* tp,float ci,float co,float J[25][3]){ float b[25][16]; handskin_setThumbOverride(tp); handskin_pose(ci,co,0.5f,b); handskin_setThumbOverride(nullptr); float A[16]; handskin_align(-1,A);
 for(int j=0;j<25;j++){ const float* r=c_hmRest[j]; float p[3]; for(int i=0;i<3;i++) p[i]=b[j][i]*r[0]+b[j][4+i]*r[1]+b[j][8+i]*r[2]+b[j][12+i]; for(int i=0;i<3;i++) J[j][i]=A[i]*p[0]+A[4+i]*p[1]+A[8+i]*p[2]+A[12+i]; } }
int main(int c,char**v){ float T[3]={(float)atof(v[1]),(float)atof(v[2]),(float)atof(v[3])}; float zf=atof(v[4]); float ci=atof(v[5]),co=atof(v[6]);
 std::mt19937 rng(1); std::normal_distribution<float> N(0,1); float best[5]={0,0,0,0.2f,0.2f}; float bc=1e9;
 auto cost=[&](const float*p){ float J[25][3]; joints(p,ci,co,J); float e=0; for(int i=0;i<3;i++) e+=(J[4][i]-T[i])*(J[4][i]-T[i]);
  // thumb segments must stay on the eye side of the front face (z > zf), pad near it
  for(int j=2;j<4;j++){ float d=zf+0.006f-J[j][2]; if(d>0) e+=d*d*4; }
  for(int k=0;k<5;k++) e+=1e-5f*p[k]*p[k]; if(p[3]<0||p[4]<0) e+=0.01f; return e; };
 bc=cost(best);
 for(int it=0;it<200000;it++){ float s=it<100000?0.3f:0.05f; float q[5]; for(int k=0;k<5;k++) q[k]=best[k]+N(rng)*s; float cc=cost(q); if(cc<bc){bc=cc; for(int k=0;k<5;k++) best[k]=q[k];} }
 float J[25][3]; joints(best,ci,co,J);
 printf("p %.3f %.3f %.3f %.3f %.3f cost %g\n",best[0],best[1],best[2],best[3],best[4],bc);
 for(int j=1;j<5;j++) printf("th%d %.3f %.3f %.3f\n",j,J[j][0],J[j][1],J[j][2]);
}
