// Menu room: when the game shows a 2D screen (main menu, pause/escape menu, agent screens,
// cutscenes), the player stands in an Imperial ship room and the screen is a computer console in
// front of them, with a viewport to space behind it. Geometry: boxes in a room frame; the look
// (panels, grating, light strips, stars) is procedural in the fragment shader.
#include "tfe_xr_room.h"
#include <GLES3/gl32.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TFEQuestXR", __VA_ARGS__)

namespace
{
	enum Kind { K_FLOOR = 0, K_WALL, K_CEIL, K_LIGHT, K_WINDOW, K_CONSOLE, K_BUTTONS, K_BEZEL, K_TRIM, K_GLOW, K_BANK, K_COUNT };
	struct RV { float x, y, z, nx, ny, nz, kind; };
	std::vector<RV> s_v;
	GLuint s_prog = 0, s_vbo = 0, s_vao = 0;
	GLint s_locVP = -1, s_locModel = -1, s_locEye = -1, s_locTime = -1;
	GLsizei s_count = 0;
	GLint s_first[K_COUNT], s_num[K_COUNT];
	GLint s_locTex = -1, s_locTexM = -1, s_locTexOff = -1, s_locHasTex = -1;
	float s_texOff[K_COUNT][2] = {};
	// Game textures per kind (slot = kind), loaded by the game from its own files.
	GLuint s_tex[K_COUNT] = {};
	float s_texM[K_COUNT][2] = {};
	float s_builtW = -1.0f, s_builtH = -1.0f, s_builtD = -1.0f;

	void quad(const float p0[3], const float p1[3], const float p2[3], const float p3[3], const float n[3], int k)
	{
		const float* p[4] = { p0, p1, p2, p3 };
		const int t[6] = { 0, 1, 2, 0, 2, 3 };
		for (int i = 0; i < 6; i++) { s_v.push_back({ p[t[i]][0], p[t[i]][1], p[t[i]][2], n[0], n[1], n[2], float(k) }); }
	}
	void box(float x0, float y0, float z0, float x1, float y1, float z1, int k)
	{
		const float a[3] = { x0, y0, z0 }, b[3] = { x1, y0, z0 }, c[3] = { x1, y1, z0 }, d[3] = { x0, y1, z0 };
		const float e[3] = { x0, y0, z1 }, f[3] = { x1, y0, z1 }, g[3] = { x1, y1, z1 }, h[3] = { x0, y1, z1 };
		const float nx0[3] = { -1, 0, 0 }, nx1[3] = { 1, 0, 0 }, ny0[3] = { 0, -1, 0 }, ny1[3] = { 0, 1, 0 }, nz0[3] = { 0, 0, -1 }, nz1[3] = { 0, 0, 1 };
		quad(a, d, c, b, nz0, k); quad(e, f, g, h, nz1, k);
		quad(a, e, h, d, nx0, k); quad(b, c, g, f, nx1, k);
		quad(a, b, f, e, ny0, k); quad(d, h, g, c, ny1, k);
	}
	// Box tilted about X (console desk top), center + half sizes, angle in radians.
	void tiltedBox(float cx, float cy, float cz, float hx, float hy, float hz, float ang, int k)
	{
		const size_t start = s_v.size();
		box(-hx, -hy, -hz, hx, hy, hz, k);
		const float c = cosf(ang), s = sinf(ang);
		for (size_t i = start; i < s_v.size(); i++)
		{
			RV& v = s_v[i];
			const float y = v.y * c - v.z * s, z = v.y * s + v.z * c;
			const float ny = v.ny * c - v.nz * s, nz = v.ny * s + v.nz * c;
			v.y = y + cy; v.z = z + cz; v.x += cx; v.ny = ny; v.nz = nz;
		}
	}

