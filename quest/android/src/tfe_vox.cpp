// Voxel replacements for Dark Forces sprites (Dark Forces Voxel Pack by Dzierzan, fan-made,
// non-commercial, see Voxels/LICENSE.txt). MagicaVoxel models whose colours are Dark Forces palette
// indices are turned into flat shaded 3D models (greedy meshed) and drawn by the GPU model renderer
// in place of the sprite billboard: same position, facing the object's yaw, lit by the sector.
#include "tfe_vox.h"
#include <TFE_System/system.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_Asset/spriteAsset_Jedi.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Level/rwall.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Level/levelData.h>
#include <tfe_xr.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>

namespace TFE_Jedi { void computeTransform3x3(fixed16_16* transform, angle14_32 yaw, angle14_32 pitch, angle14_32 roll); }
using namespace TFE_Jedi;

namespace
{
	struct VoxGrid { int sx = 0, sy = 0, sz = 0; std::vector<unsigned char> c; };

	// One sprite (wax or frame) and its voxel variants.
	struct VoxSprite
	{
		bool enemy = false;
		JediModel* base = nullptr;
		JediModel* damaged = nullptr;
		JediModel* attack = nullptr;        // _ATTACK
		JediModel* melee = nullptr;         // _MELEE_ATTACK
		JediModel* range[2] = {};           // _RANGE_ATTACK_1/2
		JediModel* pain = nullptr;
		std::vector<JediModel*> letters;    // _A, _B, ... (animation frames)
		std::vector<JediModel*> numbers;    // _01, _02, ... (walk frames)
	};

	bool s_indexLoaded = false;
	std::set<std::string> s_stems;                         // available voxel files (upper case stems)
	std::unordered_map<const void*, VoxSprite*> s_sprites; // sprite pointer -> voxels (null: none)
	std::vector<JediModel*> s_models;                      // every voxel model built (for the GPU loader)
	std::map<std::string, VoxGrid> s_grids;                // parsed files

	std::string upper(std::string s) { for (char& ch : s) { ch = (char)toupper((unsigned char)ch); } return s; }

