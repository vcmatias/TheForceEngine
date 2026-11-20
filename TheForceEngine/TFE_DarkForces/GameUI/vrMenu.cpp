#include <cstring>
#include <cstdio>
#include <vector>

#include "vrMenu.h"
#include "delt.h"
#include "menu.h"
#include "uiDraw.h"
#include <TFE_Archive/archive.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Settings/settings.h>
#include <TFE_A11y/accessibility.h>
#include <TFE_Input/input.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Game/reticle.h>
#ifdef __ANDROID__
#include <tfe_xr.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <tfe_vrmenu.h>
#endif

using namespace TFE_Jedi;
using namespace TFE_Input;

namespace TFE_DarkForces
{
#ifdef __ANDROID__
	enum VrMenuPage { PAGE_GAME = 0, PAGE_VR, PAGE_VR2, PAGE_HELP, PAGE_COUNT, PAGE_PICK = PAGE_COUNT };
	static s32 s_pickSlot = 0;

	static JBool s_open = JFALSE;
	static JBool s_loaded = JFALSE;
	static s32 s_page = PAGE_VR;
	static u8 s_canvas[320 * 200];
	static u32 s_palette[256];
	static DeltFrame* s_frames = nullptr;
	static u32 s_frameCount = 0;
	static std::vector<VrmImage> s_images;
	static std::vector<u8> s_fontData;
	static VrmFont s_font = {};
	static s32 s_pressed = -1;
	static JBool s_wantSystemMenu = JFALSE;

	extern void pauseLevelSound();
	extern void resumeLevelSound();

	static void load()
	{
		if (s_loaded) { return; }
		FilePath filePath;
		if (!TFE_Paths::getFilePath("MENU.LFD", &filePath)) { return; }
		Archive* archive = Archive::getArchive(ARCHIVE_LFD, "MENU", filePath.path);
		TFE_Paths::addLocalArchive(archive);
		s_frameCount = getFramesFromAnim("config.anim", &s_frames);
		u8 pal[768] = { 0 };
		loadPaletteFromPltt("menu.pltt", pal);
		FilePath fontPath;
		if (TFE_Paths::getFilePath("font8.FONT", &fontPath))
		{
			FileStream file;
			if (file.open(&fontPath, Stream::MODE_READ))
			{
				s_fontData.resize(file.getSize());
				file.readBuffer(s_fontData.data(), (u32)s_fontData.size());
				file.close();
				vrm_parseFont(s_fontData.data(), (s32)s_fontData.size(), &s_font);
			}
		}
		TFE_Paths::removeLastArchive();

		for (u32 i = 0; i < 256; i++)
		{
			s_palette[i] = 0xffu << 24 | u32(pal[i * 3]) | (u32(pal[i * 3 + 1]) << 8) | (u32(pal[i * 3 + 2]) << 16);
		}
		s_images.resize(s_frameCount);
		for (u32 i = 0; i < s_frameCount; i++)
		{
			s_images[i] = { s_frames[i].texture.width, s_frames[i].texture.height, s_frames[i].texture.image };
		}
		s_loaded = JTRUE;
	}

	// ---- Page contents ----
	static f32 snap(f32 v, f32 mn, f32 mx, f32 step)
	{
		v = mn + floorf((v - mn) / step + 0.5f) * step;
		return v < mn ? mn : (v > mx ? mx : v);
	}
	static f32 frac(f32 v, f32 mn, f32 mx) { return (v - mn) / (mx - mn); }

	static void setRow(VrmPage& p, s32 i, const char* label, const char* value, f32 fill)
	{
		p.rows[i].label = label;
		snprintf(p.rows[i].value, sizeof(p.rows[i].value), "%s", value);
		p.rows[i].fill = fill;
	}

