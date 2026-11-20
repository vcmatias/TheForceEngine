// OpenXR layer for The Force Engine on Meta Quest.
// - Stereo world: the GPU sector renderer draws each eye into an OpenXR swapchain.
// - Everything 2D (TFE menus, HUD, weapon, Dark Forces screens) is rendered into an
//   offscreen "screen" framebuffer that replaces the window back buffer and is shown
//   as a quad layer. While the world is visible, black pixels of that screen become
//   transparent so the HUD floats over the stereo view.
// - Touch controllers feed TFE_Input as a game controller; the right controller ray
//   drives the mouse on the screen quad when the game is not in relative mouse mode.
#include "tfe_xr.h"
#include "tfe_xr_hands.h"
#include "tfe_xr_font.h"
#include "tfe_xr_room.h"
#include "tfe_xr_mask.h"
static void headCorrection(float c[3]);
extern "C" float tfe_df_vr_wall_penetration(const float off[3], float margin);
extern "C" void tfe_df_vr_map_zoom_out(void);
extern "C" void tfe_df_vr_map_zoom_reset(void);

#include <SDL.h>
#include <jni.h>
#include <EGL/egl.h>
#include <android/log.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

#include <TFE_RenderBackend/Win32OpenGL/gl.h>
#include <TFE_Input/input.h>
#include <TFE_Input/inputEnum.h>
#include <TFE_FrontEndUI/frontEndUi.h>

#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "TFEQuestXR", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TFEQuestXR", __VA_ARGS__)

#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

namespace
{
	// Tunables (meters / units).
	const float c_unitsPerMeter = 3.3f;      // Dark Forces units are close to feet.
	const float c_screenDistance = 1.6f;     // Screen quad distance.
	const float c_screenWidthBase = 1.7f;    // Screen quad width at 1.6 m.

	XrInstance s_instance = XR_NULL_HANDLE;
	XrSystemId s_system = XR_NULL_SYSTEM_ID;
	XrSession  s_session = XR_NULL_HANDLE;
	XrSpace    s_appSpace = XR_NULL_HANDLE;
	XrSpace    s_viewSpace = XR_NULL_HANDLE;
	XrSessionState s_state = XR_SESSION_STATE_UNKNOWN;
	bool s_running = false;
	bool s_active = false;
	bool s_srgbWriteControl = false;
	GLenum s_colorFormat = GL_RGBA8;

	struct Swapchain
	{
		XrSwapchain handle = XR_NULL_HANDLE;
		int width = 0, height = 0;
		std::vector<XrSwapchainImageOpenGLESKHR> images;
		std::vector<GLuint> fbos;
		GLuint depth = 0;
		uint32_t index = 0;
		bool acquired = false;
	};
	Swapchain s_eye[2];
	Swapchain s_quad;
	Swapchain s_hud[2];          // wrist panels (left: health/shields, right: ammo)
	const int c_hudW = 768, c_hudH = 480;
	Swapchain s_holo;              // map hologram
	const float c_holoW = 0.18f;   // hologram width (m)
	// Part of the screen shown as the map (x, y, w, h; GL uv): the area around the player.
	const float c_mapCrop[4] = { 0.18f, 0.18f, 0.64f, 0.64f };
	bool s_holoActive = false;
	bool s_mapHolo = false;       // current map view: hologram (true) or the watch screen
	int s_mapStage = 0;           // 0 = first view shown, 1 = second view shown
	XrPosef s_holoPose = { {0,0,0,1}, {0,0,0} };

	// User options (files/tfe_vr.ini).
	TfeVrOptions s_opt =
	{
		1, 45.0f, 1.0f,          // snap turn, angle, smooth speed
		1, 0.6f,                 // run mode, comfort vignette
		1, 0.35f, 0.0f,          // physical crouch, depth, height offset
		1, 1, 1,                 // laser, wrist HUD, map mode (always hologram)
		1, 1.6f,                 // screen follows head, screen distance
		1, 1.0f,                 // haptics, strength
		3,                       // weapon style: DFmod models
		1,                       // menu room
		0,                       // weapon in walls: hidden by the wall
		1,                       // right hand on the weapon
		1.0f,                    // PDA size
		1,                       // gas mask view
		0,                       // walk direction: 0 head, 1 left controller, 2 right controller
		1,                       // weapon spread: 1 original, 0 reduced
		0,                       // FPS default applied
		0,                       // Portuguese default applied
		// Button slots: A, GD+A, GE+A, B, GD+B, GE+B, X, GD+X, GE+X, Y, GD+Y, GE+Y, left trigger, GE+left trigger, R3 click
		{ XRA_USE, XRA_NIGHT, XRA_QSAVE, XRA_JUMP, XRA_CLEATS, XRA_QLOAD, XRA_CROUCH, XRA_NONE, XRA_MASK,
		  XRA_PDA, XRA_MAPZOOM, XRA_MAP, XRA_LASTWPN, XRA_HOLSTER, XRA_LAMP, XRA_MAP, XRA_NONE },
		1,                       // mission tablet bind applied (new installs have it)
		1,                       // map zoom bind applied (new installs have it)
		1,                       // voxel models on
	};
	// Screen width keeps the same angular size at any distance.
	float screenW() { return c_screenWidthBase * fmaxf(0.8f, fminf(3.0f, s_opt.screenDistance)) / 1.6f; }

	// Game state for the presentation.
	int s_weaponId = -1;
	bool s_weaponAmmoEmpty = false;
	float s_flash = 0.0f;
	float s_ambient = 1.0f, s_weaponFlashLight = 0.0f;
	bool s_headlamp = false;
	float s_hudRect[2][4] = {};
	bool s_hudRectValid = false;
	int s_pendingYaw = 0;
	bool s_snapArmed = true;
	bool s_weaponArmed = true;
	bool s_physCrouch = false;
	float s_eyeDrop = 0.0f;
	XrAction s_actHaptic = XR_NULL_HANDLE;

	XrViewConfigurationView s_viewConfig[2];
	XrView s_views[2];
	XrPosef s_headPose = { {0,0,0,1}, {0,0,0} };
	XrPosef s_headPoseApp = { {0,0,0,1}, {0,0,0} };   // head in app space
	XrFrameState s_frameState = { XR_TYPE_FRAME_STATE };
	bool s_frameBegun = false;
	bool s_viewsValid = false;
	bool s_worldThisFrame = false;
	bool s_worldLastFrame = false;
	bool s_eyeRendered[2] = { false, false };

	// Screen (replaces the window back buffer).
	int s_screenW = 0, s_screenH = 0;
	GLuint s_screenFbo = 0, s_screenTex = 0, s_screenDepth = 0;
	GLuint s_keyProgram = 0, s_keyVao = 0;
	GLint s_keyLocDilate = -1, s_keyLocHolo = -1;
	GLint s_keyLocTex = -1, s_keyLocKey = -1, s_keyLocCursor = -1, s_keyLocUvRect = -1, s_keyLocMask0 = -1, s_keyLocMask1 = -1;
	bool s_cursorVisible = false;
	XrPosef s_quadPose = { {0,0,0,1}, {0,0,-c_screenDistance} };
	// In-game pause: the menu is on a PDA in the left hand and the (paused) world stays visible.
	bool s_pdaMenu = false, s_pdaValid = false;
	unsigned char s_pdaImg[320 * 200 * 4];   // mission tablet image from the game (CPU composed)
	bool s_pdaImgNew = false;
	int s_pdaImgFrames = 0;
	GLuint s_pdaImgTex = 0;
	bool s_pdaImgShown = false;   // the tablet face shows the CPU image this frame
	bool s_inRoomDraw = false;
	// Frozen world behind the hand PDA (the DF PDA switches the engine to the software renderer, so the
	// world is not drawn while it is open): a half resolution copy of each eye, taken before the hands are
	// drawn, shown again with a rotation reprojection.
	GLuint s_snapTex[2] = { 0, 0 }, s_snapFbo[2] = { 0, 0 }, s_snapProg = 0, s_snapVao = 0;
	int s_snapW = 0, s_snapH = 0, s_snapReq = 0, s_snapTick = 0;
	bool s_snapValid = false;
	XrPosef s_snapPose[2];
	XrFovf s_snapFov[2];
	int s_pdaFrames = 0;
	XrPosef s_pdaPose = { {0,0,0,1}, {0,0,0} };
	float s_quadYaw = 0.0f;
	bool s_quadFollowing = false;
	double s_lastFrameTime = 0.0;
	float s_frameDt = 1.0f / 72.0f;
	XrVector3f s_prevHandPos[2] = {};
	bool s_prevHandValid[2] = { false, false };
	int s_punchFrames = 0;
	bool s_shoulderArmed = true;
	bool s_twoHanded = false;
	int s_r3Pulse = 0;   // frames of the right stick quick click (its remappable slot)
	// Weapon strip (stick up/down cycling): weapon shown in the middle, last press time and direction.
	int s_cycCur = -1, s_cycDir = 1;
	double s_cycTime = -10.0, s_cycShowUntil = -10.0;
	// Weapon wheel.
	bool s_wheelOpen = false;
	float s_wheelHold = 0.0f;
	int s_lampPulse = 0;
	int s_wheelSel = -1, s_wheelPick = -1;
	int s_ownedWeapons = 1;
	XrVector3f s_wheelCenter = { 0, 0, 0 };
	XrQuaternionf s_wheelOri = { 0, 0, 0, 1 };
	const float c_punchTime = 0.28f;
	float s_punchAnim = 0.0f;      // trigger punch animation time left
	float s_wallPush = 1.0f;       // depth scale of the right hand and weapon (1 = real, less near walls)
	bool s_systemUi = false;
	int s_lastMuzzleLog = 0;
	bool s_automap = false;
	// Frames the screen quad stays hidden: the 2D screen still holds the automap lines right after the
	// map closes (and while the PDA is being opened from the map), which flashed in front of the player.
	int s_screenHide = 0;
	int s_pdaPointHold = 0;
	bool s_gasMask = false;   // the player wears the gas mask (player.cpp)
	float s_maskFade = 0.0f;   // frames left of the right hand pointing at the PDA
	bool s_automapWas = false;
	bool s_runActive = false;
	bool s_runLatched = false;
	float s_vignette = 0.0f, s_moveAmount = 0.0f;
	GLuint s_vigProgram = 0, s_vigVao = 0;
	GLint s_vigLocStrength = -1, s_vigLocSize = -1;

	// Input.
	XrActionSet s_actionSet = XR_NULL_HANDLE;
	XrAction s_actMove = XR_NULL_HANDLE, s_actTurn = XR_NULL_HANDLE;
	XrAction s_actTrigR = XR_NULL_HANDLE, s_actTrigL = XR_NULL_HANDLE;
	XrAction s_actGripR = XR_NULL_HANDLE, s_actGripL = XR_NULL_HANDLE;
	XrAction s_actA = XR_NULL_HANDLE, s_actB = XR_NULL_HANDLE, s_actX = XR_NULL_HANDLE, s_actY = XR_NULL_HANDLE;
	XrAction s_actMenu = XR_NULL_HANDLE, s_actStickL = XR_NULL_HANDLE, s_actStickR = XR_NULL_HANDLE;
	XrAction s_actAimPose = XR_NULL_HANDLE;
	XrSpace s_aimSpaceH[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
	XrAction s_actGripPose = XR_NULL_HANDLE, s_actTrigTouch = XR_NULL_HANDLE, s_actThumbTouch = XR_NULL_HANDLE, s_actRest = XR_NULL_HANDLE;
	XrSpace s_gripSpace[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
	HandInput s_hands[2] = {};
	double s_lastHandTime = 0.0;
	XrSpace s_aimSpace = XR_NULL_HANDLE;
	XrPath s_handPath[2];
	bool s_btnState[CONTROLLER_BUTTON_COUNT] = {};
	bool s_mouseDown = false;
	bool s_triggerWasMouse = false;
	int s_mouseX = -1, s_mouseY = -1;
	XrPosef s_aimPose = { {0,0,0,1}, {0,0,0} };
	// Reference frame (recentered head): everything given to the game is relative to it.
	bool s_refValid = false;
	bool s_recenterRequested = true;
	XrVector3f s_refPos = { 0, 0, 0 };
	float s_refYaw = 0.0f;
	bool s_stickLWas = false;
	bool s_menuWas = false;
	bool s_restHeld[2] = { false, false };
	bool s_aimValid = false;

	const char* resultStr(XrResult r)
	{
		static char buf[XR_MAX_RESULT_STRING_SIZE];
		if (s_instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(s_instance, r, buf))) { return buf; }
		snprintf(buf, sizeof(buf), "%d", (int)r);
		return buf;
	}
#define XR_CHECK(x) do { XrResult _r = (x); if (XR_FAILED(_r)) { LOGE("%s failed: %s", #x, resultStr(_r)); } } while (0)

	XrPath path(const char* s) { XrPath p = XR_NULL_PATH; xrStringToPath(s_instance, s, &p); return p; }

	XrAction makeAction(XrActionType type, const char* name, const char* loc, bool perHand)
	{
		XrActionCreateInfo ci = { XR_TYPE_ACTION_CREATE_INFO };
		ci.actionType = type;
		strncpy(ci.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
		strncpy(ci.localizedActionName, loc, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
		if (perHand) { ci.countSubactionPaths = 2; ci.subactionPaths = s_handPath; }
		XrAction a = XR_NULL_HANDLE;
		XR_CHECK(xrCreateAction(s_actionSet, &ci, &a));
		return a;
	}

	void initInput()
	{
		XrActionSetCreateInfo si = { XR_TYPE_ACTION_SET_CREATE_INFO };
		strcpy(si.actionSetName, "gameplay");
		strcpy(si.localizedActionSetName, "Gameplay");
		XR_CHECK(xrCreateActionSet(s_instance, &si, &s_actionSet));
		s_handPath[0] = path("/user/hand/left");
		s_handPath[1] = path("/user/hand/right");

		s_actMove   = makeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "move", "Move", false);
		s_actTurn   = makeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "turn", "Turn", false);
		s_actTrigR  = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger_r", "Trigger R", false);
		s_actTrigL  = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger_l", "Trigger L", false);
		s_actGripR  = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "grip_r", "Grip R", false);
		s_actGripL  = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "grip_l", "Grip L", false);
		s_actA      = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_a", "A", false);
		s_actB      = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_b", "B", false);
		s_actX      = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_x", "X", false);
		s_actY      = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_y", "Y", false);
		s_actMenu   = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_menu", "Menu", false);
		s_actStickL = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_l", "Stick L", false);
		s_actStickR = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_r", "Stick R", false);
		s_actAimPose = makeAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim pose", true);
		s_actHaptic = makeAction(XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic", true);
		s_actGripPose = makeAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip pose", true);
		s_actTrigTouch = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_touch", "Trigger touch", true);
		s_actThumbTouch = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumb_touch", "Thumb touch", true);
		s_actRest = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbrest", "Thumb rest", true);

		XrActionSuggestedBinding b[] =
		{
			{ s_actMove,   path("/user/hand/left/input/thumbstick") },
			{ s_actTurn,   path("/user/hand/right/input/thumbstick") },
			{ s_actTrigR,  path("/user/hand/right/input/trigger/value") },
			{ s_actTrigL,  path("/user/hand/left/input/trigger/value") },
			{ s_actGripR,  path("/user/hand/right/input/squeeze/value") },
			{ s_actGripL,  path("/user/hand/left/input/squeeze/value") },
			{ s_actA,      path("/user/hand/right/input/a/click") },
			{ s_actB,      path("/user/hand/right/input/b/click") },
			{ s_actX,      path("/user/hand/left/input/x/click") },
			{ s_actY,      path("/user/hand/left/input/y/click") },
			{ s_actMenu,   path("/user/hand/left/input/menu/click") },
			{ s_actStickL, path("/user/hand/left/input/thumbstick/click") },
			{ s_actStickR, path("/user/hand/right/input/thumbstick/click") },
			{ s_actAimPose, path("/user/hand/left/input/aim/pose") },
			{ s_actAimPose, path("/user/hand/right/input/aim/pose") },
			{ s_actHaptic, path("/user/hand/left/output/haptic") },
			{ s_actHaptic, path("/user/hand/right/output/haptic") },
			{ s_actGripPose, path("/user/hand/left/input/grip/pose") },
			{ s_actGripPose, path("/user/hand/right/input/grip/pose") },
			{ s_actTrigTouch, path("/user/hand/left/input/trigger/touch") },
			{ s_actTrigTouch, path("/user/hand/right/input/trigger/touch") },
			{ s_actThumbTouch, path("/user/hand/left/input/thumbrest/touch") },
			{ s_actThumbTouch, path("/user/hand/right/input/thumbrest/touch") },
			{ s_actThumbTouch, path("/user/hand/left/input/thumbstick/touch") },
			{ s_actThumbTouch, path("/user/hand/right/input/thumbstick/touch") },
			{ s_actThumbTouch, path("/user/hand/left/input/x/touch") },
			{ s_actThumbTouch, path("/user/hand/left/input/y/touch") },
			{ s_actThumbTouch, path("/user/hand/right/input/a/touch") },
			{ s_actThumbTouch, path("/user/hand/right/input/b/touch") },
			{ s_actRest, path("/user/hand/left/input/thumbrest/touch") },
			{ s_actRest, path("/user/hand/right/input/thumbrest/touch") },
		};
		XrInteractionProfileSuggestedBinding sb = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		sb.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
		sb.suggestedBindings = b;
		sb.countSuggestedBindings = sizeof(b) / sizeof(b[0]);
		XR_CHECK(xrSuggestInteractionProfileBindings(s_instance, &sb));

		for (int h = 0; h < 2; h++)
		{
			XrActionSpaceCreateInfo asi = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
			asi.action = s_actAimPose;
			asi.subactionPath = s_handPath[h];
			asi.poseInActionSpace.orientation.w = 1.0f;
			XR_CHECK(xrCreateActionSpace(s_session, &asi, &s_aimSpaceH[h]));
		}
		s_aimSpace = s_aimSpaceH[1];
		for (int h = 0; h < 2; h++)
		{
			XrActionSpaceCreateInfo gsi = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
			gsi.action = s_actGripPose;
			gsi.subactionPath = s_handPath[h];
			gsi.poseInActionSpace.orientation.w = 1.0f;
			XR_CHECK(xrCreateActionSpace(s_session, &gsi, &s_gripSpace[h]));
		}

		XrSessionActionSetsAttachInfo ai = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
		ai.countActionSets = 1;
		ai.actionSets = &s_actionSet;
		XR_CHECK(xrAttachSessionActionSets(s_session, &ai));
	}

	bool getBool(XrAction a)
	{
		XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
		gi.action = a;
		XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
		xrGetActionStateBoolean(s_session, &gi, &st);
		return st.isActive && st.currentState;
	}
	float getFloat(XrAction a)
	{
		XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
		gi.action = a;
		XrActionStateFloat st = { XR_TYPE_ACTION_STATE_FLOAT };
		xrGetActionStateFloat(s_session, &gi, &st);
		return st.isActive ? st.currentState : 0.0f;
	}
	XrVector2f getVec2(XrAction a)
	{
		XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
		gi.action = a;
		XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
		xrGetActionStateVector2f(s_session, &gi, &st);
		XrVector2f zero = { 0, 0 };
		return st.isActive ? st.currentState : zero;
	}

