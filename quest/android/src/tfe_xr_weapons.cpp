// Detailed voxel models of the Dark Forces arsenal ("3D pixel art").
// Each weapon is described with simple solids (boxes, cylinders, spheres) in millimeters,
// including carved details (vent holes, bores, grooves), then voxelized on a 4 mm grid and
// turned into a mesh of visible cube faces. Proportions follow the real-world props and
// the Star Wars references:
//  - Bryar pistol: Kyle Katarn's modified Bryar, drawn like the DL-44 family (Mauser C96
//    "broomhandle" base): box magazine ahead of the trigger, ribbed grip, top scope,
//    conical flash hider and the gold coil seen on the Dark Forces sprite.
//  - E-11: Sterling L2A3 base: perforated barrel jacket, side power pack (magazine),
//    Hengstler counter, T-tracks, scope on the left rail, folded stock on top.
//  - Thermal detonator: sphere with an equator band and the arming panel on top.
//  - Imperial repeater: front drum with a barrel cluster and gold band, long rod stock.
//  - Jeron fusion cutter: four barrels in a horizontal row (it fires all four in a line).
//  - Packered mortar gun: chrome cylindrical body, big bore with perforated shroud,
//    three rotating shell containers, slender stock with a flat butt plate.
//  - I've mine: flat disk with the orange spoke pattern of the pickup sprite.
//  - Stouker concussion rifle: long silver tube over a dark body, red concussion
//    chamber near the trigger, wooden stock.
//  - Assault cannon: heavy plasma launcher, thick barrel under a tall body.
// view projection struct in tfe_xr_hands.h
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include "tfe_xr_weapons.h"
#include "tfe_xr_mesh.h"

namespace
{
	enum ShapeType { SH_BOX, SH_CYLX, SH_CYLY, SH_CYLZ, SH_SPHERE };
	struct Shape
	{
		int type;
		float cx, cy, cz;      // center (mm). x right, y up, z back (forward is -z)
		float a, b, c;         // box: half sizes; cylinders: radius, (unused), half length; sphere: radius
		float tilt;            // rotation about X (rad) around the center
		uint8_t r, g, bl;
		bool emissive;
		bool carve;
		float noise;           // brightness variation 0..1
		uint32_t stripe;       // helical stripe color (0 = none), on cylinders along Z
		float period, width;   // stripe period / width along Z (mm)
	};

