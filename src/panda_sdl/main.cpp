#ifdef __XBOX_BUILD
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#include <SDL.h>
#include <glad/gl.h>
#ifdef __XBOX_ONE_BUILD
#include <glad/glad_egl.h>
#endif
#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.hpp"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"
#include "panda_sdl/frontend_sdl.hpp"

#if defined(__DEVSTORE_BUILD)
#include <SDL_filesystem.h>
#include <SDL_loadso.h>

#include <fstream>
#include <functional>
#endif

#if defined(__DEVSTORE_BUILD)
static void* s_devStoreLib = nullptr;
static constexpr int APP_VERSION = 1;
using UploadSaveFn = char* (*)(const char*, const char*, const char*);
using DownloadSaveFn = char* (*)(const char*, const char*, const char*);
using FreeCStringFn = void (*)(char*);
using GetVersionFn = char* (*)(const char*);
using DownloadUpdateFn = char* (*)(const char*);

static UploadSaveFn s_uploadFn = nullptr;
static DownloadSaveFn s_downloadFn = nullptr;
static FreeCStringFn s_freeFn = nullptr;
static GetVersionFn s_getVerFn = nullptr;
static DownloadUpdateFn s_downloadUpdFn = nullptr;

static bool AskYesNo(SDL_Window* window, const std::string& title, const std::string& body);

static void BlockingUpdateUI(SDL_Window* window, const std::function<std::string()>& doDownload);

static std::filesystem::path GetAppDataRoot(bool usePortableBuild) {
#ifdef __ANDROID__
	std::ifstream f("/proc/self/cmdline");
	std::string pkg;
	std::getline(f, pkg, '\0');
	return std::filesystem::path("/data") / "data" / pkg / "files";
#else
	char* rawPath = nullptr;
	std::filesystem::path out;
	if (!usePortableBuild) {
		rawPath = SDL_GetPrefPath(nullptr, "Alber");
		out = std::filesystem::path(rawPath);
	} else {
		rawPath = SDL_GetBasePath();
		out = std::filesystem::path(rawPath) / "Emulator Files";
	}
	SDL_free(rawPath);
	return out;
#endif
}

static std::string g_packageId;

static bool LoadDevStoreSDK() {
	if (s_devStoreLib) return true;
#if defined(_WIN32)
	constexpr const char* kDLL = "devstoreSDK.dll";
#else
	constexpr const char* kDLL = "libdevstoreSDK.dylib";
#endif

	s_devStoreLib = SDL_LoadObject(kDLL);
	if (!s_devStoreLib) return false;

	s_uploadFn = reinterpret_cast<UploadSaveFn>(SDL_LoadFunction(s_devStoreLib, "upload_save_to_server"));
	s_downloadFn = reinterpret_cast<DownloadSaveFn>(SDL_LoadFunction(s_devStoreLib, "download_save_from_server"));
	s_freeFn = reinterpret_cast<FreeCStringFn>(SDL_LoadFunction(s_devStoreLib, "free_c_string"));
	s_getVerFn = (GetVersionFn)SDL_LoadFunction(s_devStoreLib, "get_version_from_id");
	s_downloadUpdFn = (DownloadUpdateFn)SDL_LoadFunction(s_devStoreLib, "download_update_for_product");

	return (s_uploadFn && s_downloadFn && s_freeFn && s_getVerFn && s_downloadUpdFn);
}

