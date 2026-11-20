// Fits the right hand on a palm-held object (tools/grip/<name>.sdf, kit frame):
//   sphere: thermal detonator in a power grip (the ball in the palm, the fingers wrapped round it, the
//           thumb on the arming plate on top, like a grenade held with the thumb on its lever)
//   disc:   mine in a frisbee power grip (thumb on the top face, the four fingers under the rim, the
//           rear edge in the thumb web)
// Same hand placement as hosttest/gripfit.cpp (kit frame), plus a free roll (rx) and per-joint fingers.
//   palmfit <name.sdf> sphere|disc <out prefix> [iterations] [seed]
#include "tfe_xr_handskin.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>
struct M4 { float m[16]; };
static M4 ident(){ M4 r; memset(r.m,0,64); r.m[0]=r.m[5]=r.m[10]=r.m[15]=1; return r; }
static M4 mul(const M4&a,const M4&b){ M4 r; for(int c=0;c<4;c++) for(int rr=0;rr<4;rr++){ float s=0; for(int k=0;k<4;k++) s+=a.m[k*4+rr]*b.m[c*4+k]; r.m[c*4+rr]=s;} return r; }
static M4 T(float x,float y,float z){ M4 r=ident(); r.m[12]=x; r.m[13]=y; r.m[14]=z; return r; }
static M4 rotX(float a){ M4 r=ident(); float c=cosf(a),s=sinf(a); r.m[5]=c; r.m[6]=s; r.m[9]=-s; r.m[10]=c; return r; }
static M4 rotY(float a){ M4 r=ident(); float c=cosf(a),s=sinf(a); r.m[0]=c; r.m[2]=-s; r.m[8]=s; r.m[10]=c; return r; }
static M4 rotZ(float a){ M4 r=ident(); float c=cosf(a),s=sinf(a); r.m[0]=c; r.m[1]=s; r.m[4]=-s; r.m[5]=c; return r; }
const float HT=-0.35f; const float GIW[3]={0.0f,-0.040f,0.015f};
static int NX,NY,NZ; static float X0,Y0,Z0,ST; static std::vector<float> G;
static float sdf(float x,float y,float z){ float fx=(x-X0)/ST,fy=(y-Y0)/ST,fz=(z-Z0)/ST; if(fx<0||fy<0||fz<0||fx>=NX-1||fy>=NY-1||fz>=NZ-1) return 0.05f;
 int ix=(int)fx,iy=(int)fy,iz=(int)fz; float ax=fx-ix,ay=fy-iy,az=fz-iz; auto g=[&](int a,int b,int c){ return G[((size_t)(iz+c)*NY+(iy+b))*NX+(ix+a)]; };
 float c00=g(0,0,0)*(1-ax)+g(1,0,0)*ax, c10=g(0,1,0)*(1-ax)+g(1,1,0)*ax, c01=g(0,0,1)*(1-ax)+g(1,0,1)*ax, c11=g(0,1,1)*(1-ax)+g(1,1,1)*ax;
 return (c00*(1-ay)+c10*ay)*(1-az)+(c01*(1-ay)+c11*ay)*az; }
enum { P_DX,P_DY,P_DZ,P_RY,P_RZ,P_RX,P_M0,P_M1,P_M2,P_M3,P_PIP,P_DIP,P_SPR,P_T0,P_T1,P_T2,P_T3,P_T4,NP };
static std::vector<int> g_cls; static bool g_disc=false;
static void pose(const float* p,std::vector<SVtx>& v,std::vector<uint32_t>& ix){
 M4 Hrel=mul(mul(T(0,-0.055f*cosf(HT),-0.055f*sinf(HT)),rotX(HT)),T(0.020f,0.0f,0.060f));
 M4 H=mul(mul(mul(T(-GIW[0],-GIW[1],-GIW[2]),Hrel),T(p[P_DX],p[P_DY],p[P_DZ])),mul(mul(rotY(p[P_RY]),rotZ(p[P_RZ])),rotX(p[P_RX])));
 float fj[7]={p[P_M0],p[P_M1],p[P_M2],p[P_M3],p[P_PIP],p[P_DIP],p[P_SPR]}; float tp[5]={p[P_T0],p[P_T1],p[P_T2],p[P_T3],p[P_T4]};
 handskin_setFingerJoints(fj); handskin_setThumbOverride(tp); v.clear(); ix.clear(); handskin_cpu(H.m,1.0f,0.3f,0.8f,0.8f,v,ix); handskin_setFingerJoints(nullptr); handskin_setThumbOverride(nullptr); }