	static void buildPage(VrmPage& p)
	{
		memset(&p, 0, sizeof(p));
		p.hover = -1; p.pressed = s_pressed;
		char b[32];
		if (s_page == PAGE_GAME)
		{
			TFE_Settings_Sound* snd = TFE_Settings::getSoundSettings();
			TFE_Settings_A11y* a11y = TFE_Settings::getA11ySettings();
			TFE_Settings_Game* game = TFE_Settings::getGameSettings();
			p.title = "CONFIGURAÇÃO";
			p.leftButton = "OPÇÕES VR >";
			snprintf(b, 32, "%d%%", s32(snd->masterVolume * 100.0f + 0.5f));          setRow(p, 0, "VOLUME GERAL", b, snd->masterVolume);
			snprintf(b, 32, "%d%%", s32(snd->soundFxVolume * 100.0f + 0.5f));         setRow(p, 1, "JOGO: EFEITOS", b, snd->soundFxVolume);
			snprintf(b, 32, "%d%%", s32(snd->musicVolume * 100.0f + 0.5f));           setRow(p, 2, "MÚSICA", b, snd->musicVolume);
			snprintf(b, 32, "%d%%", s32(snd->cutsceneSoundFxVolume * 100.0f + 0.5f)); setRow(p, 3, "CENAS: EFEITOS", b, snd->cutsceneSoundFxVolume);
			snprintf(b, 32, "%d%%", s32(snd->cutsceneMusicVolume * 100.0f + 0.5f));   setRow(p, 4, "MÚSICA", b, snd->cutsceneMusicVolume);
			setRow(p, 5, "LEGENDAS", (a11y->showCutsceneSubtitles || a11y->showGameplaySubtitles) ? "SIM" : "NÃO", -1.0f);
			setRow(p, 6, "AVISO DE SEGREDO", game->df_showSecretFoundMsg ? "SIM" : "NÃO", -1.0f);
			setRow(p, 7, "SEGREDOS NO PDA", game->df_showSecretCount ? "CONTAGEM" : "PORCENTAGEM", -1.0f);
			setRow(p, 8, "OPÇÕES DO MOTOR", "ABRIR", -1.0f);
			setRow(p, 9, "TESTE: ARMAS E ITENS", "DAR TODOS", -1.0f);
			setRow(p, 10, "TESTE: INVENCÍVEL", "LIGAR/DESLIGAR", -1.0f);
			p.rowCount = 11;
		}
		else if (s_page == PAGE_VR2)
		{
			TfeVrOptions* o = tfe_xr_options();
			p.title = "MÃOS, PDA E MÁSCARA";
			p.leftButton = "CONTROLES >";
			setRow(p, 0, "MÃO DIREITA NA ARMA", o->rightHand ? "SIM" : "NÃO", -1.0f);
			snprintf(b, 32, "%d%%", s32(o->pdaScale * 100.0f + 0.5f)); setRow(p, 1, "TAMANHO DO PDA", b, frac(o->pdaScale, 0.6f, 1.6f));
			setRow(p, 2, "MÁSCARA DE GÁS VISÍVEL", o->gasMaskView ? "SIM" : "NÃO", -1.0f);
			setRow(p, 3, "ANDAR NA DIREÇÃO", o->walkDir == 1 ? "CONTROLE ESQ." : (o->walkDir == 2 ? "CONTROLE DIR." : "DA CABEÇA"), -1.0f);
			setRow(p, 4, "MOSTRAR FPS", TFE_Settings::getGraphicsSettings()->showFps ? "SIM" : "NÃO", -1.0f);
			setRow(p, 5, "IMPRECISÃO DAS ARMAS", o->weaponSpread ? "ORIGINAL" : "REDUZIDA", -1.0f);
			setRow(p, 6, "IDIOMA DO JOGO", TFE_Settings::getA11ySettings()->language == "pt" ? "PORTUGUÊS" : "INGLÊS", -1.0f);
			setRow(p, 7, "OBJETOS EM VOXEL", o->voxels ? "SIM" : "NÃO", -1.0f);
			p.rowCount = 8;
		}
		else
		{
			TfeVrOptions* o = tfe_xr_options();
			p.title = "CONFIGURAÇÃO VR";
			p.leftButton = "MÃOS E PDA >";
			setRow(p, 0, "GIRO", o->snapTurn ? "EM DEGRAUS" : "SUAVE", -1.0f);
			if (o->snapTurn) { snprintf(b, 32, "%d GRAUS", s32(o->snapAngle + 0.5f)); setRow(p, 1, "ÂNGULO DO GIRO", b, frac(o->snapAngle, 15.0f, 90.0f)); }
			else { snprintf(b, 32, "%.1fX", o->smoothSpeed); setRow(p, 1, "VELOCIDADE DO GIRO", b, frac(o->smoothSpeed, 0.3f, 2.0f)); }
			snprintf(b, 32, "%d%%", s32(o->comfortVignette * 100.0f + 0.5f)); setRow(p, 2, "VINHETA AO ANDAR", b, o->comfortVignette);
			setRow(p, 3, "CORRER", o->runMode ? "LIGA ATÉ PARAR" : "SEGURAR", -1.0f);
			setRow(p, 4, "AGACHAR DE VERDADE", o->physicalCrouch ? "SIM" : "NÃO", -1.0f);
			{ static const char* c_styles[5] = { "ALTA RESOLUÇÃO", "VOXEL", "VOXEL FINO", "BLENDER", "DFMOD" }; setRow(p, 5, "ARMAS", c_styles[(o->weaponStyle >= 0 && o->weaponStyle < 5) ? o->weaponStyle : 0], -1.0f); }
			snprintf(b, 32, "%+d CM", s32(floorf(o->heightOffset * 100.0f + 0.5f))); setRow(p, 6, "AJUSTE DE ALTURA", b, frac(o->heightOffset, -0.3f, 0.8f));
			setRow(p, 7, "HUD NO RELÓGIO", o->wristHud ? "SIM" : "NÃO", -1.0f);
			setRow(p, 8, "MIRA LASER", o->laser ? "SIM" : "NÃO", -1.0f);
			snprintf(b, 32, "%.1f M", o->screenDistance); setRow(p, 9, "DISTÂNCIA DA TELA", b, frac(o->screenDistance, 0.8f, 3.0f));
			if (o->haptics) { snprintf(b, 32, "%d%%", s32(o->hapticStrength * 100.0f + 0.5f)); }
			else { snprintf(b, 32, "DESLIGADA"); }
			setRow(p, 10, "VIBRAÇÃO", b, o->haptics ? o->hapticStrength : 0.0f);
			setRow(p, 11, "MENUS NA NAVE", o->menuRoom ? "SIM" : "NÃO", -1.0f);
			p.rowCount = 12;
		}
	}