static void DevStore_CheckForUpdates(SDL_Window* win, bool checkUpdatesFlag) {
	if (!checkUpdatesFlag || !LoadDevStoreSDK() || g_packageId.empty()) {
		printf("checkUpdatesFlag: %d\n", (int)checkUpdatesFlag);
		printf("LoadDevStoreSDK: %d\n", (int)(s_devStoreLib != nullptr));
		printf("g_packageId.empty(): %d\n", (int)g_packageId.empty());
		printf("DevStore SDK not loaded or package ID is empty.\n");
		return;
	}

	if (!s_downloadUpdFn) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DevStore SDK", "download_update_for_product not found in DevStore SDK.", win);
		return;
	}

	char* verTxt = s_getVerFn(g_packageId.c_str());
	if (!verTxt) return;

	int latest = (int)strtol(verTxt, nullptr, 0);
	s_freeFn(verTxt);
	if (latest <= APP_VERSION) return;  

	std::string body =
		"An update is available.\n\n"
		"Current version : " +
		std::to_string(APP_VERSION) + "\nLatest version  : " + std::to_string(latest) + "\n\nDo you want to download it now?";

	if (!AskYesNo(win, "Update available", body)) return;  

	BlockingUpdateUI(win, [&]() -> std::string {
		char* res = s_downloadUpdFn(g_packageId.c_str());
		if (!res) return "Error: Update download failed.";
		std::string out = res;
		s_freeFn(res);
		return out;
	});
}

static void BlockingUpdateUI(SDL_Window* win, const std::function<std::string()>& doDownload) {
	SDL_GLContext ctx = SDL_GL_GetCurrentContext();

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	ImGui_ImplSDL2_InitForOpenGL(win, ctx);
#ifdef __XBOX_ONE_BUILD
	ImGui_ImplOpenGL3_Init("#version 300 es");
#else
	ImGui_ImplOpenGL3_Init("#version 410");
#endif
	auto renderPanel = [&](const char* text) {
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(win);
		ImGui::NewFrame();
		int w, h;
		SDL_GetWindowSize(win, &w, &h);
		ImGui::SetNextWindowPos({w * 0.5f, h * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
		ImGui::SetNextWindowSize({420, 120}, ImGuiCond_Always);
		ImGui::Begin("Updater", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
		ImGui::TextWrapped("%s", text);
		ImGui::End();
		ImGui::Render();
		SDL_GL_MakeCurrent(win, ctx);
		glViewport(0, 0, w, h);
		glClearColor(0.1f, 0.1f, 0.1f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(win);
	};

	renderPanel("Updating… please wait");

	std::string result = doDownload();

	std::string finalMsg;
	bool success = (result.rfind("Error:", 0) != 0);
	if (success)
		finalMsg = "Update successful!\nThe app will now quit to apply the patch.";
	else
		finalMsg = result + "\n\nPress Enter / A to continue.";

	bool wait = true;
	while (wait) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			ImGui_ImplSDL2_ProcessEvent(&e);
			if (e.type == SDL_QUIT) std::exit(0);
			if ((e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RETURN) ||
				(e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_A))
				wait = false;
		}
		renderPanel(finalMsg.c_str());
		SDL_Delay(16);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	if (success) {
		SDL_Quit();
		std::exit(0);
	}
}

static void CallSDKAndPopup(SDL_Window* wnd, const std::string& userSecret, bool usePortableBuild, bool upload) {
	if (!LoadDevStoreSDK()) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DevStore SDK", "Unable to load devstoresdk library.", wnd);
		return;
	}

	if (g_packageId.empty()) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DevStore SDK", "Package-ID is missing.", wnd);
		return;
	}
	const char* pkgID = g_packageId.c_str();
	if (userSecret.empty()) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DevStore SDK", "DevStore secret key is empty in settings.", wnd);
		return;
	}

	std::filesystem::path saveDir = GetAppDataRoot(usePortableBuild);
	std::string saveDirStr = saveDir.string();

	char* cResult = upload ? s_uploadFn(pkgID, userSecret.c_str(), saveDirStr.c_str()) : s_downloadFn(pkgID, userSecret.c_str(), saveDirStr.c_str());

	std::string result = cResult ? cResult : "NULL result";
	if (cResult) s_freeFn(cResult);

	const bool isErr = result.rfind("Error:", 0) == 0;
	SDL_ShowSimpleMessageBox(
		isErr ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION,
		upload ? (isErr ? "Upload Error" : "Upload Success") : (isErr ? "Download Error" : "Download Success"), result.c_str(), wnd
	);
}

