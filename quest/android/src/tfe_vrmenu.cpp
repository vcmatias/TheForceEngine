// Dark Forces style configuration and help screens (see tfe_vrmenu.h).
#include "tfe_vrmenu.h"
#include "tfe_vrmenu_art.h"
#include "tfe_xr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
	const int c_rowTop[VRM_ROWS] = { 29, 39, 49, 59, 73, 83, 97, 107, 121, 131, 145, 155 };
	const int c_barX0 = 174, c_barX1 = 262, c_barH = 8;
	const int c_labelRight = 169;
	// Menu palette entries used by the original art.
	const unsigned char RED = 162, RED_HI = 160, RED_SH = 167;
	const unsigned char GRN = 11, GRN_HI = 10, GRN_DK = 13, GRN_BG = 14;
	const unsigned char PANEL = 78, WHITE = 1, YELLOW = 19, ORANGE = 21;

	inline void put(unsigned char* c, int x, int y, unsigned char v)
	{
		if (x >= 0 && x < 320 && y >= 0 && y < 200) { c[y * 320 + x] = v; }
	}
	void fill(unsigned char* c, int x0, int y0, int x1, int y1, unsigned char v)
	{
		for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) { put(c, x, y, v); }
	}
	void blit(unsigned char* c, const VrmImage& im, int dx, int dy)
	{
		for (int y = 0; y < im.h; y++)
			for (int x = 0; x < im.w; x++)
			{
				const unsigned char v = im.px[y * im.w + x];
				if (v) { put(c, x + dx, y + dy, v); }
			}
	}

	// UTF-8 decode of one code point.
	unsigned decode(const char*& s)
	{
		const unsigned char c = (unsigned char)*s++;
		if (c < 0x80) { return c; }
		if ((c & 0xE0) == 0xC0 && *s) { return ((c & 0x1F) << 6) | ((unsigned char)*s++ & 0x3F); }
		if ((c & 0xF0) == 0xE0 && s[0] && s[1]) { unsigned v = ((c & 0x0F) << 12) | (((unsigned char)s[0] & 0x3F) << 6) | ((unsigned char)s[1] & 0x3F); s += 2; return v; }
		return '?';
	}
	// Base ASCII letter and accent mark: 1 acute, 2 grave, 3 circumflex, 4 tilde, 5 cedilla.
	char baseOf(unsigned cp, int* mark)
	{
		*mark = 0;
		switch (cp)
		{
		case 0xC1: case 0xE1: *mark = 1; return 'A';
		case 0xC0: case 0xE0: *mark = 2; return 'A';
		case 0xC2: case 0xE2: *mark = 3; return 'A';
		case 0xC3: case 0xE3: *mark = 4; return 'A';
		case 0xC9: case 0xE9: *mark = 1; return 'E';
		case 0xCA: case 0xEA: *mark = 3; return 'E';
		case 0xCD: case 0xED: *mark = 1; return 'I';
		case 0xD3: case 0xF3: *mark = 1; return 'O';
		case 0xD4: case 0xF4: *mark = 3; return 'O';
		case 0xD5: case 0xF5: *mark = 4; return 'O';
		case 0xDA: case 0xFA: *mark = 1; return 'U';
		case 0xC7: case 0xE7: *mark = 5; return 'C';
		case 0xBA: return 'o';
		}
		if (cp >= 'a' && cp <= 'z') { return char(cp - 'a' + 'A'); }
		return cp < 128 ? char(cp) : '?';
	}
	int glyphWidth(const VrmFont& f, char ch)
	{
		const int i = (unsigned char)ch - f.first;
		return (i >= 0 && i < f.num) ? f.widths[i] : 0;
	}
	void glyph(unsigned char* c, const VrmFont& f, char ch, int x, int y, unsigned char color)
	{
		const int i = (unsigned char)ch - f.first;
		if (i < 0 || i >= f.num) { return; }
		const int stride = f.w >> 3;
		const unsigned char* g = f.data + i * stride * f.h;
		for (int gy = 0; gy < f.h; gy++, g += stride)
			for (int gx = 0; gx < f.w; gx++)
				if (g[gx >> 3] & (1 << ((7 - gx) & 7))) { put(c, x + gx, y + gy, color); }
	}
	void mark(unsigned char* c, int m, int x, int w, int y, unsigned char color)
	{
		const int mid = x + w / 2;
		switch (m)
		{
		case 1: put(c, mid + 1, y - 3, color); put(c, mid, y - 2, color); break;
		case 2: put(c, mid - 1, y - 3, color); put(c, mid, y - 2, color); break;
		case 3: put(c, mid, y - 3, color); put(c, mid - 1, y - 2, color); put(c, mid + 1, y - 2, color); break;
		case 4: put(c, mid - 2, y - 2, color); put(c, mid - 1, y - 3, color); put(c, mid, y - 2, color); put(c, mid + 1, y - 3, color); break;
		case 5: put(c, mid, y + 7, color); put(c, mid - 1, y + 8, color); break;
		}
	}
}

