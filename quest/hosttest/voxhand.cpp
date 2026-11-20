#include <cstdlib>
void hands_hostVoxHand(float side, float ci, float co, float ct, const char* path);
int main(int c, char** v) { hands_hostVoxHand(atof(v[1]), atof(v[2]), atof(v[3]), atof(v[4]), v[5]); }
void hostDraw(const void*, int, int) {}