static bool AskYesNo(SDL_Window* window, const std::string& title, const std::string& body) {
	SDL_MessageBoxButtonData buttons[] = {{/*flags=*/0, /*id=*/0, "No"}, {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"}};
	SDL_MessageBoxData data{};
	data.flags = SDL_MESSAGEBOX_INFORMATION;
	data.window = window;
	data.title = title.c_str();
	data.message = body.c_str();
	data.numbuttons = 2;
	data.buttons = buttons;
	int button = 0;
	SDL_ShowMessageBox(&data, &button);
	return button == 1;
}

#endif

static bool s_is_durango = false;

int my_dupenv_s(char** buffer, size_t* numberOfElements, const char* varname) {
	const char* val = getenv(varname);
	if (!val) {
		if (buffer) *buffer = nullptr;
		if (numberOfElements) *numberOfElements = 0;
		return 1;
	}
	size_t len = strlen(val) + 1;
	if (numberOfElements) *numberOfElements = len;
	*buffer = (char*)malloc(len);
	if (!*buffer) return 1;
	memcpy(*buffer, val, len);
	return 0;
}
#define _dupenv_s my_dupenv_s

namespace GameLoader {
	struct InstalledGame {
		std::string title;
		std::string id;
		std::filesystem::path path;
	};
}  // namespace GameLoader

std::vector<GameLoader::InstalledGame> scanGamesInDirectory(const std::filesystem::path& dir) {
	std::vector<GameLoader::InstalledGame> games;
	if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) return games;
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		if (entry.is_regular_file()) {
			auto ext = entry.path().extension().string();
			if (ext == ".cci" || ext == ".3ds" || ext == ".cxi" || ext == ".app" || ext == ".ncch" || ext == ".elf" || ext == ".axf" ||
				ext == ".3dsx") {
				GameLoader::InstalledGame game;
				game.title = entry.path().stem().string();
				game.id = entry.path().filename().string();
				game.path = entry.path();
				games.push_back(game);
			}
		}
	}
	return games;
}

std::vector<GameLoader::InstalledGame> scanAllGames() {
	std::vector<GameLoader::InstalledGame> allGames;
	std::filesystem::path eRoot("E:/");
	{
		auto games = scanGamesInDirectory(eRoot);
		allGames.insert(allGames.end(), games.begin(), games.end());
	}
	{
		std::filesystem::path ePanda = eRoot / "PANDA3DS";
		auto games = scanGamesInDirectory(ePanda);
		allGames.insert(allGames.end(), games.begin(), games.end());
	}
	std::filesystem::path localState;
#ifdef _WIN32
	{
		char* localStateCStr = nullptr;
		size_t len = 0;
		_dupenv_s(&localStateCStr, &len, "LOCAL_STATE_PATH");
		if (localStateCStr) {
			localState = std::filesystem::path(localStateCStr);
			free(localStateCStr);
		}
	}
#else
	{
		const char* localStateCStr = getenv("LOCAL_STATE_PATH");
		if (localStateCStr) {
			localState = std::filesystem::path(localStateCStr);
		}
	}
#endif
	if (!localState.empty()) {
		auto games = scanGamesInDirectory(localState);
		allGames.insert(allGames.end(), games.begin(), games.end());
		std::filesystem::path localStatePanda = localState / "PANDA3DS";
		auto games2 = scanGamesInDirectory(localStatePanda);
		allGames.insert(allGames.end(), games2.begin(), games2.end());
	}
	return allGames;
}

static int ImGuiGameSelector(const std::vector<GameLoader::InstalledGame>& games) {
	SDL_Window* currentWindow = SDL_GL_GetCurrentWindow();
	SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
	static EmulatorConfig cfg("config.toml");
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	ImGui_ImplSDL2_InitForOpenGL(currentWindow, currentContext);
#ifdef __XBOX_ONE_BUILD
	ImGui_ImplOpenGL3_Init("#version 300 es");
#else
	ImGui_ImplOpenGL3_Init("#version 410");
#endif

	bool inSettings = false;
	int selected = 0;
	bool selectionMade = false;

	while (!selectionMade) {
		int drawableW, drawableH;
		SDL_GL_GetDrawableSize(currentWindow, &drawableW, &drawableH);
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) exit(0);
			if (!inSettings && event.type == SDL_CONTROLLERBUTTONDOWN) {
				if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP && selected > 0) selected--;
				if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN && selected < (int)games.size() - 1) selected++;
				if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) selectionMade = true;
			}
			if (!inSettings && event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == SDLK_UP && selected > 0) selected--;
				if (event.key.keysym.sym == SDLK_DOWN && selected < (int)games.size() - 1) selected++;
				if (event.key.keysym.sym == SDLK_RETURN) selectionMade = true;
			}
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(currentWindow);
		ImGui::NewFrame();