	void loadIndex()
	{
		if (s_indexLoaded) { return; }
		s_indexLoaded = true;
		char path[TFE_MAX_PATH];
		snprintf(path, sizeof(path), "%sVoxels/index.txt", TFE_Paths::getPath(PATH_PROGRAM));
		FILE* f = fopen(path, "rb");
		if (!f) { TFE_System::logWrite(LOG_WARNING, "Voxels", "no voxel index at %s", path); return; }
		char line[256];
		while (fgets(line, sizeof(line), f))
		{
			std::string s = line;
			while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) { s.pop_back(); }
			if (!s.empty()) { s_stems.insert(upper(s)); }
		}
		fclose(f);
		TFE_System::logWrite(LOG_MSG, "Voxels", "%d voxel models available", (int)s_stems.size());
	}

	bool readVox(const std::string& stem, VoxGrid& g)
	{
		auto it = s_grids.find(stem);
		if (it != s_grids.end()) { g = it->second; return g.sx > 0; }
		char path[TFE_MAX_PATH];
		snprintf(path, sizeof(path), "%sVoxels/%s.vox", TFE_Paths::getPath(PATH_PROGRAM), stem.c_str());
		FILE* f = fopen(path, "rb");
		VoxGrid out;
		if (f)
		{
			fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
			std::vector<unsigned char> d((size_t)(n > 0 ? n : 0));
			if (n > 0 && fread(d.data(), 1, (size_t)n, f) == (size_t)n && n > 20 && !memcmp(d.data(), "VOX ", 4))
			{
				auto u32at = [&](size_t o) { unsigned v; memcpy(&v, &d[o], 4); return v; };
				size_t pos = 8 + 12;   // skip MAIN header
				bool haveSize = false;
				while (pos + 12 <= d.size())
				{
					const char* id = (const char*)&d[pos];
					const unsigned cn = u32at(pos + 4), cm = u32at(pos + 8);
					const size_t body = pos + 12;
					if (body + cn > d.size()) { break; }
					if (!memcmp(id, "SIZE", 4) && !haveSize)
					{
						out.sx = (int)u32at(body); out.sy = (int)u32at(body + 4); out.sz = (int)u32at(body + 8);
						if (out.sx <= 0 || out.sy <= 0 || out.sz <= 0 || out.sx > 256 || out.sy > 256 || out.sz > 256) { out.sx = 0; break; }
						out.c.assign((size_t)out.sx * out.sy * out.sz, 0);
						haveSize = true;
					}
					else if (!memcmp(id, "XYZI", 4) && haveSize && out.sx > 0)
					{
						const unsigned count = u32at(body);
						for (unsigned i = 0; i < count && body + 4 + i * 4 + 4 <= d.size(); i++)
						{
							const unsigned char* v = &d[body + 4 + i * 4];
							if (v[0] < out.sx && v[1] < out.sy && v[2] < out.sz) { out.c[((size_t)v[2] * out.sy + v[1]) * out.sx + v[0]] = v[3]; }
						}
						break;   // first model only
					}
					pos = body + cn + cm;
				}
			}
			fclose(f);
		}
		s_grids[stem] = out;
		g = out;
		return out.sx > 0;
	}

	struct Quad { float p[4][3]; float n[3]; unsigned char color; };

	// Greedy meshing of the visible faces (same colour, same plane).
	// mode 0: sprite (centred, base at offY); 1: wall piece, thin along y (front = low y);
	// 2: wall piece, thin along z (front = high z, texture up = +y). Wall pieces: x = texture u, back on the wall.
	void meshGrid(const VoxGrid& g, std::vector<Quad>& quads, float s, float offY, int mode = 0)
	{
		const int dims[3] = { g.sx, g.sy, g.sz };
		auto at = [&](int x, int y, int z) -> unsigned char {
			if (x < 0 || y < 0 || z < 0 || x >= g.sx || y >= g.sy || z >= g.sz) { return 0; }
			return g.c[((size_t)z * g.sy + y) * g.sx + x];
		};
		// bounds of the occupied voxels
		int mn[3] = { 1 << 20, 1 << 20, 1 << 20 }, mx[3] = { -1, -1, -1 };
		for (int z = 0; z < g.sz; z++) for (int y = 0; y < g.sy; y++) for (int x = 0; x < g.sx; x++)
		{
			if (!at(x, y, z)) { continue; }
			const int q[3] = { x, y, z };
			for (int k = 0; k < 3; k++) { if (q[k] < mn[k]) mn[k] = q[k]; if (q[k] + 1 > mx[k]) mx[k] = q[k] + 1; }
		}
		if (mx[0] < 0) { return; }
		const float cx = 0.5f * (mn[0] + mx[0]), cy = 0.5f * (mn[1] + mx[1]), z0 = (float)mn[2];
		// voxel space (x right as seen from the front, y away from the viewer, z up) -> model space
		// (DF: x, y down, z forward = the object's facing). Front of the voxel model = low y.
		const float yBack = (float)mx[1], zBack = (float)mn[2];
		auto toModel = [&](const float v[3], float o[3]) {
			if (mode == 1)
			{
				// wall frame: x along the wall (texture u), y down, z out of the wall into the room
				o[0] = -v[0] * s; o[1] = -v[2] * s; o[2] = (yBack - v[1]) * s;
			}
			else if (mode == 2)
			{
				o[0] = -v[0] * s; o[1] = -v[1] * s; o[2] = (v[2] - zBack) * s;
			}
			else
			{
				o[0] = -(v[0] - cx) * s;
				o[2] = -(v[1] - cy) * s;
				o[1] = offY - (v[2] - z0) * s;
			}
		};
		std::vector<unsigned char> mask;
		for (int d = 0; d < 3; d++)
		{
			const int u = (d + 1) % 3, v = (d + 2) % 3;
			mask.assign((size_t)dims[u] * dims[v], 0);
			for (int dir = -1; dir <= 1; dir += 2)
			{
				for (int i = 0; i < dims[d]; i++)
				{
					for (int b = 0; b < dims[v]; b++)
					{
						for (int a = 0; a < dims[u]; a++)
						{
							int q[3]; q[d] = i; q[u] = a; q[v] = b;
							const unsigned char c = at(q[0], q[1], q[2]);
							int r[3] = { q[0], q[1], q[2] }; r[d] += dir;
							mask[(size_t)b * dims[u] + a] = (c && !at(r[0], r[1], r[2])) ? c : 0;
						}
					}
					for (int b = 0; b < dims[v]; b++)
					{
						for (int a = 0; a < dims[u];)
						{
							const unsigned char c = mask[(size_t)b * dims[u] + a];
							if (!c) { a++; continue; }
							int w = 1;
							while (a + w < dims[u] && mask[(size_t)b * dims[u] + a + w] == c) { w++; }
							int h = 1;
							for (; b + h < dims[v]; h++)
							{
								bool ok = true;
								for (int k = 0; k < w; k++) { if (mask[(size_t)(b + h) * dims[u] + a + k] != c) { ok = false; break; } }
								if (!ok) { break; }
							}
							for (int hh = 0; hh < h; hh++) for (int k = 0; k < w; k++) { mask[(size_t)(b + hh) * dims[u] + a + k] = 0; }
							// corners in voxel space
							const float plane = (float)(i + (dir > 0 ? 1 : 0));
							float corner[4][3];
							const int du[4] = { 0, w, w, 0 }, dv[4] = { 0, 0, h, h };
							for (int k = 0; k < 4; k++)
							{
								corner[k][d] = plane; corner[k][u] = (float)(a + du[k]); corner[k][v] = (float)(b + dv[k]);
							}
							Quad qd;
							for (int k = 0; k < 4; k++) { toModel(corner[k], qd.p[k]); }
							float nv[3] = { 0, 0, 0 }; nv[d] = (float)dir;
							{
								// normal through the same (linear) mapping
								const float z3[3] = { 0, 0, 0 };
								float o0[3], o1[3];
								toModel(z3, o0); toModel(nv, o1);
								float nn[3] = { o1[0] - o0[0], o1[1] - o0[1], o1[2] - o0[2] };
								const float l = sqrtf(nn[0]*nn[0] + nn[1]*nn[1] + nn[2]*nn[2]);
								qd.n[0] = nn[0] / l; qd.n[1] = nn[1] / l; qd.n[2] = nn[2] / l;
							}
							// Dark Forces winding: cross(b - a, c - a) points into the model.
							const float e1[3] = { qd.p[1][0] - qd.p[0][0], qd.p[1][1] - qd.p[0][1], qd.p[1][2] - qd.p[0][2] };
							const float e2[3] = { qd.p[2][0] - qd.p[0][0], qd.p[2][1] - qd.p[0][1], qd.p[2][2] - qd.p[0][2] };
							const float cr[3] = { e1[1]*e2[2] - e1[2]*e2[1], e1[2]*e2[0] - e1[0]*e2[2], e1[0]*e2[1] - e1[1]*e2[0] };
							if (cr[0]*qd.n[0] + cr[1]*qd.n[1] + cr[2]*qd.n[2] > 0.0f)
							{
								float t[3]; memcpy(t, qd.p[1], sizeof(t)); memcpy(qd.p[1], qd.p[3], sizeof(t)); memcpy(qd.p[3], t, sizeof(t));
							}
							qd.color = (unsigned char)(c - 1);   // MagicaVoxel colour c = palette entry c-1 (Dark Forces palette index)
							quads.push_back(qd);
							a += w;
						}
					}
				}
			}
		}
	}

	std::map<std::string, JediModel*> s_modelCache;   // stem + scale + offset -> model

	JediModel* buildModel(const std::string& stem, float s, float offY, int mode = 0)
	{
		char key[192];
		snprintf(key, sizeof(key), "%s|%.4f|%.3f|%d", stem.c_str(), s, offY, mode);
		auto cached = s_modelCache.find(key);
		if (cached != s_modelCache.end()) { return cached->second; }
		s_modelCache[key] = nullptr;
		VoxGrid g;
		if (!readVox(stem, g)) { return nullptr; }
		std::vector<Quad> quads;
		meshGrid(g, quads, s, offY, mode);
		if (quads.empty()) { return nullptr; }
		JediModel* m = (JediModel*)calloc(1, sizeof(JediModel));
		m->vertexCount = (s32)quads.size() * 4;
		m->vertices = (vec3*)malloc(sizeof(vec3) * m->vertexCount);
		m->polygonCount = (s32)quads.size();
		m->polygons = (JmPolygon*)calloc(m->polygonCount, sizeof(JmPolygon));
		m->polygonNormals = (vec3*)malloc(sizeof(vec3) * m->polygonCount);
		s32* idx = (s32*)malloc(sizeof(s32) * m->vertexCount);
		f32 radius = 0.0f;
		for (size_t q = 0; q < quads.size(); q++)
		{
			for (int k = 0; k < 4; k++)
			{
				vec3& v = m->vertices[q * 4 + k];
				v.x = floatToFixed16(quads[q].p[k][0]); v.y = floatToFixed16(quads[q].p[k][1]); v.z = floatToFixed16(quads[q].p[k][2]);
				idx[q * 4 + k] = (s32)(q * 4 + k);
				const f32 r = sqrtf(quads[q].p[k][0]*quads[q].p[k][0] + quads[q].p[k][1]*quads[q].p[k][1] + quads[q].p[k][2]*quads[q].p[k][2]);
				if (r > radius) { radius = r; }
			}
			JmPolygon& p = m->polygons[q];
			p.index = (s32)q;
			p.shading = PSHADE_FLAT;
			p.color = quads[q].color;
			p.vertexCount = 4;
			p.indices = &idx[q * 4];
			// Voxel models keep unit normals (not the offset form of the 3DO loader).
			m->polygonNormals[q] = { floatToFixed16(quads[q].n[0]), floatToFixed16(quads[q].n[1]), floatToFixed16(quads[q].n[2]) };
		}
		m->flags = MFLAG_VOXEL;
		m->radius = floatToFixed16(radius);
		s_models.push_back(m);
		s_modelCache[key] = m;
		return m;
	}

	s32 animFrameCount(const Wax* wax, s32 anim)
	{
		const WaxAnim* a = WAX_AnimPtr(wax, anim & 31);
		if (!a) { return 0; }
		const WaxView* v = WAX_ViewPtr(wax, a, 0);
		if (!v) { return 0; }
		s32 n = 0;
		for (s32 i = 0; i < 32; i++) { if (v->frameOffsets[i]) { n = i + 1; } }
		return n;
	}

	const WaxFrame* firstFrame(const Wax* wax)
	{
		const WaxAnim* a = WAX_AnimPtr(wax, 0);
		if (!a) { return nullptr; }
		const WaxView* v = WAX_ViewPtr(wax, a, 0);
		if (!v) { return nullptr; }
		return WAX_FramePtr(wax, v, 0);
	}

	VoxSprite* buildSprite(const char* fileName, const void* base, const WaxFrame* frame)
	{
		loadIndex();
		if (!fileName || !frame) { return nullptr; }
		std::string name = upper(fileName);
		const size_t dot = name.find('.');
		if (dot != std::string::npos) { name = name.substr(0, dot); }
		// scale: world units per sprite pixel; the voxel models are made at the sprite's pixel size.
		const WaxCell* cell = WAX_CellPtr(base, frame);
		if (!cell || cell->sizeY <= 0) { return nullptr; }
		const f32 s = fixed16ToFloat(frame->heightWS) / f32(cell->sizeY);
		const f32 offY = fixed16ToFloat(frame->offsetY);   // sprite bottom relative to the object (y down)
		if (s <= 0.0f || s > 2.0f) { return nullptr; }

		VoxSprite* vs = new VoxSprite();
		auto get = [&](const std::string& stem) -> JediModel* {
			return s_stems.count(stem) ? buildModel(stem, s, offY) : nullptr;
		};
		vs->base = get(name);
		vs->damaged = get(name + "_DAMAGED");
		vs->attack = get(name + "_ATTACK");
		vs->melee = get(name + "_MELEE_ATTACK");
		vs->range[0] = get(name + "_RANGE_ATTACK_1");
		vs->range[1] = get(name + "_RANGE_ATTACK_2");
		vs->pain = get(name + "_PAIN");
		for (char L = 'A'; L <= 'Z'; L++)
		{
			JediModel* m = get(name + "_" + std::string(1, L));
			if (!m) { break; }
			vs->letters.push_back(m);
		}
		for (int n = 1; n <= 40; n++)
		{
			char num[8]; snprintf(num, sizeof(num), "_%02d", n);
			JediModel* m = get(name + num);
			if (!m) { break; }
			vs->numbers.push_back(m);
		}
		vs->enemy = vs->attack || vs->melee || vs->pain || !vs->numbers.empty() || name == "REMOTE";
		if (!vs->base && vs->letters.empty() && vs->numbers.empty())
		{
			delete vs;
			return nullptr;
		}
		TFE_System::logWrite(LOG_MSG, "Voxels", "%s: voxel model (%d frames)", fileName, (int)(vs->letters.size() + vs->numbers.size() + (vs->base ? 1 : 0)));
		return vs;
	}

	JediModel* pick(const std::vector<JediModel*>& list, s32 frame, s32 frameCount)
	{
		if (list.empty()) { return nullptr; }
		if (frameCount <= 0) { frameCount = 1; }
		s32 i = (s32)((s64)(frame % frameCount) * (s32)list.size() / frameCount);
		if (i < 0) { i = 0; }
		if (i >= (s32)list.size()) { i = (s32)list.size() - 1; }
		return list[i];
	}
}

