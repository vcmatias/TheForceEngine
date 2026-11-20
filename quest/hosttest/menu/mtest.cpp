#include "tfe_vrmenu.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cstdint>
static std::vector<unsigned char> L;
struct Ent{std::string t,n; size_t off,sz;};
std::vector<Ent> ents(){ std::vector<Ent> e; uint32_t sz; memcpy(&sz,&L[12],4); size_t off=16+sz;
 for(size_t i=0;i<sz/16;i++){ Ent x; x.t=std::string((char*)&L[16+i*16],4); char n[9]={0}; memcpy(n,&L[20+i*16],8); x.n=n; uint32_t s; memcpy(&s,&L[28+i*16],4); x.off=off+16; x.sz=s; off+=16+s; e.push_back(x);} return e; }
std::vector<std::vector<unsigned char>> imgs;
int main(int argc,char**argv){
 FILE* f=fopen(argv[1],"rb"); fseek(f,0,2); L.resize(ftell(f)); fseek(f,0,0); fread(L.data(),1,L.size(),f); fclose(f);
 VrmFont font={}; std::vector<VrmImage> frames;
 for(auto& e:ents()){
  if(e.t=="FONT"&&e.n=="font8") vrm_parseFont(&L[e.off],(int)e.sz,&font);
  if(e.t=="ANIM"&&e.n=="config"){ const unsigned char* b=&L[e.off]; int cnt=*(int16_t*)b; size_t p=2;
   for(int i=0;i<cnt;i++){ uint32_t s; memcpy(&s,b+p,4); p+=4; const unsigned char* d=b+p; int16_t h[4]; memcpy(h,d,8); int W=h[2]+1,H=h[3]+1;
    std::vector<unsigned char> im(W*H,0); size_t q=8;
    while(q+6<=s){ int16_t ln[3]; memcpy(ln,d+q,6); if(!ln[0])break; q+=6; bool rle=ln[0]&1; int n=(ln[0]>>1)&0x3fff; unsigned char* o=&im[ln[1]+ln[2]*W];
      while(n>0){ if(rle){ unsigned char c=d[q++]; if(!(c&1)){c>>=1; memcpy(o,d+q,c); q+=c;o+=c;n-=c;} else {c>>=1; memset(o,d[q++],c); o+=c;n-=c;} } else { memcpy(o,d+q,n); q+=n; n=0; } } }
    imgs.push_back(im); frames.push_back({W,H,nullptr}); p+=s; }
   for(size_t i=0;i<frames.size();i++) frames[i].px=imgs[i].data(); }
 }
 std::vector<unsigned char> c(320*200,0);
 VrmPage pg={}; pg.title="CONFIGURAÇÃO VR"; pg.rowCount=12; pg.leftButton="CONTROLES >"; pg.hover=argc>3?atoi(argv[3]):-1; pg.pressed=-1;
 const char* lab[12]={"GIRO","ÂNGULO DO GIRO","VINHETA AO ANDAR","CORRER","AGACHAR DE VERDADE","PROFUNDIDADE","AJUSTE DE ALTURA","HUD NO RELÓGIO","MAPA (1º TOQUE Y)","MIRA LASER","DISTÂNCIA DA TELA","VIBRAÇÃO"};
 const char* val[12]={"EM DEGRAUS","45 GRAUS","0.6","LIGA ATÉ PARAR","SIM","35 CM","0 CM","SIM","HOLOGRAMA","SIM","1.6 M","100%"};
 float fl[12]={-1,0.4f,0.6f,-1,-1,0.4f,0.3f,-1,-1,-1,0.36f,1.0f};
 for(int i=0;i<12;i++){ pg.rows[i].label=lab[i]; strcpy(pg.rows[i].value,val[i]); pg.rows[i].fill=fl[i]; }
 if(argv[2][0]=='h') vrm_drawHelp(c.data(),frames.data(),(int)frames.size(),font,-1);
 else vrm_drawConfig(c.data(),frames.data(),(int)frames.size(),font,pg);
 fwrite(c.data(),1,c.size(),stdout);
}