	static void applySound()
	{
		TFE_Settings_Sound* snd = TFE_Settings::getSoundSettings();
		TFE_Audio::setVolume(snd->soundFxVolume * snd->masterVolume);
		TFE_MidiPlayer::setVolume(snd->musicVolume * snd->masterVolume);
	}

	// Click on a row: choices toggle, sliders take the position of the click (step snapped).
	static void activateRow(s32 row, f32 pos)
	{
		if (s_page == PAGE_GAME)
		{
			TFE_Settings_Sound* snd = TFE_Settings::getSoundSettings();
			TFE_Settings_A11y* a11y = TFE_Settings::getA11ySettings();
			TFE_Settings_Game* game = TFE_Settings::getGameSettings();
			f32* vol[5] = { &snd->masterVolume, &snd->soundFxVolume, &snd->musicVolume, &snd->cutsceneSoundFxVolume, &snd->cutsceneMusicVolume };
			if (row >= 0 && row < 5) { *vol[row] = snap(pos, 0.0f, 1.0f, 0.05f); applySound(); }
			else if (row == 5)
			{
				const bool on = !(a11y->showCutsceneSubtitles || a11y->showGameplaySubtitles);
				a11y->showCutsceneSubtitles = on; a11y->showGameplaySubtitles = on;
			}
			else if (row == 6) { game->df_showSecretFoundMsg = !game->df_showSecretFoundMsg; }
			else if (row == 7) { game->df_showSecretCount = !game->df_showSecretCount; }
			else if (row == 8) { s_wantSystemMenu = JTRUE; s_open = JFALSE; }
			else if (row == 9) { tfe_xr_request_cheat(27); s_open = JFALSE; }   // LAMAXOUT: all weapons, items (gas mask, goggles...) and full ammo
			else if (row == 10) { tfe_xr_request_cheat(5); s_open = JFALSE; }   // LAIMLAME: invincibility
			return;
		}
		if (s_page == PAGE_VR2)
		{
			TfeVrOptions* o = tfe_xr_options();
			if (row == 0) { o->rightHand = !o->rightHand; }
			else if (row == 1) { o->pdaScale = snap(0.6f + pos * 1.0f, 0.6f, 1.6f, 0.05f); }
			else if (row == 2) { o->gasMaskView = !o->gasMaskView; }
			else if (row == 3) { o->walkDir = (o->walkDir + 1) % 3; }
			else if (row == 4)
			{
				TFE_Settings_Graphics* g = TFE_Settings::getGraphicsSettings();
				g->showFps = !g->showFps;
				TFE_Settings::writeToDisk();
			}
			else if (row == 5) { o->weaponSpread = !o->weaponSpread; }   // takes effect on the next mission
			else if (row == 6)
			{
				// Captions switch at once; HUD texts and font on the next mission or game start.
				TFE_Settings_A11y* a = TFE_Settings::getA11ySettings();
				a->language = (a->language == "pt") ? "en" : "pt";
				TFE_Settings::writeToDisk();
				TFE_A11Y::refreshFiles();
			}
			else if (row == 7) { o->voxels = !o->voxels; }   // off: at once; on: models are built at the next level load
			tfe_xr_save_options();
			return;
		}
		TfeVrOptions* o = tfe_xr_options();
		switch (row)
		{
		case 0: o->snapTurn = !o->snapTurn; break;
		case 1:
			if (o->snapTurn) { o->snapAngle = snap(15.0f + pos * 75.0f, 15.0f, 90.0f, 15.0f); }
			else { o->smoothSpeed = snap(0.3f + pos * 1.7f, 0.3f, 2.0f, 0.1f); }
			break;
		case 2: o->comfortVignette = snap(pos, 0.0f, 1.0f, 0.1f); break;
		case 3: o->runMode = !o->runMode; break;
		case 4: o->physicalCrouch = !o->physicalCrouch; break;
		case 5: o->weaponStyle = (o->weaponStyle + 1) % 5; break;
		case 6: o->heightOffset = snap(-0.3f + pos * 1.1f, -0.3f, 0.8f, 0.05f); break;
		case 7: o->wristHud = !o->wristHud; break;
		case 8: o->laser = !o->laser; break;
		case 9: o->screenDistance = snap(0.8f + pos * 2.2f, 0.8f, 3.0f, 0.1f); break;
		case 10:
		{
			const f32 v = snap(pos, 0.0f, 1.0f, 0.1f);
			o->haptics = v > 0.05f ? 1 : 0;
			if (o->haptics) { o->hapticStrength = v; }
		} break;
		case 11: o->menuRoom = !o->menuRoom; break;
		}
		tfe_xr_save_options();
	}