#ifdef __XBOX_ONE_BUILD
		int drawableWX, drawableHX;
		SDL_GL_GetDrawableSize(currentWindow, &drawableWX, &drawableHX);
		ImGui::GetIO().DisplaySize = ImVec2((float)drawableWX, (float)drawableHX);
#else

#endif

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

		if (!inSettings) {
			// ─── Game selector ───────────────────────────────────────────────────────
			ImGui::SetNextWindowPos(ImVec2(drawableW * 0.5f, drawableH * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_Always);
			ImGui::Begin("Select Game", nullptr, flags);

			for (int i = 0; i < (int)games.size(); i++) {
				char buf[512];
				snprintf(
					buf, sizeof(buf), "%d: %s (ID: %s PATH: %s)", i, games[i].title.c_str(), games[i].id.c_str(), games[i].path.string().c_str()
				);
				if (ImGui::Selectable(buf, selected == i)) selected = i;
			}
			ImGui::Dummy(ImVec2(0, 8));
			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, 8));
			ImGui::SetCursorPosX((800 - 120) * 0.5f);
			if (ImGui::Button("Settings", ImVec2(120, 0))) {
				inSettings = true;
				selectionMade = false;
				SDL_Event clearEvent;
				while (SDL_PollEvent(&clearEvent)) {
				}
				ImGui::SetNextWindowFocus();
			}

			ImGui::End();
		} else {
			// ─── Settings page ────────────────────────────────────────────────────────
			ImGui::SetNextWindowPos(ImVec2(drawableW * 0.5f, drawableH * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_Always);
			ImGui::Begin("Settings", nullptr, flags);

			// --- General ---
			if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("Enable Discord RPC", &cfg.discordRpcEnabled);
				ImGui::Checkbox("Use Portable Build", &cfg.usePortableBuild);
				ImGui::Checkbox("Print App Version", &cfg.printAppVersion);
				static char romPath[256];
				snprintf(romPath, sizeof(romPath), "%s", cfg.defaultRomPath.string().c_str());
				if (ImGui::InputText("Default ROM Path", romPath, sizeof(romPath))) {
					cfg.defaultRomPath = romPath;
				}
				const char* langs[] = {"ja", "en", "fr", "de", "it", "es", "zh", "ko", "nl", "pt", "ru", "tw"};
				int sysLang = (int)cfg.systemLanguage;
				if (ImGui::Combo("System Language", &sysLang, langs, IM_ARRAYSIZE(langs))) {
					cfg.systemLanguage = EmulatorConfig::languageCodeFromString(langs[sysLang]);
				}
			}

			// --- Window ---
			if (ImGui::CollapsingHeader("Window")) {
				ImGui::Checkbox("Show App Version", &cfg.windowSettings.showAppVersion);
				ImGui::Checkbox("Remember Position", &cfg.windowSettings.rememberPosition);
				ImGui::InputInt("Pos X", &cfg.windowSettings.x);
				ImGui::InputInt("Pos Y", &cfg.windowSettings.y);
				ImGui::InputInt("Width", &cfg.windowSettings.width);
				ImGui::InputInt("Height", &cfg.windowSettings.height);
			}

			// --- GPU ---
			if (ImGui::CollapsingHeader("GPU")) {
				const char* renderers[] = {"", "OpenGL", "Vulkan", "Metal", "Software", "Null"};
				int rend = (int)cfg.rendererType;
				if (rend < 0 || rend >= (IM_ARRAYSIZE(renderers))) rend = 1;
				if (ImGui::Combo("Renderer", &rend, renderers, IM_ARRAYSIZE(renderers))) {
					if (rend == 0) {
						cfg.rendererType = Renderer::typeFromString("OpenGL").value();
					} else {
						cfg.rendererType = Renderer::typeFromString(renderers[rend]).value();
					}
				}
				ImGui::Checkbox("Enable VSync", &cfg.vsyncEnabled);
				ImGui::Checkbox("Enable RenderDoc", &cfg.enableRenderdoc);
				ImGui::Checkbox("Enable Shader JIT", &cfg.shaderJitEnabled);
				ImGui::Checkbox("Use Ubershaders", &cfg.useUbershaders);
				ImGui::Checkbox("Accelerate Shaders", &cfg.accelerateShaders);
				ImGui::Checkbox("Accurate Shader Mul", &cfg.accurateShaderMul);
				ImGui::Checkbox("Force Shadergen Lights", &cfg.forceShadergenForLights);
				ImGui::InputInt("Light Shader Threshold", &cfg.lightShadergenThreshold);
			}

			// --- Audio ---
			if (ImGui::CollapsingHeader("Audio")) {
				const char* dspCores[] = {"HLE", "LLE"};
				int dsp = (int)cfg.dspType;
				if (dsp < 0 || dsp >= IM_ARRAYSIZE(dspCores)) dsp = 0;
				if (ImGui::Combo("DSP Emulation", &dsp, dspCores, IM_ARRAYSIZE(dspCores)))
					cfg.dspType = Audio::DSPCore::typeFromString(dspCores[dsp]);
				ImGui::Checkbox("Enable Audio", &cfg.audioEnabled);
				ImGui::Checkbox("Enable AAC", &cfg.aacEnabled);
				ImGui::Checkbox("Mute Audio", &cfg.audioDeviceConfig.muteAudio);
				ImGui::SliderFloat("Volume", &cfg.audioDeviceConfig.volumeRaw, 0.0f, 2.0f);
				const char* curves[] = {"cubic", "linear"};
				int curve = (int)cfg.audioDeviceConfig.volumeCurve;
				if (ImGui::Combo("Volume Curve", &curve, curves, IM_ARRAYSIZE(curves)))
					cfg.audioDeviceConfig.volumeCurve = AudioDeviceConfig::volumeCurveFromString(curves[curve]);
				ImGui::Checkbox("Print DSP Firmware", &cfg.printDSPFirmware);
			}

			// --- Battery ---
			if (ImGui::CollapsingHeader("Battery")) {
				ImGui::Checkbox("Charger Plugged", &cfg.chargerPlugged);
				ImGui::InputInt("Battery Percentage", &cfg.batteryPercentage);
			}

			// --- SD ---
			if (ImGui::CollapsingHeader("SD")) {
				ImGui::Checkbox("Use Virtual SD", &cfg.sdCardInserted);
				ImGui::Checkbox("Write Protect SD", &cfg.sdWriteProtected);
			}

			// --- UI ---
			if (ImGui::CollapsingHeader("UI")) {
				const char* themes[] = {"dark", "light"};
				int theme = (int)cfg.frontendSettings.theme;
				if (ImGui::Combo("Theme", &theme, themes, IM_ARRAYSIZE(themes)))
					cfg.frontendSettings.theme = FrontendSettings::themeFromString(themes[theme]);
				const char* icons[] = {"rpog", "custom"};
				int icon = (int)cfg.frontendSettings.icon;
				if (ImGui::Combo("Icon", &icon, icons, IM_ARRAYSIZE(icons)))
					cfg.frontendSettings.icon = FrontendSettings::iconFromString(icons[icon]);
				static char uiLang[8];
				strncpy(uiLang, cfg.frontendSettings.language.c_str(), sizeof(uiLang));
				if (ImGui::InputText("Language", uiLang, sizeof(uiLang))) cfg.frontendSettings.language = uiLang;
			}

			// --- Xbox Specific ---
#ifdef __XBOX_BUILD
			if (ImGui::CollapsingHeader("Xbox Specific")) {
				ImGui::Checkbox("Show Debug Info", &cfg.xboxSpecific.debugInfo);
				ImGui::Checkbox("Stretch Window (ignores resolution)", &cfg.xboxSpecific.stretchWindow);
#ifdef __XBOX_ONE_BUILD
				if (s_is_durango) {
					const char* glBackends[] = {"OpenGLES"};
					int gb = (int)cfg.xboxSpecific.glBackend;
					if (gb != (int)EmulatorConfig::XboxSettings::GLBackend::OpenGLES) {
						gb = 0;
					}
					if (ImGui::Combo("GL Backend", &gb, glBackends, IM_ARRAYSIZE(glBackends))) {
						cfg.xboxSpecific.glBackend = EmulatorConfig::XboxSettings::GLBackend::OpenGLES;
						SDL_ShowSimpleMessageBox(
							SDL_MESSAGEBOX_INFORMATION, "Renderer Change", "Renderer change will take effect after next app reboot.", currentWindow
						);
					}
				} else {
					const char* glBackends[] = {"DesktopGL", "OpenGLES"};
					int gb = (int)cfg.xboxSpecific.glBackend;
					if (gb < 0 || gb >= IM_ARRAYSIZE(glBackends)) {
						gb = 0;
					}
					if (ImGui::Combo("GL Backend", &gb, glBackends, IM_ARRAYSIZE(glBackends))) {
						cfg.xboxSpecific.glBackend = (EmulatorConfig::XboxSettings::GLBackend)gb;
						SDL_ShowSimpleMessageBox(
							SDL_MESSAGEBOX_INFORMATION, "Renderer Change", "Renderer change will take effect after next app reboot.", currentWindow
						);
					}
				}
#else
				const char* glBackends[] = {"DesktopGL", "OpenGLES"};
				int gb = (int)cfg.xboxSpecific.glBackend;
				if (ImGui::Combo("GL Backend", &gb, glBackends, IM_ARRAYSIZE(glBackends))) {
					cfg.xboxSpecific.glBackend = (EmulatorConfig::XboxSettings::GLBackend)gb;
					SDL_ShowSimpleMessageBox(
						SDL_MESSAGEBOX_INFORMATION, "Renderer Change", "Renderer change will take effect after next app reboot.", currentWindow
					);
				}
#endif
				const char* audioBackends[] = {"SDL", "WASAPI", "OSS"};
				int ab = (int)cfg.xboxSpecific.audioBackend;
				if (ImGui::Combo("Audio Backend", &ab, audioBackends, IM_ARRAYSIZE(audioBackends)))
					cfg.xboxSpecific.audioBackend = (EmulatorConfig::XboxSettings::AudioBackend)ab;
			}
#endif

#if defined(__DEVSTORE_BUILD)
			// --- DevStore ---
			if (ImGui::CollapsingHeader("DevStore")) {
				static char devStoreKeyBuf[256];
				snprintf(devStoreKeyBuf, sizeof(devStoreKeyBuf), "%s", cfg.devStoreSettings.secretKey.c_str());
				if (ImGui::InputText("Secret Key", devStoreKeyBuf, sizeof(devStoreKeyBuf))) {
					cfg.devStoreSettings.secretKey = devStoreKeyBuf;
				}
				ImGui::Checkbox("Enable Cloud Saves", &cfg.devStoreSettings.enableCloudSaves);
				ImGui::Checkbox("Check for Updates", &cfg.devStoreSettings.checkForUpdates);

				if (cfg.devStoreSettings.enableCloudSaves) {
					SDL_Window* win = SDL_GL_GetCurrentWindow();

					if (ImGui::Button("Upload", ImVec2(100, 0))) {
						CallSDKAndPopup(
							win, cfg.devStoreSettings.secretKey, cfg.usePortableBuild,
							/*upload=*/true
						);
					}
					ImGui::SameLine();
					if (ImGui::Button("Download", ImVec2(100, 0))) {
						CallSDKAndPopup(
							win, cfg.devStoreSettings.secretKey, cfg.usePortableBuild,
							/*upload=*/false
						);
					}
				}
			}
#endif

			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, 8));
			ImGui::SetCursorPosX((500 - 80) * 0.5f);
			if (ImGui::Button("Back", ImVec2(80, 0))) {
				cfg.save();
				inSettings = false;
			}

			ImGui::End();
		}

		ImGui::Render();
		SDL_GL_MakeCurrent(currentWindow, currentContext);
		glViewport(0, 0, drawableW, drawableH);
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(currentWindow);

		SDL_Delay(16);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	return selected;
}

