#pragma once
// Skinned glove hand (WebXR Input Profiles generic hand, MIT, subdivided): rest data and posing.
// Hand frame "H" of tfe_xr_hands.cpp: fingers along -Z, index on top (+Y), the back of the right hand
// toward +X (fingers curl toward -X*side). Left hand = mirror in X.
#include <vector>
#include "tfe_xr_mesh.h"

struct HandSkinVtx { float x, y, z, nx, ny, nz, r, g, b, spec; float j[4]; float w[4]; };

int  handskin_jointCount();
// Static vertex data (model space) and triangle indices (16 bit).
const std::vector<HandSkinVtx>& handskin_vertices();
const std::vector<unsigned short>& handskin_indices();
// Skinning matrices (column major 4x4, model space) for a pose: curls 0..1 for index, the other
// three fingers, and the thumb. `grip` 0..1 bends the fingers like holding a handle (more at the base).
void handskin_pose(float curlIndex, float curlOthers, float curlThumb, float bones[][16]);
// Model space -> hand frame H (column major), with the mirror for the left hand (side = -1).
void handskin_align(float side, float m[16]);
// CPU version (host tests): posed vertices transformed by H * align, in SVtx form.
void handskin_cpu(const float H[16], float side, float curlIndex, float curlOthers, float curlThumb, std::vector<SVtx>& out, std::vector<uint32_t>& idx);
void handskin_setThumbUp(float ax, float ay, float az);
// Explicit thumb pose (base rotZ, rotY, rotX, flex 1, flex 2) for the following poses; nullptr = from the curl.
// Explicit finger joints: knuckle flexion of index..little, shared middle and end joint flexion, spread (null = off).
void handskin_setFingerJoints(const float* p7);
void handskin_setThumbOverride(const float* p);
// Explicit curls of the middle, ring and little fingers (holding a weapon); nullptr = curlOthers.
void handskin_setFingerOverride(const float* c3);
// Explicit index finger: flexion of the three joints and abduction at the knuckle; nullptr = curlIndex.
void handskin_setIndexOverride(const float* a4);
