#include <TFE_Ui/ui.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/fileutil.h>

#include "imGUI/imgui.h"
#include "imGUI/imgui_impl_sdl2.h"
#include "imGUI/imgui_impl_opengl3.h"
#include "portable-file-dialogs.h"
#include "markdown.h"
#include <SDL.h>
#include <TFE_RenderBackend/renderBackend.h>
#ifdef __ANDROID__
#include <tfe_xr.h>
#endif

namespace TFE_Ui
{
const char* glsl_version = nullptr;
static s32 s_uiScale = 100;
static bool s_guiFrameActive;

bool init(void* window, void* context, s32 uiScale)
{
#ifdef __ANDROID__
	glsl_version = "#version 300 es";
#else
	glsl_version = strcmp(SDL_GetPlatform(), "Mac OS X") == 0 ? "#version 410" : "#version 130";
#endif
	
	s_uiScale = uiScale;

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// Setup Platform/Renderer bindings
	ImGui_ImplSDL2_InitForOpenGL((SDL_Window *)window, context);
	ImGui_ImplOpenGL3_Init(glsl_version);

	// Set the default font (13 px)
	// TODO: Allow scaled UI, so loading a different font for larger scales.
	if (s_uiScale <= 100)
	{
		io.Fonts->AddFontDefault();
	}
	else
	{
		char fp[TFE_MAX_PATH];
		sprintf(fp, "Fonts/DroidSansMono.ttf");
		TFE_Paths::mapSystemPath(fp);
		io.Fonts->AddFontFromFileTTF(fp, f32(13 * s_uiScale / 100));
	}
	
	TFE_Markdown::init(f32(16 * s_uiScale / 100));

	// Initialize file dialogs.
	if (!pfd::settings::available())
	{
		// TODO: Log error
		return false;
	}
	
	return true;
}

void shutdown()
{
	TFE_Markdown::shutdown();

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
}

void setUiScale(s32 scale)
{
	s_uiScale = scale;
}

s32 getUiScale()
{
	return s_uiScale;
}

void setUiInput(const void* inputEvent)
{
	const SDL_Event* sdlEvent = (SDL_Event*)inputEvent;
#ifdef __ANDROID__
	// VR: only the controller ray drives the UI pointer (the Android surface also reports its own pointer).
	if (tfe_xr_active())
	{
		if (sdlEvent->type == SDL_MOUSEMOTION && sdlEvent->motion.which != TFE_XR_MOUSE_ID) { return; }
		if ((sdlEvent->type == SDL_MOUSEBUTTONDOWN || sdlEvent->type == SDL_MOUSEBUTTONUP) && sdlEvent->button.which != TFE_XR_MOUSE_ID) { return; }
		if (sdlEvent->type == SDL_FINGERDOWN || sdlEvent->type == SDL_FINGERUP || sdlEvent->type == SDL_FINGERMOTION) { return; }
	}
#endif
	ImGui_ImplSDL2_ProcessEvent(sdlEvent);
}

void begin()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame();
#ifdef __ANDROID__
	// VR: the UI is drawn into the virtual screen texture, not the Android window.
	if (tfe_xr_active())
	{
		DisplayInfo info;
		TFE_RenderBackend::getDisplayInfo(&info);
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)info.width, (float)info.height);
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	}
#endif
	ImGui::NewFrame();
#ifdef __ANDROID__
	if (tfe_xr_active())
	{
		ImGuiIO& io = ImGui::GetIO();
		static bool s_lastDown = false;
		if (io.MouseDown[0] != s_lastDown)
		{
			s_lastDown = io.MouseDown[0];
			TFE_System::logWrite(LOG_MSG, "VRUi", "mouse %s at %.0f,%.0f display %.0fx%.0f hoverWin=%d", s_lastDown ? "down" : "up",
				io.MousePos.x, io.MousePos.y, io.DisplaySize.x, io.DisplaySize.y, ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ? 1 : 0);
		}
	}
#endif
	s_guiFrameActive = true; // No way to check if we're inside a frame using ImGui's APIs?
}

void render()
{
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	s_guiFrameActive = false;
}

bool isGuiFrameActive() { return s_guiFrameActive; }

void invalidateFontAtlas()
{
	ImGui_ImplOpenGL3_DestroyFontsTexture();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
// General TFE tries to keep paths consistently using forward slashes for readability, consistency and
// generally they work equally well on Linux, Mac and Windows.
/////////////////////////////////////////////////////////////////////////////////////////////////////////
// *However* some specific APIs (usually of the Win32 variety) require backslashes.
// So FileUtil::convertToOSPath() must be used with initial paths to convert to the correct slash type.
// This can be a bit of a waste but I'd rather have consistent paths through most of the application
// and restrict the ugliness to as small an area as possible.
/////////////////////////////////////////////////////////////////////////////////////////////////////////
FileResult openFileDialog(const char* title, const char* initPath, std::vector<std::string> const &filters/* = { "All Files", "*" }*/, bool multiSelect/* = false*/)
{
	char initPathOS[TFE_MAX_PATH] = "";
	if (initPath)
	{
		FileUtil::convertToOSPath(initPath, initPathOS);
	}

	return pfd::open_file(title, initPathOS, filters, multiSelect ? pfd::opt::multiselect : pfd::opt::none).result();
}

FileResult directorySelectDialog(const char* title, const char* initPath, bool forceInitPath/* = false*/)
{
	char initPathOS[TFE_MAX_PATH] = "";
	if (initPath)
	{
		FileUtil::convertToOSPath(initPath, initPathOS);
	}

	FileResult result;
	std::string res = pfd::select_folder(title, initPathOS).result();
	result.push_back(res);

	return result;
}

FileResult saveFileDialog(const char* title, const char* initPath, std::vector<std::string> filters/* = { "All Files", "*" }*/, bool forceOverwrite/* = false*/)
{
	char initPathOS[TFE_MAX_PATH] = "";
	if (initPath)
	{
		FileUtil::convertToOSPath(initPath, initPathOS);
	}

	FileResult result;
	std::string res = pfd::save_file(title, initPathOS, filters, forceOverwrite ? pfd::opt::force_overwrite : pfd::opt::none).result();
	result.push_back(res);

	return result;
}

}