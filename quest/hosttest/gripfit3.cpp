// gripfit3: per-joint fingers (knuckle per finger, shared middle/end joints) and a firm, even wrap of the
// middle, ring and little fingers on the front strap, the web high on the backstrap, the thumb along the side.
// Fits the right hand on a Blender weapon grip (tools/grip/<name>.sdf): fingers around the handle with
// contact and no penetration, the index pad on the trigger, the thumb wrapped. Prints the pose table row
// and writes the posed hand (rest and trigger pulled) as OBJ in the kit frame.
//   gripfit <name.sdf> <trigger x y z | none> <out prefix> [iterations]
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
// grid
static int NX,NY,NZ; static float X0,Y0,Z0,ST; static std::vector<float> G;
static float sdf(float x,float y,float z){ float fx=(x-X0)/ST,fy=(y-Y0)/ST,fz=(z-Z0)/ST; if(fx<0||fy<0||fz<0||fx>=NX-1||fy>=NY-1||fz>=NZ-1) return 0.05f;
 int ix=(int)fx,iy=(int)fy,iz=(int)fz; float ax=fx-ix,ay=fy-iy,az=fz-iz; auto g=[&](int a,int b,int c){ return G[((size_t)(iz+c)*NY+(iy+b))*NX+(ix+a)]; };
 float c00=g(0,0,0)*(1-ax)+g(1,0,0)*ax, c10=g(0,1,0)*(1-ax)+g(1,1,0)*ax, c01=g(0,0,1)*(1-ax)+g(1,0,1)*ax, c11=g(0,1,1)*(1-ax)+g(1,1,1)*ax;
 return (c00*(1-ay)+c10*ay)*(1-az)+(c01*(1-ay)+c11*ay)*az; }
enum { P_DX,P_DY,P_DZ,P_RY,P_RZ,P_CI,P_I1,P_I2,P_I3,P_C1,P_C2,P_C3,P_T0,P_T1,P_T2,P_T3,P_T4,P_PIP,P_DIP,P_SPR,P_RX,NP };
const float PULL[4]={0.06f,0.22f,0.15f,0.0f};
static std::vector<int> g_cls;   // 0 palm, 1 thumb, 2 index, 3 middle, 4 ring, 5 little; *10 + segment
static float g_gripBottom=-0.066f; static bool g_hasTrig=false, g_free=false; static float g_thumbErr=0; static float g_penCls[6]; static float TRIG[3];
static void pose(const float* p,float pull,std::vector<SVtx>& v,std::vector<uint32_t>& ix){ float ia[4]={p[P_CI]+PULL[0]*pull,p[P_I1]+PULL[1]*pull,p[P_I2]+PULL[2]*pull,p[P_I3]};
 M4 Hrel=mul(mul(T(0,-0.055f*cosf(HT),-0.055f*sinf(HT)),rotX(HT)),T(0.020f,0.0f,0.060f));
 // top = rotX(HT)*(0,0.055,0) = (0, 0.055 cos, 0.055 sin) -> subtract
 M4 H=mul(mul(mul(T(-GIW[0],-GIW[1],-GIW[2]),Hrel),T(p[P_DX],p[P_DY],p[P_DZ])),mul(mul(rotY(p[P_RY]),rotZ(p[P_RZ])),rotX(p[P_RX])));
 float fj[7]={p[P_CI],p[P_C1],p[P_C2],p[P_C3],p[P_PIP],p[P_DIP],p[P_SPR]}; float tp[5]={p[P_T0],p[P_T1],p[P_T2],p[P_T3],p[P_T4]};
 handskin_setFingerJoints(fj); handskin_setIndexOverride(ia); handskin_setThumbOverride(tp); v.clear(); ix.clear(); handskin_cpu(H.m,1.0f,0.3f,0.8f,0.8f,v,ix); handskin_setIndexOverride(nullptr); handskin_setFingerJoints(nullptr); handskin_setThumbOverride(nullptr); }