namespace
{
	// Voxel wall pieces (switches, computer panels) for the sign textures of the level.
	struct VoxWall
	{
		std::vector<JediModel*> frames;   // one per texture animation frame (or a single one)
	};
	std::unordered_map<const void*, VoxWall*> s_walls;   // sign texture slot (TextureData**) -> voxels

	JediModel* wallModel(const std::string& stem, s32 texW)
	{
		VoxGrid g;
		if (!readVox(stem, g)) { return nullptr; }
		if (g.sx != texW) { return nullptr; }   // the voxel must be as wide as the texture
		const int mode = (g.sz <= g.sy) ? 2 : 1;  // the thin axis is the depth
		return buildModel(stem, 1.0f / 8.0f, 0.0f, mode);
	}

	void prepareWalls()
	{
		for (auto& it : s_walls) { delete it.second; }
		s_walls.clear();
		const s32 count = s_levelState.textureCount;
		if (!s_levelState.textures || count <= 0) { return; }
		for (s32 i = 0; i < count; i++)
		{
			TextureData* base = s_levelState.textures[i + count];   // the unmodified texture of this slot
			if (!base) { continue; }
			s32 index; AssetPool pool;
			if (!bitmap_getTextureIndex(base, &index, &pool)) { continue; }
			const char* nm = bitmap_getTextureName(index, pool);
			if (!nm) { continue; }
			std::string name = upper(nm);
			const size_t dot = name.find('.');
			if (dot != std::string::npos) { name = name.substr(0, dot); }
			if (!s_stems.count(name) && !s_stems.count(name + "_A")) { continue; }
			TextureData* cur = s_levelState.textures[i];
			if (!cur) { continue; }
			const AnimatedTexture* anim = (const AnimatedTexture*)cur->animPtr;
			const s32 nFrames = anim ? anim->count : 1;
			VoxWall* vw = new VoxWall();
			if (s_stems.count(name + "_A"))
			{
				for (char L = 'A'; L <= 'Z' && (s32)vw->frames.size() < nFrames; L++)
				{
					if (!s_stems.count(name + "_" + std::string(1, L))) { break; }
					vw->frames.push_back(wallModel(name + "_" + std::string(1, L), cur->width));
				}
			}
			else { vw->frames.push_back(wallModel(name, cur->width)); }
			// Only textures whose frames are the voxel states (stacked switch strips are left as they are).
			bool ok = !vw->frames.empty() && ((s32)vw->frames.size() == nFrames || nFrames == 1);
			for (JediModel* m : vw->frames) { if (!m) { ok = false; } }
			if (!ok) { delete vw; continue; }
			s_walls[&s_levelState.textures[i]] = vw;
			TFE_System::logWrite(LOG_MSG, "Voxels", "%s: voxel wall piece (%d frames)", nm, (int)vw->frames.size());
		}
	}

}