	// Room frame: x right, y up from the floor, the screen center at (0, screenY, 0) facing +z,
	// the player at +z.
	void build(float screenW, float screenH, float screenY)
	{
		s_v.clear();
		const float W = 3.2f, front = -1.6f, back = 5.0f, H = 3.4f;
		// Floor, ceiling and walls.
		box(-W, -0.05f, front, W, 0.0f, back, K_FLOOR);
		box(-W, H, front, W, H + 0.05f, back, K_CEIL);
		box(-W - 0.05f, 0.0f, front, -W, H, back, K_WALL);
		box(W, 0.0f, front, W + 0.05f, H, back, K_WALL);
		box(-W, 0.0f, back, W, H, back + 0.05f, K_WALL);
		// Front wall with a wide viewport to space behind the console.
		const float wy0 = 0.95f, wy1 = 2.85f, wx = 2.6f;
		box(-W, 0.0f, front - 0.05f, W, wy0, front, K_WALL);
		box(-W, wy1, front - 0.05f, W, H, front, K_WALL);
		box(-W, wy0, front - 0.05f, -wx, wy1, front, K_WALL);
		box(wx, wy0, front - 0.05f, W, wy1, front, K_WALL);
		box(-wx, wy0, front - 0.25f, wx, wy1, front - 0.20f, K_WINDOW);
		// Window frame and mullions.
		box(-wx - 0.08f, wy0 - 0.08f, front - 0.02f, wx + 0.08f, wy0, front + 0.10f, K_TRIM);
		box(-wx - 0.08f, wy1, front - 0.02f, wx + 0.08f, wy1 + 0.08f, front + 0.10f, K_TRIM);
		for (int i = -2; i <= 2; i++) { box(i * 1.3f - 0.05f, wy0, front - 0.18f, i * 1.3f + 0.05f, wy1, front + 0.06f, K_TRIM); }
		// Light strips along the side walls and over the back wall.
		box(-W + 0.01f, 2.75f, front, -W + 0.06f, 2.83f, back, K_LIGHT);
		box(W - 0.06f, 2.75f, front, W - 0.01f, 2.83f, back, K_LIGHT);
		box(-W, 2.75f, back - 0.06f, W, 2.83f, back - 0.01f, K_LIGHT);
		// Floor edge glow strips.
		box(-W + 0.02f, 0.0f, front, -W + 0.10f, 0.03f, back, K_GLOW);
		box(W - 0.10f, 0.0f, front, W - 0.02f, 0.03f, back, K_GLOW);
		// Wall ribs.
		for (int i = 0; i < 6; i++)
		{
			const float z = front + 0.6f + i * 1.1f;
			box(-W, 0.0f, z - 0.08f, -W + 0.14f, H, z + 0.08f, K_TRIM);
			box(W - 0.14f, 0.0f, z - 0.08f, W, H, z + 0.08f, K_TRIM);
		}
		// Computer banks along the side walls, near the console.
		for (int s2 = -1; s2 <= 1; s2 += 2)
		{
			const float x = s2 * (W - 0.15f);
			box(fminf(x, x + s2 * 0.02f), 0.0f, front + 0.75f, fmaxf(x, x + s2 * 0.02f), 1.17f, front + 3.05f, K_BANK);
		}
		// Ceiling beams.
		for (int i = 0; i < 5; i++) { const float z = front + 0.9f + i * 1.4f; box(-W, H - 0.18f, z - 0.1f, W, H, z + 0.1f, K_TRIM); }
		// Console: pedestal, slanted desk with buttons under the screen, the screen bezel.
		const float hw = screenW * 0.5f, hh = screenH * 0.5f;
		const float by = screenY - hh;
		box(-hw - 0.10f, screenY - hh - 0.06f, -0.10f, hw + 0.10f, screenY + hh + 0.06f, -0.02f, K_BEZEL);
		box(-hw - 0.14f, screenY - hh - 0.10f, -0.16f, -hw - 0.06f, screenY + hh + 0.10f, -0.02f, K_TRIM);
		box(hw + 0.06f, screenY - hh - 0.10f, -0.16f, hw + 0.14f, screenY + hh + 0.10f, -0.02f, K_TRIM);
		box(-0.25f, 0.0f, -0.35f, 0.25f, by - 0.30f, -0.05f, K_CONSOLE);
		box(-hw - 0.15f, by - 0.36f, -0.45f, hw + 0.15f, by - 0.22f, 0.05f, K_CONSOLE);
		// The desk rises toward the screen, so its keys face the player (back edge on a riser).
		box(-hw * 0.85f, by - 0.36f, -0.10f, hw * 0.85f, by - 0.13f, 0.10f, K_CONSOLE);
		tiltedBox(0.0f, by - 0.18f, 0.25f, hw * 0.85f, 0.03f, 0.22f, 0.45f, K_CONSOLE);
		tiltedBox(0.0f, by - 0.145f, 0.25f, hw * 0.78f, 0.006f, 0.17f, 0.45f, K_BUTTONS);
		// Side consoles.
		for (int s = -1; s <= 1; s += 2)
		{
			box(s * 2.2f - 0.45f, 0.0f, -0.6f, s * 2.2f + 0.45f, 0.80f, 0.1f, K_CONSOLE);
			box(s * 2.2f - 0.45f, 0.80f, -0.6f, s * 2.2f + 0.45f, 1.10f, -0.36f, K_CONSOLE);
			tiltedBox(s * 2.2f, 0.985f, -0.15f, 0.45f, 0.03f, 0.27f, 0.55f, K_CONSOLE);
			tiltedBox(s * 2.2f, 1.02f, -0.15f, 0.42f, 0.006f, 0.25f, 0.55f, K_BUTTONS);
			box(s * 2.2f - 0.40f, 1.05f, -0.62f, s * 2.2f + 0.40f, 1.75f, -0.55f, K_BEZEL);
		}
		std::stable_sort(s_v.begin(), s_v.end(), [](const RV& a, const RV& b) { return a.kind < b.kind; });
		for (int k = 0; k < K_COUNT; k++) { s_first[k] = 0; s_num[k] = 0; }
		for (size_t i = 0; i < s_v.size(); i++)
		{
			const int k = int(s_v[i].kind + 0.5f);
			if (s_num[k] == 0) { s_first[k] = GLint(i); }
			s_num[k]++;
		}
		s_count = (GLsizei)s_v.size();
	}

