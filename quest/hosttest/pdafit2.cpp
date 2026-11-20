// Fits the left hand holding the PDA by its left edge (new case with the configuration-screen bezel):
// web of the hand on the left edge, fingers flat on the back, thumb resting on the left bezel (off the
// screen), pointing up and in, no penetration. Prints the parameters and writes the posed hand (case frame).
//   pdafit2 out.obj [iterations] [seed]
#include "tfe_xr_handskin.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>
// Case in the PDA frame (screen center at the origin), scale 1: see tfe_xr_hands.cpp pdaCase().
const float L=-0.1016f, R=0.0993f, B=-0.0683f, Tp=0.0587f, ZB=-0.0050f, ZF=0.0070f, WINL=-0.0850f;
static float boxSdf(float x,float y,float z){ const float r=0.002f; float cx=(L+R)/2,cy=(B+Tp)/2,cz=(ZB+ZF)/2; float hx=(R-L)/2-r,hy=(Tp-B)/2-r,hz=(ZF-ZB)/2-r;
 float qx=fabsf(x-cx)-hx,qy=fabsf(y-cy)-hy,qz=fabsf(z-cz)-hz; float ox=std::max(qx,0.f),oy=std::max(qy,0.f),oz=std::max(qz,0.f); return sqrtf(ox*ox+oy*oy+oz*oz)+std::min(std::max(qx,std::max(qy,qz)),0.f)-r; }