	struct Builder
	{
		std::vector<Shape> shapes;
		float k = 1.0f;   // model scale (the grip keeps its real size so it fits the fist)
		void add(int type, float cx, float cy, float cz, float a, float b, float c, uint32_t rgb, float tilt = 0.0f, bool emis = false, float noise = 0.10f)
		{
			Shape s = { type, cx * k, cy * k, cz * k, a * k, b * k, c * k, tilt, uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), emis, false, noise, 0, 0.0f, 0.0f };
			shapes.push_back(s);
		}
		// Cylinder along Z with diagonal (helical) stripes, like the hazard bands of the fusion cutter.
		void cylZStriped(float cx, float cy, float z0, float z1, float r, uint32_t rgb, uint32_t stripe, float period, float width)
		{
			add(SH_CYLZ, cx, cy, (z0 + z1) * 0.5f, r, 0.0f, fabsf(z1 - z0) * 0.5f, rgb);
			shapes.back().stripe = stripe; shapes.back().period = period * k; shapes.back().width = width * k;
		}
		void carve(int type, float cx, float cy, float cz, float a, float b, float c, float tilt = 0.0f)
		{
			Shape s = { type, cx * k, cy * k, cz * k, a * k, b * k, c * k, tilt, 0, 0, 0, false, true, 0.0f, 0, 0.0f, 0.0f };
			shapes.push_back(s);
		}
		// Box from min/max corners.
		void boxMM(float x0, float y0, float z0, float x1, float y1, float z1, uint32_t rgb, bool emis = false, float noise = 0.10f)
		{
			add(SH_BOX, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f, fabsf(x1 - x0) * 0.5f, fabsf(y1 - y0) * 0.5f, fabsf(z1 - z0) * 0.5f, rgb, 0.0f, emis, noise);
		}
		void cylZ(float cx, float cy, float z0, float z1, float r, uint32_t rgb, bool emis = false, float noise = 0.10f)
		{
			add(SH_CYLZ, cx, cy, (z0 + z1) * 0.5f, r, 0.0f, fabsf(z1 - z0) * 0.5f, rgb, 0.0f, emis, noise);
		}
		void carveCylZ(float cx, float cy, float z0, float z1, float r) { carve(SH_CYLZ, cx, cy, (z0 + z1) * 0.5f, r, 0.0f, fabsf(z1 - z0) * 0.5f); }
	};

	bool inside(const Shape& s, float x, float y, float z)
	{
		float px = x - s.cx, py = y - s.cy, pz = z - s.cz;
		if (s.tilt != 0.0f)
		{
			// Inverse rotation about X.
			const float c = cosf(-s.tilt), sn = sinf(-s.tilt);
			const float ny = py * c - pz * sn, nz = py * sn + pz * c;
			py = ny; pz = nz;
		}
		switch (s.type)
		{
			case SH_BOX: return fabsf(px) <= s.a && fabsf(py) <= s.b && fabsf(pz) <= s.c;
			case SH_CYLX: return fabsf(px) <= s.c && (py * py + pz * pz) <= s.a * s.a;
			case SH_CYLY: return fabsf(py) <= s.c && (px * px + pz * pz) <= s.a * s.a;
			case SH_CYLZ: return fabsf(pz) <= s.c && (px * px + py * py) <= s.a * s.a;
			case SH_SPHERE: return (px * px + py * py + pz * pz) <= s.a * s.a;
		}
		return false;
	}
	void bounds(const Shape& s, float mn[3], float mx[3])
	{
		float e = fmaxf(fmaxf(s.a, s.b), s.c);
		if (s.type == SH_SPHERE) e = s.a;
		const float ex = (s.type == SH_BOX) ? s.a : (s.type == SH_CYLX ? s.c : s.a);
		const float rad = (s.tilt != 0.0f) ? e * 1.42f : 0.0f;
		float ey = (s.type == SH_BOX) ? s.b : (s.type == SH_CYLY ? s.c : s.a);
		float ez = (s.type == SH_BOX) ? s.c : (s.type == SH_CYLZ ? s.c : s.a);
		if (rad > 0.0f) { ey = rad; ez = rad; }
		mn[0] = s.cx - ex; mx[0] = s.cx + ex;
		mn[1] = s.cy - ey; mx[1] = s.cy + ey;
		mn[2] = s.cz - ez; mx[2] = s.cz + ez;
	}

	uint32_t hash3(int a, int b, int c)
	{
		uint32_t h = 2166136261u;
		h = (h ^ uint32_t(a)) * 16777619u; h = (h ^ uint32_t(b)) * 16777619u; h = (h ^ uint32_t(c)) * 16777619u;
		h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
		return h;
	}

	// Colors (sRGB 0xRRGGBB).
	// Metals follow the first-person renders of the game: dark navy / blue-gray steel, glossy chrome.
	const uint32_t BLACK = 0x15171d, DARK = 0x1e2732, GUN = 0x343b4e, STEEL = 0x4c5368, SILVER = 0x8c93aa, CHROME = 0xd6dae6;
	const uint32_t GOLD = 0xc49a3a, BRASS = 0x9c7a32, WOOD = 0x4a2e1c, WOODD = 0x2e1c12, RED = 0xd02a1e, ORANGE = 0xf08a1e;
	const uint32_t TAN = 0x8a7a5c, OLIVE = 0x4c5640, GREEN = 0x3cff6a, BLUE = 0x4aa0ff, WHITE = 0xe8e8e8;

	// Pistol grip shared by most weapons (grip point = origin = center of the fist).
	void addGrip(Builder& b, uint32_t col, float tilt = -0.35f, bool ribs = true)
	{
		const float saved = b.k;
		b.k = 1.0f;
		struct Restore { Builder& b; float k; ~Restore() { b.k = k; } } restore = { b, saved };
		// Upper grip block that reaches the (scaled) receiver.
		if (saved > 1.0f) { b.add(SH_BOX, 0.0f, 30.0f * saved * 0.5f + 18.0f, 4.0f, 12.5f, 30.0f * saved * 0.5f, 16.0f, col); }
		b.add(SH_BOX, 0.0f, -8.0f, 8.0f, 12.5f, 44.0f, 17.0f, col, tilt);
		if (ribs)
		{
			for (int i = 0; i < 6; i++)
			{
				const float y = -40.0f + i * 11.0f;
				b.carve(SH_BOX, 13.0f, y, 8.0f + y * 0.36f, 2.0f, 2.0f, 13.0f, tilt);
				b.carve(SH_BOX, -13.0f, y, 8.0f + y * 0.36f, 2.0f, 2.0f, 13.0f, tilt);
			}
		}
	}
	void addTriggerGuard(Builder& b, float yTop, float zFront, float zBack, uint32_t col)
	{
		b.boxMM(-3.0f, yTop - 26.0f, zFront, 3.0f, yTop - 22.0f, zBack, col);      // bottom bar
		b.boxMM(-3.0f, yTop - 26.0f, zFront - 4.0f, 3.0f, yTop, zFront, col);        // front post
		b.boxMM(-2.0f, yTop - 16.0f, zFront + 10.0f, 2.0f, yTop, zFront + 14.0f, SILVER); // trigger
	}

	// ---------------------------------------------------------------
	void buildBryar(Builder& b, float muzzle[3])
	{
		const float by = 46.0f;   // bore height above the grip point
		// Frame / receiver (C96-like box action).
		b.boxMM(-11.0f, 24.0f, 42.0f, 11.0f, 62.0f, -62.0f, BLACK);
		b.boxMM(-12.0f, 34.0f, 10.0f, 12.0f, 54.0f, -50.0f, DARK);          // side plates
		b.carve(SH_BOX, 12.5f, 44.0f, -20.0f, 1.5f, 5.0f, 24.0f);              // milled side panels
		b.carve(SH_BOX, -12.5f, 44.0f, -20.0f, 1.5f, 5.0f, 24.0f);
		b.boxMM(-6.0f, 58.0f, 40.0f, 6.0f, 66.0f, 28.0f, STEEL);            // hammer
		// Box magazine ahead of the trigger guard.
		b.boxMM(-9.0f, -6.0f, -18.0f, 9.0f, 24.0f, -52.0f, BLACK);
		b.boxMM(-10.0f, -10.0f, -16.0f, 10.0f, -6.0f, -54.0f, STEEL);
		addTriggerGuard(b, 24.0f, -14.0f, 16.0f, BLACK);
		// Barrel, gold coil and conical flash hider.
		b.cylZ(0.0f, by, -62.0f, -160.0f, 8.0f, BLACK);
		for (int i = 0; i < 9; i++)
		{
			const float z0 = -66.0f - i * 5.0f;
			b.cylZ(0.0f, by, z0, z0 - 3.0f, 11.0f, GOLD, false, 0.15f);
		}
		b.cylZ(0.0f, by, -160.0f, -168.0f, 10.0f, STEEL);
		b.cylZ(0.0f, by, -168.0f, -184.0f, 12.5f, GUN);
		b.carveCylZ(0.0f, by, -170.0f, -186.0f, 6.0f);
		for (int i = 0; i < 4; i++)                                         // flash hider slots
		{
			const float a = i * 1.5708f + 0.785f;
			b.carve(SH_BOX, cosf(a) * 11.0f, by + sinf(a) * 11.0f, -178.0f, 2.0f, 2.0f, 5.0f);
		}
		// Scope with rings and a blue lens.
		b.cylZ(0.0f, by + 26.0f, -10.0f, -96.0f, 7.0f, BLACK);
		b.cylZ(0.0f, by + 26.0f, -96.0f, -104.0f, 9.0f, DARK);
		b.cylZ(0.0f, by + 26.0f, -104.0f, -106.0f, 6.0f, BLUE, true);
		b.boxMM(-5.0f, 60.0f, -20.0f, 5.0f, by + 22.0f, -30.0f, STEEL);
		b.boxMM(-5.0f, 60.0f, -70.0f, 5.0f, by + 22.0f, -80.0f, STEEL);
		// Power cell indicator.
		b.boxMM(-13.0f, 40.0f, 0.0f, -11.0f, 46.0f, -8.0f, RED, true);
		// Broomhandle grip (dark wood with grooves).
		addGrip(b, WOODD, -0.30f, true);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -186.0f;
	}

	void buildE11(Builder& b, float muzzle[3])
	{
		const float by = 44.0f, R = 17.0f;
		// Receiver tube and perforated barrel jacket (Sterling).
		b.cylZ(0.0f, by, 70.0f, -300.0f, R, BLACK, false, 0.08f);
		b.cylZ(0.0f, by, 70.0f, 80.0f, R + 1.0f, STEEL);                       // rear end cap
		b.cylZ(0.0f, by, 80.0f, 84.0f, R - 6.0f, SILVER);
		for (int row = 0; row < 8; row++)
		{
			const float z = -122.0f - row * 22.0f;
			for (int k = 0; k < 6; k++)
			{
				const float a = 0.524f + k * 1.047f;
				b.carve(SH_BOX, cosf(a) * R, by + sinf(a) * R, z, 6.0f, 6.0f, 6.0f);
			}
		}
		// Hollow jacket with the barrel inside, so the vent holes show depth.
		b.carveCylZ(0.0f, by, -110.0f, -298.0f, 12.5f);
		b.cylZ(0.0f, by, -100.0f, -306.0f, 6.0f, STEEL);
		b.cylZ(0.0f, by, -300.0f, -314.0f, 11.0f, GUN);                         // muzzle
		b.carveCylZ(0.0f, by, -306.0f, -316.0f, 5.0f);
		b.boxMM(-2.0f, by + R - 2.0f, -284.0f, 2.0f, by + R + 10.0f, -290.0f, GUN);   // front sight post
		b.boxMM(-6.0f, by + R - 2.0f, -282.0f, 6.0f, by + R + 3.0f, -292.0f, GUN);
		// Power pack (magazine) sticking out on the left, with its well.
		b.add(SH_CYLX, -24.0f, by - 2.0f, -70.0f, 11.0f, 0.0f, 8.0f, GUN);
		b.boxMM(-30.0f, by - 10.0f, -58.0f, -88.0f, by + 6.0f, -84.0f, BLACK);
		b.boxMM(-88.0f, by - 11.0f, -56.0f, -92.0f, by + 7.0f, -86.0f, STEEL);
		// Hengstler counter on the left of the receiver, with its silver digit window.
		b.boxMM(-14.0f, by - 14.0f, -2.0f, -34.0f, by + 12.0f, -34.0f, BLACK);
		b.boxMM(-34.0f, by - 4.0f, -8.0f, -35.0f, by + 4.0f, -28.0f, WHITE, false, 0.3f);
		// T-tracks on the top right.
		b.boxMM(6.0f, by + R - 2.0f, -60.0f, 14.0f, by + R + 3.0f, -190.0f, GUN);
		for (int i = 0; i < 6; i++) b.carve(SH_BOX, 10.0f, by + R + 3.0f, -70.0f - i * 22.0f, 5.0f, 2.0f, 4.0f);
		// Scope on the left rail (M38 style), rings and lenses.
		const float sx = -10.0f, sy = by + R + 20.0f;
		b.cylZ(sx, sy, -20.0f, -140.0f, 9.0f, BLACK);
		b.cylZ(sx, sy, -20.0f, -34.0f, 12.0f, DARK);
		b.cylZ(sx, sy, -126.0f, -140.0f, 12.0f, DARK);
		b.cylZ(sx, sy, -140.0f, -142.0f, 8.0f, BLUE, true);
		b.cylZ(sx, sy, -18.0f, -20.0f, 7.0f, 0x305080, false, 0.0f);
		b.boxMM(sx - 6.0f, by + R - 2.0f, -50.0f, sx + 6.0f, sy - 6.0f, -60.0f, STEEL);
		b.boxMM(sx - 6.0f, by + R - 2.0f, -100.0f, sx + 6.0f, sy - 6.0f, -110.0f, STEEL);
		// Cocking handle on the left, ejection port on the right.
		b.boxMM(-R, by - 2.0f, -6.0f, -R - 10.0f, by + 4.0f, -14.0f, STEEL);
		b.carve(SH_BOX, R, by + 6.0f, -30.0f, 3.0f, 6.0f, 14.0f);
		// Folded stock on top (two rods and the butt plate at the front).
		b.cylZ(8.0f, by + R + 6.0f, 76.0f, -116.0f, 3.0f, GUN);
		b.cylZ(-8.0f, by + R + 6.0f, 76.0f, -116.0f, 3.0f, GUN);
		b.boxMM(-20.0f, by + R + 2.0f, -116.0f, 20.0f, by + R + 16.0f, -124.0f, GUN);
		// Grip and trigger group.
		b.boxMM(-10.0f, 20.0f, 30.0f, 10.0f, by - R + 2.0f, -36.0f, BLACK);
		addTriggerGuard(b, 22.0f, -24.0f, 12.0f, BLACK);
		addGrip(b, BLACK, -0.35f, true);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -316.0f;
	}

	void buildThermal(Builder& b, float muzzle[3])
	{
		// Held in the closed fist: the sphere sits where the fingers close.
		const float cy = 6.0f, cz = -26.0f;
		b.add(SH_SPHERE, 0.0f, cy, cz, 31.0f, 0.0f, 0.0f, SILVER, 0.0f, false, 0.12f);
		b.add(SH_CYLY, 0.0f, cy, cz, 32.5f, 0.0f, 4.0f, GUN);                      // equator band
		for (int i = 0; i < 8; i++)
		{
			const float a = i * 0.785f;
			b.carve(SH_BOX, cosf(a) * 32.0f, cy, cz + sinf(a) * 32.0f, 3.0f, 2.5f, 3.0f);
		}
		b.boxMM(-14.0f, cy + 26.0f, cz - 8.0f, 14.0f, cy + 34.0f, cz + 8.0f, BLACK);   // arming panel
		b.boxMM(-10.0f, cy + 34.0f, cz - 2.0f, -6.0f, cy + 37.0f, cz + 2.0f, RED, true);
		b.boxMM(-2.0f, cy + 34.0f, cz - 2.0f, 2.0f, cy + 37.0f, cz + 2.0f, 0xffd040, true);
		b.boxMM(6.0f, cy + 34.0f, cz - 2.0f, 10.0f, cy + 37.0f, cz + 2.0f, GREEN, true);
		b.add(SH_CYLY, 0.0f, cy - 31.0f, cz, 6.0f, 0.0f, 3.0f, STEEL);              // plunger
		muzzle[0] = 0.0f; muzzle[1] = cy; muzzle[2] = cz - 40.0f;
	}

	void buildRepeater(Builder& b, float muzzle[3])
	{
		// In-game model: boxy dark body with two rows of brass studs, a big rounded front housing
		// around the barrel cluster, ribbed top; the pickup shows the gold band and the rod stock.
		const float by = 50.0f;
		b.boxMM(-24.0f, by - 26.0f, 40.0f, 24.0f, by + 24.0f, -120.0f, DARK, false, 0.12f);
		b.boxMM(-26.0f, by - 18.0f, 30.0f, 26.0f, by + 16.0f, -110.0f, GUN, false, 0.12f);
		for (int i = 0; i < 7; i++)                                               // brass studs
		{
			const float z = 20.0f - i * 18.0f;
			b.boxMM(26.0f, by + 6.0f, z, 29.0f, by + 11.0f, z - 5.0f, BRASS, false, 0.2f);
			b.boxMM(26.0f, by - 10.0f, z, 29.0f, by - 5.0f, z - 5.0f, BRASS, false, 0.2f);
			b.boxMM(-29.0f, by + 6.0f, z, -26.0f, by + 11.0f, z - 5.0f, BRASS, false, 0.2f);
		}
		for (int i = 0; i < 8; i++) b.boxMM(-18.0f, by + 24.0f, 30.0f - i * 18.0f, 18.0f, by + 30.0f, 22.0f - i * 18.0f, BLACK);   // ribbed top
		b.cylZ(0.0f, by, -110.0f, -230.0f, 34.0f, STEEL, false, 0.18f);           // rounded front housing
		b.cylZ(0.0f, by, -140.0f, -160.0f, 35.5f, GOLD, false, 0.15f);           // gold band
		b.cylZ(0.0f, by, -226.0f, -236.0f, 30.0f, GUN);
		for (int k = 0; k < 6; k++)                                              // barrel cluster
		{
			const float a = k * 1.047f;
			const float x = cosf(a) * 16.0f, y = by + sinf(a) * 16.0f;
			b.cylZ(x, y, -230.0f, -250.0f, 7.0f, BLACK);
			b.carveCylZ(x, y, -238.0f, -252.0f, 3.5f);
		}
		b.cylZ(0.0f, by, -230.0f, -244.0f, 8.0f, DARK);
		b.boxMM(26.0f, by - 2.0f, -60.0f, 28.0f, by + 4.0f, -90.0f, GREEN, true);  // charge light
		// Rod stock with butt plate.
		b.cylZ(0.0f, by + 4.0f, 40.0f, 170.0f, 5.0f, STEEL);
		b.boxMM(-14.0f, by - 36.0f, 168.0f, 14.0f, by + 16.0f, 178.0f, GUN);
		addTriggerGuard(b, 24.0f, -20.0f, 14.0f, DARK);
		addGrip(b, BLACK, -0.35f, true);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -252.0f;
	}

	void buildFusion(Builder& b, float muzzle[3])
	{
		// In-game model: four fat barrels side by side (it fires all four in a row), dark slate
		// with diagonal orange hazard stripes, rounded black tips, a breech block at the back.
		const float by = 46.0f, R = 14.0f;
		const float xs[4] = { -45.0f, -15.0f, 15.0f, 45.0f };
		for (int i = 0; i < 4; i++)
		{
			b.cylZStriped(xs[i], by, -20.0f, -190.0f, R, DARK, ORANGE, 46.0f, 15.0f);
			b.add(SH_SPHERE, xs[i], by, -192.0f, R + 2.0f, 0.0f, 0.0f, BLACK, 0.0f, false, 0.1f);
			b.carveCylZ(xs[i], by, -196.0f, -212.0f, 5.0f);
			b.cylZ(xs[i], by, -20.0f, -30.0f, R + 2.0f, GUN);
		}
		b.boxMM(-62.0f, by - 18.0f, 40.0f, 62.0f, by + 18.0f, -24.0f, GUN, false, 0.12f);   // breech block
		b.add(SH_SPHERE, -62.0f, by, 10.0f, 16.0f, 0.0f, 0.0f, DARK);                       // side caps
		b.add(SH_SPHERE, 62.0f, by, 10.0f, 16.0f, 0.0f, 0.0f, DARK);
		b.boxMM(-30.0f, by + 18.0f, 30.0f, 30.0f, by + 24.0f, -10.0f, BLACK);
		b.boxMM(56.0f, by + 8.0f, -4.0f, 60.0f, by + 14.0f, -12.0f, RED, true);
		addTriggerGuard(b, 24.0f, -16.0f, 14.0f, DARK);
		addGrip(b, BLACK, -0.35f, true);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -212.0f;
	}

	void buildMortar(Builder& b, float muzzle[3])
	{
		const float by = 56.0f;
		b.cylZ(0.0f, by, 20.0f, -170.0f, 30.0f, CHROME, false, 0.22f);             // chrome body
		b.cylZ(-22.0f, by + 20.0f, 0.0f, -120.0f, 20.0f, CHROME, false, 0.22f);   // chrome lobes (in-game model)
		b.cylZ(22.0f, by + 20.0f, 0.0f, -120.0f, 20.0f, CHROME, false, 0.22f);
		b.boxMM(-30.0f, by - 34.0f, 10.0f, 30.0f, by - 14.0f, -150.0f, DARK);     // dark lower housing
		for (int k = 0; k < 3; k++)                                               // shell containers
		{
			const float a = 1.5708f + k * 2.094f;
			const float x = cosf(a) * 30.0f, y = by + sinf(a) * 30.0f;
			b.cylZ(x, y, -50.0f, -140.0f, 11.0f, DARK);
			b.cylZ(x, y, -140.0f, -146.0f, 7.0f, BRASS);
			b.cylZ(x, y, -146.0f, -150.0f, 4.0f, RED, true);
		}
		b.cylZ(0.0f, by, -170.0f, -300.0f, 25.0f, STEEL);                          // perforated shroud
		for (int row = 0; row < 6; row++)
		{
			const float z = -184.0f - row * 19.0f;
			for (int k = 0; k < 8; k++)
			{
				const float a = k * 0.785f + (row & 1) * 0.39f;
				b.carve(SH_BOX, cosf(a) * 25.0f, by + sinf(a) * 25.0f, z, 4.5f, 4.5f, 4.5f);
			}
		}
		b.carveCylZ(0.0f, by, -176.0f, -296.0f, 21.0f);                            // hollow shroud
		b.cylZ(0.0f, by, -150.0f, -300.0f, 18.0f, DARK);                           // barrel inside it
		b.cylZ(0.0f, by, -300.0f, -312.0f, 27.0f, CHROME);
		b.carveCylZ(0.0f, by, -150.0f, -314.0f, 14.0f);                            // big bore
		b.cylZ(0.0f, by, -150.0f, -160.0f, 17.0f, BLACK);
		b.boxMM(-4.0f, by + 24.0f, -172.0f, 4.0f, by + 34.0f, -180.0f, DARK);      // sling hooks
		b.boxMM(-4.0f, by - 34.0f, -172.0f, 4.0f, by - 24.0f, -180.0f, DARK);
		b.boxMM(-16.0f, 22.0f, 30.0f, 16.0f, by - 28.0f, -30.0f, DARK);            // trigger housing
		// Slender stock ending in a flat plate.
		b.cylZ(0.0f, by - 6.0f, 20.0f, 180.0f, 6.0f, DARK);
		b.boxMM(-22.0f, by - 44.0f, 176.0f, 22.0f, by + 24.0f, 186.0f, GUN);
		addTriggerGuard(b, 24.0f, -20.0f, 14.0f, DARK);
		addGrip(b, DARK, -0.35f, true);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -314.0f;
	}

	void buildMine(Builder& b, float muzzle[3])
	{
		// As in the game's first-person render (clay1): a silver disk lying flat on the fist,
		// orange spokes on top, red/white hazard label and a small blue light.
		const float cy = 14.0f, cz = -24.0f;
		b.add(SH_CYLY, 0.0f, cy, cz, 46.0f, 0.0f, 8.0f, SILVER, 0.0f, false, 0.14f);
		b.add(SH_CYLY, 0.0f, cy + 8.0f, cz, 40.0f, 0.0f, 2.0f, CHROME, 0.0f, false, 0.12f);
		b.add(SH_CYLY, 0.0f, cy - 8.0f, cz, 42.0f, 0.0f, 2.0f, STEEL);
		for (int k = 0; k < 3; k++)
		{
			const float a = k * 2.094f + 1.5708f;
			for (int s = 0; s < 7; s++)
			{
				const float r = 8.0f + s * 5.0f;
				b.add(SH_BOX, cosf(a) * r, cy + 11.0f, cz + sinf(a) * r, 4.0f, 1.5f, 4.0f, ORANGE, 0.0f, false, 0.1f);
			}
		}
		b.add(SH_CYLY, 0.0f, cy + 12.0f, cz, 7.0f, 0.0f, 2.0f, DARK);
		b.add(SH_CYLY, 0.0f, cy + 13.5f, cz, 3.0f, 0.0f, 1.0f, RED, 0.0f, true);
		for (int i = 0; i < 6; i++)
		{
			b.add(SH_BOX, -30.0f + i * 5.0f, cy + 10.5f, cz + 22.0f, 2.5f, 1.0f, 3.0f, (i & 1) ? WHITE : RED, 0.0f, false, 0.05f);
		}
		b.add(SH_BOX, 22.0f, cy + 10.5f, cz + 20.0f, 2.0f, 1.0f, 2.0f, BLUE, 0.0f, true);
		muzzle[0] = 0.0f; muzzle[1] = cy; muzzle[2] = cz - 50.0f;
	}

	void buildConcussion(Builder& b, float muzzle[3])
	{
		const float by = 40.0f, ty = by + 24.0f;
		b.boxMM(-15.0f, by - 16.0f, 40.0f, 15.0f, by + 14.0f, -250.0f, BLACK, false, 0.08f);   // body
		b.cylZ(0.0f, ty, 30.0f, -390.0f, 11.0f, SILVER, false, 0.10f);                         // silver tube
		for (int i = 0; i < 6; i++) b.cylZ(0.0f, ty, -60.0f - i * 50.0f, -66.0f - i * 50.0f, 13.0f, STEEL);
		b.cylZ(0.0f, ty, -380.0f, -400.0f, 15.0f, GUN);
		b.carveCylZ(0.0f, ty, -390.0f, -402.0f, 7.0f);
		b.cylZ(0.0f, by, -250.0f, -330.0f, 8.0f, DARK);                                        // lower barrel
		// Red concussion chamber near the trigger.
		b.add(SH_CYLX, 0.0f, by - 2.0f, -40.0f, 14.0f, 0.0f, 17.0f, RED, 0.0f, false, 0.15f);
		b.add(SH_CYLX, 0.0f, by - 2.0f, -40.0f, 8.0f, 0.0f, 18.5f, 0xff5a3a, 0.0f, true);
		for (int i = 0; i < 8; i++) b.carve(SH_BOX, 15.0f, by + 4.0f, -80.0f - i * 18.0f, 2.0f, 5.0f, 4.0f);
		b.boxMM(-4.0f, ty + 10.0f, -40.0f, 4.0f, ty + 18.0f, -48.0f, BRASS);                   // sight
		// Wooden stock.
		b.boxMM(-13.0f, by - 30.0f, 40.0f, 13.0f, by + 12.0f, 190.0f, WOOD, false, 0.14f);
		b.boxMM(-14.0f, by - 34.0f, 186.0f, 14.0f, by + 14.0f, 194.0f, WOODD);
		b.carve(SH_BOX, 0.0f, by - 20.0f, 110.0f, 14.0f, 6.0f, 40.0f);
		addTriggerGuard(b, 24.0f, -18.0f, 14.0f, BLACK);
		addGrip(b, WOOD, -0.35f, true);
		muzzle[0] = 0.0f; muzzle[1] = ty; muzzle[2] = -402.0f;
	}

	void buildCannon(Builder& b, float muzzle[3])
	{
		const float by = 34.0f;
		b.boxMM(-38.0f, by - 14.0f, 90.0f, 38.0f, by + 96.0f, -150.0f, GUN, false, 0.10f);    // tall body
		b.boxMM(-40.0f, by + 20.0f, 40.0f, 40.0f, by + 60.0f, -120.0f, DARK);
		b.boxMM(-39.0f, by + 64.0f, 80.0f, 39.0f, by + 80.0f, -140.0f, 0x3a1e10, false, 0.12f);  // rust-brown band
		for (int i = 0; i < 6; i++)                                                          // glowing vents
		{
			b.boxMM(40.0f, by + 28.0f + (i & 1) * 18.0f, 20.0f - i * 22.0f, 41.0f, by + 40.0f + (i & 1) * 18.0f, 8.0f - i * 22.0f, ORANGE, true);
			b.boxMM(-41.0f, by + 28.0f + (i & 1) * 18.0f, 20.0f - i * 22.0f, -40.0f, by + 40.0f + (i & 1) * 18.0f, 8.0f - i * 22.0f, ORANGE, true);
		}
		b.cylZ(0.0f, by, -150.0f, -420.0f, 20.0f, BLACK);                                     // heavy barrel
		for (int i = 0; i < 5; i++) b.cylZ(0.0f, by, -170.0f - i * 50.0f, -184.0f - i * 50.0f, 23.0f, STEEL);
		b.cylZ(0.0f, by, -420.0f, -440.0f, 25.0f, GUN);
		b.carveCylZ(0.0f, by, -424.0f, -442.0f, 12.0f);
		b.cylZ(0.0f, by + 60.0f, -150.0f, -250.0f, 12.0f, DARK);                               // missile tube
		b.carveCylZ(0.0f, by + 60.0f, -236.0f, -252.0f, 7.0f);
		b.boxMM(-8.0f, by + 96.0f, 40.0f, 8.0f, by + 110.0f, -90.0f, BLACK);                   // carry handle
		b.carve(SH_BOX, 0.0f, by + 100.0f, -25.0f, 9.0f, 5.0f, 50.0f);
		b.add(SH_CYLX, -40.0f, by + 70.0f, 40.0f, 18.0f, 0.0f, 6.0f, RED, 0.0f, true);         // energy cell cap
		addTriggerGuard(b, 20.0f, -20.0f, 14.0f, BLACK);
		addGrip(b, BLACK, -0.35f, true);
		muzzle[0] = 0.0f; muzzle[1] = by; muzzle[2] = -442.0f;
	}
}