JediModel* vox_wallModel(RWall* wall, s32 transform[9], f32 pos[3], JBool* fullbright)
{
	if (!wall || !wall->signTex || !*wall->signTex || !tfe_xr_options()->voxels || s_walls.empty()) { return nullptr; }
	auto it = s_walls.find(wall->signTex);
	if (it == s_walls.end()) { return nullptr; }
	const VoxWall& vw = *it->second;
	const TextureData* cur = *wall->signTex;
	const s32 frame = cur->animPtr ? cur->frameIdx : 0;
	JediModel* m = vw.frames[(frame >= 0 && frame < (s32)vw.frames.size()) ? frame : 0];
	if (!m || !m->drawId) { return nullptr; }

	// Where the renderer puts the sign (see the sign parts of the wall shader).
	RSector* sec = wall->sector;
	RSector* next = wall->nextSector;
	f32 texBase, partOffU;
	if ((wall->drawFlags & WDF_BOT) && next && !(next->flags1 & SEC_FLAGS1_EXT_FLOOR_ADJ))
	{
		texBase = fixed16ToFloat(sec->floorHeight); partOffU = fixed16ToFloat(wall->botOffset.x);
	}
	else if ((wall->drawFlags & WDF_TOP) && next && !(next->flags1 & SEC_FLAGS1_EXT_ADJ))
	{
		texBase = fixed16ToFloat(next->ceilingHeight); partOffU = fixed16ToFloat(wall->topOffset.x);
	}
	else if (wall->midTex && wall->drawFlags == WDF_MIDDLE && !next)
	{
		texBase = fixed16ToFloat(sec->floorHeight); partOffU = fixed16ToFloat(wall->midOffset.x);
	}
	else { return nullptr; }
	const f32 offU = partOffU - fixed16ToFloat(wall->signOffset.x);
	const f32 offV = fixed16ToFloat(wall->signOffset.z);
	const f32 x0 = fixed16ToFloat(wall->w0->x), z0 = fixed16ToFloat(wall->w0->z);
	const f32 dx = fixed16ToFloat(wall->w1->x) - x0, dz = fixed16ToFloat(wall->w1->z) - z0;
	const f32 geo = sqrtf(dx*dx + dz*dz);
	if (geo < 0.01f) { return nullptr; }
	const f32 k = fixed16ToFloat(wall->length) / geo;   // texels per unit along the wall / 8
	if (k <= 0.0f) { return nullptr; }
	const f32 ux = dx / geo, uz = dz / geo;
	// Room side: walls are seen from the side where the texture u runs to the viewer's right
	// (camera right = (forward.z, -forward.x)), so the normal into the room is (uz, -ux).
	const f32 nx = uz, nz = -ux;
	const f32 a0 = -offU / (8.0f * k);
	pos[0] = x0 + ux * a0;
	pos[2] = z0 + uz * a0;
	pos[1] = texBase + offV / 8.0f;   // bottom of the sign (y down)
	// model x along the wall (scaled to the wall's texel density), y down, z into the room
	// (model x is -u so the frame keeps the handedness of the 3D object transforms)
	transform[0] = floatToFixed16(-ux / k); transform[1] = 0;           transform[2] = floatToFixed16(nx);
	transform[3] = 0;                       transform[4] = ONE_16;      transform[5] = 0;
	transform[6] = floatToFixed16(-uz / k); transform[7] = 0;           transform[8] = floatToFixed16(nz);
	if (fullbright) { *fullbright = (wall->flags1 & WF1_ILLUM_SIGN) ? JTRUE : JFALSE; }
	return m;
}

