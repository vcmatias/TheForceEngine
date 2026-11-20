// Skinned glove hand: WebXR Input Profiles generic hand (MIT License, Copyright (c) 2019 Amazon),
// subdivided twice (tools/hand/subdiv.py), posed procedurally from the controller curls.
#include "tfe_xr_handskin.h"
#include "tfe_xr_handmesh.h"
#include <math.h>
#include <string.h>

namespace
{
	struct Mat { float m[16]; };   // column major

	Mat ident() { Mat r; memset(r.m, 0, sizeof(r.m)); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }
	Mat mul(const Mat& a, const Mat& b)
	{
		Mat r;
		for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++)
		{
			float s = 0.0f;
			for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
			r.m[c * 4 + rr] = s;
		}
		return r;
	}
	Mat trs(const float t[3], const float q[4])
	{
		const float x = q[0], y = q[1], z = q[2], w = q[3];
		Mat r = ident();
		r.m[0] = 1 - 2 * (y * y + z * z); r.m[1] = 2 * (x * y + z * w);     r.m[2] = 2 * (x * z - y * w);
		r.m[4] = 2 * (x * y - z * w);     r.m[5] = 1 - 2 * (x * x + z * z); r.m[6] = 2 * (y * z + x * w);
		r.m[8] = 2 * (x * z + y * w);     r.m[9] = 2 * (y * z - x * w);     r.m[10] = 1 - 2 * (x * x + y * y);
		r.m[12] = t[0]; r.m[13] = t[1]; r.m[14] = t[2];
		return r;
	}
	// Inverse of a rigid transform.
	Mat inv(const Mat& a)
	{
		Mat r = ident();
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { r.m[j * 4 + i] = a.m[i * 4 + j]; }
		for (int i = 0; i < 3; i++) { r.m[12 + i] = -(r.m[i] * a.m[12] + r.m[4 + i] * a.m[13] + r.m[8 + i] * a.m[14]); }
		return r;
	}
	Mat rotX(float a) { Mat r = ident(); const float c = cosf(a), s = sinf(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
	Mat rotY(float a) { Mat r = ident(); const float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
	Mat rotZ(float a) { Mat r = ident(); const float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }

	// Joint order of the WebXR hand: 0 wrist, 1-4 thumb, 5-9 index, 10-14 middle, 15-19 ring, 20-24 pinky.
	int parentOf(int j)
	{
		if (j == 0) return -1;
		if (j == 1 || j == 5 || j == 10 || j == 15 || j == 20) return 0;
		return j - 1;
	}

	bool s_init = false;
	float s_thumbUp[3] = { 0.6f, 0.0f, 0.0f };
	bool s_thumbOv = false;
	float s_thumbOvP[5] = { 0, 0, 0, 0, 0 };
	bool s_idxOv = false;
	float s_idxOvP[4] = { 0, 0, 0, 0 };
	bool s_fingOv = false;
	bool s_fjOv = false;
	float s_fjOvP[7] = { 0, 0, 0, 0, 0, 0, 0 };   // knuckle flexion x4, middle, end joint, spread
	float s_fingOvP[3] = { 0, 0, 0 };
	Mat s_rest[c_hmJoints], s_restInv[c_hmJoints], s_local[c_hmJoints];
	std::vector<HandSkinVtx> s_verts;
	std::vector<unsigned short> s_idx;

	const float c_wristCut = 0.040f;
	void init()
	{
		if (s_init) { return; }
		s_init = true;
		for (int j = 0; j < c_hmJoints; j++)
		{
			s_rest[j] = trs(&c_hmRest[j][0], &c_hmRest[j][3]);
			s_restInv[j] = inv(s_rest[j]);
		}
		for (int j = 0; j < c_hmJoints; j++)
		{
			const int p = parentOf(j);
			s_local[j] = p < 0 ? s_rest[j] : mul(s_restInv[p], s_rest[j]);
		}
		s_verts.resize(c_hmVerts);
		for (int i = 0; i < c_hmVerts; i++)
		{
			HandSkinVtx& v = s_verts[i];
			v.x = c_hmPos[i * 3]; v.y = c_hmPos[i * 3 + 1]; v.z = c_hmPos[i * 3 + 2];
			// No wrist: the stub past the heel of the palm (mesh +Y) is pressed into a short rounded end.
			if (v.y > c_wristCut) { v.y = c_wristCut + (v.y - c_wristCut) * 0.18f; }
			v.nx = c_hmNrm[i * 3]; v.ny = c_hmNrm[i * 3 + 1]; v.nz = c_hmNrm[i * 3 + 2];
			v.r = c_hmCol[i * 4]; v.g = c_hmCol[i * 4 + 1]; v.b = c_hmCol[i * 4 + 2]; v.spec = c_hmCol[i * 4 + 3];
			for (int k = 0; k < 4; k++) { v.j[k] = float(c_hmJ[i * 4 + k]); v.w[k] = c_hmW[i * 4 + k]; }
		}
		s_idx.assign(c_hmIdx, c_hmIdx + c_hmTris * 3);
	}
}

int handskin_jointCount() { return c_hmJoints; }
const std::vector<HandSkinVtx>& handskin_vertices() { init(); return s_verts; }
const std::vector<unsigned short>& handskin_indices() { init(); return s_idx; }

void handskin_pose(float ci, float co, float ct, float bones[][16])
{
	init();
	Mat rot[c_hmJoints];
	for (int j = 0; j < c_hmJoints; j++) { rot[j] = ident(); }
	// Fingers: a relaxed bend at rest, a full fist at 1. Flexion is a negative rotation about the
	// joint's local X (local +Y is the back of the hand, -Z points to the tip).
	const float relax[3] = { 0.12f, 0.18f, 0.10f };
	const float full[3] = { 1.50f, 1.75f, 1.15f };
	for (int f = 0; f < 4; f++)
	{
		const float c = f == 0 ? ci : (s_fingOv ? s_fingOvP[f - 1] : co);
		const int base = 5 + f * 5;
		// Ring and little finger metacarpals fold a little in a fist (palm arch).
		rot[base] = rotX(-c * (0.0f + 0.06f * f));
		for (int k = 0; k < 3; k++)
		{
			// Slight spread at rest that closes as the hand curls.
			Mat r = rotX(-(relax[k] * (1.0f + 0.25f * f) + c * full[k]));
			if (k == 0) { r = mul(rotY((f - 1.5f) * 0.05f * (1.0f - c)), r); }
			if (s_fjOv)
			{
				// Explicit finger joints (holding a flat object): knuckle per finger, the other two shared.
				const float a = k == 0 ? s_fjOvP[f] : s_fjOvP[3 + k];
				r = rotX(-a);
				if (k == 0) { r = mul(rotY((f - 1.5f) * s_fjOvP[6]), r); }
			}
			if (f == 0 && s_idxOv)
			{
				// Explicit index joints (on a trigger): flexion per joint, abduction at the knuckle.
				r = rotX(-s_idxOvP[k]);
				if (k == 0) { r = mul(rotY(s_idxOvP[3]), r); }
			}
			rot[base + 1 + k] = r;
		}
	}
	// Thumb: opposition at the base (it swings across the palm), then the two joints flex so the
	// tip rests on the middle phalanx of the index and middle fingers.
	// Open (0): thumbs up, the thumb straight and raised along the index side ("legal" gesture).
	const float o = 1.0f - ct;
	rot[1] = mul(rotZ(ct * 0.35f + o * s_thumbUp[2]), mul(rotY(ct * 0.25f + o * s_thumbUp[1]), rotX(-ct * 0.35f + o * s_thumbUp[0])));
	rot[2] = rotX(-(0.10f + ct * 0.55f) + o * 0.10f);
	rot[3] = rotX(-(0.15f + ct * 0.85f) + o * 0.15f);
	if (s_thumbOv)
	{
		// Explicit thumb (holding an object): base swing Z, Y, X, then the two joint flexions.
		rot[1] = mul(rotZ(s_thumbOvP[0]), mul(rotY(s_thumbOvP[1]), rotX(s_thumbOvP[2])));
		rot[2] = rotX(-s_thumbOvP[3]);
		rot[3] = rotX(-s_thumbOvP[4]);
	}
	Mat G[c_hmJoints];
	for (int j = 0; j < c_hmJoints; j++)
	{
		const int p = parentOf(j);
		G[j] = p < 0 ? s_rest[j] : mul(mul(G[p], s_local[j]), rot[j]);
		const Mat S = mul(G[j], s_restInv[j]);
		memcpy(bones[j], S.m, sizeof(S.m));
	}
}

void handskin_align(float side, float m[16])
{
	// v_H = (x_m - 0.032, -(z_m) + 0.016, y_m - 0.016): fingers -Y_m -> -Z_H, back +X_m -> +X_H,
	// index (-Z_m) on top (+Y_H); knuckles at H z = -0.045. Left hand: mirror X.
	Mat a = ident();
	a.m[0] = 1.0f; a.m[1] = 0.0f; a.m[2] = 0.0f;
	a.m[4] = 0.0f; a.m[5] = 0.0f; a.m[6] = 1.0f;
	a.m[8] = 0.0f; a.m[9] = -1.0f; a.m[10] = 0.0f;
	a.m[12] = -0.032f; a.m[13] = 0.016f; a.m[14] = -0.016f;
	if (side < 0.0f) { Mat mir = ident(); mir.m[0] = -1.0f; a = mul(mir, a); }
	memcpy(m, a.m, sizeof(a.m));
}

void handskin_cpu(const float H[16], float side, float ci, float co, float ct, std::vector<SVtx>& out, std::vector<uint32_t>& idx)
{
	init();
	float bones[c_hmJoints][16];
	handskin_pose(ci, co, ct, bones);
	Mat A; handskin_align(side, A.m);
	Mat Hm; memcpy(Hm.m, H, sizeof(Hm.m));
	const Mat M = mul(Hm, A);
	const size_t base = out.size();
	for (const HandSkinVtx& v : s_verts)
	{
		float p[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 };
		for (int k = 0; k < 4; k++)
		{
			const float w = v.w[k];
			if (w <= 0.0f) continue;
			const float* B = bones[int(v.j[k])];
			for (int i = 0; i < 3; i++)
			{
				p[i] += w * (B[i] * v.x + B[4 + i] * v.y + B[8 + i] * v.z + B[12 + i]);
				n[i] += w * (B[i] * v.nx + B[4 + i] * v.ny + B[8 + i] * v.nz);
			}
		}
		SVtx o;
		o.x = M.m[0] * p[0] + M.m[4] * p[1] + M.m[8] * p[2] + M.m[12];
		o.y = M.m[1] * p[0] + M.m[5] * p[1] + M.m[9] * p[2] + M.m[13];
		o.z = M.m[2] * p[0] + M.m[6] * p[1] + M.m[10] * p[2] + M.m[14];
		float nx = M.m[0] * n[0] + M.m[4] * n[1] + M.m[8] * n[2];
		float ny = M.m[1] * n[0] + M.m[5] * n[1] + M.m[9] * n[2];
		float nz = M.m[2] * n[0] + M.m[6] * n[1] + M.m[10] * n[2];
		const float l = sqrtf(nx * nx + ny * ny + nz * nz);
		if (l > 0.0f) { nx /= l; ny /= l; nz /= l; }
		if (side < 0.0f) { /* mirrored: normals stay outward after the reflection */ }
		o.nx = nx; o.ny = ny; o.nz = nz;
		o.r = v.r; o.g = v.g; o.b = v.b; o.spec = v.spec; o.emis = 0.0f;
		out.push_back(o);
	}
	for (size_t i = 0; i < s_idx.size(); i += 3)
	{
		if (side < 0.0f) { idx.push_back(uint32_t(base + s_idx[i])); idx.push_back(uint32_t(base + s_idx[i + 2])); idx.push_back(uint32_t(base + s_idx[i + 1])); }
		else { idx.push_back(uint32_t(base + s_idx[i])); idx.push_back(uint32_t(base + s_idx[i + 1])); idx.push_back(uint32_t(base + s_idx[i + 2])); }
	}
}

void handskin_setThumbUp(float ax, float ay, float az) { s_thumbUp[0] = ax; s_thumbUp[1] = ay; s_thumbUp[2] = az; }

void handskin_setIndexOverride(const float* a4)
{
	s_idxOv = a4 != nullptr;
	if (a4) { memcpy(s_idxOvP, a4, sizeof(s_idxOvP)); }
}

void handskin_setFingerOverride(const float* c3)
{
	s_fingOv = c3 != nullptr;
	if (c3) { memcpy(s_fingOvP, c3, sizeof(s_fingOvP)); }
}

void handskin_setFingerJoints(const float* p7)
{
	s_fjOv = p7 != nullptr;
	if (p7) { memcpy(s_fjOvP, p7, sizeof(s_fjOvP)); }
}

void handskin_setThumbOverride(const float* p)
{
	s_thumbOv = p != nullptr;
	if (p) { memcpy(s_thumbOvP, p, sizeof(s_thumbOvP)); }
}