	// Mouse (driven by the controller ray) to the 320x200 canvas.
	static void cursorPos(s32* cx, s32* cy)
	{
		DisplayInfo display;
		TFE_RenderBackend::getDisplayInfo(&display);
		u32 w, h;
		vfb_getResolution(&w, &h);
		s32 mx, my;
		TFE_Input::getMousePos(&mx, &my);
		const f32 vx = f32(mx) * f32(w) / f32(max(1u, display.width));
		const f32 vy = f32(my) * f32(h) / f32(max(1u, display.height));
		if (w == 320 && h == 200) { *cx = s32(vx); *cy = s32(vy); return; }
		ScreenRect* uiRect = vfb_getScreenRect(VFB_RECT_UI);
		const f32 virtualWidth = 320.0f * fixed16ToFloat(vfb_getXScale());
		const f32 offset = max(0.0f, (f32(uiRect->right - uiRect->left + 1) - virtualWidth) * 0.5f);
		*cx = s32((vx - offset) * 320.0f / virtualWidth);
		*cy = s32(vy * 200.0f / f32(h));
	}
#endif

	void vrMenu_open()
	{
#ifdef __ANDROID__
		load();
		if (!s_loaded) { return; }
		reticle_enable(false);
		pauseLevelSound();
		s_open = JTRUE;
		s_page = PAGE_GAME;
		s_pressed = -1;
		s_wantSystemMenu = JFALSE;
		TFE_Input::endFrame();
#endif
	}

