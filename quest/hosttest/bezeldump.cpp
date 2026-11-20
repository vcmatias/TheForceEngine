#include "../android/src/tfe_xr_hands.cpp"
int main(){ buildBezel(); FILE* f=fopen("/tmp/bezel.obj","w"); const DfmSub& sb=s_bezelSub[0];
 size_t n=sb.v.size()/11; for(size_t i=0;i<n;i++) fprintf(f,"v %f %f %f\n",sb.v[i*11],sb.v[i*11+1],sb.v[i*11+2]);
 for(size_t i=0;i<n;i++) fprintf(f,"vt %f %f\n",sb.v[i*11+6],1-sb.v[i*11+7]);
 for(size_t i=0;i<n;i++) fprintf(f,"vn %f %f %f\n",sb.v[i*11+3],sb.v[i*11+4],sb.v[i*11+5]);
 for(size_t i=0;i<sb.i.size();i+=3) fprintf(f,"f %u/%u/%u %u/%u/%u %u/%u/%u\n",sb.i[i]+1,sb.i[i]+1,sb.i[i]+1,sb.i[i+1]+1,sb.i[i+1]+1,sb.i[i+1]+1,sb.i[i+2]+1,sb.i[i+2]+1,sb.i[i+2]+1);
 fclose(f); printf("%zu verts %zu tris\n",n,sb.i.size()/3); }
void hostDraw(const void*, int, int) {}