static float g_info[32];
static float cost(const float* p){
 std::vector<SVtx> v; std::vector<uint32_t> ix; pose(p,v,ix);
 float pen=0,maxPen=0; float mins[6][3]; for(auto&a:mins) for(float&b:a) b=1;
 double tip[6][3]={{0}}; int tn[6]={0};
 for(size_t i=0;i<v.size();i+=2){ float d=sdf(v[i].x,v[i].y,v[i].z); int c=g_cls[i]/10,s=g_cls[i]%10;
  if(d<-0.0006f){ float q=-0.0006f-d; pen+=q*q; maxPen=std::max(maxPen,q); }
  if(s<3) mins[c][s]=std::min(mins[c][s],d);
  if(s==2){ tip[c][0]+=v[i].x; tip[c][1]+=v[i].y; tip[c][2]+=v[i].z; tn[c]++; } }
 for(int c=1;c<6;c++) if(tn[c]) for(int k=0;k<3;k++) tip[c][k]/=tn[c];
 float cst=pen*4e6f+maxPen*maxPen*4e5f;
 auto contact=[&](float m,float w){ float e=m-0.0005f; return w*(e>0?e*e:e*e*0.3f); };
 auto d3=[&](const double* a,const float* b){ float s=0; for(int k=0;k<3;k++){ float d=float(a[k])-b[k]; s+=d*d; } return s; };
 if(!g_disc)
 {
  // Thermal: sphere (0, 0.006, -0.026) R 0.031, arming plate on top.
  const float C[3]={0.0f,0.006f,-0.026f};
  for(int f=2;f<=5;f++){ cst+=contact(mins[f][2],6e3f); cst+=contact(mins[f][1],6e3f); cst+=contact(mins[f][0],3e3f); }   // wrapped round the ball
  cst+=contact(mins[0][0],6e3f);                     // ball in the palm
  const float thumbT[3]={-0.004f,0.006f+0.031f+0.009f,-0.026f+0.006f};   // pad on the arming plate
  cst+=d3(tip[1],thumbT)*1e4f; cst+=contact(mins[1][2],4e3f);
  // the fingertips stay below the equator on the far side (a wrap, not a pinch from above)
  for(int f=2;f<=5;f++){ float y=float(tip[f][1]); if(y>C[1]+0.006f) cst+=(y-C[1]-0.006f)*(y-C[1]-0.006f)*2e4f; }
  for(int k=P_M0;k<=P_M3;k++){ float e=p[k]-0.8f; cst+=e*e*2.0f; } cst+=(p[P_PIP]-1.0f)*(p[P_PIP]-1.0f)*3.0f+(p[P_DIP]-0.6f)*(p[P_DIP]-0.6f)*2.0f;
 }
 else
 {
  // Mine: disc centre (0, 0.014, -0.024) R 0.046, top face y 0.0245 (bars 0.027), bottom face y 0.0035.
  const float C[3]={0.0f,0.014f,-0.024f}, Rd=0.046f;
  for(int f=2;f<=5;f++){ cst+=contact(mins[f][2],6e3f); cst+=contact(mins[f][1],4e3f);
   float y=float(tip[f][1]); if(y>0.0030f) cst+=(y-0.0030f)*(y-0.0030f)*3e5f;                       // tips under the bottom face
   float dx=float(tip[f][0])-C[0], dz=float(tip[f][2])-C[2]; float r=sqrtf(dx*dx+dz*dz); if(r>Rd-0.010f) cst+=(r-Rd+0.010f)*(r-Rd+0.010f)*4e4f;   // under the disc, past the rim
  }
  cst+=contact(mins[0][0],4e3f);                     // rear rim against the palm / web
  const float thumbT[3]={-0.010f,0.0245f+0.008f,C[2]+Rd-0.022f};   // thumb pad flat on the top face, near the rear edge
  cst+=d3(tip[1],thumbT)*1e4f; cst+=contact(mins[1][2],5e3f); cst+=contact(mins[1][1],2e3f);
  for(int k=P_M0;k<=P_M3;k++){ float e=p[k]-0.9f; cst+=e*e*0.3f; } cst+=(p[P_PIP]-0.7f)*(p[P_PIP]-0.7f)*0.3f+(p[P_DIP]-0.4f)*(p[P_DIP]-0.4f)*0.3f;
  { float a=p[P_T3]-0.2f,b=p[P_T4]-0.3f; cst+=(a*a+b*b)*0.5f; }   // a flat thumb on the face
 }
 for(int k=P_M0;k<P_M3;k++){ float e=p[k+1]-p[k]; cst+=e*e*0.8f; }
 if(p[P_DIP]>p[P_PIP]+0.2f) cst+=(p[P_DIP]-p[P_PIP]-0.2f)*(p[P_DIP]-p[P_PIP]-0.2f)*3.0f;
 if(!g_disc){ /* the ball moves into the palm at runtime (object offset): no limit on the offset */ } else { float dd=sqrtf(p[P_DX]*p[P_DX]+p[P_DY]*p[P_DY]+p[P_DZ]*p[P_DZ]); if(dd>0.035f) cst+=(dd-0.035f)*(dd-0.035f)*1e5f; }
 cst+=(p[P_RY]*p[P_RY]+p[P_RZ]*p[P_RZ]+p[P_RX]*p[P_RX])*0.4f;
 g_info[0]=pen; g_info[1]=maxPen; for(int c=0;c<6;c++) g_info[2+c]=std::min(std::min(mins[c][0],mins[c][1]),mins[c][2]);
 for(int c=1;c<6;c++) for(int k=0;k<3;k++) g_info[8+(c-1)*3+k]=float(tip[c][k]);
 return cst; }