static void ShowAlertWithOK(const std::string& message) {
	SDL_Window* currentWindow = SDL_GL_GetCurrentWindow();
	SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = NULL;
	ImGui_ImplSDL2_InitForOpenGL(currentWindow, currentContext);
#ifdef __XBOX_ONE_BUILD
	ImGui_ImplOpenGL3_Init("#version 300 es");
#else
	ImGui_ImplOpenGL3_Init("#version 410");
#endif
	bool done = false;
	while (!done) {
		int drawableWidth, drawableHeight;
		SDL_GL_GetDrawableSize(currentWindow, &drawableWidth, &drawableHeight);
		glViewport(0, 0, drawableWidth, drawableHeight);
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) exit(0);
			if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN) ||
				(event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A)) {
				done = true;
			}
		}
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(currentWindow);
		ImGui::NewFrame();
#ifdef __XBOX_ONE_BUILD
		int drawableWX, drawableHX;
		SDL_GL_GetDrawableSize(currentWindow, &drawableWX, &drawableHX);
		ImGui::GetIO().DisplaySize = ImVec2((float)drawableWX, (float)drawableHX);
#else

#endif
		int w, h;
		SDL_GetWindowSize(currentWindow, &w, &h);
		ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Always);
		ImGui::Begin("Alert", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
		ImGui::TextWrapped("%s", message.c_str());
		ImGui::Text("Press Enter or A to retry.");
		ImGui::End();
		ImGui::Render();
		SDL_GL_MakeCurrent(currentWindow, currentContext);
		glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(currentWindow);
		SDL_Delay(16);
	}
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
}