void vox_prepareLevel()
{
	if (!tfe_xr_active() || !tfe_xr_options()->voxels) { return; }
	loadIndex();
	if (s_stems.empty()) { return; }
	prepareWalls();
	// The sprite pointers change between levels: map them again (the models themselves are cached).
	for (auto& it : s_sprites) { delete it.second; }
	s_sprites.clear();
	for (s32 pool = 0; pool < POOL_COUNT; pool++)
	{
		for (JediWax* wax : TFE_Sprite_Jedi::getWaxList(AssetPool(pool)))
		{
			if (!wax || s_sprites.count(wax)) { continue; }
			s_sprites[wax] = buildSprite(TFE_Sprite_Jedi::getWaxName(wax), wax, firstFrame(wax));
		}
		for (JediFrame* fme : TFE_Sprite_Jedi::getFrameList(AssetPool(pool)))
		{
			if (!fme || s_sprites.count(fme)) { continue; }
			s_sprites[fme] = buildSprite(TFE_Sprite_Jedi::getFrameName(fme), fme, fme);
		}
	}
}

const std::vector<JediModel*>& vox_models() { return s_models; }

JediModel* vox_objectModel(SecObject* obj, s32 transform[9])
{
	if (!obj || !obj->ptr || !tfe_xr_options()->voxels) { return nullptr; }
	auto it = s_sprites.find(obj->ptr);
	if (it == s_sprites.end() || !it->second) { return nullptr; }
	const VoxSprite& vs = *it->second;
	JediModel* m = nullptr;
	if (obj->type == OBJ_TYPE_FRAME)
	{
		m = vs.base ? vs.base : (vs.letters.empty() ? nullptr : vs.letters[0]);
	}
	else
	{
		const s32 anim = obj->anim & 31;
		const s32 nf = animFrameCount(obj->wax, anim);
		if (vs.enemy)
		{
			JediModel* walk = !vs.numbers.empty() ? pick(vs.numbers, obj->frame, nf) : vs.base;
			JediModel* att = vs.attack ? vs.attack : (vs.melee ? vs.melee : walk);
			switch (anim)
			{
				case 2: case 3: case 4: m = nullptr; break;                  // dying / dead: the sprite
				case 1: case 6: m = vs.melee ? vs.melee : att; break;
				case 7: m = vs.range[0] ? vs.range[0] : att; break;
				case 8: m = vs.range[1] ? vs.range[1] : (vs.range[0] ? vs.range[0] : att); break;
				case 12: m = vs.pain ? vs.pain : walk; break;
				default: m = walk; break;
			}
		}
		else if (anim == 0)
		{
			m = !vs.letters.empty() ? pick(vs.letters, obj->frame, nf) : vs.base;
		}
		else if (anim == 1)
		{
			m = vs.damaged;   // destroyed scenery; an explosion stays a sprite
		}
	}
	if (!m || !m->drawId) { return nullptr; }
	computeTransform3x3(transform, obj->yaw, 0, 0);
	return m;
}

#ifdef VOX_HOST_TEST
JediModel* vox_testBuild(const char* stem, float s, float offY) { loadIndex(); return buildModel(stem, s, offY); }
JediModel* vox_testBuildMode(const char* stem, float s, int mode) { loadIndex(); return buildModel(stem, s, 0.0f, mode); }
#endif
