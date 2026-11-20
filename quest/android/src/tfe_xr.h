#pragma once
// SDL mouse "which" id of the pointer events synthesized from the right controller ray.
#define TFE_XR_MOUSE_ID 0x5652u
// OpenXR layer for The Force Engine on Meta Quest (GLES 3.2).
#ifdef __cplusplus
extern "C" {
#endif

// VR options (files/tfe_vr.ini), editable from the "VR" tab of the settings menu.
// Remappable controller buttons (VR menu > controls page). Slots: a button alone, with the right grip
// (GD) or with the left grip (GE); each slot runs one action.
enum
{
	XRB_A = 0, XRB_GD_A, XRB_GE_A, XRB_B, XRB_GD_B, XRB_GE_B, XRB_X, XRB_GD_X, XRB_GE_X,
	XRB_Y, XRB_GD_Y, XRB_GE_Y, XRB_TRIGL, XRB_GE_TRIGL, XRB_R3, XRB_REST_L, XRB_REST_R, XRB_COUNT
};
enum
{
	XRA_NONE = 0, XRA_USE, XRA_JUMP, XRA_CROUCH, XRA_LAMP, XRA_MAP, XRA_NIGHT, XRA_MASK, XRA_CLEATS,
	XRA_LASTWPN, XRA_HOLSTER, XRA_QSAVE, XRA_QLOAD, XRA_PDA, XRA_MAPZOOM, XRA_COUNT
};
typedef struct TfeVrOptions
{
	int   snapTurn;          // 1 = snap turn, 0 = smooth turn
	float snapAngle;         // degrees
	float smoothSpeed;       // smooth turn speed multiplier
	int   runMode;           // 1 = click runs until you stop, 0 = run while held
	float comfortVignette;   // 0 = off .. 1 = strong
	int   physicalCrouch;
	float crouchDepth;       // meters below standing height
	float heightOffset;      // meters added to the eye height (seated play)
	int   laser;             // laser sight
	int   wristHud;          // health/shields on the wrist computer, ammo on the weapon
	int   mapMode;           // 0 = on the wrist screen, 1 = hologram projected above the wrist
	int   lazyScreen;        // messages screen follows the head
	float screenDistance;    // meters
	int   haptics;
	float hapticStrength;    // 0..1
	int weaponStyle;         // 0 = smooth high resolution, 1 = voxel, 2 = fine voxel (3x the voxels), 3 = Blender models (files/dfmod_weapons.bin), 4 = DFmod models (files/dfmod_combined.bin)
	int menuRoom;            // menus and cutscenes in a ship room (1) or floating in the dark (0)
	int wallMode;            // weapon at walls: 0 the wall hides it ("engolida"), 1 always in front
	int rightHand;           // right hand drawn holding the weapon (1) or only the weapon (0)
	float pdaScale;          // PDA size (1 = 17 cm screen)
	int gasMaskView;         // gas mask frame and lens seen from inside while wearing it
	int walkDir;             // walk direction: 0 head, 1 left controller, 2 right controller
	int weaponSpread;        // 1 original random spread of the weapons, 0 reduced to a third
	int fpsDefaultDone;      // 0.30: the FPS counter was turned off once (it is off by default)
	int langDefaultDone;     // 0.33: Portuguese (captions language "pt", subtitles on) was set once
	int binds[XRB_COUNT];   // action per button slot (XRA_*)
	int pdaBindDone;         // 0.34: the mission tablet got its own button (Y) once
	int zoomBindDone;        // 0.35: the map zoom got its own button (GD+Y) once
	int voxels;              // 0.39: voxel models (Dark Forces Voxel Pack) in place of the sprites
} TfeVrOptions;
TfeVrOptions* tfe_xr_options(void);
void tfe_xr_default_binds(int binds[XRB_COUNT]);
void tfe_xr_save_options(void);
// PDA bezel art from the game files (configuration screen frame), RGBA with row 0 at the top, and the
// screen window inside it in pixels.
void tfe_xr_set_pda_bezel(int kind, const unsigned char* rgba, int w, int h, int x0, int y0, int x1, int y1);   // 0 menus, 1 mission PDA
void tfe_xr_set_pda_canvas(float x0, float y0, float x1, float y1);   // the PDA's 320x200 canvas in the screen (normalized, top-down)
void tfe_xr_request_recenter(void);

// Called once the SDL GLES context exists. Returns 1 when VR is active.
int  tfe_xr_init(int screenWidth, int screenHeight);
int  tfe_xr_active(void);
// Main loop: called after SDL events are processed, before the game update.
void tfe_xr_begin_frame(void);
// Called from the render backend instead of SDL_GL_SwapWindow.
void tfe_xr_end_frame(void);
// Framebuffer used in place of the window back buffer (0 when VR is off).
unsigned int tfe_xr_screen_fbo(void);

// World rendering (called by the GPU sector renderer).
int  tfe_xr_world_ready(void);          // 1 when the eyes can be rendered this frame
// Pose of a view relative to the body: eye 0/1, or -1 for the center (head).
// quat = x,y,z,w (OpenXR axes), pos in meters, fov = left,right,up,down tangents.
void tfe_xr_get_view(int eye, float quat[4], float pos[3], float fovTan[4]);
void tfe_xr_bind_eye(int eye, int* width, int* height);
void tfe_xr_unbind_eye(int eye);
float tfe_xr_units_per_meter(void);
// Right controller aim: forward direction and position (OpenXR app space, meters). Returns 0 if not tracked.
int  tfe_xr_get_aim(float dir[3], float pos[3]);
// 3D hands, drawn per eye after the world.
void tfe_xr_draw_hands(int eye);
int  tfe_xr_hands_need_texture(void);
void tfe_xr_set_hand_texture(const unsigned char* rgba, int w, int h);
// Menu room surfaces: the game's own textures (RGBA rows bottom-up), see tfe_xr_room.h.
int  tfe_xr_room_needs_textures(void);
// In-game pause menu shown on a PDA in the left hand (call every frame while it is open).
void tfe_xr_set_pause_pda(int on);
void tfe_xr_set_room_texture(int kind, const unsigned char* rgba, int w, int h, float metersPerTexel, float offU, float offV);
// Voxel weapons from the pickup sprites (RGBA rows bottom-up, alpha 0 = empty).
int  tfe_xr_weapon_needs_sprite(int weaponId);
void tfe_xr_set_weapon_sprite(int weaponId, const unsigned char* rgba, int w, int h);
int  tfe_xr_weapon_needs_view(int weaponId);
void tfe_xr_set_owned_weapons(int mask);   // bit = WeaponID, for the weapon wheel
void tfe_xr_set_weapon_view(int weaponId, const unsigned char* rgba, int w, int h, int x0, int y0);
// Controller cursor on the screen quad (window pixels). Returns 0 when not pointing at it.
int  tfe_xr_get_cursor(int* x, int* y, int* dx, int* dy);

// Game state for the VR presentation (called by the Dark Forces code each frame).
void tfe_xr_set_weapon(int weaponId, int visible, int ammoEmpty);   // -1 = none
void tfe_xr_on_fire(int weaponId);                                  // haptics + muzzle flash
void tfe_xr_on_damage(float amount01);                              // haptics
void tfe_xr_set_light(float ambient01, float flash01, int headlamp);
// HUD pieces in screen space, normalized, y measured from the bottom: x, y, w, h.
void tfe_xr_set_hud_rects(const float left[4], const float right[4]);
// Locomotion helpers.
int  tfe_xr_take_yaw(void);              // pending snap turn, Dark Forces angle units (16384 = 360)
int  tfe_xr_physical_crouch(void);       // 1 while the player is physically crouching
void tfe_xr_set_gasmask(int on);
void tfe_xr_set_pda_image(const unsigned char* rgba);   // 320x200 RGBA of the mission tablet (row 0 at the top)
int  tfe_xr_head_yaw_units(void);   // head yaw relative to the body (Dark Forces angle units, right positive)         // the player wears the gas mask
void tfe_xr_set_eye_drop(float units);   // how much the game lowered the eye (crouch, low ceilings)
int  tfe_xr_take_cheat(void);
void tfe_xr_request_cheat(int id);   // CheatID, applied when the game runs again
double tfe_xr_time_scale(void);         // test scripts: game time scale (1 = normal)
void tfe_xr_request_keyboard(void);      // a text field is active this frame
void tfe_xr_set_automap(int on);         // automap shown: map moves to the wrist computer            // test scripts: pending Dark Forces cheat id (0 = none)

#ifdef __cplusplus
}
#endif