struct Res{ float cost,pen,maxPen,trig; float seg[6][3]; };
static Res eval(const float* p,bool pulled=false){
 std::vector<SVtx> v; std::vector<uint32_t> ix; pose(p,pulled?1.0f:0.0f,v,ix);
 Res r={}; for(float&x:g_penCls) x=0; double tc[3]={0,0,0}; int tn=0; float mins[6][3]; for(auto&a:mins) for(float&b:a) b=1; float trigBest=1;
 for(size_t i=0;i<v.size();i+=2){ float d=sdf(v[i].x,v[i].y,v[i].z); int c=g_cls[i]/10,s=g_cls[i]%10;
  if(pulled&&c==2) continue;
  if(d<-0.0008f){ float q=-0.0008f-d; r.pen+=q*q; r.maxPen=std::max(r.maxPen,q); g_penCls[c]=std::max(g_penCls[c],q); }
  if(c==1&&s==2){ tc[0]+=v[i].x; tc[1]+=v[i].y; tc[2]+=v[i].z; tn++; }
  if(s<3) mins[c][s]=std::min(mins[c][s],d);
  if(g_hasTrig&&c==2&&s==2){ float dx=v[i].x-TRIG[0],dy=v[i].y-TRIG[1],dz=v[i].z-TRIG[2]; trigBest=std::min(trigBest,dx*dx+dy*dy+dz*dz); } }
 memcpy(r.seg,mins,sizeof(mins)); if(tn){ const float tt[3]={-0.018f,0.012f,-0.026f}; float e=0; for(int k=0;k<3;k++){ float d=tc[k]/tn-tt[k]; e+=d*d; } g_thumbErr=sqrtf(e); } r.trig=sqrtf(trigBest);
 float c=r.pen*4e6f+r.maxPen*r.maxPen*4e5f;
 auto contact=[&](float m,float w){ float e=m-0.0003f; return w*(e>0?e*e:e*e*0.3f); };
 for(int f=3;f<=5;f++) for(int s=0;s<3;s++) c+=contact(mins[f][s],6e3f);
 c+=contact(mins[0][0],4e3f);           // palm on the back of the grip
 if(!g_free) c+=contact(mins[1][1],3e3f)+contact(mins[1][2],3e3f);   // thumb on the side of the frame
 else c+=contact(mins[1][2],1e3f);
 c+=g_thumbErr*g_thumbErr*1e3f;   // straight along the frame, pointing forward (parallel to the barrel)
 if(g_hasTrig) c+=r.trig*r.trig*8e3f+contact(mins[2][2],2e3f);
 else if(g_free){ const float nat[3]={0.25f,0.85f,0.45f}; for(int k=0;k<3;k++) c+=(p[P_CI+k]-nat[k])*(p[P_CI+k]-nat[k])*0.3f; c+=p[P_I3]*p[P_I3]*0.3f;
   { const float t0[5]={0.30f,0.21f,-0.30f,0.57f,0.87f}; for(int k=0;k<5;k++) c+=(p[P_T0+k]-t0[k])*(p[P_T0+k]-t0[k])*0.5f; }
   for(int s=1;s<3;s++) if(mins[2][s]<0.004f) c+=(0.004f-mins[2][s])*(0.004f-mins[2][s])*2e4f; }   // trigger finger off the grip
 else for(int s=0;s<3;s++) c+=contact(mins[2][s],3e3f);   // no trigger: the index also holds
 { float dd=sqrtf(p[P_DX]*p[P_DX]+p[P_DY]*p[P_DY]+p[P_DZ]*p[P_DZ]); if(dd>0.035f) c+=(dd-0.035f)*(dd-0.035f)*1e5f; } c+=(p[P_DX]*p[P_DX]+p[P_DY]*p[P_DY]+p[P_DZ]*p[P_DZ])*2.0f+(p[P_RY]*p[P_RY]+p[P_RZ]*p[P_RZ]+p[P_RX]*p[P_RX])*0.5f;
 for(int k=P_C1;k<=P_C3;k++){ if(p[k]<0||p[k]>1.6f) c+=1; c+=(p[k]-1.05f)*(p[k]-1.05f)*1.0f; }
 // an even, firm wrap: similar knuckles (the little finger as wrapped as the others), middle joints well bent
 c+=(p[P_C2]-p[P_C1])*(p[P_C2]-p[P_C1])*6.0f+(p[P_C3]-p[P_C2])*(p[P_C3]-p[P_C2])*6.0f;
 c+=(p[P_PIP]-1.25f)*(p[P_PIP]-1.25f)*1.0f+(p[P_DIP]-0.70f)*(p[P_DIP]-0.70f)*1.0f+p[P_SPR]*p[P_SPR]*20.0f;
 { // the little finger stays on the grip (above its bottom) and every finger pad lies on the front strap
   float lowest=1; for(size_t i=0;i<v.size();i+=2){ if(g_cls[i]==52) lowest=std::min(lowest,v[i].y); }
   if(lowest<g_gripBottom) c+=(g_gripBottom-lowest)*(g_gripBottom-lowest)*2e4f; } for(int k=P_CI;k<=P_I2;k++) if(p[k]<-0.02f||p[k]>1.6f) c+=1; for(int k=P_T3;k<=P_T4;k++) if(p[k]<0.0f||p[k]>1.7f) c+=1; for(int k=P_T0;k<=P_T2;k++) if(fabsf(p[k])>1.3f) c+=1; if(fabsf(p[P_I3])>0.35f) c+=1;
 r.cost=c; return r; }
