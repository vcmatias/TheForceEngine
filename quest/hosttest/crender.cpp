// Fast rasterizer for the host dumps: crender in.txt extra.txt out.ppm cx cy cz [ex ey ez ux uy uz]...
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
struct V{float x,y,z,r,g,b,a,e;};
static float dot(const float*a,const float*b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void nrm(float*a){float l=sqrtf(dot(a,a));a[0]/=l;a[1]/=l;a[2]/=l;}
static void crs(const float*a,const float*b,float*o){o[0]=a[1]*b[2]-a[2]*b[1];o[1]=a[2]*b[0]-a[0]*b[2];o[2]=a[0]*b[1]-a[1]*b[0];}
static void loadLast(const char*fn,std::vector<V>&out,bool last){FILE*f=fopen(fn,"r");if(!f)return;char l[512];std::vector<V> cur;while(fgets(l,512,f)){if(!strncmp(l,"END",3)){if(last)out=cur;else out.insert(out.end(),cur.begin(),cur.end());cur.clear();continue;}V v;if(sscanf(l,"%f %f %f %f %f %f %f %f",&v.x,&v.y,&v.z,&v.r,&v.g,&v.b,&v.a,&v.e)==8)cur.push_back(v);}if(!cur.empty())out.insert(out.end(),cur.begin(),cur.end());fclose(f);}
int main(int argc,char**argv){
 std::vector<V> tv; loadLast(argv[1],tv,true); std::vector<V> ex; loadLast(argv[2],ex,false); tv.insert(tv.end(),ex.begin(),ex.end());
 float c[3]={(float)atof(argv[4]),(float)atof(argv[5]),(float)atof(argv[6])};
 int nv=(argc-7)/6; const int W=420,H=420; std::vector<unsigned char> img(W*nv*H*3);
 for(int vi=0;vi<nv;vi++){
  float e[3],up[3]; for(int k=0;k<3;k++){e[k]=c[k]+atof(argv[7+vi*6+k]);up[k]=atof(argv[10+vi*6+k]);}
  float f[3]={c[0]-e[0],c[1]-e[1],c[2]-e[2]};nrm(f);float r[3];crs(f,up,r);nrm(r);float u[3];crs(r,f,u);
  std::vector<float> zb(W*H,1e9);std::vector<float> col(W*H*3);for(int i=0;i<W*H;i++){col[i*3]=0.25f;col[i*3+1]=0.27f;col[i*3+2]=0.3f;}
  float foc=W/2/0.45f; float L[3]={0.3f,0.8f,0.5f};nrm(L);
  for(size_t t=0;t+2<tv.size();t+=3){
   float sx[3],sy[3],sz[3];bool bad=false;
   for(int k=0;k<3;k++){float p[3]={tv[t+k].x-e[0],tv[t+k].y-e[1],tv[t+k].z-e[2]};float z=dot(p,f);if(z<0.01f){bad=true;break;}sx[k]=W/2+dot(p,r)/z*foc;sy[k]=H/2-dot(p,u)/z*foc;sz[k]=z;}
   if(bad)continue;
   float a[3]={tv[t+1].x-tv[t].x,tv[t+1].y-tv[t].y,tv[t+1].z-tv[t].z},b[3]={tv[t+2].x-tv[t].x,tv[t+2].y-tv[t].y,tv[t+2].z-tv[t].z},n[3];crs(a,b,n);float nl=sqrtf(dot(n,n));if(nl<1e-12f)continue;for(int k=0;k<3;k++)n[k]/=nl;
   float sh=0.35f+0.65f*fabsf(dot(n,L));
   int x0=std::max(0,(int)floorf(std::min({sx[0],sx[1],sx[2]}))),x1=std::min(W-1,(int)ceilf(std::max({sx[0],sx[1],sx[2]})));
   int y0=std::max(0,(int)floorf(std::min({sy[0],sy[1],sy[2]}))),y1=std::min(H-1,(int)ceilf(std::max({sy[0],sy[1],sy[2]})));
   float den=(sy[1]-sy[2])*(sx[0]-sx[2])+(sx[2]-sx[1])*(sy[0]-sy[2]);if(fabsf(den)<1e-9f)continue;
   for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){float px=x+0.5f,py=y+0.5f;
    float l0=((sy[1]-sy[2])*(px-sx[2])+(sx[2]-sx[1])*(py-sy[2]))/den,l1=((sy[2]-sy[0])*(px-sx[2])+(sx[0]-sx[2])*(py-sy[2]))/den,l2=1-l0-l1;
    if(l0<0||l1<0||l2<0)continue;float z=l0*sz[0]+l1*sz[1]+l2*sz[2];if(z>=zb[y*W+x])continue;zb[y*W+x]=z;
    col[(y*W+x)*3]=tv[t].r*sh;col[(y*W+x)*3+1]=tv[t].g*sh;col[(y*W+x)*3+2]=tv[t].b*sh;}
  }
  for(int y=0;y<H;y++)for(int x=0;x<W;x++)for(int k=0;k<3;k++)img[(y*W*nv+vi*W+x)*3+k]=(unsigned char)(std::min(1.0f,std::max(0.0f,col[(y*W+x)*3+k]))*255);
 }
 FILE*o=fopen(argv[3],"wb");fprintf(o,"P6 %d %d 255\n",W*nv,H);fwrite(img.data(),1,img.size(),o);fclose(o);
}
