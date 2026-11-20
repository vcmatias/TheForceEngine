#pragma once
// Smooth (non voxel) meshes for the Quest port: surface nets over a scalar field, lit with normals.
#include <vector>
#include <stdint.h>

struct SVtx { float x, y, z, nx, ny, nz, r, g, b, spec, emis; };

struct MeshField
{
	int nx = 0, ny = 0, nz = 0;
	float origin[3] = { 0, 0, 0 };
	float cell = 1.0f;
	std::vector<float> v;               // > iso = inside
	float at(int x, int y, int z) const { return v[(size_t(z) * ny + y) * nx + x]; }
};

// Material of the surface around a grid point: color 0..1, specular 0..1, emissive 0/1.
typedef void (*MeshMaterialFn)(void* user, const float p[3], float rgb[3], float* spec, float* emis);

// Surface nets: one vertex per cell crossing the iso level, quads across each crossing edge.
void mesh_surfaceNets(const MeshField& f, float iso, MeshMaterialFn mat, void* user,
	std::vector<SVtx>& verts, std::vector<uint32_t>& indices);
