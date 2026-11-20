// High resolution weapons ("ALTA RESOLUÇÃO"): signed distance models of the Dark Forces arsenal,
// sampled on a fine grid and meshed with surface nets. Every part is a rounded solid (bevels,
// fillets with smooth unions), with carved details (vents, bores, slots, grooves, screws) and
// procedural materials (brushed steel, worn paint, wood grain, rubber, hazard stripes, lights),
// plus baked ambient occlusion from the distance field.
// Frame and units as the voxel models (tfe_xr_weapons.cpp): millimeters, x right, y up, forward -z,
// grip point (center of the fist) at the origin; same scale per weapon and same muzzle points.
// Star Wars references:
//  - Bryar pistol (Kyle Katarn): Mauser C96 "broomhandle" base: box magazine ahead of the trigger,
//    ring hammer, grooved grip, tangent sight, long barrel with a cooling coil and conical flash hider,
//    top scope.
//  - E-11 blaster rifle: Sterling L2A3 base: tube receiver, perforated barrel jacket with the barrel
//    inside, side power pack, Hengstler counter, T-tracks, M38 style scope, folding stock.
//  - Class-A thermal detonator: sphere, raised equator rings, recessed sockets, arming plate with
//    lights and the slider, bottom plunger.
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include "tfe_xr_weapons.h"
#include "tfe_xr_mesh.h"

namespace
{
	// ------------------------------------------------------------------ materials
	enum Pattern { PT_PLAIN, PT_METAL, PT_PAINT, PT_WOOD, PT_RUBBER, PT_STRIPES, PT_LIGHT, PT_KNURL };
	struct Mat { float r, g, b, spec; int pattern; float p0, p1; uint32_t alt; };

	Mat rgbMat(uint32_t c, float spec, int pattern, uint32_t alt = 0, float p0 = 0.0f, float p1 = 0.0f)
	{
		Mat m;
		m.r = ((c >> 16) & 255) / 255.0f; m.g = ((c >> 8) & 255) / 255.0f; m.b = (c & 255) / 255.0f;
		m.spec = spec; m.pattern = pattern; m.p0 = p0; m.p1 = p1; m.alt = alt;
		return m;
	}

	// ------------------------------------------------------------------ primitives
	enum PrimType { PR_BOX, PR_CYL, PR_CONE, PR_SPHERE, PR_TORUS, PR_ELLIPSOID };
	enum PrimOp { OP_ADD, OP_SMOOTH, OP_SUB, OP_SMOOTHSUB, OP_PAINT };
	struct Prim
	{
		int type, op;
		float k;            // smooth radius (mm)
		float R[9];         // world -> local rotation (rows)
		float c[3];         // center (mm)
		float p[4];         // box: hx hy hz round; cyl (axis z): r hz round; cone: r0 (z=-h) r1 (z=+h) h; sphere: r; torus (axis z): R r; ellipsoid: rx ry rz
		int mat;
		float bmin[3], bmax[3];
	};

	inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
	inline float len2(float x, float y) { return sqrtf(x * x + y * y); }
	inline float len3(float x, float y, float z) { return sqrtf(x * x + y * y + z * z); }

