#pragma once
// Dark Forces style configuration screens for the Quest port, drawn on the original 320x200
// "config" art from MENU.LFD with the game's font8 (pure drawing code, no engine dependencies).

struct VrmImage { int w, h; const unsigned char* px; };   // absolute 320x200 coordinates, 0 = transparent
struct VrmFont  { int first, num, w, h, space; const unsigned char* widths; const unsigned char* data; };

enum
{
	VRM_ROWS = 12,
	VRM_BTN_LEFT = 100,     // bottom-left button (next page)
	VRM_BTN_OK = 101,       // bottom-right OK
	VRM_SLOT_BASE = 200,    // help page: VRM_SLOT_BASE + XRB_* for a remappable button line
};

struct VrmRow
{
	const char* label;      // UTF-8 (Portuguese accents are drawn over the font8 glyphs)
	char value[32];         // text shown in the bar
	float fill;             // 0..1 bar fill (sliders), < 0 for a plain choice
};

struct VrmPage
{
	const char* title;      // replaces the baked "CONFIGURATION" header text
	int rowCount;
	VrmRow rows[VRM_ROWS];
	const char* leftButton;
	int hover;              // element under the cursor (row index, VRM_BTN_*), -1 = none
	int pressed;            // element held down, -1 = none
};

// Parses a Landru .FONT file image.
bool vrm_parseFont(const unsigned char* data, int size, VrmFont* out);
int  vrm_textWidth(const VrmFont& font, const char* utf8);
void vrm_text(unsigned char* canvas, const VrmFont& font, const char* utf8, int x, int y, unsigned char color, int shadow);

// frames = the "config" ANIM (frame 0 = panel, 1/2 = OK, 3/4 = left button).
void vrm_drawConfig(unsigned char* canvas, const VrmImage* frames, int frameCount, const VrmFont& font, const VrmPage& page);
// Returns the element at a canvas position; for rows also the horizontal position inside the bar (0..1).
int  vrm_hitConfig(int x, int y, const VrmPage& page, float* barPos);

// Controls help page (full 320x200 canvas). hover = VRM_BTN_OK when the cursor is on the back button.
void vrm_drawHelp(unsigned char* canvas, const VrmImage* frames, int frameCount, const VrmFont& font, int hover, const int* binds);
int  vrm_hitHelp(int x, int y);   // VRM_BTN_OK (back), VRM_BTN_LEFT (defaults), VRM_SLOT_BASE + slot, -1
// Function picker for one button slot: the actions (XRA_*) in two columns, the current one marked.
void vrm_drawPick(unsigned char* canvas, const VrmFont& font, int slot, int current, int hover);
int  vrm_hitPick(int x, int y);   // action index, VRM_BTN_OK (back) or -1
const char* vrm_actionName(int action);
const char* vrm_slotName(int slot);
