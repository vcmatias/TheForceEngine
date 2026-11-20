// Procedural 3D hands and weapons.
// - Hands: palm + 3-segment fingers + thumb, textured with the game's own hand bitmap
//   (RHAND1.BM), animated by the Touch controller inputs.
// - Weapons: low-poly models of the Dark Forces arsenal held by the right hand, with
//   muzzle flash and an optional laser sight.
// Everything is built in the controller *aim* frame (-Z = pointing ray, +Y up), which is
// unambiguous across runtimes; the grip pose only supplies where the handle is.
#include "tfe_xr_hands.h"
#ifndef FIST_X
#define FIST_X 0.6f
#define FIST_Y 0.5f
#define FIST_S 0.5f
#endif
#include <math.h>
#include <cstdio>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "tfe_xr_weapons.h"
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TFEQuestXR", __VA_ARGS__)

namespace
{
	struct M4 { float m[16]; };  // column-major
	struct V3 { float x, y, z; };

	M4 ident() { M4 r = {}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }
	M4 mul(const M4& a, const M4& b)
	{
		M4 r;
		for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++)
		{
			float s = 0.0f;
			for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
			r.m[c * 4 + rr] = s;
		}
		return r;
	}
	M4 translate(float x, float y, float z) { M4 r = ident(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
	M4 scale(float x, float y, float z) { M4 r = ident(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
	M4 rotY(float a) { M4 r = ident(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
	M4 rotX(float a) { M4 r = ident(); float c = cosf(a), s = sinf(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
	M4 rotZ(float a) { M4 r = ident(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }
	M4 fromPose(const XrPosef& p)
	{
		const float x = p.orientation.x, y = p.orientation.y, z = p.orientation.z, w = p.orientation.w;
		M4 r = ident();
		r.m[0] = 1 - 2*(y*y + z*z); r.m[1] = 2*(x*y + z*w);     r.m[2] = 2*(x*z - y*w);
		r.m[4] = 2*(x*y - z*w);     r.m[5] = 1 - 2*(x*x + z*z); r.m[6] = 2*(y*z + x*w);
		r.m[8] = 2*(x*z + y*w);     r.m[9] = 2*(y*z - x*w);     r.m[10] = 1 - 2*(x*x + y*y);
		r.m[12] = p.position.x; r.m[13] = p.position.y; r.m[14] = p.position.z;
		return r;
	}
	M4 inverseRigid(const M4& a)
	{
		M4 r = ident();
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r.m[j * 4 + i] = a.m[i * 4 + j];
		for (int i = 0; i < 3; i++) r.m[12 + i] = -(r.m[i] * a.m[12] + r.m[4 + i] * a.m[13] + r.m[8 + i] * a.m[14]);
		return r;
	}
	V3 xform(const M4& m, float x, float y, float z)
	{
		return { m.m[0]*x + m.m[4]*y + m.m[8]*z + m.m[12], m.m[1]*x + m.m[5]*y + m.m[9]*z + m.m[13], m.m[2]*x + m.m[6]*y + m.m[10]*z + m.m[14] };
	}
	XrQuaternionf quatOf(const M4& m)
	{
		XrQuaternionf q;
		const float tr = m.m[0] + m.m[5] + m.m[10];
		if (tr > 0.0f)
		{
			const float s = sqrtf(tr + 1.0f) * 2.0f;
			q.w = 0.25f * s; q.x = (m.m[6] - m.m[9]) / s; q.y = (m.m[8] - m.m[2]) / s; q.z = (m.m[1] - m.m[4]) / s;
		}
		else if (m.m[0] > m.m[5] && m.m[0] > m.m[10])
		{
			const float s = sqrtf(1.0f + m.m[0] - m.m[5] - m.m[10]) * 2.0f;
			q.w = (m.m[6] - m.m[9]) / s; q.x = 0.25f * s; q.y = (m.m[4] + m.m[1]) / s; q.z = (m.m[8] + m.m[2]) / s;
		}
		else if (m.m[5] > m.m[10])
		{
			const float s = sqrtf(1.0f + m.m[5] - m.m[0] - m.m[10]) * 2.0f;
			q.w = (m.m[8] - m.m[2]) / s; q.x = (m.m[4] + m.m[1]) / s; q.y = 0.25f * s; q.z = (m.m[9] + m.m[6]) / s;
		}
		else
		{
			const float s = sqrtf(1.0f + m.m[10] - m.m[0] - m.m[5]) * 2.0f;
			q.w = (m.m[1] - m.m[4]) / s; q.x = (m.m[8] + m.m[2]) / s; q.y = (m.m[9] + m.m[6]) / s; q.z = 0.25f * s;
		}
		return q;
	}

	struct Vtx { float x, y, z, u, v, r, g, b, a, emis; };
	std::vector<Vtx> s_vHand, s_vSolid, s_vAdd;

	// Unit cube from (-0.5,-0.5,-1) to (0.5,0.5,0): segments grow along -Z from their joint.
	void addBoxRaw(std::vector<Vtx>& out, const M4& m, float r, float g, float b, float emis, bool handUv)
	{
		static const float c[8][3] =
		{
			{-0.5f,-0.5f, 0.0f}, { 0.5f,-0.5f, 0.0f}, { 0.5f, 0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f},
			{-0.5f,-0.5f,-1.0f}, { 0.5f,-0.5f,-1.0f}, { 0.5f, 0.5f,-1.0f}, {-0.5f, 0.5f,-1.0f},
		};
		static const int f[6][4] = { {0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0} };
		static const float shade[6] = { 0.85f, 0.85f, 0.72f, 0.95f, 1.0f, 0.55f };
		V3 p[8];
		for (int i = 0; i < 8; i++) { p[i] = xform(m, c[i][0], c[i][1], c[i][2]); }
		static const float uvH[4][2] = { {0.3f,0.3f}, {0.7f,0.3f}, {0.7f,0.7f}, {0.3f,0.7f} };
		static const float uvS[4][2] = { {0.0f,0.0f}, {1.0f,0.0f}, {1.0f,1.0f}, {0.0f,1.0f} };
		for (int k = 0; k < 6; k++)
		{
			const int idx[6] = { 0, 1, 2, 0, 2, 3 };
			const float sh = (emis > 0.5f) ? 1.0f : shade[k];
			for (int t = 0; t < 6; t++)
			{
				const V3& q = p[f[k][idx[t]]];
				Vtx v;
				v.x = q.x; v.y = q.y; v.z = q.z;
				v.u = handUv ? uvH[idx[t]][0] : uvS[idx[t]][0];
				v.v = handUv ? uvH[idx[t]][1] : uvS[idx[t]][1];
				v.r = r * sh; v.g = g * sh; v.b = b * sh; v.a = 1.0f; v.emis = emis;
				out.push_back(v);
			}
		}
	}
	void addHandBox(const M4& m, float shadeBase) { addBoxRaw(s_vHand, m, shadeBase, shadeBase, shadeBase, 0.0f, true); }

	// Unit cylinder along Z from 0 to -1, radius 0.5, 10 sides, capped.
	void addCylRaw(std::vector<Vtx>& out, const M4& m, float r, float g, float b, float emis)
	{
		const int N = 10;
		auto emit = [&](float x, float y, float z, float u, float v, float sh)
		{
			const V3 q = xform(m, x, y, z);
			const float k = (emis > 0.5f) ? 1.0f : sh;
			Vtx vt = { q.x, q.y, q.z, u, v, r * k, g * k, b * k, 1.0f, emis };
			out.push_back(vt);
		};
		for (int i = 0; i < N; i++)
		{
			const float a0 = 6.2831853f * i / N, a1 = 6.2831853f * (i + 1) / N;
			const float x0 = 0.5f * cosf(a0), y0 = 0.5f * sinf(a0), x1 = 0.5f * cosf(a1), y1 = 0.5f * sinf(a1);
			const float ny = sinf((a0 + a1) * 0.5f), nx = cosf((a0 + a1) * 0.5f);
			const float sh = 0.68f + 0.27f * ny + 0.05f * nx;
			const float u0 = float(i) / N, u1 = float(i + 1) / N;
			emit(x0, y0, 0, u0, 0, sh); emit(x1, y1, 0, u1, 0, sh); emit(x1, y1, -1, u1, 1, sh);
			emit(x0, y0, 0, u0, 0, sh); emit(x1, y1, -1, u1, 1, sh); emit(x0, y0, -1, u0, 1, sh);
			// caps
			emit(0, 0, 0, 0.5f, 0.5f, 0.8f); emit(x1, y1, 0, 0.5f, 0.5f, 0.8f); emit(x0, y0, 0, 0.5f, 0.5f, 0.8f);
			emit(0, 0, -1, 0.5f, 0.5f, 0.75f); emit(x0, y0, -1, 0.5f, 0.5f, 0.75f); emit(x1, y1, -1, 0.5f, 0.5f, 0.75f);
		}
	}

	// ---------------- Voxels ----------------
	// Everything the player holds is built from small cubes ("3D pixel art"), colored with
	// the game's own palette: glove/cuff/sleeve colors come from RHAND1.BM, weapons are
	// extruded from the Dark Forces pickup sprites (side views).
	float c_handVox = 0.0058f;   // voxel hand: the VOXEL weapons' size; VOXEL FINO uses 1/3
	struct VoxPalette { std::vector<V3> glove, cuff, sleeve; } s_vpal;
	enum VoxKind { VK_GLOVE = 0, VK_CUFF, VK_SLEEVE, VK_BAND, VK_METAL, VK_LED };

	unsigned int vhash(int a, int b, int c, int d)
	{
		unsigned int h = 2166136261u;
		const int v[4] = { a, b, c, d };
		for (int i = 0; i < 4; i++) { h ^= unsigned(v[i]); h *= 16777619u; }
		return h;
	}
	V3 voxColor(int kind, unsigned int h)
	{
		static const V3 fbGlove[3] = { {0.10f,0.10f,0.12f}, {0.16f,0.16f,0.18f}, {0.22f,0.22f,0.25f} };
		static const V3 fbCuff[2] = { {0.86f,0.86f,0.88f}, {0.70f,0.70f,0.74f} };
		static const V3 fbSleeve[2] = { {0.78f,0.42f,0.16f}, {0.62f,0.32f,0.12f} };
		if (kind == VK_BAND) { static const V3 c[2] = { {0.10f,0.09f,0.08f}, {0.14f,0.12f,0.10f} }; return c[h % 2]; }
		if (kind == VK_METAL) { static const V3 c[3] = { {0.30f,0.31f,0.34f}, {0.38f,0.39f,0.42f}, {0.24f,0.25f,0.28f} }; return c[h % 3]; }
		if (kind == VK_LED) { return { 1.0f, 0.25f, 0.1f }; }
		const std::vector<V3>& v = (kind == VK_GLOVE) ? s_vpal.glove : (kind == VK_CUFF) ? s_vpal.cuff : s_vpal.sleeve;
		if (!v.empty()) { return v[h % v.size()]; }
		if (kind == VK_GLOVE) return fbGlove[h % 3];
		if (kind == VK_CUFF) return fbCuff[h % 2];
		return fbSleeve[h % 2];
	}

	// Emits one voxel face. p0 = voxel min corner in local units, d = voxel size, f = face index.
	void voxFace(std::vector<Vtx>& out, const M4& m, const float p0[3], const float d[3], int f, const V3& col)
	{
		// faces: 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z
		static const float shade[6] = { 0.80f, 0.80f, 1.00f, 0.55f, 0.90f, 0.90f };
		static const int axis[6] = { 0, 0, 1, 1, 2, 2 };
		static const int side[6] = { 1, 0, 1, 0, 1, 0 };
		const int a = axis[f], b = (a + 1) % 3, c = (a + 2) % 3;
		float q[4][3];
		const int uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
		for (int i = 0; i < 4; i++)
		{
			q[i][a] = p0[a] + (side[f] ? d[a] : 0.0f);
			q[i][b] = p0[b] + uv[i][0] * d[b];
			q[i][c] = p0[c] + uv[i][1] * d[c];
		}
		const int idx[6] = { 0, 1, 2, 0, 2, 3 };
		const float sh = shade[f];
		for (int t = 0; t < 6; t++)
		{
			const float* w = q[idx[t]];
			const V3 v = xform(m, w[0], w[1], w[2]);
			Vtx o = { v.x, v.y, v.z, 0.5f, 0.5f, col.x * sh, col.y * sh, col.z * sh, 1.0f, 0.0f };
			out.push_back(o);
		}
	}

	// Voxelized hand segment: rounded box (-sx/2..sx/2, -sy/2..sy/2, -sz..0) in the frame "base".
	// Meshes are built once (local space, cached by shape) and drawn with their own matrix.
	struct SegMesh { int kind, nx, ny, nz, seed, side; float sx, sy, sz; std::vector<Vtx> local; GLuint vbo = 0; };
	std::vector<SegMesh> s_segMeshes;
	struct SegDraw { int mesh; M4 m; };
	std::vector<SegDraw> s_segDraws;
	struct WheelDraw { int id; M4 m; };
	std::vector<WheelDraw> s_wheelDraws;

	// Glove shading ramp (dark leather -> gloss), taken from the game's hand bitmap when available.
	V3 gloveRamp(float t, unsigned int h)
	{
		static const V3 fb[5] = { {0.05f,0.05f,0.08f}, {0.11f,0.12f,0.17f}, {0.21f,0.23f,0.31f}, {0.45f,0.49f,0.60f}, {0.88f,0.91f,0.97f} };
		t += (float((h >> 8) & 255) / 255.0f - 0.5f) * 0.10f;
		t = t < 0.0f ? 0.0f : (t > 0.999f ? 0.999f : t);
		const float f = t * 4.0f; const int i = (int)f; const float k = f - i;
		const V3& a = fb[i]; const V3& b = fb[i + 1 > 4 ? 4 : i + 1];
		return { a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k };
	}

	int segMesh(float sx, float sy, float sz, int kind, int seed, float side)
	{
		const int nx = (int)fmaxf(1.0f, roundf(sx / c_handVox));
		const int ny = (int)fmaxf(1.0f, roundf(sy / c_handVox));
		const int nz = (int)fmaxf(1.0f, roundf(sz / c_handVox));
		const int sd = side > 0.0f ? 1 : -1;
		for (size_t i = 0; i < s_segMeshes.size(); i++)
		{
			const SegMesh& m = s_segMeshes[i];
			if (m.kind == kind && m.nx == nx && m.ny == ny && m.nz == nz && m.seed == seed && m.side == sd) { return (int)i; }
		}
		SegMesh sm;
		sm.kind = kind; sm.nx = nx; sm.ny = ny; sm.nz = nz; sm.seed = seed; sm.side = sd; sm.sx = sx; sm.sy = sy; sm.sz = sz;
		const float d[3] = { sx / nx, sy / ny, sz / nz };
		const bool organic = (kind == VK_GLOVE || kind == VK_CUFF || kind == VK_SLEEVE);
		const bool finger = organic && kind == VK_GLOVE && sx < 0.025f;
		const float pw = finger ? 2.2f : (kind == VK_GLOVE ? 2.8f : 2.4f);   // superellipse exponent of the cross-section
		auto occ = [&](int i, int j, int k)
		{
			if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) return false;
			if (!organic) { return !(nx >= 3 && ny >= 3 && (i == 0 || i == nx - 1) && (j == 0 || j == ny - 1)); }
			const float u = (i + 0.5f) / nx * 2.0f - 1.0f, v = (j + 0.5f) / ny * 2.0f - 1.0f;
			float r = 1.0f;
			const float w = (k + 0.5f) / nz;          // 0 at the joint, 1 at the tip
			if (finger)
			{
				r = 1.0f - 0.10f * w;                  // slight taper
				if (seed % 3 == 2 || seed >= 32) { const float e = (w - 0.70f) / 0.30f; if (e > 0.0f) r *= sqrtf(fmaxf(0.0f, 1.0f - e * e * 0.85f)); }   // round fingertip
			}
			if (kind == VK_SLEEVE) { r = 0.92f + 0.08f * w; }                  // forearm widens toward the elbow
			return powf(fabsf(u), pw) + powf(fabsf(v), pw) <= r * r;
		};
		// Back of the hand / outer side of the arm: +X*side (fingers curl toward -X*side).
		static const int nb[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,-1}, {0,0,1} };
		for (int k = 0; k < nz; k++) for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++)
		{
			if (!occ(i, j, k)) continue;
			bool exposed = false;
			for (int f = 0; f < 6 && !exposed; f++) { exposed = !occ(i + nb[f][0], j + nb[f][1], k + nb[f][2]); }
			if (!exposed) continue;
			const unsigned int h = vhash(i, j, k, seed);
			V3 col;
			if (kind == VK_GLOVE || kind == VK_CUFF)
			{
				// Surface normal of the superellipse cross-section; key light from the back of the hand and
				// the index side, glossy highlight like the shine in the original art.
				const float u = (i + 0.5f) / nx * 2.0f - 1.0f, v = (j + 0.5f) / ny * 2.0f - 1.0f;
				float gx = (u < 0.0f ? -1.0f : 1.0f) * powf(fabsf(u), pw - 1.0f) * float(sd);
				float gy = (v < 0.0f ? -1.0f : 1.0f) * powf(fabsf(v), pw - 1.0f);
				const float gl = sqrtf(gx * gx + gy * gy) + 1e-4f; gx /= gl; gy /= gl;
				const float ndl = fmaxf(0.0f, gx * 0.80f + gy * 0.60f);
				const float w = (k + 0.5f) / nz;
				float spec = powf(ndl, 14.0f);
				if (!finger) { spec *= (w > 0.78f ? 1.0f : 0.15f); }          // palm: shine on the knuckle ridge
				if (((h >> 3) & 7) == 0) { spec *= 0.4f; }                   // break the streak up a little
				float t = 0.14f + 0.34f * ndl + 0.60f * spec;
				if (kind == VK_CUFF) { t *= 0.8f; }
				if (finger && (k == 0 || k == nz - 1)) { t *= 0.55f; }       // creases at the joints
				col = gloveRamp(t, h);
			}
			else { col = voxColor(kind, h); }
			const float p0[3] = { -sx * 0.5f + i * d[0], -sy * 0.5f + j * d[1], -(k + 1) * d[2] };
			for (int f = 0; f < 6; f++)
			{
				if (!occ(i + nb[f][0], j + nb[f][1], k + nb[f][2])) { voxFace(sm.local, ident(), p0, d, f, col); }
			}
		}
		s_segMeshes.push_back(std::move(sm));
		return (int)s_segMeshes.size() - 1;
	}

	float s_buildSide = 1.0f;
	void addVoxSeg(const M4& base, float sx, float sy, float sz, int kind, int seed)
	{
		const int id = segMesh(sx, sy, sz, kind, seed, s_buildSide);
#ifdef HANDS_HOST_TEST
		for (const Vtx& lv : s_segMeshes[id].local)
		{
			Vtx v = lv; const V3 q = xform(base, lv.x, lv.y, lv.z); v.x = q.x; v.y = q.y; v.z = q.z; s_vHand.push_back(v);
		}
#else
		s_segDraws.push_back({ id, base });
#endif
	}

	// Wrist computer on the left forearm: frame origin on the sleeve surface, +Y = outward
	// (between the top and the back of the wrist), -Z = toward the fingers.
	// Pause menu PDA gripped by its left side in the left hand: screen frame (x image right, y image up,
	// z toward the eyes). The palm faces +X (image right), the fingers wrap the back of the case and the
	// thumb rests on the bottom-left corner of the screen.
	// Fitted offline (hosttest/pdafit.cpp): no finger inside the case, fingertips on its back, thumb tip
	// just outside the bottom-left screen corner.
	// Refitted for the bezel case (hosttest/pdafit2.cpp): the left edge pinched between the thumb, resting
	// on the left bezel and pointing up and in (off the screen), and the fingers flat on the back.
	// 0.24: book hold (the usual one-hand tablet hold, hosttest/pdafit5.cpp): the left edge seated in the
	// palm and the thumb web, the fingers spread flat across the back, the thumb lying along the left
	// bezel off the screen. The case is turned a little in the hand (yaw, tilt, roll) for it.
	float s_pdaT[3] = { 0.1379f, 0.0159f, -0.0466f };
	float s_pdaYaw = 0.0580f, s_pdaTilt = -0.2848f, s_pdaRoll = -0.5570f;
	float s_pdaCurl[3] = { 0.40f, 0.80f, 0.5f };
	float s_pdaJoints[7] = { 1.1844f, 1.3212f, 1.2678f, 1.1627f, 0.3648f, 0.0f, 0.2000f };   // knuckles x4, middle, end, spread
	float s_pdaThumb[5] = { 0.0179f, 0.2768f, 0.0609f, 0.1338f, 0.6395f };
	float s_pdaScale = 1.0f;
	// PDA case in its frame (case center at the origin, screen facing +Z), shaped like the mission PDA
	// (MENU.LFD pda.anim frame, 320x200, square pixels, 20 cm wide at size 1).
	struct PdaCase { float l, r, b, t, zb, zf; };
	// Two faces for the case, both 320x200 canvases (MENU.LFD art): 0 = settings and pause menus (the
	// configuration screen frame), 1 = mission PDA (pda.anim: its window shows the game's PDA content,
	// cropped to that window, so the frame is the case itself).
	struct PdaArt { int win[4]; };
	PdaArt s_art[2] = { { { 53, 13, 268, 167 } }, { { 23, 11, 304, 166 } } };
	const int s_bezelW = 320, s_bezelH = 200;
	int s_pdaMode = 0;
	// Live face: the game's own 320x200 canvas (frame with its buttons, cursor) copied each frame into a
	// texture by tfe_xr.cpp; the static art is only the fallback before the first copy.
	bool s_pdaLiveOk = false;
	bool s_pdaFit = false;   // frameless page (escape menu, controls help): static configuration frame, page fitted in its window
	PdaCase pdaCase(float scale)
	{
		const float cw = 0.20f * scale, ch = cw * float(s_bezelH) / float(s_bezelW);
		return { -0.5f * cw, 0.5f * cw, -0.5f * ch, 0.5f * ch, -0.0050f, 0.0070f };
	}
	// Screen layer on the case, inside the window of the current face: the mission PDA fills the window
	// exactly (cropped game image), the menus fit 16:10 in it. Center (x, y) in the case frame, size.
	void pdaScreenRect(float scale, float* cx, float* cy, float* w, float* h = nullptr)
	{
		const PdaCase c = pdaCase(scale);
		const int* win = s_art[s_pdaMode].win;
		const float px = (c.r - c.l) / float(s_bezelW);
		const float ww = float(win[2] + 1 - win[0]) * px, wh = float(win[3] + 1 - win[1]) * px;
		*cx = (0.5f * float(win[0] + win[2] + 1) - 0.5f * float(s_bezelW)) * px;
		*cy = (0.5f * float(s_bezelH) - 0.5f * float(win[1] + win[3] + 1)) * px;
		if (!s_pdaFit && (s_pdaLiveOk || s_pdaMode == 1)) { *w = ww; if (h) { *h = wh; } return; }   // the game image cropped to the window
		*w = fminf(ww, wh * 1.6f);
		if (h) { *h = *w / 1.6f; }
	}
	M4 pdaFrame(const M4& H)
	{
		// The hand is fitted at scale 1; other sizes grow from the bottom-left corner (where the hand holds).
		const PdaCase c1 = pdaCase(1.0f), cs = pdaCase(s_pdaScale);
		const float dx = c1.l - cs.l, dy = c1.b - cs.b;
		const M4 P = mul(mul(mul(mul(H, translate(s_pdaT[0], s_pdaT[1], s_pdaT[2])), rotY(s_pdaYaw)), rotX(s_pdaTilt)), rotZ(s_pdaRoll));
		return mul(P, translate(dx, dy, 0.0f));
	}
	M4 watchFrame(const M4& H, float side)
	{
		M4 w = mul(H, translate(side * 0.020f, 0.020f, 0.064f));   // on the short glove stub, next to the hand
		return mul(w, rotZ(-side * 0.785f));
	}

	// Hand frame: fingers along -Z, knuckles stacked along +Y (index on top), fingers curl
	// toward -X*side (side = +1 right hand, -1 left hand), i.e. toward the body center.
	void buildHand(const M4& H, float side, float curlIndex, float curlOthers, float curlThumb)
	{
		const float c = -side;   // curl direction along X
		s_buildSide = side;
		const M4 palmBase = mul(H, translate(0.0f, 0.0f, 0.045f));
		addVoxSeg(palmBase, 0.034f, 0.090f, 0.095f, VK_GLOVE, 1);
		// Glove wrist, then Kyle's bare forearm (as in the original first-person art).
		addVoxSeg(mul(palmBase, translate(c * 0.001f, -0.003f, 0.024f)), 0.036f, 0.072f, 0.026f, VK_CUFF, 2);
		if (side < 0.0f)
		{
			// Left: glove wrist up to the end of the watch (no forearm).
			addVoxSeg(mul(palmBase, translate(c * 0.002f, -0.003f, 0.058f)), 0.042f, 0.062f, 0.030f, VK_CUFF, 3);
			// Wrist computer: strap around the sleeve and a metal case; the HUD/map is its screen.
			addVoxSeg(mul(palmBase, translate(c * 0.002f, -0.003f, 0.034f)), 0.050f, 0.068f, 0.012f, VK_BAND, 4);
			const M4 wf = watchFrame(H, side);
			// Landscape case: long side along the forearm.
			addVoxSeg(mul(wf, translate(0.0f, 0.0f, 0.031f)), 0.042f, 0.012f, 0.062f, VK_METAL, 5);
			addVoxSeg(mul(wf, translate(0.017f, 0.006f, -0.024f)), 0.006f, 0.006f, 0.006f, VK_LED, 6);
		}

		const float fingerY[4] = { 0.032f, 0.0105f, -0.0105f, -0.031f };
		const float lenScale[4] = { 1.0f, 1.07f, 1.0f, 0.82f };
		const float segLen[3] = { 0.040f, 0.026f, 0.020f };
		const float jointMax[3] = { 1.35f, 1.6f, 1.1f };
		for (int fi = 0; fi < 4; fi++)
		{
			const float curl = (fi == 0) ? curlIndex : curlOthers;
			M4 m = mul(palmBase, translate(0.0f, fingerY[fi], -0.095f));
			for (int s = 0; s < 3; s++)
			{
				m = mul(m, rotY(side * curl * jointMax[s]));
				const float len = segLen[s] * lenScale[fi];
				addVoxSeg(m, 0.0215f, 0.0215f, len, VK_GLOVE, 10 + fi * 3 + s);
				m = mul(m, translate(0.0f, 0.0f, -len));
			}
		}
		// Opposable thumb: rooted low on the palm side near the wrist (index edge), it rises along the index
		// when lifted (thumbs up) and swings across the palm, in front of the curled fingers, when closed.
		const float ct = curlThumb;
		// Placed offline (tip on the side of the curled index when closed, raised over the index when open).
		M4 t = mul(palmBase, translate(c * 0.018f, 0.020f, -0.030f));
		t = mul(t, rotY(side * 0.45f * ct));                // toward the palm side when closed (opposition)
		t = mul(t, rotX(1.0f - 0.8f * ct));                 // raised when open (thumbs up), over the curled fingers when closed
		const float thumbLen[3] = { 0.032f, 0.026f, 0.020f };
		for (int s = 0; s < 3; s++)
		{
			addVoxSeg(t, 0.0225f, 0.0225f, thumbLen[s], VK_GLOVE, 32 + s);
			t = mul(t, translate(0.0f, 0.0f, -thumbLen[s]));
			t = mul(t, rotY(side * ct * (s == 0 ? 0.12f : 0.20f)));
		}
	}

	// ---------------- Weapons ----------------
	struct Part { float cx, cy, cz, sx, sy, sz, tilt; float r, g, b; float emis; int shape; };  // shape 1 = cylinder along Z
	struct Model { const Part* parts; int count; V3 muzzle; float flash[3]; float hapticAmp, hapticSec; };

	#define GM 0.30f, 0.31f, 0.34f
	#define BK 0.13f, 0.13f, 0.15f
	#define BR 0.38f, 0.25f, 0.15f
	#define SV 0.66f, 0.67f, 0.71f
	#define OL 0.34f, 0.37f, 0.27f
	#define TN 0.55f, 0.46f, 0.33f
	const float HT = -0.35f;  // handle tilt (top leans forward)

	// cx cy cz | sx sy sz | tilt | color | emissive | shape (1 = cylinder along Z)
	const Part c_pistol[] =
	{
		{ 0.0f, 0.024f, -0.045f, 0.026f, 0.036f, 0.130f, 0, GM, 0, 0 },
		{ 0.0f, 0.030f, -0.110f, 0.020f, 0.020f, 0.100f, 0, BK, 0, 1 },
		{ 0.0f, 0.030f, -0.165f, 0.026f, 0.026f, 0.020f, 0, GM, 0, 1 },
		{ 0.0f, 0.054f, -0.060f, 0.014f, 0.014f, 0.070f, 0, BK, 0, 1 },
		{ 0.0f, -0.045f, 0.018f, 0.026f, 0.095f, 0.036f, HT, BR, 0, 0 },
		{ 0.0f, -0.006f, -0.030f, 0.006f, 0.018f, 0.032f, 0, BK, 0, 0 },
		{ -0.014f, 0.032f, 0.005f, 0.004f, 0.006f, 0.006f, 0, 1.0f, 0.2f, 0.1f, 1, 0 },
	};
	const Part c_rifle[] =
	{
		{ 0.0f, 0.026f, -0.100f, 0.034f, 0.034f, 0.240f, 0, BK, 0, 1 },
		{ 0.0f, 0.026f, -0.140f, 0.040f, 0.040f, 0.008f, 0, GM, 0, 1 },
		{ 0.0f, 0.026f, -0.170f, 0.040f, 0.040f, 0.008f, 0, GM, 0, 1 },
		{ 0.0f, 0.026f, -0.200f, 0.040f, 0.040f, 0.008f, 0, GM, 0, 1 },
		{ 0.0f, 0.026f, -0.235f, 0.026f, 0.026f, 0.040f, 0, GM, 0, 1 },
		{ -0.004f, 0.058f, -0.070f, 0.016f, 0.016f, 0.100f, 0, BK, 0, 1 },
		{ -0.004f, 0.058f, -0.121f, 0.012f, 0.012f, 0.002f, 0, 0.3f, 0.6f, 0.9f, 1, 1 },
		{ -0.028f, 0.018f, -0.040f, 0.030f, 0.016f, 0.050f, 0, GM, 0, 0 },
		{ 0.0f, -0.045f, 0.018f, 0.026f, 0.095f, 0.036f, HT, BK, 0, 0 },
		{ 0.0f, -0.006f, -0.030f, 0.006f, 0.018f, 0.032f, 0, BK, 0, 0 },
		{ 0.0f, 0.052f, 0.060f, 0.008f, 0.008f, 0.100f, 0, GM, 0, 0 },
	};
	const Part c_thermal[] =
	{
		{ 0.0f, -0.045f, -0.010f, 0.058f, 0.058f, 0.052f, 0, SV, 0, 1 },
		{ 0.0f, -0.045f, -0.010f, 0.058f, 0.058f, 0.052f, 1.5708f, SV, 0, 1 },
		{ 0.0f, -0.045f, -0.010f, 0.064f, 0.064f, 0.010f, 0, GM, 0, 1 },
		{ 0.0f, -0.016f, -0.010f, 0.012f, 0.010f, 0.012f, 0, 1.0f, 0.15f, 0.1f, 1, 0 },
	};
	const Part c_repeater[] =
	{
		{ 0.0f, 0.030f, -0.070f, 0.045f, 0.050f, 0.180f, 0, GM, 0, 0 },
		{ -0.012f, 0.044f, -0.210f, 0.013f, 0.013f, 0.120f, 0, BK, 0, 1 },
		{ 0.012f, 0.044f, -0.210f, 0.013f, 0.013f, 0.120f, 0, BK, 0, 1 },
		{ 0.0f, 0.022f, -0.210f, 0.013f, 0.013f, 0.120f, 0, BK, 0, 1 },
		{ 0.0f, 0.036f, -0.165f, 0.052f, 0.052f, 0.015f, 0, GM, 0, 1 },
		{ 0.0f, -0.008f, -0.100f, 0.040f, 0.040f, 0.060f, 0, BK, 0, 1 },
		{ 0.0f, -0.045f, 0.018f, 0.028f, 0.095f, 0.038f, HT, BK, 0, 0 },
		{ 0.0f, 0.030f, 0.040f, 0.040f, 0.050f, 0.040f, 0, GM, 0, 0 },
		{ 0.024f, 0.045f, -0.020f, 0.004f, 0.010f, 0.020f, 0, 0.2f, 1.0f, 0.3f, 1, 0 },
	};
	const Part c_fusion[] =
	{
		{ 0.0f, 0.030f, -0.060f, 0.050f, 0.050f, 0.170f, 0, TN, 0, 0 },
		{ 0.0f, 0.052f, -0.200f, 0.015f, 0.015f, 0.110f, 0, GM, 0, 1 },
		{ 0.0f, 0.008f, -0.200f, 0.015f, 0.015f, 0.110f, 0, GM, 0, 1 },
		{ -0.022f, 0.030f, -0.200f, 0.015f, 0.015f, 0.110f, 0, GM, 0, 1 },
		{ 0.022f, 0.030f, -0.200f, 0.015f, 0.015f, 0.110f, 0, GM, 0, 1 },
		{ 0.0f, 0.030f, -0.150f, 0.060f, 0.060f, 0.025f, 0, BK, 0, 1 },
		{ 0.0f, -0.045f, 0.018f, 0.026f, 0.095f, 0.036f, HT, BK, 0, 0 },
		{ 0.0f, 0.056f, -0.050f, 0.010f, 0.004f, 0.090f, 0, 1.0f, 0.5f, 0.1f, 1, 0 },
	};
	const Part c_mortar[] =
	{
		{ 0.0f, 0.040f, -0.100f, 0.070f, 0.070f, 0.280f, 0, OL, 0, 1 },
		{ 0.0f, 0.040f, -0.245f, 0.080f, 0.080f, 0.030f, 0, GM, 0, 1 },
		{ 0.0f, 0.040f, -0.261f, 0.050f, 0.050f, 0.004f, 0, 0.04f, 0.04f, 0.04f, 0, 1 },
		{ 0.0f, 0.040f, 0.050f, 0.075f, 0.075f, 0.030f, 0, GM, 0, 1 },
		{ 0.0f, -0.045f, 0.018f, 0.028f, 0.095f, 0.038f, HT, BK, 0, 0 },
		{ 0.036f, 0.060f, -0.020f, 0.004f, 0.010f, 0.010f, 0, 1.0f, 0.85f, 0.2f, 1, 0 },
	};
	const Part c_mine[] =
	{
		{ 0.0f, -0.040f, -0.010f, 0.080f, 0.080f, 0.022f, 1.5708f, GM, 0, 1 },
		{ 0.0f, -0.034f, -0.010f, 0.050f, 0.050f, 0.022f, 1.5708f, BK, 0, 1 },
		{ 0.0f, -0.022f, -0.010f, 0.014f, 0.006f, 0.014f, 0, 1.0f, 0.15f, 0.1f, 1, 0 },
	};
	const Part c_concussion[] =
	{
		{ 0.0f, 0.030f, -0.090f, 0.040f, 0.050f, 0.240f, 0, OL, 0, 0 },
		{ 0.0f, 0.035f, -0.270f, 0.024f, 0.024f, 0.130f, 0, GM, 0, 1 },
		{ 0.0f, 0.035f, -0.300f, 0.052f, 0.052f, 0.012f, 0, GM, 0, 1 },
		{ 0.0f, 0.035f, -0.330f, 0.052f, 0.052f, 0.012f, 0, GM, 0, 1 },
		{ 0.0f, -0.005f, -0.110f, 0.032f, 0.032f, 0.110f, 0, GM, 0, 1 },
		{ 0.0f, -0.045f, 0.018f, 0.026f, 0.095f, 0.036f, HT, BK, 0, 0 },
		{ 0.0f, 0.025f, 0.070f, 0.030f, 0.040f, 0.100f, 0, OL, 0, 0 },
		{ 0.0f, 0.057f, -0.090f, 0.006f, 0.004f, 0.080f, 0, 0.3f, 0.6f, 1.0f, 1, 0 },
	};
	const Part c_cannon[] =
	{
		{ 0.0f, 0.035f, -0.080f, 0.065f, 0.070f, 0.260f, 0, GM, 0, 0 },
		{ 0.0f, 0.045f, -0.270f, 0.036f, 0.036f, 0.120f, 0, BK, 0, 1 },
		{ 0.0f, 0.045f, -0.230f, 0.052f, 0.052f, 0.040f, 0, GM, 0, 1 },
		{ 0.0f, -0.022f, -0.050f, 0.045f, 0.045f, 0.120f, 0, BK, 0, 1 },
		{ 0.0f, 0.085f, -0.050f, 0.012f, 0.020f, 0.090f, 0, BK, 0, 0 },
		{ 0.0f, -0.045f, 0.018f, 0.028f, 0.095f, 0.038f, HT, BK, 0, 0 },
		{ 0.034f, 0.050f, -0.050f, 0.004f, 0.020f, 0.030f, 0, 1.0f, 0.2f, 0.1f, 1, 0 },
	};
	#define MODEL(p, mx, my, mz, fr, fg, fb, ha, hs) { p, int(sizeof(p) / sizeof(p[0])), { mx, my, mz }, { fr, fg, fb }, ha, hs }
	const Model c_models[10] =
	{
		{ nullptr, 0, { 0.0f, -0.03f, -0.08f }, { 1.0f, 0.8f, 0.5f }, 0.5f, 0.05f },        // fist
		MODEL(c_pistol,     0.0f, 0.030f, -0.180f, 1.0f, 0.35f, 0.20f, 0.55f, 0.06f),
		MODEL(c_rifle,      0.0f, 0.026f, -0.260f, 1.0f, 0.30f, 0.20f, 0.40f, 0.04f),
		MODEL(c_thermal,    0.0f, -0.040f, -0.060f, 1.0f, 0.70f, 0.30f, 0.35f, 0.05f),
		MODEL(c_repeater,   0.0f, 0.036f, -0.275f, 0.5f, 1.00f, 0.40f, 0.45f, 0.04f),
		MODEL(c_fusion,     0.0f, 0.030f, -0.260f, 1.0f, 0.60f, 0.15f, 0.60f, 0.07f),
		MODEL(c_mortar,     0.0f, 0.040f, -0.270f, 1.0f, 0.85f, 0.40f, 0.90f, 0.12f),
		MODEL(c_mine,       0.0f, -0.040f, -0.060f, 1.0f, 0.70f, 0.30f, 0.30f, 0.05f),
		MODEL(c_concussion, 0.0f, 0.035f, -0.340f, 0.5f, 0.70f, 1.00f, 0.85f, 0.10f),
		MODEL(c_cannon,     0.0f, 0.045f, -0.335f, 1.0f, 0.30f, 0.30f, 1.00f, 0.12f),
	};

	// ---- Voxel weapons (extruded side-view sprites) ----
	struct VoxWeapon { bool valid = false; bool detailed = false; std::vector<Vtx> local; V3 muzzle = { 0, 0, 0 }; GLuint vbo = 0; };
	int s_drawWeapon = -1;
	M4 s_drawWeaponW;
	VoxWeapon s_voxW[10];
	// Per weapon: grip pixel (where the fist closes), muzzle pixel, voxel size (m), max half thickness (voxels),
	// mode 0 = side view, 1 = round (thermal detonator), 2 = flat disk held level (mine).
	struct VoxSpec { int gx, gy, mx, my; float vs; int maxT; int mode; };
	const VoxSpec c_voxSpec[10] =
	{
		{ 0, 0, 0, 0, 0.0f, 0, 0 },          // fists
		{ 20, 3, 0, 9, 0.0095f, 3, 0 },      // Bryar pistol (built-in pixel art)
		{ 32, 4, 0, 10, 0.0088f, 3, 0 },     // E-11 rifle (IST-GUNI.FME)
		{ 7, 7, 7, 7, 0.0048f, 8, 1 },       // thermal detonator (IDET.FME)
		{ 29, 6, 0, 13, 0.0088f, 3, 0 },     // Imperial repeater (IAUTOGUN.FME)
		{ 6, 3, 0, 11, 0.0080f, 3, 0 },      // Jeron fusion cutter (IFUSION.FME)
		{ 26, 2, 0, 9, 0.0088f, 4, 0 },      // Packered mortar (IMORTAR.FME)
		{ 9, 10, 9, 10, 0.0050f, 2, 2 },     // I've mines (IMINE.FME)
		{ 25, 3, 0, 12, 0.0088f, 3, 0 },     // Stouker concussion rifle (ICONCUS.FME)
		{ 55, 3, 0, 6, 0.0083f, 4, 0 },      // assault cannon (ICANNON.FME)
	};
	// Where the grip pixel goes in the weapon frame W (inside the closed fist).
	const V3 c_gripInW = { 0.0f, -0.040f, 0.015f };

	void buildVoxWeapon(int id, const unsigned char* rgba, int w, int h)
	{
		if (id <= 0 || id >= 10) { return; }
		if (s_voxW[id].detailed) { return; }   // the detailed model wins over the sprite extrusion
		const VoxSpec& sp = c_voxSpec[id];
		VoxWeapon& vw = s_voxW[id];
		vw.local.clear();
		std::vector<int> dist(w * h, 0);
		auto opaque = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && rgba[(y * w + x) * 4 + 3] > 0; };
		// Distance (in pixels) to the nearest transparent pixel: gives the extrusion a rounded volume.
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
		{
			if (!opaque(x, y)) continue;
			int best = 99;
			for (int r = 1; r <= sp.maxT && best == 99; r++)
			{
				for (int dy = -r; dy <= r && best == 99; dy++) for (int dx = -r; dx <= r; dx++)
				{
					if ((abs(dx) == r || abs(dy) == r) && !opaque(x + dx, y + dy)) { best = r; break; }
				}
			}
			int t = (best == 99) ? sp.maxT : best;
			if (sp.mode == 1)
			{
				// Sphere: thickness from the distance to the sprite center.
				const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f, R = fminf(w, h) * 0.5f;
				const float rr = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) / R;
				t = (int)fmaxf(1.0f, roundf(sqrtf(fmaxf(0.0f, 1.0f - rr * rr)) * sp.maxT));
			}
			dist[y * w + x] = t;
		}
		auto thick = [&](int x, int y) { return (x >= 0 && y >= 0 && x < w && y < h) ? dist[y * w + x] : 0; };
		const float vs = sp.vs;
		const float d[3] = { vs, vs, vs };
		// Voxel (x, y, k): k in [-(t-1), t-1]. Sprite x -> weapon -Z (barrel on the left = forward).
		M4 base = ident();
		if (sp.mode == 2) { base = rotX(-1.5708f); }   // disk lying flat in the palm
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
		{
			const int t = thick(x, y);
			if (!t) continue;
			const unsigned char* c = &rgba[(y * w + x) * 4];
			const V3 col = { c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f };
			for (int k = -(t - 1); k <= t - 1; k++)
			{
				// Local layout: X = depth (k), Y = sprite y, Z = sprite x.
				const float p0[3] = { (k - 0.5f) * vs, (y - sp.gy - 0.5f) * vs, (x - sp.gx - 0.5f) * vs };
				const int ak = abs(k);
				// +x / -x: outside of the extrusion at this pixel.
				if (k == t - 1) voxFace(vw.local, base, p0, d, 0, col);
				if (k == -(t - 1)) voxFace(vw.local, base, p0, d, 1, col);
				// +y / -y / +z / -z: neighbor pixel thinner than this layer.
				if (thick(x, y + 1) <= ak) voxFace(vw.local, base, p0, d, 2, col);
				if (thick(x, y - 1) <= ak) voxFace(vw.local, base, p0, d, 3, col);
				if (thick(x + 1, y) <= ak) voxFace(vw.local, base, p0, d, 4, col);
				if (thick(x - 1, y) <= ak) voxFace(vw.local, base, p0, d, 5, col);
			}
		}
		// Shift so the grip pixel sits in the fist.
		for (Vtx& v : vw.local) { v.x += c_gripInW.x; v.y += c_gripInW.y; v.z += c_gripInW.z; }
		const V3 mz = xform(base, 0.0f, (sp.my - sp.gy) * vs, (sp.mx - sp.gx - 0.5f) * vs);
		vw.muzzle = { mz.x + c_gripInW.x, mz.y + c_gripInW.y, mz.z + c_gripInW.z };
		vw.valid = !vw.local.empty();
	}

	// Bryar pistol: there is no pickup sprite for it, so it is drawn here as pixel art in the
	// same style (barrel on the left, gold coil, black grip).
	void buildPistolArt()
	{
		static const char* art[] =
		{
			"............kkkkkk.........",
			"...........kgllggk.........",
			"kkkkkkkkkkkkdkkkkdkkkkk....",
			"lgggggggggdoooooooddddddk..",
			"kdddddddddoooooooodddddddk.",
			"..kkkkkkkkdddddddddddddddk.",
			"...........kkkkkkkkdbbbbdk.",
			".................kkdbbbbdk.",
			"..................kbbbbbdk.",
			"..................kbbbbdk..",
			".................kbbbbdk...",
			".................kbbbbdk...",
			".................kkkkkk....",
		};
		const int h = sizeof(art) / sizeof(art[0]), w = 27;
		std::vector<unsigned char> rgba(w * h * 4, 0);
		for (int row = 0; row < h; row++)
		{
			const int y = h - 1 - row;
			for (int x = 0; x < w && art[row][x]; x++)
			{
				unsigned char r = 0, g = 0, b = 0, a = 255;
				switch (art[row][x])
				{
					case 'k': r = 20; g = 20; b = 26; break;
					case 'd': r = 56; g = 56; b = 64; break;
					case 'g': r = 106; g = 106; b = 118; break;
					case 'l': r = 166; g = 166; b = 178; break;
					case 'o': r = 190; g = 140; b = 40; break;
					case 'b': r = 42; g = 36; b = 34; break;
					default: a = 0; break;
				}
				unsigned char* o = &rgba[(y * w + x) * 4];
				o[0] = r; o[1] = g; o[2] = b; o[3] = a;
			}
		}
		buildVoxWeapon(1, rgba.data(), w, h);
	}

	void addPart(const M4& W, const Part& p)
	{
		M4 m = mul(W, translate(p.cx, p.cy, p.cz));
		if (p.tilt != 0.0f) { m = mul(m, rotX(p.tilt)); }
		m = mul(m, scale(p.sx, p.sy, p.sz));
		m = mul(m, translate(0.0f, 0.0f, 0.5f));
		if (p.shape == 1) { addCylRaw(s_vSolid, m, p.r, p.g, p.b, p.emis); }
		else { addBoxRaw(s_vSolid, m, p.r, p.g, p.b, p.emis, false); }
	}

	// Additive quad (4 corners) with alpha.
	void addQuadAdd(const V3& a, const V3& b, const V3& c, const V3& d, float r, float g, float bl, float alphaAB, float alphaCD)
	{
		const V3* q[4] = { &a, &b, &c, &d };
		const float al[4] = { alphaAB, alphaAB, alphaCD, alphaCD };
		const int idx[6] = { 0, 1, 2, 0, 2, 3 };
		for (int t = 0; t < 6; t++)
		{
			const V3& p = *q[idx[t]];
			Vtx v = { p.x, p.y, p.z, 0.5f, 0.5f, r, g, bl, al[idx[t]], 1.0f };
			s_vAdd.push_back(v);
		}
	}

	// Frames for a hand. A = aim frame, g = handle center (grip origin) in A.
	// Depth compression of the right hand and its weapon toward the head (see hands_setCompression).
	float s_cScale = 1.0f;
	V3 s_cCenter = { 0, 0, 0 };
	bool s_cApply = false;

	bool handFrames(const HandInput& in, float side, M4* A, M4* H, M4* W)
	{
		V3 g = { 0.0f, -0.025f, 0.055f };
		if (in.aimValid)
		{
			*A = fromPose(in.aimPose);
			if (in.valid)
			{
				const M4 inv = inverseRigid(*A);
				g = xform(inv, in.pose.position.x, in.pose.position.y, in.pose.position.z);
				// Guard against bogus offsets.
				if (fabsf(g.x) > 0.1f || fabsf(g.y) > 0.1f || fabsf(g.z) > 0.15f) { g = { 0.0f, -0.025f, 0.055f }; }
			}
		}
		else if (in.valid) { *A = fromPose(in.pose); g = { 0.0f, 0.0f, 0.0f }; }
		else { return false; }
		const M4 G = mul(*A, translate(g.x, g.y, g.z));
		const M4 tilt = rotX(HT);
		*H = mul(mul(G, tilt), translate(side * 0.020f, 0.0f, 0.060f));
		const V3 top = xform(tilt, 0.0f, 0.055f, 0.0f);
		*W = mul(*A, translate(g.x + top.x, g.y + top.y, g.z + top.z));
		if (s_cApply && side > 0.0f && s_cScale < 0.999f)
		{
			// Scale about the head: same image from the eyes, but closer, so it stays in front of walls.
			const M4 C = mul(translate(s_cCenter.x, s_cCenter.y, s_cCenter.z), mul(scale(s_cScale, s_cScale, s_cScale), translate(-s_cCenter.x, -s_cCenter.y, -s_cCenter.z)));
			*A = mul(C, *A); *H = mul(C, *H); *W = mul(C, *W);
		}
		return true;
	}

	GLuint s_prog = 0, s_vbo = 0, s_vao = 0, s_tex = 0, s_white = 0, s_flat = 0;
	GLint s_locMvp = -1, s_locTex = -1, s_locLight = -1;
	float s_curl[2][3] = {};

	GLuint compileShader(GLenum type, const char* src)
	{
		GLuint s = glCreateShader(type);
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); LOGE("hand shader: %s", log); }
		return s;
	}

	// The meshes are built in app space once per frame and shared by both eyes.
	bool s_built = false;
	GLsizei s_nHand = 0, s_nSolid = 0, s_nAdd = 0;
	void drawRange(GLint first, GLsizei count, GLuint tex)
	{
		if (count <= 0) { return; }
		glBindTexture(GL_TEXTURE_2D, tex);
		glDrawArrays(GL_TRIANGLES, first, count);
	}
}


// ---------------- Smooth meshes (hands and high resolution weapons) ----------------
#include "tfe_xr_mesh.h"
#include "tfe_xr_handskin.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
namespace
{
	GLuint s_progS = 0;
	GLint s_locSMvp = -1, s_locSModel = -1, s_locSEye = -1, s_locSLight = -1, s_locSSkin = -1, s_locSBones = -1;
	float s_bones[2][32][16];
	GLuint s_skVao = 0, s_skVbo = 0, s_skIbo = 0; GLsizei s_skCount = 0;
	struct SMesh { std::vector<SVtx> v; std::vector<uint32_t> i; GLuint vbo = 0, ibo = 0, vao = 0; GLsizei count = 0; };

	void uploadSMesh(SMesh& m)
	{
		if (m.vao || m.i.empty()) { return; }
		glGenVertexArrays(1, &m.vao);
		glBindVertexArray(m.vao);
		glGenBuffers(1, &m.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
		glBufferData(GL_ARRAY_BUFFER, m.v.size() * sizeof(SVtx), m.v.data(), GL_STATIC_DRAW);
		glGenBuffers(1, &m.ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.i.size() * sizeof(uint32_t), m.i.data(), GL_STATIC_DRAW);
		glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SVtx), (void*)0);
		glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SVtx), (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(SVtx), (void*)(6 * sizeof(float)));
		glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(SVtx), (void*)(9 * sizeof(float)));
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		m.count = (GLsizei)m.i.size();
		std::vector<SVtx>().swap(m.v);
		std::vector<uint32_t>().swap(m.i);
	}

	void uploadSkinMesh()
	{
		const std::vector<HandSkinVtx>& v = handskin_vertices();
		const std::vector<unsigned short>& ix = handskin_indices();
		glGenVertexArrays(1, &s_skVao);
		glBindVertexArray(s_skVao);
		glGenBuffers(1, &s_skVbo);
		glBindBuffer(GL_ARRAY_BUFFER, s_skVbo);
		glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(HandSkinVtx), v.data(), GL_STATIC_DRAW);
		glGenBuffers(1, &s_skIbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_skIbo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ix.size() * sizeof(unsigned short), ix.data(), GL_STATIC_DRAW);
		const GLsizei st = sizeof(HandSkinVtx);
		glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
		glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st, (void*)(6 * sizeof(float)));
		glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*)(9 * sizeof(float)));
		glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, st, (void*)(10 * sizeof(float)));
		glEnableVertexAttribArray(5); glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, st, (void*)(14 * sizeof(float)));
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		s_skCount = (GLsizei)ix.size();
	}

	// ---- Hand pieces: signed distance functions meshed once in their local frame ----
	enum HandPiece { HP_FINGER = 0, HP_THUMB, HP_PALM, HP_CUFF, HP_SLEEVE, HP_STRAP, HP_WATCH };
	struct HandKey { int piece; float a, b, c; int side; int tip; };
	struct HandMesh { HandKey key; SMesh mesh; };
	std::vector<HandMesh> s_handMeshes;

	inline float sdRoundBox(float x, float y, float z, float bx, float by, float bz, float r)
	{
		const float qx = fabsf(x) - bx + r, qy = fabsf(y) - by + r, qz = fabsf(z) - bz + r;
		const float ox = fmaxf(qx, 0.0f), oy = fmaxf(qy, 0.0f), oz = fmaxf(qz, 0.0f);
		return sqrtf(ox * ox + oy * oy + oz * oz) + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f) - r;
	}
	// Tapered capsule along -Z from z = z0 (radius r0) to z = z1 (radius r1), x squashed by sx.
	inline float sdTaper(float x, float y, float z, float z0, float z1, float r0, float r1, float sx)
	{
		const float t = fminf(1.0f, fmaxf(0.0f, (z0 - z) / (z0 - z1)));
		const float cz = z0 + (z1 - z0) * t;
		const float r = r0 + (r1 - r0) * t;
		const float dx = x / sx, dz = z - cz;
		return sqrtf(dx * dx + y * y + dz * dz) - r;
	}
	inline float smin(float a, float b, float k) { const float h = fmaxf(k - fabsf(a - b), 0.0f) / k; return fminf(a, b) - h * h * k * 0.25f; }

	float handSdf(const HandKey& k, float x, float y, float z)
	{
		switch (k.piece)
		{
		case HP_FINGER: case HP_THUMB:
		{
			// a = length, b = radius at the joint, c = radius at the far end. Starts a bit behind the
			// joint so bent segments overlap like real knuckles.
			const float r0 = k.b, r1 = k.c;
			float d = sdTaper(x, y, z, r0 * 0.55f, -k.a + (k.tip ? r1 : 0.0f), r0, r1, 0.84f);
			if (k.tip)
			{
				// Fingertip slightly flattened on the pad side.
				d = fmaxf(d, -(x * float(k.side)) - r1 * 0.95f);
			}
			return d;
		}
		case HP_PALM:
		{
			// a = thickness (x), b = width (y), c = length (z, toward -Z). Back of the hand slightly domed,
			// knuckle ridge and a padded knuckle guard on top (tactical glove).
			const float cz = -k.c * 0.5f;
			float d = sdRoundBox(x, y, z - cz, k.a * 0.5f, k.b * 0.5f, k.c * 0.5f, 0.0145f);
			const float sx = x * float(k.side);
			const float guard = sdRoundBox(sx - k.a * 0.5f, y, z - (-k.c * 0.80f), 0.004f, k.b * 0.44f, k.c * 0.11f, 0.0035f);
			d = smin(d, guard, 0.004f);
			return d;
		}
		case HP_CUFF:
		{
			// Elliptical glove cuff (a = x size, b = y size, c = length) with a rounded lip.
			const float cz = -k.c * 0.5f;
			const float ex = x / (k.a * 0.5f), ey = y / (k.b * 0.5f);
			const float rr = sqrtf(ex * ex + ey * ey);
			const float radial = (rr - 1.0f) * fminf(k.a, k.b) * 0.5f;
			const float axial = fabsf(z - cz) - k.c * 0.5f;
			// Well rounded ends: the hand ends at the wrist, the cuff edge is what shows.
			const float rr2 = 0.007f;
			const float ra = radial + rr2, ax = axial + rr2;
			return sqrtf(fmaxf(ra, 0.0f) * fmaxf(ra, 0.0f) + fmaxf(ax, 0.0f) * fmaxf(ax, 0.0f)) + fminf(fmaxf(ra, ax), 0.0f) - rr2;
		}
		case HP_SLEEVE:
		{
			// Rolled jacket sleeve: tapered toward the wrist with soft folds.
			const float t = fminf(1.0f, fmaxf(0.0f, -z / k.c));
			const float fold = 1.0f + 0.035f * sinf(t * 18.0f + atan2f(y, x) * 2.0f);
			const float ax = k.a * 0.5f * (1.0f - 0.18f * t) * fold, ay = k.b * 0.5f * (1.0f - 0.15f * t) * fold;
			const float ex = x / ax, ey = y / ay;
			const float radial = (sqrtf(ex * ex + ey * ey) - 1.0f) * fminf(ax, ay);
			const float axial = fabsf(z + k.c * 0.5f) - k.c * 0.5f;
			return sqrtf(fmaxf(radial, 0.0f) * fmaxf(radial, 0.0f) + fmaxf(axial, 0.0f) * fmaxf(axial, 0.0f)) + fminf(fmaxf(radial, axial), 0.0f) - 0.003f;
		}
		case HP_STRAP:
		{
			const float ex = x / (k.a * 0.5f), ey = y / (k.b * 0.5f);
			const float radial = (sqrtf(ex * ex + ey * ey) - 1.0f) * fminf(k.a, k.b) * 0.5f;
			const float axial = fabsf(z + k.c * 0.5f) - k.c * 0.5f;
			return fmaxf(fmaxf(radial, -radial - 0.003f), axial) - 0.0008f;
		}
		case HP_WATCH:
		{
			// Case (a x b x c), a recessed screen on +Y.
			float d = sdRoundBox(x, y, z + k.c * 0.5f, k.a * 0.5f, k.b * 0.5f, k.c * 0.5f, 0.003f);
			const float screen = sdRoundBox(x, y - k.b * 0.5f, z + k.c * 0.5f, k.a * 0.5f - 0.003f, 0.0012f, k.c * 0.5f - 0.004f, 0.001f);
			return fmaxf(d, -screen);
		}
		}
		return 1.0f;
	}

	struct HandMat { HandKey key; };
	void handMaterial(void* user, const float p[3], float rgb[3], float* spec, float* emis)
	{
		const HandKey& k = ((HandMat*)user)->key;
		*emis = 0.0f;
		// Black tactical glove leather with a grey knuckle guard; tan jacket sleeve.
		float c[3] = { 0.075f, 0.072f, 0.070f }; float sp = 0.28f;
		if (k.piece == HP_PALM)
		{
			const float sx = p[0] * float(k.side);
			if (sx > k.a * 0.5f - 0.0005f && p[2] < -k.c * 0.66f) { c[0] = 0.20f; c[1] = 0.20f; c[2] = 0.21f; sp = 0.45f; }
			else if (sx < -k.a * 0.25f) { c[0] = 0.11f; c[1] = 0.105f; c[2] = 0.10f; sp = 0.15f; }   // suede palm
		}
		else if (k.piece == HP_FINGER || k.piece == HP_THUMB)
		{
			if (p[0] * float(k.side) < -k.b * 0.35f) { c[0] = 0.11f; c[1] = 0.105f; c[2] = 0.10f; sp = 0.15f; }
		}
		else if (k.piece == HP_CUFF)
		{
			const float band = fabsf(p[2] + k.c * 0.5f);
			if (band < k.c * 0.22f) { c[0] = 0.15f; c[1] = 0.15f; c[2] = 0.15f; sp = 0.1f; }        // velcro strap
		}
		else if (k.piece == HP_SLEEVE) { c[0] = 0.43f; c[1] = 0.34f; c[2] = 0.23f; sp = 0.05f; }
		else if (k.piece == HP_STRAP) { c[0] = 0.10f; c[1] = 0.09f; c[2] = 0.08f; sp = 0.2f; }
		else if (k.piece == HP_WATCH)
		{
			c[0] = 0.30f; c[1] = 0.31f; c[2] = 0.34f; sp = 0.7f;
			if (p[1] > k.b * 0.5f - 0.0025f && fabsf(p[0]) < k.a * 0.5f - 0.003f) { c[0] = c[1] = c[2] = 0.02f; sp = 0.9f; }
			if (p[0] > k.a * 0.5f - 0.004f && p[2] < -k.c + 0.012f && p[1] > 0.0f) { c[0] = 1.0f; c[1] = 0.25f; c[2] = 0.1f; *emis = 1.0f; }
		}
		rgb[0] = c[0]; rgb[1] = c[1]; rgb[2] = c[2]; *spec = sp;
	}

	void buildHandMesh(HandMesh& hm)
	{
		const int piece = hm.key.piece; const float a = hm.key.a, b = hm.key.b, c = hm.key.c;
		// Bounds of the piece in its frame.
		float mn[3], mx[3];
		const float pad = 0.006f;
		switch (piece)
		{
		case HP_FINGER: case HP_THUMB: mn[0] = -b - pad; mx[0] = b + pad; mn[1] = -b - pad; mx[1] = b + pad; mn[2] = -a - pad; mx[2] = b + pad; break;
		case HP_PALM: mn[0] = -a * 0.5f - pad; mx[0] = a * 0.5f + pad; mn[1] = -b * 0.5f - pad; mx[1] = b * 0.5f + pad; mn[2] = -c - pad; mx[2] = pad; break;
		case HP_WATCH: mn[0] = -a * 0.5f - pad; mx[0] = a * 0.5f + pad; mn[1] = -b * 0.5f - pad; mx[1] = b * 0.5f + pad; mn[2] = -c - pad; mx[2] = pad; break;
		default: mn[0] = -a * 0.6f - pad; mx[0] = a * 0.6f + pad; mn[1] = -b * 0.6f - pad; mx[1] = b * 0.6f + pad; mn[2] = -c - pad; mx[2] = pad; break;
		}
		// Cell size: fine enough for smooth knuckles, coarse enough for the Quest (about 70k triangles a hand).
		const float cell = (piece == HP_WATCH) ? 0.0016f : (piece == HP_FINGER || piece == HP_THUMB) ? 0.0016f : (piece == HP_SLEEVE ? 0.0030f : (piece == HP_PALM ? 0.0020f : 0.0022f));
		MeshField f;
		f.cell = cell;
		for (int i = 0; i < 3; i++) { f.origin[i] = mn[i]; }
		f.nx = int((mx[0] - mn[0]) / cell) + 2; f.ny = int((mx[1] - mn[1]) / cell) + 2; f.nz = int((mx[2] - mn[2]) / cell) + 2;
		f.v.resize(size_t(f.nx) * f.ny * f.nz);
		for (int z = 0; z < f.nz; z++) for (int y = 0; y < f.ny; y++) for (int x = 0; x < f.nx; x++)
		{
			f.v[(size_t(z) * f.ny + y) * f.nx + x] = -handSdf(hm.key, mn[0] + x * cell, mn[1] + y * cell, mn[2] + z * cell);
		}
		HandMat mat = { hm.key };
		mesh_surfaceNets(f, 0.0f, handMaterial, &mat, hm.mesh.v, hm.mesh.i);
	}

	// Pieces are meshed on a worker thread; until all of a hand's pieces are ready the voxel hand is shown.
	std::mutex s_hmMutex;
	std::vector<int> s_hmQueue;
	std::atomic<int> s_hmState[96];
	bool s_hmThread = false;
	void handWorker()
	{
		for (;;)
		{
			int job = -1;
			{
				std::lock_guard<std::mutex> lk(s_hmMutex);
				if (!s_hmQueue.empty()) { job = s_hmQueue.front(); s_hmQueue.erase(s_hmQueue.begin()); }
			}
			if (job < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(15)); continue; }
			buildHandMesh(s_handMeshes[job]);
			s_hmState[job] = 2;
		}
	}
	// Returns the piece index, or -1 while it is still being built.
	int handMesh(int piece, float a, float b, float c, float side, int tip = 0)
	{
		const int sd = side > 0.0f ? 1 : -1;
		if (s_handMeshes.capacity() < 96) { s_handMeshes.reserve(96); }
		for (size_t i = 0; i < s_handMeshes.size(); i++)
		{
			const HandKey& k = s_handMeshes[i].key;
			if (k.piece == piece && k.side == sd && k.tip == tip && fabsf(k.a - a) < 1e-5f && fabsf(k.b - b) < 1e-5f && fabsf(k.c - c) < 1e-5f)
			{
				return s_hmState[i] == 2 ? (int)i : -1;
			}
		}
		if (s_handMeshes.size() >= 96) { return -1; }
		HandMesh hm;
		hm.key = { piece, a, b, c, sd, tip };
		const int idx = (int)s_handMeshes.size();
#ifdef HANDS_HOST_TEST
		buildHandMesh(hm);
		s_handMeshes.push_back(std::move(hm));
		s_hmState[idx] = 2;
		return idx;
#else
		s_hmState[idx] = 1;
		s_handMeshes.push_back(std::move(hm));
		std::lock_guard<std::mutex> lk(s_hmMutex);
		s_hmQueue.push_back(idx);
		if (!s_hmThread) { s_hmThread = true; std::thread(handWorker).detach(); }
		return -1;
#endif
	}

	struct SDraw { SMesh* mesh; M4 m; int skin = -1; };

	std::vector<SDraw> s_sDraws;
	bool s_pieceMissing = false;
	void addPiece(const M4& m, int piece, float a, float b, float c, float side, int tip = 0)
	{
		const int id = handMesh(piece, a, b, c, side, tip);
		if (id < 0) { s_pieceMissing = true; return; }
#ifdef HANDS_HOST_TEST
		const SMesh& sm = s_handMeshes[id].mesh;
		for (uint32_t idx : sm.i)
		{
			const SVtx& v = sm.v[idx];
			const V3 q = xform(m, v.x, v.y, v.z);
			const V3 n = { m.m[0] * v.nx + m.m[4] * v.ny + m.m[8] * v.nz, m.m[1] * v.nx + m.m[5] * v.ny + m.m[9] * v.nz, m.m[2] * v.nx + m.m[6] * v.ny + m.m[10] * v.nz };
			const float L[3] = { 0.35f, 0.85f, 0.4f };
			float d = (n.x * L[0] + n.y * L[1] + n.z * L[2]) / 1.0f; if (d < 0) d = 0;
			const float k = 0.35f + 0.65f * d, sp = powf(d, 24.0f) * v.spec;
			Vtx o = { q.x, q.y, q.z, 0.5f, 0.5f, fminf(1.0f, v.r * k + sp), fminf(1.0f, v.g * k + sp), fminf(1.0f, v.b * k + sp), 1.0f, 0.0f };
			s_vHand.push_back(o);
		}
#else
		s_sDraws.push_back({ &s_handMeshes[id].mesh, m });
#endif
	}

	// Gloved hand: skinned WebXR generic hand mesh (posed by the curls) + cuff, strap and watch.
	bool buildHandSkinned(const M4& H, float side, float curlIndex, float curlOthers, float curlThumb)
	{
		const size_t drawsBefore = s_sDraws.size();
		s_pieceMissing = false;
		const float c = -side;
		const M4 palmBase = mul(H, translate(0.0f, 0.0f, 0.045f));
		if (side < 0.0f)
		{
			// Left only: one short glove stub, starting inside the heel of the hand (no gap) and ending
			// just past the watch (the HUD). The right hand ends at the palm.
			addPiece(mul(palmBase, translate(c * 0.001f, -0.002f, 0.050f)), HP_CUFF, 0.044f, 0.060f, 0.076f, side);
			const M4 wf = watchFrame(H, side);
			addPiece(mul(wf, translate(0.0f, 0.0f, 0.031f)), HP_WATCH, 0.042f, 0.012f, 0.062f, side);
		}
		if (s_pieceMissing) { s_sDraws.resize(drawsBefore); return false; }
		const int h = side > 0.0f ? 0 : 1;
		handskin_pose(curlIndex, curlOthers, curlThumb, s_bones[h]);
		M4 A; handskin_align(side, A.m);
#ifdef HANDS_HOST_TEST
		std::vector<SVtx> v; std::vector<uint32_t> ix;
		handskin_cpu(H.m, side, curlIndex, curlOthers, curlThumb, v, ix);
		for (uint32_t i : ix)
		{
			const SVtx& q = v[i];
			const float L[3] = { 0.35f, 0.85f, 0.4f };
			float d = q.nx * L[0] + q.ny * L[1] + q.nz * L[2]; if (d < 0) d = 0;
			const float k = 0.35f + 0.65f * d, sp = powf(d, 24.0f) * q.spec;
			Vtx o = { q.x, q.y, q.z, 0.5f, 0.5f, fminf(1.0f, q.r * k + sp), fminf(1.0f, q.g * k + sp), fminf(1.0f, q.b * k + sp), 1.0f, 0.0f };
			s_vHand.push_back(o);
		}
#else
		SDraw d; d.mesh = nullptr; d.m = mul(H, A); d.skin = h;
		s_sDraws.push_back(d);
#endif
		return true;
	}

	// Counter-Strike style gloved hand: same joints as the voxel hand, smooth pieces.
	// Returns false when some piece is not built yet (the caller then shows the voxel hand).
	bool buildHandSmooth(const M4& H, float side, float curlIndex, float curlOthers, float curlThumb)
	{
		const size_t drawsBefore = s_sDraws.size();
		s_pieceMissing = false;
		const float c = -side;
		const M4 palmBase = mul(H, translate(0.0f, 0.0f, 0.045f));
		addPiece(palmBase, HP_PALM, 0.030f, 0.086f, 0.094f, side);
		addPiece(mul(palmBase, translate(c * 0.001f, -0.002f, 0.030f)), HP_CUFF, 0.044f, 0.068f, 0.032f, side);
		if (side < 0.0f)
		{
			// Left: glove wrist up to the end of the watch (no forearm).
			addPiece(mul(palmBase, translate(c * 0.002f, -0.003f, 0.058f)), HP_CUFF, 0.042f, 0.062f, 0.030f, side);
			addPiece(mul(palmBase, translate(c * 0.002f, -0.003f, 0.034f)), HP_STRAP, 0.050f, 0.068f, 0.014f, side);
			const M4 wf = watchFrame(H, side);
			addPiece(mul(wf, translate(0.0f, 0.0f, 0.031f)), HP_WATCH, 0.042f, 0.012f, 0.062f, side);
		}
		const float fingerY[4] = { 0.030f, 0.0100f, -0.0100f, -0.029f };
		const float lenScale[4] = { 1.0f, 1.07f, 1.0f, 0.82f };
		const float radius[4] = { 0.0108f, 0.0112f, 0.0107f, 0.0097f };
		const float segLen[3] = { 0.040f, 0.026f, 0.020f };
		const float jointMax[3] = { 1.35f, 1.6f, 1.1f };
		for (int fi = 0; fi < 4; fi++)
		{
			const float curl = (fi == 0) ? curlIndex : curlOthers;
			M4 m = mul(palmBase, translate(0.0f, fingerY[fi], -0.090f));
			float r = radius[fi];
			for (int s = 0; s < 3; s++)
			{
				m = mul(m, rotY(side * curl * jointMax[s]));
				const float len = segLen[s] * lenScale[fi];
				const float r1 = r * 0.92f;
				addPiece(m, HP_FINGER, len, r, r1, side, s == 2 ? 1 : 0);
				m = mul(m, translate(0.0f, 0.0f, -len));
				r = r1;
			}
		}
		const float fist = fminf(curlThumb, fminf(curlIndex, curlOthers));
		M4 t = mul(palmBase, translate(c * 0.010f, 0.040f, -0.020f));
		t = mul(t, rotX(0.6f - FIST_X * fist));
		t = mul(t, rotY(side * (0.35f + 0.9f * curlThumb + FIST_Y * fist)));
		const float thumbLen[3] = { 0.034f, 0.026f, 0.021f };
		float tr = 0.0125f;
		for (int s = 0; s < 3; s++)
		{
			const float r1 = tr * 0.88f;
			addPiece(t, HP_THUMB, thumbLen[s], tr, r1, side, s == 2 ? 1 : 0);
			t = mul(t, translate(0.0f, 0.0f, -thumbLen[s]));
			t = mul(t, rotY(side * curlThumb * (0.5f + FIST_S * fist)));
			tr = r1;
		}
		if (s_pieceMissing) { s_sDraws.resize(drawsBefore); return false; }
		return true;
	}

	// ---- Weapons: smooth and fine voxel versions, built on a worker thread ----
	struct WeaponSlot
	{
		std::atomic<int> state{ 0 };   // 0 none, 1 queued/building, 2 ready on the CPU, 3 uploaded
		SMesh smooth;                  // style 0
		std::vector<Vtx> fine;         // style 2
		GLuint fineVbo = 0;
		V3 muzzle = { 0, 0, 0 };
	};
	WeaponSlot s_wSlot[2][10];         // [0] smooth, [1] fine voxel
	std::mutex s_wMutex;
	std::vector<int> s_wQueue;
	std::thread s_wThread;
	bool s_wThreadStarted = false;
	int s_weaponStyle = 0;
	bool s_rightHand = true;
	float s_trigPull = 0.0f;   // 0..1, the trigger blade follows the index finger
	// Right hand on the Blender weapons (style 3), fitted offline by hosttest/gripfit.cpp on each model:
	// hand offset in the hand frame (m), yaw/roll, index joints at rest (flexions + abduction), middle/ring/
	// little curls, thumb (base Z, Y, X, two flexions). The trigger pull adds c_holdPull to the index.
	// rx and fj (per-joint fingers: knuckles x4, middle, end, spread) are used by the palm holds
	// (hosttest/palmfit.cpp); fj[0] == 0 = the curl-based fingers of the pistol grips.
	struct HoldPose { float d[3], ry, rz, idx[4], fing[3], thumb[5]; bool hasTrigger; float rx; float fj[7]; };
	const float c_holdPull[3] = { 0.06f, 0.22f, 0.15f };   // same as hosttest/gripfit.cpp PULL
	// HOLD_TABLE_BEGIN
	// 0.28: standard grip refitted (hosttest/gripfit3.cpp) after shooting references: web high on the
	// backstrap, middle, ring and little fingers wrapped evenly down the front strap (the little finger
	// on the grip, no longer curled into the palm), the hand turned (rx) to follow the grip rake.
	const HoldPose c_hold[10] =
	{
		{},   // fists (no weapon)
		{ { 0.0147f, -0.0295f, -0.0111f }, -0.2340f, -0.0385f, { 0.3610f, 0.7484f, 0.6190f, -0.3000f }, { 0.5f, 0.5f, 0.5f }, { 0.1186f, 0.9250f, -0.5218f, 0.0000f, 1.6000f }, true, 0.3439f, { 0.3610f, 1.0831f, 0.9827f, 0.7966f, 1.0302f, 0.7862f, -0.0105f } },   // bryar (standard grip)
		{ { 0.0147f, -0.0295f, -0.0111f }, -0.2340f, -0.0385f, { 0.3610f, 0.7484f, 0.6190f, -0.3000f }, { 0.5f, 0.5f, 0.5f }, { 0.1186f, 0.9250f, -0.5218f, 0.0000f, 1.6000f }, true, 0.3439f, { 0.3610f, 1.0831f, 0.9827f, 0.7966f, 1.0302f, 0.7862f, -0.0105f } },   // e11 (standard grip)
		{ { 0.0179f, 0.0202f, -0.0307f }, -0.2746f, -0.0360f, { 0, 0, 0, 0 }, { 0.5f, 0.5f, 0.5f }, { -0.5385f, 0.2662f, 0.4406f, 1.6000f, 1.6000f }, false, -0.1365f, { 0.8724f, 0.8234f, 0.7460f, 0.6446f, 0.7808f, 0.5398f, -0.0500f } },   // thermal: power grip, ball in the palm, thumb on the arming plate
		{ { 0.0147f, -0.0295f, -0.0111f }, -0.2340f, -0.0385f, { 0.3610f, 0.7484f, 0.6190f, -0.3000f }, { 0.5f, 0.5f, 0.5f }, { 0.1186f, 0.9250f, -0.5218f, 0.0000f, 1.6000f }, true, 0.3439f, { 0.3610f, 1.0831f, 0.9827f, 0.7966f, 1.0302f, 0.7862f, -0.0105f } },   // repeater (standard grip)
		{ { 0.0147f, -0.0295f, -0.0111f }, -0.2340f, -0.0385f, { 0.3610f, 0.7484f, 0.6190f, -0.3000f }, { 0.5f, 0.5f, 0.5f }, { 0.1186f, 0.9250f, -0.5218f, 0.0000f, 1.6000f }, true, 0.3439f, { 0.3610f, 1.0831f, 0.9827f, 0.7966f, 1.0302f, 0.7862f, -0.0105f } },   // fusion (standard grip)
		{ { 0.0147f, -0.0295f, -0.0111f }, -0.2340f, -0.0385f, { 0.3610f, 0.7484f, 0.6190f, -0.3000f }, { 0.5f, 0.5f, 0.5f }, { 0.1186f, 0.9250f, -0.5218f, 0.0000f, 1.6000f }, true, 0.3439f, { 0.3610f, 1.0831f, 0.9827f, 0.7966f, 1.0302f, 0.7862f, -0.0105f } },   // mortar (standard grip)
		{ { 0.0231f, -0.0040f, -0.0028f }, -0.4177f, -0.2868f, { 0, 0, 0, 0 }, { 0.5f, 0.5f, 0.5f }, { -0.2651f, 0.6587f, -0.1895f, 0.8476f, 0.4816f }, false, -0.0442f, { 0.8345f, 0.9774f, 1.1079f, 1.1896f, 0.7189f, 0.3043f, 0.2000f } },   // mine: frisbee grip, thumb on top, fingers under the rim
		{ { 0.0147f, -0.0295f, -0.0111f }, -0.2340f, -0.0385f, { 0.3610f, 0.7484f, 0.6190f, -0.3000f }, { 0.5f, 0.5f, 0.5f }, { 0.1186f, 0.9250f, -0.5218f, 0.0000f, 1.6000f }, true, 0.3439f, { 0.3610f, 1.0831f, 0.9827f, 0.7966f, 1.0302f, 0.7862f, -0.0105f } },   // concussion (standard grip)
		{ { 0.0147f, -0.0295f, -0.0111f }, -0.2340f, -0.0385f, { 0.3610f, 0.7484f, 0.6190f, -0.3000f }, { 0.5f, 0.5f, 0.5f }, { 0.1186f, 0.9250f, -0.5218f, 0.0000f, 1.6000f }, true, 0.3439f, { 0.3610f, 1.0831f, 0.9827f, 0.7966f, 1.0302f, 0.7862f, -0.0105f } },   // cannon (standard grip)
	};
	// HOLD_TABLE_END
	const HoldPose* holdPose(int id);

	void weaponWorker()
	{
		for (;;)
		{
			int job = -1;
			{
				std::lock_guard<std::mutex> lk(s_wMutex);
				if (!s_wQueue.empty()) { job = s_wQueue.front(); s_wQueue.erase(s_wQueue.begin()); }
			}
			if (job < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
			const int kind = job / 10, id = job % 10;
			WeaponSlot& ws = s_wSlot[kind][id];
			const float grip[3] = { c_gripInW.x, c_gripInW.y, c_gripInW.z };
			float mz[3] = { 0, 0, 0 };
			if (kind == 0) { weapons_buildHiRes(id, ws.smooth.v, ws.smooth.i, mz, grip); }
			else
			{
				std::vector<float> f;
				weapons_buildModel(id, f, mz, grip, nullptr, 3.0f);
				ws.fine.resize(f.size() / 10);
				memcpy(ws.fine.data(), f.data(), f.size() * sizeof(float));
			}
			ws.muzzle = { mz[0], mz[1], mz[2] };
			ws.state = 2;
		}
	}
	// Returns the slot when it is ready to draw (uploaded), queuing the build otherwise.
	WeaponSlot* weaponSlot(int kind, int id)
	{
		if (id <= 0 || id >= 10) { return nullptr; }
		WeaponSlot& ws = s_wSlot[kind][id];
		const int st = ws.state;
		if (st == 0)
		{
			ws.state = 1;
			std::lock_guard<std::mutex> lk(s_wMutex);
			s_wQueue.push_back(kind * 10 + id);
			// Then the rest of the arsenal in the background, so switching weapons is instant.
			for (int o = 1; o < 10; o++)
			{
				if (o == id || s_wSlot[kind][o].state != 0) { continue; }
				s_wSlot[kind][o].state = 1;
				s_wQueue.push_back(kind * 10 + o);
			}
			if (!s_wThreadStarted) { s_wThreadStarted = true; s_wThread = std::thread(weaponWorker); s_wThread.detach(); }
			return nullptr;
		}
		if (st == 1) { return nullptr; }
#ifndef HANDS_HOST_TEST
		if (st == 2)
		{
			if (kind == 0) { uploadSMesh(ws.smooth); }
			else
			{
				glGenBuffers(1, &ws.fineVbo);
				glBindBuffer(GL_ARRAY_BUFFER, ws.fineVbo);
				glBufferData(GL_ARRAY_BUFFER, ws.fine.size() * sizeof(Vtx), ws.fine.data(), GL_STATIC_DRAW);
				glBindBuffer(GL_ARRAY_BUFFER, 0);
			}
			ws.state = 3;
		}
#endif
		return &ws;
	}

	// ---- DFmod weapons (Dark Forces Mod Remastered for Jedi Academy): first-person models converted by
	// tools/dfmod/convert.py from the player's copy of the mod into files/dfmod_weapons.bin. Textured,
	// lit, drawn double sided. The one-sided first-person models are completed offline: mirrored missing side
	// for symmetric weapons and a closed inset hull colored from the texture (submesh tex -2, vertex colors).
	// Style 3; thermal and mine stay smooth.
	struct DfmSub { int tex = -1; std::vector<float> v; std::vector<uint32_t> i; GLuint vao = 0, vbo = 0, ibo = 0; };
	struct DfmWeapon { bool valid = false; V3 muzzle = { 0, 0, 0 }; std::vector<DfmSub> subs; };
	// Two sets: [0] Blender weapons (style 3, files/dfmod_weapons.bin), [1] DFmod first-person models
	// completed by the mod's third-person models (style 4, files/dfmod_combined.bin, tools/dfmod/combine.py).
	DfmWeapon s_dfmSet[2][10];
	inline int dfmSetIdx() { return s_weaponStyle == 4 ? 1 : 0; }
#define s_dfm (s_dfmSet[dfmSetIdx()])
	struct DfmTex { int w = 0, h = 0; std::vector<unsigned char> rgba; GLuint id = 0; bool nearest = false; };
	// PDA bezel: one textured box (front = configuration frame art, sides/back = its metal).
	std::vector<DfmTex> s_bezelTex[2] = { std::vector<DfmTex>(1), std::vector<DfmTex>(1) };
	std::vector<DfmTex> s_liveTex(1);
	std::vector<DfmSub> s_bezelSub;
	float s_bezelBuiltScale = -1.0f;
	bool s_bezelDraw = false;
	M4 s_bezelM;
	// Case mesh: a slab with rounded corners (like the watch) and small chamfers on both faces. The front
	// (and its chamfer) maps the 320x200 art by position; the sides and back use texel (0, 0), which the
	// art loader fills with the frame metal (that corner is rounded away on the face).
	void buildBezel()
	{
		const PdaCase c = pdaCase(s_pdaScale);
		s_bezelSub.assign(1, DfmSub());
		DfmSub& sb = s_bezelSub[0];
		sb.tex = 0;
		const float mu = 0.5f / float(s_bezelW), mv = 0.5f / float(s_bezelH);
		const float R = 0.011f * s_pdaScale, bv = 0.0015f;
		const int NC = 8;
		auto vert = [&](float x, float y, float z, float nx, float ny, float nz, bool art) -> uint32_t
		{
			const uint32_t id = (uint32_t)(sb.v.size() / 11);
			const float u = art ? (x - c.l) / (c.r - c.l) : mu, v = art ? (c.t - y) / (c.t - c.b) : mv;
			const float vv[11] = { x, y, z, nx, ny, nz, u, v, 1.0f, 1.0f, 1.0f };
			sb.v.insert(sb.v.end(), vv, vv + 11);
			return id;
		};
		auto tri = [&](uint32_t a, uint32_t b, uint32_t d) { sb.i.push_back(a); sb.i.push_back(b); sb.i.push_back(d); };
		// Outline, counter-clockwise seen from the front: corner centers and outward normals.
		struct OP { float cx, cy, nx, ny; };
		std::vector<OP> ol;
		const float ccx[4] = { c.r - R, c.l + R, c.l + R, c.r - R }, ccy[4] = { c.t - R, c.t - R, c.b + R, c.b + R };
		for (int k = 0; k < 4; k++)
		{
			for (int i = 0; i <= NC; i++)
			{
				const float a = (float(k) + float(i) / NC) * 1.5707963f;
				ol.push_back({ ccx[k], ccy[k], cosf(a), sinf(a) });
			}
		}
		const int n = (int)ol.size();
		auto ring = [&](float rad, float z, float nz, float nscale, bool art, std::vector<uint32_t>& out)
		{
			out.clear();
			for (const OP& o : ol)
			{
				float nx = o.nx * nscale, ny = o.ny * nscale, nzz = nz;
				const float l = sqrtf(nx * nx + ny * ny + nzz * nzz); nx /= l; ny /= l; nzz /= l;
				out.push_back(vert(o.cx + o.nx * rad, o.cy + o.ny * rad, z, nx, ny, nzz, art));
			}
		};
		auto band = [&](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)   // a toward the front
		{
			for (int i = 0; i < n; i++) { const int j = (i + 1) % n; tri(a[i], b[i], b[j]); tri(a[i], b[j], a[j]); }
		};
		std::vector<uint32_t> fIn, fOut, sF, sB, bOut, bIn;
		// Front face (art) and its chamfer.
		ring(R - bv, c.zf, 1.0f, 0.0f, true, fIn);
		const uint32_t fc = vert(0.5f * (c.l + c.r), 0.5f * (c.b + c.t), c.zf, 0, 0, 1, true);
		for (int i = 0; i < n; i++) { tri(fc, fIn[i], fIn[(i + 1) % n]); }
		std::vector<uint32_t> fIn2; ring(R - bv, c.zf, 1.0f, 1.0f, true, fIn2);
		ring(R, c.zf - bv, 1.0f, 1.0f, true, fOut);
		band(fIn2, fOut);
		// Sides (metal), smooth around the corners.
		ring(R, c.zf - bv, 0.0f, 1.0f, false, sF);
		ring(R, c.zb + bv, 0.0f, 1.0f, false, sB);
		band(sF, sB);
		// Back chamfer and back face (metal).
		ring(R, c.zb + bv, -1.0f, 1.0f, false, bOut);
		ring(R - bv, c.zb, -1.0f, 1.0f, false, bIn);
		band(bOut, bIn);
		std::vector<uint32_t> bFace; ring(R - bv, c.zb, -1.0f, 0.0f, false, bFace);
		const uint32_t bc = vert(0.5f * (c.l + c.r), 0.5f * (c.b + c.t), c.zb, 0, 0, -1, false);
		for (int i = 0; i < n; i++) { tri(bc, bFace[(i + 1) % n], bFace[i]); }
		s_bezelBuiltScale = s_pdaScale;
	}
	std::vector<DfmTex> s_dfmTexSet[2];
	int s_dfmStateSet[2] = { 0, 0 };   // 0 not loaded, 1 loaded, -1 missing
#define s_dfmTex (s_dfmTexSet[dfmSetIdx()])
#define s_dfmState (s_dfmStateSet[dfmSetIdx()])
	GLuint s_progT = 0;
	GLint s_locTMvp = -1, s_locTModel = -1, s_locTEye = -1, s_locTLight = -1, s_locTTex = -1, s_locTHasTex = -1, s_locTPivot = -1, s_locTAngle = -1;
	void dfmLoad()
	{
		if (s_dfmState != 0) { return; }
		s_dfmState = -1;
		const char* fname = dfmSetIdx() == 1 ? "dfmod_combined.bin" : "dfmod_weapons.bin";
		FILE* f = fopen(fname, "rb");
		if (!f) { LOGE("%s not found: the weapons use the high resolution models", fname); return; }
		char magic[4]; int ver = 0, count = 0;
		bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "DFMW", 4) == 0 && fread(&ver, 4, 1, f) == 1 && fread(&count, 4, 1, f) == 1 && (ver == 1 || ver == 2);
		for (int w = 0; ok && w < count; w++)
		{
			int id = 0, ns = 0; float m[3];
			ok = fread(&id, 4, 1, f) == 1 && fread(m, 4, 3, f) == 3 && fread(&ns, 4, 1, f) == 1 && id > 0 && id < 10 && ns >= 0 && ns < 64;
			if (!ok) break;
			DfmWeapon& dw = s_dfm[id];
			dw.muzzle = { m[0] + c_gripInW.x, m[1] + c_gripInW.y, m[2] + c_gripInW.z };
			dw.subs.resize(ns);
			for (int k = 0; ok && k < ns; k++)
			{
				int hdr[3];
				ok = fread(hdr, 4, 3, f) == 3 && hdr[1] >= 0 && hdr[1] < 1000000 && hdr[2] >= 0 && hdr[2] < 3000000;
				if (!ok) break;
				DfmSub& sb = dw.subs[k];
				sb.tex = hdr[0];
				const int stride = ver == 2 ? 11 : 8;
				sb.v.resize(size_t(hdr[1]) * 11); sb.i.resize(hdr[2]);
				std::vector<float> raw(size_t(hdr[1]) * stride);
				ok = fread(raw.data(), 4, raw.size(), f) == raw.size() && fread(sb.i.data(), 4, sb.i.size(), f) == sb.i.size();
				for (int q = 0; ok && q < hdr[1]; q++)
				{
					float* d = &sb.v[size_t(q) * 11]; const float* r = &raw[size_t(q) * stride];
					for (int c = 0; c < 8; c++) { d[c] = r[c]; }
					d[8] = stride == 11 ? r[8] : 1.0f; d[9] = stride == 11 ? r[9] : 1.0f; d[10] = stride == 11 ? r[10] : 1.0f;
					d[0] += c_gripInW.x; d[1] += c_gripInW.y; d[2] += c_gripInW.z;
				}
			}
			dw.valid = ok;
		}
		int nt = 0;
		if (ok && fread(&nt, 4, 1, f) == 1 && nt >= 0 && nt < 64)
		{
			s_dfmTex.resize(nt);
			for (int t = 0; t < nt; t++)
			{
				int wh[2];
				if (fread(wh, 4, 2, f) != 2 || wh[0] <= 0 || wh[1] <= 0 || wh[0] > 4096 || wh[1] > 4096) { ok = false; break; }
				s_dfmTex[t].w = wh[0]; s_dfmTex[t].h = wh[1];
				s_dfmTex[t].rgba.resize(size_t(wh[0]) * wh[1] * 4);
				if (fread(s_dfmTex[t].rgba.data(), 1, s_dfmTex[t].rgba.size(), f) != s_dfmTex[t].rgba.size()) { ok = false; break; }
			}
		}
		fclose(f);
		if (!ok) { for (DfmWeapon& d : s_dfm) { d = DfmWeapon(); } s_dfmTex.clear(); LOGE("%s: bad file", fname); return; }
		s_dfmState = 1;
		LOGE("%s loaded (%d textures)", fname, nt);
	}
	bool dfmHas(int id) { return (s_weaponStyle == 3 || s_weaponStyle == 4) && s_dfmState == 1 && id > 0 && id < 10 && s_dfm[id].valid; }
	const HoldPose* holdPose(int id) { return (dfmHas(id) && (c_hold[id].fing[0] > 0.0f)) ? &c_hold[id] : nullptr; }