static void ShowCriticalAlertAndFreeze(const std::string& message) {
	SDL_Window* currentWindow = SDL_GL_GetCurrentWindow();
	SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = NULL;
	ImGui_ImplSDL2_InitForOpenGL(currentWindow, currentContext);
#ifdef __XBOX_ONE_BUILD
	ImGui_ImplOpenGL3_Init("#version 300 es");
#else
	ImGui_ImplOpenGL3_Init("#version 410");
#endif
	while (true) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) exit(0);
		}
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(currentWindow);
		ImGui::NewFrame();
#ifdef __XBOX_ONE_BUILD
		int drawableWX, drawableHX;
		SDL_GL_GetDrawableSize(currentWindow, &drawableWX, &drawableHX);
		ImGui::GetIO().DisplaySize = ImVec2((float)drawableWX, (float)drawableHX);
#else

#endif
		int w, h;
		SDL_GetWindowSize(currentWindow, &w, &h);
		ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Always);
		ImGui::Begin("Critical Error", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
		ImGui::TextWrapped("%s", message.c_str());
		ImGui::Text("Application will now freeze.");
		ImGui::End();
		ImGui::Render();
		SDL_GL_MakeCurrent(currentWindow, currentContext);
		int drawableWidth, drawableHeight;
		SDL_GL_GetDrawableSize(currentWindow, &drawableWidth, &drawableHeight);
		glViewport(0, 0, drawableWidth, drawableHeight);
		glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(currentWindow);
		SDL_Delay(16);
	}
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
}