// Builds weapon `id` (Dark Forces WeaponID 1..9) as a list of triangles with 10 floats per vertex
// (x y z u v r g b a emissive), meters, in the weapon frame with the grip point at `grip`.
static bool makeWeapon(int id, Builder& b, float muzzle[3], float* vs)
{
	// Scales chosen so the weapons read like the big first-person weapons of the game.
	static const float c_scale[10] = { 1.0f, 1.25f, 1.35f, 1.0f, 1.6f, 1.45f, 1.4f, 1.0f, 1.35f, 1.45f };
	b.k = c_scale[(id > 0 && id < 10) ? id : 0];
	*vs = 4.0f;
	switch (id)
	{
		case 1: buildBryar(b, muzzle); *vs = 3.5f; break;
		case 2: buildE11(b, muzzle); break;
		case 3: buildThermal(b, muzzle); *vs = 3.0f; break;
		case 4: buildRepeater(b, muzzle); break;
		case 5: buildFusion(b, muzzle); break;
		case 6: buildMortar(b, muzzle); break;
		case 7: buildMine(b, muzzle); *vs = 3.0f; break;
		case 8: buildConcussion(b, muzzle); break;
		case 9: buildCannon(b, muzzle); *vs = 4.5f; break;
		default: return false;
	}
	*vs *= fmaxf(1.0f, b.k * 0.85f);
	for (int i = 0; i < 3; i++) { muzzle[i] *= b.k; }
	return true;
}

