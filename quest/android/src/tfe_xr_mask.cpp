// Gas mask from inside: the rubber frame and lens ring at the edge of the view (head-locked mesh made
// in Blender), a faint lens tint and the breath fog that comes and goes on the lower lens.
#include "tfe_xr_mask.h"
#include <TFE_RenderBackend/Win32OpenGL/gl.h>
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

#define MLOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TFEQuestXR", __VA_ARGS__)

namespace
{
	int s_state = 0;   // 0 not loaded, 1 ready, -1 missing
	GLuint s_prog = 0, s_vao = 0, s_vbo = 0, s_ibo = 0;
	GLsizei s_count = 0;
	GLint s_locVP = -1, s_locModel = -1, s_locLight = -1, s_locFade = -1;
	GLuint s_lens = 0, s_lensVao = 0;
	GLint s_locLFade = -1, s_locLBreath = -1, s_locLSize = -1, s_locLLight = -1;

	GLuint compile(GLenum type, const char* src)
	{
		GLuint s = glCreateShader(type);
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); MLOGE("mask shader: %s", log); }
		return s;
	}
	GLuint link(const char* vs, const char* fs)
	{
		GLuint p = glCreateProgram();
		glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
		glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
		glLinkProgram(p);
		GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
		if (!ok) { MLOGE("mask shader link failed"); glDeleteProgram(p); return 0; }
		return p;
	}

	void load()
	{
		s_state = -1;
		FILE* f = fopen("gasmask.bin", "rb");
		if (!f) { MLOGE("gasmask.bin not found: no gas mask view"); return; }
		char magic[4]; int nv = 0, ni = 0;
		bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "GMSK", 4) == 0 && fread(&nv, 4, 1, f) == 1 && fread(&ni, 4, 1, f) == 1
			&& nv > 0 && nv < 400000 && ni > 0 && ni < 1200000;
		std::vector<float> v; std::vector<unsigned int> ix;
		if (ok) { v.resize(size_t(nv) * 9); ix.resize(ni); ok = fread(v.data(), 4, v.size(), f) == v.size() && fread(ix.data(), 4, ix.size(), f) == ix.size(); }
		fclose(f);
		if (!ok) { MLOGE("gasmask.bin is damaged"); return; }
		const char* vs =
			"#version 300 es\n"
			"uniform mat4 VP; uniform mat4 Model;\n"
			"layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aCol;\n"
			"out vec3 n; out vec3 c; out vec3 lp;\n"
			"void main(){ n = aNrm; c = aCol; lp = aPos; gl_Position = VP * Model * vec4(aPos, 1.0); }\n";
		const char* fs =
			"#version 300 es\n"
			"precision mediump float;\n"
			"uniform float Light; uniform float Fade;\n"
			"in vec3 n; in vec3 c; in vec3 lp; out vec4 o;\n"
			"void main(){\n"
			" vec3 N = normalize(n); vec3 V = normalize(-lp);\n"                                     // the eye is near the origin
			" float lit = 0.35 + 0.65 * clamp(dot(N, normalize(vec3(0.0, 0.8, 0.6))) * 0.5 + 0.5, 0.0, 1.0);\n"   // light from above, through the lens
			" float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 2.0);\n"                             // soft sheen on the rubber
			" float gloss = smoothstep(0.17, 0.22, c.r);\n"                                         // the lens ring is the brighter part
			" vec3 col = c * lit * (0.55 + 0.75 * Light) + vec3(0.05 + 0.10 * gloss) * rim * Light;\n"
			" o = vec4(col, Fade); }\n";
		s_prog = link(vs, fs);
		if (!s_prog) { return; }
		s_locVP = glGetUniformLocation(s_prog, "VP"); s_locModel = glGetUniformLocation(s_prog, "Model");
		s_locLight = glGetUniformLocation(s_prog, "Light"); s_locFade = glGetUniformLocation(s_prog, "Fade");
		glGenVertexArrays(1, &s_vao); glBindVertexArray(s_vao);
		glGenBuffers(1, &s_vbo); glBindBuffer(GL_ARRAY_BUFFER, s_vbo); glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
		glGenBuffers(1, &s_ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ix.size() * 4, ix.data(), GL_STATIC_DRAW);
		glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, (void*)0);
		glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 36, (void*)12);
		glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 36, (void*)24);
		glBindVertexArray(0);
		s_count = (GLsizei)ix.size();
		// Lens: a whole-view pass, very light: a cool tint, darker toward the frame, a faint reflection
		// streak and the breath fog rising from the bottom on each exhale.
		const char* lvs = "#version 300 es\nvoid main(){ vec2 p = vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2)); gl_Position = vec4(p*2.0-1.0, 0.0, 1.0); }\n";
		const char* lfs =
			"#version 300 es\n"
			"precision mediump float;\n"
			"uniform float Fade; uniform float Breath; uniform float Light; uniform vec2 Size; out vec4 o;\n"
			"float h21(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
			// fingerprint: an oval of fine curved ridges fading at its edge
			"float print(vec2 d, vec2 c, vec2 r, float rot){ vec2 q = d - c; float cs = cos(rot), sn = sin(rot); q = vec2(cs*q.x - sn*q.y, sn*q.x + cs*q.y) / r;\n"
			" float e = length(q); float ridges = 0.5 + 0.5 * sin((e + 0.18 * q.y * q.y - 0.10 * q.x) * 44.0 + 2.2 * sin(q.x * 3.0 + q.y * 2.0));\n"
			" return smoothstep(1.0, 0.55, e) * (0.35 + 0.65 * ridges); }\n"
			// soft greasy smear
			"float smear(vec2 d, vec2 c, vec2 r){ vec2 q = (d - c) / r; return smoothstep(1.0, 0.0, length(q)); }\n"
			// thin scratch from a to b
			"float scratch(vec2 d, vec2 a, vec2 b, float w){ vec2 pa = d - a, ba = b - a; float t = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);\n"
			" return smoothstep(w, 0.0, length(pa - ba * t)) * (0.6 + 0.4 * sin(t * 40.0)); }\n"
			"void main(){ vec2 d = gl_FragCoord.xy / Size * 2.0 - 1.0;\n"
			" float r = length(d * vec2(0.9, 1.0));\n"
			" float tint = 0.035 + 0.10 * smoothstep(0.70, 1.15, r);\n"
			" float streak = 0.018 * smoothstep(0.06, 0.0, abs(d.x * 0.55 + d.y - 0.55));\n"
			" float fog = Breath * 0.16 * smoothstep(-0.25, -0.95, d.y) * (1.0 - 0.45 * abs(d.x));\n"
			// dirt on the lens: mostly toward the edges, a couple of marks in view to catch the eye
			" float dirt = 0.0;\n"
			" dirt += 0.11 * print(d, vec2(-0.62, 0.42), vec2(0.13, 0.17), 0.5);\n"
			" dirt += 0.09 * print(d, vec2(0.70, -0.30), vec2(0.11, 0.15), -0.3);\n"
			" dirt += 0.06 * print(d, vec2(0.28, 0.62), vec2(0.08, 0.10), 1.1);\n"
			" dirt += 0.07 * smear(d, vec2(-0.40, -0.55), vec2(0.30, 0.10));\n"
			" dirt += 0.05 * smear(d, vec2(0.52, 0.55), vec2(0.18, 0.07));\n"
			" dirt += 0.14 * scratch(d, vec2(-0.18, 0.30), vec2(0.06, 0.22), 0.004);\n"
			" dirt += 0.10 * scratch(d, vec2(0.55, 0.05), vec2(0.66, -0.12), 0.003);\n"
			// dust specks and dried droplets (sparse, more of them near the frame)
			" vec2 g = d * 38.0; vec2 id = floor(g); vec2 f = fract(g) - 0.5; float hs = h21(id);\n"
			" float near = 0.35 + 0.65 * smoothstep(0.35, 1.0, r);\n"
			" if (hs > 1.0 - 0.045 * near) { float rr = 0.10 + 0.25 * h21(id + 3.1); dirt += 0.22 * smoothstep(rr, rr * 0.4, length(f)); }\n"
			" vec2 g2 = d * 11.0; vec2 id2 = floor(g2); vec2 f2 = fract(g2) - 0.5; float h2 = h21(id2 + 9.7);\n"
			" if (h2 > 0.93) { float rr = 0.18 + 0.12 * h21(id2); float l = length(f2); dirt += 0.10 * smoothstep(rr, rr - 0.04, l) * (0.4 + 0.6 * smoothstep(rr - 0.06, rr, l)); }\n"
			" dirt *= 0.6 + 0.6 * Light;\n"   // the marks show more in the light (headlamp, bright rooms)
			" vec3 dcol = vec3(0.80, 0.78, 0.72);\n"
			" vec3 c = mix(vec3(0.55, 0.66, 0.70), vec3(0.88, 0.90, 0.92), clamp(fog * 6.0, 0.0, 1.0));\n"
			" float a = tint + streak + fog;\n"
			" c = (c * a + dcol * dirt) / max(a + dirt, 0.0001);\n"
			" o = vec4(c, (a + dirt) * Fade); }\n";
		s_lens = link(lvs, lfs);
		if (s_lens)
		{
			s_locLFade = glGetUniformLocation(s_lens, "Fade"); s_locLBreath = glGetUniformLocation(s_lens, "Breath"); s_locLSize = glGetUniformLocation(s_lens, "Size"); s_locLLight = glGetUniformLocation(s_lens, "Light");
			glGenVertexArrays(1, &s_lensVao);
		}
		s_state = 1;
	}
}