bool vrm_parseFont(const unsigned char* d, int size, VrmFont* out)
{
	if (size < 12) { return false; }
	auto s16 = [&](int o) { return int(short(d[o] | (d[o + 1] << 8))); };
	out->first = s16(0); out->num = s16(2); out->w = s16(4); out->h = s16(6);
	out->space = 1;
	const int glyphBytes = (out->w >> 3) * out->h;
	if (size < 12 + out->num + glyphBytes * out->num) { return false; }
	out->widths = d + 12;
	out->data = d + 12 + out->num;
	return true;
}

int vrm_textWidth(const VrmFont& font, const char* s)
{
	int w = 0, n = 0;
	while (*s)
	{
		int m;
		const char ch = baseOf(decode(s), &m);
		w += glyphWidth(font, ch) + font.space;
		n++;
	}
	return n ? w - font.space : 0;
}

void vrm_text(unsigned char* c, const VrmFont& font, const char* s, int x, int y, unsigned char color, int shadow)
{
	while (*s)
	{
		int m;
		const char ch = baseOf(decode(s), &m);
		const int w = glyphWidth(font, ch);
		if (shadow >= 0)
		{
			glyph(c, font, ch, x + 1, y + 1, (unsigned char)shadow);
			if (m) { mark(c, m, x + 1, w, y + 1, (unsigned char)shadow); }
		}
		glyph(c, font, ch, x, y, color);
		if (m) { mark(c, m, x, w, y, color); }
		x += w + font.space;
	}
}

static void drawButtonText(unsigned char* c, const VrmFont& font, const char* text, int x0, int y0, int x1, int y1, bool hover, bool pressed)
{
	fill(c, x0, y0, x1, y1, pressed ? GRN_DK : (hover ? GRN_BG : PANEL));
	const int w = vrm_textWidth(font, text);
	vrm_text(c, font, text, (x0 + x1 + 1 - w) / 2, (y0 + y1 + 1 - 7) / 2, hover ? GRN_HI : GRN, -1);
}