// Surface color of the model at a point (mm): the last solid shape containing it, carves ignored.
static bool weaponColorAt(const Builder& b, float px, float py, float pz, uint32_t* rgb, bool* emis)
{
	bool found = false;
	for (const Shape& s : b.shapes)
	{
		if (s.carve || !inside(s, px, py, pz)) continue;
		uint32_t c = (uint32_t(s.r) << 16) | (uint32_t(s.g) << 8) | s.bl;
		if (s.stripe)
		{
			const float ang = atan2f(py - s.cy, px - s.cx);
			const float t = (pz - s.cz) + ang / 6.2831853f * s.period;
			float m = fmodf(t, s.period); if (m < 0.0f) m += s.period;
			if (m < s.width) { c = s.stripe; }
		}
		*rgb = c; *emis = s.emissive; found = true;
	}
	return found;
}

struct SmoothCtx { const Builder* b; float grip[3]; };
static void smoothMaterial(void* user, const float p[3], float rgb[3], float* spec, float* emis)
{
	const SmoothCtx* c = (const SmoothCtx*)user;
	const float mm[3] = { (p[0] - c->grip[0]) * 1000.0f, (p[1] - c->grip[1]) * 1000.0f, (p[2] - c->grip[2]) * 1000.0f };
	uint32_t col = 0x3a3f4c; bool em = false;
	if (!weaponColorAt(*c->b, mm[0], mm[1], mm[2], &col, &em))
	{
		// Just outside every solid (rounded edge): look a little further in.
		for (int i = 1; i <= 3 && !weaponColorAt(*c->b, mm[0] * (1.0f - 0.004f * i), mm[1], mm[2], &col, &em); i++) {}
	}
	rgb[0] = ((col >> 16) & 255) / 255.0f; rgb[1] = ((col >> 8) & 255) / 255.0f; rgb[2] = (col & 255) / 255.0f;
	const float mx = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2])), mn = fminf(rgb[0], fminf(rgb[1], rgb[2]));
	const float sat = mx > 0.01f ? (mx - mn) / mx : 0.0f;
	// Metals are grey: the brighter, the shinier (chrome). Colored parts (wood, paint, brass) less so.
	*spec = sat < 0.25f ? 0.35f + 0.6f * mx : (rgb[0] > rgb[2] + 0.2f && rgb[1] > rgb[2] + 0.1f ? 0.55f : 0.18f);
	*emis = em ? 1.0f : 0.0f;
}