#ifndef HANDS_HOST_TEST
	// Textured meshes (Blender weapons, PDA bezel): texture + light + gloss from the texture alpha.
	void texMeshDraw(std::vector<DfmSub>& subs, std::vector<DfmTex>& texs, const M4& mvp, const M4& model, const XrPosef& eye, float light, float trigAngle)
	{
		if (!s_progT)
		{
			const char* vs =
				"#version 300 es\n"
				"uniform mat4 Mvp; uniform mat4 Model; uniform vec3 TrigPivot; uniform float TrigAngle;\n"
				"layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec2 aUv; layout(location=3) in vec3 aCol;\n"
				"out vec3 wPos; out vec3 wN; out vec2 uv; out vec3 vCol;\n"
				"void main(){\n"
				" vec3 p = aPos; vec3 n = aNrm;\n"
				" if (aCol.g > 1.5) {\n"   // trigger blade: swings back about its top with the index finger
				"  float c = cos(TrigAngle), s = sin(TrigAngle); vec3 d = p - TrigPivot;\n"
				"  p = TrigPivot + vec3(d.x, c * d.y - s * d.z, s * d.y + c * d.z); n = vec3(n.x, c * n.y - s * n.z, s * n.y + c * n.z); }\n"
				" wPos = (Model * vec4(p, 1.0)).xyz; wN = mat3(Model) * n; uv = aUv; vCol = aCol; gl_Position = Mvp * vec4(p, 1.0); }\n";
			const char* fs =
				"#version 300 es\n"
				"precision mediump float;\n"
				"uniform sampler2D Tex; uniform int HasTex; uniform vec3 Eye; uniform float Light;\n"
				"in vec3 wPos; in vec3 wN; in vec2 uv; in vec3 vCol; out vec4 o;\n"
				"void main(){\n"
				" vec4 t = HasTex == 1 ? texture(Tex, uv) : vec4(vCol, 0.25);\n"
				" vec3 N = normalize(wN); if (!gl_FrontFacing) N = -N; vec3 V = normalize(Eye - wPos);\n"
				" vec3 L = normalize(vec3(0.30, 0.90, 0.30));\n"
				" float dif = max(dot(N, L), 0.0); float fill = max(dot(N, V), 0.0);\n"
				" vec3 H = normalize(L + V); float sp = pow(max(dot(N, H), 0.0), 32.0) * t.a * 1.8;\n"
				" vec3 c = t.rgb * (0.42 + 0.62 * dif + 0.30 * fill) * Light * 1.5 + vec3(sp) * Light;\n"
				" float em = HasTex == 1 ? clamp(vCol.r - 1.0, 0.0, 1.0) : 0.0;\n"
				" c = mix(c, t.rgb * 1.25, em);\n"
				" o = vec4(c, 1.0); }\n";
			s_progT = glCreateProgram();
			glAttachShader(s_progT, compileShader(GL_VERTEX_SHADER, vs));
			glAttachShader(s_progT, compileShader(GL_FRAGMENT_SHADER, fs));
			glLinkProgram(s_progT);
			GLint ok = 0; glGetProgramiv(s_progT, GL_LINK_STATUS, &ok);
			if (!ok) { LOGE("dfmod shader link failed"); return; }
			s_locTMvp = glGetUniformLocation(s_progT, "Mvp"); s_locTModel = glGetUniformLocation(s_progT, "Model");
			s_locTEye = glGetUniformLocation(s_progT, "Eye"); s_locTLight = glGetUniformLocation(s_progT, "Light");
			s_locTTex = glGetUniformLocation(s_progT, "Tex"); s_locTHasTex = glGetUniformLocation(s_progT, "HasTex");
			s_locTPivot = glGetUniformLocation(s_progT, "TrigPivot"); s_locTAngle = glGetUniformLocation(s_progT, "TrigAngle");
		}
		glUseProgram(s_progT);
		const M4 mvpW = mul(mvp, model);
		glUniformMatrix4fv(s_locTMvp, 1, GL_FALSE, mvpW.m);
		glUniformMatrix4fv(s_locTModel, 1, GL_FALSE, model.m);
		glUniform3f(s_locTEye, eye.position.x, eye.position.y, eye.position.z);
		glUniform1f(s_locTLight, light);
		glUniform1i(s_locTTex, 0);
		// Trigger of the standard trigger group (tools/weapons/kit.py std_trigger), pivot at the top of the blade.
		glUniform3f(s_locTPivot, c_gripInW.x, -0.002f + c_gripInW.y, -0.038f + c_gripInW.z);
		glUniform1f(s_locTAngle, trigAngle);
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
		for (DfmSub& sb : subs)
		{
			if (!sb.vao)
			{
				glGenVertexArrays(1, &sb.vao); glBindVertexArray(sb.vao);
				glGenBuffers(1, &sb.vbo); glBindBuffer(GL_ARRAY_BUFFER, sb.vbo);
				glBufferData(GL_ARRAY_BUFFER, sb.v.size() * 4, sb.v.data(), GL_STATIC_DRAW);
				glGenBuffers(1, &sb.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sb.ibo);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, sb.i.size() * 4, sb.i.data(), GL_STATIC_DRAW);
				glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 44, (void*)0);
				glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 44, (void*)12);
				glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 44, (void*)24);
				glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 44, (void*)32);
				glBindVertexArray(0);
			}
			const bool hasTex = sb.tex >= 0 && sb.tex < (int)texs.size();
			if (hasTex)
			{
				DfmTex& t = texs[sb.tex];
				if (!t.id)
				{
					glGenTextures(1, &t.id); glBindTexture(GL_TEXTURE_2D, t.id);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.rgba.data());
					glGenerateMipmap(GL_TEXTURE_2D);
					if (&texs == &s_dfmTexSet[0] || &texs == &s_dfmTexSet[1]) { std::vector<unsigned char>().swap(t.rgba); }   // weapon textures: GPU copy only
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, t.nearest ? GL_NEAREST : GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
				}
				glBindTexture(GL_TEXTURE_2D, t.id);
			}
			glUniform1i(s_locTHasTex, hasTex ? 1 : 0);
			glBindVertexArray(sb.vao);
			glDrawElements(GL_TRIANGLES, (GLsizei)sb.i.size(), GL_UNSIGNED_INT, nullptr);
		}
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	void dfmDraw(int id, const M4& mvp, const M4& model, const XrPosef& eye, float light)
	{
		texMeshDraw(s_dfm[id].subs, s_dfmTex, mvp, model, eye, light, -0.40f * s_trigPull);   // trigger: -0.40 rad at full pull
	}