void vrm_drawConfig(unsigned char* c, const VrmImage* frames, int frameCount, const VrmFont& font, const VrmPage& page)
{
	if (frameCount < 1) { return; }
	blit(c, frames[0], 0, 0);

	// Header: replace the baked title (green text) with the panel's own background.
	for (int y = 11; y <= 24; y++)
		for (int x = 50; x <= 272; x++)
		{
			unsigned char& v = c[y * 320 + x];
			if (v >= 10 && v <= 13) { v = c[y * 320 + 55]; }
		}
	if (page.title)
	{
		const int w = vrm_textWidth(font, page.title);
		vrm_text(c, font, page.title, 160 - w / 2, 15, GRN, GRN_BG);
	}

	// Labels column: clear the baked English labels.
	fill(c, 53, 25, 172, 165, PANEL);
	for (int i = 0; i < VRM_ROWS; i++)
	{
		const int y = c_rowTop[i];
		if (i >= page.rowCount || !page.rows[i].label)
		{
			fill(c, c_barX0 - 1, y - 1, c_barX1 + 1, y + c_barH, PANEL);
			continue;
		}
		const VrmRow& r = page.rows[i];
		const bool hover = page.hover == i;
		const int lw = vrm_textWidth(font, r.label);
		vrm_text(c, font, r.label, c_labelRight - lw, y, hover ? RED_HI : RED, RED_SH);

		// Bar: dark dotted bed (from the art), green fill for sliders, value text in green.
		if (r.fill >= 0.0f)
		{
			const float f = r.fill > 1.0f ? 1.0f : r.fill;
			const int x1 = c_barX0 + int(f * float(c_barX1 - c_barX0) + 0.5f);
			fill(c, c_barX0, y, x1, y + c_barH - 1, GRN_DK);
			fill(c, x1 - 1 < c_barX0 ? c_barX0 : x1 - 1, y, x1, y + c_barH - 1, GRN);
		}
		if (hover)
		{
			for (int x = c_barX0 - 1; x <= c_barX1 + 1; x++) { put(c, x, y - 1, GRN); put(c, x, y + c_barH, GRN); }
			for (int yy = y - 1; yy <= y + c_barH; yy++) { put(c, c_barX0 - 1, yy, GRN); put(c, c_barX1 + 1, yy, GRN); }
		}
		const int vw = vrm_textWidth(font, r.value);
		vrm_text(c, font, r.value, (c_barX0 + c_barX1 + 1 - vw) / 2, y, hover ? GRN_HI : (r.fill >= 0.0f ? WHITE : GRN_HI), r.fill >= 0.0f ? 0 : -1);
	}

	// Bottom-left button ("CALIBRATE JOYSTICK" in the original): page switch.
	drawButtonText(c, font, page.leftButton ? page.leftButton : "", 58, 177, 172, 185,
		page.hover == VRM_BTN_LEFT, page.pressed == VRM_BTN_LEFT);
	// OK: original highlight frame while pointed at.
	if (frameCount > 2 && page.hover == VRM_BTN_OK) { blit(c, frames[page.pressed == VRM_BTN_OK ? 1 : 2], 0, 0); }
}

int vrm_hitConfig(int x, int y, const VrmPage& page, float* barPos)
{
	if (x >= 56 && x <= 172 && y >= 175 && y <= 187) { return VRM_BTN_LEFT; }
	if (x >= 222 && x <= 270 && y >= 175 && y <= 187) { return VRM_BTN_OK; }
	for (int i = 0; i < page.rowCount && i < VRM_ROWS; i++)
	{
		const int top = c_rowTop[i];
		if (y >= top - 1 && y <= top + c_barH && x >= 53 && x <= c_barX1 + 3)
		{
			if (barPos)
			{
				const float f = float(x - c_barX0) / float(c_barX1 - c_barX0);
				*barPos = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
			}
			return i;
		}
	}
	return -1;
}