// Smooth version: the same solids sampled on a fine grid, softened and meshed with surface nets.
bool weapons_buildSmooth(int id, std::vector<SVtx>& verts, std::vector<uint32_t>& indices, float muzzleOut[3], const float grip[3])
{
	Builder b;
	float muzzle[3] = { 0, 0, 0 }, vs;
	if (!makeWeapon(id, b, muzzle, &vs)) { return false; }
	float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
	for (const Shape& s : b.shapes)
	{
		if (s.carve) continue;
		float a[3], c[3]; bounds(s, a, c);
		for (int i = 0; i < 3; i++) { mn[i] = fminf(mn[i], a[i]); mx[i] = fmaxf(mx[i], c[i]); }
	}
	float res = 0.9f * fmaxf(1.0f, b.k * 0.8f);
	for (;;)
	{
		const double cells = double((mx[0] - mn[0]) / res + 4) * ((mx[1] - mn[1]) / res + 4) * ((mx[2] - mn[2]) / res + 4);
		if (cells < 3.5e6) break;
		res *= 1.1f;
	}
	for (int i = 0; i < 3; i++) { mn[i] -= 2.0f * res; mx[i] += 2.0f * res; }
	const int nx = int((mx[0] - mn[0]) / res) + 1, ny = int((mx[1] - mn[1]) / res) + 1, nz = int((mx[2] - mn[2]) / res) + 1;
	std::vector<uint8_t> occ(size_t(nx) * ny * nz, 0);
	auto oi = [&](int x, int y, int z) { return (size_t(z) * ny + y) * nx + x; };
	for (const Shape& s : b.shapes)
	{
		float a[3], c[3]; bounds(s, a, c);
		const int x0 = (int)fmaxf(0.0f, floorf((a[0] - mn[0]) / res)), x1 = (int)fminf(float(nx - 1), ceilf((c[0] - mn[0]) / res));
		const int y0 = (int)fmaxf(0.0f, floorf((a[1] - mn[1]) / res)), y1 = (int)fminf(float(ny - 1), ceilf((c[1] - mn[1]) / res));
		const int z0 = (int)fmaxf(0.0f, floorf((a[2] - mn[2]) / res)), z1 = (int)fminf(float(nz - 1), ceilf((c[2] - mn[2]) / res));
		for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
		{
			if (!inside(s, mn[0] + x * res, mn[1] + y * res, mn[2] + z * res)) continue;
			occ[oi(x, y, z)] = s.carve ? 0 : 1;
		}
	}
	// Soften with a [1 2 1] kernel along each axis (edges get a one-cell bevel).
	MeshField f;
	f.nx = nx; f.ny = ny; f.nz = nz; f.cell = res * 0.001f;
	f.origin[0] = mn[0] * 0.001f + grip[0]; f.origin[1] = mn[1] * 0.001f + grip[1]; f.origin[2] = mn[2] * 0.001f + grip[2];
	f.v.resize(occ.size());
	for (size_t i = 0; i < occ.size(); i++) { f.v[i] = occ[i]; }
	std::vector<float> tmp(f.v.size());
	for (int axis = 0; axis < 3; axis++)
	{
		const size_t step = axis == 0 ? 1 : (axis == 1 ? size_t(nx) : size_t(nx) * ny);
		const int n = axis == 0 ? nx : (axis == 1 ? ny : nz);
		for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)
		{
			const int c = axis == 0 ? x : (axis == 1 ? y : z);
			const size_t i = oi(x, y, z);
			const float a = c > 0 ? f.v[i - step] : 0.0f, bb = c < n - 1 ? f.v[i + step] : 0.0f;
			tmp[i] = (a + 2.0f * f.v[i] + bb) * 0.25f;
		}
		f.v.swap(tmp);
	}
	SmoothCtx ctx; ctx.b = &b; ctx.grip[0] = grip[0]; ctx.grip[1] = grip[1]; ctx.grip[2] = grip[2];
	verts.clear(); indices.clear();
	mesh_surfaceNets(f, 0.5f, smoothMaterial, &ctx, verts, indices);
	const float m = 0.001f;
	muzzleOut[0] = muzzle[0] * m + grip[0]; muzzleOut[1] = muzzle[1] * m + grip[1]; muzzleOut[2] = muzzle[2] * m + grip[2];
	return !indices.empty();
}

