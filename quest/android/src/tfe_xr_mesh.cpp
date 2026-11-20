#include "tfe_xr_mesh.h"
#include <math.h>

void mesh_surfaceNets(const MeshField& f, float iso, MeshMaterialFn mat, void* user,
	std::vector<SVtx>& verts, std::vector<uint32_t>& indices)
{
	const int nx = f.nx, ny = f.ny, nz = f.nz;
	if (nx < 2 || ny < 2 || nz < 2) { return; }
	const int cx = nx - 1, cy = ny - 1, cz = nz - 1;
	std::vector<int32_t> cellVert(size_t(cx) * cy * cz, -1);
	auto cidx = [&](int x, int y, int z) { return (size_t(z) * cy + y) * cx + x; };
	static const int corner[8][3] = { {0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1} };
	static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7} };

	// Vertices.
	for (int z = 0; z < cz; z++) for (int y = 0; y < cy; y++) for (int x = 0; x < cx; x++)
	{
		float val[8];
		int mask = 0;
		for (int c = 0; c < 8; c++)
		{
			val[c] = f.at(x + corner[c][0], y + corner[c][1], z + corner[c][2]);
			if (val[c] > iso) { mask |= 1 << c; }
		}
		if (mask == 0 || mask == 255) { continue; }
		float p[3] = { 0, 0, 0 };
		int n = 0;
		for (int e = 0; e < 12; e++)
		{
			const int a = edges[e][0], b = edges[e][1];
			if (((mask >> a) & 1) == ((mask >> b) & 1)) { continue; }
			const float t = (iso - val[a]) / (val[b] - val[a]);
			for (int i = 0; i < 3; i++) { p[i] += corner[a][i] + (corner[b][i] - corner[a][i]) * t; }
			n++;
		}
		for (int i = 0; i < 3; i++) { p[i] /= float(n); }
		// Gradient of the field (trilinear derivative inside the cell) -> outward normal.
		const float u = p[0], v = p[1], w = p[2];
		auto V = [&](int c) { return val[c]; };
		const float gx = ((V(1) - V(0)) * (1 - v) * (1 - w) + (V(3) - V(2)) * v * (1 - w) + (V(5) - V(4)) * (1 - v) * w + (V(7) - V(6)) * v * w);
		const float gy = ((V(2) - V(0)) * (1 - u) * (1 - w) + (V(3) - V(1)) * u * (1 - w) + (V(6) - V(4)) * (1 - u) * w + (V(7) - V(5)) * u * w);
		const float gz = ((V(4) - V(0)) * (1 - u) * (1 - v) + (V(5) - V(1)) * u * (1 - v) + (V(6) - V(2)) * (1 - u) * v + (V(7) - V(3)) * u * v);
		float nl = sqrtf(gx * gx + gy * gy + gz * gz);
		if (nl < 1e-8f) { nl = 1.0f; }
		SVtx o;
		o.x = f.origin[0] + (x + p[0]) * f.cell;
		o.y = f.origin[1] + (y + p[1]) * f.cell;
		o.z = f.origin[2] + (z + p[2]) * f.cell;
		o.nx = -gx / nl; o.ny = -gy / nl; o.nz = -gz / nl;
		float rgb[3] = { 0.5f, 0.5f, 0.5f }, spec = 0.3f, emis = 0.0f;
		if (mat)
		{
			// Sample the material a little inside the surface.
			const float q[3] = { o.x - o.nx * f.cell * 0.7f, o.y - o.ny * f.cell * 0.7f, o.z - o.nz * f.cell * 0.7f };
			mat(user, q, rgb, &spec, &emis);
		}
		o.r = rgb[0]; o.g = rgb[1]; o.b = rgb[2]; o.spec = spec; o.emis = emis;
		cellVert[cidx(x, y, z)] = (int32_t)verts.size();
		verts.push_back(o);
	}

	// Faces: every grid edge with a sign change is shared by 4 cells.
	for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)
	{
		const bool in0 = f.at(x, y, z) > iso;
		for (int axis = 0; axis < 3; axis++)
		{
			const int x1 = x + (axis == 0), y1 = y + (axis == 1), z1 = z + (axis == 2);
			if (x1 >= nx || y1 >= ny || z1 >= nz) { continue; }
			const bool in1 = f.at(x1, y1, z1) > iso;
			if (in0 == in1) { continue; }
			// The 4 cells around this edge.
			int c[4][3];
			if (axis == 0) { int t[4][3] = { {x,y-1,z-1},{x,y,z-1},{x,y,z},{x,y-1,z} }; for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) c[i][j] = t[i][j]; }
			else if (axis == 1) { int t[4][3] = { {x-1,y,z-1},{x-1,y,z},{x,y,z},{x,y,z-1} }; for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) c[i][j] = t[i][j]; }
			else { int t[4][3] = { {x-1,y-1,z},{x,y-1,z},{x,y,z},{x-1,y,z} }; for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) c[i][j] = t[i][j]; }
			int32_t vi[4];
			bool ok = true;
			for (int i = 0; i < 4 && ok; i++)
			{
				if (c[i][0] < 0 || c[i][1] < 0 || c[i][2] < 0 || c[i][0] >= cx || c[i][1] >= cy || c[i][2] >= cz) { ok = false; break; }
				vi[i] = cellVert[cidx(c[i][0], c[i][1], c[i][2])];
				if (vi[i] < 0) { ok = false; }
			}
			if (!ok) { continue; }
			// Winding: counter-clockwise seen from outside (inside -> outside along the axis).
			if (in0)
			{
				indices.push_back(vi[0]); indices.push_back(vi[1]); indices.push_back(vi[2]);
				indices.push_back(vi[0]); indices.push_back(vi[2]); indices.push_back(vi[3]);
			}
			else
			{
				indices.push_back(vi[0]); indices.push_back(vi[2]); indices.push_back(vi[1]);
				indices.push_back(vi[0]); indices.push_back(vi[3]); indices.push_back(vi[2]);
			}
		}
	}
}