// ---------------- Help ----------------
namespace
{
	void controller(unsigned char* c, const unsigned char* px, int x0, int y0)
	{
		for (int y = 0; y < c_ctlH; y++)
			for (int x = 0; x < c_ctlW; x++)
				if (px[y * c_ctlW + x]) { put(c, x0 + x, y0 + y, px[y * c_ctlW + x]); }
	}
	struct HelpLine { unsigned char color; const char* key; const char* text; };
	void lines(unsigned char* c, const VrmFont& f, const HelpLine* l, int n, int x, int y)
	{
		for (int i = 0; i < n; i++, y += 9)
		{
			vrm_text(c, f, l[i].key, x, y, l[i].color, 0);
			vrm_text(c, f, l[i].text, x + 34, y, RED, RED_SH);
		}
	}
	struct Callout { int tx, ty; unsigned char color; const char* key; const char* text; const char* g1; const char* g2; const char* g3; int slot[4]; };
	// Clickable lines of the last help page drawn (remappable button slots).
	struct SlotRect { int x0, y0, x1, y1, slot; };
	SlotRect s_slotRects[32]; int s_slotRectCount = 0; int s_hoverSlot = -1;
	void addSlotRect(int x0, int y0, int w, int slot) { if (slot < 0 || s_slotRectCount >= 32) return; s_slotRects[s_slotRectCount++] = { x0 - 1, y0 - 1, x0 + w + 1, y0 + 8, slot }; }
	void line(unsigned char* c, int x0, int y0, int x1, int y1, unsigned char col)
	{
		const int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
		int err = dx + dy;
		for (;;)
		{
			put(c, x0, y0, col);
			if (x0 == x1 && y0 == y1) { break; }
			const int e2 = 2 * err;
			if (e2 >= dy) { err += dy; x0 += sx; }
			if (e2 <= dx) { err += dx; y0 += sy; }
		}
	}
	// Label blocks stacked in a column (x0..x1), each with a leader line to its button on the controller.
	// Lines under a label: "G+ ..." / "GE+ ..." (holding a grip) in orange, the others in green.
	unsigned char extraColor(const char* t) { return (t[0] == ' ' || (t[0] == 'G' && (t[1] == '+' || t[1] == 'E'))) ? ORANGE : GRN_HI; }
	void callouts(unsigned char* c, const VrmFont& f, const Callout* co, int n, bool leftSide, int x0, int x1, int cx, int cy)
	{
		const int colW = x1 - x0;
		bool split[16] = {};
		int lines = 0;
		for (int i = 0; i < n; i++)
		{
			char head[64];
			snprintf(head, sizeof(head), "%s ", co[i].key);
			split[i] = vrm_textWidth(f, head) + vrm_textWidth(f, co[i].text) > colW;
			lines += 1 + (split[i] ? 1 : 0) + (co[i].g1 ? 1 : 0) + (co[i].g2 ? 1 : 0) + (co[i].g3 ? 1 : 0);
		}
		const int top = 21, bottom = 197;
		int gap = n > 1 ? ((bottom - top) - lines * 9) / (n - 1) : 0;
		if (gap > 8) { gap = 8; }
		if (gap < 1) { gap = 1; }
		int y = top + ((bottom - top) - (lines * 9 + (n - 1) * gap)) / 2;
		for (int i = 0; i < n; i++)
		{
			const Callout& k = co[i];
			char head[64];
			snprintf(head, sizeof(head), split[i] ? "%s" : "%s ", k.key);
			const int wk = vrm_textWidth(f, head), wt = vrm_textWidth(f, k.text);
			const int w1 = split[i] ? wk : wk + wt;
			const int tx = leftSide ? x0 : x1 - w1;
			vrm_text(c, f, head, tx, y, k.color, 0);
			// Leader: from the end of the first line to the button.
			const int ay = y + 3;
			const int ax = leftSide ? tx + w1 + 1 : tx - 2;
			const int ex = leftSide ? x1 : x0;
			line(c, ax, ay, ex, ay, 64);
			const int bx = cx + k.tx, by = cy + k.ty;
			line(c, ex, ay, bx, by, 64);
			put(c, bx, by, YELLOW); put(c, bx + 1, by, YELLOW); put(c, bx, by + 1, YELLOW); put(c, bx + 1, by + 1, YELLOW);
			{
				const int txx = split[i] ? (leftSide ? x0 : x1 - wt) : tx + wk;
				if (split[i]) { y += 9; }
				const bool hov = k.slot[0] >= 0 && k.slot[0] == s_hoverSlot;
				if (hov) { fill(c, txx - 1, y - 1, txx + wt, y + 7, GRN_BG); }
				vrm_text(c, f, k.text, txx, y, hov ? YELLOW : RED, RED_SH);
				addSlotRect(txx, y, wt, k.slot[0]);
			}
			y += 9;
			const char* g[3] = { k.g1, k.g2, k.g3 };
			for (int j = 0; j < 3; j++)
			{
				if (!g[j]) { continue; }
				const int w = vrm_textWidth(f, g[j]);
				const int gx = leftSide ? x0 : x1 - w;
				const bool hov = k.slot[j + 1] >= 0 && k.slot[j + 1] == s_hoverSlot;
				if (hov) { fill(c, gx - 1, y - 1, gx + w, y + 7, GRN_BG); }
				vrm_text(c, f, g[j], gx, y, hov ? YELLOW : extraColor(g[j]), 0);
				addSlotRect(gx, y, w, k.slot[j + 1]);
				y += 9;
			}
			y += gap;
		}
	}
	void letter(unsigned char* c, const VrmFont& f, const char* s, int cx, int cy)
	{
		const int w = vrm_textWidth(f, s);
		vrm_text(c, f, s, cx - w / 2, cy - 3, 72, -1);
	}
}

