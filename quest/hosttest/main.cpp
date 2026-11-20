#include "tfe_xr_hands.h"
#include <cstdio>
#include <cmath>
#include <math.h>
#include <cstdlib>
#include <vector>
static FILE* g_out;
void hostDraw(const void* v, int n, int){ const float* f=(const float*)v; for(int i=0;i<n;i++){ fprintf(g_out,"%f %f %f %f %f %f %f %f\n",f[i*10],f[i*10+1],f[i*10+2],f[i*10+5],f[i*10+6],f[i*10+7],f[i*10+8],f[i*10+9]); } fprintf(g_out,"END\n"); }
int main(int argc,char**argv){
  int wpn=atoi(argv[1]); float grip=atof(argv[2]); g_out=fopen(argv[3],"w");
  hands_init();
  { FILE* f=fopen("hand.rgba","rb"); int wh[2]; fread(wh,4,2,f); std::vector<unsigned char> b(wh[0]*wh[1]*4); fread(b.data(),1,b.size(),f); fclose(f); hands_setTexture(b.data(),wh[0],wh[1]); }
  for(int id=2;id<10;id++){ char n[32]; sprintf(n,"spr%d.rgba",id); FILE* f=fopen(n,"rb"); if(!f) continue; int wh[2]; fread(wh,4,2,f); std::vector<unsigned char> b(wh[0]*wh[1]*4); fread(b.data(),1,b.size(),f); fclose(f); hands_setWeaponSprite(id,b.data(),wh[0],wh[1]); }
  HandInput h[2]={};
  float rx=argc>5?atof(argv[5]):0, rz=argc>6?atof(argv[6]):0; // WRIST test: left hand rotation
  for(int i=0;i<2;i++){ h[i].valid=h[i].aimValid=true; XrQuaternionf q={0,0,0,1};
    if(i==0){ XrQuaternionf a={sinf(rx/2),0,0,cosf(rx/2)}, b={0,0,sinf(rz/2),cosf(rz/2)}; q={a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y, a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x, a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w, a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z}; } h[i].aimPose.orientation=q; h[i].aimPose.position={i?0.14f:-0.04f,i?-0.16f:-0.17f,i?-0.36f:-0.30f};
    h[i].pose.orientation=q; h[i].pose.position={h[i].aimPose.position.x, h[i].aimPose.position.y-0.025f, h[i].aimPose.position.z+0.055f}; h[i].grip=grip; h[i].trigger=getenv("TRIG")?atof(getenv("TRIG")):0; h[i].thumbTouch=getenv("THUMB")!=0; }
  HandsFrame f={}; f.weaponId=wpn; f.light=1; f.laser=false; f.flash=argc>4?atof(argv[4]):0;
  if(getenv("WHEEL")){ f.wheel=true; f.wheelOwned=0x3ff; f.wheelSel=2; f.wheelPose.orientation={0,0,0,1}; f.wheelPose.position=h[1].pose.position; }
  if(getenv("PDAP")){ float p[13]; sscanf(getenv("PDAP"),"%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",p,p+1,p+2,p+3,p+4,p+5,p+6,p+7,p+8,p+9,p+10,p+11,p+12); hands_setPdaParams(p); }
  if(getenv("NORIGHT")){ h[1].valid=h[1].aimValid=false; }
  if(getenv("PDA")){ f.pda=hands_pdaScreen(h[0],&f.pdaPose); printf("pda %f %f %f %f %f %f %f\n",f.pdaPose.position.x,f.pdaPose.position.y,f.pdaPose.position.z,f.pdaPose.orientation.x,f.pdaPose.orientation.y,f.pdaPose.orientation.z,f.pdaPose.orientation.w); }
  if(getenv("STYLE")) hands_setWeaponStyle(atoi(getenv("STYLE")));
  XrPosef eye={{0,0,0,1},{0,0,0}}; XrFovf fov={-0.9f,0.9f,0.9f,-0.9f};
  for(int k=0;k<10;k++) hands_draw(eye,fov,3.3f,h,f,0.1f);
  fclose(g_out);
  { XrPosef face, holo; XrVector3f head={0,0,0}; if(hands_watch(h[0],&face,&holo,head)) printf("watch %.3f %.3f %.3f q %.3f %.3f %.3f %.3f\n",face.position.x,face.position.y,face.position.z,face.orientation.x,face.orientation.y,face.orientation.z,face.orientation.w); }
  XrPosef wp; if(hands_wristPanel(h[0],-1,&wp)) printf("wrist L pos %.3f %.3f %.3f q %.3f %.3f %.3f %.3f\n",wp.position.x,wp.position.y,wp.position.z,wp.orientation.x,wp.orientation.y,wp.orientation.z,wp.orientation.w);
}