	// Pages drawn without the configuration frame (the controls help): the tablet frames them itself.
	JBool vrMenu_isFrameless() { return s_open && (s_page == PAGE_HELP || s_page == PAGE_PICK); }

	JBool vrMenu_isOpen()
	{
#ifdef __ANDROID__
		return s_open;
#else
		return JFALSE;
#endif
	}

	JBool vrMenu_update()
	{
#ifdef __ANDROID__
		if (!s_open) { return JFALSE; }
		if (inputMapping_getActionState(IADF_MENU_TOGGLE) == STATE_PRESSED || TFE_Input::keyPressed(KEY_ESCAPE))
		{
			s_open = JFALSE;
		}
		if (TFE_Jedi::renderer_getType() != RENDERER_SOFTWARE)
		{
			TFE_Jedi::renderer_setType(RENDERER_SOFTWARE);
			TFE_Jedi::renderer_setLimits();
		}

		s32 cx, cy;
		cursorPos(&cx, &cy);
		VrmPage page;
		buildPage(page);
		f32 barPos = 0.0f;
		const s32 hit = (s_page == PAGE_HELP) ? vrm_hitHelp(cx, cy) : (s_page == PAGE_PICK) ? vrm_hitPick(cx, cy) : vrm_hitConfig(cx, cy, page, &barPos);
		page.hover = hit;

		if (s_open && TFE_Input::mousePressed(MBUTTON_LEFT)) { s_pressed = hit; }
		if (s_open && s_pressed >= 0 && TFE_Input::mouseDown(MBUTTON_LEFT) && s_page != PAGE_HELP && s_page != PAGE_PICK && s_pressed < VRM_ROWS && hit == s_pressed && page.rows[hit].fill >= 0.0f)
		{
			// Dragging a slider.
			activateRow(hit, barPos);
		}
		if (s_open && s_pressed >= 0 && !TFE_Input::mouseDown(MBUTTON_LEFT))
		{
			if (hit == s_pressed)
			{
				if (s_page == PAGE_HELP)
					{
						if (hit >= VRM_SLOT_BASE && hit < VRM_SLOT_BASE + XRB_COUNT) { s_pickSlot = hit - VRM_SLOT_BASE; s_page = PAGE_PICK; }
						else if (hit == VRM_BTN_LEFT) { tfe_xr_default_binds(tfe_xr_options()->binds); tfe_xr_save_options(); }
						else if (hit == VRM_BTN_OK) { s_page = PAGE_GAME; }
					}
					else if (s_page == PAGE_PICK)
					{
						if (hit >= 0 && hit < XRA_COUNT) { tfe_xr_options()->binds[s_pickSlot] = hit; tfe_xr_save_options(); s_page = PAGE_HELP; }
						else if (hit == VRM_BTN_OK) { s_page = PAGE_HELP; }
					}
				else if (hit == VRM_BTN_OK) { s_open = JFALSE; }
				else if (hit == VRM_BTN_LEFT) { s_page = (s_page + 1) % PAGE_COUNT; }
				else if (hit < VRM_ROWS && page.rows[hit].fill < 0.0f) { activateRow(hit, barPos); }
			}
			s_pressed = -1;
		}

		if (!s_open)
		{
			TFE_Settings::writeToDisk();
			reticle_enable(true);
			resumeLevelSound();
			TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
			if (graphics->rendererIndex != RENDERER_SOFTWARE)
			{
				TFE_Jedi::renderer_setType(RendererType(graphics->rendererIndex));
				TFE_Jedi::renderer_setLimits();
			}
			return s_wantSystemMenu;
		}

		buildPage(page);
		page.hover = hit;
		memset(s_canvas, 0, sizeof(s_canvas));
		if (s_page == PAGE_HELP) { vrm_drawHelp(s_canvas, s_images.data(), (s32)s_images.size(), s_font, hit, tfe_xr_options()->binds); }
		else if (s_page == PAGE_PICK) { vrm_drawPick(s_canvas, s_font, s_pickSlot, tfe_xr_options()->binds[s_pickSlot], hit); }
		else { vrm_drawConfig(s_canvas, s_images.data(), (s32)s_images.size(), s_font, page); }
		// The game's own mouse cursor (the VR cursor dot was removed): drawn last, over the page.
		if (s_cursor.texture.image) { blitDeltaFrame(&s_cursor, cx, cy, s_canvas); }
		vfb_setPalette(s_palette);
		menu_blitToScreen(s_canvas, JFALSE, JFALSE);
		vfb_swap();
#endif
		return JFALSE;
	}