	GLuint compile(GLenum type, const char* src)
	{
		GLuint s = glCreateShader(type);
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); LOGE("room shader: %s", log); }
		return s;
	}
}

void room_draw(const float viewProj[16], const float model[16], const float eye[3], float screenW, float screenH, float screenY, float time)
{
	if (!s_prog)
	{
		const char* vs =
			"#version 300 es\n"
			"uniform mat4 VP; uniform mat4 Model;\n"
			"layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in float aKind;\n"
			"out vec3 lp; out vec3 wp; out vec3 n; flat out int kind;\n"
			"void main(){ lp = aPos; vec4 w = Model * vec4(aPos, 1.0); wp = w.xyz; n = aNrm; kind = int(aKind + 0.5); gl_Position = VP * w; }\n";
		const char* fs =
			"#version 300 es\n"
			"precision highp float;\n"
			"uniform vec3 Eye; uniform float Time; uniform mat4 Model; uniform sampler2D Tex; uniform vec2 TexM; uniform vec2 TexOff; uniform float HasTex;\n"
			"in vec3 lp; in vec3 wp; in vec3 n; flat in int kind; out vec4 o;\n"
			"float h21(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
			"float lines(float x, float w){ float f = fract(x); return smoothstep(0.0, w, f) * smoothstep(0.0, w, 1.0 - f); }\n"
			"void main(){\n"
			" vec3 N = normalize(n); vec3 c; float em = 0.0; float sp = 0.15;\n"
			" vec2 uv = abs(N.y) > 0.5 ? lp.xz : (abs(N.x) > 0.5 ? lp.zy : lp.xy);\n"
			" if (HasTex > 0.5) {\n"   // the game's own textures, Dark Forces scale
			"  vec2 t = (uv - TexOff) / TexM; c = texture(Tex, t).rgb; sp = 0.2;\n"
			"  if (kind == 2 || kind == 6 || kind == 3) { em = 1.0; c *= 1.15; }\n"
			" } else\n"
			" if (kind == 0) {\n"   // floor: grating tiles with a darker grid and tread
			"  vec2 t = uv / 0.6; float g = lines(t.x, 0.04) * lines(t.y, 0.04);\n"
			"  float tread = 0.85 + 0.15 * step(0.5, fract((uv.x + uv.y) * 12.0));\n"
			"  float cell = h21(floor(t));\n"
			"  c = vec3(0.11, 0.12, 0.14) * (0.55 + 0.45 * g) * tread * (0.9 + 0.2 * cell);\n"
			"  if (abs(lp.x) < 0.45) c *= 1.25;\n"
			"  sp = 0.35;\n"
			" } else if (kind == 1) {\n"   // Imperial wall panels
			"  vec2 t = uv / vec2(0.9, 0.55); float g = lines(t.x, 0.03) * lines(t.y, 0.05);\n"
			"  float cell = h21(floor(t));\n"
			"  c = vec3(0.30, 0.32, 0.36) * (0.45 + 0.55 * g) * (0.85 + 0.25 * cell);\n"
			"  if (cell > 0.88) { vec2 f = fract(t); if (f.x > 0.15 && f.x < 0.45 && f.y > 0.35 && f.y < 0.65) { c = mix(vec3(0.05), vec3(0.9, 0.2, 0.15), step(0.5, fract(f.x * 10.0 + Time * 0.5)) * 0.8); em = 1.0; } }\n"
			"  sp = 0.25;\n"
			" } else if (kind == 2) {\n"   // ceiling with recessed lights
			"  vec2 t = uv / 1.2; vec2 f = fract(t) - 0.5; float g = lines(t.x, 0.03) * lines(t.y, 0.03);\n"
			"  c = vec3(0.10, 0.11, 0.13) * (0.5 + 0.5 * g);\n"
			"  if (abs(f.x) < 0.22 && abs(f.y) < 0.12) { c = vec3(0.75, 0.85, 1.0); em = 1.0; }\n"
			" } else if (kind == 3) { c = vec3(0.70, 0.82, 1.0); em = 1.0; }\n"
			" else if (kind == 9) { c = vec3(0.25, 0.45, 0.9) * (0.7 + 0.3 * sin(lp.z * 3.0 - Time * 1.5)); em = 1.0; }\n"
			" else if (kind == 4) {\n"   // space: stars and a planet
			"  vec3 d = normalize(wp - Eye); vec3 ld = (inverse(Model) * vec4(d, 0.0)).xyz;\n"
			"  vec2 sp2 = vec2(atan(ld.x, -ld.z), ld.y) * 140.0; vec2 cid = floor(sp2); vec2 f = fract(sp2) - 0.5;\n"
			"  float s = h21(cid); float star = s > 0.985 ? smoothstep(0.25, 0.0, length(f)) * (0.5 + 0.5 * h21(cid + 7.0)) : 0.0;\n"
			"  c = vec3(0.005, 0.008, 0.02) + vec3(star);\n"
			"  vec2 pc = vec2(atan(ld.x, -ld.z) - 0.55, ld.y + 0.05); float r = length(pc);\n"
			"  if (r < 0.22) { float l = clamp(dot(normalize(vec3(pc, sqrt(max(0.0, 0.0484 - r * r)))), normalize(vec3(-0.6, 0.4, 0.7))), 0.0, 1.0);\n"
			"   float band = 0.5 + 0.5 * sin(pc.y * 60.0 + sin(pc.x * 20.0) * 2.0); c = mix(vec3(0.45, 0.28, 0.18), vec3(0.75, 0.55, 0.35), band) * (0.08 + 0.92 * l); }\n"
			"  else if (r < 0.235) c += vec3(0.2, 0.3, 0.6) * (1.0 - (r - 0.22) / 0.015);\n"
			"  em = 1.0;\n"
			" } else if (kind == 5) {\n"   // console metal
			"  vec2 t = uv / 0.25; float g = lines(t.x, 0.04) * lines(t.y, 0.04);\n"
			"  c = vec3(0.17, 0.18, 0.21) * (0.6 + 0.4 * g); sp = 0.45;\n"
			" } else if (kind == 6) {\n"   // button fields
			"  vec2 t = uv * vec2(22.0, 26.0); vec2 f = fract(t) - 0.5; vec2 id = floor(t);\n"
			"  float b = step(max(abs(f.x), abs(f.y)), 0.32); float h = h21(id);\n"
			"  vec3 bc = h < 0.33 ? vec3(0.9, 0.15, 0.1) : (h < 0.66 ? vec3(0.15, 0.9, 0.3) : vec3(1.0, 0.7, 0.2));\n"
			"  float on = step(0.4, fract(h * 13.0 + Time * (0.2 + h)));\n"
			"  c = mix(vec3(0.06), bc * (0.25 + 0.75 * on), b); em = b;\n"
			" } else if (kind == 7) { c = vec3(0.03, 0.035, 0.04); sp = 0.8; }\n"
			" else { c = vec3(0.24, 0.26, 0.30); sp = 0.5; }\n"
			" if (em < 0.5) {\n"
			"  vec3 L = normalize(vec3(0.2, 1.0, 0.3)); vec3 V = normalize(Eye - wp); vec3 Nw = normalize(mat3(Model) * N);\n"
			"  float dif = max(dot(Nw, L), 0.0) * 0.55 + 0.45; vec3 H = normalize(L + V);\n"
			"  float k = HasTex > 0.5 ? 1.05 : 1.6;\n"
			"  c = c * dif * k + vec3(pow(max(dot(Nw, H), 0.0), 30.0) * sp * 0.4);\n"
			" }\n"
			" o = vec4(c, 1.0); }\n";
		s_prog = glCreateProgram();
		glAttachShader(s_prog, compile(GL_VERTEX_SHADER, vs));
		glAttachShader(s_prog, compile(GL_FRAGMENT_SHADER, fs));
		glLinkProgram(s_prog);
		GLint ok = 0; glGetProgramiv(s_prog, GL_LINK_STATUS, &ok);
		if (!ok) { LOGE("room shader link failed"); glDeleteProgram(s_prog); s_prog = 0; return; }
		s_locVP = glGetUniformLocation(s_prog, "VP");
		s_locModel = glGetUniformLocation(s_prog, "Model");
		s_locEye = glGetUniformLocation(s_prog, "Eye");
		s_locTime = glGetUniformLocation(s_prog, "Time");
		s_locTex = glGetUniformLocation(s_prog, "Tex");
		s_locTexM = glGetUniformLocation(s_prog, "TexM");
		s_locTexOff = glGetUniformLocation(s_prog, "TexOff");
		s_locHasTex = glGetUniformLocation(s_prog, "HasTex");
		glGenBuffers(1, &s_vbo);
		glGenVertexArrays(1, &s_vao);
	}
	if (fabsf(screenW - s_builtW) > 0.01f || fabsf(screenH - s_builtH) > 0.01f || fabsf(screenY - s_builtD) > 0.01f)
	{
		build(screenW, screenH, screenY);
		s_builtW = screenW; s_builtH = screenH; s_builtD = screenY;
		glBindVertexArray(s_vao);
		glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
		glBufferData(GL_ARRAY_BUFFER, s_v.size() * sizeof(RV), s_v.data(), GL_STATIC_DRAW);
		glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RV), (void*)0);
		glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RV), (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(RV), (void*)(6 * sizeof(float)));
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
	glUseProgram(s_prog);
	glUniformMatrix4fv(s_locVP, 1, GL_FALSE, viewProj);
	glUniformMatrix4fv(s_locModel, 1, GL_FALSE, model);
	glUniform3f(s_locEye, eye[0], eye[1], eye[2]);
	glUniform1f(s_locTime, time);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glBindVertexArray(s_vao);
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(s_locTex, 0);
	for (int k = 0; k < K_COUNT; k++)
	{
		if (s_num[k] <= 0) { continue; }
		const bool tex = s_tex[k] != 0;
		glBindTexture(GL_TEXTURE_2D, s_tex[k]);
		glUniform1f(s_locHasTex, tex ? 1.0f : 0.0f);
		glUniform2f(s_locTexM, tex ? s_texM[k][0] : 1.0f, tex ? s_texM[k][1] : 1.0f);
		glUniform2f(s_locTexOff, s_texOff[k][0], s_texOff[k][1]);
		glDrawArrays(GL_TRIANGLES, s_first[k], s_num[k]);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);
}

bool room_needsTextures()
{
	for (int k = 0; k < K_COUNT; k++) { if (k != K_WINDOW && k != K_GLOW && k != K_BEZEL && !s_tex[k]) { return true; } }
	return false;
}

void room_setTexture(int kind, const unsigned char* rgba, int w, int h, float metersPerTexel, float offU, float offV)
{
	if (kind < 0 || kind >= K_COUNT || !rgba || w <= 0 || h <= 0) { return; }
	if (!s_tex[kind]) { glGenTextures(1, &s_tex[kind]); }
	glBindTexture(GL_TEXTURE_2D, s_tex[kind]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glBindTexture(GL_TEXTURE_2D, 0);
	s_texM[kind][0] = w * metersPerTexel; s_texM[kind][1] = h * metersPerTexel;
	s_texOff[kind][0] = offU; s_texOff[kind][1] = offV;
}
