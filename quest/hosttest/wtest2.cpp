#include <vector>
#include <cstdio>
#include <cstdlib>
#include "tfe_xr_weapons.h"
int main(int argc,char**argv){ int id=atoi(argv[1]); std::vector<float> v; float mz[3]; const float g[3]={0,-0.04f,0.015f};
 WeaponView wv={}; FILE* sf=fopen(argv[3],"rb"); int hdr[4]; fread(hdr,4,4,sf); wv.w=hdr[0]; wv.h=hdr[1]; wv.x0=hdr[2]; wv.y0=hdr[3]; fread(wv.pose,4,6,sf);
 std::vector<unsigned char> px(wv.w*wv.h*4); fread(px.data(),1,px.size(),sf); fclose(sf); wv.rgba=px.data();
 weapons_buildModel(id,v,mz,g,&wv); FILE*f=fopen(argv[2],"w"); for(size_t i=0;i<v.size();i+=10) fprintf(f,"%f %f %f %f %f %f %f %f\n",v[i],v[i+1],v[i+2],v[i+5],v[i+6],v[i+7],v[i+8],v[i+9]); fprintf(f,"END\n"); fclose(f); }
