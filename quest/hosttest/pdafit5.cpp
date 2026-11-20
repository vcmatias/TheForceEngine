// Fits the left hand holding the PDA like a book by its left edge ("cradle" hold, the usual one-hand
// tablet hold): the left edge seated in the palm and thumb web, the fingers spread flat on the back,
// the thumb pad on the left bezel off the screen. Hand rotation relative to the case is free (within
// limits), so the fingers can lie on the back instead of curling round the edge.
//   pdafit5 out.obj [iterations] [seed]   (PDA_INIT = 18 params)
#include "tfe_xr_handskin.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>
const float L=-0.1000f, R=0.1000f, B=-0.0625f, Tp=0.0625f, ZB=-0.0050f, ZF=0.0070f, WINL=-0.0856f, WINB=-0.0480f, WINT=0.0560f;
static float boxSdf(float x,float y,float z){ const float r=0.002f; float cx=(L+R)/2,cy=(B+Tp)/2,cz=(ZB+ZF)/2; float hx=(R-L)/2-r,hy=(Tp-B)/2-r,hz=(ZF-ZB)/2-r;
 float qx=fabsf(x-cx)-hx,qy=fabsf(y-cy)-hy,qz=fabsf(z-cz)-hz; float ox=std::max(qx,0.f),oy=std::max(qy,0.f),oz=std::max(qz,0.f); return sqrtf(ox*ox+oy*oy+oz*oz)+std::min(std::max(qx,std::max(qy,qz)),0.f)-r; }
enum { P_TX,P_TY,P_TZ,P_YAW,P_TILT,P_ROLL,P_M0,P_M1,P_M2,P_M3,P_PIP,P_DIP,P_SPR,P_T0,P_T1,P_T2,P_T3,P_T4,NP };
static std::vector<int> g_cls;
struct M3{ float m[9]; };
static M3 rotm(float yaw,float tilt,float roll){ // R = rotY(yaw) rotX(tilt) rotZ(roll), row major
 float cy=cosf(yaw),sy=sinf(yaw),cx=cosf(tilt),sx=sinf(tilt),cz=cosf(roll),sz=sinf(roll);
 float Y[9]={cy,0,sy, 0,1,0, -sy,0,cy}, X[9]={1,0,0, 0,cx,-sx, 0,sx,cx}, Z[9]={cz,-sz,0, sz,cz,0, 0,0,1};
 float t[9],r[9]; for(int i=0;i<3;i++)for(int j=0;j<3;j++){ t[i*3+j]=0; for(int k=0;k<3;k++) t[i*3+j]+=Y[i*3+k]*X[k*3+j]; }
 for(int i=0;i<3;i++)for(int j=0;j<3;j++){ r[i*3+j]=0; for(int k=0;k<3;k++) r[i*3+j]+=t[i*3+k]*Z[k*3+j]; } M3 o; memcpy(o.m,r,sizeof r); return o; }