#endif
}

void hands_setWeaponStyle(int style)
{
	s_weaponStyle = style;
	c_handVox = style == 2 ? 0.0058f / 3.0f : 0.0058f;
	if (style == 3 || style == 4) { dfmLoad(); }
}
void hands_setRightHand(int on) { s_rightHand = on != 0; }
void hands_setPdaScale(float scale) { s_pdaScale = fminf(1.6f, fmaxf(0.6f, scale)); }
float hands_pdaScreenWidth() { float cx, cy, w; pdaScreenRect(s_pdaScale, &cx, &cy, &w); return w; }
float hands_pdaScreenHeight() { float cx, cy, w, h; pdaScreenRect(s_pdaScale, &cx, &cy, &w, &h); return h; }
void hands_setPdaFull(int on) { s_pdaMode = (on == 1) ? 1 : 0; s_pdaFit = (on == 2); }
bool hands_pdaWindow(float win[4])
{
	if (s_pdaFit || !(s_pdaLiveOk || s_pdaMode == 1)) { return false; }
	for (int k = 0; k < 4; k++) { win[k] = float(s_art[s_pdaMode].win[k]); }
	return true;
}
void hands_setPdaLiveTexture(unsigned int id, int w, int h)
{
	s_pdaLiveOk = id != 0;
	DfmTex& t = s_liveTex[0];
	t.id = id; t.w = w; t.h = h; t.nearest = false;
}
void hands_setPdaBezel(int kind, const unsigned char* rgba, int w, int h, int x0, int y0, int x1, int y1)
{
	if (kind < 0 || kind > 1 || !rgba || w != s_bezelW || h != s_bezelH || x1 <= x0 || y1 <= y0) { return; }
	s_art[kind].win[0] = x0; s_art[kind].win[1] = y0; s_art[kind].win[2] = x1; s_art[kind].win[3] = y1;
	DfmTex& t = s_bezelTex[kind][0];
#ifndef HANDS_HOST_TEST
	if (t.id) { glDeleteTextures(1, &t.id); t.id = 0; }
#endif
	t.w = w; t.h = h; t.nearest = true; t.rgba.assign(rgba, rgba + size_t(w) * h * 4);
	s_bezelBuiltScale = -1.0f;
}
#ifdef HANDS_HOST_TEST
// Host test: voxel hand in its hand frame (identity), written as triangles (x y z r g b).
void hands_hostVoxHand(float side, float ci, float co, float ct, const char* path)
{
	s_vHand.clear();
	buildHand(ident(), side, ci, co, ct);
	FILE* f = fopen(path, "w");
	if (!f) { return; }
	for (const Vtx& v : s_vHand) { fprintf(f, "%f %f %f %f %f %f\n", v.x, v.y, v.z, v.r, v.g, v.b); }
	fclose(f);
}
void hands_setPdaParams(const float* p)
{
	for (int i = 0; i < 3; i++) { s_pdaT[i] = p[i]; s_pdaCurl[i] = p[5 + i]; s_curl[0][i] = p[5 + i]; }
	s_pdaTilt = p[3]; s_pdaYaw = p[4];
	for (int i = 0; i < 5; i++) { s_pdaThumb[i] = p[8 + i]; }
}
#endif