	bool getBoolHand(XrAction a, int h)
	{
		XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
		gi.action = a;
		gi.subactionPath = s_handPath[h];
		XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
		xrGetActionStateBoolean(s_session, &gi, &st);
		return st.isActive && st.currentState;
	}

	void setButton(Button b, bool down)
	{
		if (down == s_btnState[b]) { return; }
		s_btnState[b] = down;
		if (down) { TFE_Input::setButtonDown(b); }
		else { TFE_Input::setButtonUp(b); }
	}

	// Quaternion helpers.
	XrVector3f rotate(const XrQuaternionf& q, const XrVector3f& v)
	{
		// v' = v + 2w(q x v) + 2 q x (q x v)
		const XrVector3f u = { q.x, q.y, q.z };
		XrVector3f t = { 2.0f*(u.y*v.z - u.z*v.y), 2.0f*(u.z*v.x - u.x*v.z), 2.0f*(u.x*v.y - u.y*v.x) };
		XrVector3f r = { v.x + q.w*t.x + (u.y*t.z - u.z*t.y), v.y + q.w*t.y + (u.z*t.x - u.x*t.z), v.z + q.w*t.z + (u.x*t.y - u.y*t.x) };
		return r;
	}

	float yawOf(const XrQuaternionf& q)
	{
		const XrVector3f f = rotate(q, { 0, 0, -1 });
		return atan2f(-f.x, -f.z);
	}
	XrQuaternionf mulQ(const XrQuaternionf& a, const XrQuaternionf& b)
	{
		XrQuaternionf r;
		r.w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z;
		r.x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y;
		r.y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x;
		r.z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w;
		return r;
	}
	void placeQuad(float yaw)
	{
		s_quadYaw = yaw;
		const XrQuaternionf q = { 0.0f, sinf(yaw * 0.5f), 0.0f, cosf(yaw * 0.5f) };
		const XrVector3f fwd = rotate(q, { 0, 0, -fmaxf(0.8f, fminf(3.0f, s_opt.screenDistance)) });
		s_quadPose.orientation = q;
		s_quadPose.position = { s_refPos.x + fwd.x, s_refPos.y + fwd.y, s_refPos.z + fwd.z };
	}
	XrVector3f toRefDir(const XrVector3f& d)
	{
		const XrQuaternionf inv = { 0.0f, sinf(-s_refYaw * 0.5f), 0.0f, cosf(-s_refYaw * 0.5f) };
		return rotate(inv, d);
	}
	// App space -> reference frame (origin at the recentered head, yaw aligned).
	XrPosef toRef(const XrPosef& p)
	{
		const XrQuaternionf inv = { 0.0f, sinf(-s_refYaw * 0.5f), 0.0f, cosf(-s_refYaw * 0.5f) };
		XrPosef r;
		r.orientation = mulQ(inv, p.orientation);
		const XrVector3f d = { p.position.x - s_refPos.x, p.position.y - s_refPos.y, p.position.z - s_refPos.z };
		r.position = rotate(inv, d);
		return r;
	}
	void recenter(const XrPosef& head)
	{
		s_refPos = head.position;
		s_refYaw = yawOf(head.orientation);
		s_refValid = true;
		s_recenterRequested = false;
		// Screen quad in front of the new center.
		placeQuad(s_refYaw);
		LOGI("recenter: pos %.2f %.2f %.2f yaw %.1f", s_refPos.x, s_refPos.y, s_refPos.z, s_refYaw * 57.2958f);
	}

	void pushMouseMotion(int x, int y)
	{
		SDL_Event e = {};
		e.type = SDL_MOUSEMOTION;
		e.motion.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
		e.motion.which = TFE_XR_MOUSE_ID;   // the system UI only accepts the VR pointer
		e.motion.x = x; e.motion.y = y;
		e.motion.xrel = (s_mouseX >= 0) ? x - s_mouseX : 0;
		e.motion.yrel = (s_mouseY >= 0) ? y - s_mouseY : 0;
		SDL_PushEvent(&e);
		s_mouseX = x; s_mouseY = y;
	}
	void pushMouseButton(bool down)
	{
		SDL_Event e = {};
		e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
		e.button.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
		e.button.which = TFE_XR_MOUSE_ID;
		e.button.button = SDL_BUTTON_LEFT;
		e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
		e.button.clicks = 1;
		e.button.x = s_mouseX < 0 ? 0 : s_mouseX;
		e.button.y = s_mouseY < 0 ? 0 : s_mouseY;
		SDL_PushEvent(&e);
	}