const char* vrm_actionName(int a)
{
	static const char* n[XRA_COUNT] = { "NADA", "USAR / ABRIR", "PULAR", "AGACHAR", "LANTERNA", "MAPA", "VISÃO NOTURNA",
		"MÁSCARA DE GÁS", "BOTAS DE GELO", "ARMA ANTERIOR", "GUARDAR ARMA", "SALVAR RÁPIDO", "CARREGAR RÁPIDO", "TABLET MISSÃO", "ZOOM DO MAPA" };
	return (a >= 0 && a < XRA_COUNT) ? n[a] : "NADA";
}
const char* vrm_slotName(int sl)
{
	static const char* n[XRB_COUNT] = { "A", "GD + A", "GE + A", "B", "GD + B", "GE + B", "X", "GD + X", "GE + X",
		"Y", "GD + Y", "GE + Y", "GATILHO ESQ.", "GE + GATILHO ESQ.", "CLIQUE DO ANALÓGICO DIR.", "DESCANSO ESQ. (SEGURAR)", "DESCANSO DIR. (SEGURAR)" };
	return (sl >= 0 && sl < XRB_COUNT) ? n[sl] : "";
}

void vrm_drawHelp(unsigned char* c, const VrmImage* frames, int frameCount, const VrmFont& font, int hover, const int* binds)
{
	// No border of its own: on the tablet the page sits in the configuration frame like the other pages.
	fill(c, 0, 0, 319, 199, PANEL);
	fill(c, 0, 0, 319, 14, 73);
	const char* title = "CONTROLES";
	vrm_text(c, font, title, 160 - vrm_textWidth(font, title) / 2, 5, GRN, GRN_BG);

	// Back button in the title bar, and "default" on the left.
	const bool hov = hover == VRM_BTN_OK;
	fill(c, 262, 4, 313, 13, hov ? GRN_DK : GRN_BG);
	vrm_text(c, font, "VOLTAR", 288 - vrm_textWidth(font, "VOLTAR") / 2, 5, hov ? GRN_HI : GRN, -1);
	const bool hovD = hover == VRM_BTN_LEFT;
	fill(c, 6, 4, 57, 13, hovD ? GRN_DK : GRN_BG);
	vrm_text(c, font, "PADRÃO", 32 - vrm_textWidth(font, "PADRÃO") / 2, 5, hovD ? GRN_HI : GRN, -1);
	s_hoverSlot = (hover >= VRM_SLOT_BASE && hover < VRM_SLOT_BASE + XRB_COUNT) ? hover - VRM_SLOT_BASE : -1;
	s_slotRectCount = 0;

	// Controllers (top view) in the middle; every command has a callout with a leader line to its
	// button. Line 1: button and action; orange lines: the same button while holding a grip.
	const int ly = 62, lx = 104, rx = 160;
	controller(c, c_ctlLeft, lx, ly);
	controller(c, c_ctlRight, rx, ly);
	letter(c, font, "Y", lx + 14, ly + 19);
	letter(c, font, "X", lx + 18, ly + 32);
	letter(c, font, "B", rx + 40, ly + 19);
	letter(c, font, "A", rx + 36, ly + 32);
	vrm_text(c, font, "ESQUERDO", lx + 28 - vrm_textWidth(font, "ESQUERDO") / 2, 22, YELLOW, 0);
	vrm_text(c, font, "DIREITO", rx + 28 - vrm_textWidth(font, "DIREITO") / 2, 22, YELLOW, 0);
	vrm_text(c, font, "G+ GARRA DO LADO", 160 - vrm_textWidth(font, "G+ GARRA DO LADO") / 2, 33, ORANGE, 0);
	vrm_text(c, font, "GE+ GD+ OUTRA GARRA", 160 - vrm_textWidth(font, "GE+ GD+ OUTRA GARRA") / 2, 42, ORANGE, 0);
	vrm_text(c, font, "CLIQUE PARA TROCAR", 160 - vrm_textWidth(font, "CLIQUE PARA TROCAR") / 2, 51, YELLOW, 0);

	auto nm = [&](int sl) { return vrm_actionName(binds ? binds[sl] : 0); };
	static char b[16][48];
	auto fmt = [&](int i, const char* pre, int sl) { snprintf(b[i], sizeof(b[i]), "%s%s", pre, nm(sl)); return (const char*)b[i]; };
	const Callout left[] =
	{
		{ 26, 7,  RED_HI, "GATILHO", nm(XRB_TRIGL), fmt(0, "G+ ", XRB_GE_TRIGL), nullptr, nullptr, { XRB_TRIGL, XRB_GE_TRIGL, -1, -1 } },
		{ 14, 18, WHITE,  "Y", nm(XRB_Y), fmt(1, "G+ ", XRB_GE_Y), fmt(2, "GD+ ", XRB_GD_Y), nullptr, { XRB_Y, XRB_GE_Y, XRB_GD_Y, -1 } },
		{ 34, 22, GRN_HI, "ANALÓG.", "ANDAR", "CLIQUE: TROTAR", "G+ CLIQUE:", "  CORRER", { -1, -1, -1, -1 } },
		{ 40, 30, YELLOW, "DESCANSO", nm(XRB_REST_L), nullptr, nullptr, nullptr, { XRB_REST_L, -1, -1, -1 } },
		{ 18, 31, WHITE,  "X", nm(XRB_X), fmt(3, "G+ ", XRB_GE_X), fmt(4, "GD+ ", XRB_GD_X), nullptr, { XRB_X, XRB_GE_X, XRB_GD_X, -1 } },
		{ 29, 41, WHITE,  "MENU", "MENU DO JOGO", nullptr, nullptr, nullptr, { -1, -1, -1, -1 } },
		{ 44, 44, ORANGE, "GARRA", "MODIFICADOR", nullptr, nullptr, nullptr, { -1, -1, -1, -1 } },
	};
	const Callout right[] =
	{
		{ 29, 7,  RED_HI, "GATILHO", "ATIRAR", "G+ TIRO ALTERN.", nullptr, nullptr, { -1, -1, -1, -1 } },
		{ 40, 18, WHITE,  "B", nm(XRB_B), fmt(5, "G+ ", XRB_GD_B), fmt(6, "GE+ ", XRB_GE_B), nullptr, { XRB_B, XRB_GD_B, XRB_GE_B, -1 } },
		{ 12, 22, GRN_HI, "ANALÓG.", "GIRAR", "CIMA/BAIXO: ARMA", fmt(7, "CLIQUE: ", XRB_R3), "SEGURE: ROLETA", { -1, -1, XRB_R3, -1 } },
		{ 6, 30, YELLOW, "DESCANSO", nm(XRB_REST_R), nullptr, nullptr, nullptr, { XRB_REST_R, -1, -1, -1 } },
		{ 36, 31, WHITE,  "A", nm(XRB_A), fmt(8, "G+ ", XRB_GD_A), fmt(9, "GE+ ", XRB_GE_A), nullptr, { XRB_A, XRB_GD_A, XRB_GE_A, -1 } },
		{ 11, 44, ORANGE, "GARRA", "MODIFICADOR", nullptr, nullptr, nullptr, { -1, -1, -1, -1 } },
	};
	callouts(c, font, left, 7, true, 3, 101, lx, ly);
	callouts(c, font, right, 6, false, 219, 316, rx, ly);

	// Gestures under the controllers.
	const struct { unsigned char color; const char* text; } ges[] =
	{
		{ YELLOW, "GESTOS" },
		{ RED, "SOCO: MÃO VAZIA" },
		{ RED, "OMBRO: PEGA ARMA" },
	};
	for (int i = 0; i < 3; i++)
	{
		vrm_text(c, font, ges[i].text, 160 - vrm_textWidth(font, ges[i].text) / 2, 150 + i * 10, ges[i].color, i == 0 ? 0 : RED_SH);
	}
}