void hands_init()
{
	const char* vs =
		"#version 300 es\n"
		"uniform mat4 Mvp;\n"
		"layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUv; layout(location=2) in vec4 aCol; layout(location=3) in float aEmis;\n"
		"out vec2 uv; out vec4 col; out float emis;\n"
		"void main(){ uv = aUv; col = aCol; emis = aEmis; gl_Position = Mvp * vec4(aPos, 1.0); }\n";
	const char* fs =
		"#version 300 es\n"
		"precision mediump float;\n"
		"uniform sampler2D Tex; uniform float Light; in vec2 uv; in vec4 col; in float emis; out vec4 o;\n"
		"void main(){ vec3 c = texture(Tex, uv).rgb * col.rgb * mix(Light, 1.0, emis); o = vec4(c * col.a, col.a); }\n";
	{
		const char* svs =
			"#version 300 es\n"
			"uniform mat4 Mvp; uniform mat4 Model; uniform int Skin; uniform mat4 Bones[25];\n"
			"layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aCol; layout(location=3) in vec2 aMat;\n"
			"layout(location=4) in vec4 aJ; layout(location=5) in vec4 aW;\n"
			"out vec3 wPos; out vec3 wN; out vec3 col; out vec2 mat;\n"
			"void main(){ vec4 p = vec4(aPos, 1.0); vec3 n = aNrm;\n"
			" if (Skin == 1) { mat4 B = Bones[int(aJ.x)] * aW.x + Bones[int(aJ.y)] * aW.y + Bones[int(aJ.z)] * aW.z + Bones[int(aJ.w)] * aW.w; p = B * p; n = mat3(B) * n; }\n"
			" wPos = (Model * p).xyz; wN = mat3(Model) * n; col = aCol; mat = aMat; gl_Position = Mvp * p; }\n";
		const char* sfs =
			"#version 300 es\n"
			"precision mediump float;\n"
			"uniform vec3 Eye; uniform float Light;\n"
			"in vec3 wPos; in vec3 wN; in vec3 col; in vec2 mat; out vec4 o;\n"
			"void main(){\n"
			" vec3 N = normalize(wN); vec3 V = normalize(Eye - wPos);\n"
			" vec3 L = normalize(vec3(0.30, 0.90, 0.30));\n"   // light from above, like the level lights
			" float dif = max(dot(N, L), 0.0); float fill = max(dot(N, V), 0.0);\n"
			" float rim = pow(1.0 - fill, 3.0) * 0.25;\n"
			" vec3 H = normalize(L + V); float sp = pow(max(dot(N, H), 0.0), 40.0) * mat.x * 1.6 + pow(fill, 24.0) * mat.x * 0.25;\n"
			" vec3 c = col * (0.30 + 0.62 * dif + 0.28 * fill + rim) * Light + vec3(sp) * Light;\n"
			" if (mat.y > 0.5) c = col;\n"
			" o = vec4(c, 1.0); }\n";
		s_progS = glCreateProgram();
		glAttachShader(s_progS, compileShader(GL_VERTEX_SHADER, svs));
		glAttachShader(s_progS, compileShader(GL_FRAGMENT_SHADER, sfs));
		glLinkProgram(s_progS);
		GLint ok = 0; glGetProgramiv(s_progS, GL_LINK_STATUS, &ok);
		if (!ok) { LOGE("smooth shader link failed"); glDeleteProgram(s_progS); s_progS = 0; }
		else
		{
			s_locSMvp = glGetUniformLocation(s_progS, "Mvp");
			s_locSModel = glGetUniformLocation(s_progS, "Model");
			s_locSEye = glGetUniformLocation(s_progS, "Eye");
			s_locSLight = glGetUniformLocation(s_progS, "Light");
			s_locSSkin = glGetUniformLocation(s_progS, "Skin");
			s_locSBones = glGetUniformLocation(s_progS, "Bones");
		}
	}
	s_prog = glCreateProgram();
	glAttachShader(s_prog, compileShader(GL_VERTEX_SHADER, vs));
	glAttachShader(s_prog, compileShader(GL_FRAGMENT_SHADER, fs));
	glLinkProgram(s_prog);
	s_locMvp = glGetUniformLocation(s_prog, "Mvp");
	s_locTex = glGetUniformLocation(s_prog, "Tex");
	s_locLight = glGetUniformLocation(s_prog, "Light");
	glGenBuffers(1, &s_vbo);
	glGenVertexArrays(1, &s_vao);
	glBindVertexArray(s_vao);
	glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(5 * sizeof(float)));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(9 * sizeof(float)));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// Neutral skin color until the game texture is available.
	const unsigned char px[4] = { 170, 130, 105, 255 };
	glGenTextures(1, &s_tex);
	glBindTexture(GL_TEXTURE_2D, s_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// Weapons: slightly noisy metal so flat faces do not look like plastic.
	{
		unsigned char n[16 * 16 * 4];
		unsigned int seed = 1234567u;
		for (int i = 0; i < 16 * 16; i++)
		{
			seed = seed * 1103515245u + 12345u;
			const int v = 215 + int((seed >> 16) % 41);
			const bool edge = (i % 16) == 0 || (i / 16) == 0 || (i % 16) == 15 || (i / 16) == 15;
			const int e = edge ? v - 50 : v;
			n[i * 4 + 0] = n[i * 4 + 1] = n[i * 4 + 2] = (unsigned char)(e > 255 ? 255 : e);
			n[i * 4 + 3] = 255;
		}
		glGenTextures(1, &s_white);
		glBindTexture(GL_TEXTURE_2D, s_white);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, n);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	{
		const unsigned char wpx[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &s_flat);
		glBindTexture(GL_TEXTURE_2D, s_flat);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, wpx);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	buildPistolArt();
	// Detailed voxel models of the whole arsenal.
	for (int id = 1; id < 10; id++)
	{
		std::vector<float> f;
		float mz[3];
		const float grip[3] = { c_gripInW.x, c_gripInW.y, c_gripInW.z };
		if (!weapons_buildModel(id, f, mz, grip)) { continue; }
		VoxWeapon& vw = s_voxW[id];
		vw.local.resize(f.size() / 10);
		memcpy(vw.local.data(), f.data(), f.size() * sizeof(float));
		vw.muzzle = { mz[0], mz[1], mz[2] };
		vw.valid = vw.detailed = true;
	}
}