void mask_draw(const float vp[16], const float model[16], float light, float fade, float breath, int eyeW, int eyeH)
{
	if (fade <= 0.001f) { return; }
	if (s_state == 0) { load(); }
	if (s_state != 1) { return; }
	GLint prevProg = 0, prevVao = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
	const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	if (s_lens)
	{
		glUseProgram(s_lens);
		glUniform1f(s_locLFade, fade); glUniform1f(s_locLBreath, breath); glUniform1f(s_locLLight, fminf(1.0f, light)); glUniform2f(s_locLSize, float(eyeW), float(eyeH));
		glBindVertexArray(s_lensVao); glDrawArrays(GL_TRIANGLES, 0, 3);
	}
	glUseProgram(s_prog);
	glUniformMatrix4fv(s_locVP, 1, GL_FALSE, vp); glUniformMatrix4fv(s_locModel, 1, GL_FALSE, model);
	glUniform1f(s_locLight, light); glUniform1f(s_locFade, fade);
	glBindVertexArray(s_vao); glDrawElements(GL_TRIANGLES, s_count, GL_UNSIGNED_INT, nullptr);
	glBindVertexArray(prevVao); glUseProgram(prevProg);
	if (depth) glEnable(GL_DEPTH_TEST);
	if (!blend) glDisable(GL_BLEND);
	if (cull) glEnable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
}