enum { P_TX,P_TY,P_TZ,P_CI,P_C1,P_C2,P_C3,P_T0,P_T1,P_T2,P_T3,P_T4,NP };
static std::vector<int> g_cls;
static void pose(const float* p,std::vector<SVtx>& v){ float c3[3]={p[P_C1],p[P_C2],p[P_C3]}; float tp[5]={p[P_T0],p[P_T1],p[P_T2],p[P_T3],p[P_T4]};
 float I[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; std::vector<uint32_t> ix; v.clear(); handskin_setFingerOverride(c3); handskin_setThumbOverride(tp); handskin_cpu(I,-1,p[P_CI],0.8f,0.5f,v,ix); handskin_setFingerOverride(nullptr); handskin_setThumbOverride(nullptr);
 for(auto&a:v){ a.x-=p[P_TX]; a.y-=p[P_TY]; a.z-=p[P_TZ]; } }
static float g_info[16];
static float cost(const float* p){
 std::vector<SVtx> v; pose(p,v);
 float pen=0,maxPen=0; float mins[6][3]; for(auto&a:mins) for(float&b:a) b=1;
 double tc[3]={0,0,0}; int tn=0; double t1[3]={0,0,0}; int t1n=0; float tipX[6]; for(float&x:tipX) x=-1; float minZfing=1;
 for(size_t i=0;i<v.size();i+=2){ float d=boxSdf(v[i].x,v[i].y,v[i].z); int c=g_cls[i]/10,s=g_cls[i]%10;
  if(d<-0.0005f){ float q=-0.0005f-d; pen+=q*q; maxPen=std::max(maxPen,q); }
  if(s<3) mins[c][s]=std::min(mins[c][s],d);
  if(c==1&&s==2){ tc[0]+=v[i].x; tc[1]+=v[i].y; tc[2]+=v[i].z; tn++; }
  if(c==1&&s==1){ t1[0]+=v[i].x; t1[1]+=v[i].y; t1[2]+=v[i].z; t1n++; }
  if(c>=2&&s==2) tipX[c]=std::max(tipX[c],v[i].x);
 }
 float cst=pen*4e6f+maxPen*maxPen*4e5f;
 auto contact=[&](float m,float w){ float e=m-0.0006f; return w*(e>0?e*e:e*e*0.3f); };
 for(int f=2;f<=5;f++){ cst+=contact(mins[f][2],6e3f); cst+=contact(mins[f][1],1.5e3f); }   // finger pads on the back
 cst+=contact(std::min(mins[0][0],mins[1][0]),3e3f);                                   // web / palm on the left edge
 cst+=contact(mins[1][2],6e3f);                                                         // thumb pad on the bezel
 float thumbErr=1; if(tn&&t1n){ float tx=tc[0]/tn,ty=tc[1]/tn,tz=tc[2]/tn; const float tt[3]={L+0.0095f,B+0.40f*(Tp-B),ZF+0.0065f};
   thumbErr=sqrtf((tx-tt[0])*(tx-tt[0])+(ty-tt[1])*(ty-tt[1])+(tz-tt[2])*(tz-tt[2])); cst+=thumbErr*thumbErr*8e3f;
   if(tx>WINL-0.002f) cst+=(tx-WINL+0.002f)*(tx-WINL+0.002f)*1e5f;                    // the thumb stays on the bezel
   float px=t1[0]/t1n,py=t1[1]/t1n; if(px>tx) cst+=(px-tx)*(px-tx)*2e4f; if(py>ty+0.004f) cst+=(py-ty-0.004f)*(py-ty-0.004f)*2e4f;   // pointing in and up, not crossed
   g_info[0]=tx; g_info[1]=ty; g_info[2]=tz; g_info[3]=px; g_info[4]=py; }
 for(int f=2;f<=5;f++){ if(tipX[f]>L+0.075f) cst+=(tipX[f]-L-0.075f)*(tipX[f]-L-0.075f)*4e3f; }   // fingers on the left part of the back
 { float d[3]={p[P_C1]-p[P_CI],p[P_C2]-p[P_C1],p[P_C3]-p[P_C2]}; for(float x:d) cst+=x*x*0.5f; }     // similar finger curls
 g_info[5]=pen; g_info[6]=maxPen; g_info[7]=thumbErr; for(int f=0;f<6;f++) g_info[8+f]=std::min(std::min(mins[f][0],mins[f][1]),mins[f][2]);
 return cst; }
int main(int argc,char**argv){
 const auto& hv=handskin_vertices(); g_cls.resize(hv.size());
 for(size_t i=0;i<hv.size();i++){ int j=0; float bw=-1; for(int k=0;k<4;k++) if(hv[i].w[k]>bw){bw=hv[i].w[k]; j=(int)hv[i].j[k];}
  int c,s; if(j==0){c=0;s=0;} else if(j<=4){c=1;s=(j==1?0:(j==2?1:2));} else { int fi=(j-5)/5, k=(j-5)%5; if(k==0){c=0;s=0;} else {c=2+fi; s=std::min(k-1,2);} } g_cls[i]=c*10+s; }
 int iters=argc>2?atoi(argv[2]):30000; int seed=argc>3?atoi(argv[3]):0;
 float best[NP]={0.150f,0.060f,-0.055f, 0.40f,0.80f,0.80f,0.80f, -0.677f,-0.509f,-0.542f,0.689f,0.579f};
 if(getenv("PDA_INIT")){ const char* q=getenv("PDA_INIT"); for(int k=0;k<NP;k++){ char* e; best[k]=strtof(q,&e); q=e; while(*q==' '||*q==',') q++; } }
 static const float lo[NP]={0.05f,-0.05f,-0.15f, 0.0f,0.0f,0.0f,0.0f, -1.3f,-1.3f,-1.3f,0.0f,0.0f};
 static const float hi[NP]={0.25f, 0.15f, 0.05f, 1.3f,1.3f,1.3f,1.3f,  1.3f, 1.3f, 1.3f,1.6f,1.6f};
 std::mt19937 rng(11+seed*977); std::normal_distribution<float> N(0,1);
 if(seed>0){ for(int k=0;k<3;k++) best[k]+=N(rng)*0.012f; for(int k=P_T0;k<=P_T4;k++) best[k]+=N(rng)*0.3f; }
 float bc=cost(best);
 for(int it=0;it<iters;it++){ float f=1.0f-float(it)/iters, sc=0.1f+0.9f*f; float q[NP]; memcpy(q,best,sizeof q); int nk=1+rng()%3;
  for(int n=0;n<nk;n++){ int k=rng()%NP; float s=(k<=P_TZ)?0.004f:0.08f; q[k]+=N(rng)*s*sc; }
  for(int k=0;k<NP;k++) q[k]=std::min(hi[k],std::max(lo[k],q[k]));
  float c=cost(q); if(c<bc){ bc=c; memcpy(best,q,sizeof q);} }
 cost(best);
 printf("cost %.4f pen %.2e max %.4f thumbErr %.4f thumb tip %.4f %.4f %.4f prox %.4f %.4f | min d palm %.4f thumb %.4f idx %.4f mid %.4f ring %.4f lit %.4f\n",bc,g_info[5],g_info[6],g_info[7],g_info[0],g_info[1],g_info[2],g_info[3],g_info[4],g_info[8],g_info[9],g_info[10],g_info[11],g_info[12],g_info[13]);
 printf("PDA"); for(int k=0;k<NP;k++) printf(" %.4f",best[k]); printf("\n");
 std::vector<SVtx> v; pose(best,v); std::vector<SVtx> v2; float I[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; std::vector<uint32_t> ix;
 handskin_cpu(I,-1,0.4f,0.8f,0.5f,v2,ix);
 FILE* fo=fopen(argv[1],"w"); for(auto&a:v) fprintf(fo,"v %.5f %.5f %.5f\n",a.x,a.y,a.z); for(size_t i=0;i<ix.size();i+=3) fprintf(fo,"f %u %u %u\n",ix[i]+1,ix[i+1]+1,ix[i+2]+1); fclose(fo);
}