int main(int argc,char**argv){
 FILE* f=fopen(argv[1],"rb"); if(!f) return 1; fread(&NX,4,1,f); fread(&NY,4,1,f); fread(&NZ,4,1,f); fread(&X0,4,1,f); fread(&Y0,4,1,f); fread(&Z0,4,1,f); fread(&ST,4,1,f);
 G.resize((size_t)NX*NY*NZ); fread(G.data(),4,G.size(),f); fclose(f);
 g_disc=!strcmp(argv[2],"disc"); const char* out=argv[3]; int iters=argc>4?atoi(argv[4]):40000; int seed=argc>5?atoi(argv[5]):0;
 const auto& hv=handskin_vertices(); g_cls.resize(hv.size());
 for(size_t i=0;i<hv.size();i++){ int j=0; float bw=-1; for(int k=0;k<4;k++) if(hv[i].w[k]>bw){bw=hv[i].w[k]; j=(int)hv[i].j[k];}
  int c,s; if(j==0){c=0;s=0;} else if(j<=4){c=1;s=(j==1?0:(j==2?1:2));} else { int fi=(j-5)/5, k=(j-5)%5; if(k==0){c=0;s=0;} else {c=2+fi; s=std::min(k-1,2);} } g_cls[i]=c*10+s; }
 float best[NP];
 if(!g_disc){ const float b0[NP]={0.0f,0.040f,-0.035f,0.0109f,0.0492f,0.0f, 1.0f,1.0f,1.0f,1.0f,0.9f,0.6f,0.05f, 0.5336f,0.4896f,-0.9026f,0.2983f,0.8f}; memcpy(best,b0,sizeof best); }
 else { const float b0[NP]={0.0060f,0.0037f,-0.0015f,-0.0616f,-0.0990f,0.0f, 0.9f,0.9f,0.9f,0.9f,0.7f,0.4f,0.05f, 0.30f,0.21f,-0.30f,0.2f,0.3f}; memcpy(best,b0,sizeof best); }
 if(getenv("PF_INIT")){ const char* q=getenv("PF_INIT"); for(int k=0;k<NP;k++){ char* e; best[k]=strtof(q,&e); q=e; while(*q==','||*q==' ') q++; } }
 static const float lo[NP]={-0.09f,-0.09f,-0.09f,-0.8f,-0.8f,-0.8f, 0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,-0.05f, -1.3f,-1.3f,-1.3f,0.0f,0.0f};
 static const float hi[NP]={ 0.09f, 0.09f, 0.09f, 0.8f, 0.8f, 0.8f, 1.6f,1.6f,1.6f,1.6f,1.6f,1.2f, 0.20f,  1.3f, 1.3f, 1.3f,1.6f,1.6f};
 std::mt19937 rng(5+seed*733); std::normal_distribution<float> N(0,1);
 if(seed>0){ for(int k=0;k<3;k++) best[k]+=N(rng)*0.010f; for(int k=P_RY;k<=P_RX;k++) best[k]+=N(rng)*0.25f; for(int k=P_T0;k<=P_T4;k++) best[k]+=N(rng)*0.3f; }
 for(int k=0;k<NP;k++) best[k]=std::min(hi[k],std::max(lo[k],best[k]));
 float bc=cost(best);
 for(int it=0;it<iters;it++){ float fr=1.0f-float(it)/iters, sc=0.1f+0.9f*fr; float q[NP]; memcpy(q,best,sizeof q); int nk=1+rng()%3;
  for(int n=0;n<nk;n++){ int k=rng()%NP; float s=(k<=P_DZ)?0.004f:(k<=P_RX?0.05f:0.08f); q[k]+=N(rng)*s*sc; }
  for(int k=0;k<NP;k++) q[k]=std::min(hi[k],std::max(lo[k],q[k]));
  float c=cost(q); if(c<bc){ bc=c; memcpy(best,q,sizeof q);} }
 cost(best);
 printf("cost %.4f pen %.2e max %.4f | min palm %.4f thumb %.4f idx %.4f mid %.4f ring %.4f lit %.4f | thumb tip %.4f %.4f %.4f | tips y %.4f %.4f %.4f %.4f\n",bc,g_info[0],g_info[1],g_info[2],g_info[3],g_info[4],g_info[5],g_info[6],g_info[7],g_info[8],g_info[9],g_info[10],g_info[12],g_info[15],g_info[18],g_info[21]);
 printf("ROW"); for(int k=0;k<NP;k++) printf(" %.4f",best[k]); printf("\n");
 std::vector<SVtx> v; std::vector<uint32_t> ix; pose(best,v,ix);
 char fn[512]; snprintf(fn,sizeof fn,"%s_rest.obj",out); FILE* fo=fopen(fn,"w"); for(auto&a:v) fprintf(fo,"v %.5f %.5f %.5f\n",a.x,a.y,a.z); for(size_t i=0;i<ix.size();i+=3) fprintf(fo,"f %u %u %u\n",ix[i]+1,ix[i+1]+1,ix[i+2]+1); fclose(fo);
}