int vrm_hitHelp(int x, int y)
{
	if (x >= 258 && x <= 316 && y >= 2 && y <= 16) { return VRM_BTN_OK; }
	if (x >= 4 && x <= 60 && y >= 2 && y <= 16) { return VRM_BTN_LEFT; }
	for (int i = 0; i < s_slotRectCount; i++)
	{
		const SlotRect& r = s_slotRects[i];
		if (x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) { return VRM_SLOT_BASE + r.slot; }
	}
	return -1;
}

// ---------------- Function picker (remap one button slot) ----------------
namespace
{
	void pickCell(int i, int* x0, int* y0, int* x1, int* y1)
	{
		const int col = i % 2, row = i / 2;
		*x0 = 14 + col * 150; *x1 = *x0 + 142;
		*y0 = 44 + row * 18; *y1 = *y0 + 14;
	}
}
void vrm_drawPick(unsigned char* c, const VrmFont& font, int slot, int current, int hover)
{
	fill(c, 0, 0, 319, 199, PANEL);
	fill(c, 0, 0, 319, 14, 73);
	const char* title = "ESCOLHA A FUNÇÃO";
	vrm_text(c, font, title, 160 - vrm_textWidth(font, title) / 2, 5, GRN, GRN_BG);
	const bool hov = hover == VRM_BTN_OK;
	fill(c, 262, 4, 313, 13, hov ? GRN_DK : GRN_BG);
	vrm_text(c, font, "VOLTAR", 288 - vrm_textWidth(font, "VOLTAR") / 2, 5, hov ? GRN_HI : GRN, -1);
	char head[64];
	snprintf(head, sizeof(head), "BOTÃO: %s", vrm_slotName(slot));
	vrm_text(c, font, head, 160 - vrm_textWidth(font, head) / 2, 24, YELLOW, 0);
	for (int i = 0; i < XRA_COUNT; i++)
	{
		int x0, y0, x1, y1; pickCell(i, &x0, &y0, &x1, &y1);
		const bool cur = (i == current), h = (i == hover);
		fill(c, x0, y0, x1, y1, h ? GRN_DK : (cur ? GRN_BG : 74));
		const char* t = vrm_actionName(i);
		vrm_text(c, font, t, (x0 + x1) / 2 - vrm_textWidth(font, t) / 2, y0 + 4, cur ? GRN_HI : (h ? YELLOW : RED), cur ? -1 : RED_SH);
	}
	const char* hint = "APONTE E APERTE A (OU O GATILHO)";
	vrm_text(c, font, hint, 160 - vrm_textWidth(font, hint) / 2, 186, ORANGE, 0);
}
int vrm_hitPick(int x, int y)
{
	if (x >= 258 && x <= 316 && y >= 2 && y <= 16) { return VRM_BTN_OK; }
	for (int i = 0; i < XRA_COUNT; i++)
	{
		int x0, y0, x1, y1; pickCell(i, &x0, &y0, &x1, &y1);
		if (x >= x0 && x <= x1 && y >= y0 && y <= y1) { return i; }
	}
	return -1;
}