static bool s_hasTexture = false;
bool hands_hasTexture() { return s_hasTexture; }

void hands_setTexture(const unsigned char* rgba, int w, int h)
{
	glBindTexture(GL_TEXTURE_2D, s_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	s_hasTexture = true;
	// Glove / cuff / sleeve colors from the game's hand bitmap.
	s_vpal.glove.clear(); s_vpal.cuff.clear(); s_vpal.sleeve.clear();
	for (int i = 0; i < w * h; i += 7)
	{
		const unsigned char* c = &rgba[i * 4];
		const int r = c[0], g = c[1], b = c[2];
		const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
		const V3 v = { r / 255.0f, g / 255.0f, b / 255.0f };
		if (r > g + 40 && r > b + 60 && mx > 90) { s_vpal.sleeve.push_back(v); }
		else if (mn > 170 && mx - mn < 30) { s_vpal.cuff.push_back(v); }
		else if (mx < 90 && mx - mn < 25 && mx > 12) { s_vpal.glove.push_back(v); }
	}
	// Keep the variety small so the voxels read as pixel art, not noise.
	auto trim = [](std::vector<V3>& v, size_t n) { if (v.size() > n) { std::vector<V3> o; for (size_t i = 0; i < n; i++) o.push_back(v[i * v.size() / n]); v = o; } };
	trim(s_vpal.glove, 6); trim(s_vpal.cuff, 3); trim(s_vpal.sleeve, 4);
}

// Pose of the weapon frame relative to the eye that reproduces each original first-person sprite
// (fitted offline on the silhouettes: yaw, pitch, roll, x, y, z).
namespace
{
	const float c_viewPose[10][6] =
	{
		{ 0, 0, 0, 0, 0, 0 },
		{ 0.2512f, 0.1501f, 0.0002f, 0.0666f, -0.1725f, -0.2976f },
		{ 0.2825f, 0.1733f, 0.0059f, 0.0853f, -0.1580f, -0.1533f },
		{ 0.0030f, 0.2114f, 0.0057f, 0.0319f, -0.0434f, -0.1415f },
		{ 0.0055f, 0.1011f, 0.0035f, 0.0907f, -0.2116f, 0.0818f },
		{ 0.7251f, -0.1451f, 0.0117f, -0.0076f, -0.1986f, -0.2140f },
		{ -0.0038f, 0.1819f, 0.0055f, 0.0563f, -0.1433f, -0.1558f },
		{ 0.0101f, 0.0230f, 0.0054f, 0.0149f, -0.0430f, -0.0934f },
		{ -0.1397f, -0.0260f, 0.0045f, 0.0477f, -0.1208f, -0.1100f },
		{ -0.1308f, 0.2253f, 0.0036f, 0.2223f, -0.1649f, -0.2057f },
	};
	bool s_viewApplied[10] = {};
}

bool hands_needsWeaponView(int weaponId) { return weaponId > 0 && weaponId < 10 && !s_viewApplied[weaponId]; }

void hands_setWeaponView(int id, const unsigned char* rgba, int w, int h, int x0, int y0)
{
	if (id <= 0 || id >= 10) { return; }
	s_viewApplied[id] = true;
	WeaponView view = {};
	view.rgba = rgba; view.w = w; view.h = h; view.x0 = x0; view.y0 = y0;
	for (int i = 0; i < 6; i++) { view.pose[i] = c_viewPose[id][i]; }
	std::vector<float> f;
	float mz[3];
	const float grip[3] = { c_gripInW.x, c_gripInW.y, c_gripInW.z };
	if (!weapons_buildModel(id, f, mz, grip, &view)) { return; }
	VoxWeapon& vw = s_voxW[id];
#ifndef HANDS_HOST_TEST
	if (vw.vbo) { glDeleteBuffers(1, &vw.vbo); vw.vbo = 0; }
#endif
	vw.local.resize(f.size() / 10);
	memcpy(vw.local.data(), f.data(), f.size() * sizeof(float));
	vw.muzzle = { mz[0], mz[1], mz[2] };
	vw.valid = vw.detailed = true;
}

void hands_setWeaponSprite(int weaponId, const unsigned char* rgba, int w, int h)
{
	buildVoxWeapon(weaponId, rgba, w, h);
}
bool hands_hasWeaponSprite(int weaponId) { return weaponId > 0 && weaponId < 10 && s_voxW[weaponId].valid; }

// Muzzle of the weapon as drawn: the remaster models have their own barrels.
static V3 weaponMuzzle(int id)
{
	if (dfmHas(id)) { return s_dfm[id].muzzle; }
	return s_voxW[id].valid ? s_voxW[id].muzzle : c_models[id].muzzle;
}

static bool holdsModel(const HandsFrame& f) { return f.weaponId > 0 && f.weaponId < 10 && !f.ammoEmpty; }

bool hands_muzzle(const HandInput& right, int weaponId, XrVector3f* pos, XrVector3f* dir)
{
	M4 A, H, W;
	if (!handFrames(right, 1.0f, &A, &H, &W)) { return false; }
	const int id = (weaponId >= 0 && weaponId < 10) ? weaponId : 0;
	const V3 m = weaponMuzzle(id);
	const V3 p = xform(W, m.x, m.y, m.z);
	pos->x = p.x; pos->y = p.y; pos->z = p.z;
	dir->x = -W.m[8]; dir->y = -W.m[9]; dir->z = -W.m[10];
	return true;
}

namespace
{
	// Ammo panel spot on the left wall of the weapon (weapon frame W), found on the drawn mesh: the
	// leftmost surface of the body a few cm ahead of the hand. Cached per look and weapon.
	struct PanelSpot { int state = 0; V3 p = { 0, 0, 0 }; };
	PanelSpot s_panelSpot[5][10];
	template <class F> void forWeaponPoints(int style, int id, F f)
	{
		if ((style == 3 || style == 4) && dfmHas(id))
		{
			for (const DfmSub& sb : s_dfm[id].subs) for (size_t q = 0; q + 10 < sb.v.size(); q += 11) { f(sb.v[q], sb.v[q + 1], sb.v[q + 2]); }
			return;
		}
		if (style == 0)
		{
			const WeaponSlot& ws = s_wSlot[0][id];
			if (ws.state >= 2) { for (const SVtx& v : ws.smooth.v) { f(v.x, v.y, v.z); } return; }
		}
		if (style == 2)
		{
			const WeaponSlot& ws = s_wSlot[1][id];
			if (ws.state >= 2 && !ws.fine.empty()) { for (const Vtx& v : ws.fine) { f(v.x, v.y, v.z); } return; }
		}
		for (const Vtx& v : s_voxW[id].local) { f(v.x, v.y, v.z); }
	}
	bool panelSpot(int id, V3* out)
	{
		const int st = (s_weaponStyle >= 0 && s_weaponStyle < 5) ? s_weaponStyle : 0;
		PanelSpot& ps = s_panelSpot[st][id];
		if (ps.state == 0)
		{
			// Columns of the body from 11 cm ahead of the grip to 5 cm behind it, above the trigger housing:
			// the one with the tallest flat left wall wins (preferring 5 cm ahead of the hand).
			const float y0 = c_gripInW.y + 0.065f;
			float bestScore = -1e9f; int nAll = 0;
			for (int k = 0; k <= 16; k++)
			{
				const float zc = c_gripInW.z - 0.10f + 0.01f * k;
				float minX = 1.0f; int n = 0;
				forWeaponPoints(st, id, [&](float x, float y, float z) { if (fabsf(z - zc) < 0.020f && y > y0) { minX = fminf(minX, x); n++; } });
				nAll += n;
				if (n < 8) { continue; }
				float ya = 1.0f, yb = -1.0f;
				forWeaponPoints(st, id, [&](float x, float y, float z) { if (fabsf(z - zc) < 0.020f && y > y0 && x < minX + 0.004f) { ya = fminf(ya, y); yb = fmaxf(yb, y); } });
				const float score = fminf(yb - ya, 0.04f) - fabsf(zc - (c_gripInW.z - 0.05f)) * 0.3f;
				if (score > bestScore) { bestScore = score; ps.p = { minX - 0.003f, 0.5f * (ya + yb), zc }; }
			}
			if (nAll < 8 || bestScore < -1e8f) { return false; }   // mesh not ready yet: try again next frame
			ps.state = 1;
		}
		*out = ps.p;
		return true;
	}
}

int hands_weaponPanel(const HandInput& right, int weaponId, XrPosef* pose, const XrVector3f& head)
{
	// Detonators and mines sit in the palm: their ammo goes to the right wrist instead.
	if (weaponId <= 0 || weaponId >= 10 || weaponId == 3 || weaponId == 7) { return 0; }
	M4 A, H, W;
	s_cApply = true;
	const bool ok = handFrames(right, 1.0f, &A, &H, &W);
	s_cApply = false;
	if (!ok) { return 0; }
	// Ammo display flat on the left wall of the weapon (facing out, tilted a little up toward the eyes):
	// a layer drawn over the world, so it sits on the surface instead of inside the weapon.
	M4 P;
	if (s_weaponStyle == 3 && dfmHas(weaponId) && (weaponId == 5 || weaponId == 9))
	{
		// Big Blender weapons (fusion cutter, assault cannon): on the flat back of the body, facing the eyes.
		// Spots from tools/weapons (fusion.py breech, cannon.py rear body), kit frame + c_gripInW.
		const V3 rear = (weaponId == 9) ? V3{ 0.0f, 0.094f, 0.2335f } : V3{ 0.0f, 0.056f, 0.0545f };
		P = mul(mul(W, translate(rear.x + c_gripInW.x, rear.y + c_gripInW.y, rear.z + c_gripInW.z)), rotX(-0.25f));
	}
	else
	{
		V3 sp;
		if (!panelSpot(weaponId, &sp)) { return -1; }
		P = mul(mul(W, translate(sp.x, sp.y, sp.z)), mul(rotY(-1.5707963f), rotX(-0.30f)));
	}
	{
		// The depth compression scales the frame: rebuild a pure rotation for the quaternion.
		const float sx = sqrtf(P.m[0] * P.m[0] + P.m[1] * P.m[1] + P.m[2] * P.m[2]);
		if (sx > 1e-6f && fabsf(sx - 1.0f) > 1e-4f) { for (int i = 0; i < 12; i++) { if ((i & 3) != 3) P.m[i] /= sx; } }
	}
	// Only from the side it faces (seen from the other side it would show through the weapon).
	const V3 nrm = { P.m[8], P.m[9], P.m[10] };
	if ((head.x - P.m[12]) * nrm.x + (head.y - P.m[13]) * nrm.y + (head.z - P.m[14]) * nrm.z < 0.02f) { return -1; }
	pose->orientation = quatOf(P);
	{
		XrQuaternionf& q = pose->orientation;
		const float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
		if (l > 1e-6f) { q.x /= l; q.y /= l; q.z /= l; q.w /= l; } else { q = { 0, 0, 0, 1 }; }
	}
	pose->position = { P.m[12], P.m[13], P.m[14] };
	return 1;
}

bool hands_pdaScreen(const HandInput& left, XrPosef* screen)
{
	M4 A, H, W;
	if (!handFrames(left, -1.0f, &A, &H, &W)) { return false; }
	float cx, cy, w;
	pdaScreenRect(s_pdaScale, &cx, &cy, &w);
	const M4 P = mul(pdaFrame(H), translate(cx, cy, 0.0085f));
	screen->orientation = quatOf(P);
	screen->position = { P.m[12], P.m[13], P.m[14] };
	return true;
}

bool hands_watch(const HandInput& left, XrPosef* face, XrPosef* holo, const XrVector3f& head)
{
	M4 A, H, W;
	if (!handFrames(left, -1.0f, &A, &H, &W)) { return false; }
	const M4 wf = watchFrame(H, -1.0f);
	M4 P = mul(wf, translate(0.0f, 0.0065f, 0.0f));
	// Landscape screen: image right = toward the fingers (-Z), image up = -X, facing +Y.
	M4 R = ident();
	R.m[0] = 0.0f; R.m[1] = 0.0f; R.m[2] = -1.0f;     // X column
	R.m[4] = -1.0f; R.m[5] = 0.0f; R.m[6] = 0.0f;     // Y column
	R.m[8] = 0.0f; R.m[9] = 1.0f; R.m[10] = 0.0f;     // Z column
	P = mul(P, R);
	face->orientation = quatOf(P);
	face->position = { P.m[12], P.m[13], P.m[14] };
	// Holographic map: projected straight up from the watch (world up, like a Star Wars holo
	// projector), slightly pushed away from the eyes, turned toward them (yaw + pitch, no roll).
	float ax = wf.m[12] - head.x, az = wf.m[14] - head.z;
	const float al = sqrtf(ax * ax + az * az);
	if (al > 1e-3f) { ax /= al; az /= al; } else { ax = 0.0f; az = 0.0f; }
	const V3 c = { wf.m[12] + ax * 0.04f, wf.m[13] + 0.14f, wf.m[14] + az * 0.04f };
	const float dx = head.x - c.x, dy = head.y - c.y, dz = head.z - c.z;
	const float yaw = atan2f(dx, dz), pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
	M4 Hm = mul(translate(c.x, c.y, c.z), mul(rotY(yaw), rotX(-pitch)));
	holo->orientation = quatOf(Hm);
	holo->position = { c.x, c.y, c.z };
	return true;
}

bool hands_wristPanel(const HandInput& hand, int side, XrPosef* pose)
{
	M4 A, H, W;
	if (!handFrames(hand, side > 0 ? 1.0f : -1.0f, &A, &H, &W)) { return false; }
	// On top of the forearm, just behind the wrist, tilted toward the eyes.
	M4 P = mul(H, translate(-float(side) * 0.005f, 0.032f, 0.065f));
	P = mul(P, rotY(-float(side) * 0.35f));
	P = mul(P, rotX(-1.05f));
	pose->orientation = quatOf(P);
	pose->position = { P.m[12], P.m[13], P.m[14] };
	return true;
}

void hands_viewProj(const XrPosef& eyePose, const XrFovf& fov, float zn, float zf, float out[16])
{
	const float l = tanf(fov.angleLeft), r = tanf(fov.angleRight), u = tanf(fov.angleUp), d = tanf(fov.angleDown);
	M4 proj = {};
	proj.m[0] = 2.0f / (r - l);
	proj.m[5] = 2.0f / (u - d);
	proj.m[8] = (r + l) / (r - l);
	proj.m[9] = (u + d) / (u - d);
	proj.m[10] = zf / (zn - zf);
	proj.m[11] = -1.0f;
	proj.m[14] = zn * zf / (zn - zf);
	const M4 vp = mul(proj, inverseRigid(fromPose(eyePose)));
	memcpy(out, vp.m, sizeof(vp.m));
}

void hands_setCompression(const XrVector3f& center, float scale)
{
	s_cCenter = { center.x, center.y, center.z };
	s_cScale = fminf(1.0f, fmaxf(0.2f, scale));
}

void hands_draw(const XrPosef& eyePose, const XrFovf& fov, float unitsPerMeter, const HandInput hands[2], const HandsFrame& frame, float dt)
{
	if (!s_prog) { return; }
	struct ApplyScope { ApplyScope() { s_cApply = true; } ~ApplyScope() { s_cApply = false; } } applyScope;
	const bool rebuild = dt > 0.0f || !s_built;
	if (rebuild)
	{
	s_vHand.clear(); s_vSolid.clear(); s_vAdd.clear();
	s_segDraws.clear();
	s_sDraws.clear();
	s_drawWeapon = -1;
	s_bezelDraw = false;
	const bool holding = holdsModel(frame) && !frame.wheel && !frame.point;   // the weapon goes back while choosing or pointing
	for (int h = 0; h < 2; h++)
	{
		const HandInput& in = hands[h];
		float target[3] =
		{
			in.trigger > 0.02f ? 0.25f + 0.75f * in.trigger : (in.triggerTouch ? 0.2f : 0.0f),
			in.grip,
			// Thumb: closed when resting on a button, or as a full fist with grip + trigger.
			in.thumbTouch ? 1.0f : 0.0f,   // thumb on the controller = closed; lifted = thumbs up
		};
		if (h == 1 && frame.point)
		{
			// Pointing at the PDA: index out (it bends a little on the click), the rest closed.
			target[0] = 0.35f * in.trigger;
			target[1] = 0.90f;
			target[2] = 0.75f;
		}
		else if (h == 1 && frame.weaponId >= 0)
		{
			if (holding && frame.weaponId != 3 && frame.weaponId != 7)
			{
				// Pistol grip: the fingers stay around the handle; only the index moves, on the trigger.
				target[0] = 0.30f + 0.35f * in.trigger;
				target[1] = 0.80f;
				target[2] = 0.85f;
			}
			else if (holding)
			{
				// Thermal detonator / mine in the palm: the whole hand holds it.
				target[0] = 0.55f;
				target[1] = 0.60f;
				target[2] = 0.7f;
			}
			else
			{
				// Fists.
				target[0] = fmaxf(target[0], 0.85f);
				target[1] = fmaxf(target[1], 0.90f);
				target[2] = in.thumbTouch ? 1.0f : 0.0f;
			}
		}
		if (h == 0 && frame.pda)
		{
			// Holding the PDA: flat fingers under it, the thumb over its edge.
			target[0] = s_pdaCurl[0]; target[1] = s_pdaCurl[1]; target[2] = s_pdaCurl[2];
		}
		const float k = fminf(1.0f, dt * 18.0f);
		for (int i = 0; i < 3; i++) { s_curl[h][i] += (target[i] - s_curl[h][i]) * k; }
		if (h == 1) { s_trigPull = holding ? fminf(1.0f, fmaxf(0.0f, (s_curl[h][0] - 0.30f) / 0.35f)) : 0.0f; }
		if (!in.valid && !in.aimValid) { continue; }
		const float side = (h == 1) ? 1.0f : -1.0f;
		M4 A, H, W;
		if (!handFrames(in, side, &A, &H, &W)) { continue; }
		// The hand holding a weapon: drawn when the option is on (otherwise only the weapon).
		const bool hideHand = (h == 1 && holding && !s_rightHand);
		// Always the standard glove (the voxel glove was dropped in 0.24).
		const bool voxHand = false;
		if (!hideHand)
		{
			M4 Hd = H;
			const HoldPose* hp = (h == 1 && holding && !voxHand) ? holdPose(frame.weaponId) : nullptr;
			if (hp)
			{
				// Pose fitted on the Blender model (hosttest/gripfit.cpp): hand offset, fingers around the
				// grip, thumb wrapped; the index goes from resting on the trigger to pulled.
				Hd = mul(mul(mul(H, translate(hp->d[0], hp->d[1], hp->d[2])), mul(rotY(hp->ry), rotZ(hp->rz))), rotX(hp->rx));
				const float pull = fminf(1.0f, fmaxf(0.0f, (s_curl[h][0] - 0.30f) / 0.35f));
				const float ia[4] = { hp->idx[0] + c_holdPull[0] * pull, hp->idx[1] + c_holdPull[1] * pull, hp->idx[2] + c_holdPull[2] * pull, hp->idx[3] };
				if (hp->fj[0] > 0.0f)
				{
					// Per-joint fingers; on a trigger the index keeps its own joints (rest to pulled).
					handskin_setFingerJoints(hp->fj);
					if (hp->hasTrigger) { handskin_setIndexOverride(ia); }
				}
				else
				{
					handskin_setIndexOverride(hp->hasTrigger ? ia : hp->idx);
					handskin_setFingerOverride(hp->fing);
				}
				handskin_setThumbOverride(hp->thumb);
			}
			if (h == 0 && frame.pda) { handskin_setThumbOverride(s_pdaThumb); handskin_setFingerJoints(s_pdaJoints); }
			if (voxHand || !buildHandSkinned(Hd, side, s_curl[h][0], s_curl[h][1], s_curl[h][2])) { buildHand(Hd, side, s_curl[h][0], s_curl[h][1], s_curl[h][2]); }
			handskin_setThumbOverride(nullptr);
			handskin_setFingerOverride(nullptr);
			handskin_setFingerJoints(nullptr);
			handskin_setIndexOverride(nullptr);
		}
		if (h == 0 && frame.pda)
		{
			// PDA body: the bezel case (textured with the configuration screen frame) under the screen layer.
			s_bezelDraw = true;
			{ float cx, cy, w; pdaScreenRect(s_pdaScale, &cx, &cy, &w); s_bezelM = mul(fromPose(frame.pdaPose), translate(-cx, -cy, -0.0085f)); }
			if (!s_pdaLiveOk && !s_bezelTex[s_pdaMode][0].w)
			{
				// No bezel art yet (game files not read): plain case.
				const PdaCase c = pdaCase(s_pdaScale);
				M4 B = ident();
				B.m[0] = 1.0f; B.m[1] = 0.0f; B.m[2] = 0.0f;
				B.m[4] = 0.0f; B.m[5] = 0.0f; B.m[6] = 1.0f;
				B.m[8] = 0.0f; B.m[9] = -1.0f; B.m[10] = 0.0f;
				const M4 W = mul(mul(s_bezelM, translate(0.5f * (c.l + c.r), c.b, 0.5f * (c.zb + c.zf))), B);   // the piece grows along +Y from its origin
				addPiece(W, HP_WATCH, c.r - c.l, c.zf - c.zb, c.t - c.b, -1.0f);
				s_bezelDraw = false;
			}
		}

		if (h == 0 && frame.pda && frame.pdaDot)
		{
			// Pointer dot on the PDA frame (the game's own cursor only shows inside the screen).
			const M4 P = fromPose(frame.pdaPose);
			const V3 ax = { P.m[0], P.m[1], P.m[2] }, ay = { P.m[4], P.m[5], P.m[6] }, az = { P.m[8], P.m[9], P.m[10] };
			const XrVector3f& o = frame.pdaDotPos;
			auto Q = [&](float x, float y) { return V3{ o.x + ax.x * x + ay.x * y + az.x * 0.0012f, o.y + ax.y * x + ay.y * y + az.y * 0.0012f, o.z + ax.z * x + ay.z * y + az.z * 0.0012f }; };
			const float r = 0.0022f;
			addQuadAdd(Q(-r, -r), Q(r, -r), Q(r, r), Q(-r, r), 1.0f, 0.85f, 0.2f, 0.95f, 0.95f);
			const float r2 = 0.0010f;
			addQuadAdd(Q(-r2, -r2), Q(r2, -r2), Q(r2, r2), Q(-r2, r2), 1.0f, 1.0f, 1.0f, 0.9f, 0.9f);
		}
		if (h == 1 && holding)
		{
			const Model& md = c_models[frame.weaponId];
			if (s_voxW[frame.weaponId].valid)
			{
#ifdef HANDS_HOST_TEST
				const VoxWeapon& hw = s_voxW[frame.weaponId];
				for (const Vtx& lv : hw.local)
				{
					Vtx v = lv; const V3 q = xform(W, lv.x, lv.y, lv.z); v.x = q.x; v.y = q.y; v.z = q.z; s_vSolid.push_back(v);
				}
#else
				// Static mesh, drawn with its own model matrix.
				s_drawWeapon = frame.weaponId;
				s_drawWeaponW = W;
#endif
			}
			else
			{
				for (int i = 0; i < md.count; i++) { addPart(W, md.parts[i]); }
			}
		}
		if (h == 1 && frame.weaponId >= 0)
		{
			const Model& md = c_models[(frame.weaponId < 10) ? frame.weaponId : 0];
			const int wid = (frame.weaponId < 10) ? frame.weaponId : 0;
			const V3 mm = weaponMuzzle(wid);
			const V3 mz = xform(W, mm.x, mm.y, mm.z);
			const V3 ax = { W.m[0], W.m[1], W.m[2] }, ay = { W.m[4], W.m[5], W.m[6] }, az = { W.m[8], W.m[9], W.m[10] };
			if (frame.flash > 0.01f && frame.weaponId > 0 && frame.weaponId != 3 && frame.weaponId != 7)
			{
				// Muzzle flash: a cross of additive quads.
				// Small star at the muzzle plus two plumes along the barrel axis.
				const float s = 0.012f + 0.010f * frame.flash, len = 0.07f * frame.flash;
				const float* c = md.flash;
				const float a0 = frame.flash * 0.8f;
				auto P = [&](float x, float y, float z) { return V3{ mz.x + ax.x*x + ay.x*y + az.x*z, mz.y + ax.y*x + ay.y*y + az.y*z, mz.z + ax.z*x + ay.z*y + az.z*z }; };
				addQuadAdd(P(0, -s, 0), P(s, 0, 0), P(0, s, 0), P(-s, 0, 0), c[0], c[1], c[2], a0, a0);
				addQuadAdd(P(-s * 0.7f, 0, 0.005f), P(s * 0.7f, 0, 0.005f), P(s * 0.1f, 0, -len), P(-s * 0.1f, 0, -len), c[0], c[1], c[2], a0, 0.0f);
				addQuadAdd(P(0, -s * 0.7f, 0.005f), P(0, s * 0.7f, 0.005f), P(0, s * 0.1f, -len), P(0, -s * 0.1f, -len), c[0], c[1], c[2], a0, 0.0f);
			}
			if (frame.laser && holding && frame.weaponId != 3 && frame.weaponId != 7)
			{
				const float w = 0.0012f, L = 3.0f;
				auto P = [&](float x, float y, float z) { return V3{ mz.x + ax.x*x + ay.x*y + az.x*z, mz.y + ax.y*x + ay.y*y + az.y*z, mz.z + ax.z*x + ay.z*y + az.z*z }; };
				const float* c = frame.laserColor;
				addQuadAdd(P(-w, 0, 0), P(w, 0, 0), P(w, 0, -L), P(-w, 0, -L), c[0], c[1], c[2], 0.55f, 0.0f);
				addQuadAdd(P(0, -w, 0), P(0, w, 0), P(0, w, -L), P(0, -w, -L), c[0], c[1], c[2], 0.55f, 0.0f);
			}
		}
	}
	if (frame.beam)
	{
		// Projector light cone (additive, fading toward the hologram).
		const V3 e = { frame.beamFrom.x, frame.beamFrom.y, frame.beamFrom.z };
		for (int i = 0; i < 4; i++)
		{
			const XrVector3f& p0 = frame.beamTo[i];
			const XrVector3f& p1 = frame.beamTo[(i + 1) & 3];
			const V3 a = { p0.x, p0.y, p0.z }, b = { p1.x, p1.y, p1.z };
			addQuadAdd(e, e, b, a, 0.25f, 0.6f, 0.9f, 0.40f, 0.05f);
			// Brighter edge rays from the emitter to each corner.
			const V3 d = { a.x - e.x, a.y - e.y, a.z - e.z };
			const float w = 0.0015f;
			V3 side = { d.z, 0.0f, -d.x };
			const float sl = sqrtf(side.x * side.x + side.z * side.z);
			if (sl > 1e-4f) { side.x *= w / sl; side.z *= w / sl; }
			const V3 e0 = { e.x - side.x, e.y, e.z - side.z }, e1 = { e.x + side.x, e.y, e.z + side.z };
			const V3 a0 = { a.x - side.x, a.y, a.z - side.z }, a1 = { a.x + side.x, a.y, a.z + side.z };
			addQuadAdd(e0, e1, a1, a0, 0.3f, 0.75f, 1.0f, 0.75f, 0.15f);
		}
		// Glowing emitter dot.
		const float r = 0.006f;
		addQuadAdd({ e.x - r, e.y, e.z - r }, { e.x + r, e.y, e.z - r }, { e.x + r, e.y, e.z + r }, { e.x - r, e.y, e.z + r }, 0.4f, 0.85f, 1.0f, 0.9f, 0.9f);
	}
	s_wheelDraws.clear();
	if (frame.wheel)
	{
		// Weapon wheel: a disc of light per weapon the player has, the weapon on it, and a
		// brighter, bigger slot under the hand.
		const M4 WM = fromPose(frame.wheelPose);
		const float R = 0.15f;
		for (int k = 0; k < 10; k++)
		{
			if (!(frame.wheelOwned & (1 << k))) continue;
			const float a = float(k) * 0.6283185f;
			const float sx = sinf(a) * R, sy = cosf(a) * R;
			const bool sel = (k == frame.wheelSel);
			const float rad = sel ? 0.052f : 0.040f;
			const V3 c = xform(WM, sx, sy, -0.01f);
			const V3 ax = { WM.m[0] * rad, WM.m[1] * rad, WM.m[2] * rad }, ay = { WM.m[4] * rad, WM.m[5] * rad, WM.m[6] * rad };
			for (int i = 0; i < 16; i++)
			{
				const float t0 = 6.2831853f * i / 16, t1 = 6.2831853f * (i + 1) / 16;
				const V3 p0 = { c.x + ax.x * cosf(t0) + ay.x * sinf(t0), c.y + ax.y * cosf(t0) + ay.y * sinf(t0), c.z + ax.z * cosf(t0) + ay.z * sinf(t0) };
				const V3 p1 = { c.x + ax.x * cosf(t1) + ay.x * sinf(t1), c.y + ax.y * cosf(t1) + ay.y * sinf(t1), c.z + ax.z * cosf(t1) + ay.z * sinf(t1) };
				if (sel) addQuadAdd(c, c, p1, p0, 0.35f, 0.75f, 1.0f, 0.95f, 0.30f);
				else addQuadAdd(c, c, p1, p0, 0.25f, 0.45f, 0.65f, 0.55f, 0.15f);
			}
			if (k > 0)
			{
				const float sc = sel ? 0.50f : 0.36f;
				M4 m = mul(WM, translate(sx, sy, 0.0f));
				m = mul(m, rotY(-1.5707963f));
				m = mul(m, scale(sc, sc, sc));
				m = mul(m, translate(0.0f, 0.04f, 0.10f));   // weapon center near the slot center
				// Every weapon the player has, as its light voxel model (keeps the frame rate).
				s_wheelDraws.push_back({ k, m });
			}
		}
	}
	if (frame.cycAlpha > 0.0f && hands[1].valid)
	{
		// Weapon strip beside the right hand, facing the eye: previous below, current in the middle,
		// next above (the stick direction for each), sliding to the new weapon on a press.
		M4 A, H, W;
		if (handFrames(hands[1], 1.0f, &A, &H, &W))
		{
			const V3 hp = { H.m[12], H.m[13], H.m[14] };
			const V3 ce = { hp.x, hp.y + 0.11f, hp.z };
			V3 z = { eyePose.position.x - ce.x, eyePose.position.y - ce.y, eyePose.position.z - ce.z };
			float zl = sqrtf(z.x * z.x + z.y * z.y + z.z * z.z); if (zl < 1e-4f) zl = 1.0f;
			z = { z.x / zl, z.y / zl, z.z / zl };
			V3 x = { z.z, 0.0f, -z.x }; float xl = sqrtf(x.x * x.x + x.z * x.z); if (xl < 1e-4f) { x = { 1, 0, 0 }; xl = 1.0f; }
			x = { x.x / xl, 0.0f, x.z / xl };
			const V3 y = { z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x };
			M4 P = ident();
			P.m[0] = x.x; P.m[1] = x.y; P.m[2] = x.z; P.m[4] = y.x; P.m[5] = y.y; P.m[6] = y.z; P.m[8] = z.x; P.m[9] = z.y; P.m[10] = z.z;
			P.m[12] = ce.x; P.m[13] = ce.y; P.m[14] = ce.z;
			const float S = 0.060f, al = frame.cycAlpha;
			for (int k = -2; k <= 2; k++)
			{
				// k: slot relative to the current weapon; the column starts shifted toward where the
				// new weapon came from and settles at 0.
				const float yy = (float(k) + frame.cycShift) * S;
				const float fade = al * fmaxf(0.0f, 1.0f - fabsf(yy) / (1.7f * S));
				if (fade <= 0.01f) continue;
				int id;
				if (k >= -1 && k <= 1) { id = frame.cycId[k + 1]; }
				else continue;
				const bool cur = (k == 0);
				const float rad = cur ? 0.030f + 0.008f * frame.cycPulse : 0.022f;
				const V3 c = xform(P, 0.0f, yy, -0.005f);
				const V3 ax = { P.m[0] * rad, P.m[1] * rad, P.m[2] * rad }, ay = { P.m[4] * rad, P.m[5] * rad, P.m[6] * rad };
				for (int i = 0; i < 16; i++)
				{
					const float t0 = 6.2831853f * i / 16, t1 = 6.2831853f * (i + 1) / 16;
					const V3 p0 = { c.x + ax.x * cosf(t0) + ay.x * sinf(t0), c.y + ax.y * cosf(t0) + ay.y * sinf(t0), c.z + ax.z * cosf(t0) + ay.z * sinf(t0) };
					const V3 p1 = { c.x + ax.x * cosf(t1) + ay.x * sinf(t1), c.y + ax.y * cosf(t1) + ay.y * sinf(t1), c.z + ax.z * cosf(t1) + ay.z * sinf(t1) };
					if (cur) addQuadAdd(c, c, p1, p0, 0.35f, 0.75f, 1.0f, 0.90f * fade, 0.30f * fade);
					else addQuadAdd(c, c, p1, p0, 0.25f, 0.45f, 0.65f, 0.50f * fade, 0.12f * fade);
				}
				if (!cur)
				{
					// Small arrow on the far side: up for the next weapon, down for the previous one.
					const float s2 = 0.008f, dir = (k > 0) ? 1.0f : -1.0f;
					const V3 tip = xform(P, 0.0f, yy + dir * (rad + 0.012f), -0.004f);
					const V3 b0 = xform(P, -s2, yy + dir * (rad + 0.004f), -0.004f), b1 = xform(P, s2, yy + dir * (rad + 0.004f), -0.004f);
					addQuadAdd(b0, b1, tip, tip, 1.0f, 0.85f, 0.3f, 0.8f * fade, 0.8f * fade);
				}
				if (id > 0 && fade > 0.2f)
				{
					const float sc = cur ? 0.30f + 0.06f * frame.cycPulse : 0.20f;
					M4 m = mul(P, translate(0.0f, yy, 0.0f));
					m = mul(m, rotY(-1.5707963f));
					m = mul(m, scale(sc, sc, sc));
					m = mul(m, translate(0.0f, 0.04f, 0.10f));
					s_wheelDraws.push_back({ id, m });
				}
			}
		}
	}
	s_nHand = (GLsizei)s_vHand.size(); s_nSolid = (GLsizei)s_vSolid.size(); s_nAdd = (GLsizei)s_vAdd.size();
	const size_t total = size_t(s_nHand + s_nSolid + s_nAdd);
	glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
	glBufferData(GL_ARRAY_BUFFER, total * sizeof(Vtx), nullptr, GL_STREAM_DRAW);
	if (s_nHand) glBufferSubData(GL_ARRAY_BUFFER, 0, s_nHand * sizeof(Vtx), s_vHand.data());
	if (s_nSolid) glBufferSubData(GL_ARRAY_BUFFER, s_nHand * sizeof(Vtx), s_nSolid * sizeof(Vtx), s_vSolid.data());
	if (s_nAdd) glBufferSubData(GL_ARRAY_BUFFER, (s_nHand + s_nSolid) * sizeof(Vtx), s_nAdd * sizeof(Vtx), s_vAdd.data());
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	s_built = true;
	}
	if (s_nHand + s_nSolid + s_nAdd == 0 && s_drawWeapon < 0 && s_segDraws.empty() && s_sDraws.empty() && s_wheelDraws.empty()) { return; }

	// Same depth mapping as the world (near 0.01, far 4096 world units).
	const float zn = 0.01f / unitsPerMeter, zf = 4096.0f / unitsPerMeter;
	const float l = tanf(fov.angleLeft), r = tanf(fov.angleRight), u = tanf(fov.angleUp), d = tanf(fov.angleDown);
	M4 proj = {};
	proj.m[0] = 2.0f / (r - l);
	proj.m[5] = 2.0f / (u - d);
	proj.m[8] = (r + l) / (r - l);
	proj.m[9] = (u + d) / (u - d);
	proj.m[10] = zf / (zn - zf);
	proj.m[11] = -1.0f;
	proj.m[14] = zn * zf / (zn - zf);
	const M4 mvp = mul(proj, inverseRigid(fromPose(eyePose)));

	glUseProgram(s_prog);
	glUniformMatrix4fv(s_locMvp, 1, GL_FALSE, mvp.m);
	glUniform1f(s_locLight, frame.light);
	glActiveTexture(GL_TEXTURE0);
	glBindSampler(0, 0);
	glUniform1i(s_locTex, 0);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glBindVertexArray(s_vao);
	glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
	drawRange(0, s_nHand, s_flat);
	drawRange(s_nHand, s_nSolid, s_flat);
	// Hand segments: static meshes, one matrix each.
	if (!s_segDraws.empty())
	{
		glBindTexture(GL_TEXTURE_2D, s_flat);
		for (const SegDraw& sd : s_segDraws)
		{
			SegMesh& sm = s_segMeshes[sd.mesh];
			if (sm.local.empty()) { continue; }
			if (!sm.vbo)
			{
				glGenBuffers(1, &sm.vbo);
				glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
				glBufferData(GL_ARRAY_BUFFER, sm.local.size() * sizeof(Vtx), sm.local.data(), GL_STATIC_DRAW);
			}
			glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(3 * sizeof(float)));
			glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(5 * sizeof(float)));
			glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(9 * sizeof(float)));
			const M4 mvpS = mul(mvp, sd.m);
			glUniformMatrix4fv(s_locMvp, 1, GL_FALSE, mvpS.m);
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)sm.local.size());
		}
		glUniformMatrix4fv(s_locMvp, 1, GL_FALSE, mvp.m);
		glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(3 * sizeof(float)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(5 * sizeof(float)));
		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(9 * sizeof(float)));
	}
	// Weapon in the chosen style: smooth (high resolution), voxel, or fine voxel (3x the voxels).
	WeaponSlot* wslot = nullptr;
	const bool dfmWeapon = s_drawWeapon > 0 && dfmHas(s_drawWeapon);
	if (s_drawWeapon > 0 && s_weaponStyle != 1 && !dfmWeapon) { wslot = weaponSlot(s_weaponStyle == 2 ? 1 : 0, s_drawWeapon); }
	if (wslot && s_weaponStyle == 2 && wslot->fineVbo)
	{
		glBindBuffer(GL_ARRAY_BUFFER, wslot->fineVbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(3 * sizeof(float)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(5 * sizeof(float)));
		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(9 * sizeof(float)));
		const M4 mvpW = mul(mvp, s_drawWeaponW);
		glUniformMatrix4fv(s_locMvp, 1, GL_FALSE, mvpW.m);
		glBindTexture(GL_TEXTURE_2D, s_flat);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)wslot->fine.size());
		glUniformMatrix4fv(s_locMvp, 1, GL_FALSE, mvp.m);
		glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(3 * sizeof(float)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(5 * sizeof(float)));
		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(9 * sizeof(float)));
	}
	else if (wslot && (s_weaponStyle == 0 || s_weaponStyle >= 3) && wslot->smooth.vao)
	{
		s_sDraws.push_back({ &wslot->smooth, s_drawWeaponW });
	}
	else if (s_drawWeapon > 0 && !dfmWeapon)
	{
		VoxWeapon& vw = s_voxW[s_drawWeapon];
		if (!vw.vbo)
		{
			glGenBuffers(1, &vw.vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vw.vbo);
			glBufferData(GL_ARRAY_BUFFER, vw.local.size() * sizeof(Vtx), vw.local.data(), GL_STATIC_DRAW);
		}
		glBindBuffer(GL_ARRAY_BUFFER, vw.vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(3 * sizeof(float)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(5 * sizeof(float)));
		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(9 * sizeof(float)));
		const M4 mvpW = mul(mvp, s_drawWeaponW);
		glUniformMatrix4fv(s_locMvp, 1, GL_FALSE, mvpW.m);
		glBindTexture(GL_TEXTURE_2D, s_flat);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vw.local.size());
		glUniformMatrix4fv(s_locMvp, 1, GL_FALSE, mvp.m);
		glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(3 * sizeof(float)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(5 * sizeof(float)));
		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(9 * sizeof(float)));
	}
	// Weapon wheel icons: the voxel weapons, small.
	for (const WheelDraw& wd : s_wheelDraws)
	{
		VoxWeapon& vw = s_voxW[wd.id];
		if (!vw.valid || vw.local.empty()) continue;
		if (!vw.vbo)
		{
			glGenBuffers(1, &vw.vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vw.vbo);
			glBufferData(GL_ARRAY_BUFFER, vw.local.size() * sizeof(Vtx), vw.local.data(), GL_STATIC_DRAW);
		}
		glBindBuffer(GL_ARRAY_BUFFER, vw.vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(3 * sizeof(float)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(5 * sizeof(float)));
		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(9 * sizeof(float)));
		const M4 mvpW = mul(mvp, wd.m);
		glUniformMatrix4fv(s_locMvp, 1, GL_FALSE, mvpW.m);
		glUniform1f(s_locLight, 1.0f);
		glBindTexture(GL_TEXTURE_2D, s_flat);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vw.local.size());
	}
	if (!s_wheelDraws.empty())
	{
		glUniform1f(s_locLight, frame.light);
		glUniformMatrix4fv(s_locMvp, 1, GL_FALSE, mvp.m);
		glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(3 * sizeof(float)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(5 * sizeof(float)));
		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(9 * sizeof(float)));
	}
	if (s_nAdd > 0)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		glDepthMask(GL_FALSE);
		drawRange(s_nHand + s_nSolid, s_nAdd, s_white);
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	// Smooth pieces (hands, high resolution weapon): lit with normals.
	if (!s_sDraws.empty() && s_progS)
	{
		glUseProgram(s_progS);
		glUniform1f(s_locSLight, frame.light);
		glUniform3f(s_locSEye, eyePose.position.x, eyePose.position.y, eyePose.position.z);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
		glUniform1i(s_locSSkin, 0);
		for (SDraw& d : s_sDraws)
		{
			if (d.skin >= 0)
			{
				if (!s_skVao) { uploadSkinMesh(); }
				const M4 mvpK = mul(mvp, d.m);
				glUniformMatrix4fv(s_locSMvp, 1, GL_FALSE, mvpK.m);
				glUniformMatrix4fv(s_locSModel, 1, GL_FALSE, d.m.m);
				glUniformMatrix4fv(s_locSBones, handskin_jointCount(), GL_FALSE, &s_bones[d.skin][0][0]);
				glUniform1i(s_locSSkin, 1);
				glBindVertexArray(s_skVao);
				glDrawElements(GL_TRIANGLES, s_skCount, GL_UNSIGNED_SHORT, nullptr);
				glUniform1i(s_locSSkin, 0);
				continue;
			}
			uploadSMesh(*d.mesh);
			if (!d.mesh->vao) { continue; }
			const M4 mvpS = mul(mvp, d.m);
			glUniformMatrix4fv(s_locSMvp, 1, GL_FALSE, mvpS.m);
			glUniformMatrix4fv(s_locSModel, 1, GL_FALSE, d.m.m);
			glBindVertexArray(d.mesh->vao);
			glDrawElements(GL_TRIANGLES, d.mesh->count, GL_UNSIGNED_INT, nullptr);
		}
		glBindVertexArray(0);
	}
#ifndef HANDS_HOST_TEST
	if (dfmWeapon) { dfmDraw(s_drawWeapon, mvp, s_drawWeaponW, eyePose, frame.light); }
	if (s_bezelDraw && (s_pdaLiveOk || s_bezelTex[s_pdaMode][0].w))
	{
		if (s_bezelBuiltScale != s_pdaScale) { for (DfmSub& sb : s_bezelSub) { if (sb.vao) { glDeleteVertexArrays(1, &sb.vao); glDeleteBuffers(1, &sb.vbo); glDeleteBuffers(1, &sb.ibo); } } buildBezel(); }
		texMeshDraw(s_bezelSub, (s_pdaLiveOk && !s_pdaFit) ? s_liveTex : s_bezelTex[s_pdaMode], mvp, s_bezelM, eyePose, frame.light, 0.0f);
	}
#endif
	glUseProgram(0);
}
