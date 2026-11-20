#pragma once
// Procedural 3D hands and weapons for the Quest port (drawn in each eye after the world).
#include <TFE_RenderBackend/Win32OpenGL/gl.h>
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#include <jni.h>
#include <EGL/egl.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

struct HandInput
{
	bool valid;          // grip pose tracked
	XrPosef pose;        // grip pose in app space
	bool aimValid;       // aim pose tracked
	XrPosef aimPose;     // aim pose in app space (-Z = pointing ray, +Y up)
	float grip;          // 0..1 middle/ring/pinky curl
	float trigger;       // 0..1 index curl
	bool triggerTouch;   // index resting on the trigger
	bool thumbTouch;     // thumb on a button/stick/thumbrest
};

struct HandsFrame
{
	int weaponId;        // Dark Forces WeaponID, -1 = nothing in hand
	bool ammoEmpty;      // thermal detonators / mines with no ammo: empty fist
	float flash;         // 0..1 muzzle flash intensity
	float light;         // lighting multiplier for hands and weapon
	bool laser;          // draw the laser sight
	float laserColor[3];
	bool beam;           // hologram projector light: cone from the emitter to the 4 corners
	XrVector3f beamFrom;
	XrVector3f beamTo[4];
	bool wheel;          // weapon wheel open around the right hand
	XrPosef wheelPose;   // center and orientation (faces the eyes)
	int wheelOwned;      // bit = WeaponID
	int wheelSel;        // highlighted slot, -1 = none
	bool pda;            // pause menu: PDA held in the left hand
	bool point = false;
	float cycAlpha = 0.0f;   // weapon strip by the right hand (stick up/down): 0 hidden
	int cycId[3] = { 0, 0, 0 };   // previous, current, next weapon
	float cycShift = 0.0f;   // slide in slots (animation after a press)
	float cycPulse = 0.0f;   // 1 right after a press, fading  // right hand pointing at the PDA (cursor): weapon put away, index out
	XrPosef pdaPose;     // its screen (quad faces +Z)
	bool pdaDot = false; // pointer on the PDA frame (outside the screen layer): a small dot there
	XrVector3f pdaDotPos = { 0, 0, 0 };
};

void hands_init();
// View-projection matrix (column major) of an eye, app space.
void hands_viewProj(const XrPosef& eyePose, const XrFovf& fov, float zn, float zf, float out[16]);
// Right hand and weapon drawn scaled by `scale` about `center` (the head), app space: the eyes see the
// same image, closer, so a weapon held into a wall stays in front of it without moving the hand.
void hands_setCompression(const XrVector3f& center, float scale);
void hands_setTexture(const unsigned char* rgba, int w, int h);
bool hands_hasTexture();
// eyePose/fov in app space; unitsPerMeter matches the world depth mapping.
void hands_draw(const XrPosef& eyePose, const XrFovf& fov, float unitsPerMeter, const HandInput hands[2], const HandsFrame& frame, float dt);
// Muzzle of the weapon held by the right hand, app space. Returns false if the hand is not tracked.
bool hands_muzzle(const HandInput& right, int weaponId, XrVector3f* pos, XrVector3f* dir);
// Wrist panel pose (HUD) for a hand, app space; quad faces +Z of the returned pose.
bool hands_wristPanel(const HandInput& hand, int side, XrPosef* pose);
// Ammo panel pose on the held weapon (false when the right hand has no weapon model).
// Ammo panel on the left wall of the weapon: 1 shown, -1 hidden (faces away / mesh not ready), 0 no spot (use the wrist).
int hands_weaponPanel(const HandInput& right, int weaponId, XrPosef* pose, const XrVector3f& head);
// Voxel weapon from a side-view sprite (RGBA, rows bottom-up, alpha 0 = empty).
void hands_setWeaponSprite(int weaponId, const unsigned char* rgba, int w, int h);
bool hands_hasWeaponSprite(int weaponId);
// Original first-person weapon art (RGBA rows top-down) drawn at (x0, y0) on the 320x200 screen:
// projected onto the voxel model so it looks like the original from the same angle.
// 0 = smooth high resolution, 1 = voxel, 2 = fine voxel (3x the voxels).
void hands_setWeaponStyle(int style);
void hands_setRightHand(int on);   // draw the right hand holding the weapon
void hands_setPdaScale(float scale);   // PDA size (0.6 .. 1.6), grows from the corner the left hand holds
float hands_pdaScreenWidth();          // meters, at the current size
void hands_setPdaFull(int on);         // 0 menus (configuration frame, live), 1 mission PDA (live), 2 frameless page fitted in the static configuration frame
float hands_pdaScreenHeight();         // meters, at the current size
bool hands_pdaWindow(float win[4]);   // window of the current face (320x200 canvas coordinates) when the screen layer is cropped to it
void hands_setPdaLiveTexture(unsigned int id, int w, int h);   // live game canvas for the case face (0 = static art)
// PDA bezel art (RGBA, row 0 at the top) and the screen window inside it (pixels).
void hands_setPdaBezel(int kind, const unsigned char* rgba, int w, int h, int x0, int y0, int x1, int y1);   // 0 menus, 1 mission
bool hands_needsWeaponView(int weaponId);
void hands_setWeaponView(int weaponId, const unsigned char* rgba, int w, int h, int x0, int y0);
// Left wrist computer: screen pose (faces +Z) and the pose of the holographic map above it.
bool hands_watch(const HandInput& left, XrPosef* face, XrPosef* holo, const XrVector3f& headApp);
// Pause menu PDA in the left hand: pose of its screen (quad faces +Z, image up +Y).
bool hands_pdaScreen(const HandInput& left, XrPosef* screen);
const float c_pdaScreenW = 0.17f;
#ifdef HANDS_HOST_TEST
void hands_setPdaParams(const float* p);   // tx ty tz tilt yaw curlIndex curlOthers curlThumb
#endif