static void pose(const float* p,std::vector<SVtx>& v){ float fj[7]={p[P_M0],p[P_M1],p[P_M2],p[P_M3],p[P_PIP],p[P_DIP],p[P_SPR]}; float tp[5]={p[P_T0],p[P_T1],p[P_T2],p[P_T3],p[P_T4]};
 float I[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; std::vector<uint32_t> ix; v.clear(); handskin_setFingerJoints(fj); handskin_setThumbOverride(tp); handskin_cpu(I,-1,0.f,0.f,0.5f,v,ix); handskin_setFingerJoints(nullptr); handskin_setThumbOverride(nullptr);
 const M3 r=rotm(p[P_YAW],p[P_TILT],p[P_ROLL]);
 for(auto&a:v){ float d[3]={a.x-p[P_TX],a.y-p[P_TY],a.z-p[P_TZ]}; // case frame = R^T (v - T)
  a.x=r.m[0]*d[0]+r.m[3]*d[1]+r.m[6]*d[2]; a.y=r.m[1]*d[0]+r.m[4]*d[1]+r.m[7]*d[2]; a.z=r.m[2]*d[0]+r.m[5]*d[1]+r.m[8]*d[2]; } }
static float g_info[24];
static float cost(const float* p){
 std::vector<SVtx> v; pose(p,v);
 float pen=0,maxPen=0; float mins[6][3]; for(auto&a:mins) for(float&b:a) b=1;
 double tc[3]={0,0,0}; int tn=0; double t1[3]={0,0,0}; int t1n=0; double tip[6][3]={{0}}; int tipn[6]={0}; float cover=0;
 for(size_t i=0;i<v.size();i+=2){ float d=boxSdf(v[i].x,v[i].y,v[i].z); int c=g_cls[i]/10,s=g_cls[i]%10;
  if(d<-0.0005f){ float q=-0.0005f-d; pen+=q*q; maxPen=std::max(maxPen,q); }
  if(s<3) mins[c][s]=std::min(mins[c][s],d);
  if(c==1&&s==2){ tc[0]+=v[i].x; tc[1]+=v[i].y; tc[2]+=v[i].z; tn++; }
  if(c==1&&s==1){ t1[0]+=v[i].x; t1[1]+=v[i].y; t1[2]+=v[i].z; t1n++; }
  if(c>=2&&s==2){ tip[c][0]+=v[i].x; tip[c][1]+=v[i].y; tip[c][2]+=v[i].z; tipn[c]++; }
  if(v[i].z>ZF-0.002f && v[i].x>WINL+0.001f && v[i].y>B && v[i].y<Tp){ float e=std::min(v[i].x-WINL,0.02f); cover+=e*e; }   // nothing over the screen
 }
 float cst=pen*4e6f+maxPen*maxPen*4e5f+cover*2e4f;
 auto contact=[&](float m,float w){ float e=m-0.0006f; return w*(e>0?e*e:e*e*0.3f); };
 for(int f=2;f<=5;f++){ cst+=contact(mins[f][2],6e3f); cst+=contact(mins[f][1],4e3f); cst+=contact(mins[f][0],2e3f); }   // fingers flat on the back
 cst+=contact(mins[0][0],4e3f);                                                         // palm against the left edge / back
 cst+=contact(mins[1][2],6e3f);                                                         // thumb pad on the bezel
 float tipX[6]={0},tipZ[6]={0},tipY[6]={0};
 for(int f=2;f<=5;f++){ if(!tipn[f]) continue; tipX[f]=tip[f][0]/tipn[f]; tipY[f]=tip[f][1]/tipn[f]; tipZ[f]=tip[f][2]/tipn[f];
  if(tipZ[f]>ZB-0.004f) cst+=(tipZ[f]-ZB+0.004f)*(tipZ[f]-ZB+0.004f)*3e5f;           // the tips are behind the case
  if(tipX[f]<L+0.050f) cst+=(L+0.050f-tipX[f])*(L+0.050f-tipX[f])*2e4f;               // reaching across the back
  if(tipX[f]>R-0.020f) cst+=(tipX[f]-R+0.020f)*(tipX[f]-R+0.020f)*2e4f;
  if(tipY[f]<B+0.008f) cst+=(B+0.008f-tipY[f])*(B+0.008f-tipY[f])*2e4f; if(tipY[f]>Tp-0.008f) cst+=(tipY[f]-Tp+0.008f)*(tipY[f]-Tp+0.008f)*2e4f; }
 // Spread evenly over the back (index on top).
 for(int f=2;f<5;f++){ float g=tipY[f]-tipY[f+1]; if(g<0.012f) cst+=(0.012f-g)*(0.012f-g)*1e4f; }
 float thumbErr=1; if(tn&&t1n){ float tx=tc[0]/tn,ty=tc[1]/tn,tz=tc[2]/tn; const float tt[3]={L+0.0095f,B+0.45f*(Tp-B),ZF+0.0065f};
   thumbErr=sqrtf((tx-tt[0])*(tx-tt[0])+(ty-tt[1])*(ty-tt[1])+(tz-tt[2])*(tz-tt[2])); cst+=thumbErr*thumbErr*8e3f;
   if(tx>WINL-0.002f) cst+=(tx-WINL+0.002f)*(tx-WINL+0.002f)*1e5f;
   float px=t1[0]/t1n,py=t1[1]/t1n; if(px>tx) cst+=(px-tx)*(px-tx)*2e4f; if(py>ty+0.004f) cst+=(py-ty-0.004f)*(py-ty-0.004f)*2e4f;
   g_info[0]=tx; g_info[1]=ty; g_info[2]=tz; }
 // Natural joints: moderate knuckles, light middle/end joints, similar knuckles, little rotation off the controller.
 for(int k=P_M0;k<=P_M3;k++){ float e=p[k]-0.9f; cst+=e*e*0.3f; }
 for(int k=P_M0;k<P_M3;k++){ float e=p[k+1]-p[k]; cst+=e*e*1.0f; }
 cst+=p[P_PIP]*p[P_PIP]*0.6f+p[P_DIP]*p[P_DIP]*0.8f; if(p[P_DIP]>p[P_PIP]) cst+=(p[P_DIP]-p[P_PIP])*(p[P_DIP]-p[P_PIP])*3.0f;
 { float a=p[P_T3]-0.35f,b=p[P_T4]-0.45f; cst+=(a*a+b*b)*(getenv("THUMBW")?atof(getenv("THUMBW")):0.0f); }   // a flatter thumb
 cst+=(p[P_YAW]*p[P_YAW]+p[P_TILT]*p[P_TILT])*0.25f+p[P_ROLL]*p[P_ROLL]*(getenv("ROLLW")?atof(getenv("ROLLW")):0.25f);
 g_info[5]=pen; g_info[6]=maxPen; g_info[7]=thumbErr; for(int f=0;f<6;f++) g_info[8+f]=std::min(std::min(mins[f][0],mins[f][1]),mins[f][2]);
 for(int f=2;f<=5;f++){ g_info[14+f-2]=tipX[f]; g_info[18+f-2]=tipZ[f]; } g_info[3]=cover;
 return cst; }
int main(int argc,char**argv){
 const auto& hv=handskin_vertices(); g_cls.resize(hv.size());
 for(size_t i=0;i<hv.size();i++){ int j=0; float bw=-1; for(int k=0;k<4;k++) if(hv[i].w[k]>bw){bw=hv[i].w[k]; j=(int)hv[i].j[k];}
  int c,s; if(j==0){c=0;s=0;} else if(j<=4){c=1;s=(j==1?0:(j==2?1:2));} else { int fi=(j-5)/5, k=(j-5)%5; if(k==0){c=0;s=0;} else {c=2+fi; s=std::min(k-1,2);} } g_cls[i]=c*10+s; }
 int iters=argc>2?atoi(argv[2]):30000; int seed=argc>3?atoi(argv[3]):0;
 float best[NP]={0.1587f,0.0546f,-0.0497f, 0.0f,0.0f,0.0f, 0.9f,0.9f,0.9f,0.9f,0.25f,0.15f,0.06f, -0.6412f,0.2339f,-0.3408f,0.2101f,1.4273f};
 if(getenv("PDA_INIT")){ const char* q=getenv("PDA_INIT"); for(int k=0;k<NP;k++){ char* e; best[k]=strtof(q,&e); q=e; while(*q==' '||*q==',') q++; } }
 static const float lo[NP]={0.02f,-0.08f,-0.20f, -1.2f,-1.0f,-1.0f, 0.0f,0.0f,0.0f,0.0f, 0.0f,0.0f,-0.05f, -1.3f,-1.3f,-1.3f,0.0f,0.0f};
 static const float hi[NP]={0.28f, 0.18f, 0.08f,  1.2f, 1.0f, 1.0f, 1.6f,1.6f,1.6f,1.6f, 1.2f,0.9f, 0.20f,  1.3f, 1.3f, 1.3f,1.6f,1.6f};
 std::mt19937 rng(11+seed*977); std::normal_distribution<float> N(0,1);
 if(seed>0){ for(int k=0;k<3;k++) best[k]+=N(rng)*0.015f; for(int k=P_YAW;k<=P_ROLL;k++) best[k]+=N(rng)*0.4f; for(int k=P_T0;k<=P_T4;k++) best[k]+=N(rng)*0.3f; }
 for(int k=0;k<NP;k++) best[k]=std::min(hi[k],std::max(lo[k],best[k]));
 float bc=cost(best);
 for(int it=0;it<iters;it++){ float f=1.0f-float(it)/iters, sc=0.1f+0.9f*f; float q[NP]; memcpy(q,best,sizeof q); int nk=1+rng()%3;
  for(int n=0;n<nk;n++){ int k=rng()%NP; float s=(k<=P_TZ)?0.005f:0.08f; q[k]+=N(rng)*s*sc; }
  for(int k=0;k<NP;k++) q[k]=std::min(hi[k],std::max(lo[k],q[k]));
  float c=cost(q); if(c<bc){ bc=c; memcpy(best,q,sizeof q);} }
 cost(best);
 printf("cost %.4f pen %.2e max %.4f cover %.2e thumbErr %.4f thumb %.4f %.4f %.4f | min palm %.4f thumb %.4f idx %.4f mid %.4f ring %.4f lit %.4f | tipX %.3f %.3f %.3f %.3f tipZ %.4f %.4f %.4f %.4f\n",bc,g_info[5],g_info[6],g_info[3],g_info[7],g_info[0],g_info[1],g_info[2],g_info[8],g_info[9],g_info[10],g_info[11],g_info[12],g_info[13],g_info[14],g_info[15],g_info[16],g_info[17],g_info[18],g_info[19],g_info[20],g_info[21]);
 printf("PDA"); for(int k=0;k<NP;k++) printf(" %.4f",best[k]); printf("\n");
 std::vector<SVtx> v; pose(best,v); std::vector<SVtx> v2; float I[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; std::vector<uint32_t> ix;
 handskin_cpu(I,-1,0.4f,0.8f,0.5f,v2,ix);
 FILE* fo=fopen(argv[1],"w"); for(auto&a:v) fprintf(fo,"v %.5f %.5f %.5f\n",a.x,a.y,a.z); for(size_t i=0;i<ix.size();i+=3) fprintf(fo,"f %u %u %u\n",ix[i]+1,ix[i+1]+1,ix[i+2]+1); fclose(fo);
}