	float primDist(const Prim& s, float wx, float wy, float wz)
	{
		const float dx = wx - s.c[0], dy = wy - s.c[1], dz = wz - s.c[2];
		const float x = s.R[0] * dx + s.R[1] * dy + s.R[2] * dz;
		const float y = s.R[3] * dx + s.R[4] * dy + s.R[5] * dz;
		const float z = s.R[6] * dx + s.R[7] * dy + s.R[8] * dz;
		switch (s.type)
		{
		case PR_BOX:
		{
			const float r = s.p[3];
			const float qx = fabsf(x) - (s.p[0] - r), qy = fabsf(y) - (s.p[1] - r), qz = fabsf(z) - (s.p[2] - r);
			return len3(fmaxf(qx, 0.0f), fmaxf(qy, 0.0f), fmaxf(qz, 0.0f)) + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f) - r;
		}
		case PR_CYL:
		{
			const float r = s.p[2];
			const float ax = len2(x, y) - (s.p[0] - r), az = fabsf(z) - (s.p[1] - r);
			return len2(fmaxf(ax, 0.0f), fmaxf(az, 0.0f)) + fminf(fmaxf(ax, az), 0.0f) - r;
		}
		case PR_CONE:
		{
			// Capped cone along z: radius r0 at z = -h, r1 at z = +h (Inigo Quilez).
			const float r1 = s.p[0], r2 = s.p[1], h = s.p[2];
			const float qx = len2(x, y), qy = z;
			const float k1x = r2, k1y = h, k2x = r2 - r1, k2y = 2.0f * h;
			const float cax = qx - fminf(qx, qy < 0.0f ? r1 : r2), cay = fabsf(qy) - h;
			const float t = clampf(((k1x - qx) * k2x + (k1y - qy) * k2y) / (k2x * k2x + k2y * k2y), 0.0f, 1.0f);
			const float cbx = qx - k1x + k2x * t, cby = qy - k1y + k2y * t;
			const float sgn = (cbx < 0.0f && cay < 0.0f) ? -1.0f : 1.0f;
			return sgn * sqrtf(fminf(cax * cax + cay * cay, cbx * cbx + cby * cby));
		}
		case PR_SPHERE: return len3(x, y, z) - s.p[0];
		case PR_TORUS: return len2(len2(x, y) - s.p[0], z) - s.p[1];
		case PR_ELLIPSOID:
		{
			const float k0 = len3(x / s.p[0], y / s.p[1], z / s.p[2]);
			const float k1 = len3(x / (s.p[0] * s.p[0]), y / (s.p[1] * s.p[1]), z / (s.p[2] * s.p[2]));
			return k1 > 1e-6f ? k0 * (k0 - 1.0f) / k1 : -fminf(s.p[0], fminf(s.p[1], s.p[2]));
		}
		}
		return 1e9f;
	}

	// Local half extents (for the bounding box).
	void primExtent(const Prim& s, float e[3])
	{
		switch (s.type)
		{
		case PR_BOX: e[0] = s.p[0]; e[1] = s.p[1]; e[2] = s.p[2]; break;
		case PR_CYL: e[0] = e[1] = s.p[0]; e[2] = s.p[1]; break;
		case PR_CONE: e[0] = e[1] = fmaxf(s.p[0], s.p[1]); e[2] = s.p[2]; break;
		case PR_SPHERE: e[0] = e[1] = e[2] = s.p[0]; break;
		case PR_TORUS: e[0] = e[1] = s.p[0] + s.p[1]; e[2] = s.p[1]; break;
		case PR_ELLIPSOID: e[0] = s.p[0]; e[1] = s.p[1]; e[2] = s.p[2]; break;
		}
	}

	struct Model
	{
		std::vector<Prim> prims;
		std::vector<Mat> mats;
		float k = 1.0f;                       // weapon scale (the grip keeps its real size)
		float rot[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };   // current local rotation (local -> world)
		int op = OP_ADD;
		float smooth = 0.0f;

		int mat(const Mat& m) { mats.push_back(m); return int(mats.size()) - 1; }

		// Rotation for the next primitives: about X, then Y, then Z (radians), local -> world.
		void setRot(float ax, float ay = 0.0f, float az = 0.0f)
		{
			const float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
			const float X[9] = { 1, 0, 0, 0, cx, -sx, 0, sx, cx };
			const float Y[9] = { cy, 0, sy, 0, 1, 0, -sy, 0, cy };
			const float Z[9] = { cz, -sz, 0, sz, cz, 0, 0, 0, 1 };
			float t[9];
			mul(Z, Y, t); mul(t, X, rot);
		}
		void noRot() { setRot(0.0f); }
		static void mul(const float* a, const float* b, float* o)
		{
			for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { o[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j]; }
		}

		Prim& push(int type, float cx, float cy, float cz, int m, bool scaled = true)
		{
			const float s = scaled ? k : 1.0f;
			Prim p;
			memset(&p, 0, sizeof(p));
			p.type = type; p.op = op; p.k = smooth * s; p.mat = m;
			p.c[0] = cx * s; p.c[1] = cy * s; p.c[2] = cz * s;
			// world -> local = transpose of local -> world
			for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { p.R[i * 3 + j] = rot[j * 3 + i]; }
			prims.push_back(p);
			return prims.back();
		}
		void finish(Prim& p, bool scaled = true)
		{
			const float s = scaled ? k : 1.0f;
			for (int i = 0; i < 4; i++) { p.p[i] *= s; }
		}

		// --- shape helpers (mm, before the weapon scale) ---
		void box(float cx, float cy, float cz, float hx, float hy, float hz, float round, int m, bool sc = true)
		{
			Prim& p = push(PR_BOX, cx, cy, cz, m, sc);
			p.p[0] = hx; p.p[1] = hy; p.p[2] = hz; p.p[3] = fminf(round, fminf(hx, fminf(hy, hz)) * 0.999f);
			finish(p, sc);
		}
		void boxMM(float x0, float y0, float z0, float x1, float y1, float z1, float round, int m)
		{
			box((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f, fabsf(x1 - x0) * 0.5f, fabsf(y1 - y0) * 0.5f, fabsf(z1 - z0) * 0.5f, round, m);
		}
		// Cylinder along the current local z (with noRot: world z), from z0 to z1 through (cx, cy).
		void cyl(float cx, float cy, float cz, float r, float hz, float round, int m, bool sc = true)
		{
			Prim& p = push(PR_CYL, cx, cy, cz, m, sc);
			p.p[0] = r; p.p[1] = hz; p.p[2] = fminf(round, fminf(r, hz) * 0.999f);
			finish(p, sc);
		}
		void cylZ(float cx, float cy, float z0, float z1, float r, float round, int m)
		{
			noRot(); cyl(cx, cy, (z0 + z1) * 0.5f, r, fabsf(z1 - z0) * 0.5f, round, m);
		}
		void cylX(float cx, float cy, float cz, float r, float hx, float round, int m)
		{
			setRot(0.0f, 1.5707963f); cyl(cx, cy, cz, r, hx, round, m); noRot();
		}
		void cylY(float cx, float cy, float cz, float r, float hy, float round, int m)
		{
			setRot(1.5707963f); cyl(cx, cy, cz, r, hy, round, m); noRot();
		}
		// Cone along world z: radius rA at zA, rB at zB.
		void coneZ(float cx, float cy, float zA, float zB, float rA, float rB, int m)
		{
			noRot();
			Prim& p = push(PR_CONE, cx, cy, (zA + zB) * 0.5f, m);
			// local z = -h at the smaller world z
			if (zA < zB) { p.p[0] = rA; p.p[1] = rB; } else { p.p[0] = rB; p.p[1] = rA; }
			p.p[2] = fabsf(zB - zA) * 0.5f;
			finish(p);
		}
		void sphere(float cx, float cy, float cz, float r, int m)
		{
			Prim& p = push(PR_SPHERE, cx, cy, cz, m);
			p.p[0] = r; finish(p);
		}
		void torusZ(float cx, float cy, float cz, float R, float r, int m)
		{
			noRot(); Prim& p = push(PR_TORUS, cx, cy, cz, m); p.p[0] = R; p.p[1] = r; finish(p);
		}
		void torus(float cx, float cy, float cz, float R, float r, int m)
		{
			Prim& p = push(PR_TORUS, cx, cy, cz, m); p.p[0] = R; p.p[1] = r; finish(p);
		}
		void ellipsoid(float cx, float cy, float cz, float rx, float ry, float rz, int m)
		{
			Prim& p = push(PR_ELLIPSOID, cx, cy, cz, m); p.p[0] = rx; p.p[1] = ry; p.p[2] = rz; finish(p);
		}

		// Modes for the following primitives.
		void add() { op = OP_ADD; smooth = 0.0f; }
		void blend(float r) { op = OP_SMOOTH; smooth = r; }
		void cut() { op = OP_SUB; smooth = 0.0f; }
		void cutSmooth(float r) { op = OP_SMOOTHSUB; smooth = r; }
		void paint() { op = OP_PAINT; smooth = 0.0f; }
	};

	// ------------------------------------------------------------------ noise
	inline uint32_t hash3i(int a, int b, int c)
	{
		uint32_t h = 2166136261u;
		h = (h ^ uint32_t(a)) * 16777619u; h = (h ^ uint32_t(b)) * 16777619u; h = (h ^ uint32_t(c)) * 16777619u;
		h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
		return h;
	}
	inline float h01(int a, int b, int c) { return (hash3i(a, b, c) & 0xffffff) / float(0xffffff); }
	float vnoise(float x, float y, float z)
	{
		const int ix = int(floorf(x)), iy = int(floorf(y)), iz = int(floorf(z));
		float fx = x - ix, fy = y - iy, fz = z - iz;
		fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
		float v[8];
		for (int i = 0; i < 8; i++) { v[i] = h01(ix + (i & 1), iy + ((i >> 1) & 1), iz + (i >> 2)); }
		const float a = v[0] + (v[1] - v[0]) * fx, b = v[2] + (v[3] - v[2]) * fx, c = v[4] + (v[5] - v[4]) * fx, d = v[6] + (v[7] - v[6]) * fx;
		const float e = a + (b - a) * fy, f = c + (d - c) * fy;
		return e + (f - e) * fz;
	}
	float fbm(float x, float y, float z)
	{
		return vnoise(x, y, z) * 0.55f + vnoise(x * 2.1f, y * 2.1f, z * 2.1f) * 0.3f + vnoise(x * 4.3f, y * 4.3f, z * 4.3f) * 0.15f;
	}

	// ------------------------------------------------------------------ colors
	const uint32_t BLACK = 0x15171d, DARK = 0x1e2732, GUN = 0x343b4e, STEEL = 0x4c5368, SILVER = 0x8c93aa, CHROME = 0xd6dae6;
	const uint32_t GOLD = 0xc49a3a, BRASS = 0x9c7a32, WOOD = 0x5a3620, WOODD = 0x3a2416, RED = 0xd02a1e, ORANGE = 0xf08a1e;
	const uint32_t GREEN = 0x3cff6a, BLUE = 0x4aa0ff, WHITE = 0xe8e8e8, RUST = 0x4a2614;

	struct Mats
	{
		int black, dark, gun, steel, silver, chrome, gold, brass, wood, woodd, rubber, red, redLight, greenLight, blueLens,
			amberLight, orange, white, rust, knurl, bore;
	};
	Mats stdMats(Model& m)
	{
		Mats s;
		s.black = m.mat(rgbMat(BLACK, 0.45f, PT_PAINT, 0x5a6070));
		s.dark = m.mat(rgbMat(DARK, 0.45f, PT_PAINT, 0x6a7080));
		s.gun = m.mat(rgbMat(GUN, 0.55f, PT_METAL));
		s.steel = m.mat(rgbMat(STEEL, 0.7f, PT_METAL));
		s.silver = m.mat(rgbMat(SILVER, 0.8f, PT_METAL));
		s.chrome = m.mat(rgbMat(CHROME, 1.0f, PT_METAL));
		s.gold = m.mat(rgbMat(GOLD, 0.85f, PT_METAL));
		s.brass = m.mat(rgbMat(BRASS, 0.7f, PT_METAL));
		s.wood = m.mat(rgbMat(WOOD, 0.3f, PT_WOOD, WOODD, 1.0f));
		s.woodd = m.mat(rgbMat(WOODD, 0.3f, PT_WOOD, 0x24160c, 1.0f));
		s.rubber = m.mat(rgbMat(0x1a1b1f, 0.12f, PT_RUBBER));
		s.red = m.mat(rgbMat(RED, 0.4f, PT_PAINT, 0x802018));
		s.redLight = m.mat(rgbMat(0xff3020, 0.2f, PT_LIGHT));
		s.greenLight = m.mat(rgbMat(GREEN, 0.2f, PT_LIGHT));
		s.blueLens = m.mat(rgbMat(BLUE, 1.0f, PT_LIGHT));
		s.amberLight = m.mat(rgbMat(0xffc040, 0.2f, PT_LIGHT));
		s.orange = m.mat(rgbMat(ORANGE, 0.35f, PT_PAINT, 0x804010));
		s.white = m.mat(rgbMat(WHITE, 0.3f, PT_PAINT, 0x9098a0));
		s.rust = m.mat(rgbMat(RUST, 0.25f, PT_PAINT, 0x6a3a20));
		s.knurl = m.mat(rgbMat(STEEL, 0.6f, PT_KNURL));
		s.bore = m.mat(rgbMat(0x08090c, 0.1f, PT_PLAIN));
		return s;
	}

	// ------------------------------------------------------------------ shared parts
	// Pistol grip (real size: it fits the fist). Grooved/stippled, tilted back.
	void grip(Model& m, int body, int ribMat, float tilt, int style)
	{
		const float saved = m.k;
		m.k = 1.0f;
		if (saved > 1.0f)
		{
			// Upper block that reaches the (scaled) receiver.
			m.blend(3.0f); m.noRot();
			m.box(0.0f, 30.0f * saved * 0.5f + 18.0f, 4.0f, 12.0f, 30.0f * saved * 0.5f, 15.5f, 3.0f, body, false);
		}
		m.blend(4.0f);
		m.setRot(tilt);
		m.box(0.0f, -8.0f, 8.0f, 11.5f, 44.0f, 16.5f, 6.0f, body, false);
		if (style == 0)
		{
			// Horizontal grooves (broomhandle).
			for (int i = 0; i < 12; i++)
			{
				const float y = -44.0f + i * 6.5f;
				const float c = cosf(tilt), s = sinf(tilt);
				m.op = OP_SMOOTHSUB; m.smooth = 0.6f;
				m.box(0.0f, -8.0f + (y + 8.0f) * c, 8.0f + (y + 8.0f) * s, 14.0f, 1.1f, 19.0f, 0.5f, ribMat, false);
			}
			// Core so the grooves only cut 1.5 mm deep.
			m.blend(0.8f);
			m.box(0.0f, -8.0f, 8.0f, 10.0f, 42.0f, 15.0f, 5.0f, ribMat, false);
		}
		else
		{
			// Rubber side panels with a stipple texture, finger grooves on the front.
			m.add();
			m.box(0.0f, -10.0f, 8.0f, 12.2f, 34.0f, 13.0f, 5.0f, ribMat, false);
			for (int i = 0; i < 3; i++)
			{
				m.cutSmooth(2.0f);
				m.setRot(0.0f, 1.5707963f);
				const float yy = -30.0f + i * 15.0f;
				const float c = cosf(tilt), s = sinf(tilt);
				m.cyl(0.0f, -8.0f + (yy + 8.0f) * c, 8.0f + (yy + 8.0f) * s - 18.0f, 5.0f, 14.0f, 0.5f, body, false);
			}
		}
		m.noRot();
		m.k = saved;
		m.add();
	}

	void triggerGuard(Model& m, float yTop, float zFront, float zBack, int mat, int trig)
	{
		m.blend(2.0f);
		m.boxMM(-3.0f, yTop - 26.0f, zFront, 3.0f, yTop - 21.0f, zBack, 2.0f, mat);
		m.boxMM(-3.0f, yTop - 26.0f, zFront - 5.0f, 3.0f, yTop, zFront, 2.0f, mat);
		m.add();
		// Curved trigger blade.
		m.setRot(0.25f);
		m.box(0.0f, yTop - 9.0f, zFront + 12.0f, 2.0f, 8.0f, 2.2f, 1.5f, trig);
		m.setRot(-0.2f);
		m.box(0.0f, yTop - 15.0f, zFront + 13.5f, 2.0f, 3.0f, 2.0f, 1.5f, trig);
		m.noRot();
	}

	// Slotted screw head on a side (+x or -x).
	void screwX(Model& m, float x, float y, float z, float side, int mat)
	{
		m.add();
		m.cylX(x + side * 0.6f, y, z, 2.2f, 0.8f, 0.6f, mat);
		m.cut();
		m.boxMM(x + side * 0.6f, y - 0.4f, z - 2.0f, x + side * 2.0f, y + 0.4f, z + 2.0f, 0.0f, mat);
		m.add();
	}

	// Ring of radial holes on a tube along z (axis at cx, cy).
	void radialHoles(Model& m, float cx, float cy, float z, float R, int count, float a0, float holeR, float depth, int mat)
	{
		for (int i = 0; i < count; i++)
		{
			const float a = a0 + i * 6.2831853f / count;
			m.cutSmooth(0.6f);
			// cylinder axis along the radius: local z -> (cos a, sin a, 0)
			m.setRot(0.0f, 1.5707963f, a);
			m.cyl(cx + cosf(a) * R, cy + sinf(a) * R, z, holeR, depth, 0.3f, mat);
		}
		m.noRot(); m.add();
	}

	// ------------------------------------------------------------------ weapons
	void buildBryar(Model& m, const Mats& M, float muzzle[3])
	{
		const float by = 46.0f;
		// Receiver: C96 box action with milled side panels.
		m.add();
		m.box(0.0f, 43.0f, -10.0f, 10.5f, 19.0f, 50.0f, 2.5f, M.black);
		m.blend(1.5f);
		m.box(0.0f, 44.0f, -20.0f, 11.6f, 10.5f, 30.0f, 1.5f, M.dark);              // side plates
		for (int s = -1; s <= 1; s += 2)
		{
			m.cutSmooth(0.8f);
			m.box(s * 12.4f, 44.0f, -22.0f, 1.4f, 6.0f, 22.0f, 1.2f, M.gun);           // milled panel
			screwX(m, s * 11.6f, 50.0f, 2.0f, float(s), M.silver);
			screwX(m, s * 11.6f, 38.0f, -46.0f, float(s), M.silver);
		}
		// Top rib and tangent rear sight.
		m.blend(1.0f);
		m.boxMM(-3.0f, 61.0f, -58.0f, 3.0f, 64.0f, 8.0f, 1.0f, M.gun);
		m.add();
		m.boxMM(-5.0f, 61.0f, 12.0f, 5.0f, 67.0f, 0.0f, 1.0f, M.steel);
		m.cut();
		m.boxMM(-1.2f, 64.0f, 13.0f, 1.2f, 68.0f, -1.0f, 0.0f, M.steel);
		// Ring hammer at the back.
		m.add();
		m.boxMM(-4.0f, 56.0f, 42.0f, 4.0f, 63.0f, 30.0f, 1.5f, M.steel);
		m.setRot(0.0f, 1.5707963f);
		m.torus(0.0f, 68.0f, 40.0f, 6.0f, 2.0f, M.steel);
		m.noRot();
		// Box magazine ahead of the trigger guard, with floor plate.
		m.blend(1.5f);
		m.box(0.0f, 9.0f, -35.0f, 8.5f, 16.0f, 17.0f, 2.0f, M.black);
		m.add();
		m.box(0.0f, -8.5f, -35.0f, 10.0f, 2.5f, 19.0f, 1.5f, M.steel);
		for (int i = 0; i < 3; i++) { m.cutSmooth(0.5f); m.box(9.2f, 2.0f + i * 7.0f, -35.0f, 1.0f, 1.2f, 13.0f, 0.5f, M.gun); m.box(-9.2f, 2.0f + i * 7.0f, -35.0f, 1.0f, 1.2f, 13.0f, 0.5f, M.gun); }
		m.add();
		triggerGuard(m, 24.0f, -14.0f, 16.0f, M.black, M.silver);
		// Barrel with the gold cooling coil.
		m.add();
		m.cylZ(0.0f, by, -58.0f, -162.0f, 7.5f, 0.8f, M.black);
		for (int i = 0; i < 12; i++)
		{
			const float z = -68.0f - i * 6.5f;
			m.setRot(0.10f, 0.0f, 0.0f);
			m.torus(0.0f, by, z, 9.6f, 2.0f, M.gold);
		}
		m.noRot();
		m.cylZ(0.0f, by, -62.0f, -66.0f, 11.0f, 1.0f, M.gun);                        // coil collars
		m.cylZ(0.0f, by, -146.0f, -150.0f, 11.0f, 1.0f, M.gun);
		// Conical flash hider with slots and the bore.
		m.cylZ(0.0f, by, -160.0f, -166.0f, 9.5f, 1.0f, M.steel);
		m.blend(1.0f);
		m.coneZ(0.0f, by, -166.0f, -186.0f, 9.5f, 12.5f, M.gun);
		for (int i = 0; i < 6; i++)
		{
			const float a = i * 1.0471976f + 0.52f;
			m.cutSmooth(0.5f);
			m.setRot(0.0f, 1.5707963f, a);
			m.box(cosf(a) * 11.5f, by + sinf(a) * 11.5f, -178.0f, 4.0f, 1.4f, 6.0f, 1.0f, M.bore);
		}
		m.noRot();
		m.cutSmooth(0.6f);
		m.cylZ(0.0f, by, -150.0f, -190.0f, 5.0f, 0.0f, M.bore);
		m.add();
		// Front sight.
		m.boxMM(-1.0f, by + 7.0f, -150.0f, 1.0f, by + 13.0f, -156.0f, 0.6f, M.steel);
		// Scope: tube, bells, turrets, rings, blue lens.
		const float sy = by + 26.0f;
		m.cylZ(0.0f, sy, -14.0f, -92.0f, 6.5f, 0.6f, M.black);
		m.blend(1.0f);
		m.coneZ(0.0f, sy, -86.0f, -100.0f, 6.5f, 10.0f, M.black);
		m.coneZ(0.0f, sy, -20.0f, -8.0f, 6.5f, 8.5f, M.black);
		m.add();
		m.cylZ(0.0f, sy, -100.0f, -104.0f, 10.0f, 1.0f, M.gun);
		m.cylZ(0.0f, sy, -4.0f, -8.0f, 8.5f, 1.0f, M.gun);
		m.cutSmooth(0.8f);
		m.cylZ(0.0f, sy, -101.0f, -106.0f, 7.5f, 0.0f, M.blueLens);
		m.cylZ(0.0f, sy, -2.0f, -6.0f, 6.0f, 0.0f, M.blueLens);
		m.add();
		m.cylZ(0.0f, sy, -102.0f, -103.0f, 7.6f, 0.0f, M.blueLens);
		m.cylZ(0.0f, sy, -4.5f, -5.0f, 6.1f, 0.0f, M.blueLens);
		m.cylY(0.0f, sy + 8.0f, -50.0f, 4.0f, 3.5f, 0.8f, M.knurl);                   // turrets
		m.cylX(7.5f, sy, -50.0f, 4.0f, 3.0f, 0.8f, M.knurl);
		for (int i = 0; i < 2; i++)
		{
			const float z = -28.0f - i * 46.0f;
			m.torusZ(0.0f, sy, z, 7.0f, 1.6f, M.steel);
			m.blend(1.5f);
			m.boxMM(-4.5f, 60.0f, z + 4.0f, 4.5f, sy - 6.0f, z - 4.0f, 1.0f, M.steel);
			m.add();
		}
		// Power cell indicator (left side) and selector.
		m.cut();
		m.boxMM(-13.0f, 39.0f, 0.0f, -10.0f, 47.0f, -10.0f, 1.0f, M.redLight);
		m.add();
		m.boxMM(-11.4f, 40.0f, -1.0f, -10.0f, 46.0f, -9.0f, 0.5f, M.redLight);
		m.cylX(-11.6f, 52.0f, 22.0f, 3.0f, 1.5f, 0.6f, M.silver);
		// Broomhandle grip, lanyard ring.
		grip(m, M.woodd, M.wood, -0.30f, 0);
		m.add();
		m.setRot(0.0f, 1.5707963f);
		m.torus(0.0f, -53.0f, 21.0f, 4.0f, 1.2f, M.steel);
		m.noRot();
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -186.0f;
	}

	void buildE11(Model& m, const Mats& M, float muzzle[3])
	{
		const float by = 44.0f, R = 17.0f;
		m.add();
		// Receiver tube and rear cap with bayonet lugs.
		m.cylZ(0.0f, by, 70.0f, -110.0f, R, 1.0f, M.black);
		m.cylZ(0.0f, by, 68.0f, 82.0f, R + 1.2f, 1.5f, M.steel);
		for (int i = 0; i < 4; i++)
		{
			const float a = i * 1.5708f + 0.785f;
			m.setRot(0.0f, 0.0f, a);
			m.box(cosf(a) * (R + 1.5f), by + sinf(a) * (R + 1.5f), 76.0f, 2.5f, 3.0f, 5.0f, 1.0f, M.steel);
		}
		m.noRot();
		m.cylZ(0.0f, by, 82.0f, 86.0f, R - 5.0f, 1.0f, M.silver);
		// Perforated barrel jacket (hollow) with the barrel inside.
		m.cylZ(0.0f, by, -100.0f, -300.0f, R, 1.0f, M.black);
		m.cut();
		m.cylZ(0.0f, by, -112.0f, -296.0f, R - 2.2f, 0.0f, M.dark);
		for (int row = 0; row < 8; row++)
		{
			radialHoles(m, 0.0f, by, -124.0f - row * 22.0f, R, 6, 0.524f + (row & 1) * 0.0f, 5.2f, 3.0f, M.dark);
		}
		m.add();
		m.cylZ(0.0f, by, -100.0f, -306.0f, 6.0f, 0.5f, M.steel);
		for (int i = 0; i < 10; i++) { m.torusZ(0.0f, by, -130.0f - i * 16.0f, 6.0f, 0.8f, M.steel); }
		// Muzzle cap, bore and front sight with protective ears.
		m.cylZ(0.0f, by, -298.0f, -314.0f, R + 0.5f, 2.0f, M.gun);
		m.cutSmooth(0.8f);
		m.cylZ(0.0f, by, -300.0f, -318.0f, 5.0f, 0.0f, M.bore);
		m.coneZ(0.0f, by, -309.0f, -315.0f, 5.0f, 8.0f, M.bore);
		m.add();
		m.boxMM(-1.2f, by + R - 1.0f, -284.0f, 1.2f, by + R + 11.0f, -290.0f, 0.6f, M.gun);
		m.blend(1.0f);
		m.boxMM(-7.0f, by + R - 3.0f, -280.0f, 7.0f, by + R + 3.0f, -294.0f, 1.5f, M.gun);
		m.add();
		m.boxMM(-7.0f, by + R, -282.0f, -5.0f, by + R + 10.0f, -292.0f, 0.8f, M.gun);
		m.boxMM(5.0f, by + R, -282.0f, 7.0f, by + R + 10.0f, -292.0f, 0.8f, M.gun);
		// Rear flip sight.
		m.boxMM(-5.0f, by + R - 1.0f, 50.0f, 5.0f, by + R + 3.0f, 36.0f, 1.0f, M.gun);
		m.boxMM(-4.0f, by + R + 2.0f, 46.0f, 4.0f, by + R + 10.0f, 43.0f, 0.8f, M.steel);
		m.cut();
		m.cylZ(0.0f, by + R + 7.0f, 48.0f, 40.0f, 1.3f, 0.0f, M.steel);
		// Magazine housing and power pack on the left.
		m.add();
		m.cylX(-24.0f, by - 2.0f, -70.0f, 11.0f, 8.0f, 1.5f, M.gun);
		m.box(-59.0f, by - 2.0f, -71.0f, 29.0f, 8.0f, 13.0f, 2.0f, M.black);
		for (int i = 0; i < 6; i++) { m.cutSmooth(0.5f); m.box(-40.0f - i * 8.0f, by + 6.5f, -71.0f, 1.5f, 1.2f, 14.0f, 0.5f, M.dark); m.box(-40.0f - i * 8.0f, by - 10.5f, -71.0f, 1.5f, 1.2f, 14.0f, 0.5f, M.dark); }
		m.add();
		m.box(-90.0f, by - 2.0f, -71.0f, 3.0f, 9.0f, 14.5f, 1.5f, M.steel);
		m.cylX(-93.5f, by - 2.0f, -71.0f, 5.0f, 1.0f, 0.5f, M.silver);
		// Hengstler counter with its digit window and knob.
		m.blend(1.5f);
		m.boxMM(-14.0f, by - 14.0f, -2.0f, -34.0f, by + 12.0f, -34.0f, 2.0f, M.black);
		m.cutSmooth(0.5f);
		m.boxMM(-36.0f, by - 4.0f, -8.0f, -33.0f, by + 4.0f, -28.0f, 0.8f, M.white);
		m.add();
		m.boxMM(-33.8f, by - 3.5f, -9.0f, -33.0f, by + 3.5f, -27.0f, 0.3f, M.white);
		for (int d = 0; d < 4; d++) { m.paint(); m.boxMM(-34.5f, by - 3.6f, -10.5f - d * 4.6f, -32.0f, by + 3.6f, -10.8f - d * 4.6f, 0.0f, M.black); }
		m.add();
		m.cylX(-35.5f, by + 8.0f, -18.0f, 3.0f, 2.0f, 0.6f, M.knurl);
		// T-tracks on the top right.
		m.boxMM(5.0f, by + R - 3.0f, -60.0f, 15.0f, by + R + 3.0f, -190.0f, 1.0f, M.gun);
		for (int i = 0; i < 6; i++) { m.cutSmooth(0.4f); m.box(10.0f, by + R + 3.0f, -70.0f - i * 22.0f, 6.0f, 2.0f, 4.0f, 0.6f, M.dark); }
		m.add();
		// M38 style scope on the left rail: tube, bells, turrets, rings, lenses.
		const float sx = -10.0f, sy = by + R + 20.0f;
		m.cylZ(sx, sy, -26.0f, -128.0f, 8.0f, 0.6f, M.black);
		m.blend(1.5f);
		m.coneZ(sx, sy, -116.0f, -134.0f, 8.0f, 12.0f, M.black);
		m.coneZ(sx, sy, -34.0f, -20.0f, 8.0f, 11.0f, M.black);
		m.add();
		m.cylZ(sx, sy, -134.0f, -140.0f, 12.0f, 1.0f, M.gun);
		m.cylZ(sx, sy, -14.0f, -20.0f, 11.0f, 1.0f, M.gun);
		m.cutSmooth(0.6f);
		m.cylZ(sx, sy, -137.0f, -142.0f, 9.5f, 0.0f, M.blueLens);
		m.cylZ(sx, sy, -12.0f, -16.0f, 8.0f, 0.0f, M.blueLens);
		m.add();
		m.cylZ(sx, sy, -138.0f, -139.0f, 9.6f, 0.0f, M.blueLens);
		m.cylZ(sx, sy, -14.5f, -15.0f, 8.1f, 0.0f, M.blueLens);
		m.cylY(sx, sy + 10.0f, -74.0f, 4.5f, 3.5f, 0.8f, M.knurl);
		m.cylX(sx + 10.0f, sy, -74.0f, 4.5f, 3.0f, 0.8f, M.knurl);
		for (int i = 0; i < 2; i++)
		{
			const float z = -48.0f - i * 56.0f;
			m.torusZ(sx, sy, z, 8.6f, 1.8f, M.steel);
			m.blend(1.5f);
			m.boxMM(sx - 5.0f, by + R - 2.0f, z + 5.0f, sx + 5.0f, sy - 7.0f, z - 5.0f, 1.0f, M.steel);
			m.add();
		}
		// Cocking handle slot and knob on the left, ejection port on the right.
		m.cutSmooth(0.6f);
		m.boxMM(-R - 2.0f, by - 1.5f, 40.0f, -R + 2.5f, by + 1.5f, -20.0f, 0.8f, M.dark);
		m.boxMM(R - 3.0f, by + 2.0f, -14.0f, R + 2.0f, by + 12.0f, -46.0f, 1.5f, M.dark);
		m.add();
		m.cylX(-R - 6.0f, by, -10.0f, 3.5f, 5.0f, 1.0f, M.steel);
		m.sphere(-R - 11.0f, by, -10.0f, 4.0f, M.steel);
		// Folded stock on top: two rods with hinges and the butt plate at the front.
		m.cylZ(8.0f, by + R + 6.0f, 76.0f, -116.0f, 3.0f, 1.0f, M.gun);
		m.cylZ(-8.0f, by + R + 6.0f, 76.0f, -116.0f, 3.0f, 1.0f, M.gun);
		m.cylX(0.0f, by + R + 4.0f, 74.0f, 4.0f, 12.0f, 1.0f, M.steel);
		m.blend(2.0f);
		m.boxMM(-20.0f, by + R + 1.0f, -114.0f, 20.0f, by + R + 16.0f, -124.0f, 3.0f, M.gun);
		m.cut();
		m.boxMM(-14.0f, by + R + 5.0f, -112.0f, 14.0f, by + R + 12.0f, -126.0f, 2.0f, M.dark);
		// Trigger group housing, guard and grip.
		m.blend(2.0f);
		m.boxMM(-10.0f, 20.0f, 30.0f, 10.0f, by - R + 3.0f, -36.0f, 2.5f, M.black);
		m.add();
		triggerGuard(m, 22.0f, -24.0f, 12.0f, M.black, M.steel);
		grip(m, M.black, M.rubber, -0.35f, 1);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -316.0f;
	}

	void buildThermal(Model& m, const Mats& M, float muzzle[3])
	{
		const float cy = 6.0f, cz = -26.0f;
		m.add();
		m.sphere(0.0f, cy, cz, 31.0f, M.silver);
		// Raised equator rings with a recessed band of sockets between them.
		m.setRot(1.5707963f);
		m.torus(0.0f, cy + 5.0f, cz, 30.0f, 1.8f, M.steel);
		m.torus(0.0f, cy - 5.0f, cz, 30.0f, 1.8f, M.steel);
		m.noRot();
		for (int i = 0; i < 10; i++)
		{
			const float a = i * 0.6283185f;
			m.cutSmooth(0.6f);
			m.setRot(0.0f, a);
			m.box(sinf(a) * 31.0f, cy, cz + cosf(a) * 31.0f, 3.2f, 2.4f, 2.5f, 0.8f, M.dark);
			m.add();
			m.paint();
			m.box(sinf(a) * 29.6f, cy, cz + cosf(a) * 29.6f, 1.5f, 1.2f, 1.0f, 0.3f, (i % 3 == 0) ? M.redLight : M.dark);
		}
		m.noRot();
		// Arming plate on top: lights and the slider.
		m.add();
		m.blend(2.0f);
		m.box(0.0f, cy + 30.0f, cz, 14.0f, 4.0f, 9.0f, 2.5f, M.black);
		m.add();
		m.cut();
		m.box(0.0f, cy + 34.0f, cz + 3.0f, 10.0f, 1.5f, 2.0f, 0.8f, M.dark);
		m.add();
		m.box(-4.0f, cy + 34.0f, cz + 3.0f, 2.5f, 1.4f, 1.8f, 0.6f, M.silver);
		m.box(-8.0f, cy + 34.0f, cz - 4.0f, 2.0f, 1.2f, 2.0f, 0.6f, M.redLight);
		m.box(-2.0f, cy + 34.0f, cz - 4.0f, 2.0f, 1.2f, 2.0f, 0.6f, M.amberLight);
		m.box(4.0f, cy + 34.0f, cz - 4.0f, 2.0f, 1.2f, 2.0f, 0.6f, M.greenLight);
		// Bottom plunger.
		m.cylY(0.0f, cy - 31.0f, cz, 6.0f, 3.0f, 1.0f, M.steel);
		m.cylY(0.0f, cy - 34.0f, cz, 4.0f, 2.0f, 1.0f, M.knurl);
		muzzle[0] = 0.0f; muzzle[1] = cy; muzzle[2] = cz - 40.0f;
	}

	void buildRepeater(Model& m, const Mats& M, float muzzle[3])
	{
		const float by = 50.0f;
		m.add();
		m.box(0.0f, by - 1.0f, -40.0f, 24.0f, 25.0f, 80.0f, 4.0f, M.dark);
		m.blend(2.0f);
		m.box(0.0f, by - 1.0f, -40.0f, 26.0f, 17.0f, 70.0f, 3.0f, M.gun);
		m.add();
		for (int i = 0; i < 7; i++)                                                   // brass rivets
		{
			const float z = 17.5f - i * 18.0f;
			for (int s = -1; s <= 1; s += 2)
			{
				m.cylX(s * 26.8f, by + 8.5f, z, 2.8f, 1.2f, 0.8f, M.brass);
				m.cylX(s * 26.8f, by - 7.5f, z, 2.8f, 1.2f, 0.8f, M.brass);
			}
		}
		for (int i = 0; i < 8; i++)                                                   // ribbed top
		{
			m.blend(1.0f);
			m.box(0.0f, by + 26.0f, 26.0f - i * 18.0f, 17.0f, 3.5f, 4.0f, 2.0f, M.black);
		}
		m.add();
		// Rounded front housing with the gold band and a domed nose.
		m.cylZ(0.0f, by, -110.0f, -228.0f, 34.0f, 4.0f, M.steel);
		m.cylZ(0.0f, by, -140.0f, -160.0f, 35.8f, 1.5f, M.gold);
		for (int i = 0; i < 3; i++) { m.torusZ(0.0f, by, -176.0f - i * 14.0f, 34.0f, 1.3f, M.gun); }
		m.blend(3.0f);
		m.cylZ(0.0f, by, -224.0f, -236.0f, 30.0f, 4.0f, M.gun);
		m.add();
		for (int k = 0; k < 6; k++)                                                   // barrel cluster
		{
			const float a = k * 1.0471976f;
			const float x = cosf(a) * 16.0f, y = by + sinf(a) * 16.0f;
			m.cylZ(x, y, -232.0f, -252.0f, 7.0f, 1.2f, M.black);
			m.cutSmooth(0.5f);
			m.cylZ(x, y, -238.0f, -256.0f, 3.6f, 0.0f, M.bore);
			m.add();
		}
		m.cylZ(0.0f, by, -230.0f, -246.0f, 8.0f, 1.5f, M.dark);
		// Charge light, side vents.
		m.cut();
		m.boxMM(25.0f, by - 3.0f, -58.0f, 29.0f, by + 5.0f, -92.0f, 1.0f, M.greenLight);
		m.add();
		m.boxMM(25.0f, by - 2.0f, -60.0f, 27.0f, by + 4.0f, -90.0f, 0.5f, M.greenLight);
		for (int i = 0; i < 5; i++) { m.cutSmooth(0.5f); m.box(-26.0f, by, -60.0f - i * 9.0f, 2.0f, 9.0f, 2.0f, 1.0f, M.bore); }
		m.add();
		// Rod stock with the butt plate.
		m.cylZ(0.0f, by + 4.0f, 38.0f, 170.0f, 5.0f, 1.0f, M.steel);
		m.blend(3.0f);
		m.box(0.0f, by - 10.0f, 173.0f, 14.0f, 26.0f, 5.0f, 4.0f, M.gun);
		m.add();
		m.box(0.0f, by - 10.0f, 178.5f, 13.0f, 25.0f, 1.0f, 1.0f, M.rubber);
		triggerGuard(m, 24.0f, -20.0f, 14.0f, M.dark, M.steel);
		grip(m, M.black, M.rubber, -0.35f, 1);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -252.0f;
	}

	void buildFusion(Model& m, const Mats& M, float muzzle[3])
	{
		const float by = 46.0f, R = 14.0f;
		const float xs[4] = { -45.0f, -15.0f, 15.0f, 45.0f };
		const int stripes = m.mat(rgbMat(DARK, 0.45f, PT_STRIPES, ORANGE, 46.0f, 15.0f));
		m.add();
		for (int i = 0; i < 4; i++)
		{
			m.cylZ(xs[i], by, -20.0f, -190.0f, R, 1.0f, stripes);
			m.blend(2.0f);
			m.sphere(xs[i], by, -190.0f, R + 2.0f, M.black);
			m.add();
			m.cutSmooth(0.6f);
			m.cylZ(xs[i], by, -192.0f, -212.0f, 5.0f, 0.0f, M.bore);
			m.add();
			m.cylZ(xs[i], by, -20.0f, -32.0f, R + 2.0f, 1.5f, M.gun);
			for (int r = 0; r < 3; r++) { m.torusZ(xs[i], by, -60.0f - r * 40.0f, R, 1.0f, M.gun); }
		}
		m.blend(3.0f);
		m.box(0.0f, by, 8.0f, 62.0f, 18.0f, 32.0f, 5.0f, M.gun);
		m.sphere(-62.0f, by, 10.0f, 16.0f, M.dark);
		m.sphere(62.0f, by, 10.0f, 16.0f, M.dark);
		m.add();
		m.boxMM(-30.0f, by + 17.0f, 30.0f, 30.0f, by + 24.0f, -10.0f, 2.0f, M.black);
		for (int i = 0; i < 5; i++) { m.cutSmooth(0.4f); m.box(-20.0f + i * 10.0f, by + 24.0f, 10.0f, 1.5f, 1.5f, 16.0f, 0.5f, M.bore); }
		m.add();
		m.cut();
		m.boxMM(55.0f, by + 7.0f, -3.0f, 61.0f, by + 15.0f, -13.0f, 1.0f, M.redLight);
		m.add();
		m.boxMM(56.0f, by + 8.0f, -4.0f, 59.5f, by + 14.0f, -12.0f, 0.5f, M.redLight);
		triggerGuard(m, 24.0f, -16.0f, 14.0f, M.dark, M.steel);
		grip(m, M.black, M.rubber, -0.35f, 1);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -212.0f;
	}

	void buildMortar(Model& m, const Mats& M, float muzzle[3])
	{
		const float by = 56.0f;
		m.add();
		m.cylZ(0.0f, by, 20.0f, -170.0f, 30.0f, 4.0f, M.chrome);
		m.blend(4.0f);
		m.cylZ(-22.0f, by + 20.0f, 0.0f, -120.0f, 20.0f, 4.0f, M.chrome);
		m.cylZ(22.0f, by + 20.0f, 0.0f, -120.0f, 20.0f, 4.0f, M.chrome);
		m.add();
		m.box(0.0f, by - 24.0f, -70.0f, 30.0f, 10.0f, 80.0f, 3.0f, M.dark);
		for (int k = 0; k < 3; k++)
		{
			const float a = 1.5708f + k * 2.0944f;
			const float x = cosf(a) * 30.0f, y = by + sinf(a) * 30.0f;
			m.cylZ(x, y, -50.0f, -140.0f, 11.0f, 2.0f, M.dark);
			m.cylZ(x, y, -140.0f, -146.0f, 7.0f, 1.0f, M.brass);
			m.cylZ(x, y, -146.0f, -149.0f, 4.0f, 1.0f, M.redLight);
		}
		// Perforated shroud (hollow) around the barrel.
		m.cylZ(0.0f, by, -170.0f, -300.0f, 25.0f, 2.0f, M.steel);
		m.cut();
		m.cylZ(0.0f, by, -178.0f, -294.0f, 22.0f, 0.0f, M.dark);
		for (int row = 0; row < 6; row++) { radialHoles(m, 0.0f, by, -186.0f - row * 19.0f, 25.0f, 8, (row & 1) * 0.39f, 4.6f, 3.0f, M.dark); }
		m.add();
		m.cylZ(0.0f, by, -150.0f, -300.0f, 18.0f, 1.0f, M.dark);
		m.cylZ(0.0f, by, -298.0f, -312.0f, 27.0f, 3.0f, M.chrome);
		m.cutSmooth(1.0f);
		m.cylZ(0.0f, by, -160.0f, -316.0f, 14.0f, 0.0f, M.bore);
		m.add();
		m.cylZ(0.0f, by, -150.0f, -160.0f, 17.0f, 1.0f, M.black);
		m.boxMM(-4.0f, by + 24.0f, -172.0f, 4.0f, by + 34.0f, -180.0f, 1.5f, M.dark);
		m.boxMM(-4.0f, by - 34.0f, -172.0f, 4.0f, by - 24.0f, -180.0f, 1.5f, M.dark);
		m.blend(2.0f);
		m.boxMM(-16.0f, 22.0f, 30.0f, 16.0f, by - 28.0f, -30.0f, 3.0f, M.dark);
		m.add();
		m.cylZ(0.0f, by - 6.0f, 20.0f, 178.0f, 6.0f, 1.0f, M.dark);
		m.blend(3.0f);
		m.box(0.0f, by - 10.0f, 181.0f, 22.0f, 34.0f, 5.0f, 4.0f, M.gun);
		m.add();
		triggerGuard(m, 24.0f, -20.0f, 14.0f, M.dark, M.steel);
		grip(m, M.dark, M.rubber, -0.35f, 1);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -314.0f;
	}

	void buildMine(Model& m, const Mats& M, float muzzle[3])
	{
		const float cy = 14.0f, cz = -24.0f;
		m.add();
		m.cylY(0.0f, cy, cz, 46.0f, 8.0f, 3.5f, M.silver);
		m.blend(2.0f);
		m.cylY(0.0f, cy + 8.0f, cz, 40.0f, 2.0f, 1.5f, M.chrome);
		m.add();
		m.cylY(0.0f, cy - 8.0f, cz, 42.0f, 2.0f, 1.5f, M.steel);
		m.setRot(1.5707963f);
		m.torus(0.0f, cy, cz, 46.0f, 1.6f, M.steel);
		m.noRot();
		for (int k = 0; k < 3; k++)
		{
			const float a = k * 2.0944f + 1.5708f;
			m.setRot(0.0f, -a);
			m.box(cosf(a) * 24.0f, cy + 11.0f, cz + sinf(a) * 24.0f, 17.0f, 1.6f, 4.5f, 1.2f, M.orange);
		}
		m.noRot();
		m.cylY(0.0f, cy + 12.0f, cz, 8.0f, 2.0f, 1.0f, M.dark);
		m.cylY(0.0f, cy + 14.0f, cz, 3.5f, 1.2f, 0.8f, M.redLight);
		m.paint();
		for (int i = 0; i < 6; i++) { m.box(-30.0f + i * 5.0f, cy + 10.0f, cz + 22.0f, 2.5f, 1.5f, 3.0f, 0.0f, (i & 1) ? M.white : M.red); }
		m.add();
		m.box(22.0f, cy + 10.4f, cz + 20.0f, 2.0f, 1.0f, 2.0f, 0.6f, M.blueLens);
		for (int i = 0; i < 12; i++)
		{
			const float a = i * 0.5236f;
			m.cutSmooth(0.4f);
			m.setRot(0.0f, -a);
			m.box(cosf(a) * 46.0f, cy, cz + sinf(a) * 46.0f, 2.0f, 3.0f, 1.5f, 0.6f, M.dark);
		}
		m.noRot(); m.add();
		muzzle[0] = 0.0f; muzzle[1] = cy; muzzle[2] = cz - 50.0f;
	}

	void buildConcussion(Model& m, const Mats& M, float muzzle[3])
	{
		const float by = 40.0f, ty = by + 24.0f;
		m.add();
		m.box(0.0f, by - 1.0f, -105.0f, 15.0f, 15.0f, 145.0f, 3.0f, M.black);
		m.cylZ(0.0f, ty, 30.0f, -390.0f, 11.0f, 1.5f, M.silver);
		for (int i = 0; i < 6; i++) { m.cylZ(0.0f, ty, -60.0f - i * 50.0f, -66.0f - i * 50.0f, 13.0f, 1.0f, M.steel); }
		m.blend(2.0f);
		m.cylZ(0.0f, ty, -380.0f, -400.0f, 15.0f, 2.5f, M.gun);
		m.cutSmooth(0.8f);
		m.cylZ(0.0f, ty, -386.0f, -404.0f, 7.0f, 0.0f, M.bore);
		m.add();
		m.blend(1.5f);
		m.boxMM(-6.0f, by + 12.0f, 20.0f, 6.0f, ty - 6.0f, -360.0f, 1.5f, M.gun);
		m.add();
		m.cylZ(0.0f, by, -248.0f, -332.0f, 8.0f, 1.0f, M.dark);
		m.cutSmooth(0.5f);
		m.cylZ(0.0f, by, -320.0f, -336.0f, 4.0f, 0.0f, M.bore);
		m.add();
		// Red concussion chamber with a glowing core.
		m.cylX(0.0f, by - 2.0f, -40.0f, 14.0f, 17.0f, 3.0f, M.red);
		m.cut();
		m.cylX(0.0f, by - 2.0f, -40.0f, 8.5f, 19.0f, 0.0f, M.redLight);
		m.add();
		m.cylX(0.0f, by - 2.0f, -40.0f, 8.0f, 16.5f, 1.5f, M.redLight);
		for (int i = 0; i < 8; i++) { m.cutSmooth(0.5f); m.box(15.0f, by + 4.0f, -80.0f - i * 18.0f, 2.0f, 5.0f, 4.0f, 1.0f, M.bore); m.box(-15.0f, by + 4.0f, -80.0f - i * 18.0f, 2.0f, 5.0f, 4.0f, 1.0f, M.bore); }
		m.add();
		m.boxMM(-4.0f, ty + 10.0f, -40.0f, 4.0f, ty + 18.0f, -48.0f, 1.0f, M.brass);
		m.cut();
		m.cylZ(0.0f, ty + 15.0f, -38.0f, -50.0f, 1.5f, 0.0f, M.brass);
		// Wooden stock with a cheek cut and the butt plate.
		m.blend(3.0f);
		m.boxMM(-13.0f, by - 30.0f, 40.0f, 13.0f, by + 12.0f, 190.0f, 5.0f, M.wood);
		m.cutSmooth(4.0f);
		m.box(0.0f, by - 20.0f, 110.0f, 16.0f, 6.0f, 38.0f, 4.0f, M.woodd);
		m.add();
		m.boxMM(-14.0f, by - 34.0f, 186.0f, 14.0f, by + 14.0f, 195.0f, 3.0f, M.rubber);
		triggerGuard(m, 24.0f, -18.0f, 14.0f, M.black, M.steel);
		grip(m, M.wood, M.woodd, -0.35f, 0);
		muzzle[0] = 0.0f; muzzle[1] = ty; muzzle[2] = -402.0f;
	}

	void buildCannon(Model& m, const Mats& M, float muzzle[3])
	{
		const float by = 34.0f;
		m.add();
		m.boxMM(-38.0f, by - 14.0f, 90.0f, 38.0f, by + 96.0f, -150.0f, 6.0f, M.gun);
		m.blend(2.0f);
		m.boxMM(-40.0f, by + 20.0f, 40.0f, 40.0f, by + 60.0f, -120.0f, 3.0f, M.dark);
		m.boxMM(-39.5f, by + 64.0f, 80.0f, 39.5f, by + 80.0f, -140.0f, 3.0f, M.rust);
		m.add();
		for (int i = 0; i < 6; i++)
		{
			const float y = by + 34.0f + (i & 1) * 18.0f, z = 14.0f - i * 22.0f;
			m.cut();
			m.box(40.0f, y, z, 3.0f, 6.0f, 6.0f, 1.5f, M.amberLight);
			m.box(-40.0f, y, z, 3.0f, 6.0f, 6.0f, 1.5f, M.amberLight);
			m.add();
			m.box(39.0f, y, z, 1.0f, 5.0f, 5.0f, 0.5f, M.amberLight);
			m.box(-39.0f, y, z, 1.0f, 5.0f, 5.0f, 0.5f, M.amberLight);
		}
		m.cylZ(0.0f, by, -150.0f, -420.0f, 20.0f, 1.5f, M.black);
		for (int i = 0; i < 5; i++) { m.cylZ(0.0f, by, -170.0f - i * 50.0f, -184.0f - i * 50.0f, 23.0f, 2.0f, M.steel); }
		m.blend(2.0f);
		m.cylZ(0.0f, by, -418.0f, -440.0f, 25.0f, 3.0f, M.gun);
		m.cutSmooth(1.0f);
		m.cylZ(0.0f, by, -422.0f, -444.0f, 12.0f, 0.0f, M.bore);
		m.add();
		m.cylZ(0.0f, by + 60.0f, -150.0f, -250.0f, 12.0f, 1.5f, M.dark);
		m.cutSmooth(0.8f);
		m.cylZ(0.0f, by + 60.0f, -236.0f, -254.0f, 7.0f, 0.0f, M.bore);
		m.add();
		m.blend(2.0f);
		m.boxMM(-8.0f, by + 94.0f, 40.0f, 8.0f, by + 110.0f, -90.0f, 3.0f, M.black);
		m.cutSmooth(2.0f);
		m.box(0.0f, by + 100.0f, -25.0f, 10.0f, 5.0f, 50.0f, 2.0f, M.black);
		m.add();
		m.cylX(-40.0f, by + 70.0f, 40.0f, 18.0f, 6.0f, 2.0f, M.red);
		m.cylX(-46.0f, by + 70.0f, 40.0f, 10.0f, 1.0f, 0.8f, M.redLight);
		triggerGuard(m, 20.0f, -20.0f, 14.0f, M.black, M.steel);
		grip(m, M.black, M.rubber, -0.35f, 1);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -442.0f;
	}

	// ------------------------------------------------------------------ evaluation
	struct Grid
	{
		int nx = 0, ny = 0, nz = 0;
		float res = 1.0f, mn[3] = { 0, 0, 0 };
		std::vector<float> d;
		std::vector<uint8_t> m;
		size_t idx(int x, int y, int z) const { return (size_t(z) * ny + y) * nx + x; }
		float sample(float px, float py, float pz) const
		{
			float gx = (px - mn[0]) / res, gy = (py - mn[1]) / res, gz = (pz - mn[2]) / res;
			gx = clampf(gx, 0.0f, nx - 1.001f); gy = clampf(gy, 0.0f, ny - 1.001f); gz = clampf(gz, 0.0f, nz - 1.001f);
			const int x = int(gx), y = int(gy), z = int(gz);
			const float fx = gx - x, fy = gy - y, fz = gz - z;
			auto D = [&](int a, int b, int c) { return d[idx(x + a, y + b, z + c)]; };
			const float c00 = D(0, 0, 0) + (D(1, 0, 0) - D(0, 0, 0)) * fx, c10 = D(0, 1, 0) + (D(1, 1, 0) - D(0, 1, 0)) * fx;
			const float c01 = D(0, 0, 1) + (D(1, 0, 1) - D(0, 0, 1)) * fx, c11 = D(0, 1, 1) + (D(1, 1, 1) - D(0, 1, 1)) * fx;
			const float c0 = c00 + (c10 - c00) * fy, c1 = c01 + (c11 - c01) * fy;
			return c0 + (c1 - c0) * fz;
		}
		int matAt(float px, float py, float pz) const
		{
			const int x = int(clampf(roundf((px - mn[0]) / res), 0.0f, float(nx - 1)));
			const int y = int(clampf(roundf((py - mn[1]) / res), 0.0f, float(ny - 1)));
			const int z = int(clampf(roundf((pz - mn[2]) / res), 0.0f, float(nz - 1)));
			return m[idx(x, y, z)];
		}
	};

	struct BuildCtx { const Model* model; const Grid* grid; float grip[3]; };

	void materialFn(void* user, const float p[3], float rgb[3], float* spec, float* emis)
	{
		const BuildCtx* c = (const BuildCtx*)user;
		const float x = (p[0] - c->grip[0]) * 1000.0f, y = (p[1] - c->grip[1]) * 1000.0f, z = (p[2] - c->grip[2]) * 1000.0f;
		const Mat& mt = c->model->mats[c->grid->matAt(x, y, z)];
		float r = mt.r, g = mt.g, b = mt.b, s = mt.spec, e = 0.0f;
		const float ar = ((mt.alt >> 16) & 255) / 255.0f, ag = ((mt.alt >> 8) & 255) / 255.0f, ab = (mt.alt & 255) / 255.0f;
		switch (mt.pattern)
		{
		case PT_METAL:
		{
			// Brushed along the barrel axis, light scratches.
			const float n = vnoise(x * 0.9f, y * 0.9f, z * 0.06f) * 0.6f + vnoise(x * 3.0f, y * 3.0f, z * 0.2f) * 0.4f;
			const float k = 0.86f + 0.22f * n;
			r *= k; g *= k; b *= k;
			s *= 0.85f + 0.3f * n;
		} break;
		case PT_PAINT:
		{
			// Worn paint: blotches of slightly lighter paint and spots of bare metal.
			const float n = fbm(x * 0.12f, y * 0.12f, z * 0.12f);
			const float k = 0.9f + 0.18f * n;
			r *= k; g *= k; b *= k;
			const float wear = fbm(x * 0.5f + 7.0f, y * 0.5f, z * 0.5f);
			if (wear > 0.74f)
			{
				const float t = fminf(1.0f, (wear - 0.74f) * 10.0f) * 0.6f;
				r += (ar - r) * t; g += (ag - g) * t; b += (ab - b) * t; s = fminf(1.0f, s + t * 0.5f);
			}
		} break;
		case PT_WOOD:
		{
			const float n = fbm(x * 0.08f, y * 0.08f, z * 0.02f);
			const float ring = 0.5f + 0.5f * sinf((y * 0.9f + x * 0.3f) + n * 9.0f);
			const float t = ring * ring * 0.7f;
			r += (ar - r) * t; g += (ag - g) * t; b += (ab - b) * t;
			const float k = 0.92f + 0.16f * vnoise(x * 2.0f, y * 2.0f, z * 0.3f);
			r *= k; g *= k; b *= k;
		} break;
		case PT_RUBBER:
		{
			const float n = h01(int(x * 1.6f), int(y * 1.6f), int(z * 1.6f));
			const float k = 0.85f + 0.3f * n;
			r *= k; g *= k; b *= k;
		} break;
		case PT_STRIPES:
		{
			// Helical hazard stripes around the barrel (period p0, width p1 along z).
			const float t = z + (x + y) * 0.5f;
			const float per = mt.p0 * c->model->k, w = mt.p1 * c->model->k;
			float mm = fmodf(t, per); if (mm < 0.0f) mm += per;
			if (mm < w) { r = ar; g = ag; b = ab; s = 0.3f; }
			const float k = 0.9f + 0.18f * fbm(x * 0.15f, y * 0.15f, z * 0.15f);
			r *= k; g *= k; b *= k;
		} break;
		case PT_LIGHT: e = 1.0f; break;
		case PT_KNURL:
		{
			const float k = 0.75f + 0.35f * (0.5f + 0.5f * sinf(x * 3.0f + y * 3.0f + z * 3.0f) * sinf(x * 3.0f - y * 3.0f));
			r *= k; g *= k; b *= k;
		} break;
		default: break;
		}
		*spec = clampf(s, 0.0f, 1.0f);
		*emis = e;
		rgb[0] = clampf(r, 0.0f, 1.0f); rgb[1] = clampf(g, 0.0f, 1.0f); rgb[2] = clampf(b, 0.0f, 1.0f);
	}

	bool buildModel(int id, Model& m, float muzzle[3])
	{
		static const float c_scale[10] = { 1.0f, 1.25f, 1.35f, 1.0f, 1.6f, 1.45f, 1.4f, 1.0f, 1.35f, 1.45f };
		m.k = c_scale[(id > 0 && id < 10) ? id : 0];
		const Mats M = stdMats(m);
		switch (id)
		{
		case 1: buildBryar(m, M, muzzle); break;
		case 2: buildE11(m, M, muzzle); break;
		case 3: buildThermal(m, M, muzzle); break;
		case 4: buildRepeater(m, M, muzzle); break;
		case 5: buildFusion(m, M, muzzle); break;
		case 6: buildMortar(m, M, muzzle); break;
		case 7: buildMine(m, M, muzzle); break;
		case 8: buildConcussion(m, M, muzzle); break;
		case 9: buildCannon(m, M, muzzle); break;
		default: return false;
		}
		for (int i = 0; i < 3; i++) { muzzle[i] *= m.k; }
		return true;
	}

	void evaluate(Model& m, Grid& g, float res)
	{
		// Bounds of the solid primitives.
		float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
		for (Prim& p : m.prims)
		{
			float e[3]; primExtent(p, e);
			const float pad = p.k + 0.5f;
			for (int i = 0; i < 3; i++)
			{
				// |R^T| * e  (R rows = world->local, so world extent uses columns)
				const float w = fabsf(p.R[0 * 3 + i]) * e[0] + fabsf(p.R[1 * 3 + i]) * e[1] + fabsf(p.R[2 * 3 + i]) * e[2];
				p.bmin[i] = p.c[i] - w - pad; p.bmax[i] = p.c[i] + w + pad;
			}
			if (p.op == OP_ADD || p.op == OP_SMOOTH)
			{
				for (int i = 0; i < 3; i++) { mn[i] = fminf(mn[i], p.bmin[i]); mx[i] = fmaxf(mx[i], p.bmax[i]); }
			}
		}
		const float band = 5.0f * res;      // truncation of the distance field
		for (int i = 0; i < 3; i++) { mn[i] -= band + res; mx[i] += band + res; }
		g.res = res;
		for (int i = 0; i < 3; i++) { g.mn[i] = mn[i]; }
		g.nx = int((mx[0] - mn[0]) / res) + 2; g.ny = int((mx[1] - mn[1]) / res) + 2; g.nz = int((mx[2] - mn[2]) / res) + 2;
		g.d.assign(size_t(g.nx) * g.ny * g.nz, band);
		g.m.assign(g.d.size(), 0);
		for (const Prim& p : m.prims)
		{
			const float grow = (p.op == OP_ADD || p.op == OP_SMOOTH || p.op == OP_PAINT) ? band : res * 2.0f;
			const int x0 = std::max(0, int(floorf((p.bmin[0] - grow - mn[0]) / res))), x1 = std::min(g.nx - 1, int(ceilf((p.bmax[0] + grow - mn[0]) / res)));
			const int y0 = std::max(0, int(floorf((p.bmin[1] - grow - mn[1]) / res))), y1 = std::min(g.ny - 1, int(ceilf((p.bmax[1] + grow - mn[1]) / res)));
			const int z0 = std::max(0, int(floorf((p.bmin[2] - grow - mn[2]) / res))), z1 = std::min(g.nz - 1, int(ceilf((p.bmax[2] + grow - mn[2]) / res)));
			const uint8_t pm = uint8_t(p.mat);
			for (int z = z0; z <= z1; z++)
			{
				const float wz = mn[2] + z * res;
				for (int y = y0; y <= y1; y++)
				{
					const float wy = mn[1] + y * res;
					size_t i = g.idx(x0, y, z);
					for (int x = x0; x <= x1; x++, i++)
					{
						const float dp = primDist(p, mn[0] + x * res, wy, wz);
						float& d = g.d[i];
						switch (p.op)
						{
						case OP_ADD:
							if (dp < d) { d = dp; g.m[i] = pm; }
							break;
						case OP_SMOOTH:
						{
							if (dp > d + p.k) break;
							const float h = clampf(0.5f + 0.5f * (d - dp) / p.k, 0.0f, 1.0f);
							const float nd = dp + (d - dp) * (1.0f - h) - p.k * h * (1.0f - h);
							if (h > 0.5f) { g.m[i] = pm; }
							d = nd;
						} break;
						case OP_SUB:
							if (-dp > d) { d = -dp; g.m[i] = pm; }
							break;
						case OP_SMOOTHSUB:
						{
							if (-dp < d - p.k) break;
							const float h = clampf(0.5f - 0.5f * (d + dp) / p.k, 0.0f, 1.0f);
							const float nd = d + (-dp - d) * h + p.k * h * (1.0f - h);
							if (h > 0.5f) { g.m[i] = pm; }
							d = nd;
						} break;
						case OP_PAINT:
							if (dp < res * 0.75f && d < res * 1.5f) { g.m[i] = pm; }
							break;
						}
						if (d > band) { d = band; }
						else if (d < -band) { d = -band; }
					}
				}
			}
		}
	}
}