static void writeObj(const char* fn,const float* p,float ci){ std::vector<SVtx> v; std::vector<uint32_t> ix; pose(p,ci,v,ix); FILE* f=fopen(fn,"w");
 for(auto&a:v) fprintf(f,"v %.5f %.5f %.5f\n",a.x,a.y,a.z); for(size_t i=0;i<ix.size();i+=3) fprintf(f,"f %u %u %u\n",ix[i]+1,ix[i+1]+1,ix[i+2]+1); fclose(f); }
int main(int argc,char**argv){
 FILE* f=fopen(argv[1],"rb"); if(!f) return 1; fread(&NX,4,1,f); fread(&NY,4,1,f); fread(&NZ,4,1,f); fread(&X0,4,1,f); fread(&Y0,4,1,f); fread(&Z0,4,1,f); fread(&ST,4,1,f);
 G.resize((size_t)NX*NY*NZ); fread(G.data(),4,G.size(),f); fclose(f);
 int a=2; if(!strcmp(argv[a],"free")){ g_free=true; a+=1; } else if(strcmp(argv[a],"none")){ g_hasTrig=true; for(int i=0;i<3;i++) TRIG[i]=atof(argv[a+i]); a+=3; } else a+=1;
 const char* out=argv[a]; int iters=argc>a+1?atoi(argv[a+1]):40000;
 // vertex classes from the dominant joint
 const auto& hv=handskin_vertices(); g_cls.resize(hv.size());
 for(size_t i=0;i<hv.size();i++){ int j=0; float bw=-1; for(int k=0;k<4;k++) if(hv[i].w[k]>bw){bw=hv[i].w[k]; j=(int)hv[i].j[k];}
  int c,s; if(j==0){c=0;s=0;} else if(j<=4){c=1;s=j-1;} else { int fi=(j-5)/5, k=(j-5)%5; if(k==0){c=0;s=0;} else {c=2+fi; s=std::min(k-1,2);} } if(c==1) s=(j==1?0:(j==2?1:2)); g_cls[i]=c*10+s; }
 float best[NP]={0,0,0,0,0, 0.20f,0.65f,0.40f,0.0f, 1.05f,1.05f,1.05f, 0.30f,0.21f,-0.30f,0.57f,0.87f, 1.25f,0.70f,0.0f,0.0f};
 if(!g_hasTrig&&!g_free){ best[P_CI]=1.0f; best[P_I1]=1.3f; best[P_I2]=0.8f; }
 if(g_free){ best[P_CI]=0.25f; best[P_I1]=0.85f; best[P_I2]=0.45f; }
 if(getenv("GRIP_INIT")){ const char* q=getenv("GRIP_INIT"); for(int k=0;k<NP&&*q;k++){ char* e; float x=strtof(q,&e); if(e==q) break; best[k]=x; q=e; while(*q==','||*q==' ') q++; } }
 std::mt19937 rng(7+(argc>a+3?atoi(argv[a+3]):0)*131); std::normal_distribution<float> N(0,1); std::uniform_real_distribution<float> U(0,1);
 float init[NP]; memcpy(init,best,sizeof init); Res br=eval(best); int restarts=argc>a+2?atoi(argv[a+2]):1;
 for(int rs=0;rs<restarts;rs++){
  float cur[NP]; memcpy(cur,init,sizeof cur);
  if(rs>0){ cur[P_DX]+=N(rng)*0.006f; cur[P_DY]+=N(rng)*0.008f; cur[P_DZ]+=N(rng)*0.010f; for(int k=P_C1;k<=P_C3;k++) cur[k]=0.8f+0.5f*U(rng); cur[P_RX]=(U(rng)-0.5f)*0.8f; for(int k=P_T0;k<=P_T4;k++) cur[k]+=N(rng)*0.3f; }
  Res cr=eval(cur); float temp=0.0f;
  for(int it=0;it<iters;it++){ float f=1.0f-float(it)/iters; float sc=0.1f+0.9f*f; float q[NP]; memcpy(q,cur,sizeof q);
   int nk=1+rng()%3; for(int n=0;n<nk;n++){ int k=rng()%NP; float s=(k<=P_DZ)?0.004f:(k<=P_RZ?0.05f:0.08f); q[k]+=N(rng)*s*sc; }
   { static const float lo[NP]={-0.03f,-0.03f,-0.03f,-0.3f,-0.3f, 0.0f,0.0f,0.0f,-0.3f, 0.2f,0.2f,0.2f, -1.2f,-1.2f,-1.2f,0.0f,0.0f, 0.3f,0.0f,-0.05f,-0.6f};
     static const float hi[NP]={ 0.03f, 0.04f, 0.03f, 0.3f, 0.3f, 1.6f,1.6f,1.6f, 0.3f, 1.6f,1.6f,1.6f,  1.2f, 1.2f, 1.2f,1.6f,1.6f, 1.7f,1.2f,0.10f,0.6f};
     for(int k=0;k<NP;k++) q[k]=std::min(hi[k],std::max(lo[k],q[k])); }
   Res r=eval(q); float T0=temp*f*f+1e-6f;
   if(r.cost<cr.cost){ cr=r; memcpy(cur,q,sizeof q); }
   if(cr.cost<br.cost){ br=cr; memcpy(best,cur,sizeof cur);} }
  fprintf(stderr,"restart %d best %.4f\n",rs,br.cost); }
 { for(float ci: {0.0f,1.0f}){ std::vector<SVtx> v; std::vector<uint32_t> ix; pose(best,ci,v,ix); double c[3]={0,0,0}; int n=0; float zmin=1,ymin=1;
   for(size_t i=0;i<v.size();i++) if(g_cls[i]==22){ c[0]+=v[i].x; c[1]+=v[i].y; c[2]+=v[i].z; n++; zmin=std::min(zmin,v[i].z); ymin=std::min(ymin,v[i].y);} 
   fprintf(stderr,"pull %.0f index distal centroid %.4f %.4f %.4f zmin %.4f ymin %.4f\n",ci,c[0]/n,c[1]/n,c[2]/n,zmin,ymin);
   float b0[3]={1,1,1},b1[3]={-1,-1,-1}; for(size_t i=0;i<v.size();i++) if(g_cls[i]/10==2){ float q[3]={v[i].x,v[i].y,v[i].z}; for(int k=0;k<3;k++){ b0[k]=std::min(b0[k],q[k]); b1[k]=std::max(b1[k],q[k]); } }
   { float yt=-1; for(size_t i=0;i<v.size();i++) if(g_cls[i]/10==2 && v[i].z<-0.012f) yt=std::max(yt,v[i].y); fprintf(stderr,"   index top in front of the grip %.4f\n",yt); }
   fprintf(stderr,"   index box %.4f %.4f %.4f .. %.4f %.4f %.4f\n",b0[0],b0[1],b0[2],b1[0],b1[1],b1[2]); } }
 Res pr=eval(best,true); eval(best); fprintf(stderr,"pen by class palm %.4f thumb %.4f index %.4f mid %.4f ring %.4f little %.4f thumbErr %.4f\n",g_penCls[0],g_penCls[1],g_penCls[2],g_penCls[3],g_penCls[4],g_penCls[5],g_thumbErr);
 printf("%s cost %.4f pen %.2e max %.4f trig %.4f | palm %.4f thumb %.4f %.4f index %.4f %.4f %.4f mid %.4f %.4f %.4f ring %.4f %.4f %.4f lit %.4f %.4f %.4f | pulled max %.4f\n",argv[1],br.cost,br.pen,br.maxPen,br.trig,
  br.seg[0][0],br.seg[1][1],br.seg[1][2],br.seg[2][0],br.seg[2][1],br.seg[2][2],br.seg[3][0],br.seg[3][1],br.seg[3][2],br.seg[4][0],br.seg[4][1],br.seg[4][2],br.seg[5][0],br.seg[5][1],br.seg[5][2],pr.maxPen);
 printf("ROW {"); for(int k=0;k<NP;k++) printf(" %.4ff,",best[k]); printf(" },\n");
 char fn[512]; snprintf(fn,sizeof fn,"%s_rest.obj",out); writeObj(fn,best,0.0f); snprintf(fn,sizeof fn,"%s_pull.obj",out); writeObj(fn,best,1.0f);
}