bool weapons_buildModel(int id, std::vector<float>& out, float muzzleOut[3], const float grip[3], const WeaponView* view, float detail)
{
	Builder b;
	float muzzle[3] = { 0, 0, 0 };
	float vs = 4.0f;   // voxel size, mm
	if (!makeWeapon(id, b, muzzle, &vs)) { return false; }
	vs /= detail;
	// Grid bounds from the solid (non carved) shapes.
	float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
	for (const Shape& s : b.shapes)
	{
		if (s.carve) continue;
		float a[3], c[3]; bounds(s, a, c);
		for (int i = 0; i < 3; i++) { mn[i] = fminf(mn[i], a[i]); mx[i] = fmaxf(mx[i], c[i]); }
	}
	if (view && view->rgba)
	{
		// Room for the parts added from the original art.
		const float margin = 14.0f * vs;
		for (int i = 0; i < 3; i++) { mn[i] -= margin; mx[i] += margin; }
	}
	const int nx = int((mx[0] - mn[0]) / vs) + 2, ny = int((mx[1] - mn[1]) / vs) + 2, nz = int((mx[2] - mn[2]) / vs) + 2;
	std::vector<uint32_t> grid(size_t(nx) * ny * nz, 0);   // 0 = empty, else 0xE0RRGGBB-ish (bit 24 = filled, bit 25 = emissive)
	auto at = [&](int x, int y, int z) -> uint32_t& { return grid[(size_t(z) * ny + y) * nx + x]; };
	for (const Shape& s : b.shapes)
	{
		float a[3], c[3]; bounds(s, a, c);
		const int x0 = (int)fmaxf(0.0f, floorf((a[0] - mn[0]) / vs)), x1 = (int)fminf(float(nx - 1), ceilf((c[0] - mn[0]) / vs));
		const int y0 = (int)fmaxf(0.0f, floorf((a[1] - mn[1]) / vs)), y1 = (int)fminf(float(ny - 1), ceilf((c[1] - mn[1]) / vs));
		const int z0 = (int)fmaxf(0.0f, floorf((a[2] - mn[2]) / vs)), z1 = (int)fminf(float(nz - 1), ceilf((c[2] - mn[2]) / vs));
		for (int z = z0; z <= z1; z++) for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
		{
			const float px = mn[0] + (x + 0.5f) * vs, py = mn[1] + (y + 0.5f) * vs, pz = mn[2] + (z + 0.5f) * vs;
			if (!inside(s, px, py, pz)) continue;
			if (s.carve) { at(x, y, z) = 0; continue; }
			// Pixel-art texture: per-voxel brightness steps.
			const uint32_t h = hash3(x, y, z);
			const float k = 1.0f + s.noise * (float(h & 255) / 255.0f - 0.5f) * 2.0f;
			float sr = s.r, sg = s.g, sb = s.bl;
			if (s.stripe)
			{
				const float ang = atan2f(py - s.cy, px - s.cx);
				const float t = (pz - s.cz) + ang / 6.2831853f * s.period;
				float m = fmodf(t, s.period); if (m < 0.0f) m += s.period;
				if (m < s.width) { sr = float((s.stripe >> 16) & 255); sg = float((s.stripe >> 8) & 255); sb = float(s.stripe & 255); }
			}
			const int r = (int)fminf(255.0f, sr * k), g = (int)fminf(255.0f, sg * k), bl = (int)fminf(255.0f, sb * k);
			at(x, y, z) = (1u << 24) | (s.emissive ? (1u << 25) : 0u) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(bl);
		}
	}
	// Original first-person art projected onto the model: seen from the pose of the 2D weapon on the
	// 320x200 screen, the visible voxels take the colors of the game's own sprite.
	if (view && view->rgba)
	{
		const float m0 = 0.001f, vsm = vs * m0;
		const float cy = cosf(view->pose[0]), sy = sinf(view->pose[0]), cp = cosf(view->pose[1]), sp = sinf(view->pose[1]);
		const float cr = cosf(view->pose[2]), sr = sinf(view->pose[2]);
		// R = Ry * Rx * Rz
		const float Ry[9] = { cy,0,sy, 0,1,0, -sy,0,cy }, Rx[9] = { 1,0,0, 0,cp,-sp, 0,sp,cp }, Rz[9] = { cr,-sr,0, sr,cr,0, 0,0,1 };
		float T[9], R[9];
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { T[i*3+j] = 0; for (int k = 0; k < 3; k++) T[i*3+j] += Ry[i*3+k] * Rx[k*3+j]; }
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { R[i*3+j] = 0; for (int k = 0; k < 3; k++) R[i*3+j] += T[i*3+k] * Rz[k*3+j]; }
		auto proj = [&](int x, int y, int z, float* sxo, float* syo) -> float
		{
			const float p[3] = { (mn[0] + (x + 0.5f) * vs) * m0 + grip[0], (mn[1] + (y + 0.5f) * vs) * m0 + grip[1], (mn[2] + (z + 0.5f) * vs) * m0 + grip[2] };
			const float q[3] = { R[0]*p[0]+R[1]*p[1]+R[2]*p[2] + view->pose[3], R[3]*p[0]+R[4]*p[1]+R[5]*p[2] + view->pose[4], R[6]*p[0]+R[7]*p[1]+R[8]*p[2] + view->pose[5] };
			const float d = -q[2];
			if (d < 0.02f) { return -1.0f; }
			*sxo = 160.0f + 160.0f * q[0] / d; *syo = 100.0f - 133.333f * q[1] / d;
			return d;
		};
		auto surf = [&](int x, int y, int z)
		{
			static const int n6[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
			for (int f = 0; f < 6; f++)
			{
				const int a = x + n6[f][0], b = y + n6[f][1], c = z + n6[f][2];
				if (a < 0 || b < 0 || c < 0 || a >= nx || b >= ny || c >= nz || !(at(a, b, c) & (1u << 24))) { return true; }
			}
			return false;
		};
		auto spritePx = [&](int px, int py, const unsigned char** out) -> int
		{
			// 0 = nothing, 1 = weapon pixel, 2 = forearm (skin: unknown, keep the model)
			const int ix = px - view->x0, iy = py - view->y0;
			if (ix < 0 || iy < 0 || ix >= view->w || iy >= view->h) { return 0; }
			const unsigned char* c = &view->rgba[(iy * view->w + ix) * 4];
			if (!c[3]) { return 0; }
			if (out) { *out = c; }
			return (c[0] > c[1] + 35 && c[0] > c[2] + 50) ? 2 : 1;
		};
		std::vector<float> zb(320 * 200, 1e9f);
		auto buildZ = [&]()
		{
			for (float& z : zb) { z = 1e9f; }
			for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)
			{
				if (!(at(x, y, z) & (1u << 24)) || !surf(x, y, z)) continue;
				float fx, fy; const float d = proj(x, y, z, &fx, &fy);
				if (d < 0.0f) continue;
				const int r = (int)(160.0f * vsm / d * 0.5f);
				const int cx = (int)floorf(fx + 0.5f), cyy = (int)floorf(fy + 0.5f);
				for (int yy = cyy - r; yy <= cyy + r; yy++) for (int xx = cx - r; xx <= cx + r; xx++)
				{
					if (xx < 0 || yy < 0 || xx >= 320 || yy >= 200) continue;
					float& zv = zb[yy * 320 + xx]; if (d < zv) zv = d;
				}
			}
		};
		// 1. Silhouette: on screen, the model keeps only what the original art shows (2 px tolerance).
		for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)
		{
			uint32_t& v = at(x, y, z);
			if (!(v & (1u << 24))) continue;
			float fx, fy; const float d = proj(x, y, z, &fx, &fy);
			if (d < 0.0f) continue;
			const int px = (int)floorf(fx), py = (int)floorf(fy);
			if (px < 2 || py < 2 || px >= 318 || py >= 198) continue;   // the art is cut by the screen edge there
			bool inside = false;
			for (int dy = -2; dy <= 2 && !inside; dy++) for (int dx = -2; dx <= 2 && !inside; dx++) { inside = spritePx(px + dx, py + dy, nullptr) != 0; }
			if (!inside) { v = 0; }
		}
		buildZ();
		// 2. Art the model does not cover: extruded at the depth of the nearest covered pixel.
		{
			const float invR[9] = { R[0], R[3], R[6], R[1], R[4], R[7], R[2], R[5], R[8] };
			const int maxR = 14;
			for (int py = 0; py < 200; py++) for (int px = 0; px < 320; px++)
			{
				const unsigned char* c = nullptr;
				if (spritePx(px, py, &c) != 1 || zb[py * 320 + px] < 1e8f) continue;
				float d = -1.0f;
				for (int r = 1; r <= maxR && d < 0.0f; r++)
				{
					for (int dy = -r; dy <= r && d < 0.0f; dy++) for (int dx = -r; dx <= r; dx++)
					{
						if (abs(dx) != r && abs(dy) != r) continue;
						const int qx = px + dx, qy = py + dy;
						if (qx < 0 || qy < 0 || qx >= 320 || qy >= 200) continue;
						if (zb[qy * 320 + qx] < 1e8f) { d = zb[qy * 320 + qx]; break; }
					}
				}
				if (d < 0.0f) continue;
				for (int layer = 0; layer < 3; layer++)
				{
					const float dd = d + layer * vsm;
					const float X = (px + 0.5f - 160.0f) * dd / 160.0f, Y = -(py + 0.5f - 100.0f) * dd / 133.333f, Z = -dd;
					const float qv[3] = { X - view->pose[3], Y - view->pose[4], Z - view->pose[5] };
					const float pm[3] = { invR[0]*qv[0]+invR[1]*qv[1]+invR[2]*qv[2], invR[3]*qv[0]+invR[4]*qv[1]+invR[5]*qv[2], invR[6]*qv[0]+invR[7]*qv[1]+invR[8]*qv[2] };
					const int gx = (int)floorf(((pm[0] - grip[0]) / m0 - mn[0]) / vs), gy = (int)floorf(((pm[1] - grip[1]) / m0 - mn[1]) / vs), gz = (int)floorf(((pm[2] - grip[2]) / m0 - mn[2]) / vs);
					if (gx < 0 || gy < 0 || gz < 0 || gx >= nx || gy >= ny || gz >= nz) continue;
					uint32_t& v = at(gx, gy, gz);
					if (!(v & (1u << 24))) { v = (1u << 24) | (1u << 26) | (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | uint32_t(c[2]); }
				}
			}
		}
		buildZ();
		// 3. Colors: every voxel seen from that view takes the color of its pixel.
		for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)
		{
			uint32_t& v = at(x, y, z);
			if (!(v & (1u << 24)) || (v & (1u << 25))) continue;
			float fx, fy; const float d = proj(x, y, z, &fx, &fy);
			if (d < 0.0f) continue;
			const int px = (int)floorf(fx), py = (int)floorf(fy);
			if (px < 0 || py < 0 || px >= 320 || py >= 200) continue;
			if (d > zb[py * 320 + px] + vsm * 1.8f) continue;          // hidden from that view
			// Average of the art under the voxel's footprint (a voxel covers a few pixels up close).
			const int r = (int)(160.0f * vsm / d * 0.5f);
			int sum[3] = { 0, 0, 0 }, n = 0;
			for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++)
			{
				const unsigned char* c = nullptr;
				if (spritePx(px + dx, py + dy, &c) != 1) continue;
				sum[0] += c[0]; sum[1] += c[1]; sum[2] += c[2]; n++;
			}
			if (!n) continue;
			v = (1u << 24) | (1u << 26) | (uint32_t(sum[0] / n) << 16) | (uint32_t(sum[1] / n) << 8) | uint32_t(sum[2] / n);
		}
	}
	// Faces of filled voxels next to empty ones.
	static const int nb[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
	static const float shade[6] = { 0.82f, 0.82f, 1.00f, 0.55f, 0.72f, 0.92f };
	auto filled = [&](int x, int y, int z) { return x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz && (at(x, y, z) & (1u << 24)); };
	const float m = 0.001f;
	out.clear();
	for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)
	{
		const uint32_t v = at(x, y, z);
		if (!(v & (1u << 24))) continue;
		const bool emis = (v & (1u << 25)) != 0;
		const float cr = ((v >> 16) & 255) / 255.0f, cg = ((v >> 8) & 255) / 255.0f, cb = (v & 255) / 255.0f;
		const float p0[3] = { mn[0] + x * vs, mn[1] + y * vs, mn[2] + z * vs };
		for (int f = 0; f < 6; f++)
		{
			if (filled(x + nb[f][0], y + nb[f][1], z + nb[f][2])) continue;
			const int axis = f / 2, side = (f & 1) ? 0 : 1;
			const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
			float q[4][3];
			const int uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
			for (int i = 0; i < 4; i++)
			{
				q[i][axis] = p0[axis] + side * vs;
				q[i][ua] = p0[ua] + uv[i][0] * vs;
				q[i][va] = p0[va] + uv[i][1] * vs;
			}
			const float sh = emis ? 1.0f : ((v & (1u << 26)) ? 0.8f + 0.2f * shade[f] : shade[f]);
			const int idx[6] = { 0, 1, 2, 0, 2, 3 };
			for (int t = 0; t < 6; t++)
			{
				const float* w = q[idx[t]];
				const float vtx[10] = { w[0] * m + grip[0], w[1] * m + grip[1], w[2] * m + grip[2], 0.5f, 0.5f, cr * sh, cg * sh, cb * sh, 1.0f, emis ? 1.0f : 0.0f };
				out.insert(out.end(), vtx, vtx + 10);
			}
		}
	}
	muzzleOut[0] = muzzle[0] * m + grip[0]; muzzleOut[1] = muzzle[1] * m + grip[1]; muzzleOut[2] = muzzle[2] * m + grip[2];
	return !out.empty();
}