bool weapons_buildHiRes(int id, std::vector<SVtx>& verts, std::vector<uint32_t>& indices, float muzzleOut[3], const float grip[3])
{
	Model model;
	float muzzle[3] = { 0, 0, 0 };
	if (!buildModel(id, model, muzzle)) { return false; }
	float res = 0.55f * fmaxf(1.0f, model.k * 0.85f);
	Grid g;
	for (int attempt = 0; attempt < 6; attempt++)
	{
		// Size check before allocating.
		float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
		for (const Prim& p : model.prims)
		{
			if (p.op != OP_ADD && p.op != OP_SMOOTH) continue;
			float e[3]; primExtent(p, e);
			for (int i = 0; i < 3; i++)
			{
				const float w = fabsf(p.R[0 * 3 + i]) * e[0] + fabsf(p.R[1 * 3 + i]) * e[1] + fabsf(p.R[2 * 3 + i]) * e[2] + p.k;
				mn[i] = fminf(mn[i], p.c[i] - w); mx[i] = fmaxf(mx[i], p.c[i] + w);
			}
		}
		const double cells = double((mx[0] - mn[0]) / res + 14) * ((mx[1] - mn[1]) / res + 14) * ((mx[2] - mn[2]) / res + 14);
		const double maxCells = 26.0e6;
		if (cells > maxCells) { res *= float(cbrt(cells / maxCells)) * 1.02f; attempt--; continue; }
		evaluate(model, g, res);
		MeshField f;
		f.nx = g.nx; f.ny = g.ny; f.nz = g.nz; f.cell = res * 0.001f;
		f.origin[0] = g.mn[0] * 0.001f + grip[0]; f.origin[1] = g.mn[1] * 0.001f + grip[1]; f.origin[2] = g.mn[2] * 0.001f + grip[2];
		// The mesher wants "inside > iso": negate the distances in place and share the buffer.
		for (float& d : g.d) { d = -d; }
		f.v.swap(g.d);
		BuildCtx ctx; ctx.model = &model; ctx.grid = &g; ctx.grip[0] = grip[0]; ctx.grip[1] = grip[1]; ctx.grip[2] = grip[2];
		verts.clear(); indices.clear();
		mesh_surfaceNets(f, 0.0f, materialFn, &ctx, verts, indices);
		f.v.swap(g.d);
		for (float& d : g.d) { d = -d; }
		if (indices.size() / 3 > 420000 && attempt < 5) { res *= 1.15f; continue; }
		// Baked ambient occlusion from the distance field (cavities, bores, grooves get darker).
		for (SVtx& v : verts)
		{
			const float px = (v.x - grip[0]) * 1000.0f, py = (v.y - grip[1]) * 1000.0f, pz = (v.z - grip[2]) * 1000.0f;
			float occ = 0.0f, wsum = 0.0f;
			const float steps[3] = { 1.0f, 2.0f, 3.5f };
			for (int s = 0; s < 3; s++)
			{
				const float t = steps[s] * fmaxf(1.0f, res * 1.2f);
				const float dd = g.sample(px + v.nx * t, py + v.ny * t, pz + v.nz * t);
				const float w = 1.0f / (s + 1.0f);
				occ += w * clampf((t - dd) / t, 0.0f, 1.0f);
				wsum += w;
			}
			const float ao = 1.0f - 0.75f * clampf(occ / wsum, 0.0f, 1.0f);
			if (v.emis < 0.5f) { v.r *= ao; v.g *= ao; v.b *= ao; v.spec *= ao; }
		}
		break;
	}
	const float mm = 0.001f;
	muzzleOut[0] = muzzle[0] * mm + grip[0]; muzzleOut[1] = muzzle[1] * mm + grip[1]; muzzleOut[2] = muzzle[2] * mm + grip[2];
	return !indices.empty();
}
