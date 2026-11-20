// Android/Quest glue for The Force Engine.
// Prepares the working directory (app external files dir), extracts the
// support data bundled in the APK assets and forces a portable install.
#include <SDL.h>
#include <android/log.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TFEQuest", __VA_ARGS__)

static bool readAsset(const char* name, std::vector<char>& out)
{
	SDL_RWops* rw = SDL_RWFromFile(name, "rb");
	if (!rw) { return false; }
	Sint64 size = SDL_RWsize(rw);
	if (size < 0) { SDL_RWclose(rw); return false; }
	out.resize((size_t)size);
	size_t got = size ? SDL_RWread(rw, out.data(), 1, (size_t)size) : 0;
	SDL_RWclose(rw);
	return got == (size_t)size;
}

static void makeDirs(const std::string& path)
{
	for (size_t i = 1; i < path.size(); i++)
	{
		if (path[i] == '/')
		{
			std::string sub = path.substr(0, i);
			mkdir(sub.c_str(), 0775);
		}
	}
}

static bool writeFile(const std::string& path, const std::vector<char>& data)
{
	makeDirs(path);
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) { return false; }
	if (!data.empty()) { fwrite(data.data(), 1, data.size(), f); }
	fclose(f);
	return true;
}

extern "C" void tfe_android_prepare()
{
	const char* ext = SDL_AndroidGetExternalStoragePath();
	if (!ext) { LOGI("No external storage path: %s", SDL_GetError()); return; }
	std::string base = std::string(ext) + "/";
	makeDirs(base);
	chdir(base.c_str());
	setenv("HOME", base.c_str(), 1);
	LOGI("Working directory: %s", base.c_str());

	// Extract support data when the bundled version changes.
	std::vector<char> manifest, stamp;
	if (readAsset("tfe_manifest.txt", manifest))
	{
		std::vector<char> current;
		FILE* f = fopen((base + "tfe_assets_version.txt").c_str(), "rb");
		if (f)
		{
			char buf[256] = {};
			size_t n = fread(buf, 1, sizeof(buf) - 1, f);
			current.assign(buf, buf + n);
			fclose(f);
		}
		readAsset("tfe_assets_version.txt", stamp);
		if (current != stamp)
		{
			std::string list(manifest.begin(), manifest.end());
			size_t pos = 0, count = 0;
			while (pos < list.size())
			{
				size_t end = list.find('\n', pos);
				if (end == std::string::npos) { end = list.size(); }
				std::string name = list.substr(pos, end - pos);
				pos = end + 1;
				while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) { name.pop_back(); }
				if (name.empty()) { continue; }
				std::vector<char> data;
				if (readAsset(name.c_str(), data) && writeFile(base + name, data)) { count++; }
				else { LOGI("Failed to extract %s", name.c_str()); }
			}
			writeFile(base + "tfe_assets_version.txt", stamp);
			LOGI("Extracted %zu support files", count);
		}
	}

	// Portable install: settings, saves and logs live next to the program data.
	std::string settings = base + "settings.ini";
	if (access(settings.c_str(), F_OK) != 0)
	{
		std::vector<char> def;
		if (!readAsset("settings_quest_default.ini", def)) { def.clear(); }
		writeFile(settings, def);
	}
}

// Optional command line for testing: files/tfe_args.txt (one line, space separated).
extern "C" int tfe_android_args(int argc, char*** argv)
{
	static std::vector<std::string> s_args;
	static std::vector<char*> s_ptrs;
	// A test run can drop files/tfe_args_test.txt (used once, then deleted). Otherwise the game
	// starts from the beginning: Dark Forces with all cutscenes (LucasArts logo, opening crawl,
	// agent menu, briefings).
	FILE* f = fopen("tfe_args_test.txt", "rb");
	if (f) { remove("tfe_args_test.txt"); }
	else
	{
		f = fopen("tfe_args.txt", "rb");
		if (!f)
		{
			FILE* w = fopen("tfe_args.txt", "wb");
			if (w) { fputs("-gDARK -c1\n", w); fclose(w); }
			f = fopen("tfe_args.txt", "rb");
		}
	}
	if (!f) { return argc; }
	char line[1024] = {};
	size_t n = fread(line, 1, sizeof(line) - 1, f);
	fclose(f);
	line[n] = 0;
	s_args.clear();
	s_args.push_back((*argv && argc > 0) ? (*argv)[0] : "tfe");
	char* save = nullptr;
	for (char* tok = strtok_r(line, " \t\r\n", &save); tok; tok = strtok_r(nullptr, " \t\r\n", &save)) { s_args.push_back(tok); }
	s_ptrs.clear();
	for (auto& a : s_args) { s_ptrs.push_back((char*)a.c_str()); }
	s_ptrs.push_back(nullptr);
	*argv = s_ptrs.data();
	LOGI("tfe_args.txt: %zu args", s_args.size() - 1);
	return (int)s_args.size();
}
