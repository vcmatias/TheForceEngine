#include "tfe_xr_handskin.h"
#include "tfe_xr_handmesh.h"
#include <cstdio>
#include <cstdlib>
int main(int c,char**v){ float b[25][16]; handskin_pose(atof(v[1]),atof(v[2]),atof(v[3]),b); float A[16]; handskin_align(-1,A);
 const char* nm[25]={"wrist","th0","th1","th2","thTip","ix0","ix1","ix2","ix3","ixTip","md0","md1","md2","md3","mdTip","rg0","rg1","rg2","rg3","rgTip","pk0","pk1","pk2","pk3","pkTip"};
 for(int j=0;j<25;j++){ const float* r=c_hmRest[j]; float p[3]; for(int i=0;i<3;i++) p[i]=b[j][i]*r[0]+b[j][4+i]*r[1]+b[j][8+i]*r[2]+b[j][12+i];
  float q[3]; for(int i=0;i<3;i++) q[i]=A[i]*p[0]+A[4+i]*p[1]+A[8+i]*p[2]+A[12+i]; printf("%-6s %7.3f %7.3f %7.3f\n",nm[j],q[0],q[1],q[2]); } }
