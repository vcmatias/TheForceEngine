// Writes the left hand holding the PDA (pose params) and the PDA case as OBJ in the case frame.
//   pdaobj tx ty tz ci co t0 t1 t2 t3 t4 out.obj [case.obj]
#include "tfe_xr_handskin.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int c,char**v){
 float T[3]={(float)atof(v[1]),(float)atof(v[2]),(float)atof(v[3])}; float ci=atof(v[4]),co=atof(v[5]); float tp[5]; for(int k=0;k<5;k++) tp[k]=atof(v[6+k]);
 float I[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; std::vector<SVtx> vv; std::vector<uint32_t> ix; handskin_setThumbOverride(tp); handskin_cpu(I,-1,ci,co,0.5f,vv,ix);
 FILE* f=fopen(v[11],"w"); for(auto&a:vv) fprintf(f,"v %.5f %.5f %.5f\n",a.x-T[0],a.y-T[1],a.z-T[2]); for(size_t i=0;i<ix.size();i+=3) fprintf(f,"f %u %u %u\n",ix[i]+1,ix[i+1]+1,ix[i+2]+1); fclose(f);
}