extern "C" EXPORT int external_main(
	SDL_Window* host_window, SDL_GLContext host_context, bool is_durango, const char* package_id, int argc, char** argv
) {
	try {
		printf("Alber external_main: Started\n");
		s_is_durango = is_durango;

		if (package_id && *package_id) {
			g_packageId = package_id;
			printf("external_main: package_id set to %s\n", g_packageId.c_str());
		} else {
			printf("external_main: package_id not supplied or empty\n");
		}

		if (SDL_GL_MakeCurrent(host_window, host_context) != 0) {
			printf("external_main: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
			return -1;
		}
#ifdef __XBOX_ONE_BUILD
		if (!gladLoadEGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
			printf("external_main: Failed to load EGL: %s\n", SDL_GetError());
			return -1;
		}
		printf("external_main: EGL context is now current\n");
		if (!gladLoadGLES2Loader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
			Helpers::panic("OpenGL ES init failed");
		}
		printf("external_main: GLES context is now current\n");
#else
		if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
			throw std::runtime_error("Failed to initialize GLAD");
		}
		printf("external_main: GL context is now current\n");
#endif

#if defined(__DEVSTORE_BUILD)
		EmulatorConfig bootCfg("config.toml");
		DevStore_CheckForUpdates(host_window, bootCfg.devStoreSettings.checkForUpdates);
		printf("external_main: DevStore SDK check for updates completed\n");
#endif

		std::filesystem::path file = "";
		std::filesystem::path localStatePath = "";
		if (argc >= 2) {
			file = argv[1];
			printf("external_main: File provided: %s\n", file.generic_string().c_str());
		} else {
			printf("external_main: No file provided, will use boot window/game selector\n");
		}
		if (argc >= 3) {
			localStatePath = argv[2];
			printf("external_main: Local state path provided: %s\n", localStatePath.generic_string().c_str());
			std::string envVarName = "LOCAL_STATE_PATH";
			std::string envVarValue = localStatePath.generic_string();
#ifdef _WIN32
			if (_putenv_s(envVarName.c_str(), envVarValue.c_str()) != 0)
				printf("external_main: Failed to set environment variable %s\n", envVarName.c_str());
			else
				printf("external_main: Environment variable %s set to %s\n", envVarName.c_str(), envVarValue.c_str());
#else
			if (setenv(envVarName.c_str(), envVarValue.c_str(), 1) != 0)
				printf("external_main: Failed to set environment variable %s\n", envVarName.c_str());
			else
				printf("external_main: Environment variable %s set to %s\n", envVarName.c_str(), envVarValue.c_str());
#endif
		} else {
			printf("external_main: Local state path not provided as argument.\n");
		}

		std::string result = "";
		if (!result.empty()) {
			file = result;
		}

		while (true) {
			auto allGames = scanAllGames();
			while (allGames.empty()) {
				ShowAlertWithOK(
					"No ROM inserted!\n\n"
					"Please add ROMs (supported formats: .cci, .3ds, .cxi, .app, .ncch, .elf, .axf, .3dsx) to one of the following folders:\n"
					"E:/\n"
					"E:/PANDA3DS\n"
					"[LOCAL_STATE_PATH]\n"
					"[LOCAL_STATE_PATH]/PANDA3DS"
				);
				allGames = scanAllGames();
			}

			int selectedIndex = ImGuiGameSelector(allGames);
			auto selectedGamePath = allGames[selectedIndex].path;

			FrontendSDL app;
			if (!app.loadROM(selectedGamePath)) {
				printf("Failed to load ROM file: %s\n", selectedGamePath.string().c_str());
				return -1;
			}
			app.run();
		}
	} catch (const std::exception& e) {
		fprintf(stderr, "Exception caught in external_main: %s\n", e.what());
		ShowCriticalAlertAndFreeze(std::string("Exception: ") + e.what());
		return -1;
	} catch (...) {
		fprintf(stderr, "Unknown exception caught in external_main.\n");
		ShowCriticalAlertAndFreeze("Unknown exception caught.\n");
		return -1;
	}
}

#else

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "panda_sdl/frontend_sdl.hpp"

int main(int argc, char** argv) {
	FrontendSDL app;
	if (argc > 1) {
		auto romPath = std::filesystem::current_path() / argv[1];
		if (!app.loadROM(romPath)) printf("Failed to load ROM file: %s\n", romPath.string().c_str());
	} else {
		printf("No ROM inserted! Load a ROM by dragging and dropping it into the emulator window!\n");
	}
	app.run();
	return 0;
}

#endif