	// PDA bezel: the mission PDA frame (MENU.LFD pda.anim background, menu palette) as the tablet face,
	// with its inner panel left dark under the screen.
	// The two tablet faces, as 320x200 canvases for tfe_xr_set_pda_bezel: 0 = the configuration screen
	// frame (settings and pause menus), 1 = the mission PDA frame (pda.anim). The windows are darkened
	// (the screen layer lies over them) and texel (0, 0), cut away by the rounded corner, holds the
	// frame metal used for the case sides and back.
#ifdef __ANDROID__
	static void pdaFace(s32 kind, const DeltFrame& f, s32 wx0, s32 wy0, s32 wx1, s32 wy1, JBool labels)
	{
		const s32 w = f.texture.width, h = f.texture.height;
		if (w <= 0 || h <= 0 || !f.texture.image) { return; }
		const u8 metal[3] = { 59, 67, 79 };
		std::vector<u8> rgba(320 * 200 * 4);
		for (s32 i = 0; i < 320 * 200; i++) { u8* o = &rgba[size_t(i) * 4]; o[0] = metal[0]; o[1] = metal[1]; o[2] = metal[2]; o[3] = 90; }
		for (s32 y = 0; y < h; y++)
		{
			for (s32 x = 0; x < w; x++)
			{
				const s32 cx = x + f.offsetX, cy = y + f.offsetY;
				if (cx < 0 || cy < 0 || cx >= 320 || cy >= 200) { continue; }
				const u8 idx = f.texture.image[y * w + x];
				if (idx == 0) { continue; }   // transparent: frame metal
				const u32 c = s_palette[idx];
				u8 r = u8(c & 255), g = u8((c >> 8) & 255), bl = u8((c >> 16) & 255);
				if (labels && cy > wy1 && g > 110 && r < 90 && bl < 90) { r = 6; g = 34; bl = 6; }   // button labels
				u8* o = &rgba[(size_t(cy) * 320 + cx) * 4];
				o[0] = r; o[1] = g; o[2] = bl;
			}
		}
		for (s32 y = wy0; y <= wy1; y++) for (s32 x = wx0; x <= wx1; x++) { u8* o = &rgba[(size_t(y) * 320 + x) * 4]; o[0] = 8; o[1] = 8; o[2] = 10; }   // under the screen
		rgba[0] = metal[0]; rgba[1] = metal[1]; rgba[2] = metal[2];
		tfe_xr_set_pda_bezel(kind, rgba.data(), 320, 200, wx0, wy0, wx1, wy1);
	}
#endif

	void vrMenu_pdaBezel()
	{
#ifdef __ANDROID__
		if (tfe_xr_active() && !tfe_xr_options()->fpsDefaultDone)
		{
			// 0.30: the FPS counter is off by default (it can be turned on in the VR menu).
			TFE_Settings::getGraphicsSettings()->showFps = false;
			TFE_Settings::writeToDisk();
			tfe_xr_options()->fpsDefaultDone = 1;
			tfe_xr_save_options();
		}
		static JBool s_doneMenu = JFALSE, s_doneMission = JFALSE;
		static s32 s_tries = 0;
		if ((s_doneMenu && s_doneMission) || !tfe_xr_active() || s_tries > 600) { return; }
		s_tries++;
		load();   // the menu palette and the configuration frame
		if (!s_loaded) { return; }
		if (!s_doneMenu && s_frameCount >= 1)
		{
			s_doneMenu = JTRUE;
			pdaFace(0, s_frames[0], 53, 13, 268, 167, JTRUE);
		}
		if (!s_doneMission)
		{
			FilePath filePath;
			if (!TFE_Paths::getFilePath("MENU.LFD", &filePath)) { return; }
			Archive* archive = Archive::getArchive(ARCHIVE_LFD, "MENU", filePath.path);
			TFE_Paths::addLocalArchive(archive);
			DeltFrame* frames = nullptr;
			const u32 count = getFramesFromAnim("pda.anim", &frames);
			TFE_Paths::removeLastArchive();
			if (count < 1 || !frames) { return; }
			s_doneMission = JTRUE;
			pdaFace(1, frames[0], 23, 11, 304, 166, JFALSE);
		}
#endif
	}

	// The ship room around the menus is dressed with the game's own textures (Imperial base set),
	// read from the player's game files with the engine's loaders and the SECBASE palette.
	void vrMenu_roomTextures()
	{
		if (!tfe_xr_active() || !tfe_xr_room_needs_textures()) { return; }
		static s32 s_tries = 0;
		if (s_tries > 3) { return; }
		s_tries++;
		FilePath palPath;
		u8 pal[768];
		if (!TFE_Paths::getFilePath("SECBASE.PAL", &palPath) || !FileStream::readContents(&palPath, pal, 768)) { return; }
		struct Slot { s32 kind; const char* name; f32 mpt; f32 offU, offV; };
		const f32 df = 0.0366f;   // meters per texel at Dark Forces scale (8 texels per unit)
		const Slot slots[] =
		{
			{ 0, "IPSQAR2.BM", df, 0.0f, 0.0f },
			{ 1, "IWSECB2.BM", df, 0.0f, 0.0f },
			{ 2, "ZPLIT08.BM", df, 0.0f, 0.0f },
			{ 3, "ZWSTRIP1.BM", 0.006f, 0.0f, 0.0f },
			{ 5, "IWLOWCON.BM", df * 0.5f, 0.0f, 0.0f },
			{ 6, "IWPANEL3.BM", 0.0048f, -0.307f, 0.097f },   // desk keys: rows of buttons, one tile deep (flipped: top row at the far edge)
			{ 8, "IWSTRIP.BM", df * 0.5f, 0.0f, 0.0f },
			{ 10, "IWPANEL1.BM", 1.17f / 64.0f, -0.85f, 0.0f },
		};
		std::vector<u8> rgba;
		for (const Slot& sl : slots)
		{
			TextureData* tex = bitmap_load(sl.name, 1, POOL_GAME);
			if (!tex || !tex->image) { continue; }
			const s32 w = tex->width, h = tex->height;
			rgba.resize(size_t(w) * h * 4);
			for (s32 x = 0; x < w; x++) for (s32 y = 0; y < h; y++)
			{
				const u8 idx = tex->image[x * h + y];
				const s32 row = (sl.kind == 6) ? (h - 1 - y) : y;
				u8* o = &rgba[(size_t(row) * w + x) * 4];
				o[0] = u8(pal[idx * 3] * 255 / 63); o[1] = u8(pal[idx * 3 + 1] * 255 / 63); o[2] = u8(pal[idx * 3 + 2] * 255 / 63); o[3] = 255;
			}
			tfe_xr_set_room_texture(sl.kind, rgba.data(), w, h, sl.mpt, sl.offU, sl.offV);
		}
	}
}
