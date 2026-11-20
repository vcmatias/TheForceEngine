#pragma once
#include <vector>
#include <stdint.h>
// First-person weapon art (RGBA, rows top-down) placed at (x0, y0) on the 320x200 screen, and the
// pose of the weapon frame relative to the eye that reproduces it: yaw, pitch, roll (rad), x, y, z (m).
struct WeaponView { const unsigned char* rgba; int w, h, x0, y0; float pose[6]; };
// detail: voxel resolution multiplier (1 = the original 3-4.5 mm voxels, 2 = half size).
bool weapons_buildModel(int id, std::vector<float>& out, float muzzleOut[3], const float grip[3], const WeaponView* view = nullptr, float detail = 1.0f);
struct SVtx;
// Smooth high resolution version of the same model (indexed, with normals).
bool weapons_buildSmooth(int id, std::vector<SVtx>& verts, std::vector<uint32_t>& indices, float muzzleOut[3], const float grip[3]);
// High resolution model (signed distance solids, fine mesh, procedural materials, baked occlusion).
bool weapons_buildHiRes(int id, std::vector<SVtx>& verts, std::vector<uint32_t>& indices, float muzzleOut[3], const float grip[3]);
