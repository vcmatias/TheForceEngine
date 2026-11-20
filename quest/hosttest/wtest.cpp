#include <vector>
#include <cstdio>
#include <cstdlib>
#include "tfe_xr_weapons.h"
int main(int argc,char**argv){ int id=atoi(argv[1]); std::vector<float> v; float mz[3]; const float g[3]={0,-0.04f,0.015f};
 weapons_buildModel(id,v,mz,g); FILE*f=fopen(argv[2],"w"); for(size_t i=0;i<v.size();i+=10) fprintf(f,"%f %f %f %f %f %f %f %f\n",v[i],v[i+1],v[i+2],v[i+5],v[i+6],v[i+7],v[i+8],v[i+9]); fprintf(f,"END\n"); fclose(f); printf("id %d verts %zu muzzle %.3f %.3f %.3f\n",id,v.size()/10,mz[0],mz[1],mz[2]); }
