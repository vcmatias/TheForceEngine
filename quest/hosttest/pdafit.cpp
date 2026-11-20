// Fits the PDA grip: thumb tip just outside the bottom-left screen corner, fingers behind the case, no penetration.
#include "tfe_xr_handskin.h"
#include "tfe_xr_handmesh.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <random>
#include <vector>
#include <algorithm>
const float SW=0.17f, SH=SW*0.625f, PW=SW+0.026f;
// case in pdaFrame coords
const float CX0=-PW/2, CX1=PW/2, CY0=-SH/2-0.028f, CY1=SH/2+0.012f, CZ0=-0.009f, CZ1=0.005f;
static void joints(const float* tp,float ci,float co,float J[25][3]){ float b[25][16]; handskin_setThumbOverride(tp); handskin_pose(ci,co,0.5f,b); handskin_setThumbOverride(nullptr); float A[16]; handskin_align(-1,A);
 for(int j=0;j<25;j++){ const float* r=c_hmRest[j]; float p[3]; for(int i=0;i<3;i++) p[i]=b[j][i]*r[0]+b[j][4+i]*r[1]+b[j][8+i]*r[2]+b[j][12+i]; for(int i=0;i<3;i++) J[j][i]=A[i]*p[0]+A[4+i]*p[1]+A[8+i]*p[2]+A[12+i]; } }
static float g_terr=0;
static void fitThumb(const float T[3],float ci,float co,float* best){
 std::mt19937 rng(1); std::normal_distribution<float> N(0,1); float tgt[3]={T[0]+CX0+0.013f-0.004f,T[1]-SH/2-0.004f,T[2]+CZ1+0.007f};
 float b0[5]={0.27f,-0.24f,0.03f,1.4f,0.6f}; for(int k=0;k<5;k++) best[k]=b0[k];
 auto cost=[&](const float*p){ float J[25][3]; joints(p,ci,co,J); float e=0; for(int i=0;i<3;i++) e+=(J[4][i]-tgt[i])*(J[4][i]-tgt[i]);
  for(int j=2;j<4;j++){ float d=tgt[2]-J[j][2]; if(d>0) e+=d*d*4; } for(int k=0;k<5;k++) e+=1e-5f*p[k]*p[k]; if(p[3]<0||p[4]<0) e+=0.01f; return e; };
 float bc=cost(best); for(int it=0;it<6000;it++){ float s=it<10000?0.25f:0.04f; float q[5]; for(int k=0;k<5;k++) q[k]=best[k]+N(rng)*s; float cc=cost(q); if(cc<bc){bc=cc; for(int k=0;k<5;k++) best[k]=q[k];} } float J[25][3]; joints(best,ci,co,J); g_terr=sqrtf((J[4][0]-tgt[0])*(J[4][0]-tgt[0])+(J[4][1]-tgt[1])*(J[4][1]-tgt[1])+(J[4][2]-tgt[2])*(J[4][2]-tgt[2])); }
static int g_below=0;
static void eval(const float T[3],float ci,float co,const float* tp,int* pen,float* gap,float* maxPen){ g_below=0;
 float I[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; std::vector<SVtx> v; std::vector<uint32_t> ix; handskin_setThumbOverride(tp); handskin_cpu(I,-1,ci,co,0.5f,v,ix); handskin_setThumbOverride(nullptr);
 *pen=0; *gap=1; *maxPen=0;
 for(auto&a:v){ float x=a.x-T[0],y=a.y-T[1],z=a.z-T[2];
  if(x>CX0&&x<CX1&&y>CY0&&y<CY1&&z>CZ0&&z<CZ1){ (*pen)++; float d=std::min({x-CX0,CX1-x,y-CY0,CY1-y,z-CZ0,CZ1-z}); *maxPen=std::max(*maxPen,d);} 
  if(x>CX0+0.01f&&x<CX1&&y>CY0&&y<CY1&&z<=CZ0) *gap=std::min(*gap,CZ0-z);
  if(x>CX0&&y<CY0&&z<CZ1) g_below++; }
}
int main(int c,char**v){
 if(c>5){ float T[3]={(float)atof(v[1]),(float)atof(v[2]),(float)atof(v[3])}; float ci=atof(v[4]),co=atof(v[5]); float tp[5]; fitThumb(T,ci,co,tp); int pen; float gap,mp; eval(T,ci,co,tp,&pen,&gap,&mp);
  printf("T %.3f %.3f %.3f ci %.2f co %.2f thumb %.3f %.3f %.3f %.3f %.3f pen %d max %.4f gap %.4f below %d\n",T[0],T[1],T[2],ci,co,tp[0],tp[1],tp[2],tp[3],tp[4],pen,mp,gap,g_below); return 0; }
 float bestS=1e9;
 for(float ci=0.3f;ci<=0.8f;ci+=0.1f) for(float co=0.3f;co<=0.9f;co+=0.1f) for(float tx=0.12f;tx<=0.17f;tx+=0.005f) for(float ty=0.02f;ty<=0.07f;ty+=0.005f) for(float tz=-0.065f;tz<=-0.035f;tz+=0.005f){
  float T[3]={tx,ty,tz}; float tp0[5]={0.27f,-0.24f,0.03f,1.4f,0.6f}; int pen; float gap,mp; eval(T,ci,co,tp0,&pen,&gap,&mp);
  if(g_below>20||gap>0.004f) continue;
  float tp[5]; fitThumb(T,ci,co,tp); eval(T,ci,co,tp,&pen,&gap,&mp);
  float s=pen*0.05f+mp*3000.0f+gap*1500.0f+g_below*0.2f+g_terr*1500.0f; if(s<bestS){bestS=s; printf("%s %.3f %.3f %.3f %.2f %.2f  thumb %.3f,%.3f,%.3f,%.3f,%.3f pen %d max %.4f gap %.4f below %d terr %.4f\n","T",tx,ty,tz,ci,co,tp[0],tp[1],tp[2],tp[3],tp[4],pen,mp,gap,g_below,g_terr); fflush(stdout);} }
}