	float s_scriptAimYaw = 0.0f, s_scriptAimPitch = 0.0f;
	bool s_scriptAim = false;
	void haptic(int hand, float amp, float seconds);
	bool scriptValue(const char* name, float* v0, float* v1);
	// Right controller pointing ray (app space).
	bool aimRay(XrVector3f* origin, XrVector3f* fwd)
	{
		if (s_aimSpace == XR_NULL_HANDLE) { return false; }
		if (s_scriptAim)
		{
			// Scripted ray from the recentered head.
			const float y = s_refYaw + s_scriptAimYaw * 0.0174533f, pch = s_scriptAimPitch * 0.0174533f;
			*fwd = { -sinf(y) * cosf(pch), sinf(pch), -cosf(y) * cosf(pch) };
			*origin = s_refPos;
			return true;
		}
		XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
		if (XR_FAILED(xrLocateSpace(s_aimSpace, s_appSpace, s_frameState.predictedDisplayTime, &loc))) { return false; }
		if (!(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) || !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) { return false; }
		*fwd = rotate(loc.pose.orientation, { 0, 0, -1 });
		*origin = loc.pose.position;
		return true;
	}
	// Ray against a quad of size w x h (meters) at pose; u, v in [0,1] with v = 0 at the top.
	bool rayQuad(const XrPosef& pose, float w, float h, float* u, float* v, float* dist)
	{
		XrVector3f o, fwd;
		if (!aimRay(&o, &fwd)) { return false; }
		const XrVector3f n = rotate(pose.orientation, { 0, 0, 1 });
		const XrVector3f c = pose.position;
		const float denom = fwd.x*n.x + fwd.y*n.y + fwd.z*n.z;
		if (fabsf(denom) < 1e-4f) { return false; }
		const XrVector3f d = { c.x - o.x, c.y - o.y, c.z - o.z };
		const float t = (d.x*n.x + d.y*n.y + d.z*n.z) / denom;
		if (t <= 0.0f) { return false; }
		const XrVector3f hit = { o.x + fwd.x*t - c.x, o.y + fwd.y*t - c.y, o.z + fwd.z*t - c.z };
		const XrVector3f ax = rotate(pose.orientation, { 1, 0, 0 });
		const XrVector3f ay = rotate(pose.orientation, { 0, 1, 0 });
		const float hx = hit.x*ax.x + hit.y*ax.y + hit.z*ax.z;
		const float hy = hit.x*ay.x + hit.y*ay.y + hit.z*ay.z;
		*u = hx / w + 0.5f; *v = 0.5f - hy / h;
		if (dist) { *dist = t; }
		return *u >= 0.0f && *u <= 1.0f && *v >= 0.0f && *v <= 1.0f;
	}
	// Right controller ray against the screen quad; returns pixel coordinates.
	bool pdaActive() { return s_pdaMenu && s_pdaValid; }
	const XrPosef& activeScreenPose() { return pdaActive() ? s_pdaPose : s_quadPose; }
	float activeScreenW() { return pdaActive() ? hands_pdaScreenWidth() : screenW(); }
	// Where the game's 320x200 PDA canvas sits in the screen (normalized, top-down), from pda.cpp.
	float s_pdaCanvas[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	int s_pdaCanvasFrames = 0;   // frames left of the last canvas report (menus and PDA report every frame)
	bool s_pdaFitPage = false;   // frameless page on the tablet: static frame, no live copy
	XrVector3f s_pdaDotPos = { 0, 0, 0 };
	int s_pdaDotFrame = -10, s_frameIndex = 0;
	// Mission PDA on the tablet: the screen layer shows only the canvas window of pda.anim (the case is
	// the PDA frame). win = window in canvas pixels; false when the mission face is not up.
	bool pdaMissionCrop(float win[4])
	{
		if (!pdaActive()) { return false; }
		if (s_pdaFitPage && s_pdaCanvasFrames > 0) { win[0] = 0.0f; win[1] = 0.0f; win[2] = 319.0f; win[3] = 199.0f; return true; }   // frameless page: the whole canvas in the window
		return hands_pdaWindow(win);
	}
	float activeScreenH()
	{
		float win[4];
		if (pdaMissionCrop(win)) { return hands_pdaScreenHeight(); }
		return activeScreenW() * float(s_screenH) / float(s_screenW);
	}
	// Canvas pixel (320x200) -> normalized screen position (top-down).
	void canvasToScreen(float cx, float cy, float* u, float* v)
	{
		*u = s_pdaCanvas[0] + cx / 320.0f * (s_pdaCanvas[2] - s_pdaCanvas[0]);
		*v = s_pdaCanvas[1] + cy / 200.0f * (s_pdaCanvas[3] - s_pdaCanvas[1]);
	}
	bool rayToScreen(int* px, int* py, float* dist = nullptr)
	{
		float u, v;
		const float w = activeScreenW();
		float win[4];
		if (pdaMissionCrop(win))
		{
			// The whole case face is the PDA: the window shows the content, the frame around it has the
			// game's buttons (same 320x200 layout as the art), so hits on the frame click them too.
			u = v = -1e9f;
			rayQuad(activeScreenPose(), w, activeScreenH(), &u, &v, dist);
			const float cx = win[0] + u * (win[2] + 1.0f - win[0]), cy = win[1] + v * (win[3] + 1.0f - win[1]);
			if (cx < 0.0f || cx >= 320.0f || cy < 0.0f || cy >= 200.0f) { return false; }
			if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			{
				// On the frame: remember the spot for the pointer dot.
				const XrPosef& sp = activeScreenPose();
				const XrVector3f ax = rotate(sp.orientation, { 1, 0, 0 }), ay = rotate(sp.orientation, { 0, 1, 0 });
				const float hx = (u - 0.5f) * w, hy = (0.5f - v) * activeScreenH();
				s_pdaDotPos = { sp.position.x + ax.x * hx + ay.x * hy, sp.position.y + ax.y * hx + ay.y * hy, sp.position.z + ax.z * hx + ay.z * hy };
				s_pdaDotFrame = s_frameIndex;
			}
			float su, sv; canvasToScreen(cx, cy, &su, &sv);
			*px = int(su * s_screenW);
			*py = int(sv * s_screenH);
			return true;
		}
		if (!rayQuad(activeScreenPose(), w, w * float(s_screenH) / float(s_screenW), &u, &v, dist)) { return false; }
		*px = int(u * s_screenW);
		*py = int(v * s_screenH);
		return true;
	}

	// ---------------- Virtual keyboard ----------------
	// Shown under the screen whenever a Dark Forces text field is active (agent name, etc.).
	const int c_kbdW = 1024, c_kbdH = 384;
	const float c_kbdWidthM = 1.0f;
	Swapchain s_kbd;
	GLuint s_kbdTex = 0;
	int s_kbdRequest = -1000;          // frame of the last request
	int s_kbdFrame = 0;
	int s_kbdHover = -1, s_kbdDrawnHover = -2;
	bool s_kbdShift = true, s_kbdDrawnShift = false;
	bool s_kbdVisible = false, s_kbdHit = false;
	bool s_kbdTrigWas = false;
	int s_kbdPendingUp = 0;            // SDL scancode to release next frame
	float s_kbdCursor[2] = { 0, 0 };
	XrPosef s_kbdPose = { {0,0,0,1}, {0,0,0} };
	struct KbdKey { const char* label; int row, col, span; int code; };
	enum { KC_BACK = -1, KC_ENTER = -2, KC_SHIFT = -3 };
	const KbdKey c_keys[] =
	{
		{"1",0,0,1,'1'},{"2",0,1,1,'2'},{"3",0,2,1,'3'},{"4",0,3,1,'4'},{"5",0,4,1,'5'},{"6",0,5,1,'6'},{"7",0,6,1,'7'},{"8",0,7,1,'8'},{"9",0,8,1,'9'},{"0",0,9,1,'0'},{"<-",0,10,1,KC_BACK},
		{"q",1,0,1,'q'},{"w",1,1,1,'w'},{"e",1,2,1,'e'},{"r",1,3,1,'r'},{"t",1,4,1,'t'},{"y",1,5,1,'y'},{"u",1,6,1,'u'},{"i",1,7,1,'i'},{"o",1,8,1,'o'},{"p",1,9,1,'p'},{"aA",1,10,1,KC_SHIFT},
		{"a",2,0,1,'a'},{"s",2,1,1,'s'},{"d",2,2,1,'d'},{"f",2,3,1,'f'},{"g",2,4,1,'g'},{"h",2,5,1,'h'},{"j",2,6,1,'j'},{"k",2,7,1,'k'},{"l",2,8,1,'l'},{"ENTER",2,9,2,KC_ENTER},
		{"z",3,0,1,'z'},{"x",3,1,1,'x'},{"c",3,2,1,'c'},{"v",3,3,1,'v'},{"b",3,4,1,'b'},{"n",3,5,1,'n'},{"m",3,6,1,'m'},{"-",3,7,1,'-'},{".",3,8,1,'.'},{"SPACE",3,9,2,' '},
	};
	const int c_keyCount = sizeof(c_keys) / sizeof(c_keys[0]);

	void kbdKeyRect(const KbdKey& k, int* x0, int* y0, int* x1, int* y1)
	{
		const float cw = c_kbdW / 11.0f, ch = c_kbdH / 4.0f;
		*x0 = int(k.col * cw) + 4; *x1 = int((k.col + k.span) * cw) - 4;
		*y0 = int(k.row * ch) + 4; *y1 = int((k.row + 1) * ch) - 4;   // top-down
	}

	void kbdRender()
	{
		std::vector<unsigned char> img(c_kbdW * c_kbdH * 4);
		auto put = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
		{
			if (x < 0 || y < 0 || x >= c_kbdW || y >= c_kbdH) return;
			unsigned char* o = &img[((c_kbdH - 1 - y) * c_kbdW + x) * 4];   // GL rows bottom-up
			o[0] = r; o[1] = g; o[2] = b; o[3] = a;
		};
		for (int y = 0; y < c_kbdH; y++) for (int x = 0; x < c_kbdW; x++) put(x, y, 18, 20, 26, 230);
		for (int i = 0; i < c_keyCount; i++)
		{
			const KbdKey& k = c_keys[i];
			int x0, y0, x1, y1; kbdKeyRect(k, &x0, &y0, &x1, &y1);
			const bool hov = (i == s_kbdHover);
			const bool active = (k.code == KC_SHIFT && s_kbdShift);
			for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++)
			{
				const bool border = (y < y0 + 3 || y >= y1 - 3 || x < x0 + 3 || x >= x1 - 3);
				if (hov) put(x, y, border ? 255 : 210, border ? 220 : 150, border ? 120 : 30, 255);
				else if (active) put(x, y, border ? 140 : 70, border ? 200 : 110, border ? 140 : 70, 255);
				else put(x, y, border ? 110 : 58, border ? 112 : 60, border ? 124 : 70, 255);
			}
			// Label, 8x13 glyphs scaled by 4 (3 for long labels).
			char label[8];
			snprintf(label, sizeof(label), "%s", k.label);
			if (s_kbdShift && k.code >= 'a' && k.code <= 'z') { label[0] = char(k.code - 32); }
			const int len = (int)strlen(label);
			const int sc = len > 2 ? 3 : 4;
			const int tw = len * 8 * sc, th = 13 * sc;
			const int tx = (x0 + x1 - tw) / 2, ty = (y0 + y1 - th) / 2;
			for (int ci = 0; ci < len; ci++)
			{
				const int g = label[ci] - 32;
				if (g < 0 || g >= 95) continue;
				for (int gy = 0; gy < 13; gy++) for (int gx = 0; gx < 8; gx++)
				{
					if (!((c_font8x13[g][gy] >> gx) & 1)) continue;
					for (int sy = 0; sy < sc; sy++) for (int sx = 0; sx < sc; sx++)
					{
						put(tx + (ci * 8 + gx) * sc + sx, ty + gy * sc + sy, hov ? 20 : 235, hov ? 20 : 235, hov ? 20 : 240, 255);
					}
				}
			}
		}
		if (!s_kbdTex)
		{
			glGenTextures(1, &s_kbdTex);
			glBindTexture(GL_TEXTURE_2D, s_kbdTex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		glBindTexture(GL_TEXTURE_2D, s_kbdTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c_kbdW, c_kbdH, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
		glBindTexture(GL_TEXTURE_2D, 0);
		s_kbdDrawnHover = s_kbdHover;
		s_kbdDrawnShift = s_kbdShift;
	}

	void pushKey(SDL_Scancode sc, bool down)
	{
		SDL_Event e = {};
		e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
		e.key.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
		e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
		e.key.keysym.scancode = sc;
		e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
		SDL_PushEvent(&e);
	}

	void kbdPress(const KbdKey& k)
	{
		if (k.code == KC_SHIFT) { s_kbdShift = !s_kbdShift; return; }
		if (k.code == KC_BACK || k.code == KC_ENTER)
		{
			const SDL_Scancode sc = (k.code == KC_BACK) ? SDL_SCANCODE_BACKSPACE : SDL_SCANCODE_RETURN;
			pushKey(sc, true);
			s_kbdPendingUp = sc;
			return;
		}
		char c = char(k.code);
		if (s_kbdShift && c >= 'a' && c <= 'z') { c = char(c - 32); }
		SDL_Event e = {};
		e.type = SDL_TEXTINPUT;
		e.text.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
		e.text.text[0] = c; e.text.text[1] = 0;
		SDL_PushEvent(&e);
	}

	// Called from updateInput: decides visibility, places the keyboard and handles the pointer.
	void kbdUpdate(bool menuMode, float trigR)
	{
		s_kbdFrame++;
		if (s_kbdPendingUp) { pushKey(SDL_Scancode(s_kbdPendingUp), false); s_kbdPendingUp = 0; }
		if (scriptValue("kbd", nullptr, nullptr)) { s_kbdRequest = s_kbdFrame; }
		const bool want = menuMode && (s_kbdFrame - s_kbdRequest) < 10 && s_kbd.handle != XR_NULL_HANDLE;
		if (want && !s_kbdVisible)
		{
			// Below the screen, tilted up toward the player.
			const XrQuaternionf q = s_quadPose.orientation;
			const XrVector3f off = rotate(q, { 0.0f, -0.62f, 0.55f });
			s_kbdPose.position = { s_quadPose.position.x + off.x, s_quadPose.position.y + off.y, s_quadPose.position.z + off.z };
			const XrQuaternionf tilt = { sinf(-0.45f), 0.0f, 0.0f, cosf(-0.45f) };
			s_kbdPose.orientation = mulQ(q, tilt);
		}
		s_kbdVisible = want;
		s_kbdHit = false;
		s_kbdHover = -1;
		if (!s_kbdVisible) { s_kbdTrigWas = trigR > 0.6f; return; }
		float u, v;
		if (rayQuad(s_kbdPose, c_kbdWidthM, c_kbdWidthM * c_kbdH / c_kbdW, &u, &v, nullptr))
		{
			s_kbdHit = true;
			const int px = int(u * c_kbdW), py = int(v * c_kbdH);
			s_kbdCursor[0] = float(px); s_kbdCursor[1] = float(c_kbdH - py);
			for (int i = 0; i < c_keyCount; i++)
			{
				int x0, y0, x1, y1; kbdKeyRect(c_keys[i], &x0, &y0, &x1, &y1);
				if (px >= x0 && px < x1 && py >= y0 && py < y1) { s_kbdHover = i; break; }
			}
			const bool trig = trigR > 0.6f;
			if (trig && !s_kbdTrigWas && s_kbdHover >= 0) { kbdPress(c_keys[s_kbdHover]); haptic(1, 0.25f, 0.02f); }
		}
		s_kbdTrigWas = trigR > 0.6f;
	}

	// Scripted input for testing without anyone holding the controllers:
	// files/tfe_xrinput.txt, lines "start_s dur_s name value [value2]".
	// names: move (x y), turn (x), trigR, trigL, a, b, x, y, menu, stickL, stickR, aim (yaw_deg pitch_deg).
	struct ScriptEvent { float start, dur; char name[16]; float v0, v1; };
	std::vector<ScriptEvent> s_script;
	bool s_scriptLoaded = false;
	double s_scriptT0 = 0.0;
	int s_diagFrame = 0;

	void loadScript()
	{
		s_scriptLoaded = true;
		FILE* f = fopen("tfe_xrinput.txt", "rb");
		if (!f) { return; }
		char line[256];
		while (fgets(line, sizeof(line), f))
		{
			ScriptEvent e = {};
			if (sscanf(line, "%f %f %15s %f %f", &e.start, &e.dur, e.name, &e.v0, &e.v1) >= 3) { s_script.push_back(e); }
		}
		fclose(f);
		remove("tfe_xrinput.txt");   // one-shot: a test script never leaks into a normal game
		s_scriptT0 = SDL_GetTicks() / 1000.0;
		LOGI("input script: %zu events", s_script.size());
	}
	bool scriptValue(const char* name, float* v0, float* v1)
	{
		const double t = SDL_GetTicks() / 1000.0 - s_scriptT0;
		for (const ScriptEvent& e : s_script)
		{
			if (strcmp(e.name, name) == 0 && t >= e.start && t < e.start + e.dur)
			{
				if (v0) *v0 = e.v0;
				if (v1) *v1 = e.v1;
				return true;
			}
		}
		return false;
	}

	struct OptKey { const char* key; int* i; float* f; };
	const OptKey c_optKeys[] =
	{
		{ "snap_turn", &s_opt.snapTurn, nullptr },
		{ "snap_angle", nullptr, &s_opt.snapAngle },
		{ "smooth_turn_speed", nullptr, &s_opt.smoothSpeed },
		{ "run_mode", &s_opt.runMode, nullptr },
		{ "comfort_vignette", nullptr, &s_opt.comfortVignette },
		{ "physical_crouch", &s_opt.physicalCrouch, nullptr },
		{ "crouch_depth", nullptr, &s_opt.crouchDepth },
		{ "height_offset", nullptr, &s_opt.heightOffset },
		{ "laser_sight", &s_opt.laser, nullptr },
		{ "wrist_hud", &s_opt.wristHud, nullptr },
		{ "map_hologram", &s_opt.mapMode, nullptr },
		{ "screen_follows_head", &s_opt.lazyScreen, nullptr },
		{ "screen_distance", nullptr, &s_opt.screenDistance },
		{ "haptics", &s_opt.haptics, nullptr },
		{ "haptic_strength", nullptr, &s_opt.hapticStrength },
		{ "weapon_look", &s_opt.weaponStyle, nullptr },  // 0 high resolution, 1 voxel, 2 fine voxel (3x), 3 Blender models from files/dfmod_weapons.bin (default), 4 DFmod first-person + third-person models (files/dfmod_combined.bin)
		{ "menu_room", &s_opt.menuRoom, nullptr },
		{ "weapon_wall", &s_opt.wallMode, nullptr },   // 0 the wall hides the weapon, 1 weapon always in front
		{ "right_hand", &s_opt.rightHand, nullptr },   // 1 right hand drawn on the weapon, 0 only the weapon
		{ "pda_size", nullptr, &s_opt.pdaScale },      // PDA size, 0.6 .. 1.6 (1 = 17 cm screen)
		{ "gas_mask_view", &s_opt.gasMaskView, nullptr },   // 1 gas mask seen from inside while worn
		{ "walk_direction", &s_opt.walkDir, nullptr },      // 0 head, 1 left controller, 2 right controller
		{ "weapon_spread", &s_opt.weaponSpread, nullptr },  // 1 original random spread, 0 reduced to a third
		{ "fps_default_done", &s_opt.fpsDefaultDone, nullptr },
		{ "lang_default_done", &s_opt.langDefaultDone, nullptr },
		{ "bind_a", &s_opt.binds[0], nullptr },
		{ "bind_gd_a", &s_opt.binds[1], nullptr },
		{ "bind_ge_a", &s_opt.binds[2], nullptr },
		{ "bind_b", &s_opt.binds[3], nullptr },
		{ "bind_gd_b", &s_opt.binds[4], nullptr },
		{ "bind_ge_b", &s_opt.binds[5], nullptr },
		{ "bind_x", &s_opt.binds[6], nullptr },
		{ "bind_gd_x", &s_opt.binds[7], nullptr },
		{ "bind_ge_x", &s_opt.binds[8], nullptr },
		{ "bind_y", &s_opt.binds[9], nullptr },
		{ "bind_gd_y", &s_opt.binds[10], nullptr },
		{ "bind_ge_y", &s_opt.binds[11], nullptr },
		{ "bind_trig_l", &s_opt.binds[12], nullptr },
		{ "bind_ge_trig_l", &s_opt.binds[13], nullptr },
		{ "bind_r3", &s_opt.binds[14], nullptr },
		{ "bind_rest_l", &s_opt.binds[15], nullptr },   // thumb rest touch (held: the map stays open while touched)
		{ "bind_rest_r", &s_opt.binds[16], nullptr },
		{ "pda_bind_done", &s_opt.pdaBindDone, nullptr },
		{ "zoom_bind_done", &s_opt.zoomBindDone, nullptr },
		{ "voxels", &s_opt.voxels, nullptr },   // 1 voxel models (fan pack) in place of item/scenery/droid sprites
	};

	void saveOptions()
	{
		FILE* f = fopen("tfe_vr.ini", "wb");
		if (!f) { return; }
		fputs("; Dark Forces VR options (also editable in the game: Settings > VR)\n", f);
		for (const OptKey& k : c_optKeys)
		{
			if (k.i) fprintf(f, "%s=%d\n", k.key, *k.i);
			else fprintf(f, "%s=%.3f\n", k.key, *k.f);
		}
		fclose(f);
	}

	void loadOptions()
	{
		FILE* f = fopen("tfe_vr.ini", "rb");
		if (!f) { saveOptions(); return; }
		char line[128];
		while (fgets(line, sizeof(line), f))
		{
			char key[64]; float v;
			if (sscanf(line, " %63[^=]=%f", key, &v) != 2) { continue; }
			for (const OptKey& k : c_optKeys)
			{
				if (strcmp(key, k.key)) continue;
				if (k.i) *k.i = int(v); else *k.f = v;
			}
		}
		fclose(f);
		s_opt.mapMode = 1;   // the map is always the hologram
		if (!s_opt.pdaBindDone)
		{
			// 0.34: the mission tablet left the map button; it goes to Y when Y is free.
			if (s_opt.binds[XRB_Y] == XRA_NONE) { s_opt.binds[XRB_Y] = XRA_PDA; }
			s_opt.pdaBindDone = 1;
		}
		if (!s_opt.zoomBindDone)
		{
			// 0.35: map zoom left the map button; it goes to GD+Y when that slot is free.
			if (s_opt.binds[XRB_GD_Y] == XRA_NONE) { s_opt.binds[XRB_GD_Y] = XRA_MAPZOOM; }
			s_opt.zoomBindDone = 1;
		}
		saveOptions();   // adds keys introduced by newer versions
		LOGI("options: snap=%d angle=%.0f laser=%d wristHud=%d crouch=%d haptics=%d map=%d", s_opt.snapTurn, s_opt.snapAngle, s_opt.laser, s_opt.wristHud, s_opt.physicalCrouch, s_opt.haptics, s_opt.mapMode);
	}

	void haptic(int hand, float amp, float seconds)
	{
		if (!s_opt.haptics || s_actHaptic == XR_NULL_HANDLE || s_session == XR_NULL_HANDLE) { return; }
		XrHapticVibration v = { XR_TYPE_HAPTIC_VIBRATION };
		amp *= s_opt.hapticStrength;
		v.amplitude = amp > 1.0f ? 1.0f : amp;
		v.duration = XrDuration(seconds * 1e9);
		v.frequency = XR_FREQUENCY_UNSPECIFIED;
		for (int h = 0; h < 2; h++)
		{
			if (hand >= 0 && hand != h) { continue; }
			XrHapticActionInfo hi = { XR_TYPE_HAPTIC_ACTION_INFO };
			hi.action = s_actHaptic;
			hi.subactionPath = s_handPath[h];
			xrApplyHapticFeedback(s_session, &hi, (const XrHapticBaseHeader*)&v);
		}
	}

	// Keyboard keys driven by controller combos (TFE default keyboard bindings).
	struct HeldKey { SDL_Scancode sc; bool alt; bool down; };
	HeldKey s_heldKeys[16] = {};
	void holdKey(int slot, SDL_Scancode sc, bool down, bool alt = false)
	{
		HeldKey& k = s_heldKeys[slot];
		if (k.down == down) { return; }
		k.sc = sc; k.alt = alt; k.down = down;
		auto push = [](SDL_Scancode c, bool d)
		{
			SDL_Event e = {};
			e.type = d ? SDL_KEYDOWN : SDL_KEYUP;
			e.key.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
			e.key.state = d ? SDL_PRESSED : SDL_RELEASED;
			e.key.keysym.scancode = c;
			e.key.keysym.sym = SDL_GetKeyFromScancode(c);
			SDL_PushEvent(&e);
		};
		if (down) { if (alt) push(SDL_SCANCODE_LALT, true); push(sc, true); }
		else { push(sc, false); if (alt) push(SDL_SCANCODE_LALT, false); }
	}

	void updateInput()
	{
		if (!s_scriptLoaded) { loadScript(); }
		XrActiveActionSet aas = { s_actionSet, XR_NULL_PATH };
		XrActionsSyncInfo si = { XR_TYPE_ACTIONS_SYNC_INFO };
		si.countActiveActionSets = 1;
		si.activeActionSets = &aas;
		if (XR_FAILED(xrSyncActions(s_session, &si))) { return; }

		XrVector2f move = getVec2(s_actMove);
		XrVector2f turn = getVec2(s_actTurn);
		float sv0, sv1;
		if (scriptValue("move", &sv0, &sv1)) { move.x = sv0; move.y = sv1; }
		if (scriptValue("turn", &sv0, &sv1)) { turn.x = sv0; turn.y = sv1; }
		s_scriptAim = scriptValue("aim", &s_scriptAimYaw, &s_scriptAimPitch);
		// Walk where the head looks (or where a controller points): rotate the stick into body yaw.
		{
			float th = yawOf(s_headPose.orientation);   // head yaw relative to the body, left positive
			if (s_opt.walkDir == 1 || s_opt.walkDir == 2)
			{
				const HandInput& c = s_hands[s_opt.walkDir == 1 ? 0 : 1];
				if (c.aimValid) { th = yawOf(toRef(c.aimPose).orientation); }
				else if (c.valid) { th = yawOf(toRef(c.pose).orientation); }
			}
			const float c = cosf(th), sn = sinf(th);
			const float mx = move.x * c - move.y * sn;
			const float my = move.x * sn + move.y * c;
			move.x = mx; move.y = my;
		}
		s_moveAmount = (!TFE_Input::relativeModeEnabled() || !s_worldLastFrame) ? 0.0f : fminf(1.0f, sqrtf(move.x*move.x + move.y*move.y));
		TFE_Input::setAxis(AXIS_LEFT_X, move.x);
		TFE_Input::setAxis(AXIS_LEFT_Y, move.y);
		TFE_Input::setAxis(AXIS_RIGHT_Y, 0.0f);

		float trigR = getFloat(s_actTrigR);
		float trigL = getFloat(s_actTrigL);
		if (scriptValue("trigR", &sv0, nullptr)) { trigR = sv0; }
		if (scriptValue("trigL", &sv0, nullptr)) { trigL = sv0; }
		// Menus: TFE front end (absolute mouse) or any frame where the 3D world was not drawn
		// (Dark Forces escape menu, PDA, agent screens keep relative mouse mode).
		// The TFE settings menu / console draw over a running game: they are menus too.
		s_systemUi = TFE_FrontEndUI::isConfigMenuOpen() || TFE_FrontEndUI::isConsoleOpen();
		const bool menuMode = !TFE_Input::relativeModeEnabled() || !s_worldLastFrame || s_systemUi || s_pdaMenu;
		// Grip buttons are modifiers: left grip (L) for the left hand buttons, right grip (R) for the right hand.
		const bool modL = getFloat(s_actGripL) > 0.6f || scriptValue("gripL", nullptr, nullptr);
		const bool modR = getFloat(s_actGripR) > 0.6f || scriptValue("gripR", nullptr, nullptr);

		// Right stick: snap (or smooth) turn on X, weapon cycling on Y.
		{
			const bool turnX = fabsf(turn.x) > 0.7f && fabsf(turn.x) > fabsf(turn.y);
			const bool turnY = fabsf(turn.y) > 0.7f && fabsf(turn.y) > fabsf(turn.x);
			if (s_opt.snapTurn)
			{
				TFE_Input::setAxis(AXIS_RIGHT_X, 0.0f);
				if (turnX && s_snapArmed && !menuMode)
				{
					s_pendingYaw += (turn.x > 0.0f ? 1 : -1) * int(s_opt.snapAngle * 16384.0f / 360.0f);
					s_snapArmed = false;
				}
			}
			else
			{
				TFE_Input::setAxis(AXIS_RIGHT_X, fabsf(turn.x) > 0.15f ? turn.x * s_opt.smoothSpeed : 0.0f);
			}
			if (fabsf(turn.x) < 0.35f && fabsf(turn.y) < 0.35f) { s_snapArmed = true; s_weaponArmed = true; }
			bool next = false, prev = false;
			if (!s_automap && turnY && s_weaponArmed && !menuMode && turn.y > 0.0f)
			{
				next = true;
				s_weaponArmed = false;
			}
			// Stick down: previous weapon. Right stick click held opens the weapon wheel around the
			// right hand (Half-Life: Alyx style): move the hand to a weapon and let go. A quick click
			// toggles the headlamp.
			if (!s_automap && turnY && s_weaponArmed && !menuMode && turn.y < 0.0f)
			{
				prev = true;
				s_weaponArmed = false;
			}
			// Weapon strip next to the right hand: the next weapon above (stick up), the previous one
			// below (stick down); a press slides the column to the new weapon.
			{
				const double tnow = SDL_GetTicks() / 1000.0;
				auto step = [&](int from, int dir)
				{
					for (int k = 1; k <= 10; k++)
					{
						const int id = ((from + dir * k) % 10 + 10) % 10;
						if (s_ownedWeapons & (1 << id)) { return id; }
					}
					return from;
				};
				const bool showing = tnow - s_cycShowUntil < 0.0;
				if (next || prev)
				{
					const int base = (showing && s_cycCur >= 0) ? s_cycCur : (s_weaponId >= 0 ? s_weaponId : 0);
					s_cycDir = next ? 1 : -1;
					s_cycCur = step(base, s_cycDir);
					s_cycTime = tnow;
				}
				if (!showing && !(next || prev)) { s_cycCur = s_weaponId >= 0 ? s_weaponId : 0; }
				const bool lean = !s_automap && !menuMode && fabsf(turn.y) > 0.35f && fabsf(turn.y) > fabsf(turn.x);
				if (lean || next || prev) { s_cycShowUntil = tnow + 1.2; }
			}
			{
				const bool clickR = getBool(s_actStickR) || scriptValue("stickR", nullptr, nullptr);
				const bool down = !s_automap && !menuMode && clickR;
				const HandInput& R = s_hands[1];
				if (down)
				{
					s_wheelHold += s_frameDt;
					if (!s_wheelOpen && s_wheelHold > 0.18f && R.valid)
					{
						s_wheelOpen = true;
						s_wheelSel = -1;
						s_wheelCenter = R.pose.position;
						// Facing the eyes, upright.
						const XrVector3f head = s_headPoseApp.position;
						const float dx = head.x - s_wheelCenter.x, dz = head.z - s_wheelCenter.z;
						const float yaw = atan2f(dx, dz);
						const float dy = head.y - s_wheelCenter.y;
						const float pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
						const XrQuaternionf qy = { 0.0f, sinf(yaw * 0.5f), 0.0f, cosf(yaw * 0.5f) };
						const XrQuaternionf qx = { sinf(-pitch * 0.5f), 0.0f, 0.0f, cosf(-pitch * 0.5f) };
						s_wheelOri = mulQ(qy, qx);
						haptic(1, 0.3f, 0.03f);
						LOGI("weapon wheel open (owned 0x%x)", s_ownedWeapons);
					}
				}
				else
				{
					if (s_wheelOpen)
					{
						if (s_wheelSel >= 0) { s_wheelPick = s_wheelSel; haptic(1, 0.5f, 0.05f); }
						s_wheelOpen = false;
					}
					else if (s_wheelHold > 0.0f && s_wheelHold <= 0.18f) { s_r3Pulse = 2; }   // quick click: the R3 slot (headlamp by default)
					s_wheelHold = 0.0f;
				}
				if (s_wheelOpen && R.valid)
				{
					// Hand offset in the wheel plane picks the slot (slot k at k * 36 degrees, clockwise from the top).
					const XrVector3f d = { R.pose.position.x - s_wheelCenter.x, R.pose.position.y - s_wheelCenter.y, R.pose.position.z - s_wheelCenter.z };
					const XrQuaternionf inv = { -s_wheelOri.x, -s_wheelOri.y, -s_wheelOri.z, s_wheelOri.w };
					const XrVector3f l = rotate(inv, d);
					int sel = -1;
					if (sqrtf(l.x * l.x + l.y * l.y) > 0.03f)
					{
						float a = atan2f(l.x, l.y);   // 0 at the top, clockwise positive
						if (a < 0.0f) { a += 6.2831853f; }   // 0..2pi like the slots (the left half came out negative and picked the wrong slot)
						float best = 1e9f;
						for (int k = 0; k < 10; k++)
						{
							if (!(s_ownedWeapons & (1 << k))) continue;
							float da = fabsf(a - float(k) * 0.6283185f);
							da = fminf(da, 6.2831853f - da);
							if (da < best) { best = da; sel = k; }
						}
					}
					if (sel != s_wheelSel && sel >= 0) { haptic(1, 0.15f, 0.015f); }
					s_wheelSel = sel;
				}
			}
			// One-frame pulses are enough for TFE's "pressed" detection.
			setButton(CONTROLLER_BUTTON_RIGHTSHOULDER, next);
			setButton(CONTROLLER_BUTTON_LEFTSHOULDER, prev);
			// Map on the wrist: stick up/down zooms, with the right grip it changes the layer.
			const bool mapUp = s_automap && !menuMode && turnY && turn.y > 0.0f;
			const bool mapDn = s_automap && !menuMode && turnY && turn.y < 0.0f;
			holdKey(0, SDL_SCANCODE_EQUALS, mapUp && !modR);
			holdKey(1, SDL_SCANCODE_MINUS, mapDn && !modR);
			holdKey(2, SDL_SCANCODE_RIGHTBRACKET, mapUp && modR);
			holdKey(3, SDL_SCANCODE_LEFTBRACKET, mapDn && modR);
		}

		// Virtual keyboard (takes the pointer when it is hit).
		kbdUpdate(menuMode, trigR);

		// Mouse on the screen quad (menus, PDA, agent screens).
		int px, py;
		s_cursorVisible = false;
		if (menuMode && !s_kbdHit && rayToScreen(&px, &py))
		{
			if (px != s_mouseX || py != s_mouseY) { pushMouseMotion(px, py); }
			s_cursorVisible = true;
		}
		// Pointing at the PDA puts the weapon away (kept a moment so the edges do not flicker).
		if (pdaActive() && s_cursorVisible) { s_pdaPointHold = 25; }
		else if (s_pdaPointHold > 0) { s_pdaPointHold--; }
		if (!s_pdaMenu) { s_pdaPointHold = 0; }
		const bool trigDown = trigR > 0.6f;
		if (menuMode || s_triggerWasMouse)
		{
			const bool aClick = getBool(s_actA) && !modL && !modR;   // A also clicks in menus
			const bool mouseTrig = (trigDown || aClick) && (!s_kbdHit || s_mouseDown);
			if (mouseTrig != s_mouseDown) { pushMouseButton(mouseTrig); s_mouseDown = mouseTrig; }
			s_triggerWasMouse = s_mouseDown;
			TFE_Input::setAxis(AXIS_RIGHT_TRIGGER, 0.0f);
		}
		else
		{
			// Right trigger fires; with the right grip held it is the alternate fire.
			TFE_Input::setAxis(AXIS_RIGHT_TRIGGER, modR ? 0.0f : trigR);
			TFE_Input::setAxis(AXIS_LEFT_TRIGGER, modR ? trigR : 0.0f);
		}
		if (menuMode) { TFE_Input::setAxis(AXIS_LEFT_TRIGGER, 0.0f); }
		// Buttons: every slot (a button alone, with the right grip GD or the left grip GE) runs the action
		// chosen on the controls page of the VR menu (defaults: A use | GD+A night vision | GE+A quick save,
		// B jump | GD+B ice cleats | GE+B quick load, X crouch | GE+X gas mask, GE+Y map, GD+Y map zoom, Y mission tablet,
		// left trigger last weapon | GE+left trigger holster, R3 click headlamp).
		const bool bA = getBool(s_actA) || scriptValue("a", nullptr, nullptr);
		const bool bB = getBool(s_actB) || scriptValue("b", nullptr, nullptr);
		const bool bX = getBool(s_actX) || scriptValue("x", nullptr, nullptr);
		const bool bY = getBool(s_actY) || scriptValue("y", nullptr, nullptr);
		const bool bStickR = getBool(s_actStickR) || scriptValue("stickR", nullptr, nullptr);
		bool act[XRA_COUNT] = {};
		bool mapHeld = false;
		{
			const bool pl = !modL && !modR, gr = modR && !modL, gl = modL;
			const bool trigLDown = trigL > 0.6f;
			bool slot[XRB_COUNT] = {};
			slot[XRB_A] = bA && pl;  slot[XRB_GD_A] = bA && gr;  slot[XRB_GE_A] = bA && gl;
			slot[XRB_B] = bB && pl;  slot[XRB_GD_B] = bB && gr;  slot[XRB_GE_B] = bB && gl;
			slot[XRB_X] = bX && pl;  slot[XRB_GD_X] = bX && gr;  slot[XRB_GE_X] = bX && gl;
			slot[XRB_Y] = bY && pl;  slot[XRB_GD_Y] = bY && gr;  slot[XRB_GE_Y] = bY && gl;
			slot[XRB_TRIGL] = trigLDown && !modL; slot[XRB_GE_TRIGL] = trigLDown && modL;
			slot[XRB_R3] = s_r3Pulse > 0;
			// Thumb rests: a slot that is held while the thumb lies on the rest.
			{
				auto rest = [&](int h) {
					XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
					gi.action = s_actRest; gi.subactionPath = s_handPath[h];
					XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
					return XR_SUCCEEDED(xrGetActionStateBoolean(s_session, &gi, &st)) && st.isActive && st.currentState;
				};
				// a short debounce so a thumb sliding over the rest does not flicker the map
				static int s_restCnt[2] = {};
				for (int h = 0; h < 2; h++)
				{
					const bool r = rest(h) || scriptValue(h ? "restR" : "restL", nullptr, nullptr);
					s_restCnt[h] = r ? (s_restCnt[h] < 6 ? s_restCnt[h] + 1 : 6) : (s_restCnt[h] > 1 ? s_restCnt[h] - 2 : 0);
					s_restHeld[h] = s_restCnt[h] >= 4 || (s_restHeld[h] && s_restCnt[h] > 0);
				}
				slot[XRB_REST_L] = s_restHeld[0];
				slot[XRB_REST_R] = s_restHeld[1];
			}
			if (s_r3Pulse > 0) { s_r3Pulse--; }
			if (menuMode) { slot[XRB_A] = false; }   // in menus A clicks, like the trigger
			for (int i = 0; i < XRB_COUNT; i++)
			{
				const int a = s_opt.binds[i];
				if (!slot[i] || a <= XRA_NONE || a >= XRA_COUNT) { continue; }
				// The map on a thumb rest is "show while held" (handled with the map toggle below).
				if ((i == XRB_REST_L || i == XRB_REST_R) && a == XRA_MAP) { mapHeld = true; continue; }
				act[a] = true;
			}
		}
		static bool s_actWas[XRA_COUNT] = {};
		auto pressed = [&](int a) { return act[a] && !s_actWas[a]; };
		holdKey(4, SDL_SCANCODE_BACKSPACE, !menuMode && act[XRA_LASTWPN]);
		holdKey(5, SDL_SCANCODE_F8, !menuMode && act[XRA_HOLSTER]);
		setButton(CONTROLLER_BUTTON_Y, act[XRA_USE]);                      // use / open
		setButton(CONTROLLER_BUTTON_A, act[XRA_JUMP]);                     // jump
		setButton(CONTROLLER_BUTTON_X, act[XRA_CROUCH] || s_physCrouch);   // crouch (hold), or physical crouch
		{
			// Map button: opens/closes the map (hologram). Zoom button (map open): each press shows more of
			// the map (two steps out), the press after that goes back to the close view. Mission tablet: its own button.
			static int s_pulse = 0, s_f1Delay = 0, s_f1Hold = 0, s_mapStep = 0;
			if (pressed(XRA_MAP) && !menuMode) { s_pulse = 2; s_mapStep = 0; }
			{
				// Map held on a thumb rest: open while touched, closed again on release (only if the rest opened it).
				static bool s_heldWas = false, s_heldOpened = false;
				if (mapHeld && !s_heldWas && !menuMode && !s_automap && s_pulse == 0) { s_pulse = 2; s_heldOpened = true; s_mapStep = 0; }
				else if (!mapHeld && s_heldWas && s_heldOpened)
				{
					if (s_automap && !menuMode && s_pulse == 0) { s_pulse = 2; }
					s_heldOpened = false;
				}
				s_heldWas = mapHeld;
			}
			if (pressed(XRA_MAPZOOM) && !menuMode && s_automap)
			{
				if (s_mapStep < 2) { tfe_df_vr_map_zoom_out(); s_mapStep++; }
				else { tfe_df_vr_map_zoom_reset(); s_mapStep = 0; }
			}
			if (pressed(XRA_PDA))
			{
				if (s_pdaMenu) { s_f1Delay = 1; }                                                      // tablet open: close it
				else if (s_automap) { s_f1Delay = 1; s_mapStep = 0; }                                  // map open: the game closes it and opens the tablet
				else if (!menuMode) { s_f1Delay = 1; }                                                 // open the tablet
			}
			setButton(CONTROLLER_BUTTON_DPAD_UP, s_pulse > 0);   // TFE's automap toggle (one press)
			if (s_pulse > 0) { s_pulse--; }
			if (s_f1Delay > 0 && --s_f1Delay == 0) { s_f1Hold = 3; }
			const bool f1 = s_f1Hold > 0;
			static bool s_f1Was = false;
			if (f1 != s_f1Was)
			{
				SDL_Event e = {};
				e.type = f1 ? SDL_KEYDOWN : SDL_KEYUP;
				e.key.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
				e.key.state = f1 ? SDL_PRESSED : SDL_RELEASED;
				e.key.keysym.scancode = SDL_SCANCODE_F1;
				e.key.keysym.sym = SDLK_F1;
				SDL_PushEvent(&e);
				s_f1Was = f1;
			}
			if (s_f1Hold > 0) { s_f1Hold--; }
		}
		if (pressed(XRA_LAMP)) { s_lampPulse = 2; }
		// Headlamp (one press). In menus the right stick click clicks through.
		setButton(CONTROLLER_BUTTON_DPAD_RIGHT, s_lampPulse > 0 || (bStickR && menuMode));
		if (s_lampPulse > 0) { s_lampPulse--; }
		setButton(CONTROLLER_BUTTON_DPAD_LEFT, act[XRA_NIGHT]);            // night vision
		setButton(CONTROLLER_BUTTON_DPAD_DOWN, act[XRA_MASK]);             // gas mask
		holdKey(6, SDL_SCANCODE_F3, act[XRA_CLEATS]);                      // ice cleats
		holdKey(7, SDL_SCANCODE_F5, act[XRA_QSAVE] && !menuMode, true);    // quick save (Alt+F5)
		holdKey(8, SDL_SCANCODE_F9, act[XRA_QLOAD] && !menuMode, true);    // quick load (Alt+F9)
		for (int i = 0; i < XRA_COUNT; i++) { s_actWas[i] = act[i]; }
		// Run (TFE's run action = pad B): left stick click. Walking is the default speed.
		{
			// Three speeds: walk (half the old walk), L3 = jog (the old walk), L3 + a grip = run.
			const bool click = getBool(s_actStickL) || scriptValue("stickL", nullptr, nullptr);
			const float mv = sqrtf(move.x * move.x + move.y * move.y);
			static int s_runLatchLevel = 0;
			const int clickLevel = click ? ((modL || modR) ? 2 : 1) : 0;
			int level;
			if (s_opt.runMode == 1)
			{
				if (clickLevel > s_runLatchLevel) { s_runLatchLevel = clickLevel; }
				if (mv < 0.2f && !click) { s_runLatchLevel = 0; }
				level = s_runLatchLevel;
			}
			else { level = clickLevel; }
			if (scriptValue("run", nullptr, nullptr)) { level = 2; }
			s_runActive = level >= 1;
			setButton(CONTROLLER_BUTTON_B, level == 2 && !menuMode);
			if (level == 0)
			{
				TFE_Input::setAxis(AXIS_LEFT_X, move.x * 0.5f);
				TFE_Input::setAxis(AXIS_LEFT_Y, move.y * 0.5f);
			}
		}
		// Test scripts: "wpn N" holds the weapon number key N (1..10).
		{
			static int s_wpnKey = 0;
			float w = 0.0f;
			const int want = scriptValue("wpn", &w, nullptr) ? int(w) : 0;
			if (want != s_wpnKey)
			{
				SDL_Event e = {};
				const int k = want ? want : s_wpnKey;
				const SDL_Scancode sc = (k >= 10) ? SDL_SCANCODE_0 : SDL_Scancode(SDL_SCANCODE_1 + k - 1);
				e.type = want ? SDL_KEYDOWN : SDL_KEYUP;
				e.key.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
				e.key.state = want ? SDL_PRESSED : SDL_RELEASED;
				e.key.keysym.scancode = sc;
				e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
				SDL_PushEvent(&e);
				s_wpnKey = want;
			}
		}
		// Weapon picked on the wheel: its number key (fists 1 ... cannon 0).
		{
			static int s_pickKey = -1;
			if (s_pickKey >= 0)
			{
				const SDL_Scancode sc = (s_pickKey >= 9) ? SDL_SCANCODE_0 : SDL_Scancode(SDL_SCANCODE_1 + s_pickKey);
				SDL_Event e = {};
				e.type = SDL_KEYUP; e.key.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
				e.key.state = SDL_RELEASED; e.key.keysym.scancode = sc; e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
				SDL_PushEvent(&e);
				s_pickKey = -1;
			}
			if (s_wheelPick >= 0)
			{
				const SDL_Scancode sc = (s_wheelPick >= 9) ? SDL_SCANCODE_0 : SDL_Scancode(SDL_SCANCODE_1 + s_wheelPick);
				SDL_Event e = {};
				e.type = SDL_KEYDOWN; e.key.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
				e.key.state = SDL_PRESSED; e.key.keysym.scancode = sc; e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
				SDL_PushEvent(&e);
				s_pickKey = s_wheelPick;
				s_wheelPick = -1;
			}
		}
		// Menu button behaves like Escape (opens/closes the in-game menu).
		{
			const bool menu = getBool(s_actMenu) || scriptValue("menu", nullptr, nullptr);
			if (menu != s_menuWas)
			{
				SDL_Event e = {};
				e.type = menu ? SDL_KEYDOWN : SDL_KEYUP;
				e.key.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
				e.key.state = menu ? SDL_PRESSED : SDL_RELEASED;
				e.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
				e.key.keysym.sym = SDLK_ESCAPE;
				SDL_PushEvent(&e);
				s_menuWas = menu;
			}
		}
		// Recenter: the Meta button (left grip + stick click now runs).
		const bool stickL = getBool(s_actStickL) || scriptValue("stickL", nullptr, nullptr);
		if (scriptValue("recenter", nullptr, nullptr) && !s_stickLWas) { s_recenterRequested = true; }
		s_stickLWas = stickL || scriptValue("recenter", nullptr, nullptr);

		// Physical crouch (hysteresis), relative to the recentered standing head.
		if (s_opt.physicalCrouch && s_refValid && !menuMode)
		{
			const float y = s_headPose.position.y;
			if (!s_physCrouch && y < -s_opt.crouchDepth) { s_physCrouch = true; }
			else if (s_physCrouch && y > -s_opt.crouchDepth + 0.10f) { s_physCrouch = false; }
		}
		else { s_physCrouch = false; }

		// Hands.
		const bool fakeHands = scriptValue("hands", nullptr, nullptr);
		for (int h = 0; h < 2; h++)
		{
			HandInput& hi = s_hands[h];
			hi.valid = false;
			XrSpaceLocation gl = { XR_TYPE_SPACE_LOCATION };
			if (XR_SUCCEEDED(xrLocateSpace(s_gripSpace[h], s_appSpace, s_frameState.predictedDisplayTime, &gl)) &&
				(gl.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) && (gl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
			{
				hi.valid = true;
				hi.pose = gl.pose;
			}
			hi.aimValid = false;
			XrSpaceLocation al = { XR_TYPE_SPACE_LOCATION };
			if (XR_SUCCEEDED(xrLocateSpace(s_aimSpaceH[h], s_appSpace, s_frameState.predictedDisplayTime, &al)) &&
				(al.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) && (al.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
			{
				hi.aimValid = true;
				hi.aimPose = al.pose;
			}
			hi.grip = getFloat(h ? s_actGripR : s_actGripL);
			hi.trigger = h ? trigR : trigL;
			hi.triggerTouch = getBoolHand(s_actTrigTouch, h);
			hi.thumbTouch = getBoolHand(s_actThumbTouch, h);
			if (fakeHands && !hi.valid && !hi.aimValid)
			{
				// Test pose in front of the recentered head; the right hand follows the scripted aim.
				// Base frame: the recentered body, or the current head ("headrel" in the script).
				const XrQuaternionf qRef = { 0.0f, sinf(s_refYaw * 0.5f), 0.0f, cosf(s_refYaw * 0.5f) };
				const bool headRel = scriptValue("headrel", nullptr, nullptr);
				const XrQuaternionf qBase = headRel ? mulQ(qRef, s_headPose.orientation) : qRef;
				const XrVector3f hpos = rotate(qRef, s_headPose.position);
				const XrVector3f basePos = headRel ? XrVector3f{ s_refPos.x + hpos.x, s_refPos.y + hpos.y, s_refPos.z + hpos.z } : s_refPos;
				XrQuaternionf q = qBase;
				if (h == 1 && s_scriptAim)
				{
					const float ya = s_scriptAimYaw * 0.0174533f, pa = s_scriptAimPitch * 0.0174533f;
					const XrQuaternionf qy = { 0.0f, sinf(ya * 0.5f), 0.0f, cosf(ya * 0.5f) };
					const XrQuaternionf qp = { sinf(pa * 0.5f), 0.0f, 0.0f, cosf(pa * 0.5f) };
					q = mulQ(q, mulQ(qy, qp));
				}
				float hx = h ? 0.14f : -0.14f, hy = -0.16f, hz = -0.36f;
				{ float px, py; if (h == 1 && scriptValue("handpos", &px, &py)) { hx = px; hy = py; } }   // test: right hand position (x, y)
				if (h == 0 && scriptValue("wrist", nullptr, nullptr))
				{
					// Look at the watch: forearm across the chest (fingers to the right), back of the wrist up.
					hx = 0.10f; hy = -0.20f; hz = -0.34f;
					const XrQuaternionf ry = { 0.0f, sinf(-0.916f), 0.0f, cosf(-0.916f) };
					const XrQuaternionf rz = { 0.0f, 0.0f, sinf(-0.916f), cosf(-0.916f) };
					q = mulQ(q, mulQ(ry, rz));
				}
				const XrVector3f off = rotate(qBase, { hx, hy, hz });
				hi.aimValid = true;
				hi.aimPose.orientation = q;
				hi.aimPose.position = { basePos.x + off.x, basePos.y + off.y, basePos.z + off.z };
				const XrVector3f gofs = rotate(q, { 0.0f, -0.025f, 0.055f });
				hi.valid = true;
				hi.pose.orientation = q;
				hi.pose.position = { hi.aimPose.position.x + gofs.x, hi.aimPose.position.y + gofs.y, hi.aimPose.position.z + gofs.z };
				float g = 0.0f;
				if (scriptValue("grip", &g, nullptr)) { hi.grip = g; }
				if (scriptValue("thumb", nullptr, nullptr)) { hi.thumbTouch = true; }
			}
		}

		// ---- VR gestures ----
		if (!menuMode)
		{
			HandInput& R = s_hands[1];
			HandInput& L = s_hands[0];
			// 1) Punching: with fists selected, a fast forward jab fires.
			for (int h = 0; h < 2; h++)
			{
				HandInput& H = s_hands[h];
				if (H.valid && s_prevHandValid[h] && s_weaponId == 0)
				{
					const XrVector3f v = { (H.pose.position.x - s_prevHandPos[h].x) / s_frameDt, (H.pose.position.y - s_prevHandPos[h].y) / s_frameDt, (H.pose.position.z - s_prevHandPos[h].z) / s_frameDt };
					const XrVector3f fwd = rotate(s_hands[h].aimValid ? s_hands[h].aimPose.orientation : H.pose.orientation, { 0, 0, -1 });
					const float along = v.x * fwd.x + v.y * fwd.y + v.z * fwd.z;
					if (along > 1.8f && s_punchFrames <= -10) { s_punchFrames = 4; haptic(h, 0.6f, 0.05f); }
				}
				s_prevHandValid[h] = H.valid;
				if (H.valid) { s_prevHandPos[h] = H.pose.position; }
			}
			if (s_punchFrames > 0) { TFE_Input::setAxis(AXIS_RIGHT_TRIGGER, 1.0f); }
			if (s_punchFrames > -10) { s_punchFrames--; }

			// 2) Over-the-shoulder grab cycles the weapon (reach behind the head and squeeze).
			if (R.valid)
			{
				const XrPosef rr = toRef(R.pose);
				const XrVector3f hp = s_headPose.position;
				const bool zone = (rr.position.y > hp.y - 0.25f) && (rr.position.z > hp.z + 0.02f);
				if (zone && R.grip > 0.7f && s_shoulderArmed)
				{
					setButton(CONTROLLER_BUTTON_RIGHTSHOULDER, true);
					haptic(1, 0.5f, 0.06f);
					s_shoulderArmed = false;
				}
				if (R.grip < 0.3f) { s_shoulderArmed = true; }
			}

			// (Two-handed aim with the left hand was removed in 0.29.)
		}

		// ---- Right hand: punch animation and walls ----
		{
			HandInput& R = s_hands[1];
			if (R.valid || R.aimValid)
			{
				const XrQuaternionf ro = R.aimValid ? R.aimPose.orientation : R.pose.orientation;
				const XrVector3f fwd = rotate(ro, { 0, 0, -1 });
				// Trigger punch with fists: the hand jabs forward and back.
				float jab = 0.0f;
				if (s_punchAnim > 0.0f)
				{
					const float t = 1.0f - s_punchAnim / c_punchTime;
					jab = 0.17f * sinf(3.14159265f * fminf(1.0f, t));
					s_punchAnim -= s_frameDt;
				}
				// The weapon never goes into a wall, and the hand is never moved: when the muzzle would be
				// inside a wall, the right hand and weapon are drawn scaled toward the head (same image in
				// the eyes, less depth), so they stay in front of the wall.
				float push = 0.0f, reach = 0.0f;
				XrVector3f mp, md;
				if (s_weaponId >= 0 && hands_muzzle(R, s_weaponId, &mp, &md))
				{
					mp.x += fwd.x * jab; mp.y += fwd.y * jab; mp.z += fwd.z * jab;
					XrPosef m = { {0,0,0,1}, mp };
					const XrPosef r = toRef(m);
					float c[3];
					headCorrection(c);
					const float off[3] = { r.position.x + c[0], r.position.y + c[1], r.position.z + c[2] };
					push = tfe_df_vr_wall_penetration(off, 0.03f);
					const XrVector3f hp = s_headPoseApp.position;
					reach = sqrtf((mp.x - hp.x) * (mp.x - hp.x) + (mp.y - hp.y) * (mp.y - hp.y) + (mp.z - hp.z) * (mp.z - hp.z));
				}
				float target = 1.0f;
				if (push > 0.0f && reach > 0.05f) { target = fmaxf(0.3f, (reach - push) / reach); }
				s_wallPush += (target - s_wallPush) * (target < s_wallPush ? 1.0f : fminf(1.0f, s_frameDt * 8.0f));
				hands_setCompression(s_headPoseApp.position, s_opt.wallMode != 0 ? s_wallPush : 1.0f);
				if (fabsf(jab) > 1e-4f)
				{
					R.pose.position.x += fwd.x * jab; R.pose.position.y += fwd.y * jab; R.pose.position.z += fwd.z * jab;
					R.aimPose.position.x += fwd.x * jab; R.aimPose.position.y += fwd.y * jab; R.aimPose.position.z += fwd.z * jab;
				}
			}
		}

		if ((++s_diagFrame % 72) == 0)
		{
			XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
			gi.action = s_actMove;
			XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
			xrGetActionStateVector2f(s_session, &gi, &st);
			LOGI("diag2: weapon=%d empty=%d handR=%d/%d handL=%d/%d physCrouch=%d head=%.2f,%.2f,%.2f eyeDrop=%.2f",
				s_weaponId, s_weaponAmmoEmpty ? 1 : 0, s_hands[1].valid ? 1 : 0, s_hands[1].aimValid ? 1 : 0, s_hands[0].valid ? 1 : 0, s_hands[0].aimValid ? 1 : 0,
				s_physCrouch ? 1 : 0, s_headPose.position.x, s_headPose.position.y, s_headPose.position.z, s_eyeDrop);
			LOGI("diag3: run=%d automap=%d kbd=%d hit=%d hover=%d", s_runActive ? 1 : 0, s_automap ? 1 : 0, s_kbdVisible ? 1 : 0, s_kbdHit ? 1 : 0, s_kbdHover);
			LOGI("diag: world=%d shouldRender=%d moveActive=%d move=%.2f,%.2f turn=%.2f trigR=%.2f relMouse=%d aimValid=%d cursor=%d mouse=%d,%d",
				s_worldLastFrame ? 1 : 0, (int)s_frameState.shouldRender, (int)st.isActive, move.x, move.y, turn.x, trigR, TFE_Input::relativeModeEnabled() ? 1 : 0,
				s_aimValid ? 1 : 0, s_cursorVisible ? 1 : 0, s_mouseX, s_mouseY);
		}
	}

	bool createSwapchain(Swapchain& sc, int w, int h, bool depth)
	{
		XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		ci.format = s_colorFormat;
		ci.sampleCount = 1;
		ci.width = w; ci.height = h;
		ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
		XrResult r = xrCreateSwapchain(s_session, &ci, &sc.handle);
		if (XR_FAILED(r)) { LOGE("xrCreateSwapchain %dx%d: %s", w, h, resultStr(r)); return false; }
		sc.width = w; sc.height = h;
		uint32_t count = 0;
		xrEnumerateSwapchainImages(sc.handle, 0, &count, nullptr);
		sc.images.assign(count, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
		xrEnumerateSwapchainImages(sc.handle, count, &count, (XrSwapchainImageBaseHeader*)sc.images.data());
		if (depth)
		{
			glGenRenderbuffers(1, &sc.depth);
			glBindRenderbuffer(GL_RENDERBUFFER, sc.depth);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
		}
		sc.fbos.resize(count);
		glGenFramebuffers(count, sc.fbos.data());
		for (uint32_t i = 0; i < count; i++)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, sc.fbos[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sc.images[i].image, 0);
			if (depth) { glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sc.depth); }
			GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (st != GL_FRAMEBUFFER_COMPLETE) { LOGE("swapchain fbo incomplete 0x%x", st); }
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return true;
	}

	bool acquire(Swapchain& sc)
	{
		if (sc.acquired) { return true; }
		XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		if (XR_FAILED(xrAcquireSwapchainImage(sc.handle, &ai, &sc.index))) { return false; }
		XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		wi.timeout = XR_INFINITE_DURATION;
		if (XR_FAILED(xrWaitSwapchainImage(sc.handle, &wi))) { return false; }
		sc.acquired = true;
		return true;
	}
	void release(Swapchain& sc)
	{
		if (!sc.acquired) { return; }
		XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		xrReleaseSwapchainImage(sc.handle, &ri);
		sc.acquired = false;
	}

	GLuint compile(GLenum type, const char* src)
	{
		GLuint s = glCreateShader(type);
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok = 0;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); LOGE("shader: %s", log); }
		return s;
	}

	void createScreen(int w, int h)
	{
		s_screenW = w; s_screenH = h;
		glGenTextures(1, &s_screenTex);
		glBindTexture(GL_TEXTURE_2D, s_screenTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		glGenRenderbuffers(1, &s_screenDepth);
		glBindRenderbuffer(GL_RENDERBUFFER, s_screenDepth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glGenFramebuffers(1, &s_screenFbo);
		glBindFramebuffer(GL_FRAMEBUFFER, s_screenFbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_screenTex, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_screenDepth);
		GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (st != GL_FRAMEBUFFER_COMPLETE) { LOGE("screen fbo incomplete 0x%x", st); }

		const char* vs =
			"#version 300 es\n"
			"uniform vec4 UvRect;\n"
			"out vec2 uv; out vec2 lp;\n"
			"void main(){ vec2 p = vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2));\n"
			" lp = p; uv = UvRect.xy + p * UvRect.zw; gl_Position = vec4(p*2.0-1.0, 0.0, 1.0); }\n";
		const char* fs =
			"#version 300 es\n"
			"precision mediump float;\n"
			"uniform sampler2D Tex; uniform float Key; uniform vec3 Cursor; uniform vec4 Mask0; uniform vec4 Mask1;\n"
			"uniform vec2 Dilate; uniform float Holo;\n"
			"in vec2 uv; in vec2 lp; out vec4 o;\n"
			"bool inR(vec2 p, vec4 r){ return r.z > 0.0 && p.x >= r.x && p.y >= r.y && p.x <= r.x + r.z && p.y <= r.y + r.w; }\n"
			"vec4 tap(vec2 p){ return (inR(p, Mask0) || inR(p, Mask1)) ? vec4(0.0) : texture(Tex, p); }\n"
			"void main(){ vec4 c = tap(uv);\n"
			" if (Dilate.x > 0.0) {\n"   // thicken 1-pixel map lines so they survive the small wrist screen
			"  for (int j = -2; j <= 2; j++) for (int i = -2; i <= 2; i++) c = max(c, tap(uv + vec2(float(i), float(j)) * Dilate) * 0.92);\n"
			" }\n"
			" float l = max(c.r, max(c.g, c.b));\n"
			" float a = (Key > 0.5 && l < 0.015) ? 0.0 : 1.0;\n"
			" if (inR(uv, Mask0) || inR(uv, Mask1)) a = 0.0;\n"
			" o = vec4(c.rgb * a, a);\n"
			" if (Holo > 0.5) {\n"   // Star Wars style hologram: blue light, scanlines, faint panel
			"  float scan = 0.65 + 0.35 * step(1.5, mod(gl_FragCoord.y, 4.0));\n"
			"  vec3 h = (mix(c.rgb, vec3(0.35, 0.75, 1.0) * l, 0.25) * 1.35 + vec3(0.03, 0.08, 0.12)) * scan;\n"   // original map colors (walls, doors, keys, ledges, player) with a holo glow
			"  if (l < 0.03) h = vec3(0.35, 0.75, 1.0) * 0.35 * scan;\n"
			"  float ha = l < 0.03 ? 0.13 : min(1.0, 0.35 + l * 1.5) * 0.9;\n"
			"  vec2 q = min(lp, 1.0 - lp);\n"                      // 0 at the edge of the projection
			"  float edge = min(q.x * 1.6, q.y);\n"
			"  ha *= smoothstep(0.0, 0.08, edge);\n"                // soft fade at the borders
			"  if (edge < 0.012) { h = vec3(0.45, 0.85, 1.0); ha = 0.55; }\n"   // thin projector frame
			"  ha *= 0.92 + 0.08 * sin(gl_FragCoord.y * 0.05);\n"
			"  o = vec4(h * ha, ha);\n"
			" }\n"
			" if (Cursor.z > 0.5) {\n"
			"  float d = length(gl_FragCoord.xy - Cursor.xy);\n"
			"  if (d < 9.0) o = vec4(1.0, 0.85, 0.2, 1.0);\n"
			"  if (d < 5.5) o = vec4(0.1, 0.1, 0.1, 1.0);\n"
			"  if (d < 2.5) o = vec4(1.0, 1.0, 1.0, 1.0);\n"
			" } }\n";
		s_keyProgram = glCreateProgram();
		GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
		glAttachShader(s_keyProgram, v);
		glAttachShader(s_keyProgram, f);
		glLinkProgram(s_keyProgram);
		s_keyLocTex = glGetUniformLocation(s_keyProgram, "Tex");
		s_keyLocKey = glGetUniformLocation(s_keyProgram, "Key");
		s_keyLocCursor = glGetUniformLocation(s_keyProgram, "Cursor");
		s_keyLocUvRect = glGetUniformLocation(s_keyProgram, "UvRect");
		s_keyLocMask0 = glGetUniformLocation(s_keyProgram, "Mask0");
		s_keyLocMask1 = glGetUniformLocation(s_keyProgram, "Mask1");
		s_keyLocDilate = glGetUniformLocation(s_keyProgram, "Dilate");
		s_keyLocHolo = glGetUniformLocation(s_keyProgram, "Holo");
		glGenVertexArrays(1, &s_keyVao);
	}

	void handleEvents()
	{
		XrEventDataBuffer ev = { XR_TYPE_EVENT_DATA_BUFFER };
		while (xrPollEvent(s_instance, &ev) == XR_SUCCESS)
		{
			if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
			{
				s_recenterRequested = true;
			}
			else if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
			{
				const XrEventDataSessionStateChanged* sc = (const XrEventDataSessionStateChanged*)&ev;
				s_state = sc->state;
				LOGI("session state %d", (int)s_state);
				if (s_state == XR_SESSION_STATE_READY)
				{
					XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
					bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					XR_CHECK(xrBeginSession(s_session, &bi));
					s_running = true;
				}
				else if (s_state == XR_SESSION_STATE_STOPPING)
				{
					XR_CHECK(xrEndSession(s_session));
					s_running = false;
				}
				else if (s_state == XR_SESSION_STATE_EXITING || s_state == XR_SESSION_STATE_LOSS_PENDING)
				{
					s_running = false;
					SDL_Event q = {};
					q.type = SDL_QUIT;
					SDL_PushEvent(&q);
				}
			}
			ev = { XR_TYPE_EVENT_DATA_BUFFER };
		}
	}
}

extern "C" int tfe_xr_init(int screenWidth, int screenHeight)
{
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	JavaVM* vm = nullptr;
	if (!env || !activity || env->GetJavaVM(&vm) != 0) { LOGE("no JNI/activity"); return 0; }

	PFN_xrInitializeLoaderKHR initLoader = nullptr;
	xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&initLoader);
	if (initLoader)
	{
		XrLoaderInitInfoAndroidKHR li = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
		li.applicationVM = vm;
		li.applicationContext = activity;
		initLoader((const XrLoaderInitInfoBaseHeaderKHR*)&li);
	}

	const char* exts[] = { XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME };
	XrInstanceCreateInfoAndroidKHR aci = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
	aci.applicationVM = vm;
	aci.applicationActivity = activity;
	XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
	ci.next = &aci;
	strcpy(ci.applicationInfo.applicationName, "Dark Forces VR (TFE)");
	strcpy(ci.applicationInfo.engineName, "The Force Engine");
	ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	ci.enabledExtensionCount = 2;
	ci.enabledExtensionNames = exts;
	XrResult r = xrCreateInstance(&ci, &s_instance);
	if (XR_FAILED(r)) { LOGE("xrCreateInstance: %d", (int)r); return 0; }

	XrSystemGetInfo sgi = { XR_TYPE_SYSTEM_GET_INFO };
	sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (XR_FAILED(xrGetSystem(s_instance, &sgi, &s_system))) { LOGE("xrGetSystem failed"); return 0; }

	uint32_t nv = 0;
	s_viewConfig[0] = s_viewConfig[1] = { XR_TYPE_VIEW_CONFIGURATION_VIEW };
	xrEnumerateViewConfigurationViews(s_instance, s_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nv, s_viewConfig);

	PFN_xrGetOpenGLESGraphicsRequirementsKHR getReq = nullptr;
	xrGetInstanceProcAddr(s_instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&getReq);
	if (getReq) { XrGraphicsRequirementsOpenGLESKHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR }; getReq(s_instance, s_system, &req); }

	EGLDisplay dpy = eglGetCurrentDisplay();
	EGLContext ctx = eglGetCurrentContext();
	EGLConfig cfg = nullptr;
	{
		EGLint cfgId = 0, n = 0;
		eglQueryContext(dpy, ctx, EGL_CONFIG_ID, &cfgId);
		EGLint attrs[] = { EGL_CONFIG_ID, cfgId, EGL_NONE };
		eglChooseConfig(dpy, attrs, &cfg, 1, &n);
	}
	XrGraphicsBindingOpenGLESAndroidKHR gb = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
	gb.display = dpy; gb.config = cfg; gb.context = ctx;
	XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
	sci.next = &gb;
	sci.systemId = s_system;
	r = xrCreateSession(s_instance, &sci, &s_session);
	if (XR_FAILED(r)) { LOGE("xrCreateSession: %s", resultStr(r)); return 0; }

	XrReferenceSpaceCreateInfo rs = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	rs.poseInReferenceSpace.orientation.w = 1.0f;
	XR_CHECK(xrCreateReferenceSpace(s_session, &rs, &s_appSpace));
	rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	XR_CHECK(xrCreateReferenceSpace(s_session, &rs, &s_viewSpace));

	// sRGB swapchains with raw writes keep TFE's gamma-space colors unchanged.
	const char* glExt = (const char*)glGetString(GL_EXTENSIONS);
	s_srgbWriteControl = glExt && strstr(glExt, "GL_EXT_sRGB_write_control");
	uint32_t nf = 0;
	xrEnumerateSwapchainFormats(s_session, 0, &nf, nullptr);
	std::vector<int64_t> formats(nf);
	xrEnumerateSwapchainFormats(s_session, nf, &nf, formats.data());
	bool hasSrgb = false, hasRgba = false;
	for (int64_t f : formats) { if (f == GL_SRGB8_ALPHA8) hasSrgb = true; if (f == GL_RGBA8) hasRgba = true; }
	s_colorFormat = (hasSrgb && s_srgbWriteControl) ? GL_SRGB8_ALPHA8 : (hasRgba ? GL_RGBA8 : (GLenum)formats[0]);
	LOGI("swapchain format 0x%x (srgb write control %d)", s_colorFormat, s_srgbWriteControl ? 1 : 0);

	for (int e = 0; e < 2; e++)
	{
		if (!createSwapchain(s_eye[e], s_viewConfig[e].recommendedImageRectWidth, s_viewConfig[e].recommendedImageRectHeight, true)) { return 0; }
	}
	if (!createSwapchain(s_quad, screenWidth, screenHeight, false)) { return 0; }
	for (int i = 0; i < 2; i++) { createSwapchain(s_hud[i], c_hudW, c_hudH, false); }
	createSwapchain(s_kbd, c_kbdW, c_kbdH, false);
	createSwapchain(s_holo, 1024, 640, false);
	loadOptions();
	createScreen(screenWidth, screenHeight);
	initInput();

	s_active = true;
	LOGI("OpenXR ready: eye %dx%d, screen %dx%d", s_eye[0].width, s_eye[0].height, screenWidth, screenHeight);

	// Wait for the session to start so the first frames are not lost.
	for (int i = 0; i < 300 && !s_running; i++) { handleEvents(); if (!s_running) SDL_Delay(10); }
	glBindFramebuffer(GL_FRAMEBUFFER, s_screenFbo);
	return 1;
}

extern "C" int tfe_xr_active(void) { return s_active ? 1 : 0; }
extern "C" unsigned int tfe_xr_screen_fbo(void) { return s_active ? s_screenFbo : 0; }
extern "C" float tfe_xr_units_per_meter(void) { return c_unitsPerMeter; }

extern "C" void tfe_xr_begin_frame(void)
{
	if (!s_active) { return; }
	handleEvents();
	s_worldThisFrame = false;
	s_eyeRendered[0] = s_eyeRendered[1] = false;
	if (!s_running || s_frameBegun) { return; }

	XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
	s_frameState = { XR_TYPE_FRAME_STATE };
	if (XR_FAILED(xrWaitFrame(s_session, &wi, &s_frameState))) { return; }
	XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
	if (XR_FAILED(xrBeginFrame(s_session, &bi))) { return; }
	s_frameBegun = true;

	XrViewLocateInfo vli = { XR_TYPE_VIEW_LOCATE_INFO };
	vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	vli.displayTime = s_frameState.predictedDisplayTime;
	vli.space = s_appSpace;
	XrViewState vs = { XR_TYPE_VIEW_STATE };
	s_views[0] = s_views[1] = { XR_TYPE_VIEW };
	uint32_t n = 0;
	s_viewsValid = XR_SUCCEEDED(xrLocateViews(s_session, &vli, &vs, 2, &n, s_views)) &&
		(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);

	XrSpaceLocation hl = { XR_TYPE_SPACE_LOCATION };
	if (XR_SUCCEEDED(xrLocateSpace(s_viewSpace, s_appSpace, s_frameState.predictedDisplayTime, &hl)) &&
		(hl.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
	{
		if (s_recenterRequested || !s_refValid) { recenter(hl.pose); }
		XrPosef rel = toRef(hl.pose);
		// A large jump means the headset was picked up after launch: recenter automatically.
		if (sqrtf(rel.position.x*rel.position.x + rel.position.z*rel.position.z) > 0.6f) { recenter(hl.pose); rel = toRef(hl.pose); }
		s_headPose = rel;
		s_headPoseApp = hl.pose;

		const double now = SDL_GetTicks() / 1000.0;
		float dt = float(now - s_lastFrameTime);
		s_lastFrameTime = now;
		if (dt > 0.1f) dt = 0.1f;
		s_frameDt = dt > 0.001f ? dt : 0.001f;
		s_flash = fmaxf(0.0f, s_flash - dt * 14.0f);
		{
			const float target = s_opt.comfortVignette * (s_moveAmount > 0.1f ? 0.5f + 0.5f * s_moveAmount : 0.0f);
			s_vignette += (target - s_vignette) * fminf(1.0f, dt * 6.0f);
		}
		// The screen (messages, menus) lazily follows the head during gameplay.
		if (s_opt.lazyScreen && s_worldLastFrame && !s_systemUi)
		{
			const float hy = yawOf(hl.pose.orientation);
			float d = hy - s_quadYaw;
			while (d > 3.14159265f) d -= 6.2831853f;
			while (d < -3.14159265f) d += 6.2831853f;
			if (fabsf(d) > 0.6f) { s_quadFollowing = true; }
			if (s_quadFollowing)
			{
				placeQuad(s_quadYaw + d * fminf(1.0f, dt * 5.0f));
				if (fabsf(d) < 0.05f) { s_quadFollowing = false; }
			}
		}
	}

	s_frameIndex++;
	updateInput();
	// Pause menu PDA in the left hand.
	s_pdaMenu = s_pdaFrames > 0;
	if (s_pdaFrames > 0) { s_pdaFrames--; }
	// The PDA is held by the left hand and follows it.
	hands_setPdaScale(s_opt.pdaScale);
	{
		// The tablet stays where it was if the left controller loses tracking for a moment (the menu
		// would jump to the big screen otherwise).
		static bool s_pdaHad = false;
		XrPosef p;
		if (s_pdaMenu && hands_pdaScreen(s_hands[0], &p)) { s_pdaPose = p; s_pdaHad = true; }
		if (!s_pdaMenu) { s_pdaHad = false; }
		s_pdaValid = s_pdaMenu && s_pdaHad;
	}

	// Right controller aim pose for weapon aiming.
	s_aimValid = false;
	if (s_aimSpace != XR_NULL_HANDLE)
	{
		XrSpaceLocation al = { XR_TYPE_SPACE_LOCATION };
		if (XR_SUCCEEDED(xrLocateSpace(s_aimSpace, s_appSpace, s_frameState.predictedDisplayTime, &al)) &&
			(al.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) && (al.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
		{
			s_aimPose = al.pose;
			s_aimValid = true;
		}
	}
}

// Correction applied to everything given to the game in the reference frame so it matches the
// rendered camera: the head is kept close to the collision body (no seeing through walls), and a
// physical crouch adds back what the game itself lowered the eye.
static void headCorrection(float c[3])
{
	const XrVector3f& h = s_headPose.position;
	const float hl = sqrtf(h.x*h.x + h.z*h.z);
	const float maxH = 0.25f;
	c[0] = 0.0f; c[2] = 0.0f;
	if (hl > maxH) { c[0] = h.x * (maxH / hl - 1.0f); c[2] = h.z * (maxH / hl - 1.0f); }
	float yOff = h.y + (s_physCrouch ? s_eyeDrop / c_unitsPerMeter : 0.0f);
	if (yOff > 0.15f) yOff = 0.15f;
	if (yOff < -0.9f) yOff = -0.9f;
	c[1] = yOff - h.y;
	c[1] += s_opt.heightOffset;   // seated play / taller or shorter view
}

extern "C" int tfe_xr_get_aim(float dir[3], float pos[3])
{
	if (!s_active) { return 0; }
	XrVector3f mp, md;
	if (hands_muzzle(s_hands[1], s_weaponId, &mp, &md))
	{
		XrPosef m = { {0,0,0,1}, mp };
		const XrPosef r = toRef(m);
		const XrVector3f d = toRefDir(md);
		dir[0] = d.x; dir[1] = d.y; dir[2] = d.z;
		pos[0] = r.position.x; pos[1] = r.position.y; pos[2] = r.position.z;
		// Same correction as the camera, so the shot leaves the muzzle that is drawn.
		float c[3];
		headCorrection(c);
		pos[0] += c[0]; pos[1] += c[1]; pos[2] += c[2];
		s_lastMuzzleLog = 1;
		return 1;
	}
	if (s_scriptAim)
	{
		// Scripted aim without hands: from the head center (yaw left positive, pitch up positive).
		const float y = s_scriptAimYaw * 0.0174533f, pch = s_scriptAimPitch * 0.0174533f;
		dir[0] = -sinf(y) * cosf(pch); dir[1] = sinf(pch); dir[2] = -cosf(y) * cosf(pch);
		pos[0] = 0.2f; pos[1] = -0.3f; pos[2] = -0.3f;
		return 1;
	}
	if (!s_aimValid) { return 0; }
	const XrPosef a = toRef(s_aimPose);
	const XrVector3f f = rotate(a.orientation, { 0, 0, -1 });
	dir[0] = f.x; dir[1] = f.y; dir[2] = f.z;
	pos[0] = a.position.x; pos[1] = a.position.y; pos[2] = a.position.z;
	return 1;
}

extern "C" int tfe_xr_world_ready(void)
{
	return (s_active && s_frameBegun && s_viewsValid && s_frameState.shouldRender) ? 1 : 0;
}

extern "C" void tfe_xr_get_view(int eye, float quat[4], float pos[3], float fovTan[4])
{
	const XrPosef p = (eye < 0) ? s_headPose : toRef(s_views[eye].pose);
	quat[0] = p.orientation.x; quat[1] = p.orientation.y; quat[2] = p.orientation.z; quat[3] = p.orientation.w;
	pos[0] = p.position.x; pos[1] = p.position.y; pos[2] = p.position.z;
	{
		float c[3];
		headCorrection(c);
		pos[0] += c[0]; pos[1] += c[1]; pos[2] += c[2];
	}
	if (eye < 0)
	{
		// Union of both eyes, used for portal traversal.
		const XrFovf& a = s_views[0].fov;
		const XrFovf& b = s_views[1].fov;
		const float l = fminf(tanf(a.angleLeft), tanf(b.angleLeft));
		const float r = fmaxf(tanf(a.angleRight), tanf(b.angleRight));
		const float u = fmaxf(tanf(a.angleUp), tanf(b.angleUp));
		const float d = fminf(tanf(a.angleDown), tanf(b.angleDown));
		const float h = fmaxf(fmaxf(-l, r), fmaxf(u, -d)) * 1.1f;
		fovTan[0] = -h; fovTan[1] = h; fovTan[2] = h; fovTan[3] = -h;
	}
	else
	{
		const XrFovf& f = s_views[eye].fov;
		fovTan[0] = tanf(f.angleLeft); fovTan[1] = tanf(f.angleRight);
		fovTan[2] = tanf(f.angleUp);   fovTan[3] = tanf(f.angleDown);
	}
}

extern "C" void tfe_xr_bind_eye(int eye, int* width, int* height)
{
	Swapchain& sc = s_eye[eye];
	*width = sc.width; *height = sc.height;
	if (!acquire(sc)) { return; }
	glBindFramebuffer(GL_FRAMEBUFFER, sc.fbos[sc.index]);
	glViewport(0, 0, sc.width, sc.height);
	glDisable(GL_SCISSOR_TEST);
	if (s_srgbWriteControl) { glDisable(GL_FRAMEBUFFER_SRGB_EXT); }
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepthf(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

extern "C" void tfe_xr_unbind_eye(int eye)
{
	Swapchain& sc = s_eye[eye];
	if (!sc.acquired) { return; }
	// Avoid storing depth/stencil back to memory on tilers.
	// glInvalidateFramebuffer is a GLES 3.0 entry point that the desktop glad loader leaves null.
	typedef void (GLAD_API_PTR *PFN_INVALIDATE)(GLenum, GLsizei, const GLenum*);
	static PFN_INVALIDATE s_invalidate = (PFN_INVALIDATE)eglGetProcAddress("glInvalidateFramebuffer");
	if (s_invalidate)
	{
		const GLenum discard[] = { GL_DEPTH_STENCIL_ATTACHMENT };
		s_invalidate(GL_FRAMEBUFFER, 1, discard);
	}
	release(sc);
	s_eyeRendered[eye] = true;
	if (s_eyeRendered[0] && s_eyeRendered[1]) { s_worldThisFrame = true; }
}

namespace { void drawSnapshot(int eye); }

extern "C" void tfe_xr_end_frame(void)
{
	if (!s_active) { return; }
	if (!s_frameBegun)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, s_screenFbo);
		return;
	}
	{
		// Test hook: "dump" in the input script writes the 2D screen (game + system UI) to tfe_dump.ppm.
		static float dumped = -1.0f;
		float dumpId = 0.0f;
		if (scriptValue("dump", &dumpId, nullptr) && dumpId != dumped)
		{
			dumped = dumpId;
			std::vector<unsigned char> px(size_t(s_screenW) * s_screenH * 4);
			glBindFramebuffer(GL_FRAMEBUFFER, s_screenFbo);
			glReadPixels(0, 0, s_screenW, s_screenH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
			char dumpName[64];
			snprintf(dumpName, sizeof(dumpName), "tfe_dump%d.ppm", int(dumpId));
			FILE* f = fopen(dumpName, "wb");
			if (f)
			{
				fprintf(f, "P6\n%d %d\n255\n", s_screenW, s_screenH);
				for (int y = s_screenH - 1; y >= 0; y--)
					for (int x = 0; x < s_screenW; x++) { fwrite(&px[(size_t(y) * s_screenW + x) * 4], 1, 3, f); }
				fclose(f);
			}
			LOGI("dump %dx%d sysUi=%d", s_screenW, s_screenH, s_systemUi ? 1 : 0);
		}
	}
	// Eyes acquired but not completed must still be released.
	release(s_eye[0]);
	release(s_eye[1]);

	// HUD on the body while playing (the pieces are cut out of the screen quad):
	// health/shields on the screen of the left wrist computer, ammo on the weapon.
	bool hudPanel[2] = { false, false };
	XrPosef hudPose[2];
	XrPosef holoPose;
	bool holo = false, mapOnWatch = false;
	const XrVector3f headApp = { (s_views[0].pose.position.x + s_views[1].pose.position.x) * 0.5f,
		(s_views[0].pose.position.y + s_views[1].pose.position.y) * 0.5f, (s_views[0].pose.position.z + s_views[1].pose.position.z) * 0.5f };
	if (s_worldThisFrame)
	{
		XrPosef face;
		const bool watch = hands_watch(s_hands[0], &face, &holoPose, headApp);
		const bool mapOn = watch && s_automap && !s_systemUi && !s_pdaMenu;
		holo = mapOn && s_opt.mapMode == 1 && s_holo.handle != XR_NULL_HANDLE;
		mapOnWatch = mapOn && s_opt.mapMode == 0;
		if (mapOnWatch && s_hud[0].handle != XR_NULL_HANDLE) { hudPanel[0] = true; hudPose[0] = face; }
		if (s_opt.wristHud && s_hudRectValid && !s_pdaMenu)
		{
			for (int i = 0; i < 2; i++)
			{
				if (s_hud[i].handle == XR_NULL_HANDLE || s_hudRect[i][2] <= 0.0f) { continue; }
				if (i == 0) { hudPanel[0] = watch; hudPose[0] = face; }
				else
				{
					const int wp = (!s_weaponAmmoEmpty && !(s_opt.wallMode == 0 && s_wallPush < 0.9f)) ? hands_weaponPanel(s_hands[1], s_weaponId, &hudPose[1], headApp) : 0;
					hudPanel[1] = wp > 0 || (wp == 0 && hands_wristPanel(s_hands[1], 1, &hudPose[1]));
				}
			}
		}
	}
	const bool wristHud = hudPanel[0] || hudPanel[1];
	for (int i = 0; i < 2; i++)
	{
		if (!hudPanel[i] || !acquire(s_hud[i])) { hudPanel[i] = false; continue; }
		glBindFramebuffer(GL_FRAMEBUFFER, s_hud[i].fbos[s_hud[i].index]);
		glViewport(0, 0, s_hud[i].width, s_hud[i].height);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		glDepthMask(GL_FALSE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		if (s_srgbWriteControl) { glDisable(GL_FRAMEBUFFER_SRGB_EXT); }
		glUseProgram(s_keyProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, s_screenTex);
		glBindSampler(0, 0);
		glUniform1i(s_keyLocTex, 0);
		glUniform2f(s_keyLocDilate, 0.0f, 0.0f);
		glUniform1f(s_keyLocHolo, 0.0f);
		glUniform1f(s_keyLocKey, i == 0 ? 0.0f : 1.0f);   // the watch has an opaque screen
		if (i == 0 && mapOnWatch)
		{
			// The original Dark Forces automap on the wrist computer screen: central part of the
			// screen (around the player), HUD pieces cut out, lines thickened to stay readable.
			glUniform4f(s_keyLocUvRect, c_mapCrop[0], c_mapCrop[1], c_mapCrop[2], c_mapCrop[3]);
			glUniform4fv(s_keyLocMask0, 1, s_hudRect[0]);
			glUniform4fv(s_keyLocMask1, 1, s_hudRect[1]);
			glUniform2f(s_keyLocDilate, 0.8f / s_screenW, 0.8f / s_screenH);
		}
		else
		{
			glUniform4fv(s_keyLocUvRect, 1, s_hudRect[i]);
			glUniform4f(s_keyLocMask0, 0.0f, 0.0f, 0.0f, 0.0f);
			glUniform4f(s_keyLocMask1, 0.0f, 0.0f, 0.0f, 0.0f);
		}
		glUniform3f(s_keyLocCursor, 0.0f, 0.0f, 0.0f);
		glBindVertexArray(s_keyVao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glUseProgram(0);
		release(s_hud[i]);
	}

	// Map hologram projected above the wrist computer.
	if (holo && acquire(s_holo))
	{
		glBindFramebuffer(GL_FRAMEBUFFER, s_holo.fbos[s_holo.index]);
		glViewport(0, 0, s_holo.width, s_holo.height);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		glDepthMask(GL_FALSE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		if (s_srgbWriteControl) { glDisable(GL_FRAMEBUFFER_SRGB_EXT); }
		glUseProgram(s_keyProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, s_screenTex);
		glBindSampler(0, 0);
		glUniform1i(s_keyLocTex, 0);
		glUniform1f(s_keyLocKey, 1.0f);
		glUniform4f(s_keyLocUvRect, c_mapCrop[0], c_mapCrop[1], c_mapCrop[2], c_mapCrop[3]);
		glUniform4fv(s_keyLocMask0, 1, s_hudRect[0]);
		glUniform4fv(s_keyLocMask1, 1, s_hudRect[1]);
		glUniform2f(s_keyLocDilate, 0.6f / s_screenW, 0.6f / s_screenH);
		glUniform1f(s_keyLocHolo, 1.0f);
		glUniform3f(s_keyLocCursor, 0.0f, 0.0f, 0.0f);
		glBindVertexArray(s_keyVao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glUseProgram(0);
		release(s_holo);
	}
	else { holo = false; }
	s_holoActive = holo;
	if (holo) { s_holoPose = holoPose; }

	// Virtual keyboard.
	bool kbdLayer = false;
	if (s_kbdVisible && acquire(s_kbd))
	{
		if (!s_kbdTex || s_kbdHover != s_kbdDrawnHover || s_kbdShift != s_kbdDrawnShift) { kbdRender(); }
		glBindFramebuffer(GL_FRAMEBUFFER, s_kbd.fbos[s_kbd.index]);
		glViewport(0, 0, s_kbd.width, s_kbd.height);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		glDepthMask(GL_FALSE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		if (s_srgbWriteControl) { glDisable(GL_FRAMEBUFFER_SRGB_EXT); }
		glUseProgram(s_keyProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, s_kbdTex);
		glBindSampler(0, 0);
		glUniform1i(s_keyLocTex, 0);
		glUniform2f(s_keyLocDilate, 0.0f, 0.0f);
		glUniform1f(s_keyLocHolo, 0.0f);
		glUniform1f(s_keyLocKey, 0.0f);
		glUniform4f(s_keyLocUvRect, 0.0f, 0.0f, 1.0f, 1.0f);
		glUniform4f(s_keyLocMask0, 0.0f, 0.0f, 0.0f, 0.0f);
		glUniform4f(s_keyLocMask1, 0.0f, 0.0f, 0.0f, 0.0f);
		glUniform3f(s_keyLocCursor, s_kbdCursor[0], s_kbdCursor[1], s_kbdHit ? 1.0f : 0.0f);
		glBindVertexArray(s_keyVao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glUseProgram(0);
		release(s_kbd);
		kbdLayer = true;
	}

	// Tablet face: the game's 320x200 canvas (the PDA or configuration frame with its live buttons and
	// cursor) copied into a texture, so the case shows the real frame and the screen layer only the
	// window inside it (one frame, no double border).
	{
		static GLuint s_liveTexId = 0, s_liveFbo = 0;
		const int LW = 640, LH = 400;
		const bool want = s_pdaMenu && s_pdaCanvasFrames > 0 && !s_pdaFitPage;
		if (s_pdaCanvasFrames > 0) { s_pdaCanvasFrames--; }
		// Mission tablet: the game keeps the GPU renderer (the world is live behind it), so the tablet
		// is composed on the CPU and handed over as an image.
		const bool img = s_pdaMenu && s_pdaImgFrames > 0;
		s_pdaImgShown = false;
		if (s_pdaImgFrames > 0) { s_pdaImgFrames--; }
		if (img)
		{
			GLint prevTex = 0, prevUnpack = 0;
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
			glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpack);
			if (!s_pdaImgTex)
			{
				glGenTextures(1, &s_pdaImgTex);
				glBindTexture(GL_TEXTURE_2D, s_pdaImgTex);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 320, 200, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			if (s_pdaImgNew)
			{
				glBindTexture(GL_TEXTURE_2D, s_pdaImgTex);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 320, 200, GL_RGBA, GL_UNSIGNED_BYTE, s_pdaImg);
				s_pdaImgNew = false;
			}
			glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpack);
			glBindTexture(GL_TEXTURE_2D, prevTex);
			hands_setPdaLiveTexture(s_pdaImgTex, 320, 200);
			s_pdaImgShown = true;
		}
		else if (want)
		{
			GLint prevTex = 0, prevRead = 0, prevDraw = 0; GLfloat prevClear[4]; GLboolean prevMask[4];
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
			glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
			glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);
			glGetBooleanv(GL_COLOR_WRITEMASK, prevMask);
			if (!s_liveTexId)
			{
				glGenTextures(1, &s_liveTexId);
				glBindTexture(GL_TEXTURE_2D, s_liveTexId);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, LW, LH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glGenFramebuffers(1, &s_liveFbo);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_liveFbo);
				glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_liveTexId, 0);
			}
			// Canvas rectangle in GL pixels of the screen (bottom-up); the face texture has the image top
			// at its first row, so the copy flips vertically.
			const int sx0 = int(s_pdaCanvas[0] * s_screenW + 0.5f), sx1 = int(s_pdaCanvas[2] * s_screenW + 0.5f);
			const int syTop = int((1.0f - s_pdaCanvas[1]) * s_screenH + 0.5f), syBot = int((1.0f - s_pdaCanvas[3]) * s_screenH + 0.5f);
			glDisable(GL_SCISSOR_TEST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, s_screenFbo);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_liveFbo);
			glBlitFramebuffer(sx0, syBot, sx1, syTop, 0, LH, LW, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			// Alpha = gloss for the case shader; texel (0, 0) (rounded away on the face) = frame metal
			// for the sides and back.
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
			glClearColor(0.0f, 0.0f, 0.0f, 0.35f);
			glClear(GL_COLOR_BUFFER_BIT);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glEnable(GL_SCISSOR_TEST); glScissor(0, 0, 2, 2);
			glClearColor(59.0f / 255.0f, 67.0f / 255.0f, 79.0f / 255.0f, 0.35f);
			glClear(GL_COLOR_BUFFER_BIT);
			glDisable(GL_SCISSOR_TEST);
			glColorMask(prevMask[0], prevMask[1], prevMask[2], prevMask[3]);
			glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
			glBindTexture(GL_TEXTURE_2D, prevTex);
			hands_setPdaLiveTexture(s_liveTexId, LW, LH);
		}
		else { hands_setPdaLiveTexture(0, 0, 0); }
	}

	// Screen -> quad swapchain (with black keyed out while the world is visible).
	if (acquire(s_quad))
	{
		glBindFramebuffer(GL_FRAMEBUFFER, s_quad.fbos[s_quad.index]);
		glViewport(0, 0, s_quad.width, s_quad.height);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		glDepthMask(GL_FALSE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		if (s_srgbWriteControl) { glDisable(GL_FRAMEBUFFER_SRGB_EXT); }
		glUseProgram(s_keyProgram);
		glActiveTexture(GL_TEXTURE0);
		const bool pdaImg = s_pdaImgShown && pdaActive() && !s_pdaFitPage;
		glBindTexture(GL_TEXTURE_2D, pdaImg ? s_pdaImgTex : s_screenTex);
		glBindSampler(0, 0);
		glUniform1i(s_keyLocTex, 0);
		glUniform2f(s_keyLocDilate, 0.0f, 0.0f);
		glUniform1f(s_keyLocHolo, 0.0f);
		glUniform1f(s_keyLocKey, (s_worldThisFrame && !s_systemUi && !pdaImg) ? 1.0f : 0.0f);
		float win[4];
		if (pdaImg && pdaMissionCrop(win))
		{
			// Mission tablet image (row 0 at the top): the window, flipped to the bottom-up quad.
			const float u0 = win[0] / 320.0f, v0 = win[1] / 200.0f, u1 = (win[2] + 1.0f) / 320.0f, v1 = (win[3] + 1.0f) / 200.0f;
			glUniform4f(s_keyLocUvRect, u0, v1, u1 - u0, -(v1 - v0));
		}
		else if (pdaMissionCrop(win))
		{
			// Only the canvas window of the PDA (GL texture coordinates are bottom-up).
			float u0, v0, u1, v1;
			canvasToScreen(win[0], win[1], &u0, &v0);
			canvasToScreen(win[2] + 1.0f, win[3] + 1.0f, &u1, &v1);
			glUniform4f(s_keyLocUvRect, u0, 1.0f - v1, u1 - u0, v1 - v0);
		}
		else { glUniform4f(s_keyLocUvRect, 0.0f, 0.0f, 1.0f, 1.0f); }
		if (wristHud && !s_systemUi)
		{
			glUniform4fv(s_keyLocMask0, 1, s_hudRect[0]);
			glUniform4fv(s_keyLocMask1, 1, s_hudRect[1]);
		}
		else
		{
			glUniform4f(s_keyLocMask0, 0.0f, 0.0f, 0.0f, 0.0f);
			glUniform4f(s_keyLocMask1, 0.0f, 0.0f, 0.0f, 0.0f);
		}
		// Dark Forces screens draw their own mouse cursor. The TFE menus (ImGui) have none: a pointer dot
		// is drawn where the controller ray hits the screen.
		{
			float tmpWin[4];
			const bool tfeUi = (s_systemUi || !TFE_Input::relativeModeEnabled()) && s_cursorVisible && s_mouseX >= 0 && !pdaMissionCrop(tmpWin);
			if (tfeUi)
			{
				const float cx = (float(s_mouseX) + 0.5f) * float(s_quad.width) / float(s_screenW);
				const float cy = (float(s_screenH - s_mouseY) - 0.5f) * float(s_quad.height) / float(s_screenH);
				glUniform3f(s_keyLocCursor, cx, cy, 1.0f);
			}
			else { glUniform3f(s_keyLocCursor, 0.0f, 0.0f, 0.0f); }
		}
		glBindVertexArray(s_keyVao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glUseProgram(0);
		release(s_quad);
	}

	// Menus and other 2D screens: the player stands in a ship room, the screen is its console.
	bool roomFrame = false;
	// Right after gameplay (the pause PDA asked for before s_pdaMenu turns on, menu transitions),
	// the frozen world stays up for a few frames instead of the ship room flashing in.
	static int s_roomGrace = 0;
	if (s_worldThisFrame) { s_roomGrace = 10; }
	else if (s_roomGrace > 0) { s_roomGrace--; }
	const bool frozen = (s_pdaMenu || s_pdaFrames > 0 || s_roomGrace > 0) && s_snapValid;
	if (s_frameState.shouldRender && !s_worldThisFrame && (s_opt.menuRoom || frozen))
	{
		const float floorY = s_refPos.y - 1.65f;
		const float qx = s_quadPose.position.x, qz = s_quadPose.position.z;
		const float cy = cosf(s_quadYaw), sy = sinf(s_quadYaw);
		const float model[16] = { cy, 0, -sy, 0,  0, 1, 0, 0,  sy, 0, cy, 0,  qx, floorY, qz, 1 };
		const float sw = screenW(), sh = sw * float(s_screenH) / float(s_screenW);
		// The engine caches GL state: everything changed here is put back afterwards.
		GLint prevFbo = 0, prevVp[4], prevProg = 0, prevVao = 0, prevBuf = 0, prevDepthFunc = 0, prevTex = 0, prevActive = 0, prevFront = 0;
		GLint prevBlendSrc = 0, prevBlendDst = 0, prevBlendSrcA = 0, prevBlendDstA = 0;
		GLboolean prevDepthMask = GL_TRUE, prevColorMask[4];
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
		glGetIntegerv(GL_VIEWPORT, prevVp);
		glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
		glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
		glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
		glGetIntegerv(GL_FRONT_FACE, &prevFront);
		glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc); glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcA); glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstA);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
		glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);
		const GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST), prevBlend = glIsEnabled(GL_BLEND), prevCull = glIsEnabled(GL_CULL_FACE), prevScissor = glIsEnabled(GL_SCISSOR_TEST);
		GLfloat prevClear[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		bool ok = true;
		for (int e = 0; e < 2 && ok; e++)
		{
			Swapchain& sc = s_eye[e];
			if (!acquire(sc)) { ok = false; break; }
			glBindFramebuffer(GL_FRAMEBUFFER, sc.fbos[sc.index]);
			glViewport(0, 0, sc.width, sc.height);
			glDisable(GL_SCISSOR_TEST);
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClearDepthf(1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			float vp[16];
			hands_viewProj(s_views[e].pose, s_views[e].fov, 0.05f, 100.0f, vp);
			const float eye[3] = { s_views[e].pose.position.x, s_views[e].pose.position.y, s_views[e].pose.position.z };
			if (frozen) { drawSnapshot(e); }   // paused game: the world stays (frozen) behind the PDA
			else { room_draw(vp, model, eye, sw, sh, s_quadPose.position.y - floorY, SDL_GetTicks() / 1000.0f); }
			s_inRoomDraw = true;
			tfe_xr_draw_hands(e);
			s_inRoomDraw = false;
			release(sc);
		}
		roomFrame = ok;
		glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
		glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
		glUseProgram(prevProg);
		glBindVertexArray(prevVao);
		glBindBuffer(GL_ARRAY_BUFFER, prevBuf);
		glDepthFunc(prevDepthFunc);
		glActiveTexture(prevActive);
		glBindTexture(GL_TEXTURE_2D, prevTex);
		glFrontFace(prevFront);
		glBlendFuncSeparate(prevBlendSrc, prevBlendDst, prevBlendSrcA, prevBlendDstA);
		glDepthMask(prevDepthMask);
		glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
		if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
		if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
		if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
		if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
		glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
	}

	XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
	XrCompositionLayerProjection proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	XrCompositionLayerQuad hudQuad[2] = { { XR_TYPE_COMPOSITION_LAYER_QUAD }, { XR_TYPE_COMPOSITION_LAYER_QUAD } };
	XrCompositionLayerQuad kbdQuad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	XrCompositionLayerQuad holoQuad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader* layers[6];
	uint32_t layerCount = 0;
	if (s_frameState.shouldRender)
	{
		if (s_worldThisFrame || roomFrame)
		{
			for (int e = 0; e < 2; e++)
			{
				pv[e].pose = s_views[e].pose;
				pv[e].fov = s_views[e].fov;
				pv[e].subImage.swapchain = s_eye[e].handle;
				pv[e].subImage.imageRect.extent = { s_eye[e].width, s_eye[e].height };
			}
			proj.space = s_appSpace;
			proj.viewCount = 2;
			proj.views = pv;
			layers[layerCount++] = (const XrCompositionLayerBaseHeader*)&proj;
		}
		quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		quad.space = s_appSpace;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = s_quad.handle;
		quad.subImage.imageRect.extent = { s_quad.width, s_quad.height };
		quad.pose = activeScreenPose();
		quad.size = { activeScreenW(), activeScreenH() };
		// The automap only lives on the wrist (screen or hologram): never full screen in front of the player.
		// Wrist screen / hologram modes: the automap never shows full screen in front of the player.
		if (s_automapWas && !s_automap && s_screenHide < 6) { s_screenHide = 6; }
		s_automapWas = s_automap;
		if (pdaActive() && s_screenHide > 2) { s_screenHide = 2; }   // the PDA is up: show it (after its first frames)
		const bool screenHidden = s_screenHide > 0;
		if (s_screenHide > 0) { s_screenHide--; }
		if (!screenHidden && !mapOnWatch && !holo && !(s_automap && s_worldThisFrame && s_opt.mapMode != 2)) { layers[layerCount++] = (const XrCompositionLayerBaseHeader*)&quad; }
		if (holo)
		{
			holoQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			holoQuad.space = s_appSpace;
			holoQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			holoQuad.subImage.swapchain = s_holo.handle;
			holoQuad.subImage.imageRect.extent = { s_holo.width, s_holo.height };
			holoQuad.pose = holoPose;
			holoQuad.size = { c_holoW, c_holoW * float(s_holo.height) / float(s_holo.width) };
			layers[layerCount++] = (const XrCompositionLayerBaseHeader*)&holoQuad;
		}
		for (int i = 0; i < 2; i++)
		{
			if (!hudPanel[i]) { continue; }
			const float wM = (i == 1) ? 0.050f * s_wallPush : 0.056f;
			const float aspect = (i == 0) ? 0.63f : (s_hudRect[i][3] * s_screenH) / fmaxf(1.0f, s_hudRect[i][2] * s_screenW);
			hudQuad[i].layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			hudQuad[i].space = s_appSpace;
			hudQuad[i].eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			hudQuad[i].subImage.swapchain = s_hud[i].handle;
			hudQuad[i].subImage.imageRect.extent = { s_hud[i].width, s_hud[i].height };
			hudQuad[i].pose = hudPose[i];
			hudQuad[i].size = { wM, wM * aspect };
			layers[layerCount++] = (const XrCompositionLayerBaseHeader*)&hudQuad[i];
		}
		if (kbdLayer)
		{
			kbdQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			kbdQuad.space = s_appSpace;
			kbdQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			kbdQuad.subImage.swapchain = s_kbd.handle;
			kbdQuad.subImage.imageRect.extent = { s_kbd.width, s_kbd.height };
			kbdQuad.pose = s_kbdPose;
			kbdQuad.size = { c_kbdWidthM, c_kbdWidthM * c_kbdH / c_kbdW };
			layers[layerCount++] = (const XrCompositionLayerBaseHeader*)&kbdQuad;
		}
	}
	XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
	ei.displayTime = s_frameState.predictedDisplayTime;
	ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	ei.layerCount = layerCount;
	ei.layers = layers;
	XR_CHECK(xrEndFrame(s_session, &ei));
	s_frameBegun = false;
	s_worldLastFrame = s_worldThisFrame;
	s_hudRectValid = false;

	glBindFramebuffer(GL_FRAMEBUFFER, s_screenFbo);
	glViewport(0, 0, s_screenW, s_screenH);
}

static bool s_handsInit = false;
namespace
{
	// Copies the eye image (world only, before the hands) for the frozen PDA backdrop.
	void snapshotEye(int eye)
	{
		if (eye == 0) { s_snapTick++; if (s_snapReq > 0) { s_snapReq--; } }
		if (!(s_snapReq > 0 || s_pdaMenu || (s_snapTick & 7) == 0)) { return; }
		const Swapchain& sc = s_eye[eye];
		if (!sc.acquired) { return; }
		if (!s_snapTex[0])
		{
			s_snapW = sc.width / 2; s_snapH = sc.height / 2;
			glGenTextures(2, s_snapTex);
			glGenFramebuffers(2, s_snapFbo);
			GLint prevTex = 0, prevFbo = 0;
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
			for (int e = 0; e < 2; e++)
			{
				glBindTexture(GL_TEXTURE_2D, s_snapTex[e]);
				// glTexImage2D: the GL 3.3 glad loader leaves glTexStorage2D null.
				glTexImage2D(GL_TEXTURE_2D, 0, (GLint)s_colorFormat, s_snapW, s_snapH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_snapFbo[e]);
				glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_snapTex[e], 0);
			}
			glBindTexture(GL_TEXTURE_2D, prevTex);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFbo);
		}
		GLint prevRead = 0, prevDraw = 0;
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, sc.fbos[sc.index]);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_snapFbo[eye]);
		glBlitFramebuffer(0, 0, sc.width, sc.height, 0, 0, s_snapW, s_snapH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
		s_snapPose[eye] = s_views[eye].pose;
		s_snapFov[eye] = s_views[eye].fov;
		if (eye == 1) { s_snapValid = true; }
	}

	void quatToMat3(const XrQuaternionf& q, float m[9])   // row major, local -> world
	{
		const float x = q.x, y = q.y, z = q.z, w = q.w;
		m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - z * w);     m[2] = 2 * (x * z + y * w);
		m[3] = 2 * (x * y + z * w);     m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z - x * w);
		m[6] = 2 * (x * z - y * w);     m[7] = 2 * (y * z + x * w);     m[8] = 1 - 2 * (x * x + y * y);
	}

	// Draws the frozen eye image into the bound framebuffer, reprojected for the current head rotation.
	void drawSnapshot(int eye)
	{
		if (!s_snapProg)
		{
			const char* vs =
				"#version 300 es\n"
				"out vec2 uv;\n"
				"void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
			const char* fs =
				"#version 300 es\n"
				"precision highp float;\n"
				"in vec2 uv; out vec4 o;\n"
				"uniform sampler2D Tex; uniform mat3 M; uniform vec4 CurTan; uniform vec4 OldTan;\n"
				"void main(){\n"
				" vec3 r = vec3(mix(CurTan.x, CurTan.y, uv.x), mix(CurTan.w, CurTan.z, uv.y), -1.0);\n"
				" vec3 q = M * r;\n"
				" if (q.z > -1e-3) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
				" vec2 t = q.xy / -q.z;\n"
				" vec2 s = vec2((t.x - OldTan.x) / (OldTan.y - OldTan.x), (t.y - OldTan.w) / (OldTan.z - OldTan.w));\n"
				" vec2 e = smoothstep(vec2(0.0), vec2(0.04), s) * smoothstep(vec2(0.0), vec2(0.04), 1.0 - s);\n"
				" o = vec4(texture(Tex, s).rgb * 0.7 * e.x * e.y, 1.0);\n"
				"}\n";
			s_snapProg = glCreateProgram();
			GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
			glAttachShader(s_snapProg, v); glAttachShader(s_snapProg, f);
			glLinkProgram(s_snapProg);
			glDeleteShader(v); glDeleteShader(f);
			glGenVertexArrays(1, &s_snapVao);
		}
		float A[9], B[9], M[9];
		quatToMat3(s_snapPose[eye].orientation, A);   // old eye -> world
		quatToMat3(s_views[eye].pose.orientation, B);  // current eye -> world
		// M = A^T * B: current eye ray -> old eye frame. GL matrices are column major: upload transposed.
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
		{
			float sum = 0.0f;
			for (int k = 0; k < 3; k++) { sum += A[k * 3 + i] * B[k * 3 + j]; }
			M[i * 3 + j] = sum;
		}
		glUseProgram(s_snapProg);
		glUniformMatrix3fv(glGetUniformLocation(s_snapProg, "M"), 1, GL_TRUE, M);
		const XrFovf& c = s_views[eye].fov; const XrFovf& o = s_snapFov[eye];
		glUniform4f(glGetUniformLocation(s_snapProg, "CurTan"), tanf(c.angleLeft), tanf(c.angleRight), tanf(c.angleUp), tanf(c.angleDown));
		glUniform4f(glGetUniformLocation(s_snapProg, "OldTan"), tanf(o.angleLeft), tanf(o.angleRight), tanf(o.angleUp), tanf(o.angleDown));
		glUniform1i(glGetUniformLocation(s_snapProg, "Tex"), 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, s_snapTex[eye]);
		glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
		glDepthMask(GL_FALSE);
		const bool srgb = s_srgbWriteControl && s_colorFormat == GL_SRGB8_ALPHA8;
		if (srgb) { glEnable(GL_FRAMEBUFFER_SRGB_EXT); }
		glBindVertexArray(s_snapVao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		if (srgb) { glDisable(GL_FRAMEBUFFER_SRGB_EXT); }
		glDepthMask(GL_TRUE);
	}
}

extern "C" void tfe_xr_draw_hands(int eye)
{
	if (!s_active) { return; }
	if (!s_handsInit) { hands_init(); s_handsInit = true; }
	const double now = SDL_GetTicks() / 1000.0;
	float dt = (eye == 0) ? float(now - s_lastHandTime) : 0.0f;
	if (eye == 0) { s_lastHandTime = now; }
	if (dt > 0.1f) dt = 0.1f;
	if (!s_inRoomDraw) { snapshotEye(eye); }
	// Weapon against walls: "engolida" (default) keeps the world depth, so the part of the weapon
	// inside a wall is hidden by it, like any real object; the other mode draws hands and weapon over
	// the world (scaled toward the head near walls). The menu room has its own depth mapping.
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_TRUE);
	if (s_inRoomDraw || s_opt.wallMode != 0) { glClear(GL_DEPTH_BUFFER_BIT); }
	HandsFrame f;
	f.weaponId = s_weaponId;
	f.ammoEmpty = s_weaponAmmoEmpty;
	f.flash = s_flash;
	float light = 0.22f + 0.85f * s_ambient + 0.6f * s_weaponFlashLight + (s_headlamp ? 0.3f : 0.0f);
	f.light = fminf(1.25f, fmaxf(0.22f, light));
	f.laser = s_opt.laser != 0 && s_worldLastFrame;
	f.laserColor[0] = 0.9f; f.laserColor[1] = 0.08f; f.laserColor[2] = 0.05f;
	f.beam = false;
	if (s_holoActive)
	{
		XrPosef face, holoP;
		const XrVector3f headApp = { s_views[eye].pose.position.x, s_views[eye].pose.position.y, s_views[eye].pose.position.z };
		if (hands_watch(s_hands[0], &face, &holoP, headApp))
		{
			const XrVector3f n = rotate(face.orientation, { 0, 0, 1 });
			f.beam = true;
			f.beamFrom = { face.position.x + n.x * 0.006f, face.position.y + n.y * 0.006f, face.position.z + n.z * 0.006f };
			const float hw = c_holoW * 0.5f, hh = c_holoW * 0.5f * 640.0f / 1024.0f;
			const float cx[4] = { -hw, hw, hw, -hw }, cy[4] = { -hh, -hh, hh, hh };
			for (int i = 0; i < 4; i++)
			{
				const XrVector3f o = rotate(s_holoPose.orientation, { cx[i], cy[i], 0.0f });
				f.beamTo[i] = { s_holoPose.position.x + o.x, s_holoPose.position.y + o.y, s_holoPose.position.z + o.z };
			}
		}
	}
	f.wheel = s_wheelOpen;
	f.wheelPose = { s_wheelOri, s_wheelCenter };
	f.wheelOwned = s_ownedWeapons;
	f.pda = s_pdaValid;
	f.point = s_pdaValid && s_pdaPointHold > 0;
	f.pdaDot = false;   // the game's own cursor shows on the live frame
	f.pdaDotPos = s_pdaDotPos;
	f.pdaPose = s_pdaPose;
	f.wheelSel = s_wheelSel;
	{
		const double tnow = SDL_GetTicks() / 1000.0;
		const float left = float(s_cycShowUntil - tnow);
		f.cycAlpha = (s_wheelOpen || s_pdaValid) ? 0.0f : fminf(1.0f, fmaxf(0.0f, left / 0.3f));
		if (f.cycAlpha > 0.0f && s_cycCur >= 0)
		{
			auto step = [&](int from, int dir)
			{
				for (int k = 1; k <= 10; k++) { const int id = ((from + dir * k) % 10 + 10) % 10; if (s_ownedWeapons & (1 << id)) { return id; } }
				return from;
			};
			f.cycId[0] = step(s_cycCur, -1); f.cycId[1] = s_cycCur; f.cycId[2] = step(s_cycCur, 1);
			const float t = fminf(1.0f, float(tnow - s_cycTime) / 0.16f);
			const float e = 1.0f - (1.0f - t) * (1.0f - t);   // ease out
			f.cycShift = float(s_cycDir) * (1.0f - e);
			f.cycPulse = 1.0f - t;
		}
	}
	hands_setWeaponStyle(s_opt.weaponStyle);
	hands_setRightHand(s_opt.rightHand);
	hands_setPdaScale(s_opt.pdaScale);
	hands_draw(s_views[eye].pose, s_views[eye].fov, c_unitsPerMeter, s_hands, f, dt);
	// Comfort vignette while moving with the stick.
	if (s_vignette > 0.02f)
	{
		if (!s_vigProgram)
		{
			const char* vs = "#version 300 es\nvoid main(){ vec2 p = vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2)); gl_Position = vec4(p*2.0-1.0, 0.0, 1.0); }\n";
			const char* fs = "#version 300 es\nprecision mediump float; uniform float Strength; uniform vec2 Size; out vec4 o;\n"
				"void main(){ vec2 d = gl_FragCoord.xy / Size * 2.0 - 1.0; float r = length(d);\n"
				" float a = smoothstep(0.45, 0.95, r) * Strength; o = vec4(0.0, 0.0, 0.0, a); }\n";
			s_vigProgram = glCreateProgram();
			glAttachShader(s_vigProgram, compile(GL_VERTEX_SHADER, vs));
			glAttachShader(s_vigProgram, compile(GL_FRAGMENT_SHADER, fs));
			glLinkProgram(s_vigProgram);
			s_vigLocStrength = glGetUniformLocation(s_vigProgram, "Strength");
			s_vigLocSize = glGetUniformLocation(s_vigProgram, "Size");
			glGenVertexArrays(1, &s_vigVao);
		}
		glUseProgram(s_vigProgram);
		glUniform1f(s_vigLocStrength, fminf(1.0f, s_vignette));
		glUniform2f(s_vigLocSize, float(s_eye[eye].width), float(s_eye[eye].height));
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBindVertexArray(s_vigVao);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		glUseProgram(0);
	}
	// Gas mask, seen from inside, locked to the head (both eyes share the head frame, so the close frame
	// has the right parallax). In the game and over the paused world, never in the ship room.
	if (eye == 0) { s_maskFade += ((s_gasMask && s_opt.gasMaskView) ? 1.0f : -1.0f) * dt * 2.5f; s_maskFade = fminf(1.0f, fmaxf(0.0f, s_maskFade)); }
	if (s_maskFade > 0.0f && (!s_inRoomDraw || s_pdaMenu))
	{
		const XrPosef& p0 = s_views[0].pose; const XrPosef& p1 = s_views[1].pose;
		float R[9]; quatToMat3(p0.orientation, R);
		const float model[16] = { R[0], R[3], R[6], 0,  R[1], R[4], R[7], 0,  R[2], R[5], R[8], 0,
			0.5f * (p0.position.x + p1.position.x), 0.5f * (p0.position.y + p1.position.y), 0.5f * (p0.position.z + p1.position.z), 1 };
		float vp[16];
		hands_viewProj(s_views[eye].pose, s_views[eye].fov, 0.005f, 100.0f, vp);
		// Breathing: a slow exhale fog every 4.2 s, a little faster while running.
		const float t = float(SDL_GetTicks()) / 1000.0f;
		const float ph = fmodf(t / (s_moveAmount > 0.5f ? 3.0f : 4.2f), 1.0f);
		const float breath = ph < 0.35f ? sinf(ph / 0.35f * 3.14159265f) : 0.0f;
		mask_draw(vp, model, fminf(1.25f, 0.25f + 0.85f * s_ambient + (s_headlamp ? 0.3f : 0.0f)), s_maskFade, breath * breath, s_eye[eye].width, s_eye[eye].height);
	}
}

extern "C" void tfe_xr_set_gasmask(int on)
{
	if ((on != 0) != s_gasMask) { LOGI("gas mask %s", on ? "on" : "off"); }
	s_gasMask = on != 0;
}

extern "C" int tfe_xr_hands_need_texture(void) { return (s_active && !hands_hasTexture()) ? 1 : 0; }
extern "C" void tfe_xr_set_hand_texture(const unsigned char* rgba, int w, int h)
{
	if (!s_handsInit) { hands_init(); s_handsInit = true; }
	hands_setTexture(rgba, w, h);
}

// Cursor for SDL_GetMouseState users (Dark Forces menus read SDL's mouse state, not events).
extern "C" int tfe_xr_get_cursor(int* x, int* y, int* dx, int* dy)
{
	static int s_lastX = -1, s_lastY = -1;
	if (!s_active || !s_cursorVisible) { s_lastX = s_lastY = -1; return 0; }
	*x = s_mouseX; *y = s_mouseY;
	*dx = (s_lastX >= 0) ? s_mouseX - s_lastX : 0;
	*dy = (s_lastY >= 0) ? s_mouseY - s_lastY : 0;
	s_lastX = s_mouseX; s_lastY = s_mouseY;
	return 1;
}

extern "C" void tfe_xr_set_weapon(int weaponId, int visible, int ammoEmpty)
{
	s_weaponId = visible ? weaponId : -1;
	s_weaponAmmoEmpty = ammoEmpty != 0;
}

extern "C" void tfe_xr_on_fire(int weaponId)
{
	// Fists fired with the trigger (not by a real jab): animate the punch.
	if (weaponId == 0 && s_punchFrames <= 0) { s_punchAnim = c_punchTime; }
	static const float amp[10] = { 0.5f, 0.55f, 0.4f, 0.35f, 0.45f, 0.6f, 0.9f, 0.3f, 0.85f, 1.0f };
	static const float sec[10] = { 0.05f, 0.06f, 0.04f, 0.05f, 0.04f, 0.07f, 0.12f, 0.05f, 0.10f, 0.12f };
	const int id = (weaponId >= 0 && weaponId < 10) ? weaponId : 1;
	s_flash = 1.0f;
	haptic(1, amp[id], sec[id]);
}

extern "C" void tfe_xr_on_damage(float amount01)
{
	haptic(-1, 0.35f + 0.65f * fminf(1.0f, amount01), 0.12f);
}

extern "C" void tfe_xr_set_light(float ambient01, float flash01, int headlamp)
{
	s_ambient = ambient01;
	s_weaponFlashLight = flash01;
	s_headlamp = headlamp != 0;
}

extern "C" void tfe_xr_set_hud_rects(const float left[4], const float right[4])
{
	for (int i = 0; i < 4; i++) { s_hudRect[0][i] = left[i]; s_hudRect[1][i] = right[i]; }
	s_hudRectValid = true;
}

// Head yaw relative to the body in Dark Forces angle units (right positive), for the use action.
extern "C" int tfe_xr_head_yaw_units(void)
{
	if (!s_active) { return 0; }
	return int(-yawOf(s_headPose.orientation) * 16384.0f / 6.2831853f);
}

extern "C" int tfe_xr_take_yaw(void)
{
	const int y = s_pendingYaw;
	s_pendingYaw = 0;
	return y;
}

extern "C" int tfe_xr_physical_crouch(void) { return s_physCrouch ? 1 : 0; }
extern "C" void tfe_xr_set_eye_drop(float units) { s_eyeDrop = units > 0.0f ? units : 0.0f; }

static int s_pendingCheat = 0;
// Cheats asked from the VR menu (weapon tests): applied when the game runs again.
extern "C" void tfe_xr_request_cheat(int id) { s_pendingCheat = id; }

extern "C" int tfe_xr_take_cheat(void)
{
	if (s_pendingCheat > 0) { const int c = s_pendingCheat; s_pendingCheat = 0; return c; }
	static bool s_done[64] = {};
	float id = 0.0f;
	if (scriptValue("cheat", &id, nullptr) && id > 0.0f && id < 64.0f && !s_done[int(id)])
	{
		s_done[int(id)] = true;
		return int(id);
	}
	return 0;
}

extern "C" int tfe_xr_weapon_needs_sprite(int weaponId)
{
	return (s_active && !hands_hasWeaponSprite(weaponId)) ? 1 : 0;
}
extern "C" void tfe_xr_set_weapon_sprite(int weaponId, const unsigned char* rgba, int w, int h)
{
	if (!s_handsInit) { hands_init(); s_handsInit = true; }
	hands_setWeaponSprite(weaponId, rgba, w, h);
	LOGI("voxel weapon %d from %dx%d sprite", weaponId, w, h);
}

extern "C" int tfe_xr_weapon_needs_view(int weaponId)
{
	return (s_active && hands_needsWeaponView(weaponId)) ? 1 : 0;
}
extern "C" void tfe_xr_set_weapon_view(int weaponId, const unsigned char* rgba, int w, int h, int x0, int y0)
{
	if (!s_handsInit) { hands_init(); s_handsInit = true; }
	hands_setWeaponView(weaponId, rgba, w, h, x0, y0);
	LOGI("voxel weapon %d colored from its %dx%d first-person art", weaponId, w, h);
}

extern "C" void tfe_xr_set_owned_weapons(int mask) { s_ownedWeapons = mask | 1; }

extern "C" void tfe_xr_request_keyboard(void) { s_kbdRequest = s_kbdFrame; }
extern "C" void tfe_xr_set_automap(int on) { s_automap = on != 0; }

extern "C" double tfe_xr_time_scale(void)
{
	float v = 1.0f;
	if (s_active && scriptValue("slowmo", &v, nullptr) && v > 0.0f && v < 1.0f) { return v; }
	return 1.0;
}

extern "C" TfeVrOptions* tfe_xr_options(void) { return &s_opt; }
extern "C" void tfe_xr_default_binds(int binds[XRB_COUNT])
{
	static const int d[XRB_COUNT] = { XRA_USE, XRA_NIGHT, XRA_QSAVE, XRA_JUMP, XRA_CLEATS, XRA_QLOAD, XRA_CROUCH, XRA_NONE, XRA_MASK,
		XRA_PDA, XRA_MAPZOOM, XRA_MAP, XRA_LASTWPN, XRA_HOLSTER, XRA_LAMP, XRA_MAP, XRA_NONE };
	for (int i = 0; i < XRB_COUNT; i++) { binds[i] = d[i]; }
}
extern "C" void tfe_xr_set_pause_pda(int on)
{
	// Called every frame the in-game pause menu is up; it lapses on its own when the calls stop.
	// 2 = the mission PDA (its own frame fills the tablet face), 1 = other menus (inside the bezel).
	if (on)
	{
		if (s_pdaFrames == 0 && !s_pdaMenu) { s_screenHide = 3; }   // first frame: the menu is not on the tablet yet
		s_pdaFrames = 10;   // a few frames of slack (the menu task does not run on every VR frame)
		hands_setPdaFull(on == 2 ? 1 : (on == 3 ? 2 : 0));
		s_pdaFitPage = (on == 3);
	}
}
extern "C" int tfe_xr_room_needs_textures(void) { return (s_active && room_needsTextures()) ? 1 : 0; }
extern "C" void tfe_xr_set_room_texture(int kind, const unsigned char* rgba, int w, int h, float metersPerTexel, float offU, float offV)
{
	room_setTexture(kind, rgba, w, h, metersPerTexel, offU, offV);
}
extern "C" void tfe_xr_save_options(void) { saveOptions(); }
extern "C" void tfe_xr_set_pda_bezel(int kind, const unsigned char* rgba, int w, int h, int x0, int y0, int x1, int y1) { hands_setPdaBezel(kind, rgba, w, h, x0, y0, x1, y1); }
extern "C" void tfe_xr_set_pda_image(const unsigned char* rgba)
{
	if (!rgba) { return; }
	memcpy(s_pdaImg, rgba, sizeof(s_pdaImg));
	// Alpha = gloss for the case shader; the corner texels (rounded away on the face) = frame metal
	// for the sides and back.
	for (int i = 0; i < 320 * 200; i++) { s_pdaImg[i * 4 + 3] = 89; }
	const int corner[4] = { 0, 1, 320, 321 };
	for (int c : corner) { s_pdaImg[c * 4 + 0] = 59; s_pdaImg[c * 4 + 1] = 67; s_pdaImg[c * 4 + 2] = 79; }
	s_pdaImgNew = true;
	s_pdaImgFrames = 3;
	s_pdaCanvas[0] = 0.0f; s_pdaCanvas[1] = 0.0f; s_pdaCanvas[2] = 1.0f; s_pdaCanvas[3] = 1.0f;
	s_pdaCanvasFrames = 3;
}
extern "C" void tfe_xr_set_pda_canvas(float x0, float y0, float x1, float y1)
{
	if (x1 <= x0 || y1 <= y0) { return; }
	s_pdaCanvas[0] = x0; s_pdaCanvas[1] = y0; s_pdaCanvas[2] = x1; s_pdaCanvas[3] = y1;
	s_pdaCanvasFrames = 3;
}
extern "C" void tfe_xr_request_recenter(void) { s_recenterRequested = true; }